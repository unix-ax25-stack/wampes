/* @(#) $Id: callvalid.c,v 1.4 1996/08/12 18:53:41 deyke Exp $ */

#include <ctype.h>
#include <string.h>

#include "callvalid.h"

/*---------------------------------------------------------------------------*/

int callvalid(const char *call)
{
  int d, l;

  l = strlen(call);
  if (l < 3 || l > 6) return 0;
  if (isdigit(call[0] & 0xff) && isdigit(call[1] & 0xff)) return 0;
  if (!(isdigit(call[1] & 0xff) || isdigit(call[2] & 0xff))) return 0;
  if (!isalpha(call[l-1] & 0xff)) return 0;
  d = 0;
  for (; *call; call++) {
    if (!isalnum(*call & 0xff)) return 0;
    if (isdigit(*call & 0xff)) d++;
  }
  if (d < 1 || d > 2) return 0;
  return 1;
}
