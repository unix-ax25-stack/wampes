/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/services.c,v 1.1 1990/10/12 19:26:33 deyke Exp $ */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "global.h"
#include "socket.h"
#include "netuser.h"

struct port_table {
  char  *name;
  int  port;
};

static struct port_table tcp_port_table[] = {
  "*",           0,
  "convers",     3600,
  "discard",     IPPORT_DISCARD,/* ARPA discard protocol */
  "echo",        IPPORT_ECHO,   /* ARPA echo protocol */
  "finger",      IPPORT_FINGER, /* ARPA finger protocol */
  "ftp",         IPPORT_FTP,    /* ARPA file transfer protocol (cmd) */
  "ftp-data",    IPPORT_FTPD,   /* ARPA file transfer protocol (data) */
  "netupds",     4715,
  "smtp",        IPPORT_SMTP,   /* ARPA simple mail transfer protocol */
  "telnet",      IPPORT_TELNET, /* ARPA virtual terminal protocol */
  "ttylink",     IPPORT_TTYLINK,
  NULLCHAR
};

static struct port_table udp_port_table[] = {
  "*",           0,
  "domain",      IPPORT_DOMAIN, /* ARPA domain nameserver */
  "remote",      IPPORT_REMOTE,
  "rip",         IPPORT_RIP,
  "stime",       4713,
  NULLCHAR
};

static char *port_name __ARGS((struct port_table *table, int port));
static int port_number __ARGS((struct port_table *table, char *name));

/*---------------------------------------------------------------------------*/

static char  *port_name(table, port)
struct port_table *table;
int  port;
{
  static char  buf[16];

  for (; table->name; table++)
    if (port == table->port) return table->name;
  sprintf(buf, "%u", port);
  return buf;
}

/*---------------------------------------------------------------------------*/

char  *tcp_port_name(port)
int  port;
{
  return port_name(tcp_port_table, port);
}

/*---------------------------------------------------------------------------*/

char  *udp_port_name(port)
int  port;
{
  return port_name(udp_port_table, port);
}

/*---------------------------------------------------------------------------*/

static int  port_number(table, name)
struct port_table *table;
char  *name;
{
  int  len;

  if (!isdigit(uchar(*name))) {
    len = strlen(name);
    for (; table->name; table++)
      if (!strncmp(name, table->name, len)) return table->port;
  }
  return atoi(name);
}

/*---------------------------------------------------------------------------*/

int  tcp_port_number(name)
char  *name;
{
  return port_number(tcp_port_table, name);
}

/*---------------------------------------------------------------------------*/

int  udp_port_number(name)
char  *name;
{
  return port_number(udp_port_table, name);
}

/*---------------------------------------------------------------------------*/

char  *pinet_tcp(s)
struct socket *s;
{
  static char  buf[128];

  sprintf(buf, "%s:%s", inet_ntoa(s->address), tcp_port_name(s->port));
  return buf;
}

/*---------------------------------------------------------------------------*/

char  *pinet_udp(s)
struct socket *s;
{
  static char  buf[128];

  sprintf(buf, "%s:%s", inet_ntoa(s->address), udp_port_name(s->port));
  return buf;
}

