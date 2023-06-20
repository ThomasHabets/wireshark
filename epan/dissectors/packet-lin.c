/* packet-lin.c
 *
 * LIN dissector.
 * By Dr. Lars Voelker <lars.voelker@technica-engineering.de>
 * Copyright 2021-2021 Dr. Lars Voelker
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <epan/packet.h>
#include <epan/decode_as.h>
#include <epan/prefs.h>
#include <wiretap/wtap.h>
#include <epan/expert.h>
#include <epan/uat.h>

#include <packet-lin.h>

 /*
  * Dissector for the Local Interconnect Network (LIN) bus.
  *
  * see ISO 17987 or search for "LIN Specification 2.2a" online.
  */

#define LIN_NAME                             "LIN"
#define LIN_NAME_LONG                        "LIN Protocol"
#define LIN_NAME_FILTER                      "lin"

static heur_dissector_list_t                 heur_subdissector_list;
static heur_dtbl_entry_t                    *heur_dtbl_entry;

static int proto_lin = -1;

static dissector_handle_t lin_handle;

/* header field */
static int hf_lin_msg_format_rev = -1;
static int hf_lin_reserved1 = -1;
static int hf_lin_payload_length = -1;
static int hf_lin_message_type = -1;
static int hf_lin_checksum_type = -1;
static int hf_lin_pid = -1;
static int hf_lin_id = -1;
static int hf_lin_parity = -1;
static int hf_lin_checksum = -1;
static int hf_lin_err_errors = -1;
static int hf_lin_err_no_slave_response = -1;
static int hf_lin_err_framing = -1;
static int hf_lin_err_parity = -1;
static int hf_lin_err_checksum = -1;
static int hf_lin_err_invalidid = -1;
static int hf_lin_err_overflow = -1;
static int hf_lin_event_id = -1;

static gint ett_lin = -1;
static gint ett_lin_pid = -1;
static gint ett_errors = -1;

static int * const error_fields[] = {
    &hf_lin_err_overflow,
    &hf_lin_err_invalidid,
    &hf_lin_err_checksum,
    &hf_lin_err_parity,
    &hf_lin_err_framing,
    &hf_lin_err_no_slave_response,
    NULL
};

static dissector_table_t subdissector_table;

#define LIN_MSG_TYPE_FRAME 0
#define LIN_MSG_TYPE_EVENT 3

static const value_string lin_msg_type_names[] = {
    { LIN_MSG_TYPE_FRAME, "Frame" },
    { LIN_MSG_TYPE_EVENT, "Event" },
    {0, NULL}
};

#define LIN_CHKSUM_TYPE_UNKN_ERR 0
#define LIN_CHKSUM_TYPE_CLASSIC  1
#define LIN_CHKSUM_TYPE_ENHANCED 2
#define LIN_CHKSUM_TYPE_UNDEF    3

static const value_string lin_checksum_type_names[] = {
    { LIN_CHKSUM_TYPE_UNKN_ERR, "Unknown/Error" },
    { LIN_CHKSUM_TYPE_CLASSIC, "Classic" },
    { LIN_CHKSUM_TYPE_ENHANCED, "Enhanced" },
    { LIN_CHKSUM_TYPE_UNDEF, "Undefined" },
    {0, NULL}
};

#define LIN_EVENT_TYPE_GO_TO_SLEEP_EVENT_BY_GO_TO_SLEEP 0xB0B00001
#define LIN_EVENT_TYPE_GO_TO_SLEEP_EVENT_BY_INACTIVITY  0xB0B00002
#define LIN_EVENT_TYPE_WAKE_UP_BY_WAKE_UP_SIGNAL        0xB0B00004

static const value_string lin_event_type_names[] = {
    { LIN_EVENT_TYPE_GO_TO_SLEEP_EVENT_BY_GO_TO_SLEEP, "Go-to-Sleep event by Go-to-Sleep frame" },
    { LIN_EVENT_TYPE_GO_TO_SLEEP_EVENT_BY_INACTIVITY,  "Go-to-Sleep event by Inactivity for more than 4s" },
    { LIN_EVENT_TYPE_WAKE_UP_BY_WAKE_UP_SIGNAL,        "Wake-up event by Wake-up signal" },
    {0, NULL}
};

