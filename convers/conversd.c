static char  rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/conversd.c,v 1.5 1988/09/02 20:36:59 dk5sg Exp $";

#include <sys/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <memory.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <utmp.h>

extern char  *calloc();
extern char  *malloc();
extern long  time();
extern struct utmp *getutent();
extern void endutent();
extern void exit();
extern void free();

#define PORT 3600

struct mbuf {
  struct mbuf *next;
  char  *data;
};

struct connection {
  int  type;                    /* Connection type */
#define CT_UNKNOWN      0
#define CT_USER         1
#define CT_HOST         2
#define CT_CLOSED       3
  char  name[80];               /* Name of user or host */
  char  host[80];               /* Name of host where user is logged on */
  struct connection *via;       /* Pointer to neighbor host */
  int  channel;                 /* Channel number */
  long  time;                   /* Login time */
  int  locked;                  /* Set if mesg already sent */
  int  fd;                      /* Socket file descriptor */
  int  fmask;                   /* Socket file descriptor mask */
  char  ibuf[2048];             /* Input buffer */
  int  icnt;                    /* Number of bytes in input buffer */
  struct mbuf *obuf;            /* Output queue */
  struct connection *next;      /* Linked list pointer */
};

static struct connection *connections;

/*---------------------------------------------------------------------------*/

static void appendstring(bpp, string)
struct mbuf **bpp;
char  *string;
{
  register struct mbuf *bp, *p;

  if (!*string) return;

  bp = (struct mbuf *) malloc(sizeof(struct mbuf ) + strlen(string) + 1);
  bp->next = 0;
  bp->data = strcpy((char *) (bp + 1), string);

  if (*bpp) {
    for (p = *bpp; p->next; p = p->next) ;
    p->next = bp;
  } else
    *bpp = bp;
}

/*---------------------------------------------------------------------------*/

static int  pullchar(bpp)
struct mbuf **bpp;
{

  int  c;
  register struct mbuf *bp, *p;

  bp = *bpp;
  c = (*bp->data++) & 0xff;
  while (bp && !*bp->data) {
    p = bp;
    bp = bp->next;
    free(p);
  }
  *bpp = bp;
  return c;
}

/*---------------------------------------------------------------------------*/

static void freequeue(bpp)
struct mbuf **bpp;
{
  register struct mbuf *bp, *p;

  bp = *bpp;
  while (bp) {
    p = bp;
    bp = bp->next;
    free(p);
  }
  *bpp = 0;
}

/*---------------------------------------------------------------------------*/

