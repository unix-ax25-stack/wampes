static char  rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/conversd.c,v 2.3 1988/10/02 16:33:06 dk5sg Exp $";

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <utmp.h>

extern char  *calloc();
extern char  *malloc();
extern long  time();
extern struct utmp *getutent();
extern void endutent();
extern void exit();
extern void free();

#define MAXCHANNEL 99999
#define PORT       3600

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
  long  time;                   /* Connect time */
  int  locked;                  /* Set if mesg already sent */
  int  fd;                      /* Socket descriptor */
  int  fmask;                   /* Socket mask */
  char  ibuf[2048];             /* Input buffer */
  int  icnt;                    /* Number of bytes in input buffer */
  struct mbuf *obuf;            /* Output queue */
  int  received;                /* Number of bytes received */
  int  xmitted;                 /* Number of bytes transmitted */
  struct connection *next;      /* Linked list pointer */
};

struct permlink {
  char  name[80];               /* Name of host */
  int  connected;               /* Set if currently connected */
  long  statetime;              /* Time of last (dis)connect */
  int  tries;                   /* Number of connect tries */
  long  waittime;               /* Time between connect tries */
  long  retrytime;              /* Time of next connect try */
  struct permlink *next;        /* Linked list pointer */
};

#define CM_UNKNOWN   (1 << CT_UNKNOWN)
#define CM_USER      (1 << CT_USER)
#define CM_HOST      (1 << CT_HOST)
#define CM_CLOSED    (1 << CT_CLOSED)

static char  *myhostname;
static long  currtime;
static struct connection *connections;
static struct permlink *permlinks;
static struct utsname utsname;

/*---------------------------------------------------------------------------*/

static int  sigpipe_handler(sig, code, scp)
int  sig;
int  code;
struct sigcontext *scp;
{
  scp->sc_syscall_action = SIG_RETURN;
}

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

