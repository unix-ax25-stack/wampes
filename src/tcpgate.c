#include <sys/types.h>

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "hpux.h"

extern struct sockaddr *build_sockaddr();
extern void free();

/*---------------------------------------------------------------------------*/

static void tcp_send(tcb)
struct tcb *tcb;
{

  int  cnt, fd;
  struct mbuf *bp;

  fd = (int) tcb->user;
  if ((cnt = space_tcp(tcb)) <= 0) {
    clrmask(filemask, fd);
    return;
  }
  if (!(bp = alloc_mbuf(cnt))) return;
  cnt = doread(fd, bp->data, (unsigned) cnt);
  if (cnt <= 0) {
    free_p(bp);
    clrmask(filemask, fd);
    close_tcp(tcb);
    return;
  }
  bp->cnt = cnt;
  send_tcp(tcb, bp);
}

/*---------------------------------------------------------------------------*/

static void tcp_receive(tcb, cnt)
struct tcb *tcb;
int16 cnt;
{

  char  buffer[1024];
  int  fd;
  struct mbuf *bp;

  fd = (int) tcb->user;
  recv_tcp(tcb, &bp, 0);
  while ((cnt = pullup(&bp, buffer, sizeof(buffer))) > 0)
    if (dowrite(fd, buffer, (unsigned) cnt) < 0) close_tcp(tcb);
}

/*---------------------------------------------------------------------------*/

static void tcp_ready(tcb, cnt)
struct tcb *tcb;
int16 cnt;
{
  int  fd;

  fd = (int) tcb->user;
  setmask(filemask, fd);
}

/*---------------------------------------------------------------------------*/

static void tcp_state(tcb, old, new)
struct tcb *tcb;
char  old, new;
{

  int  addrlen, fd;
  struct sockaddr *addr;

  switch (new) {
#ifdef  QUICKSTART
  case SYN_RECEIVED:
#else
  case ESTABLISHED:
#endif
    log(tcb, "open %s", tcp_port(tcb->conn.local.port));
    if (!(addr = build_sockaddr(tcb->user, &addrlen)) ||
	(fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) {
      close_tcp(tcb);
      return;
    }
    tcb->user = (char *) fd;
    if (connect(fd, addr, addrlen)) {
      close_tcp(tcb);
      return;
    }
    setmask(filemask, fd);
    readfnc[fd] = tcp_send;
    readarg[fd] = (char *) tcb;
    return;
  case CLOSE_WAIT:
    close_tcp(tcb);
    return;
  case CLOSED:
    if (old == LISTEN) {
      free(tcb->user);
      del_tcp(tcb);
      return;
    }
    log(tcb, "close %s", tcp_port(tcb->conn.local.port));
    fd = (int) tcb->user;
    if (fd > 0 && fd < _NFILE) {
      clrmask(filemask, fd);
      readfnc[fd] = (void (*)()) 0;
      readarg[fd] = (char *) 0;
      close(fd);
    }
    del_tcp(tcb);
    return;
  }
}

/*---------------------------------------------------------------------------*/

tcpgate1(argc, argv)
int  argc;
char  *argv[];
{

  char  *socketname;
  char  buf[80];
  struct socket lsocket;

  lsocket.address = ip_addr;
  lsocket.port = tcp_portnum(argv[1]);
  if (argc < 3)
    sprintf(socketname = buf, "loopback:%d", lsocket.port);
  else
    socketname = argv[2];
  socketname = strcpy(malloc((unsigned) (strlen(socketname) + 1)), socketname);
  open_tcp(&lsocket, NULLSOCK, TCP_SERVER, 0, tcp_receive, tcp_ready, tcp_state, 0, socketname);
  return 0;
}

