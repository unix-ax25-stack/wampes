/* @(#) $Id: ftp.c,v 1.14 1996/08/19 16:30:14 deyke Exp $ */

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
ftpdr(
struct tcb *tcb,
int32 cnt)
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
	while(bp != NULL){
		if(bp->cnt != 0)
			fwrite(bp->data,1,(unsigned)bp->cnt,ftp->fp);
		free_mbuf(&bp);
	}

	if(ftp->fp != stdout && ferror(ftp->fp)){ /* write error (dsk full?) */
		fclose(ftp->fp);
		ftp->fp = NULL;
		close_self(tcb,RESET);
	}
}
/* FTP Data channel Transmit upcall handler */
void
ftpdt(
struct tcb *tcb,
int32 cnt)
{
	struct ftp *ftp;
	struct mbuf *bp;
	register uint8 *cp;
	register int c;
	int eof_flag;

	ftp = (struct ftp *)tcb->user;
	if(ftp->state != SENDING_STATE){
		close_tcp(tcb);
		return;
	}
	if((bp = alloc_mbuf((uint16) cnt)) == NULL){
		/* Hard to know what to do here */
		return;
	}
	eof_flag = 0;
	if(ftp->type != ASCII_TYPE){
		bp->cnt = fread(bp->data,1,(unsigned) cnt,ftp->fp);
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
		send_tcp(tcb,&bp);
	else
		free_p(&bp);

	if(eof_flag){   /* EOF seen */
		if(ftp->fp != stdout)
			fclose(ftp->fp);
		ftp->fp = NULL;
		close_tcp(tcb);
	}
}
/* Allocate an FTP control block */
struct ftp *
ftp_create(
unsigned bufsize)
{
	register struct ftp *ftp;

	if((ftp = (struct ftp *)calloc(1,sizeof (struct ftp))) == NULL)
		return NULL;
	if(bufsize != 0 && (ftp->buf = (char *) malloc(bufsize)) == NULL){
		printf("called by ftp_create\n");
		ftp_delete(ftp);
		printf("called by ftp_create\n");
		return NULL;
	}
	ftp->state = COMMAND_STATE;
	ftp->type = ASCII_TYPE; /* Default transfer type */
	return ftp;
}
/* Free resources, delete control block */
void
ftp_delete(
register struct ftp *ftp)
{
	if(ftp->fp != NULL && ftp->fp != stdout)
		fclose(ftp->fp);
	if(ftp->data != NULL)
		del_tcp(&ftp->data);
	if(ftp->username != NULL)
		free(ftp->username);
	if(ftp->root != NULL)
		free(ftp->root);
	if(ftp->buf != NULL)
		free(ftp->buf);
	if(ftp->cd != NULL)
		free(ftp->cd);
	if(ftp->session != NULL)
		freesession(ftp->session);
	free(ftp);
}
