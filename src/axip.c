/* @(#) $Id: axip.c,v 1.31 2006/03/12 10:05:01 dl9sau Exp $ */

#include <sys/types.h>

#include "global.h"
#undef  hiword
#undef  loword
#undef  hibyte
#undef  lobyte

#include <errno.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "strerror.h"

#include "mbuf.h"
#include "iface.h"
#include "timer.h"
#include "internet.h"
#include "netuser.h"
#include "ax25.h"
#include "socket.h"
#include "trace.h"
#include "cmdparse.h"
#include "hpux.h"
#include "crc.h"
#include "axip.h"
#include "domain.h"
#include "../lib/buildsaddr.h"

#define MAX_FRAME       2048

#ifdef	notdef
struct edv_t {
  int type;
#define USE_IP          0
#define USE_UDP         1
  int port;
  int fd;
};
#else
#include "sockaddr_util.h"
#include "uhnp.h"
#endif

struct axip_route {
  uint8 call[AXALEN];
  struct sockaddr_storage dest;         /* outer peer, port filled in at send */

  /* DIE SONDERADRESSE "DEFAULT", und ausdruecklich ein Feld und nicht ein
   * Rufzeichen: "default" hat sieben Buchstaben, ein AX.25-Rufzeichen hat
   * sechs plus SSID.  Es passt also in kein call[] und kann dort auch nicht
   * durch pax25() hindurch - wer es dort vergleicht, vergleicht immer gegen
   * nichts und haelt die Funktion fuer tot (Thomas).
   *
   * Der Eintrag mit diesem Flag ist kein Partner, sondern die Antwort auf
   * die Frage "wenn ich das Rufzeichen nicht kenne, wohin damit?" - der
   * AX.25-Weg durch ax25 route default, nur eine Ebene tiefer: dort decides
   * der Pfad ueber das Interface, hier ueber die aeussere Gegenstelle.
   */
  int is_default;

  /* WENN ES KEIN AXIP/AXUDP IST, sondern eine TCP-SITZUNG.  Die aeussere
   * Adresse ist bei TCP nicht die Kennung: zu einer Adresse kann es mehrere
   * gleichzeitige Sitzungen geben, und ein KISS-Kanal ist ueberhaupt keine
   * Adresse.  Deshalb traegt der Eintrag dann die SITZUNG selbst.
   *
   * tsock != NULL heisst: dies ist eine TCP-Route, und "wohin" heisst "in
   * diese Sitzung, auf diesem Kanal".  Und sie stirbt mit der Sitzung - siehe
   * axip_forget_transport().
   */
  void *tsock;
  int tchan;			/* KISS-Kanal, 0 sonst */
  int tproto;			/* TCPAD_AXTCP oder TCPAD_KISSTCP */
  /* DER GELERNTE QUELLPORT, JE RUFZEICHEN - nach dem Vorbild der
   * bpqether-Route (mac/mac_ifp/mactime): der Wert, das Interface, auf dem er
   * gehoert wurde, und wann.
   *
   * Bis hierher lernte allein uhnp, und zwar JE HOST.  Das geht auf, solange
   * hinter einer Adresse ein Knoten steht - und verwechselt zwei, sobald dort
   * zwei unabhaengige stehen, denn dann teilen sich beide Rufzeichen einen
   * Port und die Antwort an das eine landet beim anderen.  Die Tabelle je
   * Rufzeichen gab es dabei die ganze Zeit; sie warf den Port nur weg
   * (axip_route_add() mit keepport 0).  Sie behaelt ihn jetzt.
   *
   * uhnp bleibt: fuer ipip, das gar keine Rufzeichen kennt, und hier als
   * Rueckfall fuer ein Rufzeichen, das wir noch nie gehoert haben - etwa das
   * zweite eines Partners, der mehrere auf einem ax25ipd fuehrt.
   */
  int lport;
  struct edv_t *ledv;
  time_t ltime;

  /* DER NAME, UND DIE FRAGEN, DIE NUR MIT EINEM NAME UEBERHAUPT SINKEN.  Bis
   * hierher wurde der Name beim Eintragen einmal aufgeloest und dann
   * weggeworfen: die Route kannte nur die Adresse, die gerade dastand, und
   * eine, die sich aenderte, blieb bis zum Neustart stehen.  Mit dem Namen
   * im Eintrag kann sie nachfragen - und nur Eintraege MIT Namen fragen
   * ueberhaupt nach, eine Literale hat nichts zu fragen.
   */
  char *name;

  /* PERSISTENT, und das heisst: die Zuordnung Rufzeichen -> aeussere Adresse
   * wird von einem Frame aus dem Verkehr nicht mehr ueberschrieben.  Weder
   * der eigene noch der fremde Weg: das eine waere eine Luecke, weil der
   * Aufloeser denselben Eintrag anfasst wie das Lernen (Thomas), das andere
   * waere ein Port, auf dem eine Station ihre Adresse wechseln darf, nur
   * weil sie es gerade braucht.
   *
   * Getrennt von "der Sysop hat es gesagt" - nach dem Vorbild von noarp und
   * noarp_auto, damit "axip route" die beiden unterscheiden kann und eine
   * automatisch geschuetzte Zeile nicht so aussieht, als haette der Sysop
   * sie geschrieben.
   */
  int perm;                   /* the sysop wrote "permanent" */
  int perm_once;              /* "axip-learn once": trust on first use */

  /* WANN ZULETZT von dieser ADRESSE etwas kam, und wann zuletzt danach
   * gesendet wurde.  Beide zusammen sind das, wonach die Stille-Frist
   * misst: ein Ziel, das nur UI-Pakete von uns bekommt und wenig zurueck,
   * sieht ueber htime still aus und waere damit abgeschaltet (Thomas).
   */
  time_t htime;
  time_t stime;

  /* WANN ZULETZT nach dem Namen gefragt wurde - auch im Fehlerfall, sonst
   * fragt ein NXDOMAIN bei jedem Paket nach.  Getrennt von htime, weil
   * beides Verschiedenes ist: htime sagt, ob der Host lebt, rtime sagt, ob
   * wir schon gefragt haben.
   */
  time_t rtime;

  /* WIR WISSEN, DASS ES FALSCH IST: ein Frame kam von einer anderen Adresse
   * und wurde deshalb verworfen.  Das ist ein Beweis und keine Vermutung,
   * und es geht deshalb an beiden Fristen vorbei - bis der Sysop eine
   * andere Adresse nennt, nuetzt Warten nichts.
   */
  int qdue;

  /* WELCHER PORT.  Die Tabelle ist global, die Politik ist je Port, und ohne
   * diese Zahl wuerde eine auf axudp 93 gelernte Route auf 931 benutzt und
   * den Rahmen ueber den falschen Socket an die 93-Adresse schicken (siehe
   * "AXIP" in TODO.txt).  0 = noch keiner, dann bindet die Route beim
   * ersten Benutzen; danach bewegt sie sich nur, wenn der Sysop das
   * "axip route add" wiederholt.
   *
   * Eine axip_ifp_to_rdev()-Nummer und keine Stellung in der Liste - siehe das
   * Feld devnum in struct iface.
   */
  int rdev;

  struct axip_route *next;
};

static struct axip_route *Axip_routes;

/* WOHER EIN EINTRAG KOMMT.  In axip_route_add() steht, weil dort der
 * perm-Schutz steht, und nicht bei den Aufrufern: der Aufloeser fasst
 * denselben Eintrag an wie das Lernen aus dem Verkehr, und eine Regel, die
 * nur in einem von beiden steht, laesst genau die andere Seite durch.
 */
#define AXIP_FROM_SYSOP    0     /* "axip route add": der Sysop hat es gesagt */
#define AXIP_FROM_RESOLVE  1     /* der Timer oder der Sendeweg fragt nach */
#define AXIP_FROM_LEARNED  2     /* ein kam von selbst aus dem Verkehr */

/* DIE KNOTENWEITEN VORGABEN, mit "0 am Port = der Knotenwert" wie bei paclen
 * und emaxframe.  Die Variablen sind nicht statisch, denn "axip dns-interval
 * " schreibt sie von aussen.
 */
int Axip_dns_interval = AXIP_DNS_INTERVAL_DEFAULT;
int Axip_dns_silence = AXIP_DNS_SILENCE_DEFAULT;

/* Die Zaehler fuer "axip stats".  Beides sind Faelle, in denen wir einen
 * Rahmen wegwerfen, und beide waeren im Betrieb lautlos - einmal abgelehnt
 * heisst, dass die Adresse nicht mehr passt, und das sieht sonst aus wie ein
 * Funkloch.
 */
static long Axip_refused;      /* gelernt werden wollte, aber permanent */
static long Axip_dropped;      /* Adresse passt nicht, oder Lernen ist aus */
static long Axip_lookups;      /* durchgefuehrte Namensfragen */
static long Axip_lookupfail;   /* davon ohne Ergebnis */

static int axip_raw(struct iface *ifp, struct mbuf **bpp);
static void axip_recv(void *argp);
static int axip_route_add(uint8 *call, const struct sockaddr *dest,
	  int keepport, int from, struct iface *ifp);
static void axip_learn_port(uint8 *call, const struct sockaddr *addr, struct edv_t *edv);
static int axip_learned_port(struct axip_route *rp, struct edv_t *edv);
static int doaxiproute(int argc, char *argv[], void *p);
static int doaxiprouteadd(int argc, char *argv[], void *p);
static int doaxiproutedrop(int argc, char *argv[], void *p);
static int doaxipstats(int argc, char *argv[], void *p);

/* Fuer die TCP-Ports in axtcp.c und kisstcp.c, siehe axip_forget_transport()
 * und axip_learn_transport() weiter unten.
 */
void axip_forget_transport(void *tsock);
int axip_learn_transport(const uint8 *call, void *tsock, int chan, int proto, struct iface *ifp);
void *axip_transport_route(const uint8 *call, struct iface *ifp);
void axip_heard(const uint8 *call);
void axip_dropped(void);

/*---------------------------------------------------------------------------*/

/* DARF AUF DIESEM PORT GELERNT WERDEN?  "once" heisst ja: gelernt wird
 * weiter, nur die Adresse friert danach ein.  Deshalb steht hier kein
 * "!= OFF", sondern genau die Stufe, bei der etwas NEU entsteht - sonst
 * waere "once" nur ein anderes Wort fuer "off".
 */
static int axip_may_learn(
struct iface *ifp)
{
  return !ifp || ifp->axip_learn != AXIP_LEARN_OFF;
}


/* IST DAS DAS WORTT "default"?  Der Befehl, nicht die Tabelle - dort steht es
 * als Flag in axip_route.is_default, und der Grund ist oben notiert.
 */
static int axip_arg_is_default(
const char *arg)
{
  return !strcasecmp(arg, "default");
}


/* DER TRANSPORT, wenn es kein AXIP/AXUDP mehr ist.  Die aeussere Adresse ist
 * bei TCP nicht die Kennung: es kann zu einer Adresse mehrere gleichzeitige
 * Sitzungen geben, und ein KISS-Kanal ist ueberhaupt keine Adresse.  Deshalb
 * traegt der Eintrag dann die SITZUNG selbst - und stirbt mit ihr, siehe
 * axip_forget_transport().
 */
static int axip_call_match(const uint8 *route_call, const uint8 *target_call)
{
  return addreq(route_call, target_call);
}

/* Der Port zu einer gespeicherten Nummer, oder NULL.  Ein Port, der
 * wegging, gibt NULL zurueck - und nicht den Nachbarn, das waere die
 * schlimmere Ueberraschung.
 */
/* Helfer fuer die Portbindung der axip-Routen - stabil ohne Struct-Aenderung */
static unsigned long axip_ifp_key(const struct iface *ifp)
{
  return (unsigned long) ifp;
}
static struct iface *axip_iface_from_key(unsigned long key)
{
  struct iface *ifp;
  for (ifp = Ifaces; ifp != NULL; ifp = ifp->next)
    if ((unsigned long) ifp == key)
      return ifp;
  return NULL;
}
static int axip_ifp_to_rdev(const struct iface *ifp)
{
  if (ifp == NULL || ifp == &Loopback || ifp == &Encap) return 0;
  return (int)((unsigned long) ifp & 0x7fffffff);
}
static struct iface *axip_iface_from_rdev(int rdev)
{
  if (rdev <= 0) return NULL;
  struct iface *ifp;
  for (ifp = Ifaces; ifp != NULL; ifp = ifp->next)
    if ((int)((unsigned long) ifp & 0x7fffffff) == rdev)
      return ifp;
  return NULL;
}

static struct iface *axip_iface(
int devnum)
{
  return axip_iface_from_rdev(devnum);
}

int axip_isport(
const struct iface *ifp)
{
  return ifp != NULL && ifp->raw == axip_raw;
}

/*---------------------------------------------------------------------------*/

