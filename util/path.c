static char  rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/path.c,v 1.2 1990/10/28 21:17:49 deyke Exp $";

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NULLCHAR        ((char *) 0)

#define ALEN            6       /* Number of chars in callsign field */
#define AXALEN          7       /* Total AX.25 address length, including SSID */
#define SSID            0x1e    /* Sub station ID */

#define ASY_MAX         64
#define AXROUTESIZE     499

struct axroute_tab {
  char  call[AXALEN];
  struct axroute_tab *digi;
  short  dev;
  long  time;
  struct axroute_tab *next;
};

struct axroutesaverecord {
  char  call[AXALEN];
  char  pad1;
  char  digi[AXALEN];
  char  pad2;
  short  dev;
  long  time;
};

static char  axroutefile[] = "/tcp/axroute_data";
static struct axroute_tab *axroute_tab[AXROUTESIZE];

/*---------------------------------------------------------------------------*/

#ifdef __TURBOC__       /* PC specific functions */

static void swap2(p)
char  *p;
{
  char  t;

  t = p[0];
  p[0] = p[1];
  p[1] = t;
}

static void swap4(p)
char  *p;
{
  char  t;

  t = p[0];
  p[0] = p[3];
  p[3] = t;

  t = p[1];
  p[1] = p[2];
  p[2] = t;
}

#endif

/*---------------------------------------------------------------------------*/

/* Convert encoded AX.25 address to printable string */
char *
pax25(e,addr)
char *e;
char *addr;
{
	register int i;
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
int
setcall(out,call)
char *out;
char *call;
{
	int csize;
	unsigned ssid;
	register int i;
	register char *dp;
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

int
addreq(a,b)
register char *a,*b;
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

void
addrcp(to,from)
register char *to,*from;
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

static int  axroute_hash(call)
char  *call;
{
  long  hashval;

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

static struct axroute_tab *axroute_tabptr(call, create)
char  *call;
int  create;
{

  int  hashval;
  struct axroute_tab *rp;

  hashval = axroute_hash(call);
  for (rp = axroute_tab[hashval]; rp && !addreq(rp->call, call); rp = rp->next) ;
  if (!rp && create) {
    rp = (struct axroute_tab *) calloc(1, sizeof(struct axroute_tab ));
    addrcp(rp->call, call);
    rp->next = axroute_tab[hashval];
    axroute_tab[hashval] = rp;
  }
  return rp;
}

/*---------------------------------------------------------------------------*/

static void axroute_loadfile()
{

  FILE * fp;
  struct axroute_tab *rp;
  struct axroutesaverecord buf;

  if (!(fp = fopen(axroutefile, "rb"))) return;
  while (fread((char *) & buf, sizeof(buf), 1, fp)) {
    rp = axroute_tabptr(buf.call, 1);
    if (buf.digi[0]) rp->digi = axroute_tabptr(buf.digi, 1);
    rp->dev = buf.dev;
    rp->time = buf.time;
#ifdef __TURBOC__
    swap2((char *) & rp->dev);
    swap4((char *) & rp->time);
#endif
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void doroutelistentry(rp)
struct axroute_tab *rp;
{

  char  *cp, buf[1024];
  char  devstr[20];
  int  i, n;
  short  dev;
  struct axroute_tab *rp_stack[20];
  struct tm *tm;

  tm = gmtime(&rp->time);
  cp = pax25(buf, rp->call);
  for (n = 0; rp; rp = rp->digi) {
    rp_stack[++n] = rp;
    dev = rp->dev;
  }
  for (i = n; i > 1; i--) {
    strcat(cp, i == n ? " via " : ",");
    while (*cp) cp++;
    pax25(cp, rp_stack[i]->call);
  }
  if (dev < 0)
    strcpy(devstr, "?");
  else
    sprintf(devstr, "%d", dev);
  printf("%2d-%.3s  %9s  %s\n",
	 tm->tm_mday,
	 "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	 devstr,
	 buf);
}

/*---------------------------------------------------------------------------*/

static int  doroutelist(argc, argv)
int  argc;
char  *argv[];
{

  char  call[AXALEN];
  int  i;
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
      printf("*** Not in table *** %s\n", *argv);
    else
      doroutelistentry(rp);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  doroutestat()
{

  int  count[ASY_MAX], total;
  int  i, dev;
  struct axroute_tab *rp, *dp;

  memset(count, 0, sizeof(count));
  for (i = 0; i < AXROUTESIZE; i++)
    for (rp = axroute_tab[i]; rp; rp = rp->next) {
      for (dp = rp; dp->digi; dp = dp->digi) ;
      if (dp->dev >= 0 && dp->dev < ASY_MAX) count[dp->dev]++;
    }
  puts("Interface  Count");
  total = 0;
  for (dev = 0; dev < ASY_MAX; dev++) {
    if (count[dev]) printf("%9d  %5d\n", dev, count[dev]);
    total += count[dev];
  }
  puts("---------  -----");
  printf("  total    %5d\n", total);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void hash_performance()
{

  int  i, len;
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

int  main(argc, argv)
int  argc;
char  **argv;
{
  axroute_loadfile();
  if (!strcmp(argv[1], "hash"))
    hash_performance();
  else if (!strcmp(argv[1], "stat"))
    doroutestat();
  else
    doroutelist(argc, argv);
  return 0;
}

