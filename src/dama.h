/* DAMA slave - see dama.c. */

#ifndef _DAMA_H
#define _DAMA_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _LAPB_H
#include "lapb.h"
#endif

/* EIN MINDESTABSTAND ZWISCHEN ZWEI POLLS, ohne den die Runde rast.
 *
 * Gemessen, als der erste echte Slave gegenueberstand: er beantwortet den
 * Poll, das beendet den Zug, der naechste begann sofort - 221794 Polls in
 * 25 Sekunden, bei EINER Station.  Mit mehreren waere es dasselbe, nur
 * abwechselnd.
 *
 * TNN fuehrt dafuer dama_init, Vorgabe 100 in Einheiten von 10 ms, also eine
 * Sekunde (config.c; einstellbar bis 1000, das waeren zehn).  Dieselbe Zahl
 * hier, und aus demselben Grund: der Abstand ist es, der aus "so schnell wie
 * die Leitung kann" eine Runde macht - und er laesst dem Kanal Luft fuer
 * das, was NICHT gepollt wird, den Verbindungsaufbau naemlich, der nach der
 * Spezifikation in CSMA laeuft.
 *
 * NUR NOCH DIE VORGABE, seit "ifconfig <iface> dama-gap <ms>" den Wert je
 * Port setzen kann - so wie TNN ihn seit jeher setzen laesst.
 */
#define DAMA_GAP_DEFAULT        1000L   /* ms */

/* Roles. */
#define DAMA_OFF        0
#define DAMA_SLAVE      1
#define DAMA_MASTER     2

/* WIE STRENG DER MASTER IST - Sache des Sysops, nicht unsere (Thomas).
 *
 *   lazy        sagt nichts und nimmt DAMA- wie Nicht-DAMA-Nutzer
 *               stillschweigend an.
 *   permissive  informiert den Stoerer per UI, dass er DAMA benutzen
 *               sollte.  Kein Disconnect.  VORGABE.
 *   enforce     verwarnt und trennt nach DAMA_MAXVIOL Verstoessen - das
 *               ist TNNs Verhalten (par 6 DAMA-MaxPol, Vorgabe 5).
 *
 * Die Vorgabe ist die mittlere, und das passt zu dem, was sonst ueberall
 * hier steht: unser Slave legt bei verstummtem Master auch nicht auf, weil
 * ein Disconnect den Benutzer alles oberhalb von AX.25 kostet.  Dieselbe
 * Haltung, nur von der Master-Seite.
 */
#define DAMA_LAZY       0
#define DAMA_PERMISSIVE 1
#define DAMA_ENFORCE    2

void dama_heard_frame(struct iface *ifp,const struct ax25 *hdr);
int  dama_holds(struct ax25_cb *axp);
void dama_poll_begin(struct iface *ifp,int ispoll,const struct ax25 *hdr);
void dama_poll_end(struct iface *ifp);
void dama_serve_others(struct iface *ifp,struct ax25_cb *polled);
/* UI auf einem DAMA-Port: kurz halten, ob ein Poll-Fenster kommt, und dort
 * mitsenden.  1 heisst uebernommen, 0 "sende selbst".  Siehe dama.c.
 */
int  dama_defer_ui(struct iface *ifp,struct mbuf **bpp);
void dama_ui_flush(struct iface *ifp);
void dama_mark(struct ax25_cb *axp);
void dama_wait(struct ax25_cb *axp);
/* Der Master: eine Runde reihum, eine Station je Zug.  dama_master_input()
 * wird fuer JEDEN Rahmen gerufen, der auf einem Master-Port hereinkommt und
 * bei uns endet - dort endet der Zug, und dort werden Verstoesse gezaehlt.
 */
void dama_master_input(struct iface *ifp, struct ax25_cb *axp,
	const struct ax25 *hdr, int isu, int isi, int ispoll, int isfinal);
void dama_master_stop(struct iface *ifp);
void dama_master_kick(struct iface *ifp);
int  dama_connect_refused(struct iface *ifp,const struct ax25 *hdr);
/* Der Master antwortet nicht in einen fremden Zug hinein.  Dasselbe
 * Gespann wie dama_holds()/dama_wait() auf der Slave-Seite: das erste
 * fragt, das zweite merkt es vor.
 */
int  dama_master_holds(struct ax25_cb *axp);
void dama_master_owe(struct ax25_cb *axp);

const char *dama_master_port(void);
/* Die geltende Luecke zwischen zwei Zuegen: was der Sysop gesetzt hat,
 * sonst berechnet aus hf-datarate und TX-Delay.  Auch iface.c zeigt sie an.
 */
int32 dama_gap_time(struct iface *ifp);

int  ifdama(int argc, char *argv[], void *p);
void dama_show(struct iface *ifp);

#endif /* _DAMA_H */
