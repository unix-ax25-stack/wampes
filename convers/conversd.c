#ifndef __lint
static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/conversd.c,v 2.30 1993/05/10 11:22:50 deyke Exp $";
#endif

#define _HPUX_SOURCE

#include <sys/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utmp.h>

#ifndef SOMAXCONN
#define SOMAXCONN       5
#endif

#ifndef WNOHANG
#define WNOHANG         1
#endif

#ifdef __STDC__
#define __ARGS(x)       x
#else
#define __ARGS(x)       ()
#define const
#endif

#ifdef __hpux
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

#include "buildsaddr.h"

#define PROGFILE        "/tcp/conversd"
#define CONFFILE        "/tcp/convers.conf"
#define MAXCHANNEL      32767

struct mbuf {
  struct mbuf *next;
  char *data;
};

struct connection {
  int type;                     /* Connection type */
#define CT_UNKNOWN      0
#define CT_USER         1
#define CT_HOST         2
#define CT_CLOSED       3
  char name[80];                /* Name of user or host */
  char host[80];                /* Name of host where user is logged on */
  struct connection *via;       /* Pointer to neighbor host */
  int channel;                  /* Channel number */
  long time;                    /* Connect time */
  int locked;                   /* Set if mesg already sent */
  int fd;                       /* Socket descriptor */
  char ibuf[2048];              /* Input buffer */
  int icnt;                     /* Number of bytes in input buffer */
  struct mbuf *obuf;            /* Output queue */
  int received;                 /* Number of bytes received */
  int xmitted;                  /* Number of bytes transmitted */
  struct connection *next;      /* Linked list pointer */
};

#define CM_UNKNOWN   (1 << CT_UNKNOWN)
#define CM_USER      (1 << CT_USER)
#define CM_HOST      (1 << CT_HOST)
#define CM_CLOSED    (1 << CT_CLOSED)

#define NULLCONNECTION  ((struct connection *) 0)

struct permlink {
  char name[80];                /* Name of host */
  char socket[80];              /* Name of socket to connect to */
  char command[256];            /* Optional connect command */
  struct connection *connection;/* Pointer to associated connection */
  long statetime;               /* Time of last (dis)connect */
  int tries;                    /* Number of connect tries */
  long waittime;                /* Time between connect tries */
  long retrytime;               /* Time of next connect try */
  struct permlink *next;        /* Linked list pointer */
};

#define NULLPERMLINK  ((struct permlink *) 0)

static char *myhostname;
static int maxfd = -1;
static long currtime;
static struct connection *connections;
static struct fd_set chkread;
static struct fd_set chkwrite;

static struct permlink *permlinks;

static void appendstring __ARGS((struct connection *cp, const char *string));
static int queuelength __ARGS((const struct mbuf *bp));
static void free_connection __ARGS((struct connection *cp));
static void free_closed_connections __ARGS((void));
static char *getarg __ARGS((char *line, int all));
static char *formatline __ARGS((char *prefix, char *text));
static char *timestring __ARGS((long gmt));
static struct connection *alloc_connection __ARGS((int fd));
static void accept_connect_request __ARGS((int flisten));
static void clear_locks __ARGS((void));
static void send_user_change_msg __ARGS((char *name, char *host, int oldchannel, int newchannel));
static void send_msg_to_user __ARGS((char *fromname, char *toname, char *text));
static void send_msg_to_channel __ARGS((char *fromname, int channel, char *text));
static void send_invite_msg __ARGS((char *fromname, char *toname, int channel));
static void update_permlinks __ARGS((char *name, struct connection *cp));
static void connect_permlinks __ARGS((void));
static void bye_command __ARGS((struct connection *cp));
static void channel_command __ARGS((struct connection *cp));
static void help_command __ARGS((struct connection *cp));
static void invite_command __ARGS((struct connection *cp));
static void links_command __ARGS((struct connection *cp));
static void msg_command __ARGS((struct connection *cp));
static void name_command __ARGS((struct connection *cp));
static void who_command __ARGS((struct connection *cp));
static void h_cmsg_command __ARGS((struct connection *cp));
static void h_host_command __ARGS((struct connection *cp));
static void h_invi_command __ARGS((struct connection *cp));
static void h_umsg_command __ARGS((struct connection *cp));
static void h_user_command __ARGS((struct connection *cp));
static void process_input __ARGS((struct connection *cp));
static void read_configuration __ARGS((void));
static void check_files_changed __ARGS((void));

