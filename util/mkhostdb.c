#ifndef __lint
static const char rcsid[] = "@(#) $Id: mkhostdb.c,v 1.18 2016/03/13 06:37:27 dl9sau Exp $";
#endif

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#include "dbmapi.h"
#include "hostdb.h"

#define DBHOSTADDR      TCPDIR "/hostaddr"
#define DBHOSTNAME      TCPDIR "/hostname"
#define DOMAINFILE      TCPDIR "/domain.txt"
#define HOSTSFILE       TCPDIR "/hosts"
#define LOCALDOMAIN     "ampr.org"
#define LOCALDOMAINFILE TCPDIR "/domain.local"

#if USE_GDBM
static GDBM_FILE Dbhostaddr;
static GDBM_FILE Dbhostname;
#else
static DBM *Dbhostaddr;
static DBM *Dbhostname;
#endif
static char origin[1024];

/*---------------------------------------------------------------------------*/

/* Parse an address literal of either family.  Stores HOSTDB_V4 or HOSTDB_V6
 * in *family and the address in network byte order in addr, which needs room
 * for 16 bytes.  Returns 0 on success, -1 if it is not an address.
 *
 * Addresses in TCPDIR/hosts are written plainly, without the brackets the
 * configuration uses for "host:service" - there is no service here to keep
 * the colons apart from.
 */

static int parse_addr(const char *s, int *family, unsigned char *addr)
{
  if (!s || !*s) return -1;

  if (strchr(s, ':')) {
    struct in6_addr a6;

    if (inet_pton(AF_INET6, s, &a6) != 1) return -1;
    memcpy(addr, &a6, 16);
    *family = HOSTDB_V6;
    return 0;
  }
  {
    struct in_addr a4;

    if (inet_pton(AF_INET, s, &a4) != 1) return -1;
    memcpy(addr, &a4, 4);
    *family = HOSTDB_V4;
    return 0;
  }
}

/*---------------------------------------------------------------------------*/

/* Printable form of a decoded record */

static char *addr_to_string(int family, const unsigned char *addr)
{
  static char buf[64];

  buf[0] = 0;
  if (family == HOSTDB_V6)
    inet_ntop(AF_INET6, addr, buf, sizeof(buf));
  else
    inet_ntop(AF_INET, addr, buf, sizeof(buf));
  return buf;
}

/*---------------------------------------------------------------------------*/

static void store_in_db(const char *name, const char *addrstr)
{

  datum daddr;
  datum dname;
  int i;
  int family;
  int reclen;
  unsigned char addr[16];
  unsigned char rec[HOSTDB_RECLEN];

  if (parse_addr(addrstr, &family, addr)) return;
  /* 0.0.0.0 and 255.255.255.255 are not hosts */
  if (family == HOSTDB_V4 &&
      (!memcmp(addr, "\0\0\0\0", 4) ||
       !memcmp(addr, "\377\377\377\377", 4))) return;
  if (!(reclen = hostdb_encode(family, addr, rec))) return;
  dname.dptr = (char *) name;
  dname.dsize = strlen(name) + 1;
  daddr.dptr = (char *) rec;
  daddr.dsize = reclen;
#if USE_GDBM
  i = gdbm_store(Dbhostaddr, dname, daddr, GDBM_INSERT);
#else
  i = dbm_store(Dbhostaddr, dname, daddr, DBM_INSERT);
#endif
  if (i < 0) {
    perror("dbm_store");
    exit(1);
  }
  if (i > 0) fprintf(stderr, "duplicate name: %s\n", name);
#if USE_GDBM
  i = gdbm_store(Dbhostname, daddr, dname, GDBM_INSERT);
#else
  i = dbm_store(Dbhostname, daddr, dname, DBM_INSERT);
#endif
  if (i < 0) {
    perror("dbm_store");
    exit(1);
  }
  if (i > 0) fprintf(stderr, "duplicate addr: %s\n", addrstr);
}

/*---------------------------------------------------------------------------*/

static void fix_line(char *line)
{
  for (; *line; line++) {
    if (*line == ';' || *line == '#') {
      *line = 0;
      return;
    }
    if (*line >= 'A' && *line <= 'Z') *line = tolower(*line);
  }
}

/*---------------------------------------------------------------------------*/

static char *fix_name(const char *name)
{

  int len;
  static char fullname[1024];

  if (!strcmp(name, "@"))
    strcpy(fullname, origin);
  else if (!*name || name[strlen(name)-1] == '.')
    strcpy(fullname, name);
  else
    sprintf(fullname, "%s.%s", name, origin);
  len = strlen(fullname);
  if (len && fullname[len-1] == '.') fullname[len-1] = 0;
  return fullname;
}

