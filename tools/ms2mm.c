#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/ms2mm.c,v 1.1 1994/07/12 16:31:57 deyke Exp $";
#endif

#include <string.h>

static print_header(int n)
{

  char *cp;
  char line[1024];

  printf(".H %d \"", n);
  gets(line);
  cp = strpbrk(line, "<[\\");
  if (cp > line) {
    cp[-1] = 0;
    printf("%s\" \" %s\"\n", line, cp);
  } else {
    printf("%s\"\n", line);
  }
}

/*---------------------------------------------------------------------------*/

int main()
{

  char line[1024];

  while (gets(line)) {
    if (!strcmp(line, ".NH 1")) {
      print_header(1);
    } else if (!strcmp(line, ".NH 2")) {
      print_header(2);
    } else if (!strcmp(line, ".NH 3")) {
      print_header(3);
    } else if (!strcmp(line, ".NH 4")) {
      print_header(4);
    } else if (!strcmp(line, ".LP")) {
      puts(".P");
    } else {
      puts(line);
    }
  }

  return 0;
}