/*---------------------------------------------------------------------------*/

#define uchar(x) ((x) & 0xff)

/*---------------------------------------------------------------------------*/

#ifdef ULTRIX_RISC

static char *strdup(s)
const char *s;
{
  char *p;

  if (p = malloc(strlen(s) + 1)) strcpy(p, s);
  return p;
}

#endif

/*---------------------------------------------------------------------------*/

static void appendstring(cp, string)
struct connection *cp;
const char *string;
{
  struct mbuf *bp, *p;

  if (!*string) return;

  bp = (struct mbuf *) malloc(sizeof(*bp) + strlen(string) + 1);
  bp->next = 0;
  bp->data = strcpy((char *) (bp + 1), string);

  if (cp->obuf) {
    for (p = cp->obuf; p->next; p = p->next) ;
    p->next = bp;
  } else {
    cp->obuf = bp;
    FD_SET(cp->fd, &chkwrite);
  }
}

/*---------------------------------------------------------------------------*/

static int queuelength(bp)
const struct mbuf *bp;
{
  int len;

  for (len = 0; bp; bp = bp->next)
    len += strlen(bp->data);
  return len;
}

/*---------------------------------------------------------------------------*/

static void free_connection(cp)
struct connection *cp;
{

  struct mbuf *bp;
  struct permlink *p;

  for (p = permlinks; p; p = p->next)
    if (p->connection == cp) p->connection = NULLCONNECTION;
  if (cp->fd >= 0) {
    close(cp->fd);
    FD_CLR(cp->fd, &chkread);
    FD_CLR(cp->fd, &chkwrite);
    if (cp->fd == maxfd)
      while (--maxfd >= 0)
	if (FD_ISSET(maxfd, &chkread)) break;
  }
  while (bp = cp->obuf) {
    cp->obuf = bp->next;
    free(bp);
  }
  free(cp);
}

/*---------------------------------------------------------------------------*/

static void free_closed_connections()
{
  struct connection *cp, *p;

  for (p = NULLCONNECTION, cp = connections; cp; )
    if (cp->type == CT_CLOSED ||
	cp->type == CT_UNKNOWN && cp->time + 300 < currtime) {
      if (p) {
	p->next = cp->next;
	free_connection(cp);
	cp = p->next;
      } else {
	connections = cp->next;
	free_connection(cp);
	cp = connections;
      }
    } else {
      p = cp;
      cp = cp->next;
    }
}

/*---------------------------------------------------------------------------*/

static char *getarg(line, all)
char *line;
int all;
{

  char *arg;
  static char *p;

  if (line) p = line;
  while (isspace(uchar(*p))) p++;
  if (all) return p;
  arg = p;
  while (*p && !isspace(uchar(*p))) {
    if (*p >= 'A' && *p <= 'Z') *p = tolower(*p);
    p++;
  }
  if (*p) *p++ = '\0';
  return arg;
}

/*---------------------------------------------------------------------------*/

/* Returns a formatted version of the given text.
 * The prefix appears at the beginning of the text, and each
 * line after the first will be indented to column PREFIXLEN.
 * All whitespace (SPACE and TAB) will be replaced by a single
 * SPACE character,
 * Lines will be filled to be CONVLINELEN characters long, wrapping
 * words as necessary.
 */

