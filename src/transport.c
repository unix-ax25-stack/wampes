/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/transport.c,v 1.13 1993/05/17 13:45:23 deyke Exp $ */

#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "netuser.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "tcp.h"
#include "session.h"
#include "transport.h"

static const char *delim = " \t\r\n";

static int convert_eol(struct mbuf **bpp, int mode, int *last_chr);
static void transport_recv_upcall_ax25(struct ax25_cb *cp, int cnt);
static void transport_recv_upcall_netrom(struct circuit *cp, int cnt);
static void transport_recv_upcall_tcp(struct tcb *cp, int cnt);
static void transport_send_upcall_ax25(struct ax25_cb *cp, int cnt);
static void transport_send_upcall_netrom(struct circuit *cp, int cnt);
static void transport_send_upcall_tcp(struct tcb *cp, int cnt);
static void transport_state_upcall_ax25(struct ax25_cb *cp, int oldstate, int newstate);
static void transport_state_upcall_netrom(struct circuit *cp, int oldstate, int newstate);
static void transport_state_upcall_tcp(struct tcb *cp, int oldstate, int newstate);
static struct ax25_cb *transport_open_ax25(const char *address, struct transport_cb *tp);
static struct circuit *transport_open_netrom(const char *address, struct transport_cb *tp);
static struct tcb *transport_open_tcp(const char *address, struct transport_cb *tp);

/*---------------------------------------------------------------------------*/

