#ifndef __lint
static const char rcsid[] = "@(#) $Id: conversd.c,v 2.79 1998/03/09 17:42:49 deyke Exp $";
#endif

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#ifdef _AIX
#include <sys/select.h>
#endif

#ifndef MAXIOV
#if defined IOV_MAX
#define MAXIOV          IOV_MAX
#else
#define MAXIOV          16
#endif
#endif

#ifndef O_NOCTTY
#define O_NOCTTY        0
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK      O_NDELAY
#endif

#ifndef SOMAXCONN
#define SOMAXCONN       5
#endif

#ifndef WNOHANG
#define WNOHANG         1
#endif

#if defined __hpux && !defined _FD_SET
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

extern char *optarg;
extern int optind;

#include "buildsaddr.h"
#include "configure.h"
#include "md5.h"
#include "strdup.h"

#define CHECK_UNIQUE_TIME       (  30*60)
#define KEEP_USERS_TIME         (  60*60)
#define MAKE_UNIQUE_TIME        (  60*60)
#define MAX_CHANNEL             32767
#define MAX_IDLETIME            (  60*60)
#define MAX_PINGTIME            (2*60*60)
#define MAX_RTT                 (3*60*60)
#define MAX_UNKNOWNTIME         (  15*60)
#define MAX_WAITTIME            (3*60*60)
#define MIN_PINGTIME            (   5*60)
#define MIN_WAITTIME            (     60)
#define NO_NOTE                 "@"
#define UNIQMARKER              (' ' | 0x80)

enum e_dir {
  INBOUND,
  OUTBOUND
};

enum e_what {
  ONE_TOKEN,
  REST_OF_LINE
};

enum e_case {
  KEEP_CASE,
  LOWER_CASE
};

struct mbuf {
  struct mbuf *m_next;          /* Linked list pointer */
  char *m_data;                 /* Active working pointer */
  int m_cnt;                    /* Number of bytes in data buffer */
};

struct quality {
  struct link *q_link;          /* Pointer to link */
  long q_rtt;                   /* RTT via this link */
  long q_lastrtt;               /* Last RTT reported to this link */
  long q_sendtime;              /* Time DEST was sent to this link */
  struct quality *q_next;       /* Linked-list pointer */
};

struct host {
  char *h_name;                 /* Name of host */
  char *h_software;             /* Software on this host */
  char *h_capabilities;         /* Capabilities of this host */
  struct quality *h_qualities;  /* List of qualities */
  struct host *h_next;          /* Linked list pointer */
};

struct user {
  char *u_name;                 /* Name of user */
  struct host *u_host;          /* Host where user is logged on */
  long u_seq;                   /* Sequence number of this entry */
  struct link *u_link;          /* Link to this user */
  int u_channel;                /* Channel number */
  int u_oldchannel;             /* Prev channel number */
  char *u_note;                 /* Personal note */
  long u_stime;                 /* Connect time */
  struct user *u_next;          /* Linked list pointer */
};

struct link {
  struct user *l_user;          /* Pointer to user if user link */
  struct host *l_host;          /* Pointer to host if host link */
  int l_locked;                 /* Set if mesg already sent */
  int l_fd;                     /* Socket descriptor */
  char l_ibuf[2048];            /* Input buffer */
  int l_icnt;                   /* Number of bytes in input buffer */
  int l_received;               /* Number of bytes received */
  struct mbuf *l_sndq;          /* Output queue */
  int l_xmitted;                /* Number of bytes transmitted */
  int l_closed;                 /* Set if BYE command was executed */
  long l_stime;                 /* Time of last state change */
  long l_mtime;                 /* Time of last input/output */
  long l_sendtime;              /* Time PING was sent */
  long l_nexttime;              /* Time next PING will be send */
  long l_rxrtt;                 /* RTT received by PONG */
  long l_txrtt;                 /* RTT measured (PONG_time - PING_time) */
  double l_srtt;                /* Smoothed RTT */
  struct link *l_next;          /* Linked list pointer */
};

struct peer {
  char *p_socket;               /* Name of socket to connect to */
  char *p_command;              /* Optional connect command */
  long p_stime;                 /* Time of last state change */
  int p_tries;                  /* Number of connect tries */
  long p_retrytime;             /* Time of next connect try */
  struct link *p_link;          /* Pointer to link if connected */
  struct peer *p_next;          /* Linked list pointer */
};

static TYPE_FD_SET chkread;
static void (*readfnc[FD_SETSIZE])(void *);
static void *readarg[FD_SETSIZE];

static TYPE_FD_SET chkwrite;
static void (*writefnc[FD_SETSIZE])(void *);
static void *writearg[FD_SETSIZE];

static const char conversd[] = "conversd";
static const char progfile[] = "/tcp/conversd";
static const char *conffile = "/tcp/convers.conf";
static int debug;
static int maxfd = -1;
static int min_waittime = MIN_WAITTIME;
static long currtime;
static long next_desttime;
static struct host my, *hosts = &my;
static struct link *links;
static struct peer *peers;
static struct user *users;

static void close_link(struct link *lp);
static void link_recv(struct link *lp);
static void link_send(struct link *lp);

/*---------------------------------------------------------------------------*/

#define lround(d)               ((long) ((d) + 0.5))

/*---------------------------------------------------------------------------*/

static void strchg(char **string_ptr, const char *string)
{
  if (*string_ptr)
    free(*string_ptr);
  *string_ptr = strdup((char *) string);
  if (!*string_ptr)
    exit(1);
}

/*---------------------------------------------------------------------------*/

static void inc_seq(long *seq_ptr)
{
  if (++(*seq_ptr) < currtime)
    *seq_ptr = currtime;
}

/*---------------------------------------------------------------------------*/

static int is_string_unique(const char *string, long duration)
{

  struct string_t {
    struct string_t *s_next;
    long s_time;
    unsigned char s_digest[16];
  };

  int i;
  long time_limit;
  MD5_CTX mdContext;
  static struct string_t *stringstail[256];
  static struct string_t *strings[256];
  struct string_t *sp;

  MD5Init(&mdContext);
  MD5Update(&mdContext, (unsigned char *) string, strlen(string));
  MD5Final(&mdContext);
  i = mdContext.digest[0] & 0xff;

  time_limit = currtime - MAKE_UNIQUE_TIME;
  while ((sp = strings[i]) && sp->s_time < time_limit) {
    strings[i] = sp->s_next;
    free(sp);
  }

  time_limit = currtime - duration;
  for (sp = strings[i]; sp; sp = sp->s_next) {
    if (sp->s_time >= time_limit &&
	!memcmp((char *) sp->s_digest, (char *) mdContext.digest, sizeof(mdContext.digest)))
      return 0;
  }

  sp = (struct string_t *) malloc(sizeof(struct string_t));
  if (!sp)
    exit(1);
  sp->s_next = 0;
  sp->s_time = currtime;
  memcpy((char *) sp->s_digest, (char *) mdContext.digest, sizeof(mdContext.digest));
  if (strings[i])
    stringstail[i]->s_next = sp;
  else
    strings[i] = sp;
  stringstail[i] = sp;
  return 1;
}

/*---------------------------------------------------------------------------*/

static void make_string_unique(char *string)
{

  char *cp;
  int i;

  if (is_string_unique(string, MAKE_UNIQUE_TIME))
    return;
  cp = strchr(string, UNIQMARKER);
  if (cp) {
    cp++;
    i = atoi(cp);
  } else {
    for (cp = string; *cp; cp++) ;
    if (cp > string && cp[-1] == '\n')
      cp--;
    *cp++ = (char) UNIQMARKER;
    i = 0;
  }
  for (;;) {
    sprintf(cp, "%d\n", ++i);
    if (is_string_unique(string, MAKE_UNIQUE_TIME))
      return;
  }
}

/*---------------------------------------------------------------------------*/

static struct host *hostptr(const char *name)
{

