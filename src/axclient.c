#include <memory.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "config.h"
#include "netuser.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "axproto.h"
#include "session.h"

/*---------------------------------------------------------------------------*/

static int  axclient_parse(buf, n)
char  *buf;
int16 n;
{
  if (!(current && current->type == AX25TNC && current->cb.ax25)) return (-1);
  if (buf[n-1] == '\n') n--;
  if (n <= 0) return (-1);
  return send_ax(current->cb.ax25, qdata(buf, n));
}

/*---------------------------------------------------------------------------*/

void axclient_recv_upcall(cp, cnt)
struct axcb *cp;
int16 cnt;
{

  char  c;
  struct mbuf *bp;

  if (!(mode == CONV_MODE && current && current->type == AX25TNC && current->cb.ax25 == cp)) return;
  recv_ax(cp, &bp, 0);
  while (pullup(&bp, &c, 1) == 1) putchar((c == '\r') ? '\n' : c);
  fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static void axclient_state_upcall(cp, oldstate, newstate)
struct axcb *cp;
int  oldstate, newstate;
{
  int  notify;

  notify = (current && current->type == AX25TNC && current == (struct session *) cp->user);
  if (newstate != DISCONNECTED) {
    if (notify) printf("%s\n", ax25states[newstate]);
  } else {
    if (notify) printf("%s (%s)\n", ax25states[newstate], ax25reasons[cp->reason]);
    if (cp->user) freesession((struct session *) cp->user);
    del_ax(cp);
    if (notify) cmdmode();
  }
}

/*---------------------------------------------------------------------------*/

print_ax25_session(s)
struct session *s;
{
  printf("%c%-3d%8lx AX25    %4d  %-13s%-s",
	 (current == s) ? '*' : ' ',
	 (int) (s - sessions),
	 (long) s->cb.ax25,
	 s->cb.ax25->rcvcnt,
	 ax25states[s->cb.ax25->state],
	 pathtostr(s->cb.ax25));
}

/*---------------------------------------------------------------------------*/

int  doconnect(argc, argv)
int  argc;
char  *argv[];
{

  char  *p;
  char  path[10*AXALEN];
  struct session *s;

  argc--;
  argv++;
  for (p = path; argc > 0; argc--, argv++) {
    if (!strncmp("via", *argv, strlen(*argv))) continue;
    if (p > path + sizeof(path) - 1) {
      printf("Too many digipeaters (max 8)\n");
      return 1;
    }
    if (setcall(axptr(p), *argv)) {
      printf("Invalid call \"%s\"\n", *argv);
      return 1;
    }
    if (p == path) {
      p += AXALEN;
      memcpy(p, (char *) &mycall, AXALEN);
      p[6] &= ~E;
    }
    p += AXALEN;
  }
  if (p < path + 2 * AXALEN) {
    printf("Missing call\n");
    return 1;
  }
  p[-1] |= E;
  if (!(s = newsession())) {
    printf("Too many sessions\n");
    return 1;
  }
  current = s;
  s->type = AX25TNC;
  s->name = NULLCHAR;
  s->cb.ax25 = NULLAXCB;
  s->parse = axclient_parse;
  if (!(s->cb.ax25 = open_ax(path, AX25_ACTIVE, axclient_recv_upcall, NULLVFP, axclient_state_upcall, (char *) s))) {
    freesession(s);
    switch (net_error) {
    case NONE:
      printf("No error\n");
      break;
    case CON_EXISTS:
      printf("Connection already exists\n");
      break;
    case NO_CONN:
      printf("Connection does not exist\n");
      break;
    case CON_CLOS:
      printf("Connection closing\n");
      break;
    case NO_SPACE:
      printf("No memory\n");
      break;
    case WOULDBLK:
      printf("Would block\n");
      break;
    case NOPROTO:
      printf("Protocol or mode not supported\n");
      break;
    case INVALID:
      printf("Invalid arguments\n");
      break;
    }
    return 1;
  }
  go();
  return 0;
}

