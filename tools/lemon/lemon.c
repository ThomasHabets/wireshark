/*
** Copyright (c) 1991, 1994, 1997, 1998 D. Richard Hipp
**
** This file contains all sources (including headers) to the LEMON
** LALR(1) parser generator.  The sources have been combined into a
** single file to make it easy to include LEMON as part of another
** program.
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License as published by the Free Software Foundation; either
** version 2 of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
**
** Author contact information:
**   drh@acm.org
**   http://www.hwaci.com/drh/
**
** $Id: lemon.c,v 1.3 2001/02/15 06:01:23 guy Exp $
*/
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
 * Wrapper around "isupper()", "islower()", etc. to cast the argument to
 * "unsigned char", so that they at least handle non-ASCII 8-bit characters
 * (and don't provoke a pile of warnings from GCC).
 */
#define safe_isupper(c)	isupper((unsigned char)(c))
#define safe_islower(c)	islower((unsigned char)(c))
#define safe_isalpha(c)	isalpha((unsigned char)(c))
#define safe_isalnum(c)	isalnum((unsigned char)(c))
#define safe_isspace(c)	isspace((unsigned char)(c))

extern int access(const char *, int);

#ifndef __WIN32__
#   if defined(_WIN32) || defined(WIN32)
#	define __WIN32__
#   endif
#endif

/* #define PRIVATE static */
#define PRIVATE

#ifdef TEST
#define MAXRHS 5       /* Set low to exercise exception code */
#else
#define MAXRHS 1000
#endif

char *msort(char *, char **, int (*)(void *, void *));


/********** From the file "struct.h" *************************************/
/*
** Principal data structures for the LEMON parser generator.
*/

typedef enum {FALSE=0, TRUE} Boolean;

/* Symbols (terminals and nonterminals) of the grammar are stored
** in the following: */
struct symbol {
  char *name;              /* Name of the symbol */
  int index;               /* Index number for this symbol */
  enum {
    TERMINAL,
    NONTERMINAL
  } type;                  /* Symbols are all either TERMINALS or NTs */
  struct rule *rule;       /* Linked list of rules of this (if an NT) */
  int prec;                /* Precedence if defined (-1 otherwise) */
  enum e_assoc {
    LEFT,
    RIGHT,
    NONE,
    UNK
  } assoc;                 /* Associativity if predecence is defined */
  char *firstset;          /* First-set for all rules of this symbol */
  Boolean lambda;          /* True if NT and can generate an empty string */
  char *destructor;        /* Code which executes whenever this symbol is
                           ** popped from the stack during error processing */
  int destructorln;        /* Line number of destructor code */
  char *datatype;          /* The data type of information held by this
                           ** object. Only used if type==NONTERMINAL */
  int dtnum;               /* The data type number.  In the parser, the value
                           ** stack is a union.  The .yy%d element of this
                           ** union is the correct data type for this object */
};

/* Each production rule in the grammar is stored in the following
** structure.  */
struct rule {
  struct symbol *lhs;      /* Left-hand side of the rule */
  char *lhsalias;          /* Alias for the LHS (NULL if none) */
  int ruleline;            /* Line number for the rule */
  int nrhs;                /* Number of RHS symbols */
  struct symbol **rhs;     /* The RHS symbols */
  char **rhsalias;         /* An alias for each RHS symbol (NULL if none) */
  int line;                /* Line number at which code begins */
  char *code;              /* The code executed when this rule is reduced */
  struct symbol *precsym;  /* Precedence symbol for this rule */
  int index;               /* An index number for this rule */
  Boolean canReduce;       /* True if this rule is ever reduced */
  struct rule *nextlhs;    /* Next rule with the same LHS */
  struct rule *next;       /* Next rule in the global list */
};

/* A configuration is a production rule of the grammar together with
** a mark (dot) showing how much of that rule has been processed so far.
** Configurations also contain a follow-set which is a list of terminal
** symbols which are allowed to immediately follow the end of the rule.
** Every configuration is recorded as an instance of the following: */
struct config {
  struct rule *rp;         /* The rule upon which the configuration is based */
  int dot;                 /* The parse point */
  char *fws;               /* Follow-set for this configuration only */
  struct plink *fplp;      /* Follow-set forward propagation links */
  struct plink *bplp;      /* Follow-set backwards propagation links */
  struct state *stp;       /* Pointer to state which contains this */
  enum {
    COMPLETE,              /* The status is used during followset and */
    INCOMPLETE             /*    shift computations */
  } status;
  struct config *next;     /* Next configuration in the state */
  struct config *bp;       /* The next basis configuration */
};

/* Every shift or reduce operation is stored as one of the following */
struct action {
  struct symbol *sp;       /* The look-ahead symbol */
  enum e_action {
    SHIFT,
    ACCEPT,
    REDUCE,
    ERROR,
    CONFLICT,                /* Was a reduce, but part of a conflict */
    SH_RESOLVED,             /* Was a shift.  Precedence resolved conflict */
    RD_RESOLVED,             /* Was reduce.  Precedence resolved conflict */
    NOT_USED                 /* Deleted by compression */
  } type;
  union {
    struct state *stp;     /* The new state, if a shift */
    struct rule *rp;       /* The rule, if a reduce */
  } x;
  struct action *next;     /* Next action for this state */
  struct action *collide;  /* Next action with the same hash */
};

/* Each state of the generated parser's finite state machine
** is encoded as an instance of the following structure. */
struct state {
  struct config *bp;       /* The basis configurations for this state */
  struct config *cfp;      /* All configurations in this set */
  int index;               /* Sequencial number for this state */
  struct action *ap;       /* Array of actions for this state */
  int naction;             /* Number of actions for this state */
  int tabstart;            /* First index of the action table */
  int tabdfltact;          /* Default action */
};

/* A followset propagation link indicates that the contents of one
** configuration followset should be propagated to another whenever
** the first changes. */
struct plink {
  struct config *cfp;      /* The configuration to which linked */
  struct plink *next;      /* The next propagate link */
};

/* The state vector for the entire parser generator is recorded as
** follows.  (LEMON uses no global variables and makes little use of
** static variables.  Fields in the following structure can be thought
** of as begin global variables in the program.) */
struct lemon {
  struct state **sorted;   /* Table of states sorted by state number */
  struct rule *rule;       /* List of all rules */
  int nstate;              /* Number of states */
  int nrule;               /* Number of rules */
  int nsymbol;             /* Number of terminal and nonterminal symbols */
  int nterminal;           /* Number of terminal symbols */
  struct symbol **symbols; /* Sorted array of pointers to symbols */
  int errorcnt;            /* Number of errors */
  struct symbol *errsym;   /* The error symbol */
  char *name;              /* Name of the generated parser */
  char *arg;               /* Declaration of the 3th argument to parser */
  char *tokentype;         /* Type of terminal symbols in the parser stack */
  char *start;             /* Name of the start symbol for the grammar */
  char *stacksize;         /* Size of the parser stack */
  char *include;           /* Code to put at the start of the C file */
  int  includeln;          /* Line number for start of include code */
  char *error;             /* Code to execute when an error is seen */
  int  errorln;            /* Line number for start of error code */
  char *overflow;          /* Code to execute on a stack overflow */
  int  overflowln;         /* Line number for start of overflow code */
  char *failure;           /* Code to execute on parser failure */
  int  failureln;          /* Line number for start of failure code */
  char *accept;            /* Code to execute when the parser excepts */
  int  acceptln;           /* Line number for the start of accept code */
  char *extracode;         /* Code appended to the generated file */
  int  extracodeln;        /* Line number for the start of the extra code */
  char *tokendest;         /* Code to execute to destroy token data */
  int  tokendestln;        /* Line number for token destroyer code */
  char *filename;          /* Name of the input file */
  char *basename;          /* Basename of inputer file (no directory or path */
  char *outname;           /* Name of the current output file */
  char *outdirname;        /* Name of the output directory, specified by user */
  char *templatename;      /* Name of template file to use, specified by user */
  char *tokenprefix;       /* A prefix added to token names in the .h file */
  int nconflict;           /* Number of parsing conflicts */
  int tablesize;           /* Size of the parse tables */
  int basisflag;           /* Print only basis configurations */
  char *argv0;             /* Name of the program */
};

#define MemoryCheck(X) if((X)==0){ \
  extern void memory_error(void); \
  memory_error(); \
}

/******** From the file "action.h" *************************************/
struct action *Action_new(void);
struct action *Action_sort(struct action *);
void Action_add(struct action **, enum e_action, struct symbol *, void *);

/********* From the file "assert.h" ************************************/
void myassert(char *, int);
#ifndef NDEBUG
#  define assert(X) if(!(X))myassert(__FILE__,__LINE__)
#else
#  define assert(X)
#endif

/********** From the file "build.h" ************************************/
void FindRulePrecedences(struct lemon *);
void FindFirstSets(struct lemon *);
void FindStates(struct lemon *);
void FindLinks(struct lemon *);
void FindFollowSets(struct lemon *);
void FindActions(struct lemon *);

/********* From the file "configlist.h" *********************************/
void Configlist_init(void);
struct config *Configlist_add(struct rule *, int);
struct config *Configlist_addbasis(struct rule *, int);
void Configlist_closure(struct lemon *);
void Configlist_sort(void);
void Configlist_sortbasis(void);
struct config *Configlist_return(void);
struct config *Configlist_basis(void);
void Configlist_eat(struct config *);
void Configlist_reset(void);

/********* From the file "error.h" ***************************************/
void ErrorMsg( char *, int, char *, ... );

/****** From the file "option.h" ******************************************/
struct s_options {
  enum { OPT_FLAG=1,  OPT_INT,  OPT_DBL,  OPT_STR,
         OPT_FFLAG, OPT_FINT, OPT_FDBL, OPT_FSTR} type;
  char *label;
  char *arg;
  char *message;
};
int    optinit(char**,struct s_options*,FILE*);
int    optnargs(void);
char  *get_optarg(int);
void   get_opterr(int);
void   optprint(void);

/******** From the file "parse.h" *****************************************/
void Parse(struct lemon *lemp);

/********* From the file "plink.h" ***************************************/
struct plink *Plink_new(void);
void Plink_add(struct plink **, struct config *);
void Plink_copy(struct plink **, struct plink *);
void Plink_delete(struct plink *);

/********** From the file "report.h" *************************************/
void Reprint(struct lemon *);
void ReportOutput(struct lemon *);
void ReportTable(struct lemon *, int);
void ReportHeader(struct lemon *);
void CompressTables(struct lemon *);

/********** From the file "set.h" ****************************************/
void  SetSize(int N);             /* All sets will be of size N */
char *SetNew(void);               /* A new set for element 0..N */
void  SetFree(char*);             /* Deallocate a set */

int SetAdd(char*,int);            /* Add element to a set */
int SetUnion(char *A,char *B);    /* A <- A U B, thru element N */

#define SetFind(X,Y) (X[Y])       /* True if Y is in set X */

/**************** From the file "table.h" *********************************/
/*
** All code in this file has been automatically generated
** from a specification in the file
**              "table.q"
** by the associative array code building program "aagen".
** Do not edit this file!  Instead, edit the specification
** file, then rerun aagen.
*/
/*
** Code for processing tables in the LEMON parser generator.
*/

/* Routines for handling a strings */

char *Strsafe(char *);

void Strsafe_init(void);
int Strsafe_insert(char *);
char *Strsafe_find(char *);

/* Routines for handling symbols of the grammar */

struct symbol *Symbol_new(char *x);
int Symbolcmpp(struct symbol **, struct symbol **);
void Symbol_init(void);
int Symbol_insert(struct symbol *, char *);
struct symbol *Symbol_find(char *);
struct symbol *Symbol_Nth(int);
int Symbol_count(void);
struct symbol **Symbol_arrayof(void);

/* Routines to manage the state table */

int Configcmp(void *, void *);
struct state *State_new(void);
void State_init(void);
int State_insert(struct state *, struct config *);
struct state *State_find(struct config *);
struct state **State_arrayof(void);

/* Routines used for efficiency in Configlist_add */

void Configtable_init(void);
int Configtable_insert(struct config *);
struct config *Configtable_find(struct config *);
void Configtable_clear(int(*)(struct config *));
/****************** From the file "action.c" *******************************/
/*
** Routines processing parser actions in the LEMON parser generator.
*/

/* Allocate a new parser action */
struct action *Action_new(void){
  static struct action *freelist = 0;
  struct action *new;

  if( freelist==0 ){
    int i;
    int amt = 100;
    freelist = (struct action *)malloc( sizeof(struct action)*amt );
    if( freelist==0 ){
      fprintf(stderr,"Unable to allocate memory for a new parser action.");
      exit(1);
    }
    for(i=0; i<amt-1; i++) freelist[i].next = &freelist[i+1];
    freelist[amt-1].next = 0;
  }
  new = freelist;
  freelist = freelist->next;
  return new;
}

/* Compare two actions */
static int actioncmp(void *ap1_arg, void *ap2_arg)
{
  struct action *ap1 = ap1_arg, *ap2 = ap2_arg;
  int rc;
  rc = ap1->sp->index - ap2->sp->index;
  if( rc==0 ) rc = (int)ap1->type - (int)ap2->type;
  if( rc==0 ){
    assert( ap1->type==REDUCE && ap2->type==REDUCE );
    rc = ap1->x.rp->index - ap2->x.rp->index;
  }
  return rc;
}

/* Sort parser actions */
struct action *Action_sort(struct action *ap)
{
  ap = (struct action *)msort((char *)ap,(char **)&ap->next,actioncmp);
  return ap;
}

void Action_add(struct action **app, enum e_action type, struct symbol *sp,
    void *arg)
{
  struct action *new;
  new = Action_new();
  new->next = *app;
  *app = new;
  new->type = type;
  new->sp = sp;
  if( type==SHIFT ){
    new->x.stp = (struct state *)arg;
  }else{
    new->x.rp = (struct rule *)arg;
  }
}
/********************** From the file "assert.c" ****************************/
/*
** A more efficient way of handling assertions.
*/
void myassert(char *file, int line)
{
  fprintf(stderr,"Assertion failed on line %d of file \"%s\"\n",line,file);
  exit(1);
}
/********************** From the file "build.c" *****************************/
/*
** Routines to construction the finite state machine for the LEMON
** parser generator.
*/

/* Find a precedence symbol of every rule in the grammar.
** 
** Those rules which have a precedence symbol coded in the input
** grammar using the "[symbol]" construct will already have the
** rp->precsym field filled.  Other rules take as their precedence
** symbol the first RHS symbol with a defined precedence.  If there
** are not RHS symbols with a defined precedence, the precedence
** symbol field is left blank.
*/
void FindRulePrecedences(struct lemon *xp)
{
  struct rule *rp;
  for(rp=xp->rule; rp; rp=rp->next){
    if( rp->precsym==0 ){
      int i;
      for(i=0; i<rp->nrhs; i++){
        if( rp->rhs[i]->prec>=0 ){
          rp->precsym = rp->rhs[i];
          break;
	}
      }
    }
  }
  return;
}

/* Find all nonterminals which will generate the empty string.
** Then go back and compute the first sets of every nonterminal.
** The first set is the set of all terminal symbols which can begin
** a string generated by that nonterminal.
*/
void FindFirstSets(struct lemon *lemp)
{
  int i;
  struct rule *rp;
  int progress;

  for(i=0; i<lemp->nsymbol; i++){
    lemp->symbols[i]->lambda = FALSE;
  }
  for(i=lemp->nterminal; i<lemp->nsymbol; i++){
    lemp->symbols[i]->firstset = SetNew();
  }

  /* First compute all lambdas */
  do{
    progress = 0;
    for(rp=lemp->rule; rp; rp=rp->next){
      if( rp->lhs->lambda ) continue;
      for(i=0; i<rp->nrhs; i++){
         if( rp->rhs[i]->lambda==FALSE ) break;
      }
      if( i==rp->nrhs ){
        rp->lhs->lambda = TRUE;
        progress = 1;
      }
    }
  }while( progress );

  /* Now compute all first sets */
  do{
    struct symbol *s1, *s2;
    progress = 0;
    for(rp=lemp->rule; rp; rp=rp->next){
      s1 = rp->lhs;
      for(i=0; i<rp->nrhs; i++){
        s2 = rp->rhs[i];
        if( s2->type==TERMINAL ){
          progress += SetAdd(s1->firstset,s2->index);
          break;
	}else if( s1==s2 ){
          if( s1->lambda==FALSE ) break;
	}else{
          progress += SetUnion(s1->firstset,s2->firstset);
          if( s2->lambda==FALSE ) break;
	}
      }
    }
  }while( progress );
  return;
}

