/* @(#) $Id: htmldiff.c,v 1.3 2000/03/04 18:31:15 deyke Exp $ */

/*
   Dieter Deyke
   May 4, 1996
   Copyright (c) 1996-2000 CoCreate Software GmbH. All Rights Reserved.
   Company Confidential
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if !defined S_ISDIR
#define S_ISDIR(mode) ((mode & S_IFMT) == S_IFDIR)
#endif

#if defined WIN32
#include <process.h>
#define getpid _getpid
#define pclose _pclose
#define popen  _popen
#endif

static const char green[] = "<b class=green>";
static const char greener[] = "<b class=greener>";
static const char red[] = "<b class=red>";
static const char redder[] = "<b class=redder>";
static const char white[] = "<b class=white>";
static const char yellow[] = "<b class=yellow>";

struct region {
  char *buffer;
  int size;
  int count;
};

static struct region added;
static struct region deleted;

static char addfile[1024];
static char delfile[1024];
static char diffile[1024];

static int chargroup[256];

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

/*
   Dieter Deyke
   January 13, 1997
   Copyright (c) 1997-2000 CoCreate Software GmbH.  All Rights Reserved.
   Company Confidential
*/

#ifndef GETOPT
#define GETOPT

extern char *optarg;
extern int opterr;
extern int optind;

#if defined WIN32

int getopt(int argc, char *const *argv, char *opts);

#else

#include <unistd.h>

#endif

#endif

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

/*
   Dieter Deyke
   January 13, 1997
   Copyright (c) 1997-2000 CoCreate Software GmbH.  All Rights Reserved.
   Company Confidential
*/

#if defined WIN32

#include <stdio.h>
#include <string.h>

char *optarg;
int opterr = 1;
int optind = 1;

/*---------------------------------------------------------------------------*/

static void ERR(char *const *argv, const char *str, int c)
{
  if (opterr) {
    fprintf(stderr, "%s%s%c\n", *argv, str, c);
  }
}

/*---------------------------------------------------------------------------*/

int getopt(int argc, char *const *argv, char *opts)
{

  const char *cp;
  int c;
  static int sp = 1;

  if (sp == 1) {
    if (optind >= argc || argv[optind][0] != '-' || !argv[optind][1])
      return EOF;
    if (!strcmp(argv[optind], "--")) {
      optind++;
      return EOF;
    }
  }
  c = argv[optind][sp];
  if (c == ':' || (cp = strchr(opts, c)) == 0) {
    ERR(argv, ": illegal option -- ", c);
    if (!argv[optind][++sp]) {
      optind++;
      sp = 1;
    }
    return '?';
  }
  if (*++cp == ':') {
    if (argv[optind][sp + 1])
      optarg = &argv[optind++][sp + 1];
    else if (++optind >= argc) {
      ERR(argv, ": option requires an argument -- ", c);
      sp = 1;
      return '?';
    } else
      optarg = argv[optind++];
    sp = 1;
  } else {
    if (!argv[optind][++sp]) {
      sp = 1;
      optind++;
    }
    optarg = 0;
  }
  return c;
}

#else

void getopt_prevent_empty_file_message(void)
{
}

#endif

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

static void syscallerr(const char *mesg)
{
  perror(mesg);
  exit(1);
}

/*---------------------------------------------------------------------------*/

static void usererr(const char *mesg)
{
  fprintf(stderr, "%s\n", mesg);
  exit(1);
}

/*---------------------------------------------------------------------------*/

static void add_text_to_region(struct region *rp, const char *text)
{
  int len;

  len = strlen(text);
  while (rp->count + len + 1 > rp->size) {
    rp->size *= 2;
    if (rp->size < 64 * 1024) {
      rp->size = 64 * 1024;
    }
    rp->buffer = realloc(rp->buffer, rp->size);
    if (!rp->buffer) {
      syscallerr("realloc");
    }
  }
  strcpy(rp->buffer + rp->count, text);
  rp->count += len;
}

/*---------------------------------------------------------------------------*/

