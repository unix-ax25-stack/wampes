/* AX.25 in IP, die axip- und axudp-Ports - see axip.c.
 *
 * Das ist die axip-Variante von pidfilter.h: die drei ifconfig-Befehle und
 * die eine Zeile fuer "ifconfig <iface> verbose" liegen hier, damit iface.c
 * nicht den ganzen axip.c braucht und axip.c nicht wiederum.iface.c.
 */

#ifndef _AXIP_H
#define _AXIP_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* NICHT INLINEN, und das ist kein Stil sondern eine Fehlervermeidung.
 *
 * axip_is_multicast() in axip.c enthaelt eine Schleife ueber Ax25multi[], die
 * clang 17 zerlegt, sobald sie in axip_raw() hineingezogen wird: der Test
 * von (*mpp)[0] und der "break" fallen weg, die Schleife laeuft ueber das
 * Array hinaus, und die Node stirbt beim Senden.  Als eigene Funktion wird
 * sie richtig uebersetzt - solange sie eine eigene Funktion BLEIBT.  Eine
 * eigene Funktion genuegt nicht, clang zieht sie wieder hinein, wenn sie nur
 * einmal aufgerufen wird, und dann ist der Fehler wieder da.  Deshalb
 * dieses Attribut, und deshalb steht die Begruendung auch dort.
 */
#if defined(__GNUC__) || defined(__clang__)
#define AXIP_NOINLINE __attribute__((noinline))
#else
#define AXIP_NOINLINE
#endif

/* DIE DREI STUFEN, und nicht on|off: siehe if_axip_learn() in axip.c.  0 ist
 * die Vorgabe, weil ein Port, den niemand eingestellt hat, so laufen soll
 * wie heute - ein Sysop, der "axip-learn off" meint, schreibt es hin.
 */
#define AXIP_LEARN_ON     0       /* learn freely, and relearn a new address */
#define AXIP_LEARN_ONCE   1       /* trust on first use: the address sticks */
#define AXIP_LEARN_OFF    2       /* learn nothing; only what the sysop wrote */

/* MINUTEN, und beide Vorgaben sind VIEL_SCHRIFT.  Keine stille Frist als
 * Vorgabe waere die schlechtere Wahl: ein eingetragener Host, von dem nie ein
 * Byte kommt, wird dann fuer immer alle zehn Minuten nachgefragt, und das ist
 * genau das, was die Frist verhindert (Thomas).
 */
#define AXIP_DNS_INTERVAL_DEFAULT   10
#define AXIP_DNS_SILENCE_DEFAULT    60
/* Zwei Aufloesungen je Takt.  Der Aufrufer blockiert, und ein toter
 * Nameserver, an dem ein Hanger haengt, darf den Knoten nicht minutenlang
 * festhalten; wer danach dran ist, ist im naechsten Takt dran.
 */
#define AXIP_LOOKUPS_PER_TICK       2
#define AXIP_TICK                   60000L    /* 60 s */
/* Aufloesung des Keepalive-Ticks der axudp-Ports.  Werte kommen
 * hauptsaechlich zwischen 30 und 300 s vor, und zehn Sekunden treffen sie
 * alle; der Tick des TCP-Keepalives liegt bei 30 s.
 */
#define AXIP_KEEPALIVE_TICK         10000L    /* 10 s */

/* The usage strings, here so that the Ifcmds entry in iface.c - which is what
 * "ifconfig <iface> axip-learn ?" prints - and the command's own messages say
 * the same thing.
 */
extern char Axip_learn_usage[];
extern char Axip_dns_usage[];

int axip_isport(const struct iface *ifp);
/* Ist der axip-Port ein AXUDP-Port?  ifkeepalive() braucht den Unterschied:
 * hinter einem Raw-IP-Port sitzt keine NAT-Box, die eine Portnummer vergessen
 * koennte - der Keepalive ist die Antwort auf ein UDP-Problem.
 */
int axip_isudp(const struct iface *ifp);
/* WHICH PORTS CARRY A ROUTE AT ALL: the axip/axudp ones (raw hook axip_raw)
 * and the two TCP carriers axtcp/kisstcp (raw hook tcpsock_raw).  Both learn
 * callsigns, so both want the same commands - a setting refused on one of the
 * two would be one the sysop has to guess (Thomas).
 */
int axip_iscarrier(const struct iface *ifp);
/* Der Keepalive der axudp-Ports braucht einen eigenen Tick: axip_timer()
 * gehoert zu den Routen und hat keine Stelle, die ihn anstossen koennte.
 * ifkeepalive() ruft das hier, sobald ein axudp-Port einen Keepalive
 * bekommt.  Der Tick stellt sich selbst ab, wenn keiner mehr laeuft.
 *
 * Und: hat dieser Port in diesem Moment ein Ziel?  Sonst sagt ihm der
 * Aufrufer, dass der Keepalive noch nichts zu halten hat (Thomas).
 */
void axip_keepalive_start(void);
int axip_keepalive_has_target(const struct iface *ifp);
/* Fuer "ifconfig <iface> verbose": eine Zeile je eingestelltem Wert, und
 * nur dann - dieselbe Regel wie beim pid-filter.  Ein Gatter, das vor dem
 * Parsen verwirft, darf nicht ausgerechnet dort unsichtbar sein, wo man
 * nachsieht, was ein Port tut (Thomas).
 */
void axip_show_verbose(const struct iface *ifp);

int if_axip_learn(int argc, char *argv[], void *p);
int if_axip_dns_interval(int argc, char *argv[], void *p);
int if_axip_dns_silence(int argc, char *argv[], void *p);

/* DIE TABELLE IST AUCH DIE DER TCP-PORTE, und das ist der Grund fuer diese
 * drei Funktionen hier: axip-learn, "permanent" und die Frage, was beim Weggehen
 * einer Verbindung sterbt, sind dieselben Fragen fuer axudp, axtcp und kisstcp.
 * Stehen sie nur in axip.c, hat der TCP-Weg seine eigene, abweichende Fassung -
 * und die faellt erst auf, wenn eine gelernte Route nach einem reconnect nicht
 * mehr stimmt (Thomas).
 */
void axip_forget_transport(void *tsock);
int axip_learn_transport(const uint8 *call, void *tsock, int chan, int proto, struct iface *ifp);
void *axip_transport_route(const uint8 *call, struct iface *ifp);
/* A WRITTEN route on a TCP port (tsock == NULL, so not a learned one) for
 * this callsign - or a default written on it.  The address of such a route
 * is a stand-in: over TCP the connection is the address, and the port picks
 * the session it has (tcpsock_raw(), Thomas).
 */
int axip_sysop_route_on(const uint8 *call, struct iface *ifp);
void axip_heard(const uint8 *call);
void axip_dropped(void);

#endif /* _AXIP_H */