/* Compute all LR(0) states for the grammar.  Links
** are added to between some states so that the LR(1) follow sets
** can be computed later.
*/
PRIVATE struct state *getstate(struct lemon *);  /* forward reference */
void FindStates(lemp)
struct lemon *lemp;
{
  struct symbol *sp;
  struct rule *rp;

  Configlist_init();

  /* Find the start symbol */
  if( lemp->start ){
    sp = Symbol_find(lemp->start);
    if( sp==0 ){
      ErrorMsg(lemp->filename,0,
"The specified start symbol \"%s\" is not \
in a nonterminal of the grammar.  \"%s\" will be used as the start \
symbol instead.",lemp->start,lemp->rule->lhs->name);
      lemp->errorcnt++;
      sp = lemp->rule->lhs;
    }
  }else{
    sp = lemp->rule->lhs;
  }

  /* Make sure the start symbol doesn't occur on the right-hand side of
  ** any rule.  Report an error if it does.  (YACC would generate a new
  ** start symbol in this case.) */
  for(rp=lemp->rule; rp; rp=rp->next){
    int i;
    for(i=0; i<rp->nrhs; i++){
      if( rp->rhs[i]==sp ){
        ErrorMsg(lemp->filename,0,
"The start symbol \"%s\" occurs on the \
right-hand side of a rule. This will result in a parser which \
does not work properly.",sp->name);
        lemp->errorcnt++;
      }
    }
  }

  /* The basis configuration set for the first state
  ** is all rules which have the start symbol as their
  ** left-hand side */
  for(rp=sp->rule; rp; rp=rp->nextlhs){
    struct config *newcfp;
    newcfp = Configlist_addbasis(rp,0);
    SetAdd(newcfp->fws,0);
  }

  /* Compute the first state.  All other states will be
  ** computed automatically during the computation of the first one.
  ** The returned pointer to the first state is not used. */
  (void)getstate(lemp);
  return;
}

/* Return a pointer to a state which is described by the configuration
** list which has been built from calls to Configlist_add.
*/
PRIVATE void buildshifts(struct lemon *, struct state *); /* Forwd ref */
PRIVATE struct state *getstate(struct lemon *lemp)
{
  struct config *cfp, *bp;
  struct state *stp;

  /* Extract the sorted basis of the new state.  The basis was constructed
  ** by prior calls to "Configlist_addbasis()". */
  Configlist_sortbasis();
  bp = Configlist_basis();

  /* Get a state with the same basis */
  stp = State_find(bp);
  if( stp ){
    /* A state with the same basis already exists!  Copy all the follow-set
    ** propagation links from the state under construction into the
    ** preexisting state, then return a pointer to the preexisting state */
    struct config *x, *y;
    for(x=bp, y=stp->bp; x && y; x=x->bp, y=y->bp){
      Plink_copy(&y->bplp,x->bplp);
      Plink_delete(x->fplp);
      x->fplp = x->bplp = 0;
    }
    cfp = Configlist_return();
    Configlist_eat(cfp);
  }else{
    /* This really is a new state.  Construct all the details */
    Configlist_closure(lemp);    /* Compute the configuration closure */
    Configlist_sort();           /* Sort the configuration closure */
    cfp = Configlist_return();   /* Get a pointer to the config list */
    stp = State_new();           /* A new state structure */
    MemoryCheck(stp);
    stp->bp = bp;                /* Remember the configuration basis */
    stp->cfp = cfp;              /* Remember the configuration closure */
    stp->index = lemp->nstate++; /* Every state gets a sequence number */
    stp->ap = 0;                 /* No actions, yet. */
    State_insert(stp,stp->bp);   /* Add to the state table */
    buildshifts(lemp,stp);       /* Recursively compute successor states */
  }
  return stp;
}

/* Construct all successor states to the given state.  A "successor"
** state is any state which can be reached by a shift action.
*/
PRIVATE void buildshifts(
    struct lemon *lemp,
    struct state *stp)     /* The state from which successors are computed */
{
  struct config *cfp;  /* For looping thru the config closure of "stp" */
  struct config *bcfp; /* For the inner loop on config closure of "stp" */
  struct config *new;  /* */
  struct symbol *sp;   /* Symbol following the dot in configuration "cfp" */
  struct symbol *bsp;  /* Symbol following the dot in configuration "bcfp" */
  struct state *newstp; /* A pointer to a successor state */

  /* Each configuration becomes complete after it contibutes to a successor
  ** state.  Initially, all configurations are incomplete */
  for(cfp=stp->cfp; cfp; cfp=cfp->next) cfp->status = INCOMPLETE;

  /* Loop through all configurations of the state "stp" */
  for(cfp=stp->cfp; cfp; cfp=cfp->next){
    if( cfp->status==COMPLETE ) continue;    /* Already used by inner loop */
    if( cfp->dot>=cfp->rp->nrhs ) continue;  /* Can't shift this config */
    Configlist_reset();                      /* Reset the new config set */
    sp = cfp->rp->rhs[cfp->dot];             /* Symbol after the dot */

    /* For every configuration in the state "stp" which has the symbol "sp"
    ** following its dot, add the same configuration to the basis set under
    ** construction but with the dot shifted one symbol to the right. */
    for(bcfp=cfp; bcfp; bcfp=bcfp->next){
      if( bcfp->status==COMPLETE ) continue;    /* Already used */
      if( bcfp->dot>=bcfp->rp->nrhs ) continue; /* Can't shift this one */
      bsp = bcfp->rp->rhs[bcfp->dot];           /* Get symbol after dot */
      if( bsp!=sp ) continue;                   /* Must be same as for "cfp" */
      bcfp->status = COMPLETE;                  /* Mark this config as used */
      new = Configlist_addbasis(bcfp->rp,bcfp->dot+1);
      Plink_add(&new->bplp,bcfp);
    }

    /* Get a pointer to the state described by the basis configuration set
    ** constructed in the preceding loop */
    newstp = getstate(lemp);

    /* The state "newstp" is reached from the state "stp" by a shift action
    ** on the symbol "sp" */
    Action_add(&stp->ap,SHIFT,sp,newstp);
  }
}

/*
** Construct the propagation links
*/
void FindLinks(struct lemon *lemp)
{
  int i;
  struct config *cfp, *other;
  struct state *stp;
  struct plink *plp;

  /* Housekeeping detail:
  ** Add to every propagate link a pointer back to the state to
  ** which the link is attached. */
  for(i=0; i<lemp->nstate; i++){
    stp = lemp->sorted[i];
    for(cfp=stp->cfp; cfp; cfp=cfp->next){
      cfp->stp = stp;
    }
  }

  /* Convert all backlinks into forward links.  Only the forward
  ** links are used in the follow-set computation. */
  for(i=0; i<lemp->nstate; i++){
    stp = lemp->sorted[i];
    for(cfp=stp->cfp; cfp; cfp=cfp->next){
      for(plp=cfp->bplp; plp; plp=plp->next){
        other = plp->cfp;
        Plink_add(&other->fplp,cfp);
      }
    }
  }
}

/* Compute all followsets.
**
** A followset is the set of all symbols which can come immediately
** after a configuration.
*/
void FindFollowSets(struct lemon *lemp)
{
  int i;
  struct config *cfp;
  struct plink *plp;
  int progress;
  int change;

  for(i=0; i<lemp->nstate; i++){
    for(cfp=lemp->sorted[i]->cfp; cfp; cfp=cfp->next){
      cfp->status = INCOMPLETE;
    }
  }
  
  do{
    progress = 0;
    for(i=0; i<lemp->nstate; i++){
      for(cfp=lemp->sorted[i]->cfp; cfp; cfp=cfp->next){
        if( cfp->status==COMPLETE ) continue;
        for(plp=cfp->fplp; plp; plp=plp->next){
          change = SetUnion(plp->cfp->fws,cfp->fws);
          if( change ){
            plp->cfp->status = INCOMPLETE;
            progress = 1;
	  }
	}
        cfp->status = COMPLETE;
      }
    }
  }while( progress );
}

static int resolve_conflict(struct action *, struct action *, struct symbol *);

/* Compute the reduce actions, and resolve conflicts.
*/
void FindActions(struct lemon *lemp)
{
  int i,j;
  struct config *cfp;
  struct state *stp;
  struct symbol *sp;
  struct rule *rp;

  /* Add all of the reduce actions 
  ** A reduce action is added for each element of the followset of
  ** a configuration which has its dot at the extreme right.
  */
  for(i=0; i<lemp->nstate; i++){   /* Loop over all states */
    stp = lemp->sorted[i];
    for(cfp=stp->cfp; cfp; cfp=cfp->next){  /* Loop over all configurations */
      if( cfp->rp->nrhs==cfp->dot ){        /* Is dot at extreme right? */
        for(j=0; j<lemp->nterminal; j++){
          if( SetFind(cfp->fws,j) ){
            /* Add a reduce action to the state "stp" which will reduce by the
            ** rule "cfp->rp" if the lookahead symbol is "lemp->symbols[j]" */
            Action_add(&stp->ap,REDUCE,lemp->symbols[j],cfp->rp);
          }
	}
      }
    }
  }

  /* Add the accepting token */
  if( lemp->start ){
    sp = Symbol_find(lemp->start);
    if( sp==0 ) sp = lemp->rule->lhs;
  }else{
    sp = lemp->rule->lhs;
  }
  /* Add to the first state (which is always the starting state of the
  ** finite state machine) an action to ACCEPT if the lookahead is the
  ** start nonterminal.  */
  Action_add(&lemp->sorted[0]->ap,ACCEPT,sp,0);

  /* Resolve conflicts */
  for(i=0; i<lemp->nstate; i++){
    struct action *ap, *nap;
    struct state *stp;
    stp = lemp->sorted[i];
    assert( stp->ap );
    stp->ap = Action_sort(stp->ap);
    for(ap=stp->ap; ap && ap->next; ap=nap){
      for(nap=ap->next; nap && nap->sp==ap->sp; nap=nap->next){
         /* The two actions "ap" and "nap" have the same lookahead.
         ** Figure out which one should be used */
         lemp->nconflict += resolve_conflict(ap,nap,lemp->errsym);
      }
    }
  }

  /* Report an error for each rule that can never be reduced. */
  for(rp=lemp->rule; rp; rp=rp->next) rp->canReduce = FALSE;
  for(i=0; i<lemp->nstate; i++){
    struct action *ap;
    for(ap=lemp->sorted[i]->ap; ap; ap=ap->next){
      if( ap->type==REDUCE ) ap->x.rp->canReduce = TRUE;
    }
  }
  for(rp=lemp->rule; rp; rp=rp->next){
    if( rp->canReduce ) continue;
    ErrorMsg(lemp->filename,rp->ruleline,"This rule can not be reduced.\n");
    lemp->errorcnt++;
  }
}

/* Resolve a conflict between the two given actions.  If the
** conflict can't be resolve, return non-zero.
**
** NO LONGER TRUE:
**   To resolve a conflict, first look to see if either action
**   is on an error rule.  In that case, take the action which
**   is not associated with the error rule.  If neither or both
**   actions are associated with an error rule, then try to
**   use precedence to resolve the conflict.
**
** If either action is a SHIFT, then it must be apx.  This
** function won't work if apx->type==REDUCE and apy->type==SHIFT.
*/
static int resolve_conflict(
    struct action *apx,
    struct action *apy,
    struct symbol *errsym)  /* The error symbol (if defined.  NULL otherwise) */
{
  struct symbol *spx, *spy;
  int errcnt = 0;
  assert( apx->sp==apy->sp );  /* Otherwise there would be no conflict */
  if( apx->type==SHIFT && apy->type==REDUCE ){
    spx = apx->sp;
    spy = apy->x.rp->precsym;
    if( spy==0 || spx->prec<0 || spy->prec<0 ){
      /* Not enough precedence information. */
      apy->type = CONFLICT;
      errcnt++;
    }else if( spx->prec>spy->prec ){    /* Lower precedence wins */
      apy->type = RD_RESOLVED;
    }else if( spx->prec<spy->prec ){
      apx->type = SH_RESOLVED;
    }else if( spx->prec==spy->prec && spx->assoc==RIGHT ){ /* Use operator */
      apy->type = RD_RESOLVED;                             /* associativity */
    }else if( spx->prec==spy->prec && spx->assoc==LEFT ){  /* to break tie */
      apx->type = SH_RESOLVED;
    }else{
      assert( spx->prec==spy->prec && spx->assoc==NONE );
      apy->type = CONFLICT;
      errcnt++;
    }
  }else if( apx->type==REDUCE && apy->type==REDUCE ){
    spx = apx->x.rp->precsym;
    spy = apy->x.rp->precsym;
    if( spx==0 || spy==0 || spx->prec<0 ||
    spy->prec<0 || spx->prec==spy->prec ){
      apy->type = CONFLICT;
      errcnt++;
    }else if( spx->prec>spy->prec ){
      apy->type = RD_RESOLVED;
    }else if( spx->prec<spy->prec ){
      apx->type = RD_RESOLVED;
    }
  }else{
    /* Can't happen.  Shifts have to come before Reduces on the
    ** list because the reduces were added last.  Hence, if apx->type==REDUCE
    ** then it is impossible for apy->type==SHIFT */
  }
  return errcnt;
}
/********************* From the file "configlist.c" *************************/
/*
** Routines to processing a configuration list and building a state
** in the LEMON parser generator.
*/

static struct config *freelist = 0;      /* List of free configurations */
static struct config *current = 0;       /* Top of list of configurations */
static struct config **currentend = 0;   /* Last on list of configs */
static struct config *basis = 0;         /* Top of list of basis configs */
static struct config **basisend = 0;     /* End of list of basis configs */

/* Return a pointer to a new configuration */
PRIVATE struct config *newconfig(void){
  struct config *new;
  if( freelist==0 ){
    int i;
    int amt = 3;
    freelist = (struct config *)malloc( sizeof(struct config)*amt );
    if( freelist==0 ){
      fprintf(stderr,"Unable to allocate memory for a new configuration.");
      exit(1);
    }
    for(i=0; i<amt-1; i++) freelist[i].next = &freelist[i+1];
    freelist[amt-1].next = 0;
  }
  new = freelist;
  freelist = freelist->next;
  return new;
}

/* The configuration "old" is no longer used */
PRIVATE void deleteconfig(struct config *old)
{
  old->next = freelist;
  freelist = old;
}

/* Initialized the configuration list builder */
void Configlist_init(void){
  current = 0;
  currentend = &current;
  basis = 0;
  basisend = &basis;
  Configtable_init();
  return;
}

/* Initialized the configuration list builder */
void Configlist_reset(void){
  current = 0;
  currentend = &current;
  basis = 0;
  basisend = &basis;
  Configtable_clear(0);
  return;
}

/* Add another configuration to the configuration list */
struct config *Configlist_add(
    struct rule *rp,    /* The rule */
    int dot)            /* Index into the RHS of the rule where the dot goes */
{
  struct config *cfp, model;

  assert( currentend!=0 );
  model.rp = rp;
  model.dot = dot;
  cfp = Configtable_find(&model);
  if( cfp==0 ){
    cfp = newconfig();
    cfp->rp = rp;
    cfp->dot = dot;
    cfp->fws = SetNew();
    cfp->stp = 0;
    cfp->fplp = cfp->bplp = 0;
    cfp->next = 0;
    cfp->bp = 0;
    *currentend = cfp;
    currentend = &cfp->next;
    Configtable_insert(cfp);
  }
  return cfp;
}

/* Add a basis configuration to the configuration list */
struct config *Configlist_addbasis(struct rule *rp, int dot)
{
  struct config *cfp, model;