static char *formatline(prefix, text)
char *prefix, *text;
{

#define PREFIXLEN       10
#define CONVLINELEN     79

  char *e, *f, *t, *x;
  int l, lw;
  static char buf[2048];

  e = buf + (sizeof(buf) - 2);

  /* Copy prefix into buf, set l to length of prefix */
  for (f = prefix, t = buf; *f; f++)
    if (t < e) *t++ = *f;
  l = t - buf;

  for (f = text; ; ) {

    /* Skip leading spaces */
    while (isspace(uchar(*f))) f++;

    /* Return if nothing more or no room left */
    if (!*f || t >= e) {
      *t++ = '\n';
      *t = 0;
      return buf;
    }

    /* Find length of next word (seq. of non-blanks) */
    for (x = f; *x && !isspace(uchar(*x)); x++) ;
    lw = x - f;

    /* If the word would extend past end of line, do newline */
    if (l > PREFIXLEN && l + 1 + lw > CONVLINELEN) {
      if (t < e) *t++ = '\n';
      l = 0;
    }

    /* Put out a single space, or indent to column PREFIXLEN */
    do {
      if (t < e) *t++ = ' ';
      l++;
    } while (l < PREFIXLEN);

    /* Put out the word */
    while (lw--) {
      if (t < e) *t++ = *f++;
      l++;
    }
  }
}

/*---------------------------------------------------------------------------*/

static char *timestring(gmt)
long gmt;
{

  static char buffer[80];
  static char monthnames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  struct tm *tm;

  tm = localtime(&gmt);
  if (gmt + 24 * 60 * 60 > currtime)
    sprintf(buffer, " %2d:%02d", tm->tm_hour, tm->tm_min);
  else
    sprintf(buffer, "%-3.3s %2d", monthnames + 3 * tm->tm_mon, tm->tm_mday);
  return buffer;
}

/*---------------------------------------------------------------------------*/

static struct connection *alloc_connection(fd)
int fd;
{

  int flags;
  struct connection *cp;

  if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
    close(fd);
    return 0;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    close(fd);
    return 0;
  }
  cp = (struct connection *) calloc(1, sizeof(*cp));
  cp->fd = fd;
  cp->time = currtime;
  cp->next = connections;
  connections = cp;
  if (maxfd < fd) maxfd = fd;
  FD_SET(fd, &chkread);
  return cp;
}

/*---------------------------------------------------------------------------*/

static void accept_connect_request(flisten)
int flisten;
{

  int addrlen;
  int fd;
  struct sockaddr addr;

  addrlen = sizeof(addr);
  if ((fd = accept(flisten, &addr, &addrlen)) >= 0) alloc_connection(fd);
}

/*---------------------------------------------------------------------------*/

static void clear_locks()
{
  struct connection *p;

  for (p = connections; p; p = p->next) p->locked = 0;
}

/*---------------------------------------------------------------------------*/

