static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/path.c,v 1.10 1993/06/07 06:33:14 deyke Exp $";

#define _HPUX_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern char *strdup();

#define NULLCHAR        ((char *) 0)

#define ALEN            6       /* Number of chars in callsign field */
#define AXALEN          7       /* Total AX.25 address length, including SSID */
#define SSID            0x1e    /* Sub station ID */

#define AXROUTESIZE     499

struct iface {
  struct iface *next;
  char *name;
  int cnt;
};

struct axroute_tab {
  char call[AXALEN];
  struct axroute_tab *digi;
  struct iface *ifp;
  long time;
  struct axroute_tab *next;
};

struct axroute_saverecord_1 {
  char call[AXALEN];
  char digi[AXALEN];
  long time;
/*char ifname[]; */
};

static char axroutefile[] = "/tcp/axroute_data";
static struct axroute_tab *axroute_tab[AXROUTESIZE];
static struct iface *Ifaces;

static int axroute_hash(char *call);
static struct axroute_tab *axroute_tabptr(char *call, int create);
static struct iface *ifaceptr(char *name);
static void axroute_loadfile(void);
static void doroutelistentry(struct axroute_tab *rp);
static int doroutelist(int argc, char *argv[]);
static int doroutestat(void);
static void hash_performance(void);

/*---------------------------------------------------------------------------*/

/* Convert encoded AX.25 address to printable string */

char *pax25(char *e, char *addr)
{
	int i;
	char c;
	char *cp;

	cp = e;
	for(i=ALEN;i != 0;i--){
		c = (*addr++ >> 1) & 0x7f;
		if(c != ' ')
			*cp++ = c;
	}
	if ((*addr & SSID) != 0)
		sprintf(cp,"-%d",(*addr >> 1) & 0xf);   /* ssid */
	else
		*cp = '\0';
	return e;
}

/*---------------------------------------------------------------------------*/

/*
 * setcall - convert callsign plus substation ID of the form
 * "KA9Q-0" to AX.25 (shifted) address format
 *   Address extension bit is left clear
 *   Return -1 on error, 0 if OK
 */

int setcall(char *out, char *call)
{
	int csize;
	unsigned ssid;
	int i;
	char *dp;
	char c;

	if(out == NULLCHAR || call == NULLCHAR || *call == '\0'){
		return -1;
	}
	/* Find dash, if any, separating callsign from ssid
	 * Then compute length of callsign field and make sure
	 * it isn't excessive
	 */
	dp = strchr(call,'-');
	if(dp == NULLCHAR)
		csize = strlen(call);
	else
		csize = dp - call;
	if(csize > ALEN)
		return -1;
	/* Now find and convert ssid, if any */
	if(dp != NULLCHAR){
		dp++;   /* skip dash */
		ssid = atoi(dp);
		if(ssid > 15)
			return -1;
	} else
		ssid = 0;
	/* Copy upper-case callsign, left shifted one bit */
	for(i=0;i<csize;i++){
		c = *call++;
		if(islower(c))
			c = toupper(c);
		*out++ = c << 1;
	}
	/* Pad with shifted spaces if necessary */
	for(;i<ALEN;i++)
		*out++ = ' ' << 1;

	/* Insert substation ID field and set reserved bits */
	*out = 0x60 | (ssid << 1);
	return 0;
}

/*---------------------------------------------------------------------------*/

int addreq(char *a, char *b)
{
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	return (*a & SSID) == (*b & SSID);
}

/*---------------------------------------------------------------------------*/

void addrcp(char *to, char *from)
{
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to = (*from & SSID) | 0x60;
}

/*---------------------------------------------------------------------------*/

static int axroute_hash(char *call)
{
  long hashval;

  hashval  = ((*call++ << 23) & 0x0f000000);
  hashval |= ((*call++ << 19) & 0x00f00000);
  hashval |= ((*call++ << 15) & 0x000f0000);
  hashval |= ((*call++ << 11) & 0x0000f000);
  hashval |= ((*call++ <<  7) & 0x00000f00);
  hashval |= ((*call++ <<  3) & 0x000000f0);
  hashval |= ((*call   >>  1) & 0x0000000f);
  return hashval % AXROUTESIZE;
}

/*---------------------------------------------------------------------------*/

