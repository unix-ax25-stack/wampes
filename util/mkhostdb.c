#ifndef __lint
static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/mkhostdb.c,v 1.1 1992/11/09 16:32:45 deyke Exp $";
#endif

#define _HPUX_SOURCE

#include <ctype.h>
#include <fcntl.h>
#include <ndbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__STDC__)
#define __ARGS(x)       x
#else
#define __ARGS(x)       ()
#define const
#endif

#define DBHOSTADDR      "/tcp/hostaddr"
#define DBHOSTNAME      "/tcp/hostname"
#define DOMAINFILE      "/tcp/domain.txt"
#define HOSTSFILE       "/tcp/hosts"
#define LOCALDOMAIN     ".ampr.org"

static DBM *dbhostaddr;
static DBM *dbhostname;

static int aton __ARGS((const char *name, long *addrptr));
static char *ntoa __ARGS((long addr));
static void store_in_db __ARGS((const char *name, const char *addrstr));
static void read_hosts_file __ARGS((void));
static void read_domain_file __ARGS((void));
static void qaddr __ARGS((const char *name));
static void qname __ARGS((const char *addrstr));
int main __ARGS((int argc, char **argv));

/*---------------------------------------------------------------------------*/

static int aton(name, addrptr)
const char *name;
long *addrptr;
{

  const char * p;
  int i;
  long addr;

  if (!name || !isdigit(*name & 0xff)) return (-1);
  for (p = name; *p; p++)
    if (!isdigit(*p & 0xff) && *p != '.') return (-1);
  addr = 0;
  for (i = 24; i >= 0; i -= 8) {
    addr |= atol(name) << i;
    if (!(name = strchr(name, '.'))) break;
    name++;
    if (!*name) break;
  }
  *addrptr = addr;
  return 0;
}

/*---------------------------------------------------------------------------*/

static char *ntoa(addr)
long addr;
{
  static char buf[16];

  sprintf(buf, "%d.%d.%d.%d",
	  (addr >> 24) & 0xff,
	  (addr >> 16) & 0xff,
	  (addr >>  8) & 0xff,
	  (addr      ) & 0xff);
  return buf;
}

/*---------------------------------------------------------------------------*/

static void store_in_db(name, addrstr)
const char *name;
const char *addrstr;
{

  datum daddr;
  datum dname;
  int i;
  long addr;

  if (aton(addrstr, &addr)) return;
  dname.dptr = (char *) name;
  dname.dsize = strlen(dname.dptr) + 1;
  daddr.dptr = (char *) & addr;
  daddr.dsize = sizeof(addr);
  i = dbm_store(dbhostaddr, dname, daddr, DBM_INSERT);
  if (i < 0) {
    perror("dbm_store");
    exit(1);
  }
  if (i > 0) fprintf(stderr, "duplicate name: %s\n", name);
  i = dbm_store(dbhostname, daddr, dname, DBM_INSERT);
  if (i < 0) {
    perror("dbm_store");
    exit(1);
  }
  if (i > 0) fprintf(stderr, "duplicate addr: %s\n", addrstr);
}

/*---------------------------------------------------------------------------*/

