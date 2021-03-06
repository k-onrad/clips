#include <stdio.h>
#include <stdlib.h>

#include <editline/readline.h>
#include "mpc.h"

/* LVAL Types */
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR };

typedef struct lval {
  int type;
  long num;
  char* err;
  char* sym;
  int count;
  struct lval** cell;
} lval;

lval* lval_num(long x) {
  /* Construct Number LVAL */
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval* lval_err(char* m) {
  /* Construct Error LVAL */
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(m) + 1);
  strcpy(v->err, m);
  return v;
}

lval* lval_sym(char* s) {
  /* Construct Symbol LVAL */
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

lval* lval_sexpr(void) {
  /* Construct empty S-expression LVAL */
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

void lval_del(lval* v) {
  /* Free LVAL memory, clean up children if needed */
  switch (v->type) {
    case LVAL_NUM: break;
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;
    case LVAL_SEXPR: 
      for (int i = 0; i < v->count; i++) {
        lval_del(v->cell[i]);
      }
      free(v->cell);
      break;
  }
  free(v);
}

lval* lval_read_num(mpc_ast_t* t) {
  /* Convert AST obj to LVAL Number */
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x) : lval_err("Invalid number");
}

lval* lval_add(lval* v, lval* x) {
  /* Add LVAL x to LVAL v's array of children */
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

lval* lval_read(mpc_ast_t* t) {
  /* Convert AST obj to LVAL Numbers and Symbols */
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

  /* If AST obj is S-expression, construct empty LVAL S-expression */
  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr")) { x = lval_sexpr(); }

  /* Go through AST children, populating LVAL S-expression */
  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

void lval_print(lval* v);
void lval_expr_print(lval* v, char open, char close) {
  /* Print LVAL S-expression */
  putchar(open);
  for (int i = 0; i < v->count; i++) {
    lval_print(v->cell[i]);
    if (i != (v->count-1)) {
      putchar(' ');
    }
  }
  putchar(close);
}

void lval_print(lval* v) {
  /* Print LVAL */
  switch (v->type) {
    case LVAL_NUM: printf("%li", v->num); break;
    case LVAL_ERR: printf("Error: %s", v->err); break;
    case LVAL_SYM: printf("%s", v->sym); break;
    case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
  }
}

/* Print LVAL with newline */
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

lval* lval_pop(lval* v, int i) {
  /* Get LVAL at index i of LVAL v's array of children */
  lval* x = v->cell[i];
  /* Move array of pointers around to overwrite pointer at index i */
  memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));
  /* Decrease LVAL v's count of children */
  v->count--;
  /* Reallocate memory to LVAL v's array of children to new size */
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);

  return x;
}

lval* lval_take(lval* v, int i) { 
  /* Pop LVAL value at index i of LVAL v's array of children */
  lval* x = lval_pop(v, i);
  /* Delete the rest of LVAL v */
  lval_del(v);
  return x;
}

lval* builtin_op(lval* a, char* op) {
  /* Check if all LVAL children are numbers */
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on non-number!");
    }
  }

  /* Pop first element */
  lval* x = lval_pop(a, 0);

  /* If symbol is "-" and only one other element, this is unary negation */
  if ((strcmp(op, "-") == 0) && a->count == 0) { x->num = -x->num; }

  /* While there are elements remaining */
  while (a->count > 0) {
    /* Pop next element */
    lval* y = lval_pop(a, 0);

    /* Accumulate value computed on x */
    if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0) { x->num += y->num; }
    if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) { x->num -= y->num; }
    if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) { x->num *= y->num; }
    if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) { 
      if (y->num == 0) {
        lval_del(x); lval_del(y);
        x = lval_err("Division by zero!"); break;

      }
      x->num /= y->num; 
    }
    if (strcmp(op, "%") == 0) { x->num %= y->num; }

    /* Free popped value */
    lval_del(y);
  }

  /* Free the whole of a */
  lval_del(a);
  return x;
}

lval* lval_eval(lval* v);
lval* lval_eval_sexpr(lval* v) {
  /* Evaluate children */
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(v->cell[i]);
  }

  /* Error checking */
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  /* Empty expression */
  if (v->count == 0) { return v; }

  /* Single expression */
  if (v->count == 1) { return lval_take(v, 0); }

  /* Ensure first element is symbol */
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_SYM) {
    lval_del(f); lval_del(v);
    return lval_err("S-expression does not start with symbol!");
  }

  /* Evaluate operation w/ host language ops */
  lval* result = builtin_op(v, f->sym);
  lval_del(f);
  return result;
}

lval* lval_eval(lval* v) {
  /* Evaluate S-expressions */
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
  /* Return all other lval types as they are */
  return v;
}

int main(int argc, char** argv) {
  /* Construct MPC parser */
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Clips = mpc_new("clips");

  mpca_lang(MPCA_LANG_DEFAULT,
      "                                                                                  \
      number   : /-?([0-9]*[.])?[0-9]+/ ;                                                \
      symbol : '+' | '-' | '*' | '/' | '%' | \"add\" | \"sub\" | \"div\" | \"mul\" ;     \
      sexpr    : '(' <expr>* ')' ;                                                       \
      expr     : <number> | <symbol> | <sexpr> ;                                         \
      clips    : /^/ <expr>* /$/ ;                                                       \
      ",
      Number, Symbol, Sexpr, Expr, Clips);

  puts("Clips v0.0.2");
  puts("Press Ctrl+C to Exit\n");

  /* REPL's L */
  while (1) {
    char* input = readline("clips> ");
    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Clips, &r)) {
      /* Read, Eval */
      lval* x = lval_eval(lval_read(r.output));
      /* Print */
      lval_println(x);
      lval_del(x);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }

  mpc_cleanup(5, Number, Symbol, Sexpr, Expr, Clips);
  return 0;
}