static void free_closed_connections()
{
  register struct connection *cp, *p;

  for (p = 0, cp = connections; cp; ) {
    if (cp->type != CT_CLOSED) {
      p = cp;
      cp = cp->next;
    } else if (p) {
      p->next = cp->next;
      close(cp->fd);
      freequeue(&cp->obuf);
      free((char *) cp);
      cp = p->next;
    } else {
      connections = cp->next;
      close(cp->fd);
      freequeue(&cp->obuf);
      free((char *) cp);
      cp = connections;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void sendchannel(cp, string)
struct connection *cp;
char  *string;
{
  register struct connection *p;

  for (p = connections; p; p = p->next)
    if (p->channel == cp->channel && p != cp) appendstring(&p->obuf, string);
}

/*---------------------------------------------------------------------------*/

static char  *getarg(line, all)
char  *line;
int  all;
{

  char  *arg;
  int  c, quote;
  static char  *p;

  if (line) p = line;
  while (isspace(*p & 0xff)) p++;
  if (all) return p;
  quote = '\0';
  if (*p == '"' || *p == '\'') quote = *p++;
  arg = p;
  if (quote) {
    if (!(p = strchr(p, quote))) p = "";
  } else
    while (*p && !isspace(*p & 0xff)) {
      c = tolower(*p & 0xff);
      *p++ = c;
    }
  if (*p) *p++ = '\0';
  return arg;
}

/*---------------------------------------------------------------------------*/

static char  *formatline(prefix, text)
char  *prefix, *text;
{

#define PREFIXLEN 10
#define LINELEN   79

  register char  *f, *t, *x;
  register int  l, lw;

  static char  buf[2048];

  for (f = prefix, t = buf, l = 0; *f; *t++ = *f++, l++) ;
  f = text;

  for (; ; ) {
    while (isspace(*f & 0xff)) f++;
    if (!*f) {
      *t++ = '\n';
      *t = '\0';
      return buf;
    }
    for (lw = 0, x = f; *x && !isspace(*x & 0xff); x++, lw++) ;
    if (l > PREFIXLEN && l + 1 + lw > LINELEN) {
      *t++ = '\n';
      l = 0;
    }
    do {
      *t++ = ' ';
      l++;
    } while (l < PREFIXLEN);
    while (lw--) {
      *t++ = *f++;
      l++;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void doconnect(flisten)
int  flisten;
{

  int  addrlen;
  int  fd;
  struct connection *cp;
  struct sockaddr_in addr_in;

  addrlen = sizeof(addr_in);
  if ((fd = accept(flisten, &addr_in, &addrlen)) < 0) return;
  cp = (struct connection *) calloc(1, sizeof(struct connection ));
  cp->next = connections;
  connections = cp;
  cp->fd = fd;
  cp->fmask = (1 << fd);
  cp->time = time(0l);
  appendstring(&cp->obuf, "conversd $Revision: 1.5 $  Type /HELP for help.\n");
}

/*---------------------------------------------------------------------------*/

static void bye_command(cp)
struct connection *cp;
{
  char  buffer[2048];

  cp->type = CT_CLOSED;
  sprintf(buffer, "*** %s signed off.\n", cp->name);
  sendchannel(cp, buffer);
}

/*---------------------------------------------------------------------------*/

static void channel_command(cp)
struct connection *cp;
{

  char  buffer[2048];
  int  newchannel;

  newchannel = atoi(getarg(0, 0));
  if (cp->channel == newchannel) return;
  sprintf(buffer, "*** %s signed off.\n", cp->name);
  sendchannel(cp, buffer);
  cp->channel = newchannel;
  sprintf(buffer, "*** %s signed on.\n", cp->name);
  sendchannel(cp, buffer);
}

/*---------------------------------------------------------------------------*/

static void help_command(cp)
struct connection *cp;
{
  appendstring(&cp->obuf, "Commands may be abbreviated. Commands are:\n");

  appendstring(&cp->obuf, "/?                   Print help information\n");
  appendstring(&cp->obuf, "/BYE                 Terminate the convers session\n");
  appendstring(&cp->obuf, "/CHANNEL n           Switch to channel n\n");
  appendstring(&cp->obuf, "/EXIT                Terminate the convers session\n");
  appendstring(&cp->obuf, "/HELP                Print help information\n");
  appendstring(&cp->obuf, "/INVITE user         Invite user to join your channel\n");
  appendstring(&cp->obuf, "/MSG user text...    Send a private message to user\n");
  appendstring(&cp->obuf, "/QUIT                Terminate the convers session\n");
  appendstring(&cp->obuf, "/WHO                 Print all users and their channel numbers\n");
  appendstring(&cp->obuf, "/WRITE user text...  Send a private message to user\n");

  appendstring(&cp->obuf, "***\n");
}

/*---------------------------------------------------------------------------*/

static void invite_command(cp)
struct connection *cp;
{

  char  *name;
  char  buffer[2048];
  char  line[80];
  int  fd;
  long  now;
  struct connection *p;
  struct stat stbuf;
  struct tm *tm;
  struct utmp *up;

  name = getarg(0, 0);
  now = time(0l);
  tm = localtime(&now);
  sprintf(buffer, "\n\007\007*** Message from %s at %d:%02d ...\nPlease join convers channel %d.\n\007\007\n", cp->name, tm->tm_hour, tm->tm_min, cp->channel);
  for (p = connections; p; p = p->next)
    if (!strcmp(p->name, name)) {
      if (p->channel != cp->channel) appendstring(&p->obuf, buffer);
      return;
    }
  while (up = getutent())
    if (up->ut_type == USER_PROCESS && !strcmp(up->ut_user, name)) {
      sprintf(line, "/dev/%s", up->ut_line);
      if (stat(line, &stbuf)) continue;
      if ((stbuf.st_mode & 2) != 2) continue;
      if ((fd = open(line, O_WRONLY, 0644)) < 0) continue;
      write(fd, buffer, (unsigned) strlen(buffer));
      close(fd);
      endutent();
      return;
    }
  endutent();
}

/*---------------------------------------------------------------------------*/

static void msg_command(cp)
struct connection *cp;
{

  char  *msg;
  char  *name;
  char  buffer[2048];
  int  found;
  struct connection *p;

  name = getarg(0, 0);
  sprintf(buffer, "<*%s*>:", cp->name);
  msg = formatline(buffer, getarg(0, 1));
  found = 0;
  for (p = connections; p; p = p->next)
    if (!strcmp(p->name, name)) {
      appendstring(&p->obuf, msg);
      found = 1;
    }
  if (!found) {
    sprintf(buffer, "*** No such user: %s\n", name);
    appendstring(&cp->obuf, buffer);
  }
}

/*---------------------------------------------------------------------------*/

static void name_command(cp)
struct connection *cp;
{
  char  buffer[2048];

  if (*cp->name) return;
  strcpy(cp->name, getarg(0, 0));
  sprintf(buffer, "*** %s signed on.\n", cp->name);
  sendchannel(cp, buffer);
}

/*---------------------------------------------------------------------------*/

static void who_command(cp)
struct connection *cp;
{

  char  buffer[2048];
  struct connection *p;
  struct tm *tm;

  appendstring(&cp->obuf, "Channel  Time   User\n");
  for (p = connections; p; p = p->next) {
    tm = localtime(&p->time);
    sprintf(buffer, "%7d  %2d:%02d  %s\n", p->channel, tm->tm_hour, tm->tm_min, p->name);
    appendstring(&cp->obuf, buffer);
  }
  appendstring(&cp->obuf, "***\n");
}

/*---------------------------------------------------------------------------*/

static void process_input(cp)
struct connection *cp;
{

  static struct cmdtable {
    char  *name;
    void (*fnc)();
  } cmdtable[] = {

    "?",       help_command,
    "bye",     bye_command,
    "channel", channel_command,
    "exit",    bye_command,
    "help",    help_command,
    "invite",  invite_command,
    "msg",     msg_command,
    "name",    name_command,
    "quit",    bye_command,
    "who",     who_command,
    "write",   msg_command,

    0, 0
  };

  char  *arg;
  char  buffer[2048];
  int  arglen;
  struct cmdtable *cmdp;

  if (*cp->ibuf == '/') {
    arglen = strlen(arg = getarg(cp->ibuf + 1, 0));
    for (cmdp = cmdtable; cmdp->name; cmdp++)
      if (!strncmp(cmdp->name, arg, arglen)) {
	(*cmdp->fnc)(cp);
	return;
      }
    sprintf(buffer, "*** Unknown command '/%s'. Type /HELP for help.\n", arg);
    appendstring(&cp->obuf, buffer);
    return;
  }
  sprintf(buffer, "<%s>:", cp->name);
  sendchannel(cp, formatline(buffer, cp->ibuf));
}

/*---------------------------------------------------------------------------*/

main(argc, argv)
int  argc;
char  **argv;
{

  char  buffer[2048];
  char  c;
  int  flisten;
  int  flistenmask;
  int  i;
  int  nfd;
  int  rmask, wmask;
  int  size;
  struct connection *cp;
  struct sockaddr_in addr_in;

  for (i = 0; i < _NFILE; i++) close(i);
  chdir("/");
  setpgrp();
  putenv("TZ=MEZ-1MESZ");

  if ((flisten = socket(AF_INET, SOCK_STREAM, 0)) < 0) exit(1);
  flistenmask = (1 << flisten);
  memset((char *) & addr_in, 0, sizeof(addr_in ));
  addr_in.sin_family = AF_INET;
  addr_in.sin_addr.s_addr = INADDR_ANY;
  addr_in.sin_port = (argc >= 2) ? atoi(argv[1]) : PORT;
  setsockopt(flisten, SOL_SOCKET, SO_REUSEADDR, (char *) 0, 0);
  if (bind(flisten, &addr_in, sizeof(addr_in ))) exit(1);
  if (listen(flisten, 20)) exit(1);

  for (; ; ) {

    free_closed_connections();

    nfd = flisten + 1;
    rmask = flistenmask;
    wmask = 0;
    for (cp = connections; cp; cp = cp->next) {
      if (nfd <= cp->fd) nfd = cp->fd + 1;
      rmask |= cp->fmask;
      if (cp->obuf) wmask |= cp->fmask;
    }
    select(nfd, &rmask, &wmask, (int *) 0, (struct timeval *) 0);

    if (rmask & flistenmask) doconnect(flisten);

    for (cp = connections; cp; cp = cp->next) {

      if (rmask & cp->fmask)
	if ((size = read(cp->fd, buffer, sizeof(buffer))) <= 0)
	  bye_command(cp);
	else
	  for (i = 0; i < size; i++)
	    switch (buffer[i]) {
	    case '\b':
	      if (cp->icnt) cp->icnt--;
	      break;
	    case '\n':
	    case '\r':
	      if (cp->icnt) {
		cp->ibuf[cp->icnt] = '\0';
		process_input(cp);
		cp->icnt = 0;
	      }
	      break;
	    default:
	      if (cp->icnt < sizeof(cp->ibuf) - 5)
		cp->ibuf[cp->icnt++] = buffer[i];
	      break;
	    }

      if (wmask & cp->fmask) {
	c = pullchar(&cp->obuf);
	write(cp->fd, &c, 1);
      }

    }
  }
}