/* DER NAME IN EINEN SOCKADDR, unabhaengig von der Familie.
 *
 * Drei Wege, in dieser Reihenfolge, und die Reihenfolge ist das einzige
 * Interessante daran:
 *
 *  1 resolve_sa() liest den WAMPES-eigenen Hosttable in TCPDIR/hosts und
 *    kann beide Familien.  resolve() kann nur IPv4, weil es ein int32
 *    zurueckgibt - see domain.c, resolve_sa() ist genau deswegen da.
 *  2 build_sockaddr_host() ist der getaddrinfo()-Weg.  Damit ist die
 *    TCPDIR-Zuordnung nicht mehr die einzige Sonderbehandlung, sondern nur
 *    noch die erste - was sonst eine Routen-IP aus dem Systemresolver
 *    bekommen haette, wenn der Name zufuell auch in TCPDIR/hosts stand.
 *  3 build_sockaddr_host() noch einmal, fuer die Literale und fuer Namen, die
 *    nicht in TCPDIR/hosts stehen - resolve_sa() liest nur die eigene Tabelle
 *    und kennt keinen Nameserver.  Die Klammern [name] kann inzwischen
 *    resolve_sa() selbst, seit domain.c sie abstreift; dieser Weg bleibt
 *    trotzdem noetig, weil er der einzige ist, der DNS fragt.
 *
 * "axip route add" und der Timer fragen auf genau diesem Weg, weil zwei
 * Aufloeser mit derselben Frage sich irgendwann unterschiedlich verhalten -
 * einmal IPv4, einmal IPv6, je nachdem, welchen der Aufrufer zufaellig
 * genommen haette.
 *
 * DAS ERGEBNIS LIEGT IM AUFRUFER, nicht in einem statischen Puffer hier.
 * build_sockaddr_host() gibt zwar selbst statischen Speicher zurueck, und
 * den darf man nicht weiterreichen - der wird beim naechsten Aufruf
 * ueberschrieben, auch von resolve() weiter oben.  Der Aufrufer hat also
 * seinen eigenen sockaddr_storage, und der bleibt auch dann gueltig, wenn
 * zwischen hier und dem Benutzen noch ein zweiter Name gefragt wurde.
 *
 * BLOCKIEREND, und das ist hier richtig: der Aufrufer ist entweder ein
 * Sysop-Befehl oder eine Minute Arbeit im Timer.  Fuer den Sendeweg ist es
 * das nicht - dort wird nur gefragt, wenn es faellig ist, und der erste
 * Versuch stempelt rtime, sodass nicht jedes Paket dasselbe wiederholt.
 */
static int axip_resolve_host(
const char *name,
int port,
struct sockaddr_storage *out,
socklen_t *lenp)
{
  struct sockaddr_storage ss;
  struct sockaddr *sa;
  socklen_t len;
  int l;

  if (!name || !*name || !out) return 0;
  memset(&ss, 0, sizeof(ss));
  len = 0;
  if (resolve_sa(name, &ss, &len) && len) {
    /* resolve_sa() haengt kein Portfeld an, und der ist hier nicht
     * kosmetisch: die axudp-Ports werden ueber sin_port ausgehandelt.
     */
    sockaddr_set_port((struct sockaddr *) &ss, port);
  } else if ((sa = build_sockaddr_host(name, port, &l)) != NULL && l > 0) {
    memset(&ss, 0, sizeof(ss));
    len = (socklen_t) l;
    memcpy(&ss, sa, (size_t) len > sizeof(ss) ? sizeof(ss) : (size_t) len);
  } else {
    return 0;
  }
  memcpy(out, &ss, sizeof(*out));
  if (lenp) *lenp = len;
  return 1;
}

/* Ist das eine Adresse und kein Name?  Nur die eine Stelle unterscheidet die
 * beiden, und zwar fuer eine einzige Frage: nur ein NAME wird gemerkt und
 * nachgefragt.  Eine Literale einzutragen ist sinnvoll - sie ist nicht nur
 * schneller, sie ist der einzige Weg fuer eine Adresse, die es nicht in
 * DNS oder TCPDIR/hosts gibt.
 *
 * Die Pruefung fragt build_sockaddr_host() und schaut sich dann an, ob es
 * eine Literal-ADRESSE zurueckbekommen hat - nicht, ob es irgendetwas
 * zurueckbekommen hat.  Denn "es liess sich aufloesen" ist bei einem Namen
 * genau das, was hier NICHT die Antwort ist.
 *
 * Wichtig ist die Reihenfolge "erst fragen, dann entscheiden": ein Name,
 * der sich nicht aufloesen laesst, ist an dieser Stelle ein Name, sonst
 * wuerde ein Tippfehler als Adresse gespeichert und nie wieder aufgeloest.
 */
static int axip_is_literal(
const char *host)
{
  struct sockaddr_storage ss;
  char abuf[SOCKADDR_STRLEN];
  struct sockaddr *sa;
  socklen_t len;
  int l;

  if (!host || !*host) return 0;
  /* Ein Doppelpunkt kann hier nur IPv6 bedeuten: build_sockaddr_host()
   * braucht fuer IPv6 die Klammern, und die Frage stellt es sich nicht -
   * "2001:db8::1" ist genau so eine Adresse wie "127.0.0.1".
   *
   * NACH DEN KLAMMERN, nicht in ihnen: "[2001:db8::1]" ist die Adresse, und
   * "[db0sao.ampr.org]" ein Name, den man nachfragen will.  Ohne das
   * Nachsehen stuft die Zeile jeden geklammerten Eintrag als Adresse ein und
   * der Name wird nie aufgeloest - ein Eintrag, der still dasteht und
   * niemals eine Route traegt.
   */
  {
    char hbuf[1024];
    char sbuf[32];
    int fam;

    /* Scheitert der Aufteiler, bleibt der Originaltext stehen: eine nackte
     * IPv6-Literal ist mehrdeutig und wird deshalb abgewiesen - fuer diese
     * Frage ist sie genau so eine Adresse wie "127.0.0.1", denn der Doppelpunkt
     * steht hier ohnehin fuer IPv6.
     */
    if (!build_hostport(host, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), &fam)) {
      if (*sbuf) return 0;	/* "host:port" - a service, not a host */
      host = hbuf;
    }
  }
  if (strchr(host, ':')) return 1;
  memset(&ss, 0, sizeof(ss));
  if (!(sa = build_sockaddr_host(host, 0, &l)) || l <= 0) return 0;
  memcpy(&ss, sa, (size_t) l > sizeof(ss) ? sizeof(ss) : (size_t) l);
  /* Die zurueckgegebene Zeichenkette mit der EINGEGEBENEN vergleichen, nicht
   * darauf schauen, ob sie ueberhaupt eine Adresse aussieht: "localhost" ist
   * eine gueltige Antwort und trotzdem ein Name, den man nachfragen will.
   */
  sockaddr_to_string((struct sockaddr *) &ss, abuf, sizeof(abuf));
  return strchr(abuf, ':') != NULL || strspn(abuf, "0123456789.") == strlen(abuf);
}

/*---------------------------------------------------------------------------*/

/* EINEN EINTRAG NACHFRAGEN.  Nur benannte Eintraege ueberhaupt, und die
 * Mindestzeit zwischen zwei Fragen gilt auch hier: rtime wird auch im
 * Fehlerfall gesetzt, weil ein Name, der nicht aufgeloest werden kann,
 * sonst bei jedem Paket erneut im Nameserver steht.
 *
 * Zurueckgeben, ob sich die Adresse bewegt hat: der Portverfall in
 * axip_route_add() haengt daran, und der Zaehler des Timers braucht es,
 * weil er sonst seine eigene Traegheit mitzaehlt.
 */
static int axip_lookup_one(
struct axip_route *rp,
int interval)
{
  struct sockaddr_storage ss;
  socklen_t len;
  int moved;
  time_t now;

  if (!rp->name) return 0;
  if (!interval) return 0;

  now = secclock();
  /* rtime == 0 heisst "nie gefragt": das ist faellig, sonst frage eine
   * frisch angelegte Route nie.
   */
  if (rp->rtime && rp->rtime + interval > now) return 0;

  /* DIE FRAGESTELLE VOR DER ADRESSE.  Nach dem Versuch, nicht davor: ein
   * Eintrag darf auch fragen, wenn wir es ihm gerade erst beigebracht haben
   * - das ist der Fall, der auftritt, wenn "axip route add" mit einem Namen
   * gelaufen ist, den es da noch nicht gab.
   */
  Axip_lookups++;
  rp->rtime = now;
  if (!axip_resolve_host(rp->name, sockaddr_port((struct sockaddr *) &rp->dest), &ss, &len)) {
    Axip_lookupfail++;
    return 0;
  }
  /* Die alte Adresse vor dem Ueberschreiben vergleichen - der Portverfall
   * in axip_route_add() haengt an dieser Frage.
   */
  moved = !sockaddr_addr_eq((struct sockaddr *) &ss, (struct sockaddr *) &rp->dest);
  if (moved)
    axip_route_add(rp->call, (struct sockaddr *) &ss, 1, AXIP_FROM_RESOLVE, NULL);
  return moved;
}

/* DIE BEIDEN VORGABEN FUER EINEN PORT, in Sekunden.  "0 am Port" heisst
 * Knotenwert, und 0 am Knoten heisst beim Intervall: gar nicht.
 *
 * ZWEI FUNKTIONEN statt einer mit zwei Ausgabezeigern, und der Grund ist ein
 * Absturz: der Sendeweg braucht nur das Intervall und gab dem einen Zeiger
 * NULL mit, und die Ein-Funktions-Form schrieb trotzdem durch beide.  Ein
 * Zeiger auf nichts ist beim Aufruf richtig und beim Schreiben trotzdem
 * falsch, und ein Aufrufer, der sich das vorstellt, ist kein Aufrufer, dem
 * man traut.  Wer hier einen Wert zurueckgibt, kann nichts vergessen.
 */
static int axip_interval(struct iface *ifp)
{
  if (ifp && ifp->axip_dns_interval) return ifp->axip_dns_interval * 60;
  return Axip_dns_interval * 60;
}

static int axip_silence(struct iface *ifp)
{
  if (ifp && ifp->axip_dns_silence) return ifp->axip_dns_silence * 60;
  return Axip_dns_silence * 60;
}

/* DEN NAMEN EINER ROUTE FRAGEN, wenn er faellig ist.  Aufrufer: der Timer,
 * und der Sendeweg vor der Adresswahl.
 *
 * DIE STILLE-FRIST STEHT NICHT HIER, sondern in axip_timer() fuer alle
 * Eintraege auf einmal: sie ist eine Frage des Betriebs und nicht der Route.
 * Der Sendeweg fragt ausdruecklich nach, weil ein Paket, das jetzt dasteht,
 * die Frage "ist der Name richtig" beantwortet - siehe axip_raw().
 */
static int axip_lookup(
uint8 *call,
struct iface *ifp)
{
  struct axip_route *rp;
  int interval;

