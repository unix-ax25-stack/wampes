/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/commands.h,v 1.1 1990/08/23 17:32:43 deyke Exp $ */

/* In 8250.c, amiga.c: */
int doasystat __ARGS((int argc,char *argv[],void *p));

/* In alloc.c: */
int domem __ARGS((int argc,char *argv[],void *p));

/* In arpcmd.c: */
int doarp __ARGS((int argc,char *argv[],void *p));

/* In asy.c: */
int asy_attach __ARGS((int argc,char *argv[],void *p));

/* In ax_mbx.c: */
int dombox __ARGS((int argc,char *argv[],void *p));

/* In ax25cmd.c: */
int ax250 __ARGS((int argc,char *argv[],void *p));
int ax25start __ARGS((int argc,char *argv[],void *p));
int doax25 __ARGS((int argc,char *argv[],void *p));
int doaxheard __ARGS((int argc,char *argv[],void *p));
int doconnect __ARGS((int argc,char *argv[],void *p));

/* In dirutil.c: */
int docd __ARGS((int argc,char *argv[],void *p));
int dodir __ARGS((int argc,char *argv[],void *p));

/* In domain.c: */
int dodomain __ARGS((int argc,char *argv[],void *p));
int dodtrace __ARGS((int argc,char *argv[],void *p));

/* In drsi.c: */
int dodrstat __ARGS((int argc,char *argv[],void *p));
int dr_attach __ARGS((int argc,char *argv[],void *p));
int dodr __ARGS((int argc,char *argv[],void *p));

/* In eagle.c: */
int eg_attach __ARGS((int argc,char *argv[],void *p));
int doegstat __ARGS((int argc,char *argv[],void *p));

/* In ec.c: */
int doetherstat __ARGS((int argc,char *argv[],void *p));
int ec_attach __ARGS((int argc,char *argv[],void *p));

/* In finger.c: */
int dofinger __ARGS((int argc,char *argv[],void *p));

/* In fingerd.c: */
int finstart __ARGS((int argc,char *argv[],void *p));
int fin0 __ARGS((int argc,char *argv[],void *p));

/* In ftpcli.c: */
int doftp __ARGS((int argc,char *argv[],void *p));
int doabort __ARGS((int argc,char *argv[],void *p));

/* In ftpserv.c: */
int ftpstart __ARGS((int argc,char *argv[],void *p));
int ftp0 __ARGS((int argc,char *argv[],void *p));

/* In hapn.c: */
int dohapnstat __ARGS((int argc,char *argv[],void *p));
int hapn_attach __ARGS((int argc,char *argv[],void *p));

/* In hs.c: */
int dohs __ARGS((int argc,char *argv[],void *p));
int hs_attach __ARGS((int argc,char *argv[],void *p));

/* In icmpcmd.c: */
int doicmp __ARGS((int argc,char *argv[],void *p));
int doping __ARGS((int argc,char *argv[],void *p));

/* In iface.c: */
int doifconfig __ARGS((int argc,char *argv[],void *p));
int dodetach __ARGS((int argc,char *argv[],void *p));

/* In ipcmd.c: */
int doip __ARGS((int argc,char *argv[],void *p));
int doroute __ARGS((int argc,char *argv[],void *p));

/* In ksubr.c: */
int ps __ARGS((int argc,char *argv[],void *p));

/* In main.c: */
int dodelete __ARGS((int argc,char *argv[],void *p));
int dorename __ARGS((int argc,char *argv[],void *p));
int doexit __ARGS((int argc,char *argv[],void *p));
int dohostname __ARGS((int argc,char *argv[],void *p));
int dolog __ARGS((int argc,char *argv[],void *p));
int dohelp __ARGS((int argc,char *argv[],void *p));
int doattach __ARGS((int argc,char *argv[],void *p));
int doparam __ARGS((int argc,char *argv[],void *p));
int domode __ARGS((int argc,char *argv[],void *p));
int domore __ARGS((int argc,char *argv[],void *p));
int donrstat __ARGS((int argc,char *argv[],void *p));
int doescape __ARGS((int argc,char *argv[],void *p));
int doremote __ARGS((int argc,char *argv[],void *p));