  assert( basisend!=0 );
  assert( currentend!=0 );
  model.rp = rp;
  model.dot = dot;
  cfp = Configtable_find(&model);
  if( cfp==0 ){
    cfp = newconfig();
    cfp->rp = rp;
    cfp->dot = dot;
    cfp->fws = SetNew();
    cfp->stp = 0;
    cfp->fplp = cfp->bplp = 0;
    cfp->next = 0;
    cfp->bp = 0;
    *currentend = cfp;
    currentend = &cfp->next;
    *basisend = cfp;
    basisend = &cfp->bp;
    Configtable_insert(cfp);
  }
  return cfp;
}

/* Compute the closure of the configuration list */
void Configlist_closure(struct lemon *lemp)
{
  struct config *cfp, *newcfp;
  struct rule *rp, *newrp;
  struct symbol *sp, *xsp;
  int i, dot;

  assert( currentend!=0 );
  for(cfp=current; cfp; cfp=cfp->next){
    rp = cfp->rp;
    dot = cfp->dot;
    if( dot>=rp->nrhs ) continue;
    sp = rp->rhs[dot];
    if( sp->type==NONTERMINAL ){
      if( sp->rule==0 && sp!=lemp->errsym ){
        ErrorMsg(lemp->filename,rp->line,"Nonterminal \"%s\" has no rules.",
          sp->name);
        lemp->errorcnt++;
      }
      for(newrp=sp->rule; newrp; newrp=newrp->nextlhs){
        newcfp = Configlist_add(newrp,0);
        for(i=dot+1; i<rp->nrhs; i++){
          xsp = rp->rhs[i];
          if( xsp->type==TERMINAL ){
            SetAdd(newcfp->fws,xsp->index);
            break;
	  }else{
            SetUnion(newcfp->fws,xsp->firstset);
            if( xsp->lambda==FALSE ) break;
	  }
	}
        if( i==rp->nrhs ) Plink_add(&cfp->fplp,newcfp);
      }
    }
  }
  return;
}

/* Sort the configuration list */
void Configlist_sort(void){
  current = (struct config *)msort((char *)current,(char **)&(current->next),Configcmp);
  currentend = 0;
  return;
}

/* Sort the basis configuration list */
void Configlist_sortbasis(void){
  basis = (struct config *)msort((char *)current,(char **)&(current->bp),Configcmp);
  basisend = 0;
  return;
}

/* Return a pointer to the head of the configuration list and
** reset the list */
struct config *Configlist_return(void){
  struct config *old;
  old = current;
  current = 0;
  currentend = 0;
  return old;
}

/* Return a pointer to the head of the configuration list and
** reset the list */
struct config *Configlist_basis(void){
  struct config *old;
  old = basis;
  basis = 0;
  basisend = 0;
  return old;
}

/* Free all elements of the given configuration list */
void Configlist_eat(struct config *cfp)
{
  struct config *nextcfp;
  for(; cfp; cfp=nextcfp){
    nextcfp = cfp->next;
    assert( cfp->fplp==0 );
    assert( cfp->bplp==0 );
    if( cfp->fws ) SetFree(cfp->fws);
    deleteconfig(cfp);
  }
  return;
}
/***************** From the file "error.c" *********************************/
/*
** Code for printing error message.
*/

/* Find a good place to break "msg" so that its length is at least "min"
** but no more than "max".  Make the point as close to max as possible.
*/
static int findbreak(char *msg, int min, int max)
{
  int i,spot;
  char c;
  for(i=spot=min; i<=max; i++){
    c = msg[i];
    if( c=='\t' ) msg[i] = ' ';
    if( c=='\n' ){ msg[i] = ' '; spot = i; break; }
    if( c==0 ){ spot = i; break; }
    if( c=='-' && i<max-1 ) spot = i+1;
    if( c==' ' ) spot = i;
  }
  return spot;
}

/*
** The error message is split across multiple lines if necessary.  The
** splits occur at a space, if there is a space available near the end
** of the line.
*/
#define ERRMSGSIZE  10000 /* Hope this is big enough.  No way to error check */
#define LINEWIDTH      79 /* Max width of any output line */
#define PREFIXLIMIT    30 /* Max width of the prefix on each line */
void ErrorMsg(char *filename, int lineno, char *format, ...)
{
  char errmsg[ERRMSGSIZE];
  char prefix[PREFIXLIMIT+10];
  int errmsgsize;
  int prefixsize;
  int availablewidth;
  va_list ap;
  int end, restart, base;

  va_start(ap, format);
  /* Prepare a prefix to be prepended to every output line */
  if( lineno>0 ){
    sprintf(prefix,"%.*s:%d: ",PREFIXLIMIT-10,filename,lineno);
  }else{
    sprintf(prefix,"%.*s: ",PREFIXLIMIT-10,filename);
  }
  prefixsize = strlen(prefix);
  availablewidth = LINEWIDTH - prefixsize;

  /* Generate the error message */
  vsprintf(errmsg,format,ap);
  va_end(ap);
  errmsgsize = strlen(errmsg);
  /* Remove trailing '\n's from the error message. */
  while( errmsgsize>0 && errmsg[errmsgsize-1]=='\n' ){
     errmsg[--errmsgsize] = 0;
  }

  /* Print the error message */
  base = 0;
  while( errmsg[base]!=0 ){
    end = restart = findbreak(&errmsg[base],0,availablewidth);
    restart += base;
    while( errmsg[restart]==' ' ) restart++;
    fprintf(stdout,"%s%.*s\n",prefix,end,&errmsg[base]);
    base = restart;
  }
}
/**************** From the file "main.c" ************************************/
/*
** Main program file for the LEMON parser generator.
*/

/* Report an out-of-memory condition and abort.  This function
** is used mostly by the "MemoryCheck" macro in struct.h
*/
void memory_error(void){
  fprintf(stderr,"Out of memory.  Aborting...\n");
  exit(1);
}

/* Locates the basename in a string possibly containing paths,
 * including forward-slash and backward-slash delimiters on Windows,
 * and allocates a new string containing just the basename.
 * Returns the pointer to that string.
 */
PRIVATE char*
make_basename(char* fullname)
{
	char	*cp;
	char	*new_string;

	/* Find the last forward slash */
	cp = strrchr(fullname, '/');

#ifdef WIN32
	/* On Windows, if no forward slash was found, look ofr
	 * backslash also */
	if (!cp)
		cp = strrchr(fullname, '\\');
#endif

	if (!cp) {
		new_string = malloc( strlen(fullname) );
		strcpy(new_string, fullname);
	}
	else {
		/* skip the slash */
		cp++;
		new_string = malloc( strlen(cp) );
		strcpy(new_string, cp);
	}

	return new_string;
}




/* The main program.  Parse the command line and do it... */
int main(int argc, char **argv)
{
  static int version = 0;
  static int rpflag = 0;
  static int basisflag = 0;
  static int compress = 0;
  static int quiet = 0;
  static int statistics = 0;
  static int mhflag = 0;
  static char *outdirname = NULL;
  static char *templatename = NULL;
  static struct s_options options[] = {
    {OPT_FLAG, "b", (char*)&basisflag, "Print only the basis in report."},
    {OPT_FLAG, "c", (char*)&compress, "Don't compress the action table."},
    {OPT_STR,  "d", (char*)&outdirname, "Output directory name."},
    {OPT_FLAG, "g", (char*)&rpflag, "Print grammar without actions."},
    {OPT_FLAG, "m", (char*)&mhflag, "Output a makeheaders compatible file"},
    {OPT_FLAG, "q", (char*)&quiet, "(Quiet) Don't print the report file."},
    {OPT_FLAG, "s", (char*)&statistics, "Print parser stats to standard output."},
    {OPT_STR,  "t", (char*)&templatename, "Template file to use."},
    {OPT_FLAG, "x", (char*)&version, "Print the version number."},
    {OPT_FLAG,0,0,0}
  };
  int i;
  struct lemon lem;

  optinit(argv,options,stderr);
  if( version ){
     printf("Lemon version 1.0\n"
       "Copyright 1991-1997 by D. Richard Hipp\n"
       "Freely distributable under the GNU Public License.\n"
     );
     exit(0); 
  }
  if( optnargs()!=1 ){
    fprintf(stderr,"Exactly one filename argument is required.\n");
    exit(1);
  }
  lem.errorcnt = 0;

  /* Initialize the machine */
  Strsafe_init();
  Symbol_init();
  State_init();
  lem.argv0 = argv[0];
  lem.filename = get_optarg(0);
  lem.basisflag = basisflag;
  lem.nconflict = 0;
  lem.name = lem.include = lem.arg = lem.tokentype = lem.start = 0;
  lem.stacksize = 0;
  lem.error = lem.overflow = lem.failure = lem.accept = lem.tokendest =
     lem.tokenprefix = lem.outname = lem.extracode = 0;
  lem.tablesize = 0;
  Symbol_new("$");
  lem.errsym = Symbol_new("error");
  lem.outdirname = outdirname;
  lem.templatename = templatename;
  lem.basename = make_basename(lem.filename);

  /* Parse the input file */
  Parse(&lem);
  if( lem.errorcnt ) exit(lem.errorcnt);
  if( lem.rule==0 ){
    fprintf(stderr,"Empty grammar.\n");
    exit(1);
  }

  /* Count and index the symbols of the grammar */
  lem.nsymbol = Symbol_count();
  Symbol_new("{default}");
  lem.symbols = Symbol_arrayof();
  qsort(lem.symbols,lem.nsymbol+1,sizeof(struct symbol*),
        (int(*)())Symbolcmpp);
  for(i=0; i<=lem.nsymbol; i++) lem.symbols[i]->index = i;
  for(i=1; safe_isupper(lem.symbols[i]->name[0]); i++);
  lem.nterminal = i;

  /* Generate a reprint of the grammar, if requested on the command line */
  if( rpflag ){
    Reprint(&lem);
  }else{
    /* Initialize the size for all follow and first sets */
    SetSize(lem.nterminal);

    /* Find the precedence for every production rule (that has one) */
    FindRulePrecedences(&lem);

    /* Compute the lambda-nonterminals and the first-sets for every
    ** nonterminal */
    FindFirstSets(&lem);

    /* Compute all LR(0) states.  Also record follow-set propagation
    ** links so that the follow-set can be computed later */
    lem.nstate = 0;
    FindStates(&lem);
    lem.sorted = State_arrayof();

    /* Tie up loose ends on the propagation links */
    FindLinks(&lem);

    /* Compute the follow set of every reducible configuration */
    FindFollowSets(&lem);

    /* Compute the action tables */
    FindActions(&lem);

    /* Compress the action tables */
    if( compress==0 ) CompressTables(&lem);

    /* Generate a report of the parser generated.  (the "y.output" file) */
    if( !quiet ) ReportOutput(&lem);

    /* Generate the source code for the parser */
    ReportTable(&lem, mhflag);

    /* Produce a header file for use by the scanner.  (This step is
    ** omitted if the "-m" option is used because makeheaders will
    ** generate the file for us.) */
    if( !mhflag ) ReportHeader(&lem);
  }
  if( statistics ){
    printf("Parser statistics: %d terminals, %d nonterminals, %d rules\n",
      lem.nterminal, lem.nsymbol - lem.nterminal, lem.nrule);
    printf("                   %d states, %d parser table entries, %d conflicts\n",
      lem.nstate, lem.tablesize, lem.nconflict);
  }
  if( lem.nconflict ){
    fprintf(stderr,"%d parsing conflicts.\n",lem.nconflict);
  }
  exit(lem.errorcnt + lem.nconflict);
}
/******************** From the file "msort.c" *******************************/
/*
** A generic merge-sort program.
**
** USAGE:
** Let "ptr" be a pointer to some structure which is at the head of
** a null-terminated list.  Then to sort the list call:
**
**     ptr = msort(ptr,&(ptr->next),cmpfnc);
**
** In the above, "cmpfnc" is a pointer to a function which compares
** two instances of the structure and returns an integer, as in
** strcmp.  The second argument is a pointer to the pointer to the
** second element of the linked list.  This address is used to compute
** the offset to the "next" field within the structure.  The offset to
** the "next" field must be constant for all structures in the list.
**
** The function returns a new pointer which is the head of the list
** after sorting.
**
** ALGORITHM:
** Merge-sort.
*/

/*
** Return a pointer to the next structure in the linked list.
*/
#define NEXT(A) (*(char**)(((char *)A)+offset))

/*
** Inputs:
**   a:       A sorted, null-terminated linked list.  (May be null).
**   b:       A sorted, null-terminated linked list.  (May be null).
**   cmp:     A pointer to the comparison function.
**   offset:  Offset in the structure to the "next" field.
**
** Return Value:
**   A pointer to the head of a sorted list containing the elements
**   of both a and b.
**
** Side effects:
**   The "next" pointers for elements in the lists a and b are
**   changed.
*/
static char *merge(char *a, char *b, int (*cmp)(void *, void *), int offset)
{
  char *ptr, *head;

  if( a==0 ){
    head = b;
  }else if( b==0 ){
    head = a;
  }else{
    if( (*cmp)(a,b)<0 ){
      ptr = a;
      a = NEXT(a);
    }else{
      ptr = b;
      b = NEXT(b);
    }
    head = ptr;
    while( a && b ){
      if( (*cmp)(a,b)<0 ){
        NEXT(ptr) = a;
        ptr = a;
        a = NEXT(a);
      }else{
        NEXT(ptr) = b;
        ptr = b;
        b = NEXT(b);
      }
    }
    if( a ) NEXT(ptr) = a;
    else    NEXT(ptr) = b;
  }
  return head;
}

/*
** Inputs:
**   list:      Pointer to a singly-linked list of structures.
**   next:      Pointer to pointer to the second element of the list.
**   cmp:       A comparison function.
**
** Return Value:
**   A pointer to the head of a sorted list containing the elements
**   orginally in list.
**
** Side effects:
**   The "next" pointers for elements in list are changed.
*/
#define LISTSIZE 30
char *msort(char *list, char **next, int (*cmp)(void *, void *))
{
  int offset;
  char *ep;
  char *set[LISTSIZE];
  int i;
  offset = (char *)next - (char *)list;
  for(i=0; i<LISTSIZE; i++) set[i] = 0;
  while( list ){
    ep = list;
    list = NEXT(list);
    NEXT(ep) = 0;
    for(i=0; i<LISTSIZE-1 && set[i]!=0; i++){
      ep = merge(ep,set[i],cmp,offset);
      set[i] = 0;
    }
    set[i] = ep;
  }
  ep = 0;
  for(i=0; i<LISTSIZE; i++) if( set[i] ) ep = merge(ep,set[i],cmp,offset);
  return ep;
}
/************************ From the file "option.c" **************************/
static char **argv;
static struct s_options *op;
static FILE *errstream;

#define ISOPT(X) ((X)[0]=='-'||(X)[0]=='+'||strchr((X),'=')!=0)

/*
** Print the command line with a carrot pointing to the k-th character
** of the n-th field.
*/
static void errline(int n, int k, FILE *err)
{
  int spcnt, i;
  spcnt = 0;
  if( argv[0] ) fprintf(err,"%s",argv[0]);
  spcnt = strlen(argv[0]) + 1;
  for(i=1; i<n && argv[i]; i++){
    fprintf(err," %s",argv[i]);
    spcnt += strlen(argv[i]+1);
  }
  spcnt += k;
  for(; argv[i]; i++) fprintf(err," %s",argv[i]);
  if( spcnt<20 ){
    fprintf(err,"\n%*s^-- here\n",spcnt,"");
  }else{
    fprintf(err,"\n%*shere --^\n",spcnt-7,"");
  }
}

/*
** Return the index of the N-th non-switch argument.  Return -1
** if N is out of range.
*/
static int argindex(int n)
{
  int i;
  int dashdash = 0;
  if( argv!=0 && *argv!=0 ){
    for(i=1; argv[i]; i++){
      if( dashdash || !ISOPT(argv[i]) ){
        if( n==0 ) return i;
        n--;
      }
      if( strcmp(argv[i],"--")==0 ) dashdash = 1;
    }
  }
  return -1;
}

static char emsg[] = "Command line syntax error: ";

