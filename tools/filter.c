#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/filter.c,v 1.4 1994/10/25 10:22:15 deyke Exp $";
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG 0

enum e_type {
  AND,
  LPAREN,
  NOT,
  OR,
  RPAREN,
  STRING
};

struct node {
  enum e_type type;
  char *value;
  struct node *left;
  struct node *right;
};

struct symbol {
  enum e_type type;
  const char *string;
};

static const struct symbol Symbol_table[] =
{

  {AND, "and"},
  {LPAREN, "("},
  {NOT, "not"},
  {OR, "or"},
  {RPAREN, ")"},

  {AND, "a"},
  {LPAREN, "l"},
  {NOT, "n"},
  {OR, "o"},
  {RPAREN, "r"},

  {AND, 0}
};

static char buf[64][256];
static int numlines;
static struct node *Nodes;
static struct node *Tokens;

static struct node *getexpr(void);

/*---------------------------------------------------------------------------*/

static void halt(const char *msg)
{
  fprintf(stderr, "%s\n", msg);
  exit(1);
}

/*---------------------------------------------------------------------------*/

static struct node *getnode(void)
{
  struct node *np;

  if (!Tokens)
    halt("Unexpected end-of-input");
  np = Tokens;
  Tokens = Tokens->left;
  np->left = np->right = 0;
  return np;
}

/*---------------------------------------------------------------------------*/

static struct node *getexpr2(void)
{

  struct node *np;
  struct node *npleft;

  np = getnode();
  switch (np->type) {
  case LPAREN:
    np = getexpr();
    npleft = getnode();
    if (npleft->type != RPAREN)
      halt("')' expected");
    break;
  case NOT:
    np->left = getexpr2();
    break;
  case STRING:
    break;
  default:
    halt("String or '(' expected");
  }
  return np;
}

/*---------------------------------------------------------------------------*/

static struct node *getexpr1(void)
{

  struct node *np;
  struct node *npleft;

  np = getexpr2();
  if (Tokens && Tokens->type == AND) {
    npleft = np;
    np = getnode();
    np->left = npleft;
    np->right = getexpr1();
  }
  return np;
}

/*---------------------------------------------------------------------------*/

static struct node *getexpr(void)
{

  struct node *np;
  struct node *npleft;

  np = getexpr1();
  if (Tokens && Tokens->type == OR) {
    npleft = np;
    np = getnode();
    np->left = npleft;
    np->right = getexpr();
  }
  return np;
}

/*---------------------------------------------------------------------------*/

#if DEBUG

static void print_tokens(void)
{
  struct node *np;

  for (np = Tokens; np; np = np->left)
    if (np->type != STRING)
      printf("%s ", Symbol_table[np->type].string);
    else
      printf("\"%s\" ", np->value);
  putchar('\n');
}

#endif

/*---------------------------------------------------------------------------*/

#if DEBUG

static void print_expr(struct node *np)
{
  switch (np->type) {
  case AND:
  case OR:
    printf("(");
    print_expr(np->left);
    printf(" %s ", Symbol_table[np->type].string);
    print_expr(np->right);
    printf(")");
    break;
  case NOT:
    printf("(NOT ");
    print_expr(np->left);
    printf(")");
    break;
  case STRING:
    printf("\"%s\"", np->value);
    break;
  default:
    halt("Unexpected node in tree");
    break;
  }
}

#endif

/*---------------------------------------------------------------------------*/

static int match(struct node *np)
{
  int n;

  switch (np->type) {
  case AND:
    return match(np->left) && match(np->right);
  case OR:
    return match(np->left) || match(np->right);
  case NOT:
    return !match(np->left);
  case STRING:
    for (n = 0; n < numlines; n++)
      if (strstr(buf[n], np->value))
	return 1;
    return 0;
  default:
    halt("Unexpected node in tree");
    return 0;
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  const struct symbol *sp;
  int i;
  struct node *np;
  struct node *tail = 0;

  for (i = 1; i < argc; i++) {
    np = (struct node *) calloc(1, sizeof(struct node));
    np->type = STRING;
    np->value = argv[i];
    for (sp = Symbol_table; sp->string; sp++)
      if (!strcmp(sp->string, argv[i])) {
	np->type = sp->type;
	break;
      }
    if (!Tokens)
      Tokens = np;
    else
      tail->left = np;
    tail = np;
  }

#if DEBUG
  print_tokens();
#endif

  Nodes = getexpr();
  if (Tokens)
    halt("Unexpected tokens after expression");

#if DEBUG
  print_expr(Nodes);
  putchar('\n');
#endif

  for (;;) {
    for (numlines = 0;;) {
      if (!gets(buf[numlines]))
	break;
      numlines++;
      if (!buf[numlines - 1][0])
	break;
    }
    if (!numlines)
      break;
    if (match(Nodes))
      for (i = 0; i < numlines; i++)
	puts(buf[i]);
  }

  return 0;
}