  interval = axip_interval(ifp);
  for (rp = Axip_routes; rp; rp = rp->next)
    if (axip_call_match(rp->call, call))
      return axip_lookup_one(rp, interval);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* DER TIMER.  Alle 60 Sekunden einmal, und in einem Takt hoechstens
 * AXIP_LOOKUPS_PER_TICK Namen.
 *
 * ZWEI FRAGEN IN EINEM TAKT, und die Reihenfolge ist nicht fair, sondern
 * einfach: die Liste ist eine Kette.  Wer im Wettbewerb um einen Platz im
 * Takt leer ausgeht, ist im naechsten wieder dran, weil sein rtime nicht
 * weitergewandert ist - sonst wuerde die Liste hier ab einer Stelle nie
 * wieder drankommen, und das ist genau der Fehler, den eine feste
 * Rundreihenliste nicht macht.
 */
static void axip_timer(
void *arg)
{
  static struct timer tmr;
  struct axip_route *rp;
  struct iface *ifp;
  time_t now;
  int interval;
  int silence;
  int lookups = 0;

  switch (tmr.state) {
  case TIMER_STOP:
    tmr.func = axip_timer;
    tmr.arg = 0;
    set_timer(&tmr, AXIP_TICK);
    start_timer(&tmr);
    return;
  case TIMER_RUN:
    return;
  case TIMER_EXPIRE:
    tmr.state = TIMER_STOP;
    break;
  }

  now = secclock();
  for (rp = Axip_routes; rp && lookups < AXIP_LOOKUPS_PER_TICK; rp = rp->next) {
    /* Ohne Namen gibt es nichts zu fragen, und ein Eintrag, den kein axip-
     * Port mehr benutzt, schon gar nicht.
     */
    if (!rp->name) continue;
    /* Die Vorgaben gehoeren zum Port, ueber den die Route laeuft.  Ist der
     * Port inzwischen weg, faellt die Route fuer den Timer einfach weg -
     * ein Port, den es nicht mehr gibt, ist kein Nachbar mehr, und eine
     * Frage nach dem Namen waere dann eine Frage ins Leere.  (Ein gebundener
     * Port ist uebrigens nicht weg: die Nummer bleibt fuer immer vergeben,
     * nur der Port selbst verschwindet - axip_ifp_to_rdev() in iface.c.)
     */
    ifp = rp->rdev ? axip_iface(rp->rdev) : NULL;
    if (rp->rdev && !ifp) continue;
    interval = axip_interval(ifp);
    silence = axip_silence(ifp);
    if (!interval) continue;
    if (rp->rtime && rp->rtime + interval > now) continue;
    /* DIE STILLE, IN BEIDEN RICHTUNGEN.  htime allein genuegt nicht: auf
     * einer Frequenz, wo nur die eine Station uns erreicht, sieht jeder
     * Eintrag aktiv aus, und der mit dem toten Namen im Hintergrund fragt
     * ewig weiter (Thomas).  max() ist damit der letzte Verkehr auf dem
     * Weg in beide Richtungen, und die Frist darueber - nicht darunter,
     * ein Neuzuhaeler soll nicht erst eine Stunde warten, bis sein Name
     * ueberhaupt geprueft wird.
     *
     * qdue dagegen heisst "die Adresse ist falsch, wir haben es gesehen",
     * und darauf wartet man nicht: das wuerde die Nachfrage vertagen, bis
     * jemand wieder frage - und der fragt niemand.
     */
    if (silence && !rp->qdue) {
      time_t last = rp->htime > rp->stime ? rp->htime : rp->stime;

      if (last && last + silence > now) continue;
    }
    lookups += axip_lookup_one(rp, interval);
  }
  axip_timer(0);
}

/*---------------------------------------------------------------------------*/

/* GLEICHES ZIEL - ADRESSE UND PORT.  Beides gehoert dazu, und der Port ist
 * nicht das Beiwerk: ueber axudp teilen sich zwei Partner dieselbe Adresse und
 * unterscheiden sich NUR am Port, das ist der ganze Sinn von "axip route add
 * <call> <ip> <port>" mit eigener Portangabe.  Wer hier nur die Adresse
 * vergleicht, glaubt zwei Partner zu sehen, wo einer ist - und schickt
 * genau einem von ihnen nichts.  Umgekehrt sind zwei Routen auf denselben
 * Host mit DEMSELBEN Port derselbe Partner, und der soll einmal drankommen.
 *
 * Ausdruecklich nach Familie verglichen statt mit memcmp ueber die
 * sockaddr: die Fuellbytes hinter der Adresse sind nicht alle Null, und ein
 * memcmp wuerde zwei gleiche Ziele je nach Herkunft unterscheiden.
 * Nur zum Vergleich benutzt, nie zum Speichern.
 */
static int axip_same_target(const struct sockaddr *a, const struct sockaddr *b)
{
	if (a->sa_family != b->sa_family) return 0;
	if (sockaddr_port(a) != sockaddr_port(b)) return 0;
	switch (a->sa_family) {
	case AF_INET: {
		const struct sockaddr_in *x = (const struct sockaddr_in *) a;
		const struct sockaddr_in *y = (const struct sockaddr_in *) b;

		return x->sin_addr.s_addr == y->sin_addr.s_addr;
	}
#if HAS_AF_INET6
	case AF_INET6: {
		const struct sockaddr_in6 *x = (const struct sockaddr_in6 *) a;
		const struct sockaddr_in6 *y = (const struct sockaddr_in6 *) b;

		return IN6_ARE_ADDR_EQUAL(&x->sin6_addr, &y->sin6_addr);
	}
#endif
	default:
		return 0;
	}
}

/* STEHT VOR rp SCHON EIN EINTRAG AUF DASSELBE ZIEL?  Die Liste enthaelt
 * beides nebeneinander: die gelernte Route auf den Partner und die
 * Default-Route, die auf denselben Host mit demselben Port zeigt.  Ohne
 * diese Frage bekaeme der Partner jeden Rundspruk zweimal - und zweimal ist
 * schlimmer als einmal, weil der Empfaenger zwei Kopien nicht von einem Frame
 * unterscheiden kann und der Absender auch nicht.
 */
static int axip_route_preceded_by_same_target(struct axip_route *rp)
{
	struct axip_route *q;

	for (q = Axip_routes; q && q != rp; q = q->next)
		if (axip_same_target((struct sockaddr *) &q->dest,
		                     (struct sockaddr *) &rp->dest))
			return 1;
	return 0;
}

/* DAS ZIEL EINER ROUTE, FERTIG FUER sendto().  Der Port ist nicht einfach
 * der aus der Route: ueber UDP kann der gelernte oder der NAT-Port der
 * sein, und der steht nur hier.  An EINER Stelle, weil ein Rundspruk und
 * ein Unicast nicht daran variousieren duerfen, wie dieselbe Route
 * aufgeloest wird - sonst gaenge ein und derselbe Partner je nach
 * Rahmenlage an zwei verschiedene Orte.
 */
static int axip_route_target(struct axip_route *rp, struct edv_t *edv,
                             struct sockaddr_storage *to, int *port)
{
	struct sockaddr *sa;