  int r;
  struct host **hpp;
  struct host *hp;

  for (hpp = &hosts; ; hpp = &hp->h_next) {
    hp = *hpp;
    if (!hp || (r = strcmp(hp->h_name, name)) >= 0) {
      if (!hp || r) {
	hp = (struct host *) calloc(1, sizeof(struct host));
	if (!hp)
	  exit(1);
	strchg(&hp->h_name, name);
	strchg(&hp->h_software, "");
	strchg(&hp->h_capabilities, "");
	hp->h_next = *hpp;
	*hpp = hp;
      }
      return hp;
    }
  }
}

/*---------------------------------------------------------------------------*/

static struct user *userptr(const char *name, struct host *hp)
{

  int r = 0;
  struct user **upp;
  struct user *up;

  for (upp = &users; ; upp = &up->u_next) {
    up = *upp;
    if (!up ||
	(r = strcmp(up->u_name, name)) > 0 ||
	(!r && (r = strcmp(up->u_host->h_name, hp->h_name)) >= 0)) {
      if (!up || r) {
	up = (struct user *) calloc(1, sizeof(struct user));
	if (!up)
	  exit(1);
	strchg(&up->u_name, name);
	up->u_host = hp;
	up->u_channel = up->u_oldchannel = -1;
	strchg(&up->u_note, NO_NOTE);
	up->u_stime = currtime;
	up->u_next = *upp;
	*upp = up;
      }
      return up;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void trace(enum e_dir dir, const struct link *lp, const char *string)
{

  char buf[16];
  char *cp;
  const char *name;

  if (lp->l_user)
    name = lp->l_user->u_name;
  else if (lp->l_host)
    name = lp->l_host->h_name;
  else {
    sprintf(buf, "%08lx", (long) lp);
    name = buf;
  }
  cp = ctime((time_t *) &currtime);
  cp[24] = 0;
  if (dir == OUTBOUND)
    printf("\n%s SEND %s->%s:\n%s", cp, my.h_name, name, string);
  else
    printf("\n%s RECV %s->%s:\n%s", cp, name, my.h_name, string);
  fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static void send_string(struct link *lp, const char *string)
{

  int len;
  struct mbuf *mp;
  struct mbuf *p;

  if (!*string) return;
  if (debug) trace(OUTBOUND, lp, string);
  len = strlen(string);
  mp = (struct mbuf *) malloc(sizeof(struct mbuf) + len);
  if (!mp)
    exit(1);
  mp->m_next = 0;
  memcpy(mp->m_data = (char *) (mp + 1), string, mp->m_cnt = len);
  if (lp->l_sndq) {
    for (p = lp->l_sndq; p->m_next; p = p->m_next) ;
    p->m_next = mp;
  } else {
    lp->l_sndq = mp;
    writefnc[lp->l_fd] = (void (*)(void *)) link_send;
    writearg[lp->l_fd] = lp;
    FD_SET(lp->l_fd, &chkwrite);
  }
  lp->l_locked = 1;
}

/*---------------------------------------------------------------------------*/

static int queuelength(const struct mbuf *mp)
{
  int len;

  for (len = 0; mp; mp = mp->m_next)
    len += mp->m_cnt;
  return len;
}

/*---------------------------------------------------------------------------*/

static void free_resources(void)
{

  struct link **lpp;
  struct link *lp;
  struct mbuf *mp;
  struct user **upp;
  struct user *up;

  for (lpp = &links; (lp = *lpp); ) {
    if (lp->l_user || lp->l_host) {
      if (lp->l_mtime + MAX_IDLETIME < currtime) send_string(lp, "\n");
    } else if (lp->l_fd >= 0) {
      if (lp->l_stime + MAX_UNKNOWNTIME < currtime) close_link(lp);
    }
    if (lp->l_fd < 0) {
      *lpp = lp->l_next;
      while ((mp = lp->l_sndq)) {
	lp->l_sndq = mp->m_next;
	free(mp);
      }
      free(lp);
    } else
      lpp = &lp->l_next;
  }

  for (upp = &users; (up = *upp); )
    if (up->u_channel < 0 && up->u_stime + KEEP_USERS_TIME < currtime) {
      *upp = up->u_next;
      free(up->u_name);
      free(up->u_note);
      free(up);
    } else
      upp = &up->u_next;

}

/*---------------------------------------------------------------------------*/

static char *getarg(char *line, enum e_what what, enum e_case how)
{

  char *arg;
  char *cp;
  static char *next_arg;

  if (line) {
    for (next_arg = cp = line; *cp; cp++) ;
    while (--cp >= line && isspace(*cp & 0xff)) ;
    cp[1] = 0;
  }
  while (isspace(*next_arg & 0xff))
    next_arg++;
  arg = next_arg;
  if (what == ONE_TOKEN) {
    while (*next_arg && !isspace(*next_arg & 0xff))
      next_arg++;
    if (*next_arg)
      *next_arg++ = 0;
  } else {
    next_arg = "";
  }
  if (how == LOWER_CASE) {
    for (cp = arg; *cp; cp++) {
      if (*cp >= 'A' && *cp <= 'Z')
	*cp = tolower(*cp);
    }
  }
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

static char *formatline(const char *prefix, const char *text)
{

#define PREFIXLEN       10
#define CONVLINELEN     79

  char *e, *t;
  const char *f, *x;
  int l, lw;
  static char buf[2048];

  e = buf + (sizeof(buf) - 2);

  /* Copy prefix into buf, set l to length of prefix */
  for (f = prefix, t = buf; *f; f++)
    if (t < e) *t++ = *f;
  l = t - buf;

  for (f = text; ; ) {

    /* Skip leading spaces */
    while (isspace(*f & 0xff)) f++;

    /* Return if nothing more or no room left */
    if (!*f || t >= e) {
      *t++ = '\n';
      *t = 0;
      return buf;
    }

    /* Find length of next word (seq. of non-blanks) */
    for (x = f; *x && !isspace(*x & 0xff); x++) ;
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

static char *localtimestring(long utc)
{

  static char buffer[8];
  static const char monthnames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  struct tm *tm;

  tm = localtime((time_t *) &utc);
  if (utc + 24 * 60 * 60 > currtime)
    sprintf(buffer, " %2d:%02d", tm->tm_hour, tm->tm_min);
  else
    sprintf(buffer, "%-3.3s %2d", monthnames + 3 * tm->tm_mon, tm->tm_mday);
  return buffer;
}

/*---------------------------------------------------------------------------*/

static void accept_connect_request(const int *flistenptr)
{

  int addrlen;
  int fd;
  int flags;
  struct link *lp;
  struct sockaddr addr;

  addrlen = sizeof(addr);
  if ((fd = accept(*flistenptr, &addr, &addrlen)) < 0) return;
  if ((flags = fcntl(fd, F_GETFL, 0)) == -1 ||
      fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    close(fd);
    return;
  }
  lp = (struct link *) calloc(1, sizeof(struct link));
  if (!lp)
    exit(1);
  lp->l_fd = fd;
  lp->l_stime = lp->l_mtime = currtime;
  lp->l_next = links;
  links = lp;
  readfnc[fd] = (void (*)(void *)) link_recv;
  readarg[fd] = lp;
  FD_SET(fd, &chkread);
  if (maxfd < fd) maxfd = fd;
}

/*---------------------------------------------------------------------------*/

static void clear_locks(void)
{
  struct link *lp;

  for (lp = links; lp; lp = lp->l_next) lp->l_locked = 0;
}

/*---------------------------------------------------------------------------*/

static struct quality *find_quality(struct host *hp, struct link *lp)
{
  struct quality *qp;

  for (qp = hp->h_qualities; qp && qp->q_link != lp; qp = qp->q_next) ;
  if (!qp) {
    qp = (struct quality *) calloc(1, sizeof(struct quality));
    if (!qp)
      exit(1);
    qp->q_link = lp;
    qp->q_next = hp->h_qualities;
    hp->h_qualities = qp;
  }
  return qp;
}

/*---------------------------------------------------------------------------*/

static struct quality *find_best_quality(const struct host *hp)
{

  struct quality *qpbest = 0;
  struct quality *qp;

  for (qp = hp->h_qualities; qp; qp = qp->q_next) {
    if (qp->q_rtt && (!qpbest || qpbest->q_rtt >= qp->q_rtt)) {
      qpbest = qp;
    }
  }
  return qpbest;
}

/*---------------------------------------------------------------------------*/

static void send_user_change_msg(const struct user *up)
{

  char buffer[2048];
  struct link *lp;

  for (lp = links; lp; lp = lp->l_next) {
    if (!lp->l_locked) {
      if (lp->l_user && up->u_oldchannel != up->u_channel) {
	if (lp->l_user->u_channel == up->u_oldchannel) {
	  if (up->u_channel >= 0)
	    sprintf(buffer, "*** %s switched to channel %d.\n", up->u_name, up->u_channel);
	  else
	    sprintf(buffer, "*** %s signed off.\n", up->u_name);
	  send_string(lp, buffer);
	} else if (lp->l_user->u_channel == up->u_channel) {
	  sprintf(buffer, "*** %s signed on.\n", up->u_name);
	  send_string(lp, buffer);
	}
      } else if (lp->l_host) {
	sprintf(buffer,
		"/\377\200USER %s %s %ld %d %d %s\n",
		up->u_name,
		up->u_host->h_name,
		up->u_seq,
		up->u_oldchannel,
		up->u_channel,
		up->u_channel >= 0 ? up->u_note : "");
	send_string(lp, buffer);
      }
    }
  }
}

/*---------------------------------------------------------------------------*/

static void send_msg_to_user(const char *fromname, const char *toname, const char *text, int make_unique)
{

  char *cp;
  char *formatted_line;
  char host_buffer[2048];
  char prefix[2048];
  struct link *lp;
  struct user *up;

  if (!*text) return;
  sprintf(host_buffer, "/\377\200UMSG %s %s %s\n", fromname, toname, text);
  if (make_unique)
    make_string_unique(host_buffer);
  else if (!is_string_unique(host_buffer, CHECK_UNIQUE_TIME))
    return;
  if ((cp = strchr(text, UNIQMARKER))) *cp = 0;
  if (strcmp(fromname, conversd)) {
    sprintf(prefix, "<*%s*>:", fromname);
    formatted_line = formatline(prefix, text);
  } else {
    sprintf(prefix, "%s\n", text);
    formatted_line = prefix;
  }
  for (up = users; up; up = up->u_next)
    if (up->u_channel >= 0) {
      lp = up->u_link;
      if (!lp->l_locked && !strcmp(up->u_name, toname))
	send_string(lp, lp->l_host ? host_buffer : formatted_line);
    }
}

/*---------------------------------------------------------------------------*/

static void send_msg_to_channel(const char *fromname, int channel, const char *text, int make_unique)
{

  char *cp;
  char *formatted_line;
  char host_buffer[2048];
  char prefix[2048];
  struct link *lp;
  struct user *up;

  if (!*text) return;
  sprintf(host_buffer, "/\377\200CMSG %s %d %s\n", fromname, channel, text);
  if (make_unique)
    make_string_unique(host_buffer);
  else if (!is_string_unique(host_buffer, CHECK_UNIQUE_TIME))
    return;
  if ((cp = strchr(text, UNIQMARKER))) *cp = 0;
  sprintf(prefix, "<%s>:", fromname);
  formatted_line = formatline(prefix, text);
  for (up = users; up; up = up->u_next)
    if (up->u_channel >= 0) {
      lp = up->u_link;
      if (!lp->l_locked && up->u_channel == channel)
	send_string(lp, lp->l_host ? host_buffer : formatted_line);
    }
}

/*---------------------------------------------------------------------------*/

static void send_invite_msg(const char *fromname, const char *toname, int channel, const char *text, int make_unique)
{

  static const char invitetext[] =
  "\n\007\007*** Message from %s at %s ...\n"
  "Please join convers channel %d.\n\007\007\n";

  static const char responsetext[] =
  "*** Invitation sent to %s @ %s.";

  char buffer[2048];
  char host_buffer[2048];
  int fdtty;
  int fdut;
  struct link *lp;
  struct stat statbuf;
  struct user *up;
  struct utmp utmpbuf;

  sprintf(host_buffer, "/\377\200INVI %s %s %d %s\n", fromname, toname, channel, text);
  if (make_unique)
    make_string_unique(host_buffer);
  else if (!is_string_unique(host_buffer, CHECK_UNIQUE_TIME))
    return;

  for (up = users; up; up = up->u_next)
    if (up->u_channel == channel && !strcmp(up->u_name, toname)) {
      clear_locks();
      sprintf(buffer, "*** User %s is already on this channel.", toname);
      send_msg_to_user(conversd, fromname, buffer, 1);
      return;
    }

  for (up = users; up; up = up->u_next)
    if (up->u_channel >= 0 && up->u_link->l_user && !strcmp(up->u_name, toname)) {
      sprintf(buffer, invitetext, fromname, localtimestring(currtime), channel);
      send_string(up->u_link, buffer);
      clear_locks();
      sprintf(buffer, responsetext, toname, my.h_name);
      send_msg_to_user(conversd, fromname, buffer, 1);
      return;
    }

  for (up = users; up; up = up->u_next)
    if (up->u_channel >= 0 && !up->u_link->l_locked && !strcmp(up->u_name, toname)) {
      send_string(up->u_link, host_buffer);
      return;
    }

  if (*UTMP__FILE && (fdut = open(UTMP__FILE, O_RDONLY, 0644)) >= 0) {
    while (read(fdut, &utmpbuf, sizeof(utmpbuf)) == sizeof(utmpbuf))
      if (
#ifdef USER_PROCESS
	  utmpbuf.ut_type == USER_PROCESS &&
#endif
	  !strncmp(utmpbuf.ut_name, toname, sizeof(utmpbuf.ut_name))) {
	strcpy(buffer, "/dev/");
	strncat(buffer, utmpbuf.ut_line, sizeof(utmpbuf.ut_line));
	if (stat(buffer, &statbuf)) continue;
	if (!(statbuf.st_mode & 022)) continue;
	if ((fdtty = open(buffer, O_WRONLY | O_NOCTTY, 0644)) < 0) continue;
	sprintf(buffer, invitetext, fromname, localtimestring(currtime), channel);
	if (!fork()) {
	  write(fdtty, buffer, strlen(buffer));
	  _exit(0);
	}
	close(fdtty);
	close(fdut);
	clear_locks();
	sprintf(buffer, responsetext, toname, my.h_name);
	send_msg_to_user(conversd, fromname, buffer, 1);
	return;
      }
    close(fdut);
  }

  for (lp = links; lp; lp = lp->l_next)
    if (lp->l_host && !lp->l_locked)
      send_string(lp, host_buffer);

}

/*---------------------------------------------------------------------------*/

static void handle_rout_msg(const char *fromname, const char *toname, int ttl)
{

  char buffer[2048];
  struct host *hp;
  struct quality *qp;

  if (!*fromname || !*toname)
    return;

  clear_locks();

  for (hp = hosts;; hp = hp->h_next) {
    if (!hp) {
      sprintf(buffer,
	      "*** Route: %s does not know '%s'",
	      my.h_name,
	      toname);
      send_msg_to_user(conversd, fromname, buffer, 1);
      return;
    }
    if (!strcmp(hp->h_name, toname))
      break;
  }

  qp = find_best_quality(hp);
  if (!qp) {
    sprintf(buffer,
	    "*** Route: %s has no route to '%s'",
	    my.h_name,
	    toname);
    send_msg_to_user(conversd, fromname, buffer, 1);
    return;
  }

  sprintf(buffer,
	  "*** Route: %s (%ld) %s -> %s",
	  my.h_name,
	  qp->q_rtt,
	  qp->q_link->l_host->h_name,
	  toname);
  send_msg_to_user(conversd, fromname, buffer, 1);

  if (ttl > 0 && strcmp(qp->q_link->l_host->h_name, toname)) {
    sprintf(buffer, "/\377\200ROUT %s %s %d\n", toname, fromname, ttl - 1);
    send_string(qp->q_link, buffer);
  }
}

/*---------------------------------------------------------------------------*/

static void connect_peers(void)
{

  char *cp1;
  char *cp;
  char buffer[2048];
  int addrlen;
  int fd;
  int flags;
  int wt;
  struct link *lp;
  struct peer *pp;
  struct sockaddr *addr;

  for (pp = peers; pp; pp = pp->p_next) {
    if (pp->p_link || pp->p_retrytime > currtime) continue;
    wt = min_waittime * (1 << pp->p_tries);
    pp->p_tries++;
    if (pp->p_tries >= 16 || wt > MAX_WAITTIME) wt = MAX_WAITTIME;
    pp->p_retrytime = currtime + wt;
    if (!(addr = build_sockaddr(pp->p_socket, &addrlen))) continue;
    if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) continue;
    if ((flags = fcntl(fd, F_GETFL, 0)) == -1 ||
	fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
      close(fd);
      continue;
    }
    if (connect(fd, addr, addrlen) && errno != EINPROGRESS) {
      if (debug) perror("connect()");
      close(fd);
      continue;
    }
    lp = pp->p_link = (struct link *) calloc(1, sizeof(struct link));
    if (!lp)
      exit(1);
    lp->l_fd = fd;
    lp->l_stime = lp->l_mtime = currtime;
    lp->l_next = links;
    links = lp;
    readfnc[fd] = (void (*)(void *)) link_recv;
    readarg[fd] = lp;
    FD_SET(fd, &chkread);
    if (maxfd < fd) maxfd = fd;
    *buffer = 0;
    cp = pp->p_command;
    while (*cp) {
      if ((cp1 = strstr(cp, "\\n"))) {
	strncat(buffer, cp, cp1 - cp);
	cp = cp1 + 2;
      } else {
	strcat(buffer, cp);
	cp = "";
      }
      strcat(buffer, "\n");
    }
    strcat(buffer, "convers\n/\377\200HOST ");
    strcat(buffer, my.h_name);
    strcat(buffer, " ");
    strcat(buffer, my.h_software);
    strcat(buffer, " ");
    strcat(buffer, my.h_capabilities);
    strcat(buffer, "\n");
    send_string(lp, buffer);
  }
}

/*---------------------------------------------------------------------------*/

static long ping_interval(const struct link *lp)
{
  long d;

  if (!lp->l_srtt)
    return MAX_PINGTIME;
  d = lround(60 * lp->l_srtt);
  if (d < MIN_PINGTIME)
    return MIN_PINGTIME;
  if (d > MAX_PINGTIME)
    return MAX_PINGTIME;
  return d;
}

/*---------------------------------------------------------------------------*/

static void send_pings(void)
{
  struct link *lp;

  for (lp = links; lp; lp = lp->l_next) {
    if (lp->l_host && lp->l_nexttime <= currtime) {
      send_string(lp, "/\377\200PING\n");
      lp->l_sendtime = currtime;
      lp->l_nexttime = currtime + ping_interval(lp);
    }
  }
}

/*---------------------------------------------------------------------------*/

static void send_dests(void)
{

  char buffer[2048];
  long delay;
  long diff;
  long lastrtt;
  long minrtt;
  long rtt;
  long sendtime;
  struct host *hp;
  struct link *lp;
  struct quality *qpbest;
  struct quality *qp;

  if (next_desttime > currtime)
    return;
  next_desttime = 0x7fffffff;
  for (lp = links; lp; lp = lp->l_next) {
    if (!lp->l_host || !lp->l_srtt || !strchr(lp->l_host->h_capabilities, 'd'))
      continue;
    for (hp = hosts; hp; hp = hp->h_next) {
      if (hp == &my || hp == lp->l_host)
	continue;
      qp = find_quality(hp, lp);
      qpbest = find_best_quality(hp);
      if (qpbest && qpbest != qp) {
	rtt = qpbest->q_rtt + lround(lp->l_srtt);
	if (rtt > MAX_RTT) {
	  rtt = MAX_RTT + 1;
	}
      } else {
	rtt = MAX_RTT + 1;
      }
      if (!(lastrtt = qp->q_lastrtt))
	lastrtt = MAX_RTT + 1;
      diff = (rtt >= lastrtt) ? (rtt - lastrtt) : (lastrtt - rtt);
      if (!diff)
	continue;
      if (rtt > MAX_RTT) {
	delay = 0;
      } else if (lastrtt > MAX_RTT) {
	delay = 3 * rtt;
      } else {
	minrtt = (lastrtt <= rtt) ? lastrtt : rtt;
	delay = 36 * minrtt / diff;
      }
      sendtime = qp->q_sendtime + delay;
      if (sendtime <= currtime) {
	if (rtt > MAX_RTT)
	  rtt = 0;
	sprintf(buffer, "/\377\200DEST %s %ld %s\n", hp->h_name, qp->q_lastrtt = rtt, hp->h_software);
	send_string(lp, buffer);
	qp->q_sendtime = currtime;
      } else {
	if (next_desttime > sendtime)
	  next_desttime = sendtime;
      }
    }
  }
}

/*---------------------------------------------------------------------------*/

static void close_link(struct link *lp)
{

  int fd;
  struct host *hp;
  struct peer *pp;
  struct quality *qp;
  struct quality **qpp;
  struct user *up;

  lp->l_user = 0;
  lp->l_host = 0;

  if ((fd = lp->l_fd) >= 0) {
    readfnc[fd] = 0;
    FD_CLR(fd, &chkread);
    writefnc[fd] = 0;
    FD_CLR(fd, &chkwrite);
    if (fd == maxfd)
      while (--maxfd >= 0)
	if (FD_ISSET(maxfd, &chkread))
	  break;
    close(fd);
    lp->l_fd = -1;
  }

  for (pp = peers; pp; pp = pp->p_next)
    if (pp->p_link == lp) {
      pp->p_stime = currtime;
      if (pp->p_retrytime < currtime + min_waittime)
	pp->p_retrytime = currtime + min_waittime;
      pp->p_link = 0;
    }

  for (hp = hosts; hp; hp = hp->h_next) {
    for (qpp = &hp->h_qualities; (qp = *qpp);) {
      if (qp->q_link == lp) {
	*qpp = qp->q_next;
	free(qp);
      } else {
	qpp = &qp->q_next;
      }
    }
  }
  next_desttime = 0;

  for (up = users; up; up = up->u_next) {
    if (up->u_channel >= 0 && up->u_link == lp) {
      qp = find_best_quality(up->u_host);
      if (qp) {
	up->u_link = qp->q_link;
      } else {
	up->u_link = 0;
	if (up->u_seq) {
	  up->u_seq++;
	  if (up->u_host == &my && up->u_seq < currtime) {
	    up->u_seq = currtime;
	  }
	}
	up->u_oldchannel = up->u_channel;
	up->u_channel = -1;
	up->u_stime = currtime;
	clear_locks();
	send_user_change_msg(up);
      }
    }
  }

}

/*---------------------------------------------------------------------------*/

static void bye_command(struct link *lp)
{
  lp->l_closed = 1;
  if (!lp->l_sndq)
    close_link(lp);
}

/*---------------------------------------------------------------------------*/

static void channel_command(struct link *lp)
{

  char *cp;
  char buffer[2048];
  int newchannel;
  struct user *up;

  up = lp->l_user;
  cp = getarg(0, ONE_TOKEN, KEEP_CASE);
  if (!*cp) {
    sprintf(buffer, "*** You are on channel %d.\n", up->u_channel);
    send_string(lp, buffer);
    return;
  }
  newchannel = atoi(cp);
  if (newchannel < 0 || newchannel > MAX_CHANNEL) {
    sprintf(buffer, "*** Channel numbers must be in the range 0..%d.\n", MAX_CHANNEL);
    send_string(lp, buffer);
    return;
  }
  if (newchannel == up->u_channel) {
    sprintf(buffer, "*** Already on channel %d.\n", up->u_channel);
    send_string(lp, buffer);
    return;
  }
  inc_seq(&up->u_seq);
  up->u_oldchannel = up->u_channel;
  up->u_channel = newchannel;
  send_user_change_msg(up);
  sprintf(buffer, "*** Now on channel %d.\n", up->u_channel);
  send_string(lp, buffer);
}

/*---------------------------------------------------------------------------*/

static void help_command(struct link *lp)
{
  send_string(lp,
    "Commands may be abbreviated.  Commands are:\n"

    "/?                   Show help information\n"
    "/HELP                Show help information\n"

    "/BYE                 Terminate the convers session\n"
    "/EXIT                Terminate the convers session\n"
    "/QUIT                Terminate the convers session\n"

    "/WHO                 Show active channels and users\n"

    "/CHANNEL [n]         Switch to channel n\n"
    "/NOTE [text...]      Set personal note to text\n"
    "/PERSONAL [text...]  Set personal note to text\n"
    "/NOTE @              Clear personal note\n"
    "/PERSONAL @          Clear personal note\n"

    "/MSG user text...    Send a private message to user\n"
    "/WRITE user text...  Send a private message to user\n"

    "/INVITE user         Invite user to join your channel\n"

    "/HOSTS               Show all hosts\n"
    "/LINKS               Show all links\n"
    "/PEERS               Show all peers\n"
    "/USERS               Show all users\n"

    "/ROUTE host [ttl]    Show route to host\n"

    "***\n");
}

/*---------------------------------------------------------------------------*/

static void hosts_command(struct link *lp)
{

  char buffer[2048];
  char srttstr[80];
  const char *viastr;
  int verbose;
  struct host *hp;
  struct quality *qp;

  verbose = !strcmp(getarg(0, ONE_TOKEN, LOWER_CASE), "debug");
  send_string(lp, "Name     Via       SRTT Software Capabilities\n");
  for (hp = hosts; hp; hp = hp->h_next) {
    qp = find_best_quality(hp);
    if (qp) {
      viastr = qp->q_link->l_host->h_name;
      sprintf(srttstr, "%5ld", qp->q_rtt);
    } else {
      viastr = "";
      strcpy(srttstr, "     ");
    }
    sprintf(buffer,
	    "%-8.8s %-8.8s %s %-8.8s %s\n",
	    hp->h_name,
	    viastr,
	    srttstr,
	    hp->h_software,
	    hp->h_capabilities);
    send_string(lp, buffer);
    if (verbose && hp->h_qualities) {
      *buffer = 0;
      for (qp = hp->h_qualities; qp; qp = qp->q_next) {
	sprintf(buffer + strlen(buffer),
		" %s (S=%ld L=%ld)",
		qp->q_link->l_host->h_name,
		qp->q_rtt,
		qp->q_lastrtt);
      }
      strcat(buffer, "\n");
      send_string(lp, buffer);
    }
  }
  send_string(lp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void invite_command(struct link *lp)
{
  char *toname;

  toname = getarg(0, ONE_TOKEN, LOWER_CASE);
  if (*toname)
    send_invite_msg(lp->l_user->u_name, toname, lp->l_user->u_channel, "", 1);
}

/*---------------------------------------------------------------------------*/

static void kick_command(struct link *lp)
{
  struct peer *pp;

  for (pp = peers; pp; pp = pp->p_next)
    if (pp->p_retrytime > currtime) pp->p_retrytime = currtime;
}

/*---------------------------------------------------------------------------*/

static void links_command(struct link *lp)
{

  char buffer[2048];
  char srttstr[80];
  char *name;
  char *state;
  struct link *p;

  send_string(lp, "Name     State      Since  SRTT  Idle Queue Receivd Xmitted\n");
  for (p = links; p; p = p->l_next) {
    if (p->l_user) {
      state = "User";
      name = p->l_user->u_name;
    } else if (p->l_host) {
      state = "Host";
      name = p->l_host->h_name;
    } else if (p->l_fd >= 0) {
      state = "Conn pend";
      name = "";
    } else {
      state = "Closed";
      name = "";
    }
    if (p->l_srtt)
      sprintf(srttstr, "%5.4g", p->l_srtt);
    else
      strcpy(srttstr, "     ");
    sprintf(buffer,
	    "%-8.8s %-9s %s %s %5ld %5d %7d %7d\n",
	    name,
	    state,
	    localtimestring(p->l_stime),
	    srttstr,
	    currtime - p->l_mtime,
	    queuelength(p->l_sndq),
	    p->l_received,
	    p->l_xmitted);
    send_string(lp, buffer);
  }
  send_string(lp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void msg_command(struct link *lp)
{

  char buffer[2048];
  char *text;
  char *toname;
  struct user *up;

  toname = getarg(0, ONE_TOKEN, LOWER_CASE);
  text = getarg(0, REST_OF_LINE, KEEP_CASE);
  if (!*text)
    return;
  for (up = users; up; up = up->u_next)
    if (up->u_channel >= 0 && !strcmp(up->u_name, toname)) {
      send_msg_to_user(lp->l_user->u_name, toname, text, 1);
      return;
    }
  sprintf(buffer, "*** No such user: %s.\n", toname);
  send_string(lp, buffer);
}

/*---------------------------------------------------------------------------*/

static void note_command(struct link *lp)
{

  char *note;
  char buffer[2048];
  struct user *up;

  up = lp->l_user;
  note = getarg(0, REST_OF_LINE, KEEP_CASE);
  if (*note && strcmp(up->u_note, note)) {
    strchg(&up->u_note, note);
    inc_seq(&up->u_seq);
    up->u_oldchannel = up->u_channel;
    send_user_change_msg(up);
  }
  sprintf(buffer, "*** Your personal note is set to \"%s\".\n", strcmp(up->u_note, NO_NOTE) ? up->u_note : "");
  send_string(lp, buffer);
}

/*---------------------------------------------------------------------------*/

static void route_command(struct link *lp)
{

  char *toname;
  char *ttlstr;
  int ttl;

  toname = getarg(0, ONE_TOKEN, LOWER_CASE);
  if (!*toname) {
    return;
  }
  ttlstr = getarg(0, ONE_TOKEN, KEEP_CASE);
  if (*ttlstr) {
    ttl = atoi(ttlstr);
    if (ttl > 99) {
      ttl = 99;
    }
  } else {
    ttl = 16;
  }
  handle_rout_msg(lp->l_user->u_name, toname, ttl);
}

/*---------------------------------------------------------------------------*/

static void peers_command(struct link *lp)
{

  char *name;
  char *state;
  char buffer[2048];
  char nexttry[8];
  struct peer *pp;

  send_string(lp, "Address                                 State      Since NextTry Tries LinkedTo\n");
  for (pp = peers; pp; pp = pp->p_next) {
    strcpy(nexttry, "      ");
    if (!pp->p_link) {
      state = "Wait";
      name = "";
      if (pp->p_retrytime > currtime)
	strcpy(nexttry, localtimestring(pp->p_retrytime));
    } else if (pp->p_link->l_user) {
      state = "User";
      name = pp->p_link->l_user->u_name;
    } else if (pp->p_link->l_host) {
      state = "Connected";
      name = pp->p_link->l_host->h_name;
    } else if (pp->p_link->l_fd >= 0) {
      state = "Conn pend";
      name = "";
    } else {
      state = "Closed";
      name = "";
    }
    sprintf(buffer,
	    "%-39.39s %-9s %s  %s %5d %-8.8s\n",
	    *pp->p_command ? pp->p_command : pp->p_socket,
	    state,
	    localtimestring(pp->p_stime),
	    nexttry,
	    pp->p_tries,
	    name);
    send_string(lp, buffer);
  }
  send_string(lp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void name_command(struct link *lp)
{

  char *name;
  char *note;
  char buffer[2048];
  struct link *lpold;
  struct user *up;

  name = getarg(0, ONE_TOKEN, LOWER_CASE);
  if (!*name) return;
  up = userptr(name, &my);
  inc_seq(&up->u_seq);
  lpold = up->u_link;
  up->u_link = lp;
  if (up->u_channel >= 0 && lpold) close_link(lpold);
  lp->l_user = up;
  lp->l_stime = currtime;
  sprintf(buffer, "conversd @ %s $Revision: 2.79 $  Type /HELP for help.\n", my.h_name);
  send_string(lp, buffer);
  up->u_oldchannel = up->u_channel;
  up->u_channel = atoi(getarg(0, ONE_TOKEN, KEEP_CASE));
  if (up->u_channel < 0 || up->u_channel > MAX_CHANNEL) {
    sprintf(buffer, "*** Channel numbers must be in the range 0..%d.\n", MAX_CHANNEL);
    send_string(lp, buffer);
    up->u_channel = 0;
  }
  note = getarg(0, REST_OF_LINE, KEEP_CASE);
  if (!*note) note = NO_NOTE;
  if (strcmp(up->u_note, note)) {
    strchg(&up->u_note, note);
  }
  send_user_change_msg(up);
}

/*---------------------------------------------------------------------------*/

static void users_command(struct link *lp)
{

  char buffer[2048];
  struct user *up;

  send_string(lp, "User     Host     Channel   Time Personal note\n");
  for (up = users; up; up = up->u_next)
    if (up->u_channel >= 0) {
      sprintf(buffer,
	      "%-8.8s %-8.8s %7d %s %s\n",
	      up->u_name,
	      up->u_host->h_name,
	      up->u_channel,
	      localtimestring(up->u_stime),
	      strcmp(up->u_note, NO_NOTE) ? up->u_note : "");
      send_string(lp, buffer);
    }
  send_string(lp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void who_command(struct link *lp)
{

  char buffer[2048];
  int channel;
  int nextchannel;
  struct user *up;

  send_string(lp, "Channel Users\n");
  nextchannel = MAX_CHANNEL + 1;
  for (up = users; up; up = up->u_next)
    if (up->u_channel >= 0 && up->u_channel < nextchannel)
      nextchannel = up->u_channel;
  while (nextchannel <= MAX_CHANNEL) {
    channel = nextchannel;
    nextchannel = MAX_CHANNEL + 1;
    *buffer = 0;
    for (up = users; up; up = up->u_next) {
      if (up->u_channel > channel && up->u_channel < nextchannel)
	nextchannel = up->u_channel;
      else if (up->u_channel == channel) {
	if (!*buffer) sprintf(buffer, "%7d", channel);
	strcat(buffer, " ");
	strcat(buffer, up->u_name);
      }
    }
    strcat(buffer, "\n");
    send_string(lp, buffer);
  }
  send_string(lp, "***\n");
}

/*---------------------------------------------------------------------------*/

static void h_cmsg_command(struct link *lp)
{

  char *name;
  int channel;

  name = getarg(0, ONE_TOKEN, LOWER_CASE);
  channel = atoi(getarg(0, ONE_TOKEN, KEEP_CASE));
  send_msg_to_channel(name, channel, getarg(0, REST_OF_LINE, KEEP_CASE), 0);
}

/*---------------------------------------------------------------------------*/

static void h_dest_command(struct link *lp)
{

  char *name;
  char *software;
  long rtt;
  struct host *hp;
  struct quality *qp;

  name = getarg(0, ONE_TOKEN, LOWER_CASE);
  if (!*name)
    return;
  hp = hostptr(name);
  if (hp == &my || hp == lp->l_host)
    return;
  rtt = atol(getarg(0, ONE_TOKEN, KEEP_CASE));
  if (rtt < 0)
    return;
  if (rtt > MAX_RTT)
    rtt = 0;
  qp = find_quality(hp, lp);
  if (qp->q_rtt != rtt) {
    qp->q_rtt = rtt;
    next_desttime = 0;
  }
  software = getarg(0, ONE_TOKEN, KEEP_CASE);
  if (*software)
    strchg(&hp->h_software, software);
}

/*---------------------------------------------------------------------------*/

static void h_host_command(struct link *lp)
{

  char buffer[2048];
  char *name;
  struct host *hp;
  struct link *lpold;
  struct peer *pp;
  struct user *up;

  name = getarg(0, ONE_TOKEN, LOWER_CASE);
  if (!*name)
    return;
  hp = hostptr(name);
  if (hp == &my) {
    close_link(lp);
    return;
  }
  strchg(&hp->h_software, getarg(0, ONE_TOKEN, KEEP_CASE));
  strchg(&hp->h_capabilities, getarg(0, ONE_TOKEN, KEEP_CASE));
  if (!debug) {
    if (!strncmp(hp->h_software, "PP-", 3) ||
	!strncmp(hp->h_software, "pp-", 3)) {
      /* Disallow pp hosts for now,
	 but keep connection open to avoid frequent retries */
      return;
    }
  }
  lp->l_host = hp;
  lp->l_stime = currtime;
  if (strchr(hp->h_capabilities, 'p'))
    lp->l_nexttime = 0;
  else
    lp->l_nexttime = 0x7fffffff;

  for (lpold = links; lpold; lpold = lpold->l_next)
    if (lpold != lp && lpold->l_host == hp) {
      for (pp = peers; pp; pp = pp->p_next)
	if (pp->p_link == lpold) pp->p_link = lp;
      for (up = users; up; up = up->u_next)
	if (up->u_link == lpold) up->u_link = lp;
      close_link(lpold);
    }

  for (pp = peers; pp; pp = pp->p_next)
    if (pp->p_link == lp) {
      pp->p_stime = currtime;
      pp->p_tries = 0;
      pp->p_retrytime = currtime + min_waittime;
    }

  sprintf(buffer,
	  "/\377\200HOST %s %s %s\n",
	  my.h_name,
	  my.h_software,
	  my.h_capabilities);
  send_string(lp, buffer);

  for (up = users; up; up = up->u_next)
    if (up->u_channel >= 0 || up->u_seq) {
      sprintf(buffer,
	      "/\377\200USER %s %s %ld %d %d %s\n",
	      up->u_name,
	      up->u_host->h_name,
	      up->u_seq,
	      -1,
	      up->u_channel,
	      up->u_channel >= 0 ? up->u_note : "");
      send_string(lp, buffer);
    }

  next_desttime = 0;

}

/*---------------------------------------------------------------------------*/

static void h_invi_command(struct link *lp)
{

  char *channel;
  char *fromname;
  char *toname;

  fromname = getarg(0, ONE_TOKEN, LOWER_CASE);
  toname = getarg(0, ONE_TOKEN, LOWER_CASE);
  channel = getarg(0, ONE_TOKEN, KEEP_CASE);
  if (*channel)
    send_invite_msg(fromname, toname, atoi(channel), getarg(0, REST_OF_LINE, KEEP_CASE), 0);
}

/*---------------------------------------------------------------------------*/

static void h_ping_command(struct link *lp)
{
  char buffer[2048];

  sprintf(buffer, "/\377\200PONG %ld\n", lp->l_txrtt);
  send_string(lp, buffer);
}

/*---------------------------------------------------------------------------*/

static void update_srtt(struct link *lp, long rtt)
{
  if (lp->l_srtt) {
    lp->l_srtt = (7.0 * lp->l_srtt + rtt) / 8.0;
  } else {
    lp->l_srtt = rtt;
  }
}

/*---------------------------------------------------------------------------*/

static void h_pong_command(struct link *lp)
{

  long d;
  long oldsrtt;
  long srtt;
  struct host *hp;
  struct quality *qp;

  oldsrtt = lround(lp->l_srtt);
  lp->l_rxrtt = atol(getarg(0, ONE_TOKEN, KEEP_CASE));
  if (lp->l_rxrtt > 0) {
    update_srtt(lp, lp->l_rxrtt);
  }
  if (lp->l_sendtime) {
    lp->l_txrtt = currtime - lp->l_sendtime;
    if (lp->l_txrtt < 1) {
      lp->l_txrtt = 1;
    }
    update_srtt(lp, lp->l_txrtt);
    lp->l_sendtime = 0;
  }
  srtt = lround(lp->l_srtt);
  if (srtt != oldsrtt) {
    d = ping_interval(lp);
    if (lp->l_nexttime > currtime + d) {
      lp->l_nexttime = currtime + d;
    }
    for (hp = hosts; hp; hp = hp->h_next) {
      for (qp = hp->h_qualities; qp; qp = qp->q_next) {
	if (qp->q_link == lp && qp->q_rtt) {
	  qp->q_rtt = qp->q_rtt - oldsrtt + srtt;
	  if (qp->q_rtt < 1)
	    qp->q_rtt = 1;
	}
      }
    }
    qp = find_quality(lp->l_host, lp);
    qp->q_rtt = srtt;
    next_desttime = 0;
  }
}

/*---------------------------------------------------------------------------*/

static void h_rout_command(struct link *lp)
{

  char *fromname;
  char *toname;

  toname = getarg(0, ONE_TOKEN, LOWER_CASE);
  fromname = getarg(0, ONE_TOKEN, LOWER_CASE);
  handle_rout_msg(fromname, toname, atoi(getarg(0, ONE_TOKEN, KEEP_CASE)));
}

/*---------------------------------------------------------------------------*/

static void h_umsg_command(struct link *lp)
{

  char *fromname;
  char *toname;

  fromname = getarg(0, ONE_TOKEN, LOWER_CASE);
  toname = getarg(0, ONE_TOKEN, LOWER_CASE);
  send_msg_to_user(fromname, toname, getarg(0, REST_OF_LINE, KEEP_CASE), 0);
}

/*---------------------------------------------------------------------------*/

static void h_user_command(struct link *lp)
{

  char *channel;
  char *host;
  char *name;
  char *note;
  char buffer[2048];
  int newchannel;
  long seq;
  struct host *hp;
  struct user *up;

  name = getarg(0, ONE_TOKEN, LOWER_CASE);
  host = getarg(0, ONE_TOKEN, LOWER_CASE);
  seq = atol(getarg(0, ONE_TOKEN, KEEP_CASE));
  getarg(0, ONE_TOKEN, KEEP_CASE); /*** oldchannel is ignored, protocol has changed ***/
  channel = getarg(0, ONE_TOKEN, KEEP_CASE);
  if (!*channel) {
    if (debug >= 2) printf("*** Syntax error: ignored.\n");
    return;
  }
  newchannel = atoi(channel);
  hp = hostptr(host);
  up = userptr(name, hp);
  note = getarg(0, REST_OF_LINE, KEEP_CASE);
  if (!*note) note = up->u_note;

  if ((seq > up->u_seq) ||
      (seq == 0 &&
       up->u_seq == 0 &&
       (!up->u_link || lp == up->u_link) &&
       (newchannel != up->u_channel || (newchannel >= 0 && strcmp(note, up->u_note))))) {
    up->u_seq = seq;
    if (hp == &my) {
      if (debug >= 2) printf("*** Got info about my own user: rejected.\n");
      inc_seq(&up->u_seq);
      sprintf(buffer, "/\377\200USER %s %s %ld %d %d %s\n", name, host, up->u_seq, newchannel, up->u_channel, up->u_note);
      send_string(lp, buffer);
    } else {
      if (debug >= 2) printf("*** New user info: accepted.\n");
      up->u_link = lp;
      up->u_oldchannel = up->u_channel;
      up->u_channel = newchannel;
      if (strcmp(up->u_note, note)) {
	strchg(&up->u_note, note);
      }
      if ((up->u_oldchannel ^ up->u_channel) < 0) up->u_stime = currtime;
      send_user_change_msg(up);
    }
  } else {
    if (debug >= 2) printf("*** Bad sequencer or no change: ignored.\n");
  }

}

/*---------------------------------------------------------------------------*/

static void process_input(struct link *lp)
{

  struct command {
    const char *c_name;
    void (*c_fnc)(struct link *);
  };

  static const struct command login_commands[] = {

    { "\377\200host",   h_host_command },
    { "name",           name_command },

    { "bye",            bye_command },
    { "cstat",          links_command },
    { "exit",           bye_command },
    { "hosts",          hosts_command },
    { "kick",           kick_command },
    { "links",          links_command },
    { "online",         users_command },
    { "peers",          peers_command },
    { "quit",           bye_command },
    { "users",          users_command },
    { "who",            who_command },

    { 0,                0 }
  };

  static const struct command user_commands[] = {

    { "?",              help_command },

    { "bye",            bye_command },
    { "channel",        channel_command },
    { "cstat",          links_command },
    { "exit",           bye_command },
    { "help",           help_command },
    { "hosts",          hosts_command },
    { "invite",         invite_command },
    { "kick",           kick_command },
    { "links",          links_command },
    { "msg",            msg_command },
    { "note",           note_command },
    { "online",         users_command },
    { "peers",          peers_command },
    { "personal",       note_command },
    { "quit",           bye_command },
    { "route",          route_command },
    { "users",          users_command },
    { "who",            who_command },
    { "write",          msg_command },

    { 0,                0 }
  };

  static const struct command host_commands[] = {

    { "\377\200cmsg",   h_cmsg_command },
    { "\377\200dest",   h_dest_command },
    { "\377\200invi",   h_invi_command },
    { "\377\200ping",   h_ping_command },
    { "\377\200pong",   h_pong_command },
    { "\377\200rout",   h_rout_command },
    { "\377\200umsg",   h_umsg_command },
    { "\377\200user",   h_user_command },

    { 0,                0 }
  };

  char *arg;
  char buffer[2048];
  const struct command * cp;
  int arglen;

  if (debug) trace(INBOUND, lp, lp->l_ibuf);

  clear_locks();
  lp->l_locked = 1;

  if (*lp->l_ibuf == '/') {
    arglen = strlen(arg = getarg(lp->l_ibuf + 1, ONE_TOKEN, LOWER_CASE));
    if (lp->l_user)
      cp = user_commands;
    else if (lp->l_host)
      cp = host_commands;
    else
      cp = login_commands;
    for (; cp->c_name; cp++)
      if (!strncmp(cp->c_name, arg, arglen)) {
	(*cp->c_fnc)(lp);
	return;
      }
    if (lp->l_user) {
      sprintf(buffer, "*** Unknown command '/%s'.  Type /HELP for help.\n", arg);
      send_string(lp, buffer);
    }
  } else if (lp->l_user)
    send_msg_to_channel(lp->l_user->u_name, lp->l_user->u_channel, getarg(lp->l_ibuf, REST_OF_LINE, KEEP_CASE), 1);
}

/*---------------------------------------------------------------------------*/

static void read_configuration(void)
{

  char line[1024];
  char *cp;
  char *host_name;
  char *sock_name;
  FILE *fp;
  int got_host_name = 0;
  struct peer *pp;

  if (*conffile && (fp = fopen(conffile, "r"))) {
    while (fgets(line, sizeof(line), fp)) {
      if ((cp = strchr(line, '#'))) *cp = 0;
      host_name = getarg(line, ONE_TOKEN, LOWER_CASE);
      if (*host_name && !got_host_name) {
	strchg(&my.h_name, host_name);
	got_host_name = 1;
	continue;
      }
      sock_name = getarg(0, ONE_TOKEN, LOWER_CASE);
      if (*sock_name) {
	pp = (struct peer *) calloc(1, sizeof(struct peer));
	if (!pp)
	  exit(1);
	strchg(&pp->p_socket, sock_name);
	strchg(&pp->p_command, getarg(0, REST_OF_LINE, KEEP_CASE));
	pp->p_stime = currtime;
	pp->p_retrytime = currtime + min_waittime;
	pp->p_next = peers;
	peers = pp;
      }
    }
    fclose(fp);
  }
}

/*---------------------------------------------------------------------------*/

static void check_files_changed(void)
{

  static long conftime;
  static long nexttime;
  static long progtime;
  struct stat statbuf;

  if (debug || nexttime > currtime)
    return;
  nexttime = currtime + 600;

  if (*progfile && !stat(progfile, &statbuf)) {
    if (!progtime)
      progtime = statbuf.st_mtime;
    if (progtime != statbuf.st_mtime && statbuf.st_mtime + 21600 < currtime)
      exit(0);
  }

  if (*conffile && !stat(conffile, &statbuf)) {
    if (!conftime)
      conftime = statbuf.st_mtime;
    if (conftime != statbuf.st_mtime && statbuf.st_mtime + 21600 < currtime)
      exit(0);
  }

}

/*---------------------------------------------------------------------------*/

static void link_recv(struct link *lp)
{

  char buffer[2048];
  int i;
  int n;

  n = read(lp->l_fd, buffer, sizeof(buffer));
  if (n <= 0)
    close_link(lp);
  else {
    lp->l_mtime = currtime;
    lp->l_received += n;
    for (i = 0; i < n; i++)
      switch (buffer[i]) {
      case '\b':
	if (lp->l_icnt) lp->l_icnt--;
	break;
      case '\n':
      case '\r':
	if (lp->l_icnt) {
	  lp->l_ibuf[lp->l_icnt++] = '\n';
	  lp->l_ibuf[lp->l_icnt] = 0;
	  lp->l_icnt = 0;
	  process_input(lp);
	}
	break;
      default:
	if (lp->l_icnt < sizeof(lp->l_ibuf) - 5) lp->l_ibuf[lp->l_icnt++] = buffer[i];
	break;
      }
  }
}

/*---------------------------------------------------------------------------*/

static void link_send(struct link *lp)
{

  int n;
  struct iovec iov[MAXIOV];
  struct mbuf *mp;

  n = 0;
  for (mp = lp->l_sndq; mp && n < MAXIOV; mp = mp->m_next) {
    iov[n].iov_base = mp->m_data;
    iov[n].iov_len = mp->m_cnt;
    n++;
  }
  n = writev(lp->l_fd, iov, n);
  if (n <= 0)
    close_link(lp);
  else {
    lp->l_mtime = currtime;
    lp->l_xmitted += n;
    while (n > 0) {
      if (n >= lp->l_sndq->m_cnt) {
	n -= lp->l_sndq->m_cnt;
	mp = lp->l_sndq;
	lp->l_sndq = mp->m_next;
	free(mp);
      } else {
	lp->l_sndq->m_data += n;
	lp->l_sndq->m_cnt -= n;
	n = 0;
      }
    }
  }
  if (!lp->l_sndq) {
    FD_CLR(lp->l_fd, &chkwrite);
    if (lp->l_closed)
      close_link(lp);
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  struct listeners {
    const char *s_name;
    int s_fd;
  };

  static struct listeners listeners[256] = {
    {"unix:/tcp/sockets/convers", 0},
    {"*:3600", 0},
  };

  TYPE_FD_SET actread;
  TYPE_FD_SET actwrite;
  char *cp;
  char buffer[256];
  int addrlen;
  int arg;
  int chr;
  int errflag = 0;
  int i;
  int nlisteners = 0;
  int status;
  struct listeners *sp;
  struct peer *pp;
  struct sockaddr *addr;
  struct timeval timeout;

  umask(022);

  time((time_t *) &currtime);

  gethostname(buffer, sizeof(buffer));
  if ((cp = strchr(buffer, '.')))
    *cp = 0;
  strchg(&my.h_name, buffer);
  strcpy(buffer, "W-");
  if ((cp = strchr("$Revision: 2.79 $", ' ')))
    strcat(buffer, cp + 1);
  if ((cp = strchr(buffer, ' ')))
    *cp = 0;
  strchg(&my.h_software, buffer);
  strchg(&my.h_capabilities, "dpu");

  while ((chr = getopt(argc, argv, "a:c:dh:p:")) != EOF)
    switch (chr) {
    case 'a':
      listeners[nlisteners].s_name = optarg;
      nlisteners++;
      listeners[nlisteners].s_name = 0;
      break;
    case 'c':
      conffile = optarg;
      break;
    case 'd':
      debug++;
      min_waittime = 1;
      break;
    case 'h':
      strchg(&my.h_name, getarg(optarg, ONE_TOKEN, LOWER_CASE));
      break;
    case 'p':
      pp = (struct peer *) calloc(1, sizeof(struct peer));
      if (!pp)
	exit(1);
      strchg(&pp->p_socket, getarg(optarg, ONE_TOKEN, LOWER_CASE));
      strchg(&pp->p_command, getarg(0, REST_OF_LINE, KEEP_CASE));
      pp->p_stime = currtime;
      pp->p_retrytime = currtime + min_waittime;
      pp->p_next = peers;
      peers = pp;
      break;
    case '?':
      errflag = 1;
      break;
    }

  if (errflag || optind < argc) {
    fprintf(stderr,
	    "Usage: conversd"
	    " [-d]"
	    " [-c conffile]"
	    " [-a address]"
	    " [-h hostname]"
	    " [-p socket_address [connect_command]]"
	    "\n");
    exit(1);
  }

  close(0);
  for (i = debug ? 3 : 1; i < FD_SETSIZE; i++)
    close(i);

  chdir("/");
  setsid();
  signal(SIGPIPE, SIG_IGN);
  if (!getenv("TZ"))
    putenv("TZ=MEZ-1MESZ");

  read_configuration();

  for (sp = listeners; sp->s_name; sp++)
    if ((addr = build_sockaddr(sp->s_name, &addrlen)) &&
	(sp->s_fd = socket(addr->sa_family, SOCK_STREAM, 0)) >= 0) {
      switch (addr->sa_family) {
      case AF_UNIX:
	remove(addr->sa_data);
	break;
      case AF_INET:
	arg = 1;
	setsockopt(sp->s_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &arg, sizeof(arg));
	break;
      }
      if (bind(sp->s_fd, addr, addrlen) || listen(sp->s_fd, SOMAXCONN)) {
	close(sp->s_fd);
      } else {
	readfnc[sp->s_fd] = (void (*)(void *)) accept_connect_request;
	readarg[sp->s_fd] = &sp->s_fd;
	FD_SET(sp->s_fd, &chkread);
	if (maxfd < sp->s_fd) maxfd = sp->s_fd;
      }
    }

  for (; ; ) {
#ifdef ibm032
    wait3(&status, WNOHANG, 0);
#else
    waitpid(-1, &status, WNOHANG);
#endif
    free_resources();
    check_files_changed();
    connect_peers();
    send_pings();
    send_dests();
    actread = chkread;
    actwrite = chkwrite;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    i = select(maxfd + 1, SEL_ARG(&actread), SEL_ARG(&actwrite), 0, &timeout);
    time((time_t *) &currtime);
    if (i > 0)
      for (i = maxfd; i >= 0; i--) {
	if (readfnc [i] && FD_ISSET(i, &actread )) (*readfnc [i])(readarg [i]);
	if (writefnc[i] && FD_ISSET(i, &actwrite)) (*writefnc[i])(writearg[i]);
      }
  }
}