/*---------------------------------------------------------------------------*/

static void read_hosts_file(const char *filename)
{

  char addrstr[1024];
  char line[1024];
  char name[1024];
  FILE *fp;

  if (!(fp = fopen(filename, "r"))) {
    perror(filename);
    return;
  }
  while (fgets(line, sizeof(line), fp)) {
    fix_line(line);
    if (sscanf(line, "%s %s", addrstr, name) == 2) {
      strcat(name, ".");
      strcat(name, LOCALDOMAIN);
      store_in_db(name, addrstr);
    }
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void read_domain_file(const char *filename)
{

  char line[1024];
  char name[1024];
  char *p;
  datum daddr;
  datum dname;
  FILE *fp;
  static const char delim[] = " \t\n";

  strcpy(origin, LOCALDOMAIN);
  *name = 0;
  if (!(fp = fopen(filename, "r"))) {
    perror(filename);
    return;
  }
  while (fgets(line, sizeof(line), fp)) {
    fix_line(line);

    if (!strncmp(line, "$origin", 7)) {
      if ((p = strtok(line + 7, delim))) strcpy(origin, p);
      continue;
    }

    if (!(p = strtok(line, delim))) continue;

    if (!isspace(*line & 0xff)) {
      strcpy(name, p);
      p = strtok(0, delim);
    }

    while (p && (isdigit(*p & 0xff) || !strcmp(p, "in")))
      p = strtok(0, delim);
    if (!p) continue;

    if (strcmp(p, "a")) continue;

    if (!(p = strtok(0, delim))) continue;

    store_in_db(fix_name(name), p);

  }
  fclose(fp);

  strcpy(origin, LOCALDOMAIN);
  *name = 0;
  if (!(fp = fopen(filename, "r"))) {
    perror(filename);
    return;
  }
  while (fgets(line, sizeof(line), fp)) {
    fix_line(line);

    if (!strncmp(line, "$origin", 7)) {
      if ((p = strtok(line + 7, delim))) strcpy(origin, p);
      continue;
    }

    if (!(p = strtok(line, delim))) continue;

    if (!isspace(*line & 0xff)) {
      strcpy(name, p);
      p = strtok(0, delim);
    }

    while (p && (isdigit(*p & 0xff) || !strcmp(p, "in")))
      p = strtok(0, delim);
    if (!p) continue;

    if (strcmp(p, "cname")) continue;

    if (!(p = strtok(0, delim))) continue;

    dname.dptr = fix_name(p);
    dname.dsize = strlen(dname.dptr) + 1;
#if USE_GDBM
    daddr = gdbm_fetch(Dbhostaddr, dname);
#else
    daddr = dbm_fetch(Dbhostaddr, dname);
#endif
    if (!daddr.dptr) {
      fprintf(stderr, "no such key: %s\n", dname.dptr);
      continue;
    }
    {
      int family;
      unsigned char a[16];

      if (!hostdb_decode((unsigned char *) daddr.dptr, daddr.dsize, &family, a))
	continue;
      store_in_db(fix_name(name), addr_to_string(family, a));
    }

  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void qaddr(const char *name)
{

  char fullname[1024];
  datum daddr;
  datum dname;
  int len;
  int family;
  unsigned char addr[16];

  strcpy(fullname, name);
  len = strlen(fullname);

  if (len && fullname[len-1] == '.') {
    fullname[len-1] = 0;
    dname.dptr = fullname;
    dname.dsize = strlen(fullname) + 1;
#if USE_GDBM
    daddr = gdbm_fetch(Dbhostaddr, dname);
#else
    daddr = dbm_fetch(Dbhostaddr, dname);
#endif
    if (daddr.dptr &&
	hostdb_decode((unsigned char *) daddr.dptr, daddr.dsize, &family, addr)) {
      printf("%s  %s\n", addr_to_string(family, addr), fullname);
    } else
      fprintf(stderr, "no such key: %s\n", fullname);
    return;
  }

  strcat(fullname, ".");
  strcat(fullname, LOCALDOMAIN);
  dname.dptr = fullname;
  dname.dsize = strlen(fullname) + 1;
#if USE_GDBM
  daddr = gdbm_fetch(Dbhostaddr, dname);
#else
  daddr = dbm_fetch(Dbhostaddr, dname);
#endif
  if (daddr.dptr &&
      hostdb_decode((unsigned char *) daddr.dptr, daddr.dsize, &family, addr)) {
    printf("%s  %s\n", addr_to_string(family, addr), fullname);
    return;
  }

  dname.dptr = (char *) name;
  dname.dsize = strlen(name) + 1;
#if USE_GDBM
  daddr = gdbm_fetch(Dbhostaddr, dname);
#else
  daddr = dbm_fetch(Dbhostaddr, dname);
#endif
  if (daddr.dptr &&
      hostdb_decode((unsigned char *) daddr.dptr, daddr.dsize, &family, addr)) {
    printf("%s  %s\n", addr_to_string(family, addr), name);
  } else
    fprintf(stderr, "no such key: %s\n", name);
}

/*---------------------------------------------------------------------------*/

static void qname(const char *addrstr)
{

  datum daddr;
  datum dname;
  int family;
  int reclen;
  unsigned char addr[16];
  unsigned char rec[HOSTDB_RECLEN];

  if (parse_addr(addrstr, &family, addr) ||
      !(reclen = hostdb_encode(family, addr, rec))) {
    fprintf(stderr, "no such key: %s\n", addrstr);
    return;
  }
  daddr.dptr = (char *) rec;
  daddr.dsize = reclen;
#if USE_GDBM
  dname = gdbm_fetch(Dbhostname, daddr);
#else
  dname = dbm_fetch(Dbhostname, daddr);
#endif
  if (!dname.dptr && family == HOSTDB_V4) {
    /* A database from an older mkhostdb keys on a bare long */
    long old;

    old = ((long) addr[0] << 24) | ((long) addr[1] << 16) |
	  ((long) addr[2] << 8) | (long) addr[3];
    daddr.dptr = (char *) &old;
    daddr.dsize = sizeof(old);
#if USE_GDBM
    dname = gdbm_fetch(Dbhostname, daddr);
#else
    dname = dbm_fetch(Dbhostname, daddr);
#endif
  }
  if (dname.dptr)
    printf("%s  %s\n", addr_to_string(family, addr), dname.dptr);
  else
    fprintf(stderr, "no such key: %s\n", addrstr);
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
  int i;

  if (argc >= 1 && strstr(*argv, "qaddr")) {
#if USE_GDBM
    if (!(Dbhostaddr = gdbm_open(DBHOSTADDR, 0, GDBM_READER, 0644, NULL))) {
#else
    if (!(Dbhostaddr = dbm_open(DBHOSTADDR, O_RDONLY, 0644))) {
#endif
      perror(DBHOSTADDR);
      exit(1);
    }
    for (i = 1; i < argc; i++) qaddr(argv[i]);
#if USE_GDBM
    gdbm_close(Dbhostaddr);
#else
    dbm_close(Dbhostaddr);
#endif
  } else if (argc >= 1 && strstr(*argv, "qname")) {
#if USE_GDBM
    if (!(Dbhostname = gdbm_open(DBHOSTNAME, 0, GDBM_READER, 0644, NULL))) {
#else
    if (!(Dbhostname = dbm_open(DBHOSTNAME, O_RDONLY, 0644))) {
#endif
      perror(DBHOSTNAME);
      exit(1);
    }
    for (i = 1; i < argc; i++) qname(argv[i]);
#if USE_GDBM
    gdbm_close(Dbhostname);
#else
    dbm_close(Dbhostname);
#endif
  } else {
    remove(DBHOSTADDR ".db");
    remove(DBHOSTADDR ".dir");
    remove(DBHOSTADDR ".pag");
    remove(DBHOSTNAME ".db");
    remove(DBHOSTNAME ".dir");
    remove(DBHOSTNAME ".pag");
#if USE_GDBM
    if (!(Dbhostname = gdbm_open(DBHOSTNAME, 0, GDBM_WRCREAT, 0644, NULL))) {
#else
    if (!(Dbhostname = dbm_open(DBHOSTNAME, O_RDWR | O_CREAT, 0644))) {
#endif
      perror(DBHOSTNAME);
      exit(1);
    }
#if USE_GDBM
    if (!(Dbhostaddr = gdbm_open(DBHOSTADDR, 0, GDBM_WRCREAT, 0644, NULL))) {
#else
    if (!(Dbhostaddr = dbm_open(DBHOSTADDR, O_RDWR | O_CREAT, 0644))) {
#endif
      perror(DBHOSTADDR);
      exit(1);
    }
    store_in_db("localhost", "127.0.0.1");
    read_domain_file(LOCALDOMAINFILE);
    read_hosts_file(HOSTSFILE);
    read_domain_file(DOMAINFILE);
#if USE_GDBM
    gdbm_close(Dbhostname);
    gdbm_close(Dbhostaddr);
#else
    dbm_close(Dbhostname);
    dbm_close(Dbhostaddr);
#endif
  }
  return 0;
}