	*port = sockaddr_port((struct sockaddr *) &rp->dest);
	if (!*port) *port = edv->dport;
	*to = rp->dest;
	if (edv->type == USE_UDP) {
		/* Das Rufzeichen zuerst, der Host als Rueckfall: nur so bekommen
		 * zwei Stationen hinter einer Adresse ihre eigene Antwort.  Fuer
		 * den einen Knoten hinter einer Adresse sagen beide dasselbe.
		 */
		int lp = axip_learned_port(rp, edv);

		if (lp) {
			*port = lp;
		} else {
			sa = search_udp_host_nat_port((struct sockaddr *) to, edv);
			if (sa) *port = sockaddr_port(sa);
		}
		uhnp_cleanup(edv);
	}
	sockaddr_set_port((struct sockaddr *) to, *port);
	return 0;
}

/* IST DAS EIN RUNDSPRUK?
 *
 * Eigene Funktion, und der Grund steht hier, weil er sonst niemand findet:
 * als Schleife IN axip_raw() war sie an zwei Stellen zugleich sicher vor
 * einem Optimierer und doch nicht.  clang 17 loescht dort den Test von
 * (*mpp)[0] UND den "break" und laeuft ueber Ax25multi[] hinaus in
 * fremden Speicher - Segfault beim Senden, bei jedem axudp-Paket.  -O0 ist
 * richtig, ab -O1 bis -Oz ist es falsch.
 *
 * AUSGELOEST WIRD ES VON axip_lookup(): der Aufruf steht zwischen dieser
 * Schleife und der Verwendung von multicast, wird inline gesetzt, und danach
 * ist die Schleife entartet.  Nimmt man genau diese eine Zeile heraus, ist
 * der Maschinencode richtig - mit ihr ist er falsch.  Reproduzierbar in
 * beiden Richtungen.
 *
 * EINE EXPLIZITE SCHRANKE HILFT NICHT.  Das wurde probiert, mit
 * "mpp < Ax25multi + N &&" in allen sieben Schleifen des Baums: der Test
 * bleibt weg, die Schleife bleibt offen.  Wer das noch einmal versuchen
 * will, spare sich die Stunde - der Optimierer behauptet hier etwas ueber
 * die Erreichbarkeit, das nicht stimmt, und eine Schranke im Quelltext
 * widerlegt ihn nicht.
 *
 * In einer eigenen Funktion ist die Schleife eine eigene Einheit und wird
 * nicht mehr in axip_raw() hineingezogen.  Nebeneffekt, aber der wichtigere:
 * "ist das ein Rundspruk?" ist damit eine Frage mit einem Namen, die man
 * aufrufen kann, statt eines Blockes, den man beim Lesen von axip_raw()
 * uebersieht.
 */
static AXIP_NOINLINE int axip_is_multicast(const uint8 *dest)
{
  uint8 (*mpp)[AXALEN];

  for (mpp = Ax25multi; (*mpp)[0]; mpp++)
    if (addreq(dest, *mpp)) return 1;
  return 0;
}

static int axip_raw(struct iface *ifp, struct mbuf **bpp)
{

  int l;
  int multicast;
  int ndigi;
  int thisdev;
  struct axip_route *rp;
  struct edv_t *edv;
  uint8 buf[MAX_FRAME];
  uint8 *dest;
  uint8 *p;

  dump(ifp, IF_TRACE_OUT, *bpp);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();

  edv = (struct edv_t *) ifp->edv;
  thisdev = axip_ifp_to_rdev(ifp);

  append_crc_ccitt(*bpp);

  if (ifp->trace & IF_TRACE_RAW)
    raw_dump(ifp, -1, *bpp);

  l = pullup(bpp, buf, sizeof(buf));
  if (l <= 0 || *bpp) {
    free_p(bpp);
    return -1;
  }

  /* Loop-Schutz: eigene UI-Aussendung merken - nur die Bytes ohne CRC,
   * denn der Empfangspfad verwirft die zwei Pruefbytes erst und prueft
   * dann.  Ein byte-identischer Rueckkehrer auf demselben Datagramm-Port
   * ist unser Echo (ein Loopback-Tool oder eine Gegenstelle, die eben
   * zurueckwirft) und darf nicht als neues Rahmenstueck erscheinen, nicht
   * getraced und schon gar nicht erneut weitergegeben werden.  Connected-
   * mode (I/RR+/SABM) wird nie gemerkt: dessen identische Wiederholung ist
   * Protokoll-Timing.
   */
  if (l > 2 && ax25_frame_is_ui(buf, l - 2))
    ax_dup_remember(ax_fingerprint_data(buf, l - 2));

  /* Walk the AX.25 address field to find the immediate destination.  Bound
   * the walk against the end of the frame and against MAXDIGIS: a frame whose
   * addresses never carry the E bit would otherwise run off the end of buf.
   */
  if (l < 2 * AXALEN)
    return -1;
  dest = buf;
  p = dest + AXALEN;
  for (ndigi = 0; !(p[6] & E); ndigi++) {
    if (ndigi >= MAXDIGIS || p + 2 * AXALEN > buf + l)
      return -1;
    p += AXALEN;
    if (!(p[6] & REPEATED)) {
      dest = p;
      break;
    }
  }

  /* Der Test darf hier nicht wieder als Schleife stehen - siehe
   * axip_is_multicast() oben.  Und er muss VOR axip_lookup() liegen, weil
   * er dann allerdings nicht mehr neben der Verwendung von multicast
   * steht, mit der einen Ausnahme: aufgerufen wird er hier, benutzt wird
   * multicast weiter unten.  Das ist genau der Aufbau, der clang nicht
   * moeglich ist - deshalb der eigene Funktionsname und nicht ein Block.
   */
  multicast = axip_is_multicast(dest);

  /* NACHFRAGEN, BEVOR DIE ADRESSE FESTSTEHT - und nur hier, nicht im Timer
   * allein.  Der Fall ist der eigentliche: ein Partner, von dem stundenlang
   * nichts kam, dessen Paket jetzt aber da liegt.  Der Timer hat ihn nach
   * der Stille-Frist absichtlich liegen lassen, und ohne diese Zeile ginge
   * das Paket an die alte Adresse, ohne dass je wieder nachgefragt wuerde -
   * "wir senden nichts, er sendet nicht, also aufhoeren mit resolving"
   * hiesse dann "wir senden nichts, er sendet nicht, also senden wir
   * scheinbar an eine Adresse, die es nicht mehr gibt".
   *
   * DIE STILLE-FRIST GILT FUER DEN VORSCHLAG, NICHT FUER DIE NACHRAGE: sie
   * beantwortet die Frage, ob wir ungefragt nachfragen sollen, und nicht
   * die Frage, ob die Adresse stimmt.  Die Mindestzeit zwischen zwei Fragen
   * gilt dagegen auch hier - axip_lookup_one() stempelt rtime, also fragt
   * nicht jedes Paket denselben Namen erneut.
   *
   * NACH dem Multicast-Test, weil der Aufruf vorher nicht hingehort: ein
   * Rundspruk ist eine Nachfrage nach allem, was hinter diesem Port steht,
   * und die gehoert genauso beantwortet - aber der Test, ob ueberhaupt ein
   * Rundspruk vorliegt, steht weiter oben und wird von hier nicht gebraucht.
   */

  /* NOT BUILT - but the shape is decided here so that building it later does
   * not mean guessing.  It covers axip and axudp, which share this function.
   *
   * WHY THERE IS NO ROOM FOR A REAL SECRET IN AN ADDRESS FIELD: an AX.25
   * source field is who you claim to be, and nothing more.  Whoever connects
   * can write any callsign into the first frame, so a code that travels in an
   * address field is not a credential - it is a filter, and it stops precisely
   * the people who never read the manual.  What the packet community has
   * settled on instead is a SHARED key: one code the operator hands to every
   * partner.  That is protection against misuse, not against an adversary,
   * and the code should be named for what it is.
   *
   * WHAT WOULD BE BUILT, in both directions.  The code lives IN THE INTERFACE
   * (ifp), one per axip/axudp/axtcp/kisstcp port - NOT in Ax25multi[], which is
   * the table of multicast destinations (QST-0, NODES-0) and has nothing to do
   * with partners.  Do not put the two together.
   *
   *   Incoming.  Two conditions together, and both are needed: an auth code is
   *   set on the interface, AND the interface has not yet accepted a packet.
   *   Then the destination field of that first packet is checked - and only
   *   that one packet, because after it the session is established.  The check
   *   is deliberately crude: the first six bytes of the destination field are
   *   the callsign, each shifted left by one, and the SSID sits in bits 1..4 of
   *   the seventh byte.  So it is exactly addreq(), and there is nothing to
   *   mask by hand: addreq() already drops bit 0 (E, end of address) and bit 7
   *   (C/R, command or repeated) with SSID == 0x1E, and compares only what names
   *   the station.  Do not "fix" a comparison here that addreq() does not need
   *   fixing - a byte-for-byte memcmp() would be the actual bug.  If it matches,
   *   the code becomes the alias the peer is known by, and learning and the
   *   route table take it from there as any other address.
   *
   *   Outgoing, to the configured default IP.  Exactly ONE empty UI frame:
   *   source the interface's own call (ifp->hwaddr, already filled in from
   *   ax25 mycall), destination the code.  Once per session, not per tick.
   *   That it can only go to the default IP is the whole reason this is
   *   awkward: over axip/axudp we do not know which peer sits behind which
   *   MAC, so the first frame has no better address than "the one we send to
   *   anyway".  An empty UI frame is the right shape for it - it is the
   *   standard AX.25 link probe, so the far end sees something it recognises
   *   rather than a stranger - and it must stay distinguishable from the
   *   keepalive, which addresses nobody and must not count as an answer.
   *
   * WHY THE CODE IS CONFIGURED RATHER THAN DERIVED: the peer normally hands
   * the same code to everyone he serves.  So "the alias from our own CONFIG
   * line" is the wrong model - there is no per-peer alias to take, there is
   * one string both ends agreed on out of band.
   *
   * WHERE THE CONFIGURATION BELONGS: on attach, not on ifconfig.  A code
   * means nothing on an asy port, a netrom port or a digipeater, so an
   * ifconfig subcommand would have to reject every interface type it does not
   * apply to.  attach is where the port type is already being named, so the
   * set of interfaces that can carry a code is exactly the set that asked for
   * one.  OPEN: the exact wording of the attach argument.
   */
  axip_lookup(dest, ifp);

  {
    struct axip_route *best = NULL;
    /* DIE VIER STUFEN, und die Zahlen sagen, wie gut sie sind: 2 ist eine
     * bekannte Adresse auf diesem Port, 1 dieselbe Adresse irgendwo, 0 die
     * Default-Adresse auf diesem Port, -1 die Default-Adresse irgendwo.
     * Ein Aufruf sucht das Beste, das es gibt - nicht das erste, und nicht
     * alle.  fuer einen UNICAST.  Ein Rundspruk nimmt sie nicht, siehe unten.
     */
    int best_prio = -2;

    if (multicast) {
      /* EIN RUNDSPRUK GEHT AN ALLE, DIE HINTER DIESEM PORT LIEGEN.
       *
       * Das ist eine Liste und keine Rangliste, und deshalb steht dieser
       * Zweig neben der Schleife unten und nicht in ihr.  "Welche Route
       * gewinnt" ist bei einem Rundspruk die falsche Frage: sein Zweck ist,
       * alle zu erreichen, und eine Bestroute erreicht genau einen.  In HEAD
       * war das richtig - dort stand kein "break", also bekam jede passende
       * Route ein sendto().  Beim Umbau auf die vier Stufen ist das
       * verlorengegangen und durch "genau eine" ersetzt worden, und das ist
       * fuer QST-0 ein Verlust: ein Rundspruk, der nur einen Partner erreicht,
       * ist kein Rundspruk.
       *
       * NUR DIESER PORT.  Ob ein Rundspruk ueberhaupt wiederholt wird und
       * wohin, entscheidet die AX.25-Schicht darueber - der Weg dafuer ist der
       * Digi-Pfad in ax25.c, und der gehoert nicht hierher.  Hier steht nur,
       * wohin dieser Port sendet, wenn er sendet.  In HEAD war das nicht so:
       * dort lief ein Rundspruk ueber die Routen ALLER Ports und war damit
       * ein Rahmenverstaerker ueber die ganze Node.
       *
       * UND OHNE DOPPELTE KOPIE, siehe axip_route_preceded_by_same_target().
       */
      for (rp = Axip_routes; rp; rp = rp->next) {
        struct sockaddr_storage to;
        int port;

        if (rp->dest.ss_family != (sa_family_t) edv->family) continue;
        if (rp->rdev && rp->rdev != thisdev) continue;
        if (!rp->rdev) rp->rdev = thisdev;
        if (axip_route_preceded_by_same_target(rp)) continue;
        if (axip_route_target(rp, edv, &to, &port) < 0) continue;
        rp->stime = secclock();
        sendto(edv->fd, (char *) buf, l, 0, (struct sockaddr *) &to,
               sockaddr_len((struct sockaddr *) &to));
      }
    } else {
      for (rp = Axip_routes; rp; rp = rp->next) {
        int prio;
        /* DIE FAMILIE GEHOERT zur Wahl: eine IPv6-Route auf einem IPv4-Port
         * ist nicht "schlechter", sie ist unbrauchbar.
         */
        if (rp->dest.ss_family != (sa_family_t) edv->family) continue;
        if (!rp->is_default && !axip_call_match(rp->call, dest)) continue;
        if (rp->is_default)
          prio = (rp->rdev && rp->rdev == thisdev) ? 0 : -1;
        else
          prio = (rp->rdev && rp->rdev == thisdev) ? 2 : 1;
        if (prio > best_prio) {
          best = rp;
          best_prio = prio;
          if (prio == 2) break; /* best possible */
        }
      }
      if (best) {
        struct sockaddr_storage to;
        int port;

        rp = best;
        /* EIN GEBUNDENER EINTRAG AUF EINEM ANDEREN PORT IST NICHT KANDIDAT.
         * Er wurde an diesen Port geschrieben, und ihn hier umzubiegen waere
         * eine zweite, stillere Wahrheit: der Sysop haette eine Route, die er
         * nicht geschrieben hat, und "axip route" zeigte ihr einen anderen
         * Port als den, ueber den sie laeuft (Thomas).
         */
        if (rp->rdev && rp->rdev != thisdev) return l;
        if (!rp->rdev) rp->rdev = thisdev;
        if (axip_route_target(rp, edv, &to, &port) < 0) return l;
        rp->stime = secclock();
        sendto(edv->fd, (char *) buf, l, 0, (struct sockaddr *) &to,
               sockaddr_len((struct sockaddr *) &to));
      }
    }
  }

  return l;
}

/*---------------------------------------------------------------------------*/

static void axip_recv(void *argp)
{

  socklen_t addrlen;
  int hdr_len;
  int l;
  int ndigi;
  struct axip_route *rp;
  struct edv_t *edv;
  struct iface *ifp;
  struct ip *ipptr;
  struct mbuf *bp;
  struct sockaddr_storage addr;
  uint8 buf[MAX_FRAME];
  uint8 *bufptr;
  uint8 *p;
  uint8 *src;
  int trust_port;

  ifp = (struct iface *) argp;
  edv = (struct edv_t *) ifp->edv;
  addrlen = sizeof(addr);
  l = recvfrom(edv->fd, (char *) (bufptr = buf), sizeof(buf), 0, (struct sockaddr *) &addr, &addrlen);
  if (edv->type == USE_IP) {
    /* cast: l is int, and recvfrom() returns -1 on error.  Comparing against
     * an unsigned sizeof would convert that -1 to SIZE_MAX and pass. */
    if (l <= (int) sizeof(struct ip)) goto Fail;
    ipptr = (struct ip *) bufptr;
    hdr_len = 4 * ipptr->ip_hl;
    bufptr += hdr_len;
    l -= hdr_len;
  }
  if (l <= 2) goto Fail;

  if (!check_crc_ccitt((char *) bufptr, l)) goto Fail;
  l -= 2;

  /* Loop-Schutz: unser eigenes Echo - nur UI, und nur wenn derselbe Rahmen,
   * den wir selbst gerade auf diesem Datagramm-Port weggegeben haben,
   * byte-identisch zurueckkommt (ein Loopback-Tool anstelle einer
   * Gegenstelle, oder eine Gegenstelle, die eben zurueckwirft).  Still
   * verwerfen - vor dem Trace, vor dem Lernen und vor dem Weitergeben.
   * Wuerde er durchlaufen, erschiene er als "recv" und (wenn unser
   * Rufzeichen im Pfad steht) ein zweites Mal gesendet.  Connected-mode
   * identische Wiederholungen (RR+/I/SABM) sind nie ein Echo, sondern
   * Protokoll-Timing.
   */
  if (ax25_frame_is_ui(bufptr, l) &&
      ax_dup_recent(ax_fingerprint_data(bufptr, l))) {
    Ax_echoes++;
    return;
  }

  /* secure-port model of trust: src address adaption, but only
     - if my listen port >= 1024,
     - or if my listen port < 1024 and src port is also < 1024
   * Der Merker haelt das Ergebnis fest, weil dasselbe Vertrauen auch fuer den
   * Port je Rufzeichen gilt - das Rufzeichen steht aber erst nach dem
   * Adressfeld fest, die Bedingung gehoert hierher.
   */
  trust_port = (edv->type == USE_UDP &&
        (edv->port >= 1024 || sockaddr_port((struct sockaddr *) &addr) < 1024));
  if (trust_port) {
    learn_udp_host_nat_port((struct sockaddr *) &addr, edv);
    uhnp_cleanup(edv);
  }

  /* Walk the AX.25 address field to find the immediate source.  Bound the
   * walk against the end of the datagram and against MAXDIGIS.  Without this
   * a datagram whose addresses never carry the E bit walks off the end of
   * buf, and src ends up pointing at stack memory that axip_route_add() would
   * then copy into the AX.25 routing table.
   */
  if (l < 2 * AXALEN) goto Fail;
  p = src = bufptr + AXALEN;
  for (ndigi = 0; !(p[6] & E); ndigi++) {
    if (ndigi >= MAXDIGIS || p + 2 * AXALEN > bufptr + l) goto Fail;
    p += AXALEN;
    if (p[6] & REPEATED)
      src = p;
    else
      break;
  }

  /* DER DIREKTE PARTNER, UND NUR DER.  src ist die Quelle, wenn zwischen ihr
   * und uns kein Digipeater liegt, sonst der letzte Digipeater vor uns mit
   * gesetztem R-Bit.  Das ist der, der uns auf dieser Frequenz erreicht
   * hat - und genau der gehoert in die axip-Tabelle, weil genau er den
   * Tunnel haelt.  Wer weiter vorn im Pfad stand, steht noch hinter dem
   * Digipeater, und der ist wieder dafuer zustaendig, wer noch weiter vorn
   * steht: das ist die Aufgabe von ax25 route, nicht von hier (Thomas).
   *
   * Auf der SENDESEITE ist es der erste OHNE R-Bit, und das ist ein
   * anderes Feld: das ist der Sprung, ueber den wir diesen Rahmen
   * hinausgeben, nicht jemand, den wir kennenlernen.
   */

  /* GELERNT ODER VERWORFEN - hier ist die einzige Stelle, an der das
   * entschieden wird, und axip_route_add() sagt es zurueck.  Die Frage ist
   * eine doppelte, und beide Teile sind noetig:
   *
   *  - DIESE ADRESSE, UND NICHT EINE ANDERE?  Sonst schiebt eine Station, die
   *    mit der falschen Adresse aufgetaucht ist, die Route um - und weil die
   *    Tabelle global ist, auch fuer alle anderen Ports, die dieses
   *    Rufzeichen ueberhaupt benutzen.  Der Sysop haette dann eine Route, die
   *    er nie geschrieben hat, und die nach dem Zufall wieder anders heisst.
   *  - UND DARF HIER GELERNT WERDEN?  "axip-learn off" heisst: auf diesem
   *    Port kommen nur die Rufzeichen herein, die der Sysop eingetragen hat -
   *    und das ist bei 93 Usern in einer Richtung und eigener Infrastruktur in
   *    der anderen genau der Unterschied (Thomas).
   */
  if (!axip_route_add(src, (struct sockaddr *) &addr, 0, AXIP_FROM_LEARNED, ifp))
    goto Dropped;

  /* Der Quellport wird auch dann gemerkt, wenn die Adresse gleich war - es
   * ist die NAT-Zuordnung dieses Rufzeichens, und die gehoert nicht davon ab,
   * ob der Eintrag eben neu geschrieben wurde.
   */
  if (trust_port)
    axip_learn_port(src, (struct sockaddr *) &addr, edv);

  /* WIR HABEN ES GEHOERT, und zwar an der Adresse, die auch die Rueckrichtung
   * benutzt.  Deshalb htime an alle Eintraege dieser Adresse, nicht nur an den,
   * den dieser Rahmen getroffen hat: ein hinter einer Adresse liegendes
   * Rufzeichen, dessen eigener Eintrag unveraendert stehen bleibt, waere nach
   * der Stille-Frist "still", obwohl gerade gesprochen wurde.  Die Familie
   * steht mit im Vergleich - ein 127.0.0.1 und ein ::1 sind nicht dasselbe.
   */
  for (rp = Axip_routes; rp; rp = rp->next)
    if (sockaddr_addr_eq((struct sockaddr *) &rp->dest, (struct sockaddr *) &addr))
      rp->htime = secclock();

  bp = qdata(bufptr, l);
  net_route(ifp, &bp);
  return;

Dropped:
  Axip_dropped++;
  /* Der Rahmen wird nicht weitergegeben, aber verworfen ist nicht crcerrors:
   * die Pruefsumme war gut, das Paket war echt, und die Diagnose soll den
   * Unterschied sagen - "crc errors 40000" auf einem Port, an dem nur
   * jemand sein Rufzeichen schuetzt, sieht nach Kabel aus.
   *
   * Freigegeben wird hier nichts: bp gibt es noch nicht, der Rahmen liegt
   * noch in buf.
   */
Fail:
  ifp->crcerrors++;
}

/*---------------------------------------------------------------------------*/

int axip_attach(int argc, char *argv[], void *p)
{

  char *ifname = "axip";
  char *bindhost = 0;
  char *av[8];
  int ac = 0;
  int i;
  int fd;
  int family = AF_INET;
  int port = AX25_PTCL;
  int dport = 0;
  int type = USE_IP;
  struct edv_t *edv;
  struct iface *ifp;
  struct sockaddr_storage addr;
  socklen_t addrlen;

  /* WHICH ADDRESS TO LISTEN ON, and the only word here that is not
   * positional - the rest is "each word needs the one before it", and a
   * setting that is usually left out cannot live at the end of such a chain.
   *
   * Left out it is every address, which is what it always was and what a node
   * on the air wants: peers turn up on whatever interface the routing gives
   * them.  Named, it is the one - "bind=127.0.0.1" or "bind=::1" for a node
   * that only talks to programs on the same machine, so the axudp port is not
   * reachable from outside without a firewall in front of it.
   */
  for (i = 0; i < argc && ac < (int) (sizeof(av) / sizeof(av[0])); i++) {
    if (!strncmp(argv[i], "bind=", 5)) {
      bindhost = argv[i] + 5;
      continue;
    }
    av[ac++] = argv[i];
  }
  argc = ac;
  argv = av;

  if (argc >= 2) ifname = argv[1];

  if (if_lookup(ifname) != NULL) {
    printf("Interface %s already exists\n", ifname);
    return -1;
  }

  /* "ip"/"udp" as before; a trailing 6 - "ip6", "udp6" - selects IPv6 for the
   * outer transport.  The encapsulated frame is untouched by that, so an IPv4
   * peer running ax25ipd or XNET sees no difference; an IPv6 peer needs
   * something that speaks it, which today means another WAMPES.
   */
  if (argc >= 3) {
    char *t = argv[2];
    int is6 = *t && t[strlen(t) - 1] == '6';

    switch (*t) {
    case 'I':
    case 'i':
      type = USE_IP;
      break;
    case 'U':
    case 'u':
      type = USE_UDP;
      break;
    default:
      printf("Type must be IP, UDP, IP6 or UDP6\n");
      return -1;
    }
    if (is6) {
#if HAS_AF_INET6
      family = AF_INET6;
#else
      printf("This build has no IPv6 support\n");
      return -1;
#endif
    }
  }

  /* One number means both, as it always did.  "<src>:<dst>" separates them:
   * the first is what we bind to, the second where we send when neither the
   * route nor a learned source port says otherwise.  Needed where the two
   * genuinely differ - behind a NAT that rewrites one of them, or when a
   * peer insists on talking to 93 while we may not bind a privileged port.
   *
   * Only for UDP.  With a raw socket the number is the IP protocol, there is
   * no port at either end, and a colon there would be nonsense rather than a
   * setting nobody uses.
   */
  if (argc >= 4) {
    char *colon = strchr(argv[3], ':');

    if (colon) {
      if (type != USE_UDP) {
        printf("\"%s\": with ip or ip6 the number is the IP protocol, and a "
               "raw\nsocket has no ports to keep apart\n", argv[3]);
        return -1;
      }
      *colon = '\0';
      dport = atoi(colon + 1);
      if (dport <= 0 || dport > 65535) {
        printf("\"%s\" is not a port\n", colon + 1);
        return -1;
      }
    }
    port = atoi(argv[3]);
    if (type == USE_UDP && (port <= 0 || port > 65535)) {
      printf("\"%s\" is not a port\n", argv[3]);
      return -1;
    }
  }
  if (!dport) dport = port;             /* one number means both */

  if (type == USE_IP)
    fd = socket(family, SOCK_RAW, port);
  else
    fd = socket(family, SOCK_DGRAM, 0);
  if (fd < 0) {
    printf("cannot create socket: %s\n", strerror(errno));
    return -1;
  }

#if HAS_AF_INET6
  if (family == AF_INET6) {
    int arg = 1;

    /* Pin this down rather than inheriting it: whether an IPv6 socket also
     * accepts IPv4 differs between Linux and the BSDs, and an axip and an
     * axip6 interface have to be able to hold the same port side by side. */
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &arg, sizeof(arg));
  }
#endif