/* In nrcmd.c: */
int donetrom __ARGS((int argc,char *argv[],void *p));
int nr40 __ARGS((int argc,char *argv[],void *p));
int nr4start __ARGS((int argc,char *argv[],void *p));
int nr_attach __ARGS((int argc,char *argv[],void *p));

/* In pc.c: */
int doshell __ARGS((int argc,char *argv[],void *p));

/* In pc100.h: */
int pc_attach __ARGS((int argc,char *argv[],void *p));

/* In pktdrvr.c: */
int pk_attach __ARGS((int argc,char *argv[],void *p));

/* In rip.c: */
int dorip __ARGS((int argc,char *argv[],void *p));

/* In ripcmd.c: */
int doaddrefuse __ARGS((int argc,char *argv[],void *p));
int dodroprefuse __ARGS((int argc,char *argv[],void *p));
int dorip __ARGS((int argc,char *argv[],void *p));
int doripadd __ARGS((int argc,char *argv[],void *p));
int doripdrop __ARGS((int argc,char *argv[],void *p));
int doripinit __ARGS((int argc,char *argv[],void *p));
int doripmerge __ARGS((int argc,char *argv[],void *p));
int doripreq __ARGS((int argc,char *argv[],void *p));
int doripstat __ARGS((int argc,char *argv[],void *p));
int doripstop __ARGS((int argc,char *argv[],void *p));
int doriptrace __ARGS((int argc,char *argv[],void *p));

/* In scc.c: */
int scc_attach __ARGS((int argc,char *argv[],void *p));
int dosccstat __ARGS((int argc,char *argv[],void *p));

/* In session.c: */
int dosession __ARGS((int argc,char *argv[],void *p));
int go __ARGS((int argc,char *argv[],void *p));
int doclose __ARGS((int argc,char *argv[],void *p));
int doreset __ARGS((int argc,char *argv[],void *p));
int dokick __ARGS((int argc,char *argv[],void *p));
int dorecord __ARGS((int argc,char *argv[],void *p));
int doupload __ARGS((int argc,char *argv[],void *p));

/* In smisc.c: */
int telnet1 __ARGS((int argc,char *argv[],void *p));
int telnet0 __ARGS((int argc,char *argv[],void *p));
int dis1 __ARGS((int argc,char *argv[],void *p));
int dis0 __ARGS((int argc,char *argv[],void *p));
int echo1 __ARGS((int argc,char *argv[],void *p));
int echo0 __ARGS((int argc,char *argv[],void *p));
int rem1 __ARGS((int argc,char *argv[],void *p));
int rem0 __ARGS((int argc,char *argv[],void *p));

/* In smtpcli.c: */
int dosmtp __ARGS((int argc,char *argv[],void *p));

/* In smtpserv.c: */
int smtp1 __ARGS((int argc,char *argv[],void *p));
int smtp0 __ARGS((int argc,char *argv[],void *p));

/* In sockcmd.c: */
int dosock __ARGS((int argc,char *argv[],void *p));

/* In sw.c: */
int doswatch __ARGS((int argc,char *argv[],void *p));

/* In tcpcmd.c: */
int dotcp __ARGS((int argc,char *argv[],void *p));

/* In telnet.c: */
int doecho __ARGS((int argc,char *argv[],void *p));
int doeol __ARGS((int argc,char *argv[],void *p));
int dotelnet __ARGS((int argc,char *argv[],void *p));

/* In ttylink.c: */
int ttylstart __ARGS((int argc,char *argv[],void *p));
int ttyl0 __ARGS((int argc,char *argv[],void *p));

/* In trace.c: */
int dotrace __ARGS((int argc,char *argv[],void *p));

/* In udpcmd.c: */
int doudp __ARGS((int argc,char *argv[],void *p));

