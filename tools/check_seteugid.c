#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/check_seteugid.c,v 1.2 1994/06/16 13:14:39 deyke Exp $";
#endif

#include <stdio.h>
#include <unistd.h>

#include "seteugid.h"

/*---------------------------------------------------------------------------*/

static void print_ids(void)
{
  printf("ruid = %4d euid = %4d rgid = %4d egid = %4d\n",
	 getuid(), geteuid(), getgid(), getegid());
}

/*---------------------------------------------------------------------------*/

int main(void)
{
  print_ids();
  seteugid(1234, 4321);
  print_ids();
  seteugid(0, 0);
  print_ids();
  return 0;
}