  if (type == USE_UDP || bindhost != NULL) {
    memset(&addr, 0, sizeof(addr));
    if (bindhost != NULL) {
      struct sockaddr *sa;
      int len;

      /* A raw socket has no port; binding it to an address still says which
       * one we answer on, so the host is asked for with port 0 there.
       */
      if (!(sa = build_sockaddr_host(bindhost,
				     type == USE_UDP ? port : 0, &len))) {
	printf("cannot look up \"%s\"\n", bindhost);
	close(fd);
	return -1;
      }
      if (sa->sa_family != family) {
	printf("\"%s\" is not an address of the family this interface "
	       "speaks -\nuse \"udp6\"/\"ip6\" for an IPv6 address\n",
	       bindhost);
	close(fd);
	return -1;
      }
      memcpy(&addr, sa, (size_t) len);
      addrlen = (socklen_t) len;
    } else {
#if HAS_AF_INET6
    if (family == AF_INET6) {
      struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) &addr;

      s6->sin6_family = AF_INET6;
      s6->sin6_addr = in6addr_any;
      s6->sin6_port = htons(port);
    } else
#endif
    {
      struct sockaddr_in *si = (struct sockaddr_in *) &addr;

      si->sin_family = AF_INET;
      si->sin_addr.s_addr = INADDR_ANY;
      si->sin_port = htons(port);
    }
    addrlen = sockaddr_len((struct sockaddr *) &addr);
    }
    if (bind(fd, (struct sockaddr *) &addr, addrlen)) {
      printf("cannot bind address: %s\n", strerror(errno));
      close(fd);
      return -1;
    }
  }

  ifp = (struct iface *) callocw(1, sizeof(struct iface));
  ifp->name = strdup(ifname);
  ifp->addr = Ip_addr;
  ifp->broadcast = 0xffffffffUL;
  ifp->netmask = 0xffffffffUL;
  ifp->hwaddr = (uint8 *) mallocw(AXALEN);
  addrcp(ifp->hwaddr, Mycall);
  ifp->mtu = 256;
  ifp->crccontrol = CRC_CCITT;
  setencap(ifp, "AX25UI");
  ifp->user_to_user_ok = 1;

  edv = (struct edv_t *) malloc(sizeof(struct edv_t));
  edv->type = type;
  edv->port = port;
  edv->dport = dport;
  edv->fd = fd;
  edv->family = family;
  edv->uhnp = 0;
  edv->uhnp_time = secclock();
  ifp->edv = edv;

  ifp->raw = axip_raw;
  on_read(fd, axip_recv, (void * ) ifp);

  ifp->next = Ifaces;
  Ifaces = ifp;

  return 0;
}

/*---------------------------------------------------------------------------*/

/* keepport: der Sysop hat eine Portnummer in die Route geschrieben und meint
 *   es.  Gelernte Routen geben 0 und halten die alte Regel - der Port kommt
 *   vom Interface oder von dem, wo der Partner zuletzt gesehen wurde, und das
 *   ist eine Tabelle mit Frist.  Einen Augenblicks-Port einer Gegenstelle in
 *   eine Route zu nageln, die nicht ablaeuft, ist etwas anderes.
 *
 * from: AXIP_FROM_SYSOP, AXIP_FROM_RESOLVE oder AXIP_FROM_LEARNED - wer den
 *   Eintrag schreibt.  LEARNED heisst: das ist ein Frame aus dem Verkehr, und
 *   ein solcher Frame darf eine Route nur dann anfassen, wenn das
 *   ausdruecklich erlaubt ist.
 *
 * ifp: der Port, ueber den es passiert.  Nur bei LEARNED noetig, fuer die
 *   Frage nach "axip-learn"; der Rest laeuft mit NULL.
 *
 * ZURUECKGEBEN, OB DIE ADRESSE JETZT STIMMT.  Nur der Empfangsweg fragt das,
 * und nur dort, wo er den Rahmen sonst weitergeben wuerde - eine Fehlermeldung
 * an einen Aufrufer, der sie nicht ausgeben kann, waere kein Hinweis.
 *
 * Und: hier steht der perm-Schutz, und nicht in axip_recv().  Der Aufloeser
 * fasst denselben Eintrag an wie das Lernen aus dem Verkehr; eine Regel, die
 * nur in einem von beiden steht, laesst genau die andere Seite durch - und
 * das ist die Luecke, die hier geschlossen wird (Thomas).
 */

static int axip_route_add(uint8 *call, const struct sockaddr *dest,
	int keepport, int from, struct iface *ifp)
{
  struct axip_route *rp;
  socklen_t len = sockaddr_len(dest);

  if (!len) return 0;

  for (rp = Axip_routes; rp && !axip_call_match(rp->call, call); rp = rp->next) ;
  if (!rp) {
    /* LERNEN, WO ES VERBOTEN IST, LEGT AUCH KEINEN AN.  Ein Eintrag, den
     * niemand lesen kann, waere nur eine Zeile in "axip route", die man
     * wegdenken muss - und der naechste Frame wuerde ihn wieder erzeugen.
     */
    if (from == AXIP_FROM_LEARNED && !axip_may_learn(ifp)) {
      Axip_refused++;
      return 0;
    }
    if (!(rp = (struct axip_route *) malloc(sizeof(struct axip_route))))
      return 0;
    memset(rp, 0, sizeof(struct axip_route));
    addrcp(rp->call, call);
    /* "once" schuetzt den Eintrag vom ersten Frame an, und zwar auch dann,
     * wenn der Sysop die Adresse gar nicht selbst geschrieben hat: das ist
     * der ganze Sinn der Stufe - die Adresse, bei der wir das Rufzeichen zum
     * ersten Mal gesehen haben, ist die, fuer die es gilt (Thomas).
     */
    if (from == AXIP_FROM_LEARNED && ifp && ifp->axip_learn == AXIP_LEARN_ONCE)
      rp->perm_once = 1;
    rp->next = Axip_routes;
    Axip_routes = rp;
  } else if (!sockaddr_addr_eq((struct sockaddr *) &rp->dest, dest)) {
    /* EINE ANDERE ADRESSE.  Drei Faelle, und sie sind nicht gleich:
     *
     *  - Ein Sysop-Befehl darf das immer.  Das ist der Weg, auf dem man eine
     *    Station nach Hause schickt.
     *  - Der Aufloeser darf es nur bei einem, den er selbst benannt hat,
     *    denn er schreibt aus keinem Grund etwas anderes hin.  Sonst
     *    ueberschriebe er die Handeingabe - und das waere die Regel
     *    "haengt von der Reihenfolge des Aufrufers ab".
     *  - Ein Frame aus dem Verkehr darf es nur, wenn die Adresse nicht
     *    geschuetzt ist UND auf diesem Port gelernt werden darf.
     */
    if (from == AXIP_FROM_RESOLVE) {
      if (!rp->name) {
	Axip_refused++;
	return 0;
      }
    } else if (from == AXIP_FROM_LEARNED) {
      /* PERMANENT UND ONCE SIND GLEICH HART, und beide hart gegen den
       * Verkehr.  "permanent" heisst: der Sysop hat diese Adresse gesagt;
       * "once" heisst: bei dieser Adresse haben wir das Rufzeichen zum ersten
       * Mal gesehen.  In beiden Faellen hat der Verkehr nichts zu sagen - ein
       * Frame von einer anderen Adresse ist dann etwas, das man meldet, und
       * nicht etwas, das man uebernimmt.
       *
       * Und die Meldung gehoert in den Zaehler, nicht auf den Bildschirm:
       * das ist ein Ereignis, das alle zehn Sekunden passiert, wenn es
       * passiert.
       */
      if (rp->perm || rp->perm_once) {
	Axip_refused++;
	return 0;
      }
      if (!axip_may_learn(ifp)) {
	Axip_refused++;
	return 0;
      }
    }
    /* Der gelernte Port stirbt mit der Adresse, zu der er gehoerte - sonst
     * traegt eine umgezogene Station den Port ihres Vorgaengers weiter.  Der
     * Empfangsweg lernt ihn unmittelbar danach neu.
     */
    rp->lport = 0;
    rp->ledv = 0;
    rp->ltime = 0;
  }
  memset(&rp->dest, 0, sizeof(rp->dest));
  memcpy(&rp->dest, dest, (size_t) len);
  if (!keepport)
    sockaddr_set_port((struct sockaddr *) &rp->dest, 0);
  /* A port of zero - which is what a route written without one carries -
   * means the same as before: take the interface's, or whatever the peer was
   * last seen using.  A port given here is for a partner that listens
   * somewhere else, which ax25ipd can express and this could not.
   */
  return 1;
}