static void print_text_as_html(const char *style, const char *text)
{

  const char *cp;
  static const char *last_style;
  static int line_length;

  if (last_style != style) {
    printf(last_style = style);
  }
  for (cp = text;; cp++) {
    switch (*cp) {
    case 0:
      return;
    case '"':
      fputs("&quot;", stdout);
      line_length++;
      break;
    case '&':
      fputs("&amp;", stdout);
      line_length++;
      break;
    case '<':
      fputs("&lt;", stdout);
      line_length++;
      break;
    case '>':
      fputs("&gt;", stdout);
      line_length++;
      break;
    case '\n':
      if (last_style == greener) {
	printf(last_style = green);
      } else if (last_style == redder) {
	printf(last_style = red);
      }
      while (line_length < 100) {
	putchar(' ');
	line_length++;
      }
      putchar('\n');
      line_length = 0;
      break;
    default:
      putchar(*cp);
      line_length++;
      break;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void write_region_as_words(const struct region *rp, const char *filename)
{

  FILE *fp;
  const char *cp;
  int chr;
  int g;

  fp = fopen(filename, "w");
  if (!fp) {
    syscallerr(filename);
  }
  cp = rp->buffer;
  g = chargroup[*cp & 0xff];
  for (;;) {
    chr = *cp++ & 0xff;
    if (g != chargroup[chr]) {
      g = chargroup[chr];
      putc('\n', fp);
    }
    if (!chr) {
      break;
    }
    putc(chr == '\n' ? 1 : chr, fp);
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void print_diff_of_regions(const char *filename, int show_deleted)
{

  FILE *fp;
  char *cp;
  char *text;
  char line[8 * 1024];
  int action;

  fp = fopen(filename, "r");
  if (!fp) {
    syscallerr(filename);
  }
  fgets(line, sizeof(line), fp);
  fgets(line, sizeof(line), fp);
  fgets(line, sizeof(line), fp);
  while (fgets(line, sizeof(line), fp)) {
    action = line[0];
    text = line + 1;
    cp = strchr(text, '\n');
    if (cp) {
      *cp = 0;
    }
    while ((cp = strchr(text, 1)) != 0) {
      *cp = '\n';
    }
    switch (action) {
    case ' ':
      print_text_as_html(show_deleted ? red : green, text);
      break;
    case '-':
      if (show_deleted) {
	print_text_as_html(redder, text);
      }
      break;
    case '+':
      if (!show_deleted) {
	print_text_as_html(greener, text);
      }
      break;
    default:
      break;
    }
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void diff_regions(void)
{
  char line[1024];

  if (deleted.count > 2048 || added.count > 2048) {
    print_text_as_html(red, deleted.buffer);
    print_text_as_html(green, added.buffer);
  } else {
    write_region_as_words(&deleted, delfile);
    write_region_as_words(&added, addfile);
    sprintf(line, "gdiff -U 50000 \"%s\" \"%s\" > \"%s\"", delfile, addfile, diffile);
    system(line);
    print_diff_of_regions(diffile, 1);
    print_diff_of_regions(diffile, 0);
  }
  deleted.count = added.count = 0;
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  FILE *fpdiff;
  char *cp;
  char diffoptions[1024];
  char line[8 * 1024];
  char newfile[1024];
  char oldfile[1024];
  int action;
  int chr;
  int context = 25;
  int errflag = 0;
  struct stat statbufnew;
  struct stat statbufold;

  for (chr = 0; chr < 256; chr++) {
    if (chr == 0) {
      chargroup[chr] = 0;
    } else if ((chr >= 'A' && chr <= 'Z') ||
	       (chr >= 'a' && chr <= 'z') ||
		chr == '_') {
      chargroup[chr] = 1;
    } else if ((chr >= '0' && chr <= '9') ||
		chr == '.') {
      chargroup[chr] = 2;
    } else if (chr == '(' || chr == ')' ||
	       chr == '[' || chr == ']' ||
	       chr == '{' || chr == '}') {
      chargroup[chr] = 3;
    } else if (ispunct(chr)) {
      chargroup[chr] = 4;
    } else if (isspace(chr)) {
      chargroup[chr] = 5;
    } else {
      chargroup[chr] = 6;
    }
  }

  diffoptions[0] = 0;
  while ((chr = getopt(argc, argv, "bc:iw")) != EOF) {
    switch (chr) {
    case 'b':
      strcat(diffoptions, " -b");
      break;
    case 'c':
      context = atoi(optarg);
      if (context < 1) {
	errflag = 1;
      }
      break;
    case 'i':
      strcat(diffoptions, " -i");
      break;
    case 'w':
      strcat(diffoptions, " -w");
      break;
    case '?':
      errflag = 1;
      break;
    }
  }
  if (errflag || optind + 2 != argc) {
    sprintf(line, "Usage: %s [-c lines] [-biw] oldfile newfile", *argv);
    usererr(line);
  }
  strcpy(oldfile, argv[optind++]);
  strcpy(newfile, argv[optind++]);
#if defined WIN32
  if ((oldfile[0] == 'N' || oldfile[0] == 'n') &&
      (oldfile[1] == 'U' || oldfile[1] == 'u') &&
      (oldfile[2] == 'L' || oldfile[2] == 'l') &&
      oldfile[3] == 0) {
    memset(&statbufold, 0, sizeof(struct stat));
  } else
#endif
  if (stat(oldfile, &statbufold)) {
    syscallerr(oldfile);
  }
#if defined WIN32
  if ((newfile[0] == 'N' || newfile[0] == 'n') &&
      (newfile[1] == 'U' || newfile[1] == 'u') &&
      (newfile[2] == 'L' || newfile[2] == 'l') &&
      newfile[3] == 0) {
    memset(&statbufnew, 0, sizeof(struct stat));
  } else
#endif
  if (stat(newfile, &statbufnew)) {
    syscallerr(newfile);
  }
  if (S_ISDIR(statbufold.st_mode) && S_ISDIR(statbufnew.st_mode)) {
    usererr("Cannot diff two directories");
  }
  if (S_ISDIR(statbufold.st_mode)) {
    strcat(oldfile, "/");
    cp = strrchr(newfile, '/');
    strcat(oldfile, cp ? cp + 1 : newfile);
  }
  if (S_ISDIR(statbufnew.st_mode)) {
    strcat(newfile, "/");
    cp = strrchr(oldfile, '/');
    strcat(newfile, cp ? cp + 1 : oldfile);
  }
#if defined WIN32
  sprintf(delfile, "%s/del%d", getenv("TEMP"), getpid());
  sprintf(addfile, "%s/add%d", getenv("TEMP"), getpid());
  sprintf(diffile, "%s/dif%d", getenv("TEMP"), getpid());
#else
  tmpnam(delfile);
  tmpnam(addfile);
  tmpnam(diffile);
#endif
  sprintf(line, "gdiff -t -U %d %s \"%s\" \"%s\"", context, diffoptions, oldfile, newfile);
  fpdiff = popen(line, "r");
  if (!fpdiff) {
    syscallerr(line);
  }
  printf("<html>\n");
  printf("<head>\n");
  printf("<title>%s</title>\n", newfile);
  printf("<style type=\"text/css\">\n");
  printf("all.green {color:#000000; background-color:#00ff00; font-weight:normal}\n");
  printf("all.greener {color:#000000; background-color:#00ffff; font-weight:normal}\n");
  printf("all.red {color:#000000; background-color:#ff0000; font-weight:normal}\n");
  printf("all.redder {color:#000000; background-color:#ff00ff; font-weight:normal}\n");
  printf("all.white {color:#ffffff; background-color:#000000; font-weight:normal}\n");
  printf("all.yellow {color:#ffff00; background-color:#000000; font-weight:normal}\n");
  printf("</style>\n");
  printf("</head>\n");
  printf("<body bgcolor=\"#000000\">\n");
  printf("<pre>\n");
  fgets(line, sizeof(line), fpdiff);
  fgets(line, sizeof(line), fpdiff);
  while (fgets(line, sizeof(line), fpdiff)) {
    action = line[0];
    line[0] = ' ';
    if (action == '@') {
      print_text_as_html(yellow, " ================================== SKIPPING ==================================\n");
    } else if (action == '-') {
      add_text_to_region(&deleted, line);
    } else if (action == '+' && deleted.count) {
      add_text_to_region(&added, line);
    } else {
      if (deleted.count) {
	if (added.count) {
	  diff_regions();
	} else {
	  print_text_as_html(red, deleted.buffer);
	  deleted.count = 0;
	}
      }
      print_text_as_html(action == '+' ? green : white, line);
    }
  }
  if (deleted.count) {
    if (added.count) {
      diff_regions();
    } else {
      print_text_as_html(red, deleted.buffer);
      deleted.count = 0;
    }
  }
  pclose(fpdiff);
  printf("</pre>\n");
  printf("</body>\n");
  printf("</html>\n");
  remove(delfile);
  remove(addfile);
  remove(diffile);
  return 0;
}