/*
** Process a flag command line argument.
*/
static int handleflags(int i, FILE *err)
{
  int v;
  int errcnt = 0;
  int j;
  for(j=0; op[j].label; j++){
    if( strcmp(&argv[i][1],op[j].label)==0 ) break;
  }
  v = argv[i][0]=='-' ? 1 : 0;
  if( op[j].label==0 ){
    if( err ){
      fprintf(err,"%sundefined option.\n",emsg);
      errline(i,1,err);
    }
    errcnt++;
  }else if( op[j].type==OPT_FLAG ){
    *((int*)op[j].arg) = v;
  }else if( op[j].type==OPT_FFLAG ){
    (*(void(*)())(op[j].arg))(v);
  }else{
    if( err ){
      fprintf(err,"%smissing argument on switch.\n",emsg);
      errline(i,1,err);
    }
    errcnt++;
  }
  return errcnt;
}

/*
** Process a command line switch which has an argument.
*/
static int handleswitch(int i, FILE *err)
{
  int lv = 0;
  double dv = 0.0;
  char *sv = 0, *end;
  char *cp;
  int j;
  int errcnt = 0;
  cp = strchr(argv[i],'=');
  *cp = 0;
  for(j=0; op[j].label; j++){
    if( strcmp(argv[i],op[j].label)==0 ) break;
  }
  *cp = '=';
  if( op[j].label==0 ){
    if( err ){
      fprintf(err,"%sundefined option.\n",emsg);
      errline(i,0,err);
    }
    errcnt++;
  }else{
    cp++;
    switch( op[j].type ){
      case OPT_FLAG:
      case OPT_FFLAG:
        if( err ){
          fprintf(err,"%soption requires an argument.\n",emsg);
          errline(i,0,err);
        }
        errcnt++;
        break;
      case OPT_DBL:
      case OPT_FDBL:
        dv = strtod(cp,&end);
        if( *end ){
          if( err ){
            fprintf(err,"%sillegal character in floating-point argument.\n",emsg);
            errline(i,(int)(end-argv[i]),err);
          }
          errcnt++;
        }
        break;
      case OPT_INT:
      case OPT_FINT:
        lv = strtol(cp,&end,0);
        if( *end ){
          if( err ){
            fprintf(err,"%sillegal character in integer argument.\n",emsg);
            errline(i,(int)(end-argv[i]),err);
          }
          errcnt++;
        }
        break;
      case OPT_STR:
      case OPT_FSTR:
        sv = cp;
        break;
    }
    switch( op[j].type ){
      case OPT_FLAG:
      case OPT_FFLAG:
        break;
      case OPT_DBL:
        *(double*)(op[j].arg) = dv;
        break;
      case OPT_FDBL:
        (*(void(*)())(op[j].arg))(dv);
        break;
      case OPT_INT:
        *(int*)(op[j].arg) = lv;
        break;
      case OPT_FINT:
        (*(void(*)())(op[j].arg))((int)lv);
        break;
      case OPT_STR:
        *(char**)(op[j].arg) = sv;
        break;
      case OPT_FSTR:
        (*(void(*)())(op[j].arg))(sv);
        break;
    }
  }
  return errcnt;
}

int optinit(char **a, struct s_options *o, FILE *err)
{
  int errcnt = 0;
  argv = a;
  op = o;
  errstream = err;
  if( argv && *argv && op ){
    int i;
    for(i=1; argv[i]; i++){
      if( argv[i][0]=='+' || argv[i][0]=='-' ){
        errcnt += handleflags(i,err);
      }else if( strchr(argv[i],'=') ){
        errcnt += handleswitch(i,err);
      }
    }
  }
  if( errcnt>0 ){
    fprintf(err,"Valid command line options for \"%s\" are:\n",*a);
    optprint();
    exit(1);
  }
  return 0;
}

int optnargs(void){
  int cnt = 0;
  int dashdash = 0;
  int i;
  if( argv!=0 && argv[0]!=0 ){
    for(i=1; argv[i]; i++){
      if( dashdash || !ISOPT(argv[i]) ) cnt++;
      if( strcmp(argv[i],"--")==0 ) dashdash = 1;
    }
  }
  return cnt;
}

char *get_optarg(int n)
{
  int i;
  i = argindex(n);
  return i>=0 ? argv[i] : 0;
}

void get_opterr(int n)
{
  int i;
  i = argindex(n);
  if( i>=0 ) errline(i,0,errstream);
}

void optprint(void){
  int i;
  int max, len;
  max = 0;
  for(i=0; op[i].label; i++){
    len = strlen(op[i].label) + 1;
    switch( op[i].type ){
      case OPT_FLAG:
      case OPT_FFLAG:
        break;
      case OPT_INT:
      case OPT_FINT:
        len += 9;       /* length of "<integer>" */
        break;
      case OPT_DBL:
      case OPT_FDBL:
        len += 6;       /* length of "<real>" */
        break;
      case OPT_STR:
      case OPT_FSTR:
        len += 8;       /* length of "<string>" */
        break;
    }
    if( len>max ) max = len;
  }
  for(i=0; op[i].label; i++){
    switch( op[i].type ){
      case OPT_FLAG:
      case OPT_FFLAG:
        fprintf(errstream,"  -%-*s  %s\n",max,op[i].label,op[i].message);
        break;
      case OPT_INT:
      case OPT_FINT:
        fprintf(errstream,"  %s=<integer>%*s  %s\n",op[i].label,
          max-strlen(op[i].label)-9,"",op[i].message);
        break;
      case OPT_DBL:
      case OPT_FDBL:
        fprintf(errstream,"  %s=<real>%*s  %s\n",op[i].label,
          max-strlen(op[i].label)-6,"",op[i].message);
        break;
      case OPT_STR:
      case OPT_FSTR:
        fprintf(errstream,"  %s=<string>%*s  %s\n",op[i].label,
          max-strlen(op[i].label)-8,"",op[i].message);
        break;
    }
  }
}
/*********************** From the file "parse.c" ****************************/
/*
** Input file parser for the LEMON parser generator.
*/

/* The state of the parser */
struct pstate {
  char *filename;       /* Name of the input file */
  int tokenlineno;      /* Linenumber at which current token starts */
  int errorcnt;         /* Number of errors so far */
  char *tokenstart;     /* Text of current token */
  struct lemon *gp;     /* Global state vector */
  enum e_state {
    INITIALIZE,
    WAITING_FOR_DECL_OR_RULE,
    WAITING_FOR_DECL_KEYWORD,
    WAITING_FOR_DECL_ARG,
    WAITING_FOR_PRECEDENCE_SYMBOL,
    WAITING_FOR_ARROW,
    IN_RHS,
    LHS_ALIAS_1,
    LHS_ALIAS_2,
    LHS_ALIAS_3,
    RHS_ALIAS_1,
    RHS_ALIAS_2,
    PRECEDENCE_MARK_1,
    PRECEDENCE_MARK_2,
    RESYNC_AFTER_RULE_ERROR,
    RESYNC_AFTER_DECL_ERROR,
    WAITING_FOR_DESTRUCTOR_SYMBOL,
    WAITING_FOR_DATATYPE_SYMBOL
  } state;                   /* The state of the parser */
  struct symbol *lhs;        /* Left-hand side of current rule */
  char *lhsalias;            /* Alias for the LHS */
  int nrhs;                  /* Number of right-hand side symbols seen */
  struct symbol *rhs[MAXRHS];  /* RHS symbols */
  char *alias[MAXRHS];       /* Aliases for each RHS symbol (or NULL) */
  struct rule *prevrule;     /* Previous rule parsed */
  char *declkeyword;         /* Keyword of a declaration */
  char **declargslot;        /* Where the declaration argument should be put */
  int *decllnslot;           /* Where the declaration linenumber is put */
  enum e_assoc declassoc;    /* Assign this association to decl arguments */
  int preccounter;           /* Assign this precedence to decl arguments */
  struct rule *firstrule;    /* Pointer to first rule in the grammar */
  struct rule *lastrule;     /* Pointer to the most recently parsed rule */
};

/* Parse a single token */
static void parseonetoken(struct pstate *psp)
{
  char *x;
  x = Strsafe(psp->tokenstart);     /* Save the token permanently */
#if 0
  printf("%s:%d: Token=[%s] state=%d\n",psp->filename,psp->tokenlineno,
    x,psp->state);
#endif
  switch( psp->state ){
    case INITIALIZE:
      psp->prevrule = 0;
      psp->preccounter = 0;
      psp->firstrule = psp->lastrule = 0;
      psp->gp->nrule = 0;
      /* Fall thru to next case */
    case WAITING_FOR_DECL_OR_RULE:
      if( x[0]=='%' ){
        psp->state = WAITING_FOR_DECL_KEYWORD;
      }else if( safe_islower(x[0]) ){
        psp->lhs = Symbol_new(x);
        psp->nrhs = 0;
        psp->lhsalias = 0;
        psp->state = WAITING_FOR_ARROW;
      }else if( x[0]=='{' ){
        if( psp->prevrule==0 ){
          ErrorMsg(psp->filename,psp->tokenlineno,
"There is not prior rule opon which to attach the code \
fragment which begins on this line.");
          psp->errorcnt++;
	}else if( psp->prevrule->code!=0 ){
          ErrorMsg(psp->filename,psp->tokenlineno,
"Code fragment beginning on this line is not the first \
to follow the previous rule.");
          psp->errorcnt++;
        }else{
          psp->prevrule->line = psp->tokenlineno;
          psp->prevrule->code = &x[1];
	}
      }else if( x[0]=='[' ){
        psp->state = PRECEDENCE_MARK_1;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Token \"%s\" should be either \"%%\" or a nonterminal name.",
          x);
        psp->errorcnt++;
      }
      break;
    case PRECEDENCE_MARK_1:
      if( !safe_isupper(x[0]) ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "The precedence symbol must be a terminal.");
        psp->errorcnt++;
      }else if( psp->prevrule==0 ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "There is no prior rule to assign precedence \"[%s]\".",x);
        psp->errorcnt++;
      }else if( psp->prevrule->precsym!=0 ){
        ErrorMsg(psp->filename,psp->tokenlineno,
"Precedence mark on this line is not the first \
to follow the previous rule.");
        psp->errorcnt++;
      }else{
        psp->prevrule->precsym = Symbol_new(x);
      }
      psp->state = PRECEDENCE_MARK_2;
      break;
    case PRECEDENCE_MARK_2:
      if( x[0]!=']' ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing \"]\" on precedence mark.");
        psp->errorcnt++;
      }
      psp->state = WAITING_FOR_DECL_OR_RULE;
      break;
    case WAITING_FOR_ARROW:
      if( x[0]==':' && x[1]==':' && x[2]=='=' ){
        psp->state = IN_RHS;
      }else if( x[0]=='(' ){
        psp->state = LHS_ALIAS_1;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Expected to see a \":\" following the LHS symbol \"%s\".",
          psp->lhs->name);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case LHS_ALIAS_1:
      if( safe_isalpha(x[0]) ){
        psp->lhsalias = x;
        psp->state = LHS_ALIAS_2;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "\"%s\" is not a valid alias for the LHS \"%s\"\n",
          x,psp->lhs->name);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case LHS_ALIAS_2:
      if( x[0]==')' ){
        psp->state = LHS_ALIAS_3;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing \")\" following LHS alias name \"%s\".",psp->lhsalias);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case LHS_ALIAS_3:
      if( x[0]==':' && x[1]==':' && x[2]=='=' ){
        psp->state = IN_RHS;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing \"->\" following: \"%s(%s)\".",
           psp->lhs->name,psp->lhsalias);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case IN_RHS:
      if( x[0]=='.' ){
        struct rule *rp;
        rp = (struct rule *)malloc( sizeof(struct rule) + 
             sizeof(struct symbol*)*psp->nrhs + sizeof(char*)*psp->nrhs );
        if( rp==0 ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "Can't allocate enough memory for this rule.");
          psp->errorcnt++;
          psp->prevrule = 0;
	}else{
          int i;
          rp->ruleline = psp->tokenlineno;
          rp->rhs = (struct symbol**)&rp[1];
          rp->rhsalias = (char**)&(rp->rhs[psp->nrhs]);
          for(i=0; i<psp->nrhs; i++){
            rp->rhs[i] = psp->rhs[i];
            rp->rhsalias[i] = psp->alias[i];
	  }
          rp->lhs = psp->lhs;
          rp->lhsalias = psp->lhsalias;
          rp->nrhs = psp->nrhs;
          rp->code = 0;
          rp->precsym = 0;
          rp->index = psp->gp->nrule++;
          rp->nextlhs = rp->lhs->rule;
          rp->lhs->rule = rp;
          rp->next = 0;
          if( psp->firstrule==0 ){
            psp->firstrule = psp->lastrule = rp;
	  }else{
            psp->lastrule->next = rp;
            psp->lastrule = rp;
	  }
          psp->prevrule = rp;
	}
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( safe_isalpha(x[0]) ){
        if( psp->nrhs>=MAXRHS ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "Too many symbol on RHS or rule beginning at \"%s\".",
            x);
          psp->errorcnt++;
          psp->state = RESYNC_AFTER_RULE_ERROR;
	}else{
          psp->rhs[psp->nrhs] = Symbol_new(x);
          psp->alias[psp->nrhs] = 0;
          psp->nrhs++;
	}
      }else if( x[0]=='(' && psp->nrhs>0 ){
        psp->state = RHS_ALIAS_1;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Illegal character on RHS of rule: \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case RHS_ALIAS_1:
      if( safe_isalpha(x[0]) ){
        psp->alias[psp->nrhs-1] = x;
        psp->state = RHS_ALIAS_2;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "\"%s\" is not a valid alias for the RHS symbol \"%s\"\n",
          x,psp->rhs[psp->nrhs-1]->name);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case RHS_ALIAS_2:
      if( x[0]==')' ){
        psp->state = IN_RHS;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing \")\" following LHS alias name \"%s\".",psp->lhsalias);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case WAITING_FOR_DECL_KEYWORD:
      if( safe_isalpha(x[0]) ){
        psp->declkeyword = x;
        psp->declargslot = 0;
        psp->decllnslot = 0;
        psp->state = WAITING_FOR_DECL_ARG;
        if( strcmp(x,"name")==0 ){
          psp->declargslot = &(psp->gp->name);
	}else if( strcmp(x,"include")==0 ){
          psp->declargslot = &(psp->gp->include);
          psp->decllnslot = &psp->gp->includeln;
	}else if( strcmp(x,"code")==0 ){
          psp->declargslot = &(psp->gp->extracode);
          psp->decllnslot = &psp->gp->extracodeln;
	}else if( strcmp(x,"token_destructor")==0 ){
          psp->declargslot = &psp->gp->tokendest;
          psp->decllnslot = &psp->gp->tokendestln;
	}else if( strcmp(x,"token_prefix")==0 ){
          psp->declargslot = &psp->gp->tokenprefix;
	}else if( strcmp(x,"syntax_error")==0 ){
          psp->declargslot = &(psp->gp->error);
          psp->decllnslot = &psp->gp->errorln;
	}else if( strcmp(x,"parse_accept")==0 ){
          psp->declargslot = &(psp->gp->accept);
          psp->decllnslot = &psp->gp->acceptln;
	}else if( strcmp(x,"parse_failure")==0 ){
          psp->declargslot = &(psp->gp->failure);
          psp->decllnslot = &psp->gp->failureln;
	}else if( strcmp(x,"stack_overflow")==0 ){
          psp->declargslot = &(psp->gp->overflow);
          psp->decllnslot = &psp->gp->overflowln;
        }else if( strcmp(x,"extra_argument")==0 ){
          psp->declargslot = &(psp->gp->arg);
        }else if( strcmp(x,"token_type")==0 ){
          psp->declargslot = &(psp->gp->tokentype);
        }else if( strcmp(x,"stack_size")==0 ){
          psp->declargslot = &(psp->gp->stacksize);
        }else if( strcmp(x,"start_symbol")==0 ){
          psp->declargslot = &(psp->gp->start);
        }else if( strcmp(x,"left")==0 ){
          psp->preccounter++;
          psp->declassoc = LEFT;
          psp->state = WAITING_FOR_PRECEDENCE_SYMBOL;
        }else if( strcmp(x,"right")==0 ){
          psp->preccounter++;
          psp->declassoc = RIGHT;
          psp->state = WAITING_FOR_PRECEDENCE_SYMBOL;
        }else if( strcmp(x,"nonassoc")==0 ){
          psp->preccounter++;
          psp->declassoc = NONE;
          psp->state = WAITING_FOR_PRECEDENCE_SYMBOL;
	}else if( strcmp(x,"destructor")==0 ){
          psp->state = WAITING_FOR_DESTRUCTOR_SYMBOL;
	}else if( strcmp(x,"type")==0 ){
          psp->state = WAITING_FOR_DATATYPE_SYMBOL;
        }else{
          ErrorMsg(psp->filename,psp->tokenlineno,
            "Unknown declaration keyword: \"%%%s\".",x);
          psp->errorcnt++;
          psp->state = RESYNC_AFTER_DECL_ERROR;
	}
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Illegal declaration keyword: \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_DESTRUCTOR_SYMBOL:
      if( !safe_isalpha(x[0]) ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Symbol name missing after %destructor keyword");
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }else{
        struct symbol *sp = Symbol_new(x);
        psp->declargslot = &sp->destructor;
        psp->decllnslot = &sp->destructorln;
        psp->state = WAITING_FOR_DECL_ARG;
      }
      break;
    case WAITING_FOR_DATATYPE_SYMBOL:
      if( !safe_isalpha(x[0]) ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Symbol name missing after %destructor keyword");
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }else{
        struct symbol *sp = Symbol_new(x);
        psp->declargslot = &sp->datatype;
        psp->decllnslot = 0;
        psp->state = WAITING_FOR_DECL_ARG;
      }
      break;
    case WAITING_FOR_PRECEDENCE_SYMBOL:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( safe_isupper(x[0]) ){
        struct symbol *sp;
        sp = Symbol_new(x);
        if( sp->prec>=0 ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "Symbol \"%s\" has already be given a precedence.",x);
          psp->errorcnt++;
	}else{
          sp->prec = psp->preccounter;
          sp->assoc = psp->declassoc;
	}
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Can't assign a precedence to \"%s\".",x);
        psp->errorcnt++;
      }
      break;
    case WAITING_FOR_DECL_ARG:
      if( (x[0]=='{' || x[0]=='\"' || safe_isalnum(x[0])) ){
        if( *(psp->declargslot)!=0 ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "The argument \"%s\" to declaration \"%%%s\" is not the first.",
            x[0]=='\"' ? &x[1] : x,psp->declkeyword);
          psp->errorcnt++;
          psp->state = RESYNC_AFTER_DECL_ERROR;
	}else{
          *(psp->declargslot) = (x[0]=='\"' || x[0]=='{') ? &x[1] : x;
          if( psp->decllnslot ) *psp->decllnslot = psp->tokenlineno;
          psp->state = WAITING_FOR_DECL_OR_RULE;
	}
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Illegal argument to %%%s: %s",psp->declkeyword,x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case RESYNC_AFTER_RULE_ERROR:
/*      if( x[0]=='.' ) psp->state = WAITING_FOR_DECL_OR_RULE;
**      break; */
    case RESYNC_AFTER_DECL_ERROR:
      if( x[0]=='.' ) psp->state = WAITING_FOR_DECL_OR_RULE;
      if( x[0]=='%' ) psp->state = WAITING_FOR_DECL_KEYWORD;
      break;
  }
}