void proto_reg_handoff_lin(void);
void proto_register_lin(void);

/********* UATs *********/

/* Interface Config UAT */
typedef struct _interface_config {
    guint     interface_id;
    gchar    *interface_name;
    guint     bus_id;
} interface_config_t;

#define DATAFILE_LIN_INTERFACE_MAPPING "LIN_interface_mapping"

static GHashTable *data_lin_interfaces_by_id = NULL;
static GHashTable *data_lin_interfaces_by_name = NULL;
static interface_config_t* interface_configs = NULL;
static guint interface_config_num = 0;

UAT_HEX_CB_DEF(interface_configs, interface_id, interface_config_t)
UAT_CSTRING_CB_DEF(interface_configs, interface_name, interface_config_t)
UAT_HEX_CB_DEF(interface_configs, bus_id, interface_config_t)

static void *
copy_interface_config_cb(void *n, const void *o, size_t size _U_) {
    interface_config_t *new_rec = (interface_config_t *)n;
    const interface_config_t *old_rec = (const interface_config_t *)o;

    new_rec->interface_id = old_rec->interface_id;
    new_rec->interface_name = g_strdup(old_rec->interface_name);
    new_rec->bus_id = old_rec->bus_id;
    return new_rec;
}

static gboolean
update_interface_config(void *r, char **err) {
    interface_config_t *rec = (interface_config_t *)r;

    if (rec->interface_id > 0xffffffff) {
        *err = ws_strdup_printf("We currently only support 32 bit identifiers (ID: %i  Name: %s)",
                               rec->interface_id, rec->interface_name);
        return FALSE;
    }

    if (rec->bus_id > 0xffff) {
        *err = ws_strdup_printf("We currently only support 16 bit bus identifiers (ID: %i  Name: %s  Bus-ID: %i)",
                                rec->interface_id, rec->interface_name, rec->bus_id);
        return FALSE;
    }

    return TRUE;
}

static void
free_interface_config_cb(void *r) {
    interface_config_t *rec = (interface_config_t *)r;
    /* freeing result of g_strdup */
    g_free(rec->interface_name);
    rec->interface_name = NULL;
}

static interface_config_t *
ht_lookup_interface_config_by_id(unsigned int identifier) {
    interface_config_t *tmp = NULL;
    unsigned int       *id = NULL;

    if (interface_configs == NULL) {
        return NULL;
    }

    id = wmem_new(wmem_epan_scope(), unsigned int);
    *id = (unsigned int)identifier;
    tmp = (interface_config_t *)g_hash_table_lookup(data_lin_interfaces_by_id, id);
    wmem_free(wmem_epan_scope(), id);

    return tmp;
}

static interface_config_t *
ht_lookup_interface_config_by_name(const gchar *name) {
    interface_config_t *tmp = NULL;
    gchar              *key = NULL;

    if (interface_configs == NULL) {
        return NULL;
    }

    key = wmem_strdup(wmem_epan_scope(), name);
    tmp = (interface_config_t *)g_hash_table_lookup(data_lin_interfaces_by_name, key);
    wmem_free(wmem_epan_scope(), key);

    return tmp;
}

static void
lin_free_key(gpointer key) {
    wmem_free(wmem_epan_scope(), key);
}