static int convert_eol(struct mbuf **bpp, int mode, int *last_chr)
{

  char buf[10240], *p;
  int chr, cnt;
  struct mbuf *bp;

  p = buf;
  bp = *bpp;
  while (bp) {
    for (; bp->cnt; bp->cnt--) {
      switch (chr = *bp->data++) {
      case '\0':
	break;
      case '\n':
	if (*last_chr == '\r')
	  break;
      case '\r':
	switch (mode) {
	case EOL_CR:
	  *p++ = '\r';
	  break;
	case EOL_LF:
	  *p++ = '\n';
	  break;
	case EOL_CRLF:
	  *p++ = '\r';
	  *p++ = '\n';
	  break;
	}
	break;
      default:
	*p++ = chr;
	break;
      }
      *last_chr = chr;
    }
    bp = free_mbuf(bp);
  }
  cnt = p - buf;
  *bpp = qdata(buf, cnt);
  return cnt;
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall_ax25(struct ax25_cb *cp, int cnt)
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->r_upcall) (*tp->r_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall_netrom(struct circuit *cp, int cnt)
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->r_upcall) (*tp->r_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall_tcp(struct tcb *cp, int cnt)
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->r_upcall) (*tp->r_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall_ax25(struct ax25_cb *cp, int cnt)
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->t_upcall) (*tp->t_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall_netrom(struct circuit *cp, int cnt)
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->t_upcall) (*tp->t_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall_tcp(struct tcb *cp, int cnt)
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->t_upcall) (*tp->t_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall_ax25(struct ax25_cb *cp, int oldstate, int newstate)
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->s_upcall && newstate == LAPB_DISCONNECTED) (*tp->s_upcall)(tp);
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall_netrom(struct circuit *cp, int oldstate, int newstate)
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->s_upcall && newstate == NR4STDISC) (*tp->s_upcall)(tp);
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall_tcp(struct tcb *cp, int oldstate, int newstate)
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  switch (newstate) {
  case TCP_CLOSE_WAIT:
    close_tcp(cp);
    break;
  case TCP_CLOSED:
    if (tp->s_upcall) (*tp->s_upcall)(tp);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static struct ax25_cb *transport_open_ax25(const char *address, struct transport_cb *tp)
{

  char *argv[128];
  char *s;
  char tmp[1024];
  int argc;
  struct ax25 hdr;

  argc = 0;
  for (s = strtok(strcpy(tmp, address), delim); s; s = strtok(NULLCHAR, delim))
    argv[argc++] = s;
  if (ax25args_to_hdr(argc, argv, &hdr)) return 0;
  return open_ax25(&hdr, AX_ACTIVE, transport_recv_upcall_ax25, transport_send_upcall_ax25, transport_state_upcall_ax25, (char *) tp);
}

/*---------------------------------------------------------------------------*/

static struct circuit *transport_open_netrom(const char *address, struct transport_cb *tp)
{

  char *ascii_node, *ascii_user;
  char node[AXALEN], user[AXALEN];
  char tmp[1024];

  strcpy(tmp, address);
  if (!(ascii_node = strtok(tmp, delim)) || setcall(node, ascii_node))
    return 0;
  if (!(ascii_user = strtok(NULLCHAR, delim)) || setcall(user, ascii_user))
    addrcp(user, Mycall);
  return open_nr(node, user, 0, transport_recv_upcall_netrom, transport_send_upcall_netrom, transport_state_upcall_netrom, (char *) tp);
}

/*---------------------------------------------------------------------------*/

static struct tcb *transport_open_tcp(const char *address, struct transport_cb *tp)
{

  char *host, *port, tmp[1024];
  struct socket fsocket, lsocket;

  if (!(host = strtok(strcpy(tmp, address), delim))) return 0;
  if (!(port = strtok(NULLCHAR, delim))) return 0;
  if (!(fsocket.address = resolve(host))) return 0;
  if (!(fsocket.port = tcp_port_number(port))) return 0;
  lsocket.address = INADDR_ANY;
  lsocket.port = Lport++;
  return open_tcp(&lsocket, &fsocket, TCP_ACTIVE, 0, transport_recv_upcall_tcp, transport_send_upcall_tcp, transport_state_upcall_tcp, 0, (int) tp);
}

/*---------------------------------------------------------------------------*/

struct transport_cb *transport_open(const char *protocol, const char *address, void (*r_upcall)(struct transport_cb *tp, int cnt), void (*t_upcall)(struct transport_cb *tp, int cnt), void (*s_upcall)(struct transport_cb *tp), void *user)
{
  struct transport_cb *tp;

  tp = calloc(1, sizeof(*tp));
  tp->r_upcall = r_upcall;
  tp->t_upcall = t_upcall;
  tp->s_upcall = s_upcall;
  tp->user = user;
  tp->timer.func = (void (*)()) transport_close;
  tp->timer.arg = tp;
  Net_error = INVALID;
  if (!strcmp(protocol, "ax25")) {
    tp->type = TP_AX25;
    tp->cp = transport_open_ax25(address, tp);
  } else if (!strcmp(protocol, "netrom")) {
    tp->type = TP_NETROM;
    tp->cp = transport_open_netrom(address, tp);
  } else if (!strcmp(protocol, "tcp")) {
    tp->type = TP_TCP;
    tp->cp = transport_open_tcp(address, tp);
  } else
    Net_error = NOPROTO;
  if (tp->cp) return tp;
  free(tp);
  return 0;
}

/*---------------------------------------------------------------------------*/

int transport_recv(struct transport_cb *tp, struct mbuf **bpp, int cnt)
{
  int result;

  if dur_timer(&tp->timer) start_timer(&tp->timer);
  switch (tp->type) {
  case TP_AX25:
    *bpp = recv_ax25(tp->cp, cnt);
    result = len_p(*bpp);
    break;
  case TP_NETROM:
    result = recv_nr(tp->cp, bpp, cnt);
    break;
  case TP_TCP:
    result = recv_tcp(tp->cp, bpp, cnt);
    break;
  default:
    result = -1;
  }
  if (result < 1 || tp->recv_mode == EOL_NONE) return result;
  return convert_eol(bpp, tp->recv_mode, &tp->recv_char);
}

/*---------------------------------------------------------------------------*/

int transport_send(struct transport_cb *tp, struct mbuf *bp)
{
  if dur_timer(&tp->timer) start_timer(&tp->timer);
  if (tp->send_mode != EOL_NONE)
    convert_eol(&bp, tp->send_mode, &tp->send_char);
  switch (tp->type) {
  case TP_AX25:
    return send_ax25(tp->cp, bp, PID_NO_L3);
  case TP_NETROM:
    return send_nr(tp->cp, bp);
  case TP_TCP:
    return send_tcp(tp->cp, bp);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int transport_send_space(struct transport_cb *tp)
{
  switch (tp->type) {
  case TP_AX25:
    return space_ax25(tp->cp);
  case TP_NETROM:
    return space_nr(tp->cp);
  case TP_TCP:
    return space_tcp(tp->cp);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

void transport_set_timeout(struct transport_cb *tp, int timeout)
{
  set_timer(&tp->timer, timeout * 1000L);
  start_timer(&tp->timer);
}

/*---------------------------------------------------------------------------*/

int transport_close(struct transport_cb *tp)
{
  switch (tp->type) {
  case TP_AX25:
    return disc_ax25(tp->cp);
  case TP_NETROM:
    return close_nr(tp->cp);
  case TP_TCP:
    return close_tcp(tp->cp);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int transport_del(struct transport_cb *tp)
{
  switch (tp->type) {
  case TP_AX25:
    del_ax25(tp->cp);
    break;
  case TP_NETROM:
    del_nr(tp->cp);
    break;
  case TP_TCP:
    del_tcp(tp->cp);
    break;
  }
  stop_timer(&tp->timer);
  free(tp);
  return 0;
}