/* In spite of its name, this function is really a scanner.  It read
** in the entire input file (all at once) then tokenizes it.  Each
** token is passed to the function "parseonetoken" which builds all
** the appropriate data structures in the global state vector "gp".
*/
void Parse(struct lemon *gp)
{
  struct pstate ps;
  FILE *fp;
  char *filebuf;
  int filesize;
  int lineno;
  char c;
  char *cp, *nextcp;
  int startline = 0;

  ps.gp = gp;
  ps.filename = gp->filename;
  ps.errorcnt = 0;
  ps.state = INITIALIZE;

  /* Begin by reading the input file */
  fp = fopen(ps.filename,"rb");
  if( fp==0 ){
    ErrorMsg(ps.filename,0,"Can't open this file for reading.");
    gp->errorcnt++;
    return;
  }
  fseek(fp,0,2);
  filesize = ftell(fp);
  rewind(fp);
  filebuf = (char *)malloc( filesize+1 );
  if( filebuf==0 ){
    ErrorMsg(ps.filename,0,"Can't allocate %d of memory to hold this file.",
      filesize+1);
    gp->errorcnt++;
    return;
  }
  if( fread(filebuf,1,filesize,fp)!=filesize ){
    ErrorMsg(ps.filename,0,"Can't read in all %d bytes of this file.",
      filesize);
    free(filebuf);
    gp->errorcnt++;
    return;
  }
  fclose(fp);
  filebuf[filesize] = 0;

  /* Now scan the text of the input file */
  lineno = 1;
  for(cp=filebuf; (c= *cp)!=0; ){
    if( c=='\n' ) lineno++;              /* Keep track of the line number */
    if( safe_isspace(c) ){ cp++; continue; }  /* Skip all white space */
    if( c=='/' && cp[1]=='/' ){          /* Skip C++ style comments */
      cp+=2;
      while( (c= *cp)!=0 && c!='\n' ) cp++;
      continue;
    }
    if( c=='/' && cp[1]=='*' ){          /* Skip C style comments */
      cp+=2;
      while( (c= *cp)!=0 && (c!='/' || cp[-1]!='*') ){
        if( c=='\n' ) lineno++;
        cp++;
      }
      if( c ) cp++;
      continue;
    }
    ps.tokenstart = cp;                /* Mark the beginning of the token */
    ps.tokenlineno = lineno;           /* Linenumber on which token begins */
    if( c=='\"' ){                     /* String literals */
      cp++;
      while( (c= *cp)!=0 && c!='\"' ){
        if( c=='\n' ) lineno++;
        cp++;
      }
      if( c==0 ){
        ErrorMsg(ps.filename,startline,
"String starting on this line is not terminated before the end of the file.");
        ps.errorcnt++;
        nextcp = cp;
      }else{
        nextcp = cp+1;
      }
    }else if( c=='{' ){               /* A block of C code */
      int level;
      cp++;
      for(level=1; (c= *cp)!=0 && (level>1 || c!='}'); cp++){
        if( c=='\n' ) lineno++;
        else if( c=='{' ) level++;
        else if( c=='}' ) level--;
        else if( c=='/' && cp[1]=='*' ){  /* Skip comments */
          char prevc;
          cp = &cp[2];
          prevc = 0;
          while( (c= *cp)!=0 && (c!='/' || prevc!='*') ){
            if( c=='\n' ) lineno++;
            prevc = c;
            cp++;
	  }
	}else if( c=='/' && cp[1]=='/' ){  /* Skip C++ style comments too */
          cp = &cp[2];
          while( (c= *cp)!=0 && c!='\n' ) cp++;
          if( c ) lineno++;
	}else if( c=='\'' || c=='\"' ){    /* String a character literals */
          char startchar, prevc;
          startchar = c;
          prevc = 0;
          for(cp++; (c= *cp)!=0 && (c!=startchar || prevc=='\\'); cp++){
            if( c=='\n' ) lineno++;
            if( prevc=='\\' ) prevc = 0;
            else              prevc = c;
	  }
	}
      }
      if( c==0 ){
        ErrorMsg(ps.filename,startline,
"C code starting on this line is not terminated before the end of the file.");
        ps.errorcnt++;
        nextcp = cp;
      }else{
        nextcp = cp+1;
      }
    }else if( safe_isalnum(c) ){          /* Identifiers */
      while( (c= *cp)!=0 && (safe_isalnum(c) || c=='_') ) cp++;
      nextcp = cp;
    }else if( c==':' && cp[1]==':' && cp[2]=='=' ){ /* The operator "::=" */
      cp += 3;
      nextcp = cp;
    }else{                          /* All other (one character) operators */
      cp++;
      nextcp = cp;
    }
    c = *cp;
    *cp = 0;                        /* Null terminate the token */
    parseonetoken(&ps);             /* Parse the token */
    *cp = c;                        /* Restore the buffer */
    cp = nextcp;
  }
  free(filebuf);                    /* Release the buffer after parsing */
  gp->rule = ps.firstrule;
  gp->errorcnt = ps.errorcnt;
}
/*************************** From the file "plink.c" *********************/
/*
** Routines processing configuration follow-set propagation links
** in the LEMON parser generator.
*/
static struct plink *plink_freelist = 0;

/* Allocate a new plink */
struct plink *Plink_new(void){
  struct plink *new;

  if( plink_freelist==0 ){
    int i;
    int amt = 100;
    plink_freelist = (struct plink *)malloc( sizeof(struct plink)*amt );
    if( plink_freelist==0 ){
      fprintf(stderr,
      "Unable to allocate memory for a new follow-set propagation link.\n");
      exit(1);
    }
    for(i=0; i<amt-1; i++) plink_freelist[i].next = &plink_freelist[i+1];
    plink_freelist[amt-1].next = 0;
  }
  new = plink_freelist;
  plink_freelist = plink_freelist->next;
  return new;
}

/* Add a plink to a plink list */
void Plink_add(struct plink **plpp, struct config *cfp)
{
  struct plink *new;
  new = Plink_new();
  new->next = *plpp;
  *plpp = new;
  new->cfp = cfp;
}

/* Transfer every plink on the list "from" to the list "to" */
void Plink_copy(struct plink **to, struct plink *from)
{
  struct plink *nextpl;
  while( from ){
    nextpl = from->next;
    from->next = *to;
    *to = from;
    from = nextpl;
  }
}

/* Delete every plink on the list */
void Plink_delete(struct plink *plp)
{
  struct plink *nextpl;

  while( plp ){
    nextpl = plp->next;
    plp->next = plink_freelist;
    plink_freelist = plp;
    plp = nextpl;
  }
}
/*********************** From the file "report.c" **************************/
/*
** Procedures for generating reports and tables in the LEMON parser generator.
*/

/* Generate a filename with the given suffix.  Space to hold the
** name comes from malloc() and must be freed by the calling
** function.
*/
PRIVATE char *file_makename(char *pattern, char *suffix)
{
  char *name;
  char *cp;

  name = malloc( strlen(pattern) + strlen(suffix) + 5 );
  if( name==0 ){
    fprintf(stderr,"Can't allocate space for a filename.\n");
    exit(1);
  }
  strcpy(name,pattern);
  cp = strrchr(name,'.');
  if( cp ) *cp = 0;
  strcat(name,suffix);
  return name;
}

/* Generate a filename with the given suffix.  Uses only
** the basename of the input file, not the entire path. This
** is useful for creating output files when using outdirname.
** Space to hold this name comes from malloc() and must be
** freed by the calling function.
*/
PRIVATE char *file_makename_using_basename(struct lemon *lemp, char *suffix)
{
	return file_makename(lemp->basename, suffix);
}

/* Open a file with a name based on the name of the input file,
** but with a different (specified) suffix, and return a pointer
** to the stream. Prepend outdirname for both reads and writes, because
** the only time we read is when checking for an already-produced
** header file, which should exist in the output directory, not the
** input directory. If we ever need to file_open(,,"r") on the input
** side, we should add another arg to file_open() indicating which
** directory, ("input, "output", or "other") we should deal with.
*/
PRIVATE FILE *file_open(struct lemon *lemp, char *suffix, char *mode)
{
  FILE *fp;
  char *name;

  if( lemp->outname ) free(lemp->outname);
  name = file_makename_using_basename(lemp, suffix);

  if ( lemp->outdirname != NULL ) {
	  lemp->outname = malloc( strlen(lemp->outdirname) + strlen(name) + 2);
	  if ( lemp->outname == 0 ) {
		  fprintf(stderr, "Can't allocate space for dir/filename");
		  exit(1);
	  }
	  strcpy(lemp->outname, lemp->outdirname);
#ifdef __WIN32__
	  strcat(lemp->outname, "\\");
#else
	  strcat(lemp->outname, "/");
#endif
	  strcat(lemp->outname, name);
	  free(name);
  }
  else {
	 lemp->outname = name;
  }

  fp = fopen(lemp->outname,mode);
  if( fp==0 && *mode=='w' ){
    fprintf(stderr,"Can't open file \"%s\".\n",lemp->outname);
    lemp->errorcnt++;
    return 0;
  }
  return fp;
}

/* Duplicate the input file without comments and without actions 
** on rules */
void Reprint(struct lemon *lemp)
{
  struct rule *rp;
  struct symbol *sp;
  int i, j, maxlen, len, ncolumns, skip;
  printf("// Reprint of input file \"%s\".\n// Symbols:\n",lemp->filename);
  maxlen = 10;
  for(i=0; i<lemp->nsymbol; i++){
    sp = lemp->symbols[i];
    len = strlen(sp->name);
    if( len>maxlen ) maxlen = len;
  }
  ncolumns = 76/(maxlen+5);
  if( ncolumns<1 ) ncolumns = 1;
  skip = (lemp->nsymbol + ncolumns - 1)/ncolumns;
  for(i=0; i<skip; i++){
    printf("//");
    for(j=i; j<lemp->nsymbol; j+=skip){
      sp = lemp->symbols[j];
      assert( sp->index==j );
      printf(" %3d %-*.*s",j,maxlen,maxlen,sp->name);
    }
    printf("\n");
  }
  for(rp=lemp->rule; rp; rp=rp->next){
    printf("%s",rp->lhs->name);
/*    if( rp->lhsalias ) printf("(%s)",rp->lhsalias); */
    printf(" ::=");
    for(i=0; i<rp->nrhs; i++){
      printf(" %s",rp->rhs[i]->name);
/*      if( rp->rhsalias[i] ) printf("(%s)",rp->rhsalias[i]); */
    }
    printf(".");
    if( rp->precsym ) printf(" [%s]",rp->precsym->name);
/*    if( rp->code ) printf("\n    %s",rp->code); */
    printf("\n");
  }
}

void ConfigPrint(FILE *fp, struct config *cfp)
{
  struct rule *rp;
  int i;
  rp = cfp->rp;
  fprintf(fp,"%s ::=",rp->lhs->name);
  for(i=0; i<=rp->nrhs; i++){
    if( i==cfp->dot ) fprintf(fp," *");
    if( i==rp->nrhs ) break;
    fprintf(fp," %s",rp->rhs[i]->name);
  }
}

/* #define TEST */
#ifdef TEST
/* Print a set */
PRIVATE void SetPrint(FILE *out, char *set, struct lemon *lemp)
{
  int i;
  char *spacer;
  spacer = "";
  fprintf(out,"%12s[","");
  for(i=0; i<lemp->nterminal; i++){
    if( SetFind(set,i) ){
      fprintf(out,"%s%s",spacer,lemp->symbols[i]->name);
      spacer = " ";
    }
  }
  fprintf(out,"]\n");
}

/* Print a plink chain */
PRIVATE void PlinkPrint(FILE *out, struct plink *plp, char *tag)
{
  while( plp ){
    fprintf(out,"%12s%s (state %2d) ","",tag,plp->cfp->stp->index);
    ConfigPrint(out,plp->cfp);
    fprintf(out,"\n");
    plp = plp->next;
  }
}
#endif

/* Print an action to the given file descriptor.  Return FALSE if
** nothing was actually printed.
*/
int PrintAction(struct action *ap, FILE *fp, int indent){
  int result = 1;
  switch( ap->type ){
    case SHIFT:
      fprintf(fp,"%*s shift  %d",indent,ap->sp->name,ap->x.stp->index);
      break;
    case REDUCE:
      fprintf(fp,"%*s reduce %d",indent,ap->sp->name,ap->x.rp->index);
      break;
    case ACCEPT:
      fprintf(fp,"%*s accept",indent,ap->sp->name);
      break;
    case ERROR:
      fprintf(fp,"%*s error",indent,ap->sp->name);
      break;
    case CONFLICT:
      fprintf(fp,"%*s reduce %-3d ** Parsing conflict **",
        indent,ap->sp->name,ap->x.rp->index);
      break;
    case SH_RESOLVED:
    case RD_RESOLVED:
    case NOT_USED:
      result = 0;
      break;
  }
  return result;
}

