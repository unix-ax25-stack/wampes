/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ftp.c,v 1.6 1993/09/22 16:44:49 deyke Exp $ */

/* Stuff common to both the FTP server and client */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "ftp.h"
#include "session.h"

/* FTP Data channel Receive upcall handler */
void
ftpdr(tcb,cnt)
struct tcb *tcb;
int cnt;
{
	register struct ftp *ftp;
	struct mbuf *bp;
	int c;

	ftp = (struct ftp *)tcb->user;
	if(ftp->state != RECEIVING_STATE){
		close_tcp(tcb);
		return;
	}
	/* This will likely also generate an ACK with window rotation */
	recv_tcp(tcb,&bp,cnt);

	if(ftp->type == ASCII_TYPE){
		while((c = PULLCHAR(&bp)) != -1){
			if(c != '\r')
				putc(c,ftp->fp);
		}
		return;
	}
	while(bp != NULLBUF){
		if(bp->cnt != 0)
			fwrite(bp->data,1,(unsigned)bp->cnt,ftp->fp);
		bp = free_mbuf(bp);
	}

	if(ftp->fp != stdout && ferror(ftp->fp)){ /* write error (dsk full?) */
		fclose(ftp->fp);
		ftp->fp = NULLFILE;
		close_self(tcb,RESET);
	}
}
/* FTP Data channel Transmit upcall handler */
void
ftpdt(tcb,cnt)
struct tcb *tcb;
int cnt;
{
	struct ftp *ftp;
	struct mbuf *bp;
	register char *cp;
	register int c;
	int eof_flag;

	ftp = (struct ftp *)tcb->user;
	if(ftp->state != SENDING_STATE){
		close_tcp(tcb);
		return;
	}
	if((bp = alloc_mbuf(cnt)) == NULLBUF){
		/* Hard to know what to do here */
		return;
	}
	eof_flag = 0;
	if(ftp->type == IMAGE_TYPE){
		bp->cnt = fread(bp->data,1,cnt,ftp->fp);
		if(bp->cnt != cnt)
			eof_flag = 1;
	} else {
		cp = bp->data;
		while(cnt > 1){
			if((c = getc(ftp->fp)) == EOF){
				eof_flag=1;
				break;
			}
			if(c == '\n'){
				*cp++ = '\r';
				bp->cnt++;
				cnt--;
			}
			*cp++ = c;
			bp->cnt++;
			cnt--;
		}
	}
	if(bp->cnt != 0)
		send_tcp(tcb,bp);
	else
		free_p(bp);

	if(eof_flag){   /* EOF seen */
		if(ftp->fp != stdout)
			fclose(ftp->fp);
		ftp->fp = NULLFILE;
		close_tcp(tcb);
	}
}
/* Allocate an FTP control block */
struct ftp *
ftp_create(bufsize)
unsigned bufsize;
{
	register struct ftp *ftp;

	if((ftp = (struct ftp *)calloc(1,sizeof (struct ftp))) == NULLFTP)
		return NULLFTP;
	if(bufsize != 0 && (ftp->buf = malloc(bufsize)) == NULLCHAR){
		printf("called by ftp_create\n");
		ftp_delete(ftp);
		printf("called by ftp_create\n");
		return NULLFTP;
	}
	ftp->state = COMMAND_STATE;
	ftp->type = ASCII_TYPE; /* Default transfer type */
	return ftp;
}
/* Free resources, delete control block */
void
ftp_delete(ftp)
register struct ftp *ftp;
{
	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);
	if(ftp->data != NULLTCB)
		del_tcp(ftp->data);
	if(ftp->username != NULLCHAR)
		free(ftp->username);
	if(ftp->path != NULLCHAR)
		free(ftp->path);
	if(ftp->buf != NULLCHAR)
		free(ftp->buf);
	if(ftp->cd != NULLCHAR)
		free(ftp->cd);
	if(ftp->session != NULLSESSION)
		freesession(ftp->session);
	free((char *)ftp);
}