static int  queuelength(bp)
register struct mbuf *bp;
{
  register int  len;

  for (len = 0; bp; bp = bp->next)
    len += strlen(bp->data);
  return len;
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

  for (p = 0, cp = connections; cp; )
    if (cp->type == CT_CLOSED ||
	cp->type == CT_UNKNOWN && cp->time + 300 < currtime) {
      if (p) {
	p->next = cp->next;
	if (cp->fmask) close(cp->fd);
	freequeue(&cp->obuf);
	free((char *) cp);
	cp = p->next;
      } else {
	connections = cp->next;
	if (cp->fmask) close(cp->fd);
	freequeue(&cp->obuf);
	free((char *) cp);
	cp = connections;
      }
    } else {
      p = cp;
      cp = cp->next;
    }
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

  for (f = prefix, t = buf; *f; *t++ = *f++) ;
  l = t - buf;
  f = text;

  for (; ; ) {
    while (isspace(*f & 0xff)) f++;
    if (!*f) {
      *t++ = '\n';
      *t = '\0';
      return buf;
    }
    for (x = f; *x && !isspace(*x & 0xff); x++) ;
    lw = x - f;
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

static char  *timestring(gmt)
long  gmt;
{

  static char  buffer[80];
  static char  monthnames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  struct tm *tm;

  tm = localtime(&gmt);
  if (gmt + 24 * 60 * 60 > currtime)
    sprintf(buffer, " %2d:%02d", tm->tm_hour, tm->tm_min);
  else
    sprintf(buffer, "%-3.3s %2d", monthnames + 3 * tm->tm_mon, tm->tm_mday);
  return buffer;
}

/*---------------------------------------------------------------------------*/

static void doconnect(flisten)
int  flisten;
{

  int  addrlen;
  int  fd;
  int  flags;
  struct connection *cp;
  struct sockaddr_in addr;

  addrlen = sizeof(addr);
  if ((fd = accept(flisten, &addr, &addrlen)) < 0) return;
  if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
    close(fd);
    return;
  }
  if (fcntl(fd, F_SETFL, flags | O_NDELAY) == -1) {
    close(fd);
    return;
  }
  cp = (struct connection *) calloc(1, sizeof(struct connection ));
  cp->fd = fd;
  cp->fmask = (1 << fd);
  cp->time = currtime;
  cp->next = connections;
  connections = cp;
}

/*---------------------------------------------------------------------------*/

static void send_user_change_msg(name, host, time, oldchannel, newchannel)
char  *name, *host;
long  time;
int  oldchannel, newchannel;
{

  char  buffer[2048];
  register struct connection *p;

  for (p = connections; p; p = p->next) {
    if (p->type == CT_USER && !p->via && !p->locked) {
      if (p->channel == oldchannel) {
	if (newchannel >= 0)
	  sprintf(buffer, "*** %s switched to channel %d.\n", name, newchannel);
	else
	  sprintf(buffer, "*** %s signed off.\n", name);
	appendstring(&p->obuf, buffer);
	p->locked = 1;
      }
      if (p->channel == newchannel) {
	sprintf(buffer, "*** %s signed on.\n", name);
	appendstring(&p->obuf, buffer);
	p->locked = 1;
      }
    }
    if (p->type == CT_HOST && !p->locked) {
      sprintf(buffer, "/\377\200USER %s %s %d %d %d\n", name, host, time, oldchannel, newchannel);
      appendstring(&p->obuf, buffer);
      p->locked = 1;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void send_msg_to_user(fromname, toname, text)
char  *fromname, *toname, *text;
{

  char  buffer[2048];
  register struct connection *p;

  for (p = connections; p; p = p->next)
    if (p->type == CT_USER && !strcmp(p->name, toname))
      if (p->via) {
	if (!p->via->locked) {
	  sprintf(buffer, "/\377\200UMSG %s %s %s\n", fromname, toname, text);
	  appendstring(&p->via->obuf, buffer);
	  p->via->locked = 1;
	}
      } else {
	if (!p->locked) {
	  sprintf(buffer, "<*%s*>:", fromname);
	  appendstring(&p->obuf, formatline(buffer, text));
	  p->locked = 1;
	}
      }
}

/*---------------------------------------------------------------------------*/

static void send_msg_to_channel(fromname, channel, text)
char  *fromname;
int  channel;
char  *text;
{

  char  buffer[2048];
  register struct connection *p;

  for (p = connections; p; p = p->next)
    if (p->type == CT_USER && p->channel == channel)
      if (p->via) {
	if (!p->via->locked) {
	  sprintf(buffer, "/\377\200CMSG %s %d %s\n", fromname, channel, text);
	  appendstring(&p->via->obuf, buffer);
	  p->via->locked = 1;
	}
      } else {
	if (!p->locked) {
	  sprintf(buffer, "<%s>:", fromname);
	  appendstring(&p->obuf, formatline(buffer, text));
	  p->locked = 1;
	}
      }
}

/*---------------------------------------------------------------------------*/

static void send_invite_msg(fromname, toname, channel)
char  *fromname, *toname;
int  channel;
{

  static char  invitetext[] = "\n\007\007*** Message from %s at %s ...\nPlease join convers channel %d.\n\007\007\n";

  char  buffer[2048];
  int  fd;
  struct connection *p;
  struct stat stbuf;
  struct utmp *up;

  for (p = connections; p; p = p->next)
    if (p->type == CT_USER && !strcmp(p->name, toname)) {
      if (p->channel == channel) return;
      if (!p->via && !p->locked) {
	sprintf(buffer, invitetext, fromname, timestring(currtime), channel);
	appendstring(&p->obuf, buffer);
	return;
      }
      if (p->via && !p->via->locked) {
	sprintf(buffer, "/\377\200INVI %s %s %d\n", fromname, toname, channel);
	appendstring(&p->via->obuf, buffer);
	return;
      }
    }

  while (up = getutent())
    if (up->ut_type == USER_PROCESS && !strcmp(up->ut_user, toname)) {
      sprintf(buffer, "/dev/%s", up->ut_line);
      if (stat(buffer, &stbuf)) continue;
      if ((stbuf.st_mode & 2) != 2) continue;
      if ((fd = open(buffer, O_WRONLY, 0644)) < 0) continue;
      sprintf(buffer, invitetext, fromname, timestring(currtime), channel);
      write(fd, buffer, (unsigned) strlen(buffer));
      close(fd);
      endutent();
      return;
    }
  endutent();

  for (p = connections; p; p = p->next)
    if (p->type == CT_HOST && !p->locked) {
      sprintf(buffer, "/\377\200INVI %s %s %d\n", fromname, toname, channel);
      appendstring(&p->obuf, buffer);
    }

}

/*---------------------------------------------------------------------------*/

static void update_permlinks(name, connected)
char  *name;
int  connected;
{
  register struct permlink *p;

  for (p = permlinks; p; p = p->next)
    if (!strcmp(p->name, name)) {
      p->connected = connected;
      p->statetime = currtime;
      p->tries = 0;
      p->waittime = 60;
      p->retrytime = currtime + p->waittime;
    }
}

/*---------------------------------------------------------------------------*/

static void connect_permlinks()
{

#define MAX_WAITTIME   (60*60*3)

  char  buffer[2048];
  int  fd;
  int  flags;
  register struct connection *cp;
  register struct permlink *p;
  struct sockaddr_in addr;

  for (p = permlinks; p; p = p->next)
    if (!p->connected && p->retrytime <= currtime) {
      p->tries++;
      p->waittime *= 2;
      if (p->waittime > MAX_WAITTIME) p->waittime = MAX_WAITTIME;
      p->retrytime = currtime + p->waittime;
      if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) continue;
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = 0x7f000001;
      addr.sin_port = 3700;
      if (connect(fd, &addr, sizeof(addr))) {
	close(fd);
	continue;
      }
      if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
	close(fd);
	continue;
      }
      if (fcntl(fd, F_SETFL, flags | O_NDELAY) == -1) {
	close(fd);
	continue;
      }
      cp = (struct connection *) calloc(1, sizeof(struct connection ));
      cp->time = currtime;
      cp->fd = fd;
      cp->fmask = (1 << fd);
      cp->next = connections;
      connections = cp;
      sprintf(buffer, "connect tcp %s convers\n", p->name);
      appendstring(&cp->obuf, buffer);
      sprintf(buffer, "/\377\200HOST %s\n", myhostname);
      appendstring(&cp->obuf, buffer);
    }
}

/*---------------------------------------------------------------------------*/

static void bye_command(cp)
struct connection *cp;
{
  register struct connection *p, *q;

  switch (cp->type) {
  case CT_UNKNOWN:
    cp->type = CT_CLOSED;
    break;
  case CT_USER:
    cp->type = CT_CLOSED;
    for (q = connections; q; q = q->next) q->locked = 0;
    send_user_change_msg(cp->name, cp->host, cp->time, cp->channel, -1);
    break;
  case CT_HOST:
    cp->type = CT_CLOSED;
    update_permlinks(cp->name, 0);
    for (p = connections; p; p = p->next)
      if (p->via == cp) {
	p->type = CT_CLOSED;
	for (q = connections; q; q = q->next) q->locked = 0;
	send_user_change_msg(p->name, p->host, p->time, p->channel, -1);
      }
    break;
  case CT_CLOSED:
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void channel_command(cp)
struct connection *cp;
{

  char  buffer[2048];
  int  newchannel;

  newchannel = atoi(getarg(0, 0));
  if (newchannel < 0 || newchannel > MAXCHANNEL) {
    sprintf(buffer, "*** Channel numbers must be in the range 0..%d.\n", MAXCHANNEL);
    appendstring(&cp->obuf, buffer);
    return;
  }
  if (newchannel == cp->channel) {
    sprintf(buffer, "*** Already on channel %d.\n", cp->channel);
    appendstring(&cp->obuf, buffer);
    return;
  }
  send_user_change_msg(cp->name, cp->host, cp->time, cp->channel, newchannel);
  cp->channel = newchannel;
  sprintf(buffer, "*** Now on channel %d.\n", cp->channel);
  appendstring(&cp->obuf, buffer);
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
  appendstring(&cp->obuf, "/LINKS [LONG]        List all connections to other hosts\n");
  appendstring(&cp->obuf, "/MSG user text...    Send a private message to user\n");
  appendstring(&cp->obuf, "/QUIT                Terminate the convers session\n");
  appendstring(&cp->obuf, "/WHO [QUICK] [LONG]  List all users and their channel numbers\n");
  appendstring(&cp->obuf, "/WRITE user text...  Send a private message to user\n");

  appendstring(&cp->obuf, "***\n");
}

/*---------------------------------------------------------------------------*/

static void invite_command(cp)
struct connection *cp;
{
  char  *toname;

  toname = getarg(0, 0);
  if (*toname) send_invite_msg(cp->name, toname, cp->channel);
}

/*---------------------------------------------------------------------------*/

static void links_command(cp)
struct connection *cp;
{

  char  buffer[2048];
  char  tmp[80];
  int  full;
  struct connection *pc;
  struct permlink *pp;

  full = *(getarg(0, 0));
  appendstring(&cp->obuf, full ?
    "Host     State         Since NextTry Tries Queue Receivd Xmitted\n" :
    "Host     State         Since\n");
  for (pc = connections; pc; pc = pc->next)
    if (pc->type == CT_HOST) {
      sprintf(buffer,
	      full ?
		"%-8.8s %-12s %s               %5d %7d %7d\n" :
		"%-8.8s %-12s %s\n",
	      pc->name,
	      "Connected",
	      timestring(pc->time),
	      queuelength(pc->obuf),
	      pc->received,
	      pc->xmitted);
      appendstring(&cp->obuf, buffer);
    }
  for (pp = permlinks; pp; pp = pp->next)
    if (!pp->connected) {
      strcpy(tmp, timestring(pp->retrytime)),
      sprintf(buffer,
	      full ?
		"%-8.8s %12s %s  %s %5d\n" :
		"%-8.8s %12s %s\n",
	      pp->name,
	      "Disconnected",
	      timestring(pp->statetime),
	      tmp,
	      pp->tries);
      appendstring(&cp->obuf, buffer);
    }
  appendstring(&cp->obuf, "***\n");
}

/*---------------------------------------------------------------------------*/

static void msg_command(cp)
struct connection *cp;
{
  char  *toname, *text;

  toname = getarg(0, 0);
  text = getarg(0, 1);
  if (*text) send_msg_to_user(cp->name, toname, text);
}

/*---------------------------------------------------------------------------*/

static void name_command(cp)
struct connection *cp;
{

  char  buffer[2048];
  int  newchannel;

  strcpy(cp->name, getarg(0, 0));
  if (!*cp->name) return;
  cp->type = CT_USER;
  strcpy(cp->host, myhostname);
  sprintf(buffer, "conversd @ %s $Revision: 2.3 $  Type /HELP for help.\n", myhostname);
  appendstring(&cp->obuf, buffer);
  newchannel = atoi(getarg(0, 0));
  if (newchannel < 0 || newchannel > MAXCHANNEL) {
    sprintf(buffer, "*** Channel numbers must be in the range 0..%d.\n", MAXCHANNEL);
    appendstring(&cp->obuf, buffer);
  } else
    cp->channel = newchannel;
  send_user_change_msg(cp->name, cp->host, cp->time, -1, cp->channel);
}

/*---------------------------------------------------------------------------*/

static void who_command(cp)
struct connection *cp;
{

  char  buffer[2048];
  int  channel;
  int  full = 0;
  int  quick = 0;
  struct connection *p;

  switch (*(getarg(0, 0))) {
  case 'l':
    full = 1;
    break;
  case 'q':
    quick = 1;
    break;
  }

  if (quick) {
    appendstring(&cp->obuf, "Channel Users\n");
    for (p = connections; p; p = p->next) p->locked = 0;
    do {
      channel = -1;
      for (p = connections; p; p = p->next)
	if (p->type == CT_USER && !p->locked && (channel < 0 || channel == p->channel)) {
	  if (channel < 0) {
	    channel = p->channel;
	    sprintf(buffer, "%7d", channel);
	  }
	  strcat(buffer, " ");
	  strcat(buffer, p->name);
	  p->locked = 1;
	}
      if (channel >= 0) {
	strcat(buffer, "\n");
	appendstring(&cp->obuf, buffer);
      }
    } while (channel >= 0);
    appendstring(&cp->obuf, "***\n");
    return;
  }

  appendstring(&cp->obuf, full ?
      "User     Host     Via      Channel   Time Queue Receivd Xmitted\n" :
      "User     Host     Via      Channel   Time\n");
  for (p = connections; p; p = p->next)
    if (p->type == CT_USER) {
      sprintf(buffer,
	      full ?
		"%-8.8s %-8.8s %-8.8s %7d %s %5d %7d %7d\n" :
		"%-8.8s %-8.8s %-8.8s %7d %s\n",
	      p->name,
	      p->host,
	      p->via ? p->via->name : "",
	      p->channel,
	      timestring(p->time),
	      queuelength(p->obuf),
	      p->received,
	      p->xmitted);
      appendstring(&cp->obuf, buffer);
    }
  appendstring(&cp->obuf, "***\n");
}

/*---------------------------------------------------------------------------*/

static void h_cmsg_command(cp)
struct connection *cp;
{

  char  *name;
  char  *text;
  int  channel;

  name = getarg(0, 0);
  channel = atoi(getarg(0, 0));
  text = getarg(0, 1);
  if (*text) send_msg_to_channel(name, channel, text);
}

/*---------------------------------------------------------------------------*/

static void h_host_command(cp)
struct connection *cp;
{

  char  buffer[2048];
  register struct connection *p;

  strcpy(cp->name, getarg(0, 0));
  if (!*cp->name) return;
  cp->type = CT_HOST;
  update_permlinks(cp->name, 1);
  sprintf(buffer, "/\377\200HOST %s\n", myhostname);
  appendstring(&cp->obuf, buffer);
  for (p = connections; p; p = p->next)
    if (p->type == CT_USER) {
      sprintf(buffer, "/\377\200USER %s %s %d %d %d\n", p->name, p->host, p->time, -1, p->channel);
      appendstring(&cp->obuf, buffer);
    }
}

/*---------------------------------------------------------------------------*/

static void h_invi_command(cp)
struct connection *cp;
{

  char  *fromname, *toname;
  int  channel;

  fromname = getarg(0, 0);
  toname = getarg(0, 0);
  channel = atoi(getarg(0, 0));
  send_invite_msg(fromname, toname, channel);
}

/*---------------------------------------------------------------------------*/

static void h_umsg_command(cp)
struct connection *cp;
{
  char  *fromname, *toname, *text;

  fromname = getarg(0, 0);
  toname = getarg(0, 0);
  text = getarg(0, 1);
  if (*text) send_msg_to_user(fromname, toname, text);
}

/*---------------------------------------------------------------------------*/

static void h_user_command(cp)
struct connection *cp;
{

  char  *host;
  char  *name;
  char  buffer[2048];
  int  newchannel;
  int  oldchannel;
  long  time;
  register struct connection *p;

  name = getarg(0, 0);
  host = getarg(0, 0);
  time = atoi(getarg(0, 0));
  oldchannel = atoi(getarg(0, 0));
  newchannel = atoi(getarg(0, 0));

  for (p = connections; p; p = p->next)
    if (p->type == CT_USER       &&
	p->channel == oldchannel &&
	p->time == time          &&
	p->via == cp             &&
	!strcmp(p->name, name)   &&
	!strcmp(p->host, host))  break;
  if (!p) {
    p = (struct connection *) calloc(1, sizeof(struct connection ));
    p->type = CT_USER;
    strcpy(p->name, name);
    strcpy(p->host, host);
    p->via = cp;
    p->channel = oldchannel;
    p->time = time;
    p->next = connections;
    connections = p;
  }
  if ((p->channel = newchannel) < 0) p->type = CT_CLOSED;
  send_user_change_msg(name, host, time, oldchannel, newchannel);
}

/*---------------------------------------------------------------------------*/

static void process_input(cp)
struct connection *cp;
{

  static struct cmdtable {
    char  *name;
    void (*fnc)();
    int  states;
  } cmdtable[] = {

    "?",          help_command,       CM_USER,
    "bye",        bye_command,        CM_USER,
    "channel",    channel_command,    CM_USER,
    "exit",       bye_command,        CM_USER,
    "help",       help_command,       CM_USER,
    "invite",     invite_command,     CM_USER,
    "links",      links_command,      CM_USER,
    "msg",        msg_command,        CM_USER,
    "name",       name_command,       CM_UNKNOWN,
    "quit",       bye_command,        CM_USER,
    "who",        who_command,        CM_USER,
    "write",      msg_command,        CM_USER,

    "\377\200cmsg", h_cmsg_command,   CM_HOST,
    "\377\200host", h_host_command,   CM_UNKNOWN,
    "\377\200invi", h_invi_command,   CM_HOST,
    "\377\200umsg", h_umsg_command,   CM_HOST,
    "\377\200user", h_user_command,   CM_HOST,

    0, 0, 0
  };

  char  *arg;
  char  buffer[2048];
  int  arglen;
  register struct connection *p;
  struct cmdtable *cmdp;

  for (p = connections; p; p = p->next) p->locked = 0;
  cp->locked = 1;

  if (*cp->ibuf == '/') {
    arglen = strlen(arg = getarg(cp->ibuf + 1, 0));
    for (cmdp = cmdtable; cmdp->name; cmdp++)
      if (!strncmp(cmdp->name, arg, arglen)) {
	if (cmdp->states & (1 << cp->type)) (*cmdp->fnc)(cp);
	return;
      }
    if (cp->type == CT_USER) {
      sprintf(buffer, "*** Unknown command '/%s'. Type /HELP for help.\n", arg);
      appendstring(&cp->obuf, buffer);
    }
    return;
  }

  if (cp->type == CT_USER)
    send_msg_to_channel(cp->name, cp->channel, cp->ibuf);
}

/*---------------------------------------------------------------------------*/

static void read_configuration()
{

  static char  conffile[] = "/tcp/convers.conf";

  FILE * fp;
  char  line[1024];
  char  name[1024];
  struct permlink *p;

  if (!(fp = fopen(conffile, "r"))) return;
  while (fgets(line, sizeof(line), fp))
    if (sscanf(line, "%s", name) == 1) {
      p = (struct permlink *) calloc(1, sizeof(struct permlink ));
      strcpy(p->name, name);
      p->next = permlinks;
      permlinks = p;
      update_permlinks(name, 0);
    }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

main(argc, argv)
int  argc;
char  **argv;
{

  static struct timeval select_timeout = { 60, 0 };

  char  buffer[2048];
  char  c;
  int  flisten;
  int  flistenmask;
  int  i;
  int  nfd;
  int  rmask, wmask;
  int  size;
  struct connection *cp;
  struct mbuf *bp;
  struct sigvec vec;
  struct sockaddr_in addr;

  for (i = 0; i < _NFILE; i++) close(i);
  chdir("/");
  setpgrp();
  vec.sv_mask = vec.sv_flags = 0;
  vec.sv_handler = sigpipe_handler;
  sigvector(SIGPIPE, &vec, (struct sigvec *) 0);
  putenv("TZ=MEZ-1MESZ");
  uname(&utsname);
  myhostname = utsname.nodename;
  if (!strcmp(myhostname, "hpbeo11")) myhostname = "dk0hu";
  time(&currtime);

  if (argc < 2) read_configuration();

  if ((flisten = socket(AF_INET, SOCK_STREAM, 0)) < 0) exit(1);
  flistenmask = (1 << flisten);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = (argc >= 2) ? atoi(argv[1]) : PORT;
  setsockopt(flisten, SOL_SOCKET, SO_REUSEADDR, (char *) 0, 0);
  if (bind(flisten, &addr, sizeof(addr ))) exit(1);
  if (listen(flisten, 20)) exit(1);

  for (; ; ) {

    free_closed_connections();

    connect_permlinks();

    nfd = flisten + 1;
    rmask = flistenmask;
    wmask = 0;
    for (cp = connections; cp; cp = cp->next) {
      if (nfd <= cp->fd) nfd = cp->fd + 1;
      rmask |= cp->fmask;
      if (cp->obuf) wmask |= cp->fmask;
    }
    if (select(nfd, &rmask, &wmask, (int *) 0, &select_timeout) < 1)
      rmask = wmask = 0;

    time(&currtime);

    if (rmask & flistenmask) doconnect(flisten);

    for (cp = connections; cp; cp = cp->next) {

      if (rmask & cp->fmask)
	if ((size = read(cp->fd, buffer, sizeof(buffer))) < 0)
	  bye_command(cp);
	else {
	  cp->received += size;
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
	}

      if (wmask & cp->fmask) {
	size = write(cp->fd, cp->obuf->data, (unsigned) strlen(cp->obuf->data));
	if (size < 0) {
	  if (errno != EINTR) bye_command(cp);
	} else {
	  cp->xmitted += size;
	  cp->obuf->data += size;
	  while ((bp = cp->obuf) && !*cp->obuf->data) {
	    cp->obuf = cp->obuf->next;
	    free(bp);
	  }
	}
      }

    }
  }
}

