#include <memory.h>
#include <string.h>

#include "global.h"
#include "netuser.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "axproto.h"
#include "netrom.h"
#include "tcp.h"
#include "transport.h"

extern int16 lport;
extern void free();

/*---------------------------------------------------------------------------*/

static int  convert_eol(bpp, mode, last_chr)
struct mbuf **bpp;
int  mode, *last_chr;
{

  char  buf[10240], *p;
  int  chr, cnt;
  struct mbuf *bp, *bp1;

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
    bp1 = bp;
    bp = bp->next;
    free((char *) bp1);
  }
  cnt = p - buf;
  if (bp = alloc_mbuf(cnt)) memcpy(bp->data, buf, bp->cnt = cnt);
  *bpp = bp;
  return cnt;
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall_ax25(cp, cnt)
struct axcb *cp;
int16 cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->r_upcall) (*tp->r_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall_netrom(cp, cnt)
struct circuit *cp;
int16 cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->r_upcall) (*tp->r_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall_tcp(cp, cnt)
struct tcb *cp;
int16 cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->r_upcall) (*tp->r_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall_ax25(cp, cnt)
struct axcb *cp;
int16 cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->t_upcall) (*tp->t_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall_netrom(cp, cnt)
struct circuit *cp;
int16 cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->t_upcall) (*tp->t_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall_tcp(cp, cnt)
struct tcb *cp;
int16 cnt;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->t_upcall) (*tp->t_upcall)(tp, cnt);
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall_ax25(cp, oldstate, newstate)
struct axcb *cp;
int  oldstate, newstate;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->s_upcall && newstate == DISCONNECTED) (*tp->s_upcall)(tp);
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall_netrom(cp, oldstate, newstate)
struct circuit *cp;
int  oldstate, newstate;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  if (tp->s_upcall && newstate == DISCONNECTED) (*tp->s_upcall)(tp);
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall_tcp(cp, oldstate, newstate)
struct tcb *cp;
char  oldstate, newstate;
{
  struct transport_cb *tp = (struct transport_cb *) cp->user;
  switch (newstate) {
  case CLOSE_WAIT:
    close_tcp(cp);
    break;
  case CLOSED:
    if (tp->s_upcall) (*tp->s_upcall)(tp);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static struct axcb *transport_open_ax25(address, tp)
char  *address;
struct transport_cb *tp;
{

  char  *pathptr;
  char  *strptr;
  char  path[10*AXALEN];
  char  tmp[1024];
  int  recv_ax(), send_ax(), space_ax(), close_ax(), del_ax();

  pathptr = path;
  strptr = strtok(strcpy(tmp, address), " \t\r\n");
  while (strptr) {
    if (strncmp("via", strptr, strlen(strptr))) {
      if (pathptr > path + 10 * AXALEN - 1) return 0;
      if (setcall(axptr(pathptr), strptr)) return 0;
      if (pathptr == path) {
	pathptr += AXALEN;
	memcpy(pathptr, (char *) & mycall, AXALEN);
	pathptr[6] &= ~E;
      }
      pathptr += AXALEN;
    }
    strptr = strtok(NULLCHAR, " \t\r\n");
  }
  if (pathptr < path + 2 * AXALEN) return 0;
  pathptr[-1] |= E;
  tp->recv = recv_ax;
  tp->send = send_ax;
  tp->send_space = space_ax;
  tp->close = close_ax;
  tp->del = del_ax;
  return open_ax(path, AX25_ACTIVE, transport_recv_upcall_ax25, transport_send_upcall_ax25, transport_state_upcall_ax25, (char *) tp);
}

/*---------------------------------------------------------------------------*/

static struct circuit *transport_open_netrom(address, tp)
char  *address;
struct transport_cb *tp;
{

  int  recv_nr(), send_nr(), space_nr(), close_nr(), del_nr();
  struct ax25_addr node;

  if (setcall(&node, address)) return 0;
  tp->recv = recv_nr;
  tp->send = send_nr;
  tp->send_space = space_nr;
  tp->close = close_nr;
  tp->del = del_nr;
  return open_nr(&node, &mycall, 0, transport_recv_upcall_netrom, transport_send_upcall_netrom, transport_state_upcall_netrom, (char *) tp);
}

/*---------------------------------------------------------------------------*/

static struct tcb *transport_open_tcp(address, tp)
char  *address;
struct transport_cb *tp;
{

  char  *strptr, tmp[1024];
  int  recv_tcp(), send_tcp(), space_tcp(), close_tcp(), del_tcp();
  struct socket fsocket, lsocket;

  if (!(strptr = strtok(strcpy(tmp, address), " \t\r\n"))) return 0;
  if (!(fsocket.address = resolve(strptr))) return 0;
  if (!(strptr = strtok((char *) 0, " \t\r\n"))) return 0;
  if (!(fsocket.port = tcp_portnum(strptr))) return 0;
  lsocket.address = ip_addr;
  lsocket.port = lport++;
  tp->recv = recv_tcp;
  tp->send = send_tcp;
  tp->send_space = space_tcp;
  tp->close = close_tcp;
  tp->del = del_tcp;
  return open_tcp(&lsocket, &fsocket, TCP_ACTIVE, 0, transport_recv_upcall_tcp, transport_send_upcall_tcp, transport_state_upcall_tcp, 0, (char *) tp);
}

/*---------------------------------------------------------------------------*/

struct transport_cb *transport_open(protocol, address, r_upcall, t_upcall, s_upcall, user)
char  *protocol, *address;
void (*r_upcall)(), (*t_upcall)(), (*s_upcall)();
char  *user;
{
  struct transport_cb *tp;

  tp = (struct transport_cb *) calloc(1, sizeof(struct transport_cb ));
  tp->r_upcall = r_upcall;
  tp->t_upcall = t_upcall;
  tp->s_upcall = s_upcall;
  tp->user = user;
  tp->timer.func = (void (*)()) transport_close;
  tp->timer.arg = (char *) tp;
  net_error = INVALID;
  if (!strcmp(protocol, "ax25"))
    tp->cp = (char *) transport_open_ax25(address, tp);
  else if (!strcmp(protocol, "netrom"))
    tp->cp = (char *) transport_open_netrom(address, tp);
  else if (!strcmp(protocol, "tcp"))
    tp->cp = (char *) transport_open_tcp(address, tp);
  else
    net_error = NOPROTO;
  if (tp->cp) return tp;
  free((char *) tp);
  return 0;
}

/*---------------------------------------------------------------------------*/

int  transport_recv(tp, bpp, cnt)
struct transport_cb *tp;
struct mbuf **bpp;
int16 cnt;
{
  int  result;

  if (tp->timer.start) start_timer(&tp->timer);
  result = (*tp->recv)(tp->cp, bpp, cnt);
  if (result < 1 || tp->recv_mode == EOL_NONE) return result;
  return convert_eol(bpp, tp->recv_mode, &tp->recv_char);
}

/*---------------------------------------------------------------------------*/

int  transport_send(tp, bp)
struct transport_cb *tp;
struct mbuf *bp;
{
  if (tp->timer.start) start_timer(&tp->timer);
  if (tp->send_mode != EOL_NONE)
    convert_eol(&bp, tp->send_mode, &tp->send_char);
  return (*tp->send)(tp->cp, bp);
}

/*---------------------------------------------------------------------------*/

int  transport_send_space(tp)
struct transport_cb *tp;
{
  return (*tp->send_space)(tp->cp);
}

/*---------------------------------------------------------------------------*/

void transport_set_timeout(tp, timeout)
struct transport_cb *tp;
int  timeout;
{
  set_timer(&tp->timer, timeout * 1000l);
  start_timer(&tp->timer);
}

/*---------------------------------------------------------------------------*/

int  transport_close(tp)
struct transport_cb *tp;
{
  return (*tp->close)(tp->cp);
}

/*---------------------------------------------------------------------------*/

int  transport_del(tp)
struct transport_cb *tp;
{
  (*tp->del)(tp->cp);
  stop_timer(&tp->timer);
  free((char *) tp);
  return 0;
}

