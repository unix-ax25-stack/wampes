/* Der Lernfilter: was darf aus dem Verkehr in die Routen- und ARP-Tabelle?
 *
 * NICHT ZU VERWECHSELN MIT ipfilter, und die Verwechslung war der Anlass:
 * ipfilter ist eine Sperrliste fuer ADRESSEN ("mit dem rede ich gar nicht"),
 * und weil sie zugleich rt_add() gattert, konnte man das eine nicht ohne das
 * andere haben.  Wer 10/8 nicht lernen wollte, sperrte damit den Verkehr.
 * Hier steht nur die Lernfrage.
 */

#ifndef _IPLEARN_H
#define _IPLEARN_H

#define LEARN_ROUTE     1
#define LEARN_ARP       2
#define LEARN_BOTH      (LEARN_ROUTE | LEARN_ARP)

/* Darf diese Adresse gelernt werden?
 *
 * target/bits ist, was eingetragen werden soll - eine /32 aus einem
 * Datagramm ebenso wie ein Praefix aus einer INP3-Ankuendigung.  what ist
 * LEARN_ROUTE oder LEARN_ARP.  call ist, WER die Adresse fuer sich
 * beansprucht (die absendende Station, bei INP3 der ankuendigende Knoten),
 * NULL wo es keins gibt; ifp das Interface, ueber das es hereinkam.
 *
 * Ohne passende Regel: erlaubt.  Eine leere Liste aendert also nichts.
 */
int ip_may_learn(int32 target, int bits, int what,
                 const uint8 *call, struct iface *ifp);

int doiplearn(int argc, char *argv[], void *p);

extern char Iplearn_usage[];

#endif  /* _IPLEARN_H */
