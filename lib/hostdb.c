/* Record format of the host database.  See hostdb.h. */

#include <string.h>

#include "hostdb.h"

/*---------------------------------------------------------------------------*/

int hostdb_encode(int family, const unsigned char *addr, unsigned char *rec)
{
  int n;

  if (!addr || !rec) return 0;

  switch (family) {
  case HOSTDB_V4:
    n = 4;
    break;
  case HOSTDB_V6:
    n = 16;
    break;
  default:
    return 0;
  }
  rec[0] = (unsigned char) family;
  memcpy(rec + 1, addr, (size_t) n);
  return n + 1;
}

/*---------------------------------------------------------------------------*/

int hostdb_decode(const unsigned char *rec, int reclen, int *family,
	unsigned char *addr)
{
  if (!rec || !family || !addr) return 0;

  if (reclen == 5 && rec[0] == HOSTDB_V4) {
    *family = HOSTDB_V4;
    memcpy(addr, rec + 1, 4);
    return 4;
  }
  if (reclen == 17 && rec[0] == HOSTDB_V6) {
    *family = HOSTDB_V6;
    memcpy(addr, rec + 1, 16);
    return 16;
  }

  /* Pre-existing layout: an int32 or a long in host byte order, IPv4 only.
   * Recover the address the way the writer meant it, so a database that has
   * not been regenerated yet still resolves.  The four significant bytes sit
   * at the low end of the stored word: at the front on a little endian
   * machine, at the back on a big endian one.
   */
  if (reclen == 4 || reclen == 8) {
    static const unsigned int one = 1;
    int little = *(const unsigned char *) &one;
    const unsigned char *p = little ? rec : rec + reclen - 4;
    int i;

    for (i = 0; i < 4; i++)
      addr[i] = little ? p[3 - i] : p[i];
    *family = HOSTDB_V4;
    return 4;
  }

  return 0;
}