static void
post_update_lin_interfaces_cb(void) {
    guint  i;
    int   *key_id = NULL;
    gchar *key_name = NULL;

    /* destroy old hash tables, if they exist */
    if (data_lin_interfaces_by_id) {
        g_hash_table_destroy(data_lin_interfaces_by_id);
        data_lin_interfaces_by_id = NULL;
    }
    if (data_lin_interfaces_by_name) {
        g_hash_table_destroy(data_lin_interfaces_by_name);
        data_lin_interfaces_by_name = NULL;
    }

    /* create new hash table */
    data_lin_interfaces_by_id = g_hash_table_new_full(g_int_hash, g_int_equal, &lin_free_key, NULL);
    data_lin_interfaces_by_name = g_hash_table_new_full(g_str_hash, g_str_equal, &lin_free_key, NULL);

    if (data_lin_interfaces_by_id == NULL || data_lin_interfaces_by_name == NULL || interface_configs == NULL || interface_config_num == 0) {
        return;
    }

    for (i = 0; i < interface_config_num; i++) {
        if (interface_configs[i].interface_id != 0xfffffff) {
            key_id = wmem_new(wmem_epan_scope(), int);
            *key_id = interface_configs[i].interface_id;
            g_hash_table_insert(data_lin_interfaces_by_id, key_id, &interface_configs[i]);
        }

        if (interface_configs[i].interface_name != NULL && interface_configs[i].interface_name[0] != 0) {
            key_name = wmem_strdup(wmem_epan_scope(), interface_configs[i].interface_name);
            g_hash_table_insert(data_lin_interfaces_by_name, key_name, &interface_configs[i]);
        }
    }
}

/* We match based on the config in the following order:
 * - interface_name matches and interface_id matches
 * - interface_name matches and interface_id = 0xffffffff
 * - interface_name = ""    and interface_id matches
 */

static guint
get_bus_id(packet_info *pinfo) {
    guint32             interface_id = pinfo->rec->rec_header.packet_header.interface_id;
    const char         *interface_name = epan_get_interface_name(pinfo->epan, interface_id);
    interface_config_t *tmp = NULL;

    if (!(pinfo->rec->presence_flags & WTAP_HAS_INTERFACE_ID)) {
        return 0;
    }

    if (interface_name != NULL && interface_name[0] != 0) {
        tmp = ht_lookup_interface_config_by_name(interface_name);

        if (tmp != NULL && (tmp->interface_id == 0xffffffff || tmp->interface_id == interface_id)) {
            /* name + id match or name match and id = any */
            return tmp->bus_id;
        }

        tmp = ht_lookup_interface_config_by_id(interface_id);

        if (tmp != NULL && (tmp->interface_name == NULL || tmp->interface_name[0] == 0)) {
            /* id matches and name is any */
            return tmp->bus_id;
        }
    }

    /* we found nothing */
    return 0;
}