static void send_user_change_msg(name, host, oldchannel, newchannel)
char *name, *host;
int oldchannel, newchannel;
{

  char buffer[2048];
  struct connection *p;

  for (p = connections; p; p = p->next) {
    if (p->type == CT_USER && !p->via && !p->locked) {
      if (p->channel == oldchannel) {
	if (newchannel >= 0)
	  sprintf(buffer, "*** %s switched to channel %d.\n", name, newchannel);
	else
	  sprintf(buffer, "*** %s signed off.\n", name);
	appendstring(p, buffer);
	p->locked = 1;
      }
      if (p->channel == newchannel) {
	sprintf(buffer, "*** %s signed on.\n", name);
	appendstring(p, buffer);
	p->locked = 1;
      }
    }
    if (p->type == CT_HOST && !p->locked) {
      sprintf(buffer, "/\377\200USER %s %s %d %d %d\n", name, host, 0, oldchannel, newchannel);
      appendstring(p, buffer);
      p->locked = 1;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void send_msg_to_user(fromname, toname, text)
char *fromname, *toname, *text;
{

  char buffer[2048];
  struct connection *p;

  for (p = connections; p; p = p->next)
    if (p->type == CT_USER && !strcmp(p->name, toname))
      if (p->via) {
	if (!p->via->locked) {
	  sprintf(buffer, "/\377\200UMSG %s %s %s\n", fromname, toname, text);
	  appendstring(p->via, buffer);
	  p->via->locked = 1;
	}
      } else {
	if (!p->locked) {
	  if (strcmp(fromname, "conversd")) {
	    sprintf(buffer, "<*%s*>:", fromname);
	    appendstring(p, formatline(buffer, text));
	  } else {
	    appendstring(p, text);
	    appendstring(p, "\n");
	  }
	  p->locked = 1;
	}
      }
}

/*---------------------------------------------------------------------------*/

static void send_msg_to_channel(fromname, channel, text)
char *fromname;
int channel;
char *text;
{

  char buffer[2048];
  struct connection *p;

  for (p = connections; p; p = p->next)
    if (p->type == CT_USER && p->channel == channel)
      if (p->via) {
	if (!p->via->locked) {
	  sprintf(buffer, "/\377\200CMSG %s %d %s\n", fromname, channel, text);
	  appendstring(p->via, buffer);
	  p->via->locked = 1;
	}
      } else {
	if (!p->locked) {
	  sprintf(buffer, "<%s>:", fromname);
	  appendstring(p, formatline(buffer, text));
	  p->locked = 1;
	}
      }
}

/*---------------------------------------------------------------------------*/

static void send_invite_msg(fromname, toname, channel)
char *fromname, *toname;
int channel;
{

  static char invitetext[] = "\n\007\007*** Message from %s at %s ...\nPlease join convers channel %d.\n\007\007\n";

  static char responsetext[] = "*** Invitation sent to %s @ %s.";

  char buffer[2048];
  int fdtty;
  int fdut;
  struct connection *p;
  struct stat stbuf;
  struct utmp utmpbuf;

  for (p = connections; p; p = p->next)
    if (p->type == CT_USER && !strcmp(p->name, toname)) {
      if (p->channel == channel) {
	clear_locks();
	sprintf(buffer, "*** User %s is already on this channel.", toname);
	send_msg_to_user("conversd", fromname, buffer);
	return;
      }
      if (!p->via && !p->locked) {
	sprintf(buffer, invitetext, fromname, timestring(currtime), channel);
	appendstring(p, buffer);
	clear_locks();
	sprintf(buffer, responsetext, toname, myhostname);
	send_msg_to_user("conversd", fromname, buffer);
	return;
      }
      if (p->via && !p->via->locked) {
	sprintf(buffer, "/\377\200INVI %s %s %d\n", fromname, toname, channel);
	appendstring(p->via, buffer);
	return;
      }
    }

    if ((fdut = open("/etc/utmp", O_RDONLY, 0644)) >= 0) {
      while (read(fdut, &utmpbuf, sizeof(utmpbuf)) == sizeof(utmpbuf))
	if (
#ifdef USER_PROCESS
	    utmpbuf.ut_type == USER_PROCESS &&
#endif
	    !strncmp(utmpbuf.ut_name, toname, sizeof(utmpbuf.ut_name))
	   ) {
	  strcpy(buffer, "/dev/");
	  strncat(buffer, utmpbuf.ut_line, sizeof(utmpbuf.ut_line));
	  if (stat(buffer, &stbuf)) continue;
#ifdef RISCiX
	  if (!(stbuf.st_mode & 020)) continue;
#else
	  if (!(stbuf.st_mode & 002)) continue;
#endif
	  if ((fdtty = open(buffer, O_WRONLY | O_NOCTTY, 0644)) < 0) continue;
	  sprintf(buffer, invitetext, fromname, timestring(currtime), channel);
	  if (!fork()) {
	    write(fdtty, buffer, strlen(buffer));
	    exit(0);
	  }
	  close(fdtty);
	  close(fdut);
	  clear_locks();
	  sprintf(buffer, responsetext, toname, myhostname);
	  send_msg_to_user("conversd", fromname, buffer);
	  return;
	}
      close(fdut);
    }

  for (p = connections; p; p = p->next)
    if (p->type == CT_HOST && !p->locked) {
      sprintf(buffer, "/\377\200INVI %s %s %d\n", fromname, toname, channel);
      appendstring(p, buffer);
    }

}

/*---------------------------------------------------------------------------*/

static void update_permlinks(name, cp)
char *name;
struct connection *cp;
{
  struct permlink *p;

  for (p = permlinks; p; p = p->next)
    if (!strcmp(p->name, name)) {
      p->connection = cp;
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

  char buffer[2048];
  int addrlen;
  int fd;
  struct connection *cp;
  struct permlink *p;
  struct sockaddr *addr;

  for (p = permlinks; p; p = p->next) {
    if (p->connection || p->retrytime > currtime) continue;
    p->tries++;
    p->waittime <<= 1;
    if (p->waittime > MAX_WAITTIME) p->waittime = MAX_WAITTIME;
    p->retrytime = currtime + p->waittime;
    if (!(addr = build_sockaddr(p->socket, &addrlen))) continue;
    if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) continue;
    if (connect(fd, addr, addrlen) || !(cp = alloc_connection(fd))) {
      close(fd);
      continue;
    }
    p->connection = cp;
    if (*p->command) appendstring(cp, p->command);
    appendstring(cp, "convers\n");
    sprintf(buffer, "/\377\200HOST %s\n", myhostname);
    appendstring(cp, buffer);
  }
}

/*---------------------------------------------------------------------------*/

static void bye_command(cp)
struct connection *cp;
{
  struct connection *p;

  switch (cp->type) {
  case CT_UNKNOWN:
    cp->type = CT_CLOSED;
    break;
  case CT_USER:
    cp->type = CT_CLOSED;
    clear_locks();
    send_user_change_msg(cp->name, cp->host, cp->channel, -1);
    break;
  case CT_HOST:
    cp->type = CT_CLOSED;
    update_permlinks(cp->name, NULLCONNECTION);
    for (p = connections; p; p = p->next)
      if (p->via == cp) {
	p->type = CT_CLOSED;
	clear_locks();
	send_user_change_msg(p->name, p->host, p->channel, -1);
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

  char *s;
  char buffer[2048];
  int newchannel;

  s = getarg(0, 0);
  if (!*s) {
    sprintf(buffer, "*** You are on channel %d.\n", cp->channel);
    appendstring(cp, buffer);
    return;
  }
  newchannel = atoi(s);
  if (newchannel < 0 || newchannel > MAXCHANNEL) {
    sprintf(buffer, "*** Channel numbers must be in the range 0..%d.\n", MAXCHANNEL);
    appendstring(cp, buffer);
    return;
  }
  if (newchannel == cp->channel) {
    sprintf(buffer, "*** Already on channel %d.\n", cp->channel);
    appendstring(cp, buffer);
    return;
  }
  send_user_change_msg(cp->name, cp->host, cp->channel, newchannel);
  cp->channel = newchannel;
  sprintf(buffer, "*** Now on channel %d.\n", cp->channel);
  appendstring(cp, buffer);
}

/*---------------------------------------------------------------------------*/

static void help_command(cp)
struct connection *cp;
{
  appendstring(cp, "Commands may be abbreviated. Commands are:\n");

  appendstring(cp, "/?                   Print help information\n");
  appendstring(cp, "/BYE                 Terminate the convers session\n");
  appendstring(cp, "/CHANNEL n           Switch to channel n\n");
  appendstring(cp, "/EXIT                Terminate the convers session\n");
  appendstring(cp, "/HELP                Print help information\n");
  appendstring(cp, "/INVITE user         Invite user to join your channel\n");
  appendstring(cp, "/LINKS [LONG]        List all connections to other hosts\n");
  appendstring(cp, "/MSG user text...    Send a private message to user\n");
  appendstring(cp, "/QUIT                Terminate the convers session\n");
  appendstring(cp, "/WHO [QUICK] [LONG]  List all users and their channel numbers\n");
  appendstring(cp, "/WRITE user text...  Send a private message to user\n");

  appendstring(cp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void invite_command(cp)
struct connection *cp;
{
  char *toname;

  toname = getarg(0, 0);
  if (*toname) send_invite_msg(cp->name, toname, cp->channel);
}

/*---------------------------------------------------------------------------*/

static void links_command(cp)
struct connection *cp;
{

  char buffer[2048];
  char tmp[80];
  int full;
  struct connection *pc;
  struct permlink *pp;

  full = *(getarg(0, 0));
  appendstring(cp, full ?
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
      appendstring(cp, buffer);
    }
  for (pp = permlinks; pp; pp = pp->next)
    if (!pp->connection || pp->connection->type != CT_HOST) {
      strcpy(tmp, timestring(pp->retrytime)),
      sprintf(buffer,
	      full ?
		"%-8.8s %-12s %s  %s %5d\n" :
		"%-8.8s %-12s %s\n",
	      pp->name,
	      pp->connection ? "Connecting" : "Disconnected",
	      timestring(pp->statetime),
	      tmp,
	      pp->tries);
      appendstring(cp, buffer);
    }
  appendstring(cp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void msg_command(cp)
struct connection *cp;
{

  char *toname, *text;
  char buffer[2048];
  struct connection *p;

  toname = getarg(0, 0);
  text = getarg(0, 1);
  if (!*text) return;
  for (p = connections; p; p = p->next)
    if (p->type == CT_USER && !strcmp(p->name, toname)) break;
  if (!p) {
    sprintf(buffer, "*** No such user: %s.\n", toname);
    appendstring(cp, buffer);
  } else
    send_msg_to_user(cp->name, toname, text);
}

/*---------------------------------------------------------------------------*/

static void name_command(cp)
struct connection *cp;
{

  char buffer[2048];
  int newchannel;

  strcpy(cp->name, getarg(0, 0));
  if (!*cp->name) return;
  cp->type = CT_USER;
  strcpy(cp->host, myhostname);
  sprintf(buffer, "conversd @ %s $Revision: 2.30 $  Type /HELP for help.\n", myhostname);
  appendstring(cp, buffer);
  newchannel = atoi(getarg(0, 0));
  if (newchannel < 0 || newchannel > MAXCHANNEL) {
    sprintf(buffer, "*** Channel numbers must be in the range 0..%d.\n", MAXCHANNEL);
    appendstring(cp, buffer);
  } else
    cp->channel = newchannel;
  send_user_change_msg(cp->name, cp->host, -1, cp->channel);
}

/*---------------------------------------------------------------------------*/

static void who_command(cp)
struct connection *cp;
{

  char buffer[2048];
  int channel;
  int full = 0;
  int quick = 0;
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
    appendstring(cp, "Channel Users\n");
    clear_locks();
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
	appendstring(cp, buffer);
      }
    } while (channel >= 0);
    appendstring(cp, "***\n");
    return;
  }

  appendstring(cp, full ?
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
      appendstring(cp, buffer);
    }
  appendstring(cp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void h_cmsg_command(cp)
struct connection *cp;
{

  char *name;
  char *text;
  int channel;

  name = getarg(0, 0);
  channel = atoi(getarg(0, 0));
  text = getarg(0, 1);
  if (*text) send_msg_to_channel(name, channel, text);
}

/*---------------------------------------------------------------------------*/

static void h_host_command(cp)
struct connection *cp;
{

  char *name;
  char buffer[2048];
  struct connection *p;
  struct permlink *pp;

  name = getarg(0, 0);
  if (!*name) return;
  for (p = connections; p; p = p->next)
    if (!strcmp(p->name, name)) bye_command(p);
  for (pp = permlinks; pp; pp = pp->next)
    if (!strcmp(pp->name, name) && pp->connection && pp->connection != cp)
      bye_command((strcmp(myhostname, name) < 0) ? pp->connection : cp);
  if (cp->type != CT_UNKNOWN) return;
  cp->type = CT_HOST;
  strcpy(cp->name, name);
  update_permlinks(name, cp);
  sprintf(buffer, "/\377\200HOST %s\n", myhostname);
  appendstring(cp, buffer);
  for (p = connections; p; p = p->next)
    if (p->type == CT_USER) {
      sprintf(buffer, "/\377\200USER %s %s %d %d %d\n", p->name, p->host, 0, -1, p->channel);
      appendstring(cp, buffer);
    }
}

/*---------------------------------------------------------------------------*/

static void h_invi_command(cp)
struct connection *cp;
{

  char *fromname, *toname;
  int channel;

  fromname = getarg(0, 0);
  toname = getarg(0, 0);
  channel = atoi(getarg(0, 0));
  send_invite_msg(fromname, toname, channel);
}

/*---------------------------------------------------------------------------*/

static void h_umsg_command(cp)
struct connection *cp;
{
  char *fromname, *toname, *text;

  fromname = getarg(0, 0);
  toname = getarg(0, 0);
  text = getarg(0, 1);
  if (*text) send_msg_to_user(fromname, toname, text);
}

/*---------------------------------------------------------------------------*/

static void h_user_command(cp)
struct connection *cp;
{

  char *host;
  char *name;
  int newchannel;
  int oldchannel;
  struct connection *p;

  name = getarg(0, 0);
  host = getarg(0, 0);
  getarg(0, 0);            /*** ignore this argument, protocol has changed ***/
  oldchannel = atoi(getarg(0, 0));
  newchannel = atoi(getarg(0, 0));

  for (p = connections; p; p = p->next)
    if (p->type == CT_USER       &&
	p->channel == oldchannel &&
	p->via == cp             &&
	!strcmp(p->name, name)   &&
	!strcmp(p->host, host))  break;
  if (!p) {
    p = (struct connection *) calloc(1, sizeof(*p));
    p->type = CT_USER;
    strcpy(p->name, name);
    strcpy(p->host, host);
    p->via = cp;
    p->channel = oldchannel;
    p->time = currtime;
    p->fd = -1;
    p->next = connections;
    connections = p;
  }
  if ((p->channel = newchannel) < 0) p->type = CT_CLOSED;
  send_user_change_msg(name, host, oldchannel, newchannel);
}

/*---------------------------------------------------------------------------*/

static void process_input(cp)
struct connection *cp;
{

  static struct cmdtable {
    char *name;
    void (*fnc)();
    int states;
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

  char *arg;
  char buffer[2048];
  int arglen;
  struct cmdtable *cmdp;

  clear_locks();
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
      appendstring(cp, buffer);
    }
    return;
  }

  if (cp->type == CT_USER)
    send_msg_to_channel(cp->name, cp->channel, cp->ibuf);
}

/*---------------------------------------------------------------------------*/

static void read_configuration()
{

  FILE *fp;
  char *host_name, *sock_name;
  char line[1024];
  struct permlink *p;

  if (!(fp = fopen(CONFFILE, "r"))) return;
  if (fgets(line, sizeof(line), fp)) {
    host_name = getarg(line, 0);
    if (*host_name) {
      if (myhostname) free(myhostname);
      myhostname = strdup(host_name);
    }
  }
  while (fgets(line, sizeof(line), fp)) {
    host_name = getarg(line, 0);
    sock_name = getarg(0, 0);
    if (*host_name && *sock_name) {
      p = (struct permlink *) calloc(1, sizeof(*p));
      strcpy(p->name, host_name);
      strcpy(p->socket, sock_name);
      strcpy(p->command, getarg(0, 1));
      p->next = permlinks;
      permlinks = p;
      update_permlinks(host_name, NULLCONNECTION);
    }
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void check_files_changed()
{

  static long conftime;
  static long nexttime;
  static long progtime;
  struct stat statbuf;

  if (nexttime > currtime) return;
  nexttime = currtime + 600;

  if (!stat(PROGFILE, &statbuf)) {
    if (!progtime) progtime = statbuf.st_mtime;
    if (progtime != statbuf.st_mtime && statbuf.st_mtime < currtime - 60)
      exit(0);
  }
  if (!stat(CONFFILE, &statbuf)) {
    if (!conftime) conftime = statbuf.st_mtime;
    if (conftime != statbuf.st_mtime && statbuf.st_mtime < currtime - 60)
      exit(0);
  }
}

/*---------------------------------------------------------------------------*/

int main(argc, argv)
int argc;
char **argv;
{

  static struct {
    char *name;
    int fd;
  } listeners[] = {
    "unix:/tcp/sockets/convers", -1,
    "*:3600", -1,
    0, -1
  };

  char *sp;
  char buffer[2048];
  int addrlen;
  int arg;
  int i;
  int size;
  int status;
  struct connection *cp;
  struct fd_set actread;
  struct fd_set actwrite;
  struct mbuf *bp;
  struct sockaddr *addr;
  struct timeval timeout;

  umask(022);
  for (i = 0; i < FD_SETSIZE; i++) close(i);
  chdir("/");
  setsid();
  signal(SIGPIPE, SIG_IGN);
  if (!getenv("TZ")) putenv("TZ=MEZ-1MESZ");

  gethostname(buffer, sizeof(buffer));
  if (sp = strchr(buffer, '.')) *sp = 0;
  myhostname = strdup(buffer);

  time(&currtime);

  if (argc < 2) {
    read_configuration();
  } else {
    listeners[0].name = argv[1];
    listeners[1].name = 0;
  }

  for (i = 0; listeners[i].name; i++)
    if ((addr = build_sockaddr(listeners[i].name, &addrlen)) &&
	(listeners[i].fd = socket(addr->sa_family, SOCK_STREAM, 0)) >= 0) {
      switch (addr->sa_family) {
      case AF_UNIX:
	unlink(addr->sa_data);
	break;
      case AF_INET:
	arg = 1;
	setsockopt(listeners[i].fd, SOL_SOCKET, SO_REUSEADDR, (char *) &arg, sizeof(arg));
	break;
      }
      if (bind(listeners[i].fd, addr, addrlen) || listen(listeners[i].fd, SOMAXCONN)) {
	close(listeners[i].fd);
	listeners[i].fd = -1;
      } else {
	if (maxfd < listeners[i].fd) maxfd = listeners[i].fd;
	FD_SET(listeners[i].fd, &chkread);
      }
    }

  for (; ; ) {

    free_closed_connections();

    while (waitpid(-1, &status, WNOHANG) > 0) ;

    check_files_changed();

    connect_permlinks();

    actread = chkread;
    actwrite = chkwrite;
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;
    if (select(maxfd + 1, SEL_ARG(&actread), SEL_ARG(&actwrite), SEL_ARG(0), &timeout) <= 0) {
      FD_ZERO(&actread);
      FD_ZERO(&actwrite);
    }

    time(&currtime);

    for (i = 0; listeners[i].name; i++)
      if (listeners[i].fd >= 0 && FD_ISSET(listeners[i].fd, &actread))
	accept_connect_request(listeners[i].fd);

    for (cp = connections; cp; cp = cp->next) {

      if (cp->fd >= 0 && FD_ISSET(cp->fd, &actread)) {
	size = read(cp->fd, buffer, sizeof(buffer));
	if (size <= 0)
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
      }

      if (cp->fd >= 0 && FD_ISSET(cp->fd, &actwrite)) {
	bp = cp->obuf;
	size = write(cp->fd, bp->data, strlen(bp->data));
	if (size <= 0)
	  bye_command(cp);
	else {
	  cp->xmitted += size;
	  bp->data += size;
	  if (!*bp->data) {
	    cp->obuf = bp->next;
	    free(bp);
	  }
	  if (!cp->obuf) FD_CLR(cp->fd, &chkwrite);
	}
      }

    }
  }
}