/*---------------------------------------------------------------------------*/

/* Den Quellport auf dem Rufzeichen merken.  Gerufen nur direkt hinter
 * axip_route_add(), das die Adresse eben erst gesetzt hat - deshalb steht
 * hier keine zweite Adresspruefung.
 */
static void axip_learn_port(uint8 *call, const struct sockaddr *addr,
	struct edv_t *edv)
{
  struct axip_route *rp;

  for (rp = Axip_routes; rp && !axip_call_match(rp->call, call); rp = rp->next) ;
  if (!rp) return;
  rp->lport = sockaddr_port(addr);
  rp->ledv = edv;
  rp->ltime = secclock();
}

/*---------------------------------------------------------------------------*/

/* Der gelernte Port, oder 0.  Nur auf dem Interface, auf dem er gehoert
 * wurde: uhnp haengt am edv und war damit schon immer je Interface, und mit
 * zwei axudp-Ports auf einer Kiste ist die Unterscheidung auch noetig.
 * Dieselbe Frist wie uhnp - was dort altert, altert auch hier.
 */
static int axip_learned_port(struct axip_route *rp, struct edv_t *edv)
{
  if (!rp->lport || rp->ledv != edv) return 0;
  if (rp->ltime + UHNP_LEASETIME < secclock()) {
    rp->lport = 0;
    return 0;
  }
  return rp->lport;
}

/*---------------------------------------------------------------------------*/

/* DIE DREI STELLEN, AN DENEN DIE TCP-PORTEN DIE TABELLE BENUTZEN.  Sie stehen
 * hier und nicht in axtcp.c, weil die Regeln - wer darf lernen, was bleibt
 * stehen, was stirbt mit wem - dieselben sind wie bei axip und axudp, und eine
 * Tabelle mit zwei Regeln ist keine Tabelle (Thomas).
 */

/* EINEN ERSCHENENEN RUFZEICHEN DER TCP-SITZUNG ZUORDNEN.  Das Gegenstueck zu
 * axip_route_add() fuer den Verkehr aus einem AXTCP- oder KISS-Stream.
 *
 * DIES IST DER AUGENBLICK, IN DEM DER PARTNER GELERNT WIRD, und deshalb
 * steht die axip_may_learn()-Regel auch hier - und nicht im Aufrufer, denn
 * ein Aufrufer, der sie vergisst, laesst sie stillschweigend ausser Kraft.
 */
int axip_learn_transport(
const uint8 *call,
void *tsock,
int chan,
int proto,
struct iface *ifp)
{
  struct axip_route *rp;
  uint8 buf[AXALEN];
  int from = AXIP_FROM_LEARNED;

  /* DER WIRKLICHE PARTNER, und nicht das, was die Sitzung behauptet: das ist
   * dieselbe Frage, die axip_recv() am Adressfeld stellt, und dieselbe
   * Antwort gilt.  Gesucht wird der DIRECTE Partner, also der mit gesetztem
   * R-Bit, weil genau er den Tunnel haelt.
   */
  addrcp(buf, call);

  for (rp = Axip_routes; rp && !(rp->tsock == tsock && rp->tchan == chan &&
                                 axip_call_match(rp->call, buf)); rp = rp->next) ;
  if (rp) {
    /* DIESES RUFZEICHEN IST BEREITS DIESER SITZUNG ZUGEORDNET.  Kein Neuwert,
     * kein counters: der Pfad hat genau eine Adresse, und die ist die, die
     * schon drin steht.
     */
    rp->htime = secclock();
    return 1;
  }
  /* SONST: DAS GLEICHE RUFZEICHEN AUF EINER ANDEREN SITZUNG?  Dann ist das
   * eine Bewegung, und die richtet sich nach denselben Regeln wie bei axip:
   * permanent und once stehen still, "off" verbietet, und der Sysop darf
   * immer.
   */
  for (rp = Axip_routes; rp && !axip_call_match(rp->call, buf); rp = rp->next) ;
  if (rp) {
    if (rp->perm || rp->perm_once) {
      Axip_refused++;
      return 0;
    }
    if (!axip_may_learn(ifp)) {
      Axip_refused++;
      return 0;
    }
  } else {
    if (!axip_may_learn(ifp)) {
      Axip_refused++;
      return 0;
    }
    if ((rp = (struct axip_route *) malloc(sizeof(struct axip_route))) == NULL)
      return 0;
    memset(rp, 0, sizeof(struct axip_route));
    addrcp(rp->call, buf);
    if (ifp && ifp->axip_learn == AXIP_LEARN_ONCE)
      rp->perm_once = 1;
    rp->next = Axip_routes;
    Axip_routes = rp;
  }
  memset(&rp->dest, 0, sizeof(rp->dest));
  rp->tsock = tsock;
  rp->tchan = chan;
  rp->tproto = proto;
  /* htime, weil es hier eine ECHTE Nachricht ist: der Rufzeichen wurde so
   *eben gesehen.  Ohne das saehe ein frisch gelernter Eintrag im selben
   * Takt nach dem Lernen aus wie einer seit einer Stunde stumm, und die
   * Stille-Frist wuerde ihn wegraeumen.
   */
  rp->htime = secclock();
  (void) from;
  return 1;
}

/* DIE SITZUNG WEG - und damit die ihr gelernten Rufzeichen.  NUR DIE
 * GELERNTEN: "permanent", "once" und der Default sind Absichten des Sysops
 * und haben mit dieser Verbindung nichts zu tun.  Wer hier mehr loescht, als
 * der Port gelernt hat, nimmt einem Port, der zurueckkommt, seine Wege weg
 * (Thomas).
 */
void axip_forget_transport(
void *tsock)
{
  struct axip_route *rp, *pp, *nx;

  /* DER EINFACHE FALL, und er ist der haeufige: was der Verkehr ueber eine
   * TCP-Sitzung gelernt hat, gehoert ihr und stirbt mit ihr.  Einschliesslich
   * "once": das hiess "bei der Sitzung, bei der es zum ersten Mal gehoert
   * wurde" - und traegt nach, dass die Sitzung weg ist.  Sonst waere es kein
   * Vertrauen, sondern eine Eingefrorenheit ohne Ende, und die Route zeigte
   * auf etwas, das nicht mehr existiert.
   *
   * "permanent" und der Default erreichen diese Schleife gar nicht: die hat
   * kein tsock, weil der Sysop sie ueber "axip route add" geschrieben hat und
   * nicht ueber den Verkehr.  Sie sind Absichten, keine Beobachtungen, und
   * ein Port, der weg ist, loescht keine Absicht (Thomas).
   */
  for (pp = 0, rp = Axip_routes; rp; rp = nx) {
    nx = rp->next;
    if (rp->tsock != tsock) {
      pp = rp;
      continue;
    }
    if (pp)
      pp->next = nx;
    else
      Axip_routes = nx;
    if (rp->name)
      free(rp->name);
    free(rp);
  }
}

/* DIE SITZUNG FUER EIN RUFZEICHEN, oder NULL.  Zuerst die exakte Route auf
 * diesem Port, dann die exakte Route irgendwo, dann der Default auf diesem
 * Port, dann der Default irgendwo - dieselbe Reihenfolge wie axip_raw().
 */
void *axip_transport_route(
const uint8 *call,
struct iface *ifp)
{
  struct axip_route *rp;
  void *best = NULL;
  int best_prio = -2;
  int thisdev = axip_ifp_to_rdev(ifp);

  for (rp = Axip_routes; rp; rp = rp->next) {
    int prio;

    if (rp->tsock == NULL)
      continue;
    if (rp->is_default)
      prio = (rp->rdev && rp->rdev == thisdev) ? 0 : -1;
    else
      prio = (!axip_call_match(rp->call, call)) ? -2 :
	    ((rp->rdev && rp->rdev == thisdev) ? 2 : 1);
    if (prio > best_prio) {
      best = rp->tsock;
      best_prio = prio;
      if (prio == 2)
	break;
    }
  }
  return best;
}

/* DIE LETZTE NACHRICHT VON DIESER ADRESSE GESAGT - und zwar an ALLE ihre
 * Eintraege, nicht nur an den, den der Rahmen eben getroffen hat.  Sonst saehe
 * ein Rufzeichen, das hinter einer anderen Adresse liegt, nach der Stille-Frist
 * still aus, obwohl gerade gesprochen wurde (siehe axip_recv()).
 */
void axip_heard(
const uint8 *call)
{
  struct axip_route *rp;

  for (rp = Axip_routes; rp; rp = rp->next)
    if (!rp->is_default && axip_call_match(rp->call, call))
      rp->htime = secclock();
}

/* EIN RAHMEN, DER GELERNT WERDEN WOLLTE UND NICHT DURFTE.  Fuer die
 * TCP-Ports, die den Zaehler nicht selbst fuehren duerfen, weil die Tabelle
 * hier gehoert.
 */
void axip_dropped(
void)
{
  Axip_dropped++;
}

/*---------------------------------------------------------------------------*/

/* DIE HILFE TEXTE, und zwar an einem Ort, weil sie an mehreren Stellen
 * gebraucht werden: im Menue, im ifconfig-Befehl und in der Fehlermeldung.
 * Sie stehen auch in manuals/wampes/commands/axip - dort steht mehr, aber es
 * steht nichts, was nicht auch hier steht (Thomas).
 */
char Axip_learn_usage[] =
"ifconfig <iface> axip-learn on|once|off   (default on)\n"
"  on:    any callsign heard on this port is learned, and learned AGAIN if\n"
"         it turns up at another address - the behaviour to date.\n"
"  once:  like \"on\" the first time, and then the address is FIXED: a frame\n"
"         from that callsign at any other address is dropped.  Trust on\n"
"         first use.  A partner that moves has to be dropped and added\n"
"         again - which is the point: it is the only way to make a moving\n"
"         partner change address at all.\n"
"  off:   learn nothing.  Only the callsigns in \"axip route\" are used on\n"
"         this port.  For a port that carries 93 users one way and your own\n"
"         infrastructure the other.\n"
"  Only a LEARNED address is ever affected.  \"axip route add\" and \"add\n"
"  ... permanent\" are the sysop's business and are not touched by it.\n"
"  A route belongs to one port: learned on one, it is not used on another.\n"
"  The current value is in \"ifconfig <iface> verbose\".";

char Axip_dns_usage[] =
"ifconfig <iface> axip-dns-interval|axip-dns-silence <minutes>   (0 = node's)\n"
"  axip-dns-interval: the shortest time between two lookups of the SAME\n"
"     name - how often a name that is not being used is still checked.\n"
"     0 at the node means: never look up on your own initiative.\n"
"  axip-dns-silence: after this many minutes with NO TRAFFIC IN EITHER\n"
"     DIRECTION - received or sent, whichever is more recent - a name is\n"
"     looked up again even if the interval has not passed.  So a peer whose\n"
"     address has moved is found on the next packet, and a peer that is\n"
"     merely quiet is not asked about every ten minutes forever.\n"
"  A packet to send is a question by itself: the name is looked up when it is\n"
"  sent, and then no sooner than the interval says.\n"
"  What a lookup finds replaces the address in \"axip route\".  Only entries\n"
"  written with a NAME are ever looked up - a literal address has nothing to\n"
"  look up.  \"axip stats\" says how many lookups ran and how many failed.\n"
"  The current values are in \"ifconfig <iface> verbose\".\n"
"  See also \"axip dns-interval\" and \"axip dns-silence\" for the node's.";

static int doaxipdnsinterval(int argc, char *argv[], void *p)
{
  return setintrc(&Axip_dns_interval, "axip dns-interval", argc, argv, 0, 1440);
}