static int
dissect_lin(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data _U_) {
    proto_item *ti;
    proto_item *ti_root;
    proto_tree *lin_tree;
    proto_tree *lin_id_tree;
    tvbuff_t   *next_tvb;

    guint payload_length;
    guint msg_type;
    lin_info_t lininfo;

    col_set_str(pinfo->cinfo, COL_PROTOCOL, LIN_NAME);
    col_clear(pinfo->cinfo, COL_INFO);

    ti_root = proto_tree_add_item(tree, proto_lin, tvb, 0, -1, ENC_NA);
    lin_tree = proto_item_add_subtree(ti_root, ett_lin);

    proto_tree_add_item(lin_tree, hf_lin_msg_format_rev, tvb, 0, 1, ENC_BIG_ENDIAN);
    ti = proto_tree_add_item(lin_tree, hf_lin_reserved1, tvb, 1, 3, ENC_BIG_ENDIAN);
    proto_item_set_hidden(ti);

    proto_tree_add_item_ret_uint(lin_tree, hf_lin_payload_length, tvb, 4, 1, ENC_BIG_ENDIAN, &payload_length);
    proto_tree_add_item_ret_uint(lin_tree, hf_lin_message_type, tvb, 4, 1, ENC_BIG_ENDIAN, &msg_type);
    if (msg_type != LIN_MSG_TYPE_EVENT) {
        proto_tree_add_item(lin_tree, hf_lin_checksum_type, tvb, 4, 1, ENC_BIG_ENDIAN);

        ti = proto_tree_add_item(lin_tree, hf_lin_pid, tvb, 5, 1, ENC_BIG_ENDIAN);
        lin_id_tree = proto_item_add_subtree(ti, ett_lin_pid);
        proto_tree_add_item(lin_id_tree, hf_lin_parity, tvb, 5, 1, ENC_BIG_ENDIAN);
        proto_tree_add_item_ret_uint(lin_id_tree, hf_lin_id, tvb, 5, 1, ENC_BIG_ENDIAN, &(lininfo.id));

        proto_tree_add_item(lin_tree, hf_lin_checksum, tvb, 6, 1, ENC_BIG_ENDIAN);
    }
    proto_tree_add_bitmask(lin_tree, tvb, 7, hf_lin_err_errors, ett_errors, error_fields, ENC_BIG_ENDIAN);

    col_add_fstr(pinfo->cinfo, COL_INFO, "LIN %s", val_to_str(msg_type, lin_msg_type_names, "(0x%02x)"));

    switch (msg_type) {
    case LIN_MSG_TYPE_EVENT: {
        guint event_id;
        proto_tree_add_item_ret_uint(lin_tree, hf_lin_event_id, tvb, 8, 4, ENC_BIG_ENDIAN, &event_id);
        col_append_fstr(pinfo->cinfo, COL_INFO, ": %s", val_to_str(event_id, lin_event_type_names, "0x%08x"));
        proto_item_set_end(ti_root, tvb, 12);
        return 12; /* 8 Byte header + 4 Byte payload */
        }
        break;

    case LIN_MSG_TYPE_FRAME:
        if (payload_length > 0) {
            next_tvb = tvb_new_subset_length(tvb, 8, payload_length);
            proto_item_set_end(ti_root, tvb, 8 + payload_length);
            lininfo.len = (guint16)payload_length;
            lininfo.bus_id = (guint16)get_bus_id(pinfo);
            guint32 bus_frame_id = lininfo.id | (lininfo.bus_id << 16);

            if (!dissector_try_uint_new(subdissector_table, bus_frame_id, next_tvb, pinfo, tree, TRUE, &lininfo)) {
                if (!dissector_try_uint_new(subdissector_table, lininfo.id, next_tvb, pinfo, tree, TRUE, &lininfo)) {
                    if (!dissector_try_heuristic(heur_subdissector_list, next_tvb, pinfo, tree, &heur_dtbl_entry, &lininfo)) {
                        call_data_dissector(next_tvb, pinfo, tree);
                    }
                }
            }
        }
        break;
    }

    /* format pads to 4 bytes*/
    if (payload_length <= 4) {
        proto_item_set_end(ti_root, tvb, 12);
        return 12;
    } else if (payload_length <= 8) {
        proto_item_set_end(ti_root, tvb, 16);
        return 16;
    } else {
        return tvb_captured_length(tvb);
    }
}