/* Generate the "y.output" log file */
void ReportOutput(struct lemon *lemp)
{
  int i;
  struct state *stp;
  struct config *cfp;
  struct action *ap;
  FILE *fp;

  fp = file_open(lemp,".out","w");
  if( fp==0 ) return;
  fprintf(fp," \b");
  for(i=0; i<lemp->nstate; i++){
    stp = lemp->sorted[i];
    fprintf(fp,"State %d:\n",stp->index);
    if( lemp->basisflag ) cfp=stp->bp;
    else                  cfp=stp->cfp;
    while( cfp ){
      char buf[20];
      if( cfp->dot==cfp->rp->nrhs ){
        sprintf(buf,"(%d)",cfp->rp->index);
        fprintf(fp,"    %5s ",buf);
      }else{
        fprintf(fp,"          ");
      }
      ConfigPrint(fp,cfp);
      fprintf(fp,"\n");
#ifdef TEST
      SetPrint(fp,cfp->fws,lemp);
      PlinkPrint(fp,cfp->fplp,"To  ");
      PlinkPrint(fp,cfp->bplp,"From");
#endif
      if( lemp->basisflag ) cfp=cfp->bp;
      else                  cfp=cfp->next;
    }
    fprintf(fp,"\n");
    for(ap=stp->ap; ap; ap=ap->next){
      if( PrintAction(ap,fp,30) ) fprintf(fp,"\n");
    }
    fprintf(fp,"\n");
  }
  fclose(fp);
  return;
}

/* Search for the file "name" which is in the same directory as
** the exacutable */
PRIVATE char *pathsearch(char *argv0, char *name, int modemask)
{
  char *pathlist;
  char *path,*cp;
  char c;

#ifdef __WIN32__
  cp = strrchr(argv0,'\\');
#else
  cp = strrchr(argv0,'/');
#endif
  if( cp ){
    c = *cp;
    *cp = 0;
    path = (char *)malloc( strlen(argv0) + strlen(name) + 2 );
    if( path ) sprintf(path,"%s/%s",argv0,name);
    *cp = c;
  }else{
    extern char *getenv();
    pathlist = getenv("PATH");
    if( pathlist==0 ) pathlist = ".:/bin:/usr/bin";
    path = (char *)malloc( strlen(pathlist)+strlen(name)+2 );
    if( path!=0 ){
      while( *pathlist ){
        cp = strchr(pathlist,':');
        if( cp==0 ) cp = &pathlist[strlen(pathlist)];
        c = *cp;
        *cp = 0;
        sprintf(path,"%s/%s",pathlist,name);
        *cp = c;
        if( c==0 ) pathlist = "";
        else pathlist = &cp[1];
        if( access(path,modemask)==0 ) break;
      }
    }
  }
  return path;
}

/* Given an action, compute the integer value for that action
** which is to be put in the action table of the generated machine.
** Return negative if no action should be generated.
*/
PRIVATE int compute_action(struct lemon *lemp, struct action *ap)
{
  int act;
  switch( ap->type ){
    case SHIFT:  act = ap->x.stp->index;               break;
    case REDUCE: act = ap->x.rp->index + lemp->nstate; break;
    case ERROR:  act = lemp->nstate + lemp->nrule;     break;
    case ACCEPT: act = lemp->nstate + lemp->nrule + 1; break;
    default:     act = -1; break;
  }
  return act;
}

#define LINESIZE 1000
/* The next cluster of routines are for reading the template file
** and writing the results to the generated parser */
/* The first function transfers data from "in" to "out" until
** a line is seen which begins with "%%".  The line number is
** tracked.
**
** if name!=0, then any word that begin with "Parse" is changed to
** begin with *name instead.
*/
PRIVATE void tplt_xfer(char *name, FILE *in, FILE *out, int *lineno)
{
  int i, iStart;
  char line[LINESIZE];
  while( fgets(line,LINESIZE,in) && (line[0]!='%' || line[1]!='%') ){
    (*lineno)++;
    iStart = 0;
    if( name ){
      for(i=0; line[i]; i++){
        if( line[i]=='P' && strncmp(&line[i],"Parse",5)==0
          && (i==0 || !safe_isalpha(line[i-1]))
        ){
          if( i>iStart ) fprintf(out,"%.*s",i-iStart,&line[iStart]);
          fprintf(out,"%s",name);
          i += 4;
          iStart = i+1;
        }
      }
    }
    fprintf(out,"%s",&line[iStart]);
  }
}

/* The next function finds the template file and opens it, returning
** a pointer to the opened file. */
PRIVATE FILE *tplt_open(struct lemon *lemp)
{
  static char templatename[] = "lempar.c";
  char buf[1000];
  FILE *in;
  char *tpltname;
  char *cp;

  if (lemp->templatename) {
	  tpltname = lemp->templatename;
  }
  else {
	  cp = strrchr(lemp->filename,'.');
	  if( cp ){
	    sprintf(buf,"%.*s.lt",(int)(cp - lemp->filename),lemp->filename);
	  }else{
	    sprintf(buf,"%s.lt",lemp->filename);
	  }
	  if( access(buf,004)==0 ){
	    tpltname = buf;
	  }else{
	    tpltname = pathsearch(lemp->argv0,templatename,0);
	  }
  }
  if( tpltname==0 ){
    fprintf(stderr,"Can't find the parser driver template file \"%s\".\n",
    templatename);
    lemp->errorcnt++;
    return 0;
  }
  in = fopen(tpltname,"r");
  if( in==0 ){
    fprintf(stderr,"Can't open the template file \"%s\".\n",templatename);
    lemp->errorcnt++;
    return 0;
  }
  return in;
}

/* Print a string to the file and keep the linenumber up to date */
PRIVATE void tplt_print(FILE *out, struct lemon *lemp, char *str,
    int strln, int *lineno)
{
  if( str==0 ) return;
  fprintf(out,"#line %d \"%s\"\n",strln,lemp->filename); (*lineno)++;
  while( *str ){
    if( *str=='\n' ) (*lineno)++;
    putc(*str,out);
    str++;
  }
  fprintf(out,"\n#line %d \"%s\"\n",*lineno+2,lemp->outname); (*lineno)+=2;
  return;
}

/*
** The following routine emits code for the destructor for the
** symbol sp
*/
void emit_destructor_code(FILE *out, struct symbol *sp, struct lemon *lemp,
    int *lineno)
{
 char *cp;

 int linecnt = 0;
 if( sp->type==TERMINAL ){
   cp = lemp->tokendest;
   if( cp==0 ) return;
   fprintf(out,"#line %d \"%s\"\n{",lemp->tokendestln,lemp->filename);
 }else{
   cp = sp->destructor;
   if( cp==0 ) return;
   fprintf(out,"#line %d \"%s\"\n{",sp->destructorln,lemp->filename);
 }
 for(; *cp; cp++){
   if( *cp=='$' && cp[1]=='$' ){
     fprintf(out,"(yypminor->yy%d)",sp->dtnum);
     cp++;
     continue;
   }
   if( *cp=='\n' ) linecnt++;
   fputc(*cp,out);
 }
 (*lineno) += 3 + linecnt;
 fprintf(out,"}\n#line %d \"%s\"\n",*lineno,lemp->outname);
 return;
}

/*
** Return TRUE (non-zero) if the given symbol has a distructor.
*/
int has_destructor(struct symbol *sp, struct lemon *lemp)
{
  int ret;
  if( sp->type==TERMINAL ){
    ret = lemp->tokendest!=0;
  }else{
    ret = sp->destructor!=0;
  }
  return ret;
}

/* 
** Generate code which executes when the rule "rp" is reduced.  Write
** the code to "out".  Make sure lineno stays up-to-date.
*/
PRIVATE void emit_code(FILE *out, struct rule *rp, struct lemon *lemp,
    int *lineno)
{
 char *cp, *xp;
 int linecnt = 0;
 int i;
 char lhsused = 0;    /* True if the LHS element has been used */
 char used[MAXRHS];   /* True for each RHS element which is used */

 for(i=0; i<rp->nrhs; i++) used[i] = 0;
 lhsused = 0;

 /* Generate code to do the reduce action */
 if( rp->code ){
   fprintf(out,"#line %d \"%s\"\n{",rp->line,lemp->filename);
   for(cp=rp->code; *cp; cp++){
     if( safe_isalpha(*cp) && (cp==rp->code || !safe_isalnum(cp[-1])) ){
       char saved;
       for(xp= &cp[1]; safe_isalnum(*xp); xp++);
       saved = *xp;
       *xp = 0;
       if( rp->lhsalias && strcmp(cp,rp->lhsalias)==0 ){
         fprintf(out,"yygotominor.yy%d",rp->lhs->dtnum);
         cp = xp;
         lhsused = 1;
       }else{
         for(i=0; i<rp->nrhs; i++){
           if( rp->rhsalias[i] && strcmp(cp,rp->rhsalias[i])==0 ){
             fprintf(out,"yymsp[%d].minor.yy%d",i-rp->nrhs+1,rp->rhs[i]->dtnum);
             cp = xp;
             used[i] = 1;
             break;
           }
         }
       }
       *xp = saved;
     }
     if( *cp=='\n' ) linecnt++;
     fputc(*cp,out);
   } /* End loop */
   (*lineno) += 3 + linecnt;
   fprintf(out,"}\n#line %d \"%s\"\n",*lineno,lemp->outname);
 } /* End if( rp->code ) */

 /* Check to make sure the LHS has been used */
 if( rp->lhsalias && !lhsused ){
   ErrorMsg(lemp->filename,rp->ruleline,
     "Label \"%s\" for \"%s(%s)\" is never used.",
       rp->lhsalias,rp->lhs->name,rp->lhsalias);
   lemp->errorcnt++;
 }

 /* Generate destructor code for RHS symbols which are not used in the
 ** reduce code */
 for(i=0; i<rp->nrhs; i++){
   if( rp->rhsalias[i] && !used[i] ){
     ErrorMsg(lemp->filename,rp->ruleline,
       "Label $%s$ for \"%s(%s)\" is never used.",
       rp->rhsalias[i],rp->rhs[i]->name,rp->rhsalias[i]);
     lemp->errorcnt++;
   }else if( rp->rhsalias[i]==0 ){
     if( has_destructor(rp->rhs[i],lemp) ){
       fprintf(out,"  yy_destructor(%d,&yymsp[%d].minor);\n",
          rp->rhs[i]->index,i-rp->nrhs+1); (*lineno)++;
     }else{
       fprintf(out,"        /* No destructor defined for %s */\n",
        rp->rhs[i]->name);
        (*lineno)++;
     }
   }
 }
 return;
}

/*
** Print the definition of the union used for the parser's data stack.
** This union contains fields for every possible data type for tokens
** and nonterminals.  In the process of computing and printing this
** union, also set the ".dtnum" field of every terminal and nonterminal
** symbol.
*/
void print_stack_union(
    FILE *out,              /* The output stream */
    struct lemon *lemp,     /* The main info structure for this parser */
    int *plineno,           /* Pointer to the line number */
    int mhflag)             /* True if generating makeheaders output */
{
  int lineno = *plineno;    /* The line number of the output */
  char **types;             /* A hash table of datatypes */
  int arraysize;            /* Size of the "types" array */
  int maxdtlength;          /* Maximum length of any ".datatype" field. */
  char *stddt;              /* Standardized name for a datatype */
  int i,j;                  /* Loop counters */
  int hash;                 /* For hashing the name of a type */
  char *name;               /* Name of the parser */

  /* Allocate and initialize types[] and allocate stddt[] */
  arraysize = lemp->nsymbol * 2;
  types = (char**)malloc( arraysize * sizeof(char*) );
  for(i=0; i<arraysize; i++) types[i] = 0;
  maxdtlength = 0;
  for(i=0; i<lemp->nsymbol; i++){
    int len;
    struct symbol *sp = lemp->symbols[i];
    if( sp->datatype==0 ) continue;
    len = strlen(sp->datatype);
    if( len>maxdtlength ) maxdtlength = len;
  }
  stddt = (char*)malloc( maxdtlength*2 + 1 );
  if( types==0 || stddt==0 ){
    fprintf(stderr,"Out of memory.\n");
    exit(1);
  }

  /* Build a hash table of datatypes. The ".dtnum" field of each symbol
  ** is filled in with the hash index plus 1.  A ".dtnum" value of 0 is
  ** used for terminal symbols and for nonterminals which don't specify
  ** a datatype using the %type directive. */
  for(i=0; i<lemp->nsymbol; i++){
    struct symbol *sp = lemp->symbols[i];
    char *cp;
    if( sp==lemp->errsym ){
      sp->dtnum = arraysize+1;
      continue;
    }
    if( sp->type!=NONTERMINAL || sp->datatype==0 ){
      sp->dtnum = 0;
      continue;
    }
    cp = sp->datatype;
    j = 0;
    while( safe_isspace(*cp) ) cp++;
    while( *cp ) stddt[j++] = *cp++;
    while( j>0 && safe_isspace(stddt[j-1]) ) j--;
    stddt[j] = 0;
    hash = 0;
    for(j=0; stddt[j]; j++){
      hash = hash*53 + stddt[j];
    }
    if( hash<0 ) hash = -hash;
    hash = hash%arraysize;
    while( types[hash] ){
      if( strcmp(types[hash],stddt)==0 ){
        sp->dtnum = hash + 1;
        break;
      }
      hash++;
      if( hash>=arraysize ) hash = 0;
    }
    if( types[hash]==0 ){
      sp->dtnum = hash + 1;
      types[hash] = (char*)malloc( strlen(stddt)+1 );
      if( types[hash]==0 ){
        fprintf(stderr,"Out of memory.\n");
        exit(1);
      }
      strcpy(types[hash],stddt);
    }
  }

  /* Print out the definition of YYTOKENTYPE and YYMINORTYPE */
  name = lemp->name ? lemp->name : "Parse";
  lineno = *plineno;
  if( mhflag ){ fprintf(out,"#if INTERFACE\n"); lineno++; }
  fprintf(out,"#define %sTOKENTYPE %s\n",name,
    lemp->tokentype?lemp->tokentype:"void*");  lineno++;
  if( mhflag ){ fprintf(out,"#endif\n"); lineno++; }
  fprintf(out,"typedef union {\n"); lineno++;
  fprintf(out,"  %sTOKENTYPE yy0;\n",name); lineno++;
  for(i=0; i<arraysize; i++){
    if( types[i]==0 ) continue;
    fprintf(out,"  %s yy%d;\n",types[i],i+1); lineno++;
    free(types[i]);
  }
  fprintf(out,"  int yy%d;\n",lemp->errsym->dtnum); lineno++;
  free(stddt);
  free(types);
  fprintf(out,"} YYMINORTYPE;\n"); lineno++;
  *plineno = lineno;
}

