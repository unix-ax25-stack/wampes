/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/files.c,v 1.3 1990/09/11 13:45:16 deyke Exp $ */

/* System-dependent definitions of various files, spool directories, etc */
#include "global.h"
#include "files.h"

#ifdef  MSDOS
char *Startup = "/autoexec.net";        /* Initialization file */
char *Userfile = "/ftpusers";   /* Authorized FTP users and passwords */
char *Maillog = "/spool/mail.log";      /* mail log */
char *Mailspool = "/spool/mail";        /* Incoming mail */
char *Mailqdir = "/spool/mqueue";               /* Outgoing mail spool */
char *Mailqueue = "/spool/mqueue/*.wrk";        /* Outgoing mail work files */
char *Routeqdir = "/spool/rqueue";              /* queue for router */
char *Alias = "/alias";         /* the alias file */
char *Dfile = "/domain.txt";    /* Domain cache */
char *Fdir = "/finger";         /* Finger info directory */
char *Arealist = "/spool/areas";/* List of message areas */
char *Helpdir = "/spool/help";  /* Mailbox help file directory */
char *Rewritefile = "/spool/rewrite"; /* Address rewrite file */
char Eol[] = "\r\n";
#define SEPARATOR       "/"
#endif

#ifdef  UNIX
#if (defined(hpux)||defined(__hpux))
char *Startup = "/tcp/net.rc";          /* Initialization file */
#else
char *Startup = "./startup.net";        /* Initialization file */
#endif
char *Config = "./config.net";  /* Device configuration list */
char *Userfile = "./ftpusers";
char *Mailspool = "./mail";
char *Maillog = "./mail.log";   /* mail log */
char *Mailqdir = "./mqueue";
char *Mailqueue = "./mqueue/*.wrk";
char *Routeqdir = "./rqueue";           /* queue for router */
char *Alias = "./alias";        /* the alias file */
char *Dfile = "./domain.txt";   /* Domain cache */
char *Fdir = "./finger";                /* Finger info directory */
char *Arealist = "./areas";             /* List of message areas */
char *Helpdir = "./help";       /* Mailbox help file directory */
char *Rewritefile = "./rewrite"; /* Address rewrite file */
#define SEPARATOR       "/"
char Eol[] = "\n";
#endif

#ifdef  AMIGA
char *Startup = "TCPIP:net-startup";
char *Config = "TCPIP:config.net";      /* Device configuration list */
char *Userfile = "TCPIP:ftpusers";
char *Mailspool = "TCPIP:spool/mail";
char *Maillog = "TCPIP:spool/mail.log";
char *Mailqdir = "TCPIP:spool/mqueue";
char *Mailqueue = "TCPIP:spool/mqueue/#?.wrk";
char *Routeqdir = "TCPIP:spool/rqueue";         /* queue for router */
char *Alias = "TCPIP:alias";    /* the alias file */
char *Dfile = "TCPIP:domain.txt";       /* Domain cache */
char *Fdir = "TCPIP:finger";            /* Finger info directory */
char *Arealist = "TCPIP:spool/areas";   /* List of message areas */
char *Helpdir = "TCPIP:spool/help";     /* Mailbox help file directory */
char *Rewritefile = "TCPIP:spool/rewrite"; /* Address rewrite file */
#define SEPARATOR       "/"
char Eol[] = "\r\n";
#endif

#ifdef  MAC
char *Startup ="Mikes Hard Disk:net.start";
char *Config = "Mikes Hard Disk:config.net";    /* Device configuration list */
char *Userfile = "Mikes Hard Disk:ftpusers";
char *Mailspool = "Mikes Hard Disk:spool:mail:";
char *Maillog = "Mikes Hard Disk:spool:mail.log:";
char *Mailqdir = "Mikes Hard Disk:spool:mqueue:";
char *Mailqueue = "Mikes Hard Disk:spool:mqueue:*.wrk";
char *Routeqdir = "Mikes Hard Disk:spool/rqueue:";      /* queue for router */
char *Alias = "Mikes Hard Disk:alias";  /* the alias file */
char *Dfile = "Mikes Hard Disk:domain:txt";     /* Domain cache */
char *Fdir = "Mikes Hard Disk:finger";          /* Finger info directory */
char *Arealist = "Mikes Hard Disk:spool/areas"; /* List of message areas */
char *Helpdir = "Mikes Hard Disk:spool/help"; /* Mailbox help file directory */
char *Rewritefile = "Mikes Hard Disk:spool/rewrite"; /* Address rewrite file */
#define SEPARATOR       ":"
char Eol[] = "\r";
#endif

static char *strcatdup __ARGS((char *a,char *b,char *c));

/* Establish a root directory other than the default. Can only be called
 * once, at startup time
 */
void
initroot(root)
char *root;
{
	Startup = strcatdup(root,SEPARATOR,Startup);
	Userfile = strcatdup(root,SEPARATOR,Userfile);
	Maillog = strcatdup(root,SEPARATOR,Maillog);
	Mailspool = strcatdup(root,SEPARATOR,Mailspool);
	Mailqdir = strcatdup(root,SEPARATOR,Mailqdir);
	Mailqueue = strcatdup(root,SEPARATOR,Mailqueue);
	Routeqdir = strcatdup(root,SEPARATOR,Routeqdir);
	Alias = strcatdup(root,SEPARATOR,Alias);
	Dfile = strcatdup(root,SEPARATOR,Dfile);
	Fdir = strcatdup(root,SEPARATOR,Fdir);
	Arealist = strcatdup(root,SEPARATOR,Arealist);
	Helpdir = strcatdup(root,SEPARATOR,Helpdir);
	Rewritefile = strcatdup(root,SEPARATOR,Rewritefile);
}

/* Concatenate root, separator and arg strings into a malloc'ed output
 * buffer, then remove repeated occurrences of the separator char
 */
static char *
strcatdup(a,b,c)
char *a,*b,*c;
{
	char *out,*p1,*p2;

	out = mallocw(strlen(a) + strlen(b) + strlen(c) + 1);
	strcpy(out,a);
	strcat(out,b);
	strcat(out,c);
	if(*b != '\0'){
		/* Remove any repeated occurrences of the separator char */
		p1 = p2 = out;
		while(*p2 != '\0'){
			*p1++ = *p2++;
			while(p2[0] == p2[-1] && p2[0] == b[0])
				p2++;
		}
		*p1 = '\0';
	}
	return out;
}

