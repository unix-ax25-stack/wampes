/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/axclient.c,v 1.4 1990/08/23 17:32:34 deyke Exp $ */

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

static axclient_parse(buf, n)
char  *buf;
int16 n;
{
  if (!(current && current->type == AX25TNC && current->cb.ax25)) return;
  if (n >= 1 && buf[n-1] == '\n') n--;
  if (!n) return;
  send_ax(current->cb.ax25, qdata(buf, n));
  if (current->record) {
    if (buf[n-1] == '\r') buf[n-1] = '\n';
    fwrite(buf, 1, n, current->record);
  }
}

/*---------------------------------------------------------------------------*/

axclient_send_upcall(cp, cnt)
struct axcb *cp;
int  cnt;
{

  char  *p;
  int  chr;
  struct mbuf *bp;
  struct session *s;

  if (!(s = (struct session *) cp->user) || !s->upload || cnt <= 0) return;
  if (!(bp = alloc_mbuf(cnt))) return;
  p = bp->data;
  while (cnt) {
    if ((chr = getc(s->upload)) == EOF) break;
    if (chr == '\n') chr = '\r';
    *p++ = chr;
    cnt--;
  }
  if (bp->cnt = p - bp->data)
    send_ax(cp, bp);
  else
    free_p(bp);
  if (cnt) {
    fclose(s->upload);
    s->upload = 0;
    free(s->ufile);
    s->ufile = 0;
  }
}

/*---------------------------------------------------------------------------*/

axclient_recv_upcall(cp)
struct axcb *cp;
{

  char  c;
  struct mbuf *bp;

  if (!(mode == CONV_MODE && current && current->type == AX25TNC && current->cb.ax25 == cp)) return;
  recv_ax(cp, &bp, 0);
  while (pullup(&bp, &c, 1)) {
    if (c == '\r') c = '\n';
    putchar(c);
    if (current->record) putc(c, current->record);
  }
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

int  doconnect(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  char  *ap;
  char  path[10*AXALEN];
  struct session *s;

  argc--;
  argv++;
  for (ap = path; argc > 0; argc--, argv++) {
    if (!strncmp("via", *argv, strlen(*argv))) continue;
    if (ap > path + sizeof(path) - 1) {
      printf("Too many digipeaters (max 8)\n");
      return 1;
    }
    if (setcall(axptr(ap), *argv)) {
      printf("Invalid call \"%s\"\n", *argv);
      return 1;
    }
    if (ap == path) {
      ap += AXALEN;
      addrcp(axptr(ap), &mycall);
    }
    ap += AXALEN;
  }
  if (ap < path + 2 * AXALEN) {
    printf("Missing call\n");
    return 1;
  }
  ap[-1] |= E;
  if (!(s = newsession())) {
    printf("Too many sessions\n");
    return 1;
  }
  current = s;
  s->type = AX25TNC;
  s->name = NULLCHAR;
  s->cb.ax25 = NULLAXCB;
  s->parse = axclient_parse;
  if (!(s->cb.ax25 = open_ax(path, AX25_ACTIVE, axclient_recv_upcall, axclient_send_upcall, axclient_state_upcall, (char *) s))) {
    freesession(s);
    switch (Net_error) {
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
    case NO_MEM:
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
  go(argc, argv, p);
  return 0;
}

