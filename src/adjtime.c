/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/adjtime.c,v 1.7 1996/01/04 19:11:38 deyke Exp $ */

#include "configure.h"

#if defined __hpux && !HAS_ADJTIME

#include <sys/types.h>

#include <signal.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>

#define KEY     659847

typedef union {
  struct msgbuf msgp;
  struct {
    long mtype;
    int code;
    struct timeval tv;
  } msgb;
} MsgBuf;

#define MSGSIZE (sizeof(int) + sizeof(struct timeval))

/*
 * mtype values
 */

#define CLIENT  1L
#define SERVER  2L

/*
 * code values
 */

#define DELTA1  0
#define DELTA2  1

int adjtime(
     const struct timeval *delta,
     struct timeval *olddelta)
{
  int mqid;
  MsgBuf msg;
  MsgBuf *msgp = &msg;
  long mask;
/*
 * get the key to the adjtime message queue
 * (note that we must get it every time because the queue might have been
 *  removed and recreated)
 */
  if ((mqid = msgget(KEY, 0)) == -1)
    return -1;

  msgp->msgb.mtype = CLIENT;
  msgp->msgb.tv = *delta;

  if (olddelta)
    msgp->msgb.code = DELTA2;
  else
    msgp->msgb.code = DELTA1;
/*
 * block signals while we send and receive the message
 * (hopefully this will never hang)
 */
  mask = sigblock(~0);

  if (msgsnd(mqid, &msgp->msgp, MSGSIZE, 0) == -1) {
    sigsetmask(mask);
    return -1;
  }

  if (olddelta) {
    if (msgrcv(mqid, &msgp->msgp, MSGSIZE, SERVER, 0) == -1) {
      sigsetmask(mask);
      return -1;
    }

    *olddelta = msgp->msgb.tv;
  }

  sigsetmask(mask);
  return 0;
}

#else

int adjtime_dummy;      /* Prevent "Empty source file" message */

#endif