/* Generate C source code for the parser */
void ReportTable(
    struct lemon *lemp,
    int mhflag)     /* Output in makeheaders format if true */
{
  FILE *out, *in;
  char line[LINESIZE];
  int  lineno;
  struct state *stp;
  struct action *ap;
  struct rule *rp;
  int i;
  int tablecnt;
  char *name;

  in = tplt_open(lemp);
  if( in==0 ) return;
  out = file_open(lemp,".c","w");
  if( out==0 ){
    fclose(in);
    return;
  }
  lineno = 1;
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate the include code, if any */
  tplt_print(out,lemp,lemp->include,lemp->includeln,&lineno);
  if( mhflag ){
    char *name = file_makename_using_basename(lemp, ".h");
    fprintf(out,"#include \"%s\"\n", name); lineno++;
    free(name);
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate #defines for all tokens */
  if( mhflag ){
    char *prefix;
    fprintf(out,"#if INTERFACE\n"); lineno++;
    if( lemp->tokenprefix ) prefix = lemp->tokenprefix;
    else                    prefix = "";
    for(i=1; i<lemp->nterminal; i++){
      fprintf(out,"#define %s%-30s %2d\n",prefix,lemp->symbols[i]->name,i);
      lineno++;
    }
    fprintf(out,"#endif\n"); lineno++;
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate the defines */
  fprintf(out,"/* \001 */\n");
  fprintf(out,"#define YYCODETYPE %s\n",
    lemp->nsymbol>250?"int":"unsigned char");  lineno++;
  fprintf(out,"#define YYNOCODE %d\n",lemp->nsymbol+1);  lineno++;
  fprintf(out,"#define YYACTIONTYPE %s\n",
    lemp->nstate+lemp->nrule>250?"int":"unsigned char");  lineno++;
  print_stack_union(out,lemp,&lineno,mhflag);
  if( lemp->stacksize ){
    if( atoi(lemp->stacksize)<=0 ){
      ErrorMsg(lemp->filename,0,
"Illegal stack size: [%s].  The stack size should be an integer constant.",
        lemp->stacksize);
      lemp->errorcnt++;
      lemp->stacksize = "100";
    }
    fprintf(out,"#define YYSTACKDEPTH %s\n",lemp->stacksize);  lineno++;
  }else{
    fprintf(out,"#define YYSTACKDEPTH 100\n");  lineno++;
  }
  if( mhflag ){
    fprintf(out,"#if INTERFACE\n"); lineno++;
  }
  name = lemp->name ? lemp->name : "Parse";
  if( lemp->arg && lemp->arg[0] ){
    int i;
    i = strlen(lemp->arg);
    while( i>=1 && safe_isspace(lemp->arg[i-1]) ) i--;
    while( i>=1 && safe_isalnum(lemp->arg[i-1]) ) i--;
    fprintf(out,"#define %sARGDECL ,%s\n",name,&lemp->arg[i]);  lineno++;
    fprintf(out,"#define %sXARGDECL %s;\n",name,lemp->arg);  lineno++;
    fprintf(out,"#define %sANSIARGDECL ,%s\n",name,lemp->arg);  lineno++;
  }else{
    fprintf(out,"#define %sARGDECL\n",name);  lineno++;
    fprintf(out,"#define %sXARGDECL\n",name);  lineno++;
    fprintf(out,"#define %sANSIARGDECL\n",name);  lineno++;
  }
  if( mhflag ){
    fprintf(out,"#endif\n"); lineno++;
  }
  fprintf(out,"#define YYNSTATE %d\n",lemp->nstate);  lineno++;
  fprintf(out,"#define YYNRULE %d\n",lemp->nrule);  lineno++;
  fprintf(out,"#define YYERRORSYMBOL %d\n",lemp->errsym->index);  lineno++;
  fprintf(out,"#define YYERRSYMDT yy%d\n",lemp->errsym->dtnum);  lineno++;
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate the action table.
  **
  ** Each entry in the action table is an element of the following 
  ** structure:
  **   struct yyActionEntry {
  **       YYCODETYPE            lookahead;
  **       YYACTIONTYPE          action;
  **       struct yyActionEntry *next;
  **   }
  **
  ** The entries are grouped into hash tables, one hash table for each
  ** parser state.  The hash table has a size which is the smallest
  ** power of two needed to hold all entries.
  */
  tablecnt = 0;

  /* Loop over parser states */
  for(i=0; i<lemp->nstate; i++){
    int tablesize;              /* size of the hash table */
    int j,k;                    /* Loop counter */
    int collide[2048];          /* The collision chain for the table */
    struct action *table[2048]; /* Build the hash table here */

    /* Find the number of actions and initialize the hash table */
    stp = lemp->sorted[i];
    stp->tabstart = tablecnt;
    stp->naction = 0;
    for(ap=stp->ap; ap; ap=ap->next){
      if( ap->sp->index!=lemp->nsymbol && compute_action(lemp,ap)>=0 ){
        stp->naction++;
      }
    }
    tablesize = 1;
    while( tablesize<stp->naction ) tablesize += tablesize;
    assert( tablesize<= sizeof(table)/sizeof(table[0]) );
    for(j=0; j<tablesize; j++){
      table[j] = 0;
      collide[j] = -1;
    }

    /* Hash the actions into the hash table */
    stp->tabdfltact = lemp->nstate + lemp->nrule;
    for(ap=stp->ap; ap; ap=ap->next){
      int action = compute_action(lemp,ap);
      int h;
      if( ap->sp->index==lemp->nsymbol ){
        stp->tabdfltact = action;
      }else if( action>=0 ){
        h = ap->sp->index & (tablesize-1);
        ap->collide = table[h];
        table[h] = ap;
      }
    }

    /* Resolve collisions */
    for(j=k=0; j<tablesize; j++){
      if( table[j] && table[j]->collide ){
        while( table[k] ) k++;
        table[k] = table[j]->collide;
        collide[j] = k;
        table[j]->collide = 0;
        if( k<j ) j = k-1;
      }
    }

    /* Print the hash table */
    fprintf(out,"/* State %d */\n",stp->index); lineno++;
    for(j=0; j<tablesize; j++){
      if( table[j]==0 ){
        fprintf(out,
          "  {YYNOCODE,0,0}, /* Unused */\n");
      }else{
        fprintf(out,"  {%4d,%4d, ",
          table[j]->sp->index,
          compute_action(lemp,table[j]));
        if( collide[j]>=0 ){
          fprintf(out,"&yyActionTable[%4d] }, /* ",
            collide[j] + tablecnt);
        }else{
          fprintf(out,"0                    }, /* ");
        }
        PrintAction(table[j],out,22);
        fprintf(out," */\n"); 
      }
      lineno++;
    }

    /* Update the table count */
    tablecnt += tablesize;
  }
  tplt_xfer(lemp->name,in,out,&lineno);
  lemp->tablesize = tablecnt;

  /* Generate the state table
  **
  ** Each entry is an element of the following structure:
  **    struct yyStateEntry {
  **      struct yyActionEntry *hashtbl;
  **      int mask;
  **      YYACTIONTYPE actionDefault;
  **    }
  */
  for(i=0; i<lemp->nstate; i++){
    int tablesize;
    stp = lemp->sorted[i];
    tablesize = 1;
    while( tablesize<stp->naction ) tablesize += tablesize;
    fprintf(out,"  { &yyActionTable[%d], %d, %d},\n",
      stp->tabstart,
      tablesize - 1,
      stp->tabdfltact); lineno++;
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate a table containing the symbolic name of every symbol */
  for(i=0; i<lemp->nsymbol; i++){
    sprintf(line,"\"%s\",",lemp->symbols[i]->name);
    fprintf(out,"  %-15s",line);
    if( (i&3)==3 ){ fprintf(out,"\n"); lineno++; }
  }
  if( (i&3)!=0 ){ fprintf(out,"\n"); lineno++; }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which executes every time a symbol is popped from
  ** the stack while processing errors or while destroying the parser. 
  ** (In other words, generate the %destructor actions) */
  if( lemp->tokendest ){
    for(i=0; i<lemp->nsymbol; i++){
      struct symbol *sp = lemp->symbols[i];
      if( sp==0 || sp->type!=TERMINAL ) continue;
      fprintf(out,"    case %d:\n",sp->index); lineno++;
    }
    for(i=0; i<lemp->nsymbol && lemp->symbols[i]->type!=TERMINAL; i++);
    if( i<lemp->nsymbol ){
      emit_destructor_code(out,lemp->symbols[i],lemp,&lineno);
      fprintf(out,"      break;\n"); lineno++;
    }
  }
  for(i=0; i<lemp->nsymbol; i++){
    struct symbol *sp = lemp->symbols[i];
    if( sp==0 || sp->type==TERMINAL || sp->destructor==0 ) continue;
    fprintf(out,"    case %d:\n",sp->index); lineno++;
    emit_destructor_code(out,lemp->symbols[i],lemp,&lineno);
    fprintf(out,"      break;\n"); lineno++;
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which executes whenever the parser stack overflows */
  tplt_print(out,lemp,lemp->overflow,lemp->overflowln,&lineno);
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate the table of rule information 
  **
  ** Note: This code depends on the fact that rules are number
  ** sequentually beginning with 0.
  */
  for(rp=lemp->rule; rp; rp=rp->next){
    fprintf(out,"  { %d, %d },\n",rp->lhs->index,rp->nrhs); lineno++;
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which execution during each REDUCE action */
  for(rp=lemp->rule; rp; rp=rp->next){
    fprintf(out,"      case %d:\n",rp->index); lineno++;
    fprintf(out,"        YYTRACE(\"%s ::=",rp->lhs->name);
    for(i=0; i<rp->nrhs; i++) fprintf(out," %s",rp->rhs[i]->name);
    fprintf(out,"\")\n"); lineno++;
    emit_code(out,rp,lemp,&lineno);
    fprintf(out,"        break;\n"); lineno++;
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which executes if a parse fails */
  tplt_print(out,lemp,lemp->failure,lemp->failureln,&lineno);
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which executes when a syntax error occurs */
  tplt_print(out,lemp,lemp->error,lemp->errorln,&lineno);
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which executes when the parser accepts its input */
  tplt_print(out,lemp,lemp->accept,lemp->acceptln,&lineno);
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Append any addition code the user desires */
  tplt_print(out,lemp,lemp->extracode,lemp->extracodeln,&lineno);

  fclose(in);
  fclose(out);
  return;
}

/* Generate a header file for the parser */
void ReportHeader(struct lemon *lemp)
{
  FILE *out, *in;
  char *prefix;
  char line[LINESIZE];
  char pattern[LINESIZE];
  int i;

  if( lemp->tokenprefix ) prefix = lemp->tokenprefix;
  else                    prefix = "";
  in = file_open(lemp,".h","r");
  if( in ){
    for(i=1; i<lemp->nterminal && fgets(line,LINESIZE,in); i++){
      sprintf(pattern,"#define %s%-30s %2d\n",prefix,lemp->symbols[i]->name,i);
      if( strcmp(line,pattern) ) break;
    }
    fclose(in);
    if( i==lemp->nterminal ){
      /* No change in the file.  Don't rewrite it. */
      return;
    }
  }
  out = file_open(lemp,".h","w");
  if( out ){
    for(i=1; i<lemp->nterminal; i++){
      fprintf(out,"#define %s%-30s %2d\n",prefix,lemp->symbols[i]->name,i);
    }
    fclose(out);  
  }
  return;
}

/* Reduce the size of the action tables, if possible, by making use
** of defaults.
**
** In this version, if all REDUCE actions use the same rule, make
** them the default.  Only default them if there are more than one.
*/
void CompressTables(struct lemon *lemp)
{
  struct state *stp;
  struct action *ap;
  struct rule *rp;
  int i;
  int cnt;

  for(i=0; i<lemp->nstate; i++){
    stp = lemp->sorted[i];

    /* Find the first REDUCE action */
    for(ap=stp->ap; ap && ap->type!=REDUCE; ap=ap->next);
    if( ap==0 ) continue;

    /* Remember the rule used */
    rp = ap->x.rp;

    /* See if all other REDUCE acitons use the same rule */
    cnt = 1;
    for(ap=ap->next; ap; ap=ap->next){
      if( ap->type==REDUCE ){
        if( ap->x.rp!=rp ) break;
        cnt++;
      }
    }
    if( ap || cnt==1 ) continue;

    /* Combine all REDUCE actions into a single default */
    for(ap=stp->ap; ap && ap->type!=REDUCE; ap=ap->next);
    assert( ap );
    ap->sp = Symbol_new("{default}");
    for(ap=ap->next; ap; ap=ap->next){
      if( ap->type==REDUCE ) ap->type = NOT_USED;
    }
    stp->ap = Action_sort(stp->ap);
  }
}
/***************** From the file "set.c" ************************************/
/*
** Set manipulation routines for the LEMON parser generator.
*/

static int size = 0;

/* Set the set size */
void SetSize(int n)
{
  size = n+1;
}

/* Allocate a new set */
char *SetNew(void){
  char *s;
  int i;
  s = (char*)malloc( size );
  if( s==0 ){
    extern void memory_error();
    memory_error();
  }
  for(i=0; i<size; i++) s[i] = 0;
  return s;
}

/* Deallocate a set */
void SetFree(char *s)
{
  free(s);
}

/* Add a new element to the set.  Return TRUE if the element was added
** and FALSE if it was already there. */
int SetAdd(char *s, int e)
{
  int rv;
  rv = s[e];
  s[e] = 1;
  return !rv;
}

/* Add every element of s2 to s1.  Return TRUE if s1 changes. */
int SetUnion(char *s1, char *s2)
{
  int i, progress;
  progress = 0;
  for(i=0; i<size; i++){
    if( s2[i]==0 ) continue;
    if( s1[i]==0 ){
      progress = 1;
      s1[i] = 1;
    }
  }
  return progress;
}
/********************** From the file "table.c" ****************************/
/*
** All code in this file has been automatically generated
** from a specification in the file
**              "table.q"
** by the associative array code building program "aagen".
** Do not edit this file!  Instead, edit the specification
** file, then rerun aagen.
*/
/*
** Code for processing tables in the LEMON parser generator.
*/

PRIVATE int strhash(char *x)
{
  int h = 0;
  while( *x) h = h*13 + *(x++);
  return h;
}

/* Works like strdup, sort of.  Save a string in malloced memory, but
** keep strings in a table so that the same string is not in more
** than one place.
*/
char *Strsafe(char *y)
{
  char *z;

  z = Strsafe_find(y);
  if( z==0 && (z=malloc( strlen(y)+1 ))!=0 ){
    strcpy(z,y);
    Strsafe_insert(z);
  }
  MemoryCheck(z);
  return z;
}

/* There is one instance of the following structure for each
** associative array of type "x1".
*/
struct s_x1 {
  int size;               /* The number of available slots. */
                          /*   Must be a power of 2 greater than or */
                          /*   equal to 1 */
  int count;              /* Number of currently slots filled */
  struct s_x1node *tbl;  /* The data stored here */
  struct s_x1node **ht;  /* Hash table for lookups */
};

/* There is one instance of this structure for every data element
** in an associative array of type "x1".
*/
typedef struct s_x1node {
  char *data;                  /* The data */
  struct s_x1node *next;   /* Next entry with the same hash */
  struct s_x1node **from;  /* Previous link */
} x1node;

/* There is only one instance of the array, which is the following */
static struct s_x1 *x1a;

/* Allocate a new associative array */
void Strsafe_init(void){
  if( x1a ) return;
  x1a = (struct s_x1*)malloc( sizeof(struct s_x1) );
  if( x1a ){
    x1a->size = 1024;
    x1a->count = 0;
    x1a->tbl = (x1node*)malloc( 
      (sizeof(x1node) + sizeof(x1node*))*1024 );
    if( x1a->tbl==0 ){
      free(x1a);
      x1a = 0;
    }else{
      int i;
      x1a->ht = (x1node**)&(x1a->tbl[1024]);
      for(i=0; i<1024; i++) x1a->ht[i] = 0;
    }
  }
}
/* Insert a new record into the array.  Return TRUE if successful.
** Prior data with the same key is NOT overwritten */
int Strsafe_insert(char *data)
{
  x1node *np;
  int h;
  int ph;

  if( x1a==0 ) return 0;
  ph = strhash(data);
  h = ph & (x1a->size-1);
  np = x1a->ht[h];
  while( np ){
    if( strcmp(np->data,data)==0 ){
      /* An existing entry with the same key is found. */
      /* Fail because overwrite is not allows. */
      return 0;
    }
    np = np->next;
  }
  if( x1a->count>=x1a->size ){
    /* Need to make the hash table bigger */
    int i,size;
    struct s_x1 array;
    array.size = size = x1a->size*2;
    array.count = x1a->count;
    array.tbl = (x1node*)malloc(
      (sizeof(x1node) + sizeof(x1node*))*size );
    if( array.tbl==0 ) return 0;  /* Fail due to malloc failure */
    array.ht = (x1node**)&(array.tbl[size]);
    for(i=0; i<size; i++) array.ht[i] = 0;
    for(i=0; i<x1a->count; i++){
      x1node *oldnp, *newnp;
      oldnp = &(x1a->tbl[i]);
      h = strhash(oldnp->data) & (size-1);
      newnp = &(array.tbl[i]);
      if( array.ht[h] ) array.ht[h]->from = &(newnp->next);
      newnp->next = array.ht[h];
      newnp->data = oldnp->data;
      newnp->from = &(array.ht[h]);
      array.ht[h] = newnp;
    }
    free(x1a->tbl);
    *x1a = array;
  }
  /* Insert the new data */
  h = ph & (x1a->size-1);
  np = &(x1a->tbl[x1a->count++]);
  np->data = data;
  if( x1a->ht[h] ) x1a->ht[h]->from = &(np->next);
  np->next = x1a->ht[h];
  x1a->ht[h] = np;
  np->from = &(x1a->ht[h]);
  return 1;
}

/* Return a pointer to data assigned to the given key.  Return NULL
** if no such key. */
char *Strsafe_find(char *key)
{
  int h;
  x1node *np;

  if( x1a==0 ) return 0;
  h = strhash(key) & (x1a->size-1);
  np = x1a->ht[h];
  while( np ){
    if( strcmp(np->data,key)==0 ) break;
    np = np->next;
  }
  return np ? np->data : 0;
}

/* Return a pointer to the (terminal or nonterminal) symbol "x".
** Create a new symbol if this is the first time "x" has been seen.
*/
struct symbol *Symbol_new(char *x)
{
  struct symbol *sp;

  sp = Symbol_find(x);
  if( sp==0 ){
    sp = (struct symbol *)malloc( sizeof(struct symbol) );
    MemoryCheck(sp);
    sp->name = Strsafe(x);
    sp->type = safe_isupper(*x) ? TERMINAL : NONTERMINAL;
    sp->rule = 0;
    sp->prec = -1;
    sp->assoc = UNK;
    sp->firstset = 0;
    sp->lambda = FALSE;
    sp->destructor = 0;
    sp->datatype = 0;
    Symbol_insert(sp,sp->name);
  }
  return sp;
}

/* Compare two symbols */
int Symbolcmpp(struct symbol **a, struct symbol **b)
{
  return strcmp((**a).name,(**b).name);
}

/* There is one instance of the following structure for each
** associative array of type "x2".
*/
struct s_x2 {
  int size;               /* The number of available slots. */
                          /*   Must be a power of 2 greater than or */
                          /*   equal to 1 */
  int count;              /* Number of currently slots filled */
  struct s_x2node *tbl;  /* The data stored here */
  struct s_x2node **ht;  /* Hash table for lookups */
};

/* There is one instance of this structure for every data element
** in an associative array of type "x2".
*/
typedef struct s_x2node {
  struct symbol *data;                  /* The data */
  char *key;                   /* The key */
  struct s_x2node *next;   /* Next entry with the same hash */
  struct s_x2node **from;  /* Previous link */
} x2node;

/* There is only one instance of the array, which is the following */
static struct s_x2 *x2a;

/* Allocate a new associative array */
void Symbol_init(void){
  if( x2a ) return;
  x2a = (struct s_x2*)malloc( sizeof(struct s_x2) );
  if( x2a ){
    x2a->size = 128;
    x2a->count = 0;
    x2a->tbl = (x2node*)malloc( 
      (sizeof(x2node) + sizeof(x2node*))*128 );
    if( x2a->tbl==0 ){
      free(x2a);
      x2a = 0;
    }else{
      int i;
      x2a->ht = (x2node**)&(x2a->tbl[128]);
      for(i=0; i<128; i++) x2a->ht[i] = 0;
    }
  }
}
/* Insert a new record into the array.  Return TRUE if successful.
** Prior data with the same key is NOT overwritten */
int Symbol_insert(struct symbol *data, char *key)
{
  x2node *np;
  int h;
  int ph;

  if( x2a==0 ) return 0;
  ph = strhash(key);
  h = ph & (x2a->size-1);
  np = x2a->ht[h];
  while( np ){
    if( strcmp(np->key,key)==0 ){
      /* An existing entry with the same key is found. */
      /* Fail because overwrite is not allows. */
      return 0;
    }
    np = np->next;
  }
  if( x2a->count>=x2a->size ){
    /* Need to make the hash table bigger */
    int i,size;
    struct s_x2 array;
    array.size = size = x2a->size*2;
    array.count = x2a->count;
    array.tbl = (x2node*)malloc(
      (sizeof(x2node) + sizeof(x2node*))*size );
    if( array.tbl==0 ) return 0;  /* Fail due to malloc failure */
    array.ht = (x2node**)&(array.tbl[size]);
    for(i=0; i<size; i++) array.ht[i] = 0;
    for(i=0; i<x2a->count; i++){
      x2node *oldnp, *newnp;
      oldnp = &(x2a->tbl[i]);
      h = strhash(oldnp->key) & (size-1);
      newnp = &(array.tbl[i]);
      if( array.ht[h] ) array.ht[h]->from = &(newnp->next);
      newnp->next = array.ht[h];
      newnp->key = oldnp->key;
      newnp->data = oldnp->data;
      newnp->from = &(array.ht[h]);
      array.ht[h] = newnp;
    }
    free(x2a->tbl);
    *x2a = array;
  }
  /* Insert the new data */
  h = ph & (x2a->size-1);
  np = &(x2a->tbl[x2a->count++]);
  np->key = key;
  np->data = data;
  if( x2a->ht[h] ) x2a->ht[h]->from = &(np->next);
  np->next = x2a->ht[h];
  x2a->ht[h] = np;
  np->from = &(x2a->ht[h]);
  return 1;
}

/* Return a pointer to data assigned to the given key.  Return NULL
** if no such key. */
struct symbol *Symbol_find(char *key)
{
  int h;
  x2node *np;

  if( x2a==0 ) return 0;
  h = strhash(key) & (x2a->size-1);
  np = x2a->ht[h];
  while( np ){
    if( strcmp(np->key,key)==0 ) break;
    np = np->next;
  }
  return np ? np->data : 0;
}

/* Return the n-th data.  Return NULL if n is out of range. */
struct symbol *Symbol_Nth(int n)
{
  struct symbol *data;
  if( x2a && n>0 && n<=x2a->count ){
    data = x2a->tbl[n-1].data;
  }else{
    data = 0;
  }
  return data;
}

/* Return the size of the array */
int Symbol_count(void)
{
  return x2a ? x2a->count : 0;
}

/* Return an array of pointers to all data in the table.
** The array is obtained from malloc.  Return NULL if memory allocation
** problems, or if the array is empty. */
struct symbol **Symbol_arrayof(void)
{
  struct symbol **array;
  int i,size;
  if( x2a==0 ) return 0;
  size = x2a->count;
  array = (struct symbol **)malloc( sizeof(struct symbol *)*size );
  if( array ){
    for(i=0; i<size; i++) array[i] = x2a->tbl[i].data;
  }
  return array;
}

/* Compare two configurations */
int Configcmp(void *a_arg, void *b_arg)
{
  struct config *a = a_arg, *b = b_arg;
  int x;
  x = a->rp->index - b->rp->index;
  if( x==0 ) x = a->dot - b->dot;
  return x;
}

/* Compare two states */
PRIVATE int statecmp(struct config *a, struct config *b)
{
  int rc;
  for(rc=0; rc==0 && a && b;  a=a->bp, b=b->bp){
    rc = a->rp->index - b->rp->index;
    if( rc==0 ) rc = a->dot - b->dot;
  }
  if( rc==0 ){
    if( a ) rc = 1;
    if( b ) rc = -1;
  }
  return rc;
}

/* Hash a state */
PRIVATE int statehash(struct config *a)
{
  int h=0;
  while( a ){
    h = h*571 + a->rp->index*37 + a->dot;
    a = a->bp;
  }
  return h;
}

/* Allocate a new state structure */
struct state *State_new(void)
{
  struct state *new;
  new = (struct state *)malloc( sizeof(struct state) );
  MemoryCheck(new);
  return new;
}

/* There is one instance of the following structure for each
** associative array of type "x3".
*/
struct s_x3 {
  int size;               /* The number of available slots. */
                          /*   Must be a power of 2 greater than or */
                          /*   equal to 1 */
  int count;              /* Number of currently slots filled */
  struct s_x3node *tbl;  /* The data stored here */
  struct s_x3node **ht;  /* Hash table for lookups */
};

/* There is one instance of this structure for every data element
** in an associative array of type "x3".
*/
typedef struct s_x3node {
  struct state *data;                  /* The data */
  struct config *key;                   /* The key */
  struct s_x3node *next;   /* Next entry with the same hash */
  struct s_x3node **from;  /* Previous link */
} x3node;

/* There is only one instance of the array, which is the following */
static struct s_x3 *x3a;

/* Allocate a new associative array */
void State_init(void){
  if( x3a ) return;
  x3a = (struct s_x3*)malloc( sizeof(struct s_x3) );
  if( x3a ){
    x3a->size = 128;
    x3a->count = 0;
    x3a->tbl = (x3node*)malloc( 
      (sizeof(x3node) + sizeof(x3node*))*128 );
    if( x3a->tbl==0 ){
      free(x3a);
      x3a = 0;
    }else{
      int i;
      x3a->ht = (x3node**)&(x3a->tbl[128]);
      for(i=0; i<128; i++) x3a->ht[i] = 0;
    }
  }
}
/* Insert a new record into the array.  Return TRUE if successful.
** Prior data with the same key is NOT overwritten */
int State_insert(struct state *data, struct config *key)
{
  x3node *np;
  int h;
  int ph;

  if( x3a==0 ) return 0;
  ph = statehash(key);
  h = ph & (x3a->size-1);
  np = x3a->ht[h];
  while( np ){
    if( statecmp(np->key,key)==0 ){
      /* An existing entry with the same key is found. */
      /* Fail because overwrite is not allows. */
      return 0;
    }
    np = np->next;
  }
  if( x3a->count>=x3a->size ){
    /* Need to make the hash table bigger */
    int i,size;
    struct s_x3 array;
    array.size = size = x3a->size*2;
    array.count = x3a->count;
    array.tbl = (x3node*)malloc(
      (sizeof(x3node) + sizeof(x3node*))*size );
    if( array.tbl==0 ) return 0;  /* Fail due to malloc failure */
    array.ht = (x3node**)&(array.tbl[size]);
    for(i=0; i<size; i++) array.ht[i] = 0;
    for(i=0; i<x3a->count; i++){
      x3node *oldnp, *newnp;
      oldnp = &(x3a->tbl[i]);
      h = statehash(oldnp->key) & (size-1);
      newnp = &(array.tbl[i]);
      if( array.ht[h] ) array.ht[h]->from = &(newnp->next);
      newnp->next = array.ht[h];
      newnp->key = oldnp->key;
      newnp->data = oldnp->data;
      newnp->from = &(array.ht[h]);
      array.ht[h] = newnp;
    }
    free(x3a->tbl);
    *x3a = array;
  }
  /* Insert the new data */
  h = ph & (x3a->size-1);
  np = &(x3a->tbl[x3a->count++]);
  np->key = key;
  np->data = data;
  if( x3a->ht[h] ) x3a->ht[h]->from = &(np->next);
  np->next = x3a->ht[h];
  x3a->ht[h] = np;
  np->from = &(x3a->ht[h]);
  return 1;
}

/* Return a pointer to data assigned to the given key.  Return NULL
** if no such key. */
struct state *State_find(struct config *key)
{
  int h;
  x3node *np;

  if( x3a==0 ) return 0;
  h = statehash(key) & (x3a->size-1);
  np = x3a->ht[h];
  while( np ){
    if( statecmp(np->key,key)==0 ) break;
    np = np->next;
  }
  return np ? np->data : 0;
}

/* Return an array of pointers to all data in the table.
** The array is obtained from malloc.  Return NULL if memory allocation
** problems, or if the array is empty. */
struct state **State_arrayof(void)
{
  struct state **array;
  int i,size;
  if( x3a==0 ) return 0;
  size = x3a->count;
  array = (struct state **)malloc( sizeof(struct state *)*size );
  if( array ){
    for(i=0; i<size; i++) array[i] = x3a->tbl[i].data;
  }
  return array;
}

/* Hash a configuration */
PRIVATE int confighash(struct config *a)
{
  int h=0;
  h = h*571 + a->rp->index*37 + a->dot;
  return h;
}

/* There is one instance of the following structure for each
** associative array of type "x4".
*/
struct s_x4 {
  int size;               /* The number of available slots. */
                          /*   Must be a power of 2 greater than or */
                          /*   equal to 1 */
  int count;              /* Number of currently slots filled */
  struct s_x4node *tbl;  /* The data stored here */
  struct s_x4node **ht;  /* Hash table for lookups */
};

/* There is one instance of this structure for every data element
** in an associative array of type "x4".
*/
typedef struct s_x4node {
  struct config *data;                  /* The data */
  struct s_x4node *next;   /* Next entry with the same hash */
  struct s_x4node **from;  /* Previous link */
} x4node;

/* There is only one instance of the array, which is the following */
static struct s_x4 *x4a;

/* Allocate a new associative array */
void Configtable_init(void){
  if( x4a ) return;
  x4a = (struct s_x4*)malloc( sizeof(struct s_x4) );
  if( x4a ){
    x4a->size = 64;
    x4a->count = 0;
    x4a->tbl = (x4node*)malloc( 
      (sizeof(x4node) + sizeof(x4node*))*64 );
    if( x4a->tbl==0 ){
      free(x4a);
      x4a = 0;
    }else{
      int i;
      x4a->ht = (x4node**)&(x4a->tbl[64]);
      for(i=0; i<64; i++) x4a->ht[i] = 0;
    }
  }
}
/* Insert a new record into the array.  Return TRUE if successful.
** Prior data with the same key is NOT overwritten */
int Configtable_insert(struct config *data)
{
  x4node *np;
  int h;
  int ph;

  if( x4a==0 ) return 0;
  ph = confighash(data);
  h = ph & (x4a->size-1);
  np = x4a->ht[h];
  while( np ){
    if( Configcmp(np->data,data)==0 ){
      /* An existing entry with the same key is found. */
      /* Fail because overwrite is not allows. */
      return 0;
    }
    np = np->next;
  }
  if( x4a->count>=x4a->size ){
    /* Need to make the hash table bigger */
    int i,size;
    struct s_x4 array;
    array.size = size = x4a->size*2;
    array.count = x4a->count;
    array.tbl = (x4node*)malloc(
      (sizeof(x4node) + sizeof(x4node*))*size );
    if( array.tbl==0 ) return 0;  /* Fail due to malloc failure */
    array.ht = (x4node**)&(array.tbl[size]);
    for(i=0; i<size; i++) array.ht[i] = 0;
    for(i=0; i<x4a->count; i++){
      x4node *oldnp, *newnp;
      oldnp = &(x4a->tbl[i]);
      h = confighash(oldnp->data) & (size-1);
      newnp = &(array.tbl[i]);
      if( array.ht[h] ) array.ht[h]->from = &(newnp->next);
      newnp->next = array.ht[h];
      newnp->data = oldnp->data;
      newnp->from = &(array.ht[h]);
      array.ht[h] = newnp;
    }
    free(x4a->tbl);
    *x4a = array;
  }
  /* Insert the new data */
  h = ph & (x4a->size-1);
  np = &(x4a->tbl[x4a->count++]);
  np->data = data;
  if( x4a->ht[h] ) x4a->ht[h]->from = &(np->next);
  np->next = x4a->ht[h];
  x4a->ht[h] = np;
  np->from = &(x4a->ht[h]);
  return 1;
}

/* Return a pointer to data assigned to the given key.  Return NULL
** if no such key. */
struct config *Configtable_find(struct config *key)
{
  int h;
  x4node *np;

  if( x4a==0 ) return 0;
  h = confighash(key) & (x4a->size-1);
  np = x4a->ht[h];
  while( np ){
    if( Configcmp(np->data,key)==0 ) break;
    np = np->next;
  }
  return np ? np->data : 0;
}

/* Remove all data from the table.  Pass each data to the function "f"
** as it is removed.  ("f" may be null to avoid this step.) */
void Configtable_clear(int(*f)(struct config *))
{
  int i;
  if( x4a==0 || x4a->count==0 ) return;
  if( f ) for(i=0; i<x4a->count; i++) (*f)(x4a->tbl[i].data);
  for(i=0; i<x4a->size; i++) x4a->ht[i] = 0;
  x4a->count = 0;
  return;
}
