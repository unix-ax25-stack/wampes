/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mail_smtp.c,v 1.8 1991/06/04 11:34:25 deyke Exp $ */

/* SMTP Mail Delivery Agent */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "global.h"
#include "mbuf.h"
#include "transport.h"
#include "mail.h"

struct mesg {
  int  state;
#define SMTP_OPEN_STATE 0
#define SMTP_HELO_STATE 1
#define SMTP_MAIL_STATE 2
#define SMTP_RCPT_STATE 3
#define SMTP_DATA_STATE 4
#define SMTP_SEND_STATE 5
#define SMTP_UNLK_STATE 6
#define SMTP_QUIT_STATE 7
  char  buf[1024];
  int  cnt;
  FILE * fp;
  struct mailsys *sp;
  struct transport_cb *tp;
};

static void mail_smtp_transaction __ARGS((struct mesg *mp));
static void mail_smtp_recv_upcall __ARGS((struct transport_cb *tp, int cnt));
static void mail_smtp_send_upcall __ARGS((struct transport_cb *tp, int cnt));
static void mail_smtp_state_upcall __ARGS((struct transport_cb *tp));

/*---------------------------------------------------------------------------*/

static void mail_smtp_transaction(mp)
struct mesg *mp;
{

  char  tmp[1024];
  struct mailjob *jp;

  jp = mp->sp->jobs;
  if (mp->state == SMTP_OPEN_STATE && (*mp->buf < '0' || *mp->buf > '9')) return;
  if (*mp->buf == (mp->state == SMTP_DATA_STATE ? '3' : '2'))
    switch (mp->state) {
    case SMTP_OPEN_STATE:
      mp->state = SMTP_HELO_STATE;
      sprintf(tmp, "helo %s\n", Hostname);
      transport_send(mp->tp, qdata(tmp, strlen(tmp)));
      break;
    case SMTP_HELO_STATE:
nextjob:
      mp->state = SMTP_MAIL_STATE;
      sprintf(tmp,
	      "mail from:<%s@%s>\n",
	      get_user_from_path(jp->from),
	      get_host_from_path(jp->from));
      transport_send(mp->tp, qdata(tmp, strlen(tmp)));
      break;
    case SMTP_MAIL_STATE:
      mp->state = SMTP_RCPT_STATE;
      sprintf(tmp,
	      "rcpt to:<%s@%s>\n",
	      get_user_from_path(jp->to),
	      get_host_from_path(jp->to));
      transport_send(mp->tp, qdata(tmp, strlen(tmp)));
      break;
    case SMTP_RCPT_STATE:
      mp->state = SMTP_DATA_STATE;
      transport_send(mp->tp, qdata("data\n", 5));
      break;
    case SMTP_DATA_STATE:
      if (mp->fp = fopen(jp->dfile, "r")) {
	mp->state = SMTP_SEND_STATE;
	fgets(tmp, sizeof(tmp), mp->fp);
	mail_smtp_send_upcall(mp->tp, transport_send_space(mp->tp));
      } else {
	mp->state = SMTP_QUIT_STATE;
	transport_close(mp->tp);
      }
      break;
    case SMTP_SEND_STATE:
      break;
    case SMTP_UNLK_STATE:
      unlink(jp->cfile);
      unlink(jp->dfile);
      unlink(jp->xfile);
      mp->sp->jobs = jp->next;
      free(jp);
      if (jp = mp->sp->jobs) goto nextjob;
      mp->state = SMTP_QUIT_STATE;
      transport_send(mp->tp, qdata("quit\n", 5));
      break;
    case SMTP_QUIT_STATE:
      transport_close(mp->tp);
      break;
    }
  else {
    if (mp->state != SMTP_QUIT_STATE) {
      strcpy(jp->return_reason, mp->buf);
      mail_return(jp);
      mp->state = SMTP_QUIT_STATE;
      transport_send(mp->tp, qdata("quit\n", 5));
    }
    transport_close(mp->tp);
  }
}

/*---------------------------------------------------------------------------*/

static void mail_smtp_recv_upcall(tp, cnt)
struct transport_cb *tp;
int  cnt;
{

  int  c;
  struct mbuf *bp;
  struct mesg *mp;

  mp = (struct mesg *) tp->user;
  mp->sp->state = MS_TALKING;
  transport_recv(tp, &bp, 0);
  while ((c = PULLCHAR(&bp)) != -1)
    if (c == '\n') {
      mp->buf[mp->cnt] = '\0';
      mail_smtp_transaction(mp);
      mp->cnt = 0;
    }
    else if (mp->cnt < sizeof(mp->buf) - 1)
      mp->buf[mp->cnt++] = c;
}

/*---------------------------------------------------------------------------*/

static void mail_smtp_send_upcall(tp, cnt)
struct transport_cb *tp;
int  cnt;
{

  char  *p;
  int  c;
  struct mbuf *bp;
  struct mesg *mp;

  mp = (struct mesg *) tp->user;
  if (mp->state != SMTP_SEND_STATE || cnt <= 0) return;
  if (!(bp = alloc_mbuf(cnt))) return;
  p = bp->data;
  while (p - bp->data < cnt && (c = getc(mp->fp)) != EOF)
    if (c && c != '\004' && c != '\032') *p++ = c;
  if (bp->cnt = p - bp->data)
    transport_send(tp, bp);
  else
    free_p(bp);
  if (c == EOF) {
    fclose(mp->fp);
    mp->fp = 0;
    transport_send(mp->tp, qdata(".\n", 2));
    mp->state = SMTP_UNLK_STATE;
  }
}

/*---------------------------------------------------------------------------*/

static void mail_smtp_state_upcall(tp)
struct transport_cb *tp;
{
  struct mesg *mp;

  if (mp = (struct mesg *) tp->user) {
    if (mp->fp) fclose(mp->fp);
    if (!mp->sp->jobs)
      mp->sp->state = MS_SUCCESS;
    else
      mailer_failed(mp->sp);
    free(mp);
  }
  transport_del(tp);
}

/*---------------------------------------------------------------------------*/

void mail_smtp(sp)
struct mailsys *sp;
{
  struct mesg *mp;

  mp = calloc(1, sizeof(*mp));
  mp->sp = sp;
  if (mp->tp = transport_open(sp->protocol, sp->address, mail_smtp_recv_upcall, mail_smtp_send_upcall, mail_smtp_state_upcall, (char *) mp)) {
    mp->tp->recv_mode = EOL_LF;
    mp->tp->send_mode = strcmp(sp->protocol, "tcp") ? EOL_CR : EOL_CRLF;
    transport_set_timeout(mp->tp, 3600);
    if (strcmp(sp->protocol, "tcp"))
      transport_send(mp->tp, qdata("cmd.smtp\n", 9));
  } else {
    mailer_failed(sp);
    free(mp);
  }
}

