/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/axclient.c,v 1.8 1991/03/28 19:39:13 deyke Exp $ */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "config.h"
#include "netuser.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include "session.h"

/*---------------------------------------------------------------------------*/

static int axclient_parse(buf, n)
char *buf;
int n;
{
  if (!(Current && Current->type == AX25TNC && Current->cb.ax25)) return;
  if (n >= 1 && buf[n-1] == '\n') n--;
  if (!n) return;
  send_ax(Current->cb.ax25, qdata(buf, n));
  if (Current->record) {
    if (buf[n-1] == '\r') buf[n-1] = '\n';
    fwrite(buf, 1, n, Current->record);
  }
}

/*---------------------------------------------------------------------------*/

void axclient_send_upcall(cp, cnt)
struct ax25_cb *cp;
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

void axclient_recv_upcall(cp, cnt)
struct ax25_cb *cp;
int  cnt;
{

  int  c;
  struct mbuf *bp;

  if (!(Mode == CONV_MODE && Current && Current->type == AX25TNC && Current->cb.ax25 == cp)) return;
  recv_ax(cp, &bp, 0);
  while ((c = PULLCHAR(&bp)) != -1) {
    if (c == '\r') c = '\n';
    putchar(c);
    if (Current->record) putc(c, Current->record);
  }
  fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static void axclient_state_upcall(cp, oldstate, newstate)
struct ax25_cb *cp;
int  oldstate, newstate;
{
  int  notify;

  notify = (Current && Current->type == AX25TNC && Current == (struct session *) cp->user);
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
    if (setcall(ap, *argv)) {
      printf("Invalid call \"%s\"\n", *argv);
      return 1;
    }
    if (ap == path) {
      ap += AXALEN;
      addrcp(ap, Mycall);
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
  Current = s;
  s->type = AX25TNC;
  s->name = NULLCHAR;
  s->cb.ax25 = NULLAXCB;
  s->parse = axclient_parse;
  if (!(s->cb.ax25 = open_ax(path, AX_ACTIVE, axclient_recv_upcall, axclient_send_upcall, axclient_state_upcall, (char *) s))) {
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