static void read_hosts_file()
{

  FILE * fp;
  char *p;
  char addrstr[1024];
  char line[1024];
  char name[1024];

  if (!(fp = fopen(HOSTSFILE, "r"))) {
    perror(HOSTSFILE);
    exit(1);
  }
  while (fgets(line, sizeof(line), fp)) {
    for (p = line; *p; p++) {
      if (*p == ';' || *p == '#') {
	*p = 0;
	break;
      }
      *p = tolower(*p & 0xff);
    }
    if (sscanf(line, "%s %s", addrstr, name) == 2) {
      strcat(name, LOCALDOMAIN);
      store_in_db(name, addrstr);
    }
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void read_domain_file()
{

  FILE * fp;
  char *p;
  char fullname[1024];
  char line[1024];
  char name[1024];
  char origin[1024];
  const char * delim = " \t\n";

  if (!(fp = fopen(DOMAINFILE, "r"))) {
    perror(DOMAINFILE);
    exit(1);
  }
  *origin = 0;
  *name = 0;
  while (fgets(line, sizeof(line), fp)) {

    for (p = line; *p; p++) {
      if (*p == ';' || *p == '#') {
	*p = 0;
	break;
      }
      *p = tolower(*p & 0xff);
    }

    if (!strncmp(line, "$origin", 7)) {
      strcpy(origin, strtok(line + 7, delim));
      continue;
    }

    if (!(p = strtok(line, delim))) continue;

    if (!isspace(*line & 0xff)) {
      strcpy(name, p);
      if (!(p = strtok((char *) 0, delim))) continue;
    }

    if (isdigit(*p & 0xff)) {
      /* ttl */
      if (!(p = strtok((char *) 0, delim))) continue;
    }

    if (!strcmp(p, "in")) {
      /* class */
      if (!(p = strtok((char *) 0, delim))) continue;
    }

    if (strcmp(p, "a")) continue;

    if (!(p = strtok((char *) 0, delim))) continue;

    if (!strcmp(name, "@"))
      strcpy(fullname, origin);
    else if (name[strlen(name)-1] == '.')
      strcpy(fullname, name);
    else
      sprintf(fullname, "%s.%s", name, origin);

    if (fullname[strlen(fullname)-1] == '.') fullname[strlen(fullname)-1] = 0;

    store_in_db(fullname, p);

  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void qaddr(name)
const char *name;
{

  char fullname[1024];
  datum daddr;
  datum dname;
  int len;
  long addr;

  strcpy(fullname, name);
  len = strlen(fullname);

  if (len && fullname[len-1] == '.') {
    fullname[len-1] = 0;
    dname.dptr = fullname;
    dname.dsize = strlen(dname.dptr) + 1;
    daddr = dbm_fetch(dbhostaddr, dname);
    if (daddr.dptr) {
      memcpy(&addr, daddr.dptr, sizeof(addr));
      printf("%s  %s\n", ntoa(addr), name);
    } else
      fprintf(stderr, "no such key: %s\n", name);
    return;
  }

  strcat(fullname, LOCALDOMAIN);
  dname.dptr = fullname;
  dname.dsize = strlen(dname.dptr) + 1;
  daddr = dbm_fetch(dbhostaddr, dname);
  if (daddr.dptr) {
    memcpy(&addr, daddr.dptr, sizeof(addr));
    printf("%s  %s\n", ntoa(addr), name);
    return;
  }

  dname.dptr = (char *) name;
  dname.dsize = strlen(dname.dptr) + 1;
  daddr = dbm_fetch(dbhostaddr, dname);
  if (daddr.dptr) {
    memcpy(&addr, daddr.dptr, sizeof(addr));
    printf("%s  %s\n", ntoa(addr), name);
  } else
    fprintf(stderr, "no such key: %s\n", name);
}

/*---------------------------------------------------------------------------*/

static void qname(addrstr)
const char *addrstr;
{

  datum daddr;
  datum dname;
  long addr;

  if (aton(addrstr, &addr)) {
    fprintf(stderr, "no such key: %s\n", addrstr);
    return;
  }
  daddr.dptr = (char *) & addr;
  daddr.dsize = sizeof(addr);
  dname = dbm_fetch(dbhostname, daddr);
  if (dname.dptr)
    printf("%s  %s\n", ntoa(addr), dname.dptr);
  else
    fprintf(stderr, "no such key: %s\n", addrstr);
}

/*---------------------------------------------------------------------------*/

int main(argc, argv)
int argc;
char **argv;
{
  char buf[1024];

  if (strstr(*argv, "qaddr")) {
    if (!(dbhostaddr = dbm_open(DBHOSTADDR, O_RDONLY, 0644))) {
      perror(DBHOSTADDR);
      exit(1);
    }
    qaddr(argv[1]);
    dbm_close(dbhostaddr);
  } else if (strstr(*argv, "qname")) {
    if (!(dbhostname = dbm_open(DBHOSTNAME, O_RDONLY, 0644))) {
      perror(DBHOSTNAME);
      exit(1);
    }
    qname(argv[1]);
    dbm_close(dbhostname);
  } else {
    sprintf(buf, "%s.dir", DBHOSTADDR);
    unlink(buf);
    sprintf(buf, "%s.pag", DBHOSTADDR);
    unlink(buf);
    sprintf(buf, "%s.dir", DBHOSTNAME);
    unlink(buf);
    sprintf(buf, "%s.pag", DBHOSTNAME);
    unlink(buf);
    if (!(dbhostname = dbm_open(DBHOSTNAME, O_RDWR | O_CREAT, 0644))) {
      perror(DBHOSTNAME);
      exit(1);
    }
    if (!(dbhostaddr = dbm_open(DBHOSTADDR, O_RDWR | O_CREAT, 0644))) {
      perror(DBHOSTADDR);
      exit(1);
    }
    store_in_db("all-zeros-broadcast", "0.0.0.0");
    store_in_db("all-ones-broadcast", "255.255.255.255");
    store_in_db("localhost", "127.0.0.1");
    read_hosts_file();
    read_domain_file();
    dbm_close(dbhostname);
    dbm_close(dbhostaddr);
  }
  return 0;
}