static struct axroute_tab *axroute_tabptr(char *call, int create)
{

  int hashval;
  struct axroute_tab *rp;

  hashval = axroute_hash(call);
  for (rp = axroute_tab[hashval]; rp && !addreq(rp->call, call); rp = rp->next) ;
  if (!rp && create) {
    rp = (struct axroute_tab *) calloc(1, sizeof(*rp));
    addrcp(rp->call, call);
    rp->next = axroute_tab[hashval];
    axroute_tab[hashval] = rp;
  }
  return rp;
}

/*---------------------------------------------------------------------------*/

static struct iface *ifaceptr(char *name)
{
  struct iface *ifp;

  if (!*name) return 0;
  for (ifp = Ifaces; ifp && strcmp(ifp->name, name); ifp = ifp->next) ;
  if (!ifp) {
    ifp = (struct iface *) calloc(1, sizeof(*ifp));
    ifp->name = strdup(name);
    ifp->next = Ifaces;
    Ifaces = ifp;
  }
  ifp->cnt++;
  return ifp;
}

/*---------------------------------------------------------------------------*/

static void axroute_loadfile(void)
{

  FILE * fp;
  char *cp;
  char ifname[1024];
  int c;
  struct axroute_saverecord_1 buf;
  struct axroute_tab *rp;

  if (!(fp = fopen(axroutefile, "rb"))) return;
  getc(fp);
  while (fread(&buf, sizeof(buf), 1, fp)) {
    cp = ifname;
    do {
      if ((c = getc(fp)) == EOF) {
	fclose(fp);
	return;
      }
    } while (*cp++ = c);
    rp = axroute_tabptr(buf.call, 1);
    if (buf.digi[0]) rp->digi = axroute_tabptr(buf.digi, 1);
    rp->ifp = ifaceptr(ifname);
    rp->time = buf.time;
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void doroutelistentry(struct axroute_tab *rp)
{

  char *cp, buf[1024];
  int i, n;
  struct axroute_tab *rp_stack[20];
  struct iface *ifp = 0;
  struct tm *tm;

  tm = gmtime(&rp->time);
  cp = pax25(buf, rp->call);
  for (n = 0; rp; rp = rp->digi) {
    rp_stack[++n] = rp;
    ifp = rp->ifp;
  }
  for (i = n; i > 1; i--) {
    strcat(cp, i == n ? " via " : ",");
    while (*cp) cp++;
    pax25(cp, rp_stack[i]->call);
  }
  printf("%2d-%.3s  %-9s  %s\n",
	 tm->tm_mday,
	 "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	 ifp ? ifp->name : "???",
	 buf);
}

/*---------------------------------------------------------------------------*/

static int doroutelist(int argc, char *argv[])
{

  char call[AXALEN];
  int i;
  struct axroute_tab *rp;

  puts("Date    Interface  Path");
  if (argc < 2) {
    for (i = 0; i < AXROUTESIZE; i++)
      for (rp = axroute_tab[i]; rp; rp = rp->next) doroutelistentry(rp);
    return 0;
  }
  argc--;
  argv++;
  for (; argc > 0; argc--, argv++)
    if (setcall(call, *argv) || !(rp = axroute_tabptr(call, 0)))
      printf("** Not in table ** %s\n", *argv);
    else
      doroutelistentry(rp);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doroutestat(void)
{

  int total = 0;
  struct iface *ifp;

  puts("Interface  Count");
  for (ifp = Ifaces; ifp; ifp = ifp->next) {
    printf("%-9s  %5d\n", ifp->name, ifp->cnt);
    total += ifp->cnt;
  }
  puts("---------  -----");
  printf("  total    %5d\n", total);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void hash_performance(void)
{

  int i, len;
  struct axroute_tab *rp;

  puts("Index  Length");
  for (i = 0; i < AXROUTESIZE; i++) {
    len = 0;
    for (rp = axroute_tab[i]; rp; rp = rp->next) len++;
    printf("%5d  %6d  ", i, len);
    while (len--) putchar('*');
    putchar('\n');
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
  axroute_loadfile();
  if (argc >= 2 && !strcmp(argv[1], "hash"))
    hash_performance();
  else if (argc >= 2 && !strcmp(argv[1], "stat"))
    doroutestat();
  else
    doroutelist(argc, argv);
  return 0;
}