static int doaxipdnssilence(int argc, char *argv[], void *p)
{
  return setintrc(&Axip_dns_silence, "axip dns-silence", argc, argv, 0, 1440);
}

static struct cmds Axipcmds[] = {
  { "route",  doaxiproute, 0, 0,
    "axip route                             list the routes\n"
    "       axip route add [permanent] <call>[:<iface>] <host> [<port>]\n"
    "       axip route add [permanent] default[:<iface>] <host> [<port>]\n"
    "       axip route drop <call>|default[:<iface>]\n"
    "  <port> is for a partner who listens somewhere other than the port of\n"
    "  the interface; left out it means the interface's, or whatever he was\n"
    "  last seen using.\n"
    "  <host> may be a NAME.  It is looked up when the route is written and\n"
    "  looked up again later, see \"axip dns-interval\" - which is what makes\n"
    "  a partner who changes his address follow along.  A literal address is\n"
    "  written once and never looked up again.\n"
    "  <call>:<iface> ties the route to that port.  Left out, the route is\n"
    "  used on whichever port it is first needed on, and stays there.\n"
    "  Writing the same call again is how a route moves to another port.\n"
    "  permanent: the traffic may not move this address.  See also\n"
    "  \"ifconfig <iface> axip-learn once\", which does the same for the\n"
    "  address a callsign was first heard at - there is no default and no\n"
    "  learning for an address written by hand." },
  { "dns-interval", doaxipdnsinterval, 0, 1,
    "axip dns-interval [minutes 0..1440]   (0 = never look up)\n"
    "  The node's shortest time between two lookups of the same name.  An\n"
    "  interface can have one of its own, see \"ifconfig <iface> verbose\"." },
  { "dns-silence",  doaxipdnssilence, 0, 1,
    "axip dns-silence [minutes 0..1440]   (0 = use the node's)\n"
    "  After this many minutes with no traffic in either direction, a name is\n"
    "  looked up again anyway.  0 at the node means: only a packet to send\n"
    "  asks.  See \"axip dns-interval\"." },
  { "stats",  doaxipstats, 0, 0,
    "axip stats                      routes, and echoes dropped by the\n"
    "       loop-protect (see also \"ax25 loop-protect [s]\")" },
  { NULL,     NULL,        0, 0, NULL }
};