void
proto_register_lin(void) {
    module_t   *lin_module;
    uat_t      *lin_interface_uat = NULL;

    static hf_register_info hf[] = {
        { &hf_lin_msg_format_rev,
            { "Message Format Revision", "lin.message_format",
            FT_UINT8, BASE_DEC, NULL, 0x0, NULL, HFILL }},
        { &hf_lin_reserved1,
            { "Reserved", "lin.reserved",
            FT_UINT24, BASE_HEX, NULL, 0x0, NULL, HFILL }},
        { &hf_lin_payload_length,
            { "Length", "lin.length",
            FT_UINT8, BASE_DEC, NULL, 0xf0, NULL, HFILL }},
        { &hf_lin_message_type,
            { "Message Type", "lin.message_type",
            FT_UINT8, BASE_DEC, VALS(lin_msg_type_names), 0x0c, NULL, HFILL }},
        { &hf_lin_checksum_type,
            { "Checksum Type", "lin.checksum_type",
            FT_UINT8, BASE_DEC, VALS(lin_checksum_type_names), 0x03, NULL, HFILL }},
        { &hf_lin_pid,
            { "Protected ID", "lin.protected_id",
            FT_UINT8, BASE_HEX_DEC, NULL, 0x00, NULL, HFILL }},
        { &hf_lin_id,
            { "Frame ID", "lin.frame_id",
            FT_UINT8, BASE_HEX_DEC, NULL, 0x3f, NULL, HFILL }},
        { &hf_lin_parity,
            { "Parity", "lin.frame_parity",
            FT_UINT8, BASE_HEX_DEC, NULL, 0xc0, NULL, HFILL }},
        { &hf_lin_checksum,
            { "Checksum", "lin.checksum",
            FT_UINT8, BASE_HEX, NULL, 0x00, NULL, HFILL }},
        { &hf_lin_err_errors,
            { "Errors", "lin.errors",
            FT_UINT8, BASE_HEX, NULL, 0x00, NULL, HFILL }},
        { &hf_lin_err_no_slave_response,
            { "No Slave Response Error", "lin.errors.no_slave_response",
            FT_BOOLEAN, 8, NULL, 0x01, NULL, HFILL }},
        { &hf_lin_err_framing,
            { "Framing Error", "lin.errors.framing_error",
            FT_BOOLEAN, 8, NULL, 0x02, NULL, HFILL }},
        { &hf_lin_err_parity,
            { "Parity Error", "lin.errors.parity_error",
            FT_BOOLEAN, 8, NULL, 0x04, NULL, HFILL }},
        { &hf_lin_err_checksum,
            { "Checksum Error", "lin.errors.checksum_error",
            FT_BOOLEAN, 8, NULL, 0x08, NULL, HFILL }},
        { &hf_lin_err_invalidid,
            { "Invalid ID Error", "lin.errors.invalid_id_error",
            FT_BOOLEAN, 8, NULL, 0x10, NULL, HFILL }},
        { &hf_lin_err_overflow,
            { "Overflow Error", "lin.errors.overflow_error",
            FT_BOOLEAN, 8, NULL, 0x20, NULL, HFILL }},
        { &hf_lin_event_id,
            { "Event ID", "lin.event_id",
            FT_UINT32, BASE_HEX_DEC, VALS(lin_event_type_names), 0x00, NULL, HFILL }},
    };

    static gint *ett[] = {
        &ett_lin,
        &ett_lin_pid,
        &ett_errors,
    };

    proto_lin = proto_register_protocol(LIN_NAME_LONG, LIN_NAME, LIN_NAME_FILTER);
    lin_module = prefs_register_protocol(proto_lin, NULL);

    proto_register_field_array(proto_lin, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));

    lin_handle = register_dissector(LIN_NAME_FILTER, dissect_lin, proto_lin);

    /* the lin.frame_id subdissector table carries the bus id in the higher 16 bits */
    subdissector_table = register_dissector_table("lin.frame_id", "LIN Frame ID", proto_lin, FT_UINT8, BASE_HEX);
    heur_subdissector_list = register_heur_dissector_list(LIN_NAME_FILTER, proto_lin);

    static uat_field_t lin_interface_mapping_uat_fields[] = {
        UAT_FLD_HEX(interface_configs,      interface_id,   "Interface ID",   "ID of the Interface with 0xffffffff = any (hex uint32 without leading 0x)"),
        UAT_FLD_CSTRING(interface_configs,  interface_name, "Interface Name", "Name of the Interface, empty = any (string)"),
        UAT_FLD_HEX(interface_configs,      bus_id,         "Bus ID",         "Bus ID of the Interface (hex uint16 without leading 0x)"),
        UAT_END_FIELDS
    };

    lin_interface_uat = uat_new("LIN Interface Mapping",
        sizeof(interface_config_t),             /* record size           */
        DATAFILE_LIN_INTERFACE_MAPPING,         /* filename              */
        TRUE,                                   /* from profile          */
        (void**)&interface_configs,             /* data_ptr              */
        &interface_config_num,                  /* numitems_ptr          */
        UAT_AFFECTS_DISSECTION,                 /* but not fields        */
        NULL,                                   /* help                  */
        copy_interface_config_cb,               /* copy callback         */
        update_interface_config,                /* update callback       */
        free_interface_config_cb,               /* free callback         */
        post_update_lin_interfaces_cb,          /* post update callback  */
        NULL,                                   /* reset callback        */
        lin_interface_mapping_uat_fields        /* UAT field definitions */
    );

    prefs_register_uat_preference(lin_module, "_lin_interface_mapping", "Interface Mapping",
        "A table to define the mapping between interface and Bus ID.", lin_interface_uat);
}

void
proto_reg_handoff_lin(void) {
    dissector_add_uint("wtap_encap", WTAP_ENCAP_LIN, lin_handle);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
