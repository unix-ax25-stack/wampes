/* @(#) $Id: commands.h,v 1.33 1999/02/11 19:26:49 deyke Exp $ */

#ifndef _COMMANDS_H
#define _COMMANDS_H

/* In n8250.c, amiga.c: */
int doasystat(int argc,char *argv[],void *p);

/* In alloc.c: */
int domem(int argc,char *argv[],void *p);

/* In arpcmd.c: */
int doarp(int argc,char *argv[],void *p);

/* In asy.c: */
int asy_attach(int argc,char *argv[],void *p);

/* In ax25cmd.c: */
int doax25(int argc,char *argv[],void *p);
int doaxheard(int argc,char *argv[],void *p);
int doaxdest(int argc,char *argv[],void *p);
int doconnect(int argc,char *argv[],void *p);

/* In dirutil.c: */
int domkd(int argc,char *argv[],void *p);
int dormd(int argc,char *argv[],void *p);

/* In domain.c: */
int dodomain(int argc,char *argv[],void *p);

/* In finger.c: */
int dofinger(int argc,char *argv[],void *p);

/* In ftpcli.c: */
int doftp(int argc,char *argv[],void *p);

/* In ftpserv.c: */
int ftpstart(int argc,char *argv[],void *p);
int ftp0(int argc,char *argv[],void *p);

/* In icmpcmd.c: */
int doicmp(int argc,char *argv[],void *p);

/* In iface.c: */
int doifconfig(int argc,char *argv[],void *p);

/* In ipcmd.c: */
int doip(int argc,char *argv[],void *p);
int doroute(int argc,char *argv[],void *p);

/* In ksubr.c: */
int ps(int argc,char *argv[],void *p);

/* In main.c: */
int dodelete(int argc,char *argv[],void *p);
int dorename(int argc,char *argv[],void *p);
int doexit(int argc,char *argv[],void *p);
int dohostname(int argc,char *argv[],void *p);
int dolog(int argc,char *argv[],void *p);
int doattach(int argc,char *argv[],void *p);
int doparam(int argc,char *argv[],void *p);
int donothing(int argc,char *argv[],void *p);
int donrstat(int argc,char *argv[],void *p);
int doescape(int argc,char *argv[],void *p);
int doremote(int argc,char *argv[],void *p);
int dorepeat(int argc,char *argv[],void *p);

/* In nrcmd.c: */
int donetrom(int argc,char *argv[],void *p);
int nr_attach(int argc,char *argv[],void *p);

/* In pc.c: */
int doshell(int argc,char *argv[],void *p);

/* In ping.c: */
int doping(int argc,char *argv[],void *p);

/* In ripcmd.c: */
int dorip(int argc,char *argv[],void *p);

/* In ripcmd.c: */
int doaddrefuse(int argc,char *argv[],void *p);
int dodroprefuse(int argc,char *argv[],void *p);
int dorip(int argc,char *argv[],void *p);
int doripadd(int argc,char *argv[],void *p);
int doripdrop(int argc,char *argv[],void *p);
int doripinit(int argc,char *argv[],void *p);
int doripmerge(int argc,char *argv[],void *p);
int doripreq(int argc,char *argv[],void *p);
int doripstat(int argc,char *argv[],void *p);
int doripstop(int argc,char *argv[],void *p);
int doriptrace(int argc,char *argv[],void *p);

/* In session.c: */
int dosession(int argc,char *argv[],void *p);
int go(int argc,char *argv[],void *p);
int doclose(int argc,char *argv[],void *p);
int doreset(int argc,char *argv[],void *p);
int dokick(int argc,char *argv[],void *p);
int dorecord(int argc,char *argv[],void *p);
int doupload(int argc,char *argv[],void *p);

/* In smisc.c: */
int dis1(int argc,char *argv[],void *p);
int dis0(int argc,char *argv[],void *p);
int echo1(int argc,char *argv[],void *p);
int echo0(int argc,char *argv[],void *p);
int rem1(int argc,char *argv[],void *p);
int rem0(int argc,char *argv[],void *p);

/* In smtpcli.c: */
int dosmtp(int argc,char *argv[],void *p);

/* In tcpcmd.c: */
int dotcp(int argc,char *argv[],void *p);

/* In telnet.c: */
int doecho(int argc,char *argv[],void *p);
int doeol(int argc,char *argv[],void *p);
int dotelnet(int argc,char *argv[],void *p);
int dotopt(int argc,char *argv[],void *p);

/* In trace.c: */
int dotrace(int argc,char *argv[],void *p);

/* In udpcmd.c: */
int doudp(int argc,char *argv[],void *p);

/* In view.c: */
void view(int,void *,void *);

/* In various files: */

int axip_attach(int argc,char *argv[],void *p);
int doaxip(int argc,char *argv[],void *p);
int dobye(int argc,char *argv[],void *p);
int dofkey(int argc,char *argv[],void *p);
int doflexnet(int argc,char *argv[],void *p);
int doipfilter(int argc,char *argv[],void *p);
int dologin(int argc,char *argv[],void *p);
int domain0(int argc,char *argv[],void *p);
int domain1(int argc,char *argv[],void *p);
int dosntp(int argc,char *argv[],void *p);
int dosource(int argc,char *argv[],void *p);
int ipip_attach(int argc,char *argv[],void *p);
int krnlif_attach(int argc,char *argv[],void *p);
int ni_attach(int argc,char *argv[],void *p);
int sntp0(int argc,char *argv[],void *p);
int sntp1(int argc,char *argv[],void *p);
int tcpgate1(int argc,char *argv[],void *p);
int time0(int argc,char *argv[],void *p);
int time1(int argc,char *argv[],void *p);
int tun_attach(int argc,char *argv[],void *p);

#endif  /* _COMMANDS_H */