int doaxip(int argc, char *argv[], void *p)
{
  return subcmd(Axipcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

/* How long a "<call>[:<iface>]" is allowed to be - decided BEFORE anybody types
 * one.
 *
 * AXBUF on its own is too small, and that was the cause of a bug which
 * announced itself as something else entirely.  This call
 *
 *     axip route add te1st:axtcp 127.0.0.2 1000
 *
 * has eleven characters in front of the host.  The buffer was AXBUF (ten),
 * and the strncpy() quietly kept nine of them plus the null.  axip_split_call()
 * then saw "te1st:axt", failed to look up that port, and said
 *
 *     No such interface "axt"
 *
 * which is the message for a port that does not exist - and the port that was
 * written does exist.  With "default:axtcp" (thirteen characters) what was
 * left was "default:a", and the message came out as "No such interface \"a\"",
 * which is harder still to read.  The sysop goes looking for the interface
 * name, not for a buffer bound, and the message has nothing to do with the
 * cause.
 *
 * The size is built out of the parts now instead of guessed: AXBUF for the
 * callsign (the longest that setcall() takes), one colon, AXIP_IFNAMELEN for
 * the port name.
 *
 * AXIP_IFNAMELEN is deliberately NOT IFNAMSIZ.  That is 16 bytes and it is the
 * kernel's name limit, not WAMPES'.  "attach axtcp" and "attach kisstcp"
 * reserve 64 (axtcp.c:255, kisstcp.c:362), and "attach axip" does not truncate
 * the name at all: it arrives as argv[1] and is taken over with strdup()
 * (axip.c:1286).  Nothing in the tree shortens a port name to 16, so IFNAMSIZ
 * here would only have moved the same truncation further along.
 *
 * 128 is twice the longest port name the tree's own bounds reserve - 64.  "attach
 * axip" reserves nothing, so a longer name can be typed, and a route naming
 * one is then cut at 128: that is where such a bound has to be fixed, in the
 * place that says what it holds, rather than in a buffer that only pretends to
 * hold callsigns.
 */
#define AXIP_IFNAMELEN	128
#define AXIP_CALLBUF	(AXBUF + 1 + AXIP_IFNAMELEN)

static struct cmds Axiproutecmds[] = {
  { "add",    doaxiprouteadd,  0, 4,
    "axip route add [permanent] <call>[:<iface>] <host> [<port>]" },
  { "drop",   doaxiproutedrop, 0, 2,
    "axip route drop <call>[:<iface>]|default[:<iface>]" },
  { NULL,     NULL,            0, 0, NULL }
};

static int doaxiproute(int argc, char *argv[], void *p)
{

  char buf[AXBUF];
  struct axip_route *rp;

  if (argc >= 2)
    return subcmd(Axiproutecmds, argc, argv, p);

  printf("Call       Addr\n");
  for (rp = Axip_routes; rp; rp = rp->next) {
    char abuf[SOCKADDR_STRLEN];
    struct iface *ifp;

    if (rp->is_default)
      printf("%-9s  %s", "default",
             sockaddr_to_string((struct sockaddr *) &rp->dest, abuf, sizeof(abuf)));
    else
      printf("%-9s  %s", pax25(buf, rp->call),
             sockaddr_to_string((struct sockaddr *) &rp->dest, abuf, sizeof(abuf)));
    if (sockaddr_port((struct sockaddr *) &rp->dest))
      printf("  port %d", sockaddr_port((struct sockaddr *) &rp->dest));
    /* WELCHER PORT.  Und wenn die Numme keinem Port mehr gehoert, dann sagen
     * wir das, statt eine Zahl zu zeigen, die nichts mehr bedeutet - die
     * Nummer bleibt nach dem Wegfallen des Ports fuer immer vergeben, siehe
     * axip_ifp_to_rdev().
     */
    if (rp->rdev) {
      if ((ifp = axip_iface(rp->rdev)) != NULL)
	printf("  via %s", ifp->name);
      else
	printf("  via port %d (gone)", rp->rdev);
    }
    /* P heisst "der Sysop hat permanent gesagt", ONCE heisst "bei der Adresse
     * gescannt, bei der es zum ersten Mal gehoert wurde".  Zwei Worte, weil
     * es zwei verschiedene Zustaende sind und beide fuer sich genommen sind,
     * ohne dass der Sysop etwas gesagt haette.
     */
    if (rp->perm)
      printf("  P");
    if (rp->perm_once)
      printf("  once");
    if (rp->name)
      printf("  %s", rp->name);
    /* Der Name wird gesucht, und der letzte Versuch sagt, wann.  Ohne das
     * sieht eine Route, deren Name sich seit einer Stunde nicht aufloesen
     * laesst, genauso aus wie eine, die gar nicht nachfragt.
     */
    if (rp->name) {
      if (rp->qdue)
	printf("  address wrong");
      if (rp->rtime)
	printf("  asked %s ago", tformat(secclock() - rp->rtime));
    }
    /* ALTER DER BEIDEN RICHTUNGEN.  Nur was da ist; eine frisch angelegte
     * Route hat noch kein "vor ..." und soll nicht so tun, als waere sie
     * seit 1970 stumm.
     */
    if (rp->lport)
      printf("  learned %d (%ld s ago)", rp->lport,
             (long) (secclock() - rp->ltime));
    else if (rp->htime)
      printf("  heard %s ago", tformat(secclock() - rp->htime));
    putchar('\n');
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Ein "call:iface" aufteilen.  Steht hier und nicht beim Aufrufer, weil die
 * Fehlermeldung sonst an zwei Stellen gepflegt werden muss und die beiden
 * Stellen sich irgendwann unterscheiden.
 *
 * DER RUECKGABEWERT IST "kaputt", NICHT "unbekannt": der sysop hat sich
 * verschrieben, und das ist etwas anderes als ein Rufzeichen, zu dem es
 * keine Route gibt.
 */
static int axip_split_call(
char *arg,
uint8 *call,
struct iface **ifpp,
int *isdefp)
{
  char *colon;

  *ifpp = NULL;
  *isdefp = 0;
  if ((colon = strchr(arg, ':')) != NULL) {
    struct iface *ifp;

    *colon = 0;
    if ((ifp = if_lookup(colon + 1)) == NULL) {
      printf("No such interface \"%s\"\n", colon + 1);
      return 1;
    }
    /* Ein Interface, das gar kein axip-Port ist, kann keine Route bekommen:
     * es gaebe keinen Socket, ueber den sie liefe, und das waere eine Zeile
     * in "axip route", die niemals benutzt wird.  Und die Meldung sagt es,
     * statt stillzuschweigen - sonst sucht man den Fehler in ax25 route.
     */
    if (!axip_isport(ifp)) {
      printf("%s is not an axip or axudp interface\n", ifp->name);
      return 1;
    }
    *ifpp = ifp;
  }
  /* "default" ist kein Rufzeichen und wird auch nicht zu einem gemacht - es
   * passt in keins (siehe axip_route.is_default).  Der Befehl wird hier
   * erkannt und als Flag weitergegeben; alles andere bleibt bei setcall().
   */
  if (axip_arg_is_default(arg)) {
    memset(call, ' ', AXALEN);
    *isdefp = 1;
    return 0;
  }
  if (setcall(call, arg)) {
    printf("Invalid call \"%s\"\n", arg);
    return 1;
  }
  return 0;
}

static int doaxiprouteadd(int argc, char *argv[], void *p)
{

  /* AXIP_CALLBUF, not AXBUF: what lands in here is "<call>[:<iface>]", not a
   * callsign.  See the definition for the bug that AXBUF caused here. */
  char callbuf[AXIP_CALLBUF];
  uint8 call[AXALEN];
  struct sockaddr_storage ss;
  struct axip_route *rp;
  struct iface *ifp;
  char *host;
  char *name = NULL;
  int port = 0;                 /* zero: as before, the interface decides */
  int perm = 0;
  int isdef = 0;
  socklen_t l;

  /* "permanent" VOR dem Rufzeichen, wie bei "ax25 route add permanent" -
   * see ax25cmd.c.  Sonst muesste man raten, ob es zum Rufzeichen gehoert
   * oder zum Host, und beides sieht gleich aus.
   */
  if (argc >= 2 && !strcmp(argv[1], "permanent")) {
    perm = 1;
    argv++;
    argc--;
  }
  if (argc < 3) {
    printf("Usage: axip route add [permanent] <call>[:<iface>] <host> [<port>]\n");
    return 1;
  }
  if (argc >= 4) {
    long tmp;

    /* atoi() waere hier falsch: ein Wort, das keine Zahl ist, kommt als 0
     * durch und faellt dann nur zufellig aus dem Bereich - und "axip route
     * add DL0AAA host quatsch" waere "axip route add DL0AAA host" mit einer
     * Portmeldung, die niemandem auffaellt.  Das ist genau der Fehler, den
     * setintrc() in cmdparse.c seit 2026-09-01 nicht mehr macht.
     */
    if (cmd_getnum(argv[3], &tmp) || tmp <= 0 || tmp > 65535) {
      printf("Invalid port \"%s\"\n", argv[3]);
      return 1;
    }
    port = (int) tmp;
  }
  /* argv[1] is modified - axip_split_call() cuts at the colon - and that is
   * fine: argv belongs to the caller, who wants nothing else out of it, and
   * here the colon really is a separator.
   *
   * The comment that used to stand here read "AXBUF is the longest callsign,
   * plus a null" - which was true, and was the bug.  AXBUF is the longest
   * CALLSIGN, and what lands in this buffer is "<call>[:<iface>]": callsign,
   * colon, name.  The claim was about what the buffer takes, not about what it
   * has to take, and a strncpy() truncates without making a sound.  The bound
   * belongs to the definition of AXIP_CALLBUF - not to this line, and not to
   * anyone's memory.
   */
  strncpy(callbuf, argv[1], sizeof(callbuf) - 1);
  callbuf[sizeof(callbuf) - 1] = 0;
  if (axip_split_call(callbuf, call, &ifp, &isdef))
    return 1;
  host = argv[2];

  /* EIN NAME ODER EINE ADRESSE.  Ist es eine Adresse, wird sie geschrieben
   * und nie wieder angefasst.  Ist es ein Name, wird er gemerkt UND
   * aufgeloest - damit die Route nicht unbrauchbar bleibt, falls der Name im
   * Moment nicht aufgeloest werden kann.  Das ist die alte Regel, und sie
   * ist gut; neu ist nur, dass der Name danach nicht mehr weggeworfen wird.
   *
   * Auf demselben Weg wie der Timer, siehe axip_resolve_host(): resolve()
   * kann nur IPv4 und gibt ein int32 zurueck, was eine IPv6-Route nicht
   * einmal beschreiben kann - sie waere nach jedem Neustart anders.
   */
  if (!axip_is_literal(host)) {
    if ((name = strdup(host)) == NULL) {
      printf("out of memory\n");
      return 1;
    }
  }
  if (!axip_resolve_host(host, port, &ss, &l)) {
    printf(Badhost, host);
    if (name)
      free(name);
    return 1;
  }

  /* AXIP_ROUTE_ADD() schreibt die Adresse; der Name, permanent und der Port
   * gehoeren danach hinein, weil axip_route_add() sie bei from == SYSOP nicht
   * anfasst - dort steht nur, was der VERKEHR darf, und ein Sysop-Befehl ist
   * kein Verkehr.
   */
  axip_route_add(call, (struct sockaddr *) &ss, 1, AXIP_FROM_SYSOP, ifp);
  /* NUR NACH DEM RUFZEICHEN SUCHEN, und nicht zusaetzlich nach is_default.
   * Die Bedingung "rp->is_default == isdef" stand hier, um die
   * Default-Route von einer Rufzeichen-Route zu trennen - aber sie laeuft in
   * die falsche Richtung: is_default setzt erst die Zeile DARUNTER, und
   * axip_route_add() setzt es nie (es weiss nichts davon).  Ein frisch
   * angelegter "axip route add default ..." hatte also is_default == 0, die
   * Suche fand ihn nicht, das Kommando endete mit return 1, und es blieb
   * eine gewoehnliche Route mit dem Rufzeichen "      ".  Was danach passiert:
   * ein Rundspruk fand sie (der Ruecksprung fragt nicht nach dem Rufzeichen),
   * ein Unicast fand sie nicht (der fragt danach) - die also genau dann nicht,
   * wenn man sie braucht.  Und weil return 1 hier nichts ausgibt, sah der
   * Befehl aus wie gelungen.
   *
   * Nach dem Rufzeichen allein zu suchen ist genau dieselbe Suche, die
   * axip_route_add() zwei Zeilen oben gemacht hat, und sie ist eindeutig:
   * "default" ist sieben Leerzeichen (0x20), und ein echtes Rufzeichen
   * besteht aus (Zeichen << 1) & 0xfe - dort kann nie ein Leerzeichen
   * stehen, setcall() laesst es gar nicht erst zu.  Zwei Eintraege mit
   * demselben Rufzeichen kann es also nicht geben.
   */
  for (rp = Axip_routes; rp; rp = rp->next)
    if (addreq(rp->call, call)) break;
  if (!rp)
    return 1;
  rp->is_default = isdef;
  if (name) {
    if (rp->name)
      free(rp->name);
    rp->name = name;
  }
  /* EIN WIEDERHOLTES "AXIP ROUTE ADD" IST AUCH DIE GELEGENHEIT, EINEN PORT ZU
   * WECHSELN, und das ist der einzige Ort, an dem sich rdev aendert.  Wer die
   * Route an einen anderen Port haengen will, schreibt sie einfach hin - das
   * ist Absicht und nicht ein Nebeneffekt (Thomas).
   *
   * Ohne "<iface>" gehoert die Route danach wieder keinem Port: sie bindet
   * sich beim ersten Benutzen neu.  Wer sie umhaengen will, schreibt also
   * ausdruecklich "<call>:<iface>" hin.
   */
  rp->rdev = ifp ? axip_ifp_to_rdev(ifp) : 0;

  /* UND "PERMANENT" IST EIN SCHALTER, KEIN ZUSTAND, DEN MAN NUR SETZEN KANN:
   * "axip route add DL0AAA ..." ohne das Wort nimmt es wieder weg.  Wer es
   * stehen lassen will, laesst es stehen.  Ein Sysop, der es einmal
   * weggenommen hat, sieht es daran, dass es weg ist - und nicht an einer
   * Zeile in "axip route", die er fuer eine von Hand geschriebene haelt.
   */
  rp->perm = perm;
  rp->perm_once = 0;

  /* Das erste Fragen soll nicht erst eine Intervallfrist warten.
   */
  rp->rtime = 0;
  rp->qdue = 0;
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doaxiproutedrop(int argc, char *argv[], void *p)
{

  uint8 call[AXALEN];
  struct axip_route *rp, *pp;
  int isdef;

  /* "default[:<iface>]" ist ein Wort und kein Rufzeichen - siehe
   * axip_route.is_default.  "drop" darf es daher nicht durch setcall()
   * schicken, das nur echte Rufzeichen annimmt.
   */
  if (axip_arg_is_default(argv[1])) {
    isdef = 1;
    memset(call, ' ', AXALEN);
  } else {
    if (setcall(call, argv[1])) {
      printf("Invalid call \"%s\"\n", argv[1]);
      return 1;
    }
    isdef = 0;
  }
  for (pp = 0, rp = Axip_routes; rp; pp = rp, rp = rp->next)
    if (rp->is_default == isdef && axip_call_match(rp->call, call)) {
      if (pp)
	pp->next = rp->next;
      else
	Axip_routes = rp->next;
      /* Der Name ist der einzige Speicher im Eintrag und damit der einzige
       * Grund fuer ein free() hier.  Ohne das bliebe bei einem Knoten, der
       * staendig Routen mit Namen neu schreibt, eines nach dem anderen
       * liegen - und "axip route drop" waere genau der Befehl, den man zur
       * Fehlersuche braucht.
       */
      if (rp->name)
	free(rp->name);
      free(rp);
      break;
    }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doaxipstats(int argc, char *argv[], void *p)
{

  struct axip_route *rp;
  int total = 0;
  int active = 0;
  int named = 0;
  int perm = 0;
  int frozen = 0;
  int wrong = 0;
  int unbound = 0;

  for (rp = Axip_routes; rp; rp = rp->next) {
    total++;
    /* "aktiv": in der uhnp-Lease-Frist gehoert, also ein Partner, der
     * tatsaechlich gerade auf dem Datagramm-Port sitzt.
     */
    if (rp->lport && rp->ltime + UHNP_LEASETIME >= secclock())
      active++;
    if (rp->name)
      named++;
    if (rp->perm)
      perm++;
    if (rp->perm_once)
      frozen++;
    if (rp->qdue)
      wrong++;
    if (!rp->rdev)
      unbound++;
  }
  printf("routes      %d (%d active)\n", total, active);
  /* DIE VIER ZAHLEN, die man beim Suchen braucht, und zwar getrennt, weil sie
   * verschiedene Fragen beantworten:
   *
   *   refused  der Verkehr wollte eine Adresse aendern und durfte nicht.
   *            Das ist die Zahl, nach der man sucht, wenn eine Station
   *            "permanent" bekommen hat und sich wundert, dass sie ihre
   *            Adresse nicht wechselt - ohne sie sieht das nach einem
   *            Funkloch aus.
   *   dropped  der Rahmen kam, die Adresse passte nicht (oder auf diesem Port
   *            darf nicht gelernt werden).  Ohne die Zahl ist nicht
   *            unterscheidbar, ob ein Port ueberhaupt etwas empfaengt.
   *   lookups  nach Namen gefragt, davon failed ohne Ergebnis.  Das Verhaeltnis
   *            sagt mehr als beide Zahlen einzeln: 2000 zu 3 ist in Ordnung,
   *            2000 zu 2000 ist ein toter Nameserver.
   */
  printf("refused     %ld   permanent, once, or learn off\n", Axip_refused);
  printf("dropped     %ld   address mismatch or learn off\n", Axip_dropped);
  printf("lookups     %ld (%ld failed)\n", Axip_lookups, Axip_lookupfail);
  printf("named       %d (%d permanent, %d once, %d address wrong)\n",
         named, perm, frozen, wrong);
  /* UND DIE UNGEBUNDENEN, denn das ist die Liste derer, die sich beim
   * ersten Benutzen noch einen Port aussuchen - und sich bei zwei axip-Ports
   * fuer den falschen entscheiden koennen.
   */
  if (unbound)
    printf("unbound     %d   no port yet; they bind on first use\n", unbound);
  printf("dns         every %d min, after %d min quiet (node's)\n",
         Axip_dns_interval, Axip_dns_silence);
  printf("bad echoes  %d   loop-protect window %d s\n",
         Ax_echoes, Ax_dup_window / 1000);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* ifconfig <iface> axip-learn on|once|off
 *
 * Ein Interface, das gar kein axip-Port ist, sagt es, statt die Angabe zu
 * nehmen und still zu nichts zu tun - dieselbe Begruendung wie bei ifarp() in
 * iface.c, und es ist derselbe Fehler: eine Einstellung, die nichts bewirkt,
 * sieht hinterher aus wie eine, die etwas bewirkt.
 */
int if_axip_learn(int argc, char *argv[], void *p)
{
  struct iface *ifp = (struct iface *) p;

  if (!axip_isport(ifp)) {
    printf("%s is not an axip or axudp interface - it has no callsigns to\n"
	   "learn.  \"attach axip\" or \"attach axudp\" makes one.\n", ifp->name);
    return 1;
  }
  if (argc < 2) {
    printf("%s: axip-learn %s\n", ifp->name,
	   ifp->axip_learn == AXIP_LEARN_ON    ? "on" :
	   ifp->axip_learn == AXIP_LEARN_ONCE  ? "once" : "off");
    return 0;
  }
  if (!strcmp(argv[1], "on"))
    ifp->axip_learn = AXIP_LEARN_ON;
  else if (!strcmp(argv[1], "once"))
    ifp->axip_learn = AXIP_LEARN_ONCE;
  else if (!strcmp(argv[1], "off"))
    ifp->axip_learn = AXIP_LEARN_OFF;
  else {
    printf("Usage: %s\n", Axip_learn_usage);
    return 1;
  }
  return 0;
}

/* ifconfig <iface> axip-dns-interval <minuten>   und   -silence
 *
 * Beide ueber setintrc(), weil dort schon steht, warum atoi() hier falsch
 * waere: ein Wort, das keine Zahl ist, kommt als 0 durch, und 0 ist bei
 * beiden ein gueltiger Wert - "der Knotenwert" beim Port, "gar nicht" beim
 * Knoten.  Ein Tippfehler wuerde also stillschweigend die Vorgabe des Knotens
 * einsetzen.
 */
int if_axip_dns_interval(int argc, char *argv[], void *p)
{
  struct iface *ifp = (struct iface *) p;

  if (!axip_isport(ifp)) {
    printf("%s is not an axip or axudp interface\n", ifp->name);
    return 1;
  }
  if (argc < 2) {
    printf("%s: axip-dns-interval %d%s\n", ifp->name, ifp->axip_dns_interval,
	   ifp->axip_dns_interval ? "" : " (the node's)");
    return 0;
  }
  return setintrc(&ifp->axip_dns_interval, "axip-dns-interval", argc, argv, 0, 1440);
}

int if_axip_dns_silence(int argc, char *argv[], void *p)
{
  struct iface *ifp = (struct iface *) p;

  if (!axip_isport(ifp)) {
    printf("%s is not an axip or axudp interface\n", ifp->name);
    return 1;
  }
  if (argc < 2) {
    printf("%s: axip-dns-silence %d%s\n", ifp->name, ifp->axip_dns_silence,
	   ifp->axip_dns_silence ? "" : " (the node's)");
    return 0;
  }
  return setintrc(&ifp->axip_dns_silence, "axip-dns-silence", argc, argv, 0, 1440);
}

/*---------------------------------------------------------------------------*/

/* ifconfig <iface> verbose: eine Zeile je eingestelltem Wert, und das "*"
 * bedeutet "vom Knoten geerbt".  Sonst sieht ein Port, der nie eingestellt
 * wurde, ungesetzt aus - und "0" ist hier ein gesetzter Wert, der etwas
 * bedeutet, nicht "nichts".
 *
 * Der Knotenwert steht in derselben Zeile dahinter, weil die Zahl ohne ihn
 * nicht lesbar ist: "axip-dns-interval 0" heisst "alle zehn Minuten", und
 * das sieht man an dieser Stelle erst, wenn der Knotenwert danebensteht.
 */
void axip_show_verbose(const struct iface *ifp)
{
  struct iface *ifp2 = (struct iface *) ifp;

  printf("           axip-learn %s%s%s\n",
	 ifp2->axip_learn == AXIP_LEARN_ON    ? "on" :
	 ifp2->axip_learn == AXIP_LEARN_ONCE  ? "once" : "off",
	 ifp2->axip_learn ? "" : " (default)",
	 "");
  printf("           axip-dns-interval %d min%s (%d min node's)\n",
	 ifp2->axip_dns_interval * 60, ifp2->axip_dns_interval ? "" : "*",
	 Axip_dns_interval);
  printf("           axip-dns-silence %d min%s (%d min node's)\n",
	 ifp2->axip_dns_silence * 60, ifp2->axip_dns_silence ? "" : "*",
	 Axip_dns_silence);
}
