# WAMPES — Sicherheitsanalyse / Code-Audit

Stand: 2026-08-09
Basis: `wampes-import/wampes-git`, Arbeitsbaum inkl. der nicht committeten LP64-Änderungen
Umfang: 224 C/H-Dateien, ~60.000 LOC (src 39.7k, bbs 8.2k, tools 3.3k, convers 2.7k, util 2.3k, aos 2.2k, lib 1.4k)

Zwei Fragestellungen:

1. **Allgemeines Security-Audit** des von außen erreichbaren Codes.
2. **Abgleich mit Bugs, die im Linux-Kernel an der gemeinsamen Codebasis (slhc/slip/ppp)
   gefixt wurden** — siehe TODO.txt.

Beide sind fündig geworden. Der Linux-Abgleich hat *einen* Treffer, der Rest der schweren
Befunde stammt aus NOS-eigenem Code, den Linux nie übernommen hat (DNS, FTP, BBS, AXIP).

---

## 0. Kurzfassung

| ID | Datei | Befund | Schwere |
|----|-------|--------|---------|
| W-01 | `src/domhdr.c:424` | DNS-Server: unbegrenztes Serialisieren in 512-Byte-mbuf → remote Heap-Overflow, voll kontrollierter Inhalt | **kritisch** |
| W-02 | `src/ftpserv.c:394 ff.` | FTP-Server: Format-String über Dateinamen (`%n`) | **kritisch** |
| W-03 | `bbs/bbs.c:769` | `popen()` mit unvalidierten Mailadressen + Stack-Overflow — Binary ist **setuid root** | **kritisch** |
| W-04 | `src/slhc.c:77` | VJ-Dekompression: OOB-Read/Write auf `rstate[255]` (fehlendes `SLF_TOSS`) — **Divergenz zu Linux** | **kritisch** |
| W-05 | `src/axip.c:172` | AXUDP/AXIP: unbegrenzter Adressfeld-Walk → remote OOB-Read / Stack-Disclosure | **kritisch** |
| W-06 | `src/domhdr.c:256` | `dn_expand()` ohne `eom`-Grenze → beliebiger OOB-Read über Kompressionszeiger | hoch |
| W-07 | `src/domhdr.c:23,134` | DNS-Parser komplett ohne Längenprüfungen (HINFO/TXT `memcpy`) | hoch |
| W-08 | `src/ftpserv.c:284` | `physname[1024]` Stack-Overflow (`AsUser`-Makro) | hoch |
| W-09 | `src/slhc.c:504` | `slhc_remember()` ohne Mindestlängenprüfung — **Linux-Fixes fehlen** | hoch |
| W-10 | `bbs/bbs.c:669,655` | `split_address()` / `translate_bangs()` unbegrenzte Kopien | hoch |
| W-11 | `src/tcphdr.c:142` | TCP-Options-Parser liest über `options[40]` hinaus (Stack-Leak via TS-Echo) | hoch |
| W-12 | `src/domhdr.c:338` | `Compress_table[128]` ohne Grenze → .bss-Overflow | hoch |
| W-25 | `src/slip.c:210`, `src/nrs.c:183` | KISS/SLIP/NRS: keine Frame-Längenbegrenzung beim Empfang → unbegrenztes mbuf-Wachstum | hoch |
| W-29 | `src/sntp.c:290,254` | SNTP: `unsigned long` für 32-Bit-Wire-Felder → auf LP64 falsches Parsing **und** 8 Byte uninitialisierter Stack pro Antwort ans Netz | **kritisch** |
| W-30 | `src/sntp.c:449,585` | SNTP-Client akzeptiert Antworten von beliebigem Absender, Port aus vorhersagbarem `Lport++` | hoch |
| W-31 | `src/sntp.c:477` | SNTP-Client prüft den Origin-Timestamp nicht; `org == 0` wird ersetzt statt verworfen | hoch |
| W-35 | `src/ftpserv.c:709` | **FTP-Auth-Bypass auf Linux**: `#define crypt(key,salt) (key)` → Passwort `x` bzw. gar keins; keine root-Sperre | **kritisch** |
| W-36 | `src/remote_net.c:161` | Unauthentifizierter Kommandokanal (`command`/`console`) als root; derzeit nur Unix-Socket | hoch |
| W-37 | `src/smisc.c:151` | `remote`-Dienst: `KICK__ME` ohne Passwort, Ziel-IP frei wählbar | hoch |
| W-40 | `src/rip.c:315` | RIP: Updates von jedem Absender, Deny- statt Allow-Liste (erledigt, Einstufung korrigiert auf mittel) | ~~hoch~~ |
| L-1 | `src/misc.c:203` | `get32()`: Signed-Shift-Overflow — **Regression durch die LP64-Umstellung**, per UBSan belegt | hoch |
| W-13 … W-24, W-26 … W-28, W-32 … W-34, W-38, W-39, W-41, W-42 | siehe Abschnitte 2e und 3 | mittel/niedrig | |

Alle fünf mitgelieferten `net.rc.*` starten `start domain`; zwei zusätzlich `start ftp`.
W-01 ist also in der Default-Konfiguration erreichbar.

---

## 0b. Nachprüfung der Fixes (Stand 2026-08-10, Branch `main`)

| Befund | Commit | Ergebnis |
|---|---|---|
| W-04 `SLF_TOSS` | `6911c3b` | **korrekt.** Wortgleich mit Linux inkl. Kommentar. Die zitierte CVE-2026-45842 stimmt (= Commit `e7660744`, „slip: reject VJ receive packets on instances with no rstate array", veröffentlicht 2026-05-28). Einzige Unstimmigkeit: die Commit-Message spricht von `slhc_reset()` — die Funktion heißt in WAMPES `slhc_init()`, ein `slhc_reset()` existiert nicht. |
| L-1 `get32()` | `4ea1bf6` | **korrekt.** `uint32`-Akkumulator, Cast beim Return. |
| L-1-Folge­stellen | `7e8b140` | **korrekt**, alle fünf: `ftpserv.c pport()`, `sntp.c dosntpsysrefid()`, `timer.c`/`hpux.c` `Msclock` (die Kürzung auf `int32` ist hier gewollt, der Zähler wrappt bewusst), `iproute.c`/`ipfilter.c` `~0U`-Masken. Bei den Masken verhindert das vorgelagerte `if(bits)` einen Shift um 32; `rt_add()` und `rt_lookup()` klemmen zusätzlich `bits > 32`. |
| Pointer-Truncation | `f4db321` | **korrekt.** `open_tcp(..., long user)` passt jetzt zum Feld `tcb->user`. |
| Funktionszeiger-Casts | (in Arbeit) | Die `(void (*)(void *))`-Casts sind baumweit verschwunden bzw. gerade in Arbeit (29 Dateien im Arbeitsbaum). Das war echtes UB — Aufruf durch einen inkompatiblen Funktionszeigertyp — und fällt auf ARM64 eher auf als auf x86. Gute Aufräumaktion. |
| **W-35 `crypt()`** | — | **nicht im Baum.** `src/ftpserv.c` ist gegenüber `HEAD` unverändert, `git log -S crypt` zeigt nur CVS-Altcommits, und `ftpserv.c:712` enthält weiterhin `#define crypt(key, salt) (key)`. Falls der Fix in einer anderen Arbeitskopie liegt oder ungespeichert ist: er fehlt hier. |
| **`tcpin.c trim()`** | `c586859` → `8346ed0` | **erledigt.** Die unter L-5 beschriebene Regression ist behoben, und zwar vollständig: `trim()` rechnet wieder modulo 2³² (`int32 dupcnt,excess` + `(int32)((uint32)…)`), ebenso `seq_lt/gt/ge/le` (`tcpsubr.c:174-190`), `in_window()` und `geniss()`. Live gegen slip+vjslip und netrom/flexnet geprüft, UBSan-clean. |
| Callback-Casts | `6573dbb` | **erledigt**, baumweit. |

### Vollständige Nachverifikation aller Befunde gegen HEAD (`8d3a4ed`)

Alle Befunde wurden einzeln gegen den aktuellen Arbeitsbaum geprüft (Mustersuche auf
die jeweils zitierte Codestelle). Ergebnis:

**Erledigt (7):** W-04, L-1, L-5 sowie die drei begleitenden LP64-/UB-Punkte
(`open_tcp`-Pointer-Truncation, Folgestellen des Signed-Shift-UB, Callback-Casts) und
`rt_add`/`rt_lookup`-Klemmung.

**Alle Befunde W-01…W-43 und L-1…L-6 sind zu.** Stand 2026-08-11.

Zwei Punkte sind dabei bewusst *nicht* geändert worden, mit Begründung im jeweiligen
Commit: die FlexNet-Header-Erkennung in `ntohax25()` (`52d3fa0` — speichersicher, die
synthetisierte Quelladresse ist konstant, eine Bedingung bräuchte ein globales Flag und
ein Interface-Argument im Empfangspfad) und das setuid-Bit von `bbs`, das durch eine
Gruppe ersetzt statt entfernt wurde (`06b0b65`, Thomas' Entscheidung).

Zwei eigene Befunde mussten korrigiert werden: **W-40** war zu hoch eingestuft
(`ipfilter` und der Permanentschutz existierten bereits), und **W-34** behauptete eine
Reflektorschleife, die es nicht gibt — der Server nimmt nur `MODE_CLIENT` und sendet
`MODE_SERVER`, das dieselbe Prüfung verwirft. Ein Portfilter dort wäre schädlich
gewesen: `ntpd` sendet im Client-Modus von Port 123.

**Erledigt am 2026-08-11:** W-30/W-31 (`ccaa937`), W-32 (`5f7421f`),
W-03 und W-10 (`65ab936`), dazu `d847c42` (setgid-Gruppe in `dropprivileges()`)
und `06b0b65` (bbs wird nicht mehr setuid root installiert, sondern optional
setgid auf eine Gruppe — `make install BBSGROUP=hams`, siehe `README.bbs`).
**W-03 ist damit vollständig zu, einschließlich des setuid-Bits.** Ebenso **W-40**
(`4d69240`) — dazu unten die korrigierte Fassung des Befunds.

**Warum es praktisch keine Überschneidung gibt.** Die 64-Bit-Arbeit zielte auf
*undefiniertes Verhalten und Typbreiten* — Dinge, die `-Wformat`, UBSan und ein
Live-Lauf zeigen. Die Auditbefunde sind dagegen fast durchweg *fehlende
Längenprüfungen, fehlende Authentifizierung und unsichere API-Nutzung*. Ein Sanitizer
meldet die erst, wenn die bösartige Eingabe tatsächlich ankommt — im Normalbetrieb
also nie. Die beiden Mengen sind weitgehend disjunkt, und das ist zu erwarten.

**Die eine Ausnahme: W-29.** `sntp.c ntohntp()`/`htonntp()` liegt in beiden Kategorien
— `unsigned long buf[12]` mit 32-Bit-Wire-Feldern ist ein reiner LP64-Fehler. Der
einzige Commit an `sntp.c` seit Beginn der Umstellung (`7e8b140`) hat nur
`dosntpsysrefid()` angefasst; `ntohntp()` und `htonntp()` sind unverändert. Das ist
der Befund, der fachlich in die LP64-Arbeit gehört und dort noch fehlt.

Anmerkung zum Testverfahren: den uninitialisierten Lesezugriff in W-29 findet **UBSan
nicht** — das ist MemorySanitizer-Gebiet (`-fsanitize=memory`, nur clang/Linux) oder
Valgrind. Ein UBSan-sauberer Lauf ist insofern kein Gegenbeweis.

### L-5 — `trim()`: die 64-Bit-Verbreiterung zerstört die Modulo-2³²-Sequenzarithmetik

`src/tcpin.c:869-875, 927`

```c
	long dupcnt,excess;
	...
	if((excess = (long)seg->seq + (long)*length
	             - ((long)tcb->rcv.nxt + (long)tcb->rcv.wnd)) > 0){
```

Der Cast auf `long` beseitigt den Signed-Overflow — nimmt aber genau das weg, worauf
die TCP-Sequenzarithmetik beruht. Sequenznummern rechnen **modulo 2³²**; das klassische
Idiom lebt davon, dass die 32-Bit-Subtraktion umläuft und das Ergebnis vorzeichen-
behaftet interpretiert wird. Da `excess` selbst `long` (64 Bit) ist, wird das Ergebnis
nicht mehr auf 32 Bit zurückgeschnitten, und an der Vorzeichengrenze kippt die
Entscheidung:

```
ACK an der Vorzeichengrenze        seq=7fffffff nxt=80000000 wnd=4096 len=0
   long-Variante : excess=4294963199     trim=JA   *length -> 4097
   modulo-2^32   : excess=-4097          trim=nein *length -> 0     <<< weicht ab
```

Ein Segment ohne Nutzdaten bekommt also `*length = 4097` zugewiesen, obwohl die
mbuf-Kette leer ist. `trim_mbuf()` tut bei zu großer Länge nichts, die Diskrepanz
zwischen `*length` und dem tatsächlichen Pufferinhalt bleibt aber bestehen. Im
zweiten Fall (mit Daten) stimmt das Ergebnis nur zufällig, weil `(uint) excess` die
oberen Bits wieder abschneidet — dafür bekommt `tcb->rerecv` einen Zuwachs von
4,29 Milliarden.

Erreichbarkeit: die Grenze 0x7fffffff↔0x80000000 wird einmal pro 4 GB Sequenzraum
überschritten — im Packet-Radio praktisch nie **von selbst**. Die ISN wählt aber die
Gegenstelle. Ein bösartiger Peer setzt seine ISN auf 0x7ffffff0 und ist nach wenigen
hundert Byte an der Grenze.

`dupcnt` in derselben Funktion ist unkritisch: `tcb->rcv.nxt - seg->seq` wird noch in
`int32` gerechnet (das Umlaufen bleibt erhalten) und erst danach nach `long`
erweitert. Formal ist die Subtraktion aber weiterhin Signed-Overflow-UB.

**Korrektes Idiom für beide** — unsigned rechnen (definiertes Umlaufen), dann als
vorzeichenbehaftet interpretieren:

```c
	int32 dupcnt, excess;
	...
	dupcnt = (int32)((uint32)tcb->rcv.nxt - (uint32)seg->seq);
	...
	excess = (int32)((uint32)seg->seq + (uint32)*length
	                 - ((uint32)tcb->rcv.nxt + (uint32)tcb->rcv.wnd));
	if (excess > 0) { ... }
```

Dieselbe Prüfung lohnt für `seq_lt/le/gt/ge/within` (`tcp.h:290-294`) und
`in_window()` (`tcpin.c`), die mit `(int32)(tcb->rcv.nxt + tcb->rcv.wnd - 1)` rechnen —
dort ist die Addition ebenfalls Signed-Overflow-UB, das Ergebnis durch den Cast auf
`int32` aber semantisch richtig.

### Restpunkt aus `7e8b140`

`rt_blookup()` (`src/iproute.c:653`) ist die einzige der drei Maskenstellen **ohne**
`bits > 32`-Klemmung; bei `bits > 32` wäre `32-bits` als `unsigned int` riesig und der
Shift um ≥ 32 UB. Aktuell nicht erreichbar — der einzige Aufrufer mit variablem
`bits` ist `rip.c:503`, und `nbits()` (`rip.c:424`) liefert nur 0/8/16/24/32. Rein
defensiv, zwei Zeilen.

---

## 1. Kritische Befunde

### W-01 — DNS-Server: Heap-Overflow beim Serialisieren der Antwort

`src/domhdr.c:424-456`

```c
struct mbuf *htondomain(const struct dhdr *dhp)
{
  Compress_table[0].name = 0;
  bp = alloc_mbuf(512);          /* liefert real 2012 Byte (LARGE_MBUF) */
  cp = bp->data;
  ...
  cp = putq (bp->data, cp, dhp->questions);
  cp = putrr(bp->data, cp, dhp->answers);
  cp = putrr(bp->data, cp, dhp->authority);
  cp = putrr(bp->data, cp, dhp->additional);
  bp->cnt = cp - bp->data;
```

`putq()`/`putrr()` laufen über die **gesamte** RR-Liste und schreiben ohne jede
Grenzprüfung in den Puffer. Es gibt kein `if (cp - bp->data > …) break;`.

Angriff (UDP oder TCP Port 53, unauthentifiziert):

* Eine DNS-Query mit z. B. 100 Question-Records à ~200 Byte Name wird von
  `ntohdomain()` klaglos geparst und von `domain_server()` in die Antwort übernommen
  (`putq` schreibt alle Questions zurück). ~20 KB in einen 2012-Byte-Heap-Puffer.
* Verschärfend: `ntohdomain()` prüft `qdcount`/`ancount`/`nscount`/`arcount` (je bis
  65535) **nicht** gegen die tatsächliche Paketlänge. Ein 12-Byte-Paket mit
  `qdcount=65535` lässt `getq()` weit hinter dem Puffer weiterparsen (siehe W-06/W-07)
  und erzeugt 65535 RR-Strukturen, die anschließend alle geschrieben werden.

Der überschriebene Inhalt ist vollständig angreiferkontrolliert (Namen aus dem Query).
Das ist die sauberste RCE-Primitive im gesamten Baum.

Die 512 → 2012 Byte sind reiner Zufall der mbuf-Größenklassen in `mbuf.c:18-20`; wer
dort etwas ändert, verkleinert das Fenster auf 512.

**Minimalfix:** Grenze mitführen und bei Überschreitung TC-Bit setzen und abbrechen —
also das, was RFC 1035 ohnehin verlangt:

```c
#define DOMBUFLEN 512
/* putq/putrr/putname/putstring bekommen ein 'uint8 *end' und geben NULL zurück,
   sobald cp + benötigte_bytes > end.  htondomain setzt dann dhp->tc = 1 und
   schneidet nach dem letzten vollständigen RR ab. */
```

---

### W-02 — FTP-Server: Format-String über Dateinamen

`src/ftpserv.c:106-116`, `274-279`, Aufrufe u. a. `394, 406, 428, 448, 475, 494, 518, 543, 562, 576, 626, 659`

```c
static void Xprintf(struct tcb *tcb,char *message,char *arg1,char *arg2,char *arg3)
{
	bp = alloc_mbuf(256);
	sprintf((char *) bp->data, message, arg1, arg2, arg3);   /* message = Formatstring */
	...
}

static char *errmsg(const char *filename)
{
  static char buf[1024];
  sprintf(buf, "550 %s: %s.\r\n", filename, strerror(errno));   /* filename remote */
  return buf;
}
```

und dann durchgehend:

```c
	file = pathname(ftp->cd,arg);
	AsUser(ftp->fp = fopen(physname,"r"));
	if(ftp->fp == NULL){
		Xprintf(ftp->control,errmsg(file),"","","");   /* <-- Rückgabe als Format */
```

`errmsg()` baut den angreiferkontrollierten Pfad in einen String, der anschließend als
**Formatstring** an `sprintf()` geht. `RETR %n%n%n%n` schreibt an von der Stack-Lage
abhängige Adressen; `%s`/`%x` lecken Speicher. `pathname()` filtert `%` nicht.

Erreichbar für jeden angemeldeten FTP-User (`ftp->cd != NULL`), also auch für einen
Anonymous-Account, falls in `ftpusers` eingetragen.

Zusätzlich läuft `errmsg()` selbst über: `"550 " + bis zu 1023 Byte Pfad + strerror()`
passt nicht in `static char buf[1024]`.

**Fix:** `Xprintf(ftp->control, "%s", errmsg(file), "", "")` genügt nicht, weil die
Signatur drei `char*` erwartet — sauberer ist eine eigene `Xputs(tcb, const char *)`
für vorformatierte Meldungen plus `snprintf()` in `errmsg()`.

---

### W-03 — BBS: `popen()` mit unvalidierten Adressen, in einem setuid-root-Binary

`bbs/bbs.c:756-772`, Installation: `bbs/Makefile:21` → `chmod 4555 /usr/local/bin/bbs`

```c
static void send_to_mail_or_news(struct mail *mail, enum e_type dest)
{
  char cmid[1024];
  char command[1024];
  char path[1024];
  ...
    sprintf(command, SENDMAIL_PROG " -oi -oem -f %s@%s %s@%s",
	    mail->fromuser, mail->fromhost,
	    mail->touser, mail->tohost);
  if (!(fp = popen(command, "w")))
```

`fromuser`, `fromhost`, `touser`, `tohost` sind je `char[1024]` (`bbs.c:105-108`) und
kommen aus den Headern der eingelieferten Nachricht. `split_address()`
(`bbs.c:669-711`) ersetzt lediglich `%` durch `@` und macht Kleinbuchstaben — `;`,
`` ` ``, `|`, `$(`, `&` passieren unverändert.

Zwei getrennte Probleme:

1. **Command Injection.** `popen()` startet `/bin/sh -c`. Eine Nachricht an
   `` foo;`id>/tmp/x`@bar `` führt Code aus. Das Binary ist setuid root installiert.
2. **Stack-Overflow.** 4 × bis zu 1023 Byte in `command[1024]`.

Dazu im selben Block `path[1024]`, das in einer Schleife über alle `R:`-Header per
`strcat()` unbegrenzt wächst.

Weitere `system()`/`popen()`-Stellen mit interpolierten Werten: `bbs.c:1964`
(`CTLINND_PROG " -s flush %s", user.name`), `bbs.c:2329`.

**Fix:** `popen()`/`system()` durch `fork()` + `execv()` mit Argumentvektor ersetzen
(kein Shell-Interpreter), Adressen gegen ein Whitelist-Zeichenset validieren,
`snprintf()` überall, und das setuid-Bit prüfen — falls es nur für `seteugid()` beim
Zugriff auf User-Spools nötig ist, geht das auch über eine Gruppe statt setuid root.

---

### W-04 — VJ-Header-Kompression: OOB-Read/Write auf `rstate[255]`

`src/slhc.c:56-91` (Init) und `src/slhc.c:411-436` (Verwendung)

Das ist die Fundstelle aus dem Linux-Abgleich. WAMPES:

```c
	comp->xmit_oldest = 0;
	comp->xmit_current = 255;
	comp->recv_current = 255;

	if ( tslots > 0 ) {
```

Linux (`drivers/net/slip/slhc.c`, unverändert seit CSLIP-Zeiten):

```c
	comp->xmit_oldest = 0;
	comp->xmit_current = 255;
	comp->recv_current = 255;
	/*
	 * don't accept any packets with implicit index until we get
	 * one with an explicit index.  Otherwise the uncompress code
	 * will try to use connection 255, which is almost certainly
	 * out of range
	 */
	comp->flags |= SLF_TOSS;
```

**In WAMPES fehlt `comp->flags |= SLF_TOSS;`.** Der Kommentar im Linux-Original
beschreibt exakt das, was hier passieren kann:

```c
	} else {
		/* this packet has an implicit state index. ... */
		if(comp->flags & SLF_TOSS){     /* flags ist calloc'd = 0  -> Prüfung greift nicht */
			comp->sls_i_tossed++;
			return 0;
		}
	}
	cs = &comp->rstate[comp->recv_current];   /* recv_current == 255 */
	thp = &cs->cs_tcp;
	...
	thp->checksum = (uint)x;                  /* Schreibzugriff */
	thp->wnd += (uint)x;  thp->ack += x;  thp->seq += x;
	cs->cs_ip.id += (uint)x;
	cs->cs_ip.length = len;
```

`rstate` hat 16 Einträge (SLIP, `slip.c:55` `slhc_init(16,16)`) bzw. 32
(AX.25-VJ, `ax25.c:111/227/254` `slhc_init(32,32)`). `sizeof(struct cstate)` liegt bei
~150 Byte, `rstate[255]` also ~35 KB hinter dem Allokat. Geschrieben werden dort
angreiferkontrollierte 16/32-Bit-Werte; anschließend wird der Bereich per `htontcp()` /
`htonip()` wieder ausgelesen und als Paket weitergeroutet → zusätzlich Heap-Disclosure.

**Trigger:**
* SLIP/VJ: das *erste* empfangene COMPRESSED_TCP-Frame ohne `NEW_C`-Bit
  (`slip.c:263-269`).
* AX.25/VJ: `ax_rx_vjcomp()` legt `slcomp` beim ersten Frame an und verwirft es
  (`ax25.c:226-230`), das *zweite* Frame trifft dann auf `SLF_TOSS == 0` →
  über die Luft auslösbar auf jeder AX.25-Verbindung mit VJ.

**Fix — eine Zeile:**

```diff
 	comp->xmit_oldest = 0;
 	comp->xmit_current = 255;
 	comp->recv_current = 255;
+	/* Keine Pakete mit implizitem Index akzeptieren, bis einer mit explizitem
+	 * Index kam - sonst indiziert slhc_uncompress() rstate[255].  (wie Linux) */
+	comp->flags |= SLF_TOSS;
```

Empfohlen zusätzlich (analog Linux `3f01ddb9`, 2018): ein `initialized`-Flag pro
`cstate`, das `slhc_remember()` setzt und `slhc_uncompress()` prüft — sonst werden
Header aus einem nie befüllten Slot rekonstruiert.

---

### W-05 — AXIP/AXUDP: unbegrenzter Adressfeld-Walk im Empfangspfad

`src/axip.c:141-182`

```c
  uint8 buf[MAX_FRAME];                 /* 2048 */
  l = recvfrom(edv->fd, (char *)(bufptr = buf), sizeof(buf), 0, ...);
  ...
  if (l <= 2) goto Fail;
  if (!check_crc_ccitt((char *) bufptr, l)) goto Fail;
  l -= 2;
  ...
  p = src = bufptr + AXALEN;
  while (!(p[6] & E)) {                 /* keine Grenze gegen bufptr + l */
    p += AXALEN;
    if (p[6] & REPEATED)
      src = p;
    else
      break;
  }
  axip_route_add(src, ntohl(addr.sin_addr.s_addr));
```

Die Schleife läuft in 7-Byte-Schritten vorwärts, bis sie ein Byte mit gesetztem
E-Bit oder gelöschtem REPEATED-Bit findet. Weder `l` noch `sizeof(buf)` begrenzen sie.

Ein Datagramm, bei dem ab Offset 13 jedes siebte Byte `0x80` gesetzt und `0x01`
gelöscht hat, treibt `p` über das Ende des 2048-Byte-Stackpuffers hinaus. Die CRC ist
trivial berechenbar, es gibt keine Authentifizierung.

Folge: OOB-Read auf dem Stack (meist SIGSEGV = DoS); wenn die Schleife hinter dem
Puffer terminiert, werden **7 Byte fremden Stack-Speichers** über
`axip_route_add()` in die AX.25-Routing-Tabelle übernommen und sind per
`ax route`-Kommando sichtbar → Speicher-Disclosure.

Dieselbe Schleife ohne Grenze steht im Sendepfad, `axip.c:94-100`.

Nebenbefund in derselben Funktion: `if (l <= sizeof(struct ip))` vergleicht `int`
gegen `size_t`; bei `recvfrom() == -1` wird `-1` zu `SIZE_MAX` und die Prüfung greift
nicht (der spätere `l <= 2`-Test fängt es ab, korrekt ist es trotzdem nicht).

**Fix:**

```c
  uint8 *end = bufptr + l;
  p = src = bufptr + AXALEN;
  if (l < 2 * AXALEN) goto Fail;
  while (p + AXALEN <= end && !(p[6] & E)) {
      p += AXALEN;
      if (p + AXALEN > end) goto Fail;
      if (p[6] & REPEATED) src = p; else break;
  }
```

---

## 2. Der Linux-Abgleich (slhc / slip / ppp)

Die gemeinsame Codebasis ist Van Jacobsons CSLIP-Code von 1989, über KA9Q NOS in
WAMPES und über CSLIP/Katie Stevens in Linux gelandet. Linux hat den Code seit 2011
unter `drivers/net/slip/slhc.c`; dort wurde er mehrfach durch syzkaller/KMSAN
auditiert. WAMPES arbeitet auf mbuf-Ketten statt auf einem flachen Puffer, was
manche Klassen von vornherein entschärft — deshalb ist nicht jeder Linux-Fix
übertragbar.

| Linux-Commit | Datum | Thema | Status in WAMPES |
|---|---|---|---|
| `4ab42d78` | 2015-11-01 | *ppp, slip: Validate VJ compression slot parameters completely* (CVE-2015-7799) — `rslots`/`tslots` außerhalb 0–255 → NULL-Deref | **nicht betroffen in der Praxis.** `slhc_init()` prüft `> 0 && < 256` wie Linux vor dem Fix. WAMPES ruft aber nur fest mit 16/16 bzw. 32/32 auf, `callocw()` schlägt nie fehl. Kein konfigurierbarer Pfad. Defensiv trotzdem sinnvoll. |
| `3f01ddb9` | 2018-04-11 | *slip: Check if rstate is initialized before uncompressing* — Rekonstruktion aus einem nie befüllten Slot | **fehlt.** WAMPES hat kein `initialized`-Flag. Der Linux-Endlosloop in `ip_fast_csum` tritt hier nicht auf (WAMPES' `cksum()` bekommt eine explizite Länge), aber es werden Header aus Nullspeicher gebaut. |
| `baf76f0c` | 2019-04-25 | *slip: make slhc_free() silently accept an error pointer* | nicht relevant (kein `ERR_PTR`). |
| `110a40df` | 2020-03-04 | *slip: make slhc_compress() more robust against malicious packets* — `ip->version != 4`, `ihl < 5`, Länge < IP+TCP, `th->doff` prüfen | **teilweise.** `ntohip()` (`iphdr.c:87`) prüft `ihl >= IPLEN`, `ntohtcp()` prüft `hdrlen >= TCPLEN` und `optlen <= len_p()`. Es fehlt: `version != 4` und die Prüfung, ob nach dem IP-Header überhaupt ein TCP-Header vorhanden ist. `slhc_compress()` verarbeitet den Sendepfad — geringeres Risiko. |
| `7d3fce8c` | 2024-10-09 | *slip: make slhc_remember() more robust against malicious packets* — verlangt `isize >= sizeof(iphdr)`, `ihl >= 5`, `isize >= ihl*4 + sizeof(tcphdr)`, `doff >= 5`, `isize >= ihl*4 + doff*4` | **fehlt weitgehend** → siehe W-09. |
| `e7660744` | 2026-04-15 | *slip: reject VJ receive packets on instances with no rstate array* — `if (!comp->rstate) goto bad;` in `slhc_uncompress()` und `slhc_remember()` | **fehlt.** In WAMPES nicht auslösbar (rstate immer allokiert), aber die zwei Zeilen kosten nichts. |
| `4c1367a2` | 2026-04-16 | *slip: bound decode() reads against the compressed packet length* | **nicht betroffen.** WAMPES' `decode()` (`slhc.c:127-138`) arbeitet über `PULLCHAR`/`pull16` auf mbufs, liefert `-1` bei Pufferende, und jede Aufrufstelle prüft auf `-1`. Das mbuf-Design ist hier dem flachen Linux-Puffer überlegen. |
| — | — | Das fehlende `comp->flags \|= SLF_TOSS;` in `slhc_init()` | **Divergenz, siehe W-04.** Das ist kein Linux-*Fix*, den man nachziehen müsste — die Zeile steht in Linux (und im BSD-Original) seit jeher und ist in WAMPES verlorengegangen. Das ist der schwerwiegendste Einzelbefund des Abgleichs. |

### W-09 — `slhc_remember()` ohne Mindestlängenprüfung

`src/slhc.c:504-566`

```c
	/* Sneak a peek at the IP header's IHL field to find its length */
	hdrlen = ((*bpp)->data[0] & 0xf) << 2;      /* Lesezugriff vor jeder Längenprüfung */
	if(hdrlen < IPLEN){ ... }
	len = len_p(*bpp);
	ntohip(&iph,bpp);
	if(iph.length > len){ ... }
	slot = iph.protocol;
	if(slot > comp->rslot_limit){ ... }
	ntohtcp(&th,bpp);                            /* Rückgabewert wird ignoriert */
	htontcp(&th,bpp,0,0);
	htonip(&iph,bpp,IP_CS_OLD);
	if(cksum(NULL,*bpp,hdrlen) != 0){ ... }
```

Fehlend gegenüber Linux nach `7d3fce8c`:

* keine Prüfung `len_p(*bpp) >= IPLEN` **vor** dem Zugriff auf `(*bpp)->data[0]`;
* keine Prüfung, dass hinter dem IP-Header ein vollständiger TCP-Header liegt —
  `ntohtcp()` liefert bei zu kurzem Puffer `-1`, das wird nicht ausgewertet, und
  `th` enthält dann teilweise uninitialisierte Werte (Linux' KMSAN-Report war genau das);
* `cksum(NULL, *bpp, hdrlen)` wird mit `hdrlen` bis 60 über eine u. U. kürzere
  mbuf-Kette gerufen. `cksum()` bricht bei `m == NULL` ab, ist also nicht direkt
  unsicher, prüft aber nicht, ob es die verlangten Bytes überhaupt gesehen hat.

**Fix:**

```c
	len = len_p(*bpp);
	if (len < IPLEN) { comp->sls_i_error++; return slhc_toss(comp); }
	hdrlen = ((*bpp)->data[0] & 0xf) << 2;
	if (hdrlen < IPLEN || len < hdrlen + TCPLEN) { comp->sls_i_error++; return slhc_toss(comp); }
	...
	if (ntohtcp(&th,bpp) < 0) { comp->sls_i_error++; return slhc_toss(comp); }
```

### PPP

In diesem Baum nicht vorhanden (kein `ppp.c`). Die PPP-bezogenen Linux-CVEs treffen
nur über den gemeinsamen `slhc.c` zu — das ist oben abgedeckt.

---

## 2b. KISS / SLIP / NRS — der Empfangspfad

Alle drei asy-Modi (`slip`, `kissui`/`ax25ui`, `kissi`/`ax25i`) laufen über
`slip_rx()` → `slip_decode()`; NET/ROM-Serial über `nrs_recv()` → `nrs_decode()`.
Der eigentliche KISS-Code (`src/kiss.c`, 175 Zeilen) ist knapp und weitgehend sauber.
Das Problem sitzt eine Ebene tiefer, im Framing.

### W-25 — Keine Frame-Längenbegrenzung beim Empfang

`src/slip.c:210-234` und `src/nrs.c:183-211`

```c
	if(sp->rbp_head == NULL){
		if((sp->rbp_tail = sp->rbp_head = alloc_mbuf(SLIP_ALLOC)) == NULL)
			return NULL;
		sp->rcp = sp->rbp_head->data;
	} else if(sp->rbp_tail->cnt == SLIP_ALLOC){
		if((sp->rbp_tail->next = alloc_mbuf(SLIP_ALLOC)) == NULL){ ... }
		sp->rbp_tail = sp->rbp_tail->next;
		sp->rcp = sp->rbp_tail->data;
	}
	*sp->rcp++ = c;
	sp->rbp_tail->cnt++;
```

Es gibt **keine Obergrenze** für die Frame-Länge. Solange kein `FR_END` (bzw. `ETX`)
kommt, wird die mbuf-Kette in 220-Byte-Schritten beliebig weit verlängert. `iface->mtu`
wird beim Empfang nicht konsultiert. Ein defektes oder bösartiges TNC — bzw. alles,
was Bytes in die serielle Leitung schiebt — treibt den Speicherverbrauch bis zum
OOM hoch. Bei 9k6 sind das ~1 MB pro Viertelstunde, bei einem KISS-over-TCP-Setup
oder einem lokal angeschlossenen Gerät entsprechend schneller.

Bezeichnend: `struct slip` hat dafür bereits ein Feld —

```c
	uint rcnt;              /* Length of mbuf chain */      /* slip.h:41 */
```

— das in `slip.c` **nirgends benutzt** wird. In `nrs.c` wird es gepflegt
(`nrs.c:210 sp->rcnt++`), aber ausschließlich für die Statusanzeige
(`nrs.c:253`), nie als Grenze. Der Zähler war offensichtlich genau für diesen Zweck
vorgesehen und die Prüfung ist nie geschrieben worden.

Linux macht es in `drivers/net/slip/slip.c` seit jeher so:

```c
	if (sl->rcount < sl->buffsize) {
		sl->rbuff[sl->rcount++] = c;
		return;
	}
	sl->dev->stats.rx_over_errors++;
	set_bit(SLF_ERROR, &sl->flags);
```

**Fix** (analog, in `slip_decode()` vor dem Store; `nrs_decode()` entsprechend):

```c
	if(sp->rcnt >= SLIP_RXMAX){          /* z.B. 2*iface->mtu + 64, mind. 512 */
		free_p(&sp->rbp_head);
		sp->rbp_head = sp->rbp_tail = NULL;
		sp->rcnt = 0;
		sp->errors++;
		return NULL;                 /* bis zum nächsten FR_END verwerfen */
	}
	*sp->rcp++ = c;
	sp->rbp_tail->cnt++;
	sp->rcnt++;
```

und `sp->rcnt = 0;` im `FR_END`-Zweig (`slip.c:181-190`) ergänzen.

### W-26 — `kiss_recv()` liest das Typbyte ohne Längenprüfung

`src/kiss.c:94, 101` — `if(bp && (*bp->data & 0x80))` greift auf `bp->data[0]` zu,
ohne `bp->cnt > 0` zu prüfen. Aktuell nicht auslösbar, weil `slip_decode()` nie eine
leere mbuf zurückgibt (ein Zeichen ist immer gespeichert, bevor die Kette existiert).
Die Invariante ist aber nirgends dokumentiert und bricht, sobald jemand am Framing
dreht. Eine Zeile `if (!bp || !bp->cnt) { free_p(bpp); return; }` am Funktionsanfang.

### W-27 — CRC-Modus wird durch empfangene Frames dauerhaft umgeschaltet

`src/kiss.c:93-107` in Verbindung mit `kiss.c:66-79`

```c
	if(bp && (*bp->data & 0x80)){
		if(check_crc_16(bp)){ iface->crcerrors++; free_p(bpp); return; }
		iface->crccontrol = CRC_16;              /* persistent */
	}else if(bp && (*bp->data & 0x20)){
		if(check_crc_rmnc(bp)){ ... }
		iface->crccontrol = CRC_RMNC;            /* persistent */
	}
```

Das ist die beabsichtigte SMACK/RMNC-Autodetektion, und sie verlangt eine gültige
CRC — insofern kein Fehler. Erwähnenswert bleibt, dass ein Zustand des Interfaces
(und damit das Sendeformat, `kiss_raw()`) allein durch unauthentifizierte
Eingangsdaten umgeschaltet wird. Wer das nicht will, sollte den Autodetekt per
Konfiguration festnageln können.

### Geprüft, in Ordnung

* `check_crc_16()` und `check_crc_rmnc()` (`src/crc.c`) — die Abschneide-Logik
  (`cnt < 3`-Prüfung, Suche der Schnitt-mbuf, `free_p(&bp->next)`) terminiert korrekt
  und schneidet nicht über den Puffer hinaus.
* `get_asy()` (`src/n8250.c`) — wertet den `read()`-Rückgabewert korrekt aus und gibt
  bei `<= 0` eine 0 zurück; `slip_rx()`s `while(--cnt >= 0)` verarbeitet dann nichts.
  Es werden **keine** ungelesenen Pufferpositionen an den Decoder gereicht.
  Damit ist WAMPES von der Klasse **CVE-2026-53082** (siehe unten) nicht betroffen.
* `slip_encode()` / `nrs_encode()` — Worst-Case-Allokation (`2*len+2` bzw. `2*len+5`)
  reicht exakt.

### 6pack — Checkliste für die geplante Implementierung

Noch nicht implementiert. Der Fix, den du auf kernel.org gesehen hast, ist mit hoher
Wahrscheinlichkeit dieser:

* **`bf9a388` / CVE-2026-53082**, *net: hamradio: 6pack: fix uninit-value in
  `sixpack_receive_buf`* — Autorendatum 07.04.2026, CVE veröffentlicht Sommer 2026
  (daher „vor Tagen bis Monaten"). Die Schleife lief über die TTY-Flag-Bytes, hat den
  Datenzeiger `cp` aber nie weitergeschoben und dann `sixpack_decode()` mit dem
  *ursprünglichen* Count aufgerufen — inklusive der Positionen, die wegen
  TTY-Fehlerflags hätten übersprungen werden müssen. Die TTY-Schicht garantiert für
  solche Positionen keinen definierten Wert → KMSAN-Report, uninit-value.
  Der Fix dekodiert byteweise und überspringt Fehlerbytes wirklich.

Die übrigen 6pack-Fixes, die man beim Neuschreiben kennen sollte:

| Commit | Datum | Thema |
|---|---|---|
| `19d1532` | 2021-08-16 | *fix slab-out-of-bounds in decode_data* — Schreiben über den 400-Byte-RX-Puffer hinaus |
| `3c0d2a4` | 2021-09-16 | *fix tx timeout and slot time* |
| `0b91119` | 2021-11-09 | *defer 6pack kfree after unregister_netdev* |
| `81b1d54` | 2021-11-13 | *remove needs_free_netdev to avoid UAF* |
| `efe4186` | 2022-02-18 | *fix UAF bug caused by mod_timer()* |
| `2b04495` | 2022-06-17 | *fix array-index-out-of-bounds in decode_std_command()* — uninitialisiertes `rx_count_cooked` |
| `bf9a388` | 2026-04-07 | *fix uninit-value in sixpack_receive_buf* (CVE-2026-53082) |

Kurzfassung der Lehren: der Zustandsautomat muss **vor jedem** Byte-Store gegen die
Puffergrenze prüfen (das ist genau W-25 in Grün), Zähler wie `rx_count_cooked` müssen
bei jedem Zustandswechsel zurückgesetzt werden, und Timer dürfen den Teardown nicht
überholen. Wenn W-25 sauber gelöst ist, erbt eine 6pack-Implementierung auf demselben
Framing-Unterbau die Absicherung.

---

## 2c. NET/ROM — Abgleich mit den Linux-Fixes

Wichtige Einordnung vorweg: anders als bei `slhc.c` gibt es hier **keine gemeinsame
Codebasis**. `src/netrom.c` ist eine Eigenimplementierung von DK5SG (kein
NOS-`nr3.c`/`nr4.c`-Erbe im Header), Linux' `net/netrom/` stammt von G4KLX. Was sich
überträgt, sind die *Fallen des Protokolls*, nicht fehlende Patches. Zwei der
Linux-Fixes sind trotzdem lehrreich, weil sie exakt die Stellen betreffen, die in
WAMPES funktional entsprechen. Ausgenommen sind die zahlreichen `sysctl`-Data-Races
(2024-03) und die Locking-Fixes — WAMPES ist single-threaded/kooperativ und hat
kein Äquivalent.

| Linux-Commit | Datum | Thema | Status in WAMPES |
|---|---|---|---|
| `a4fd163` | 2024-12-23 | *netrom: check buffer length before accessing it* — `nr_route_frame()` griff auf `skb->data[0..14]` zu, ohne `skb->len` zu prüfen; Fix: `if (skb->len < 2*sizeof(ax25_address) + 1) return 0;` | **nicht betroffen.** `route_packet()` hat die Prüfung seit jeher: `netrom.c:578 if (!bpp \|\| !*bpp \|\| (*bpp)->cnt < 15) goto discard;` — identische Schranke (2×7+1). |
| `ba1096c` / **CVE-2026-23098** (CVSS 8.8) | 2026-01-21 | *netrom: fix double-free in `nr_route_frame()`* — `old_skb` wurde freigegeben, ohne zu prüfen, ob `nr_neigh->ax25` NULL ist; der Aufrufer gab es dann ein zweites Mal frei | **nicht betroffen, strukturell.** WAMPES reicht überall `struct mbuf **` durch und `free_p()` setzt `*bpp = NULL`. Ein zweites `free_p()` auf denselben Zeiger ist ein No-op. `send_packet_to_neighbor()` (`netrom.c`) gibt im Fehlerfall `free_p(bpp)` frei und kehrt zurück, der Aufrufer `route_packet()` fasst danach nichts mehr an. Die NOS-Konvention `**` statt `*` immunisiert gegen diese ganze Klasse. |
| `613d12d` | 2025-12-04 | *Fix memory leak in `nr_sendmsg()`* | kein Äquivalent (kein Socket-Layer). |
| `0b91302` | 2024-06-17 | *Fix a memory leak in `nr_heartbeat_expiry()`* | Timer-Pfade in `netrom.c` (`l4_t1_timeout` u. a.) sind nicht gegengeprüft worden — offen. |
| `e03e7f2` | 2024-05-17 | *fix possible dead-lock in `nr_rt_ioctl()`* | nicht relevant (kein Locking). |
| 2024-03-07-Serie (10 Commits) | 2024 | `sysctl`-Data-Races | nicht relevant. |

**Der Restbefund derselben Klasse wie `a4fd163` steht aber in WAMPES** — nur eine
Ebene weiter unten, siehe **W-14**: `route_packet()` garantiert `cnt >= 15`, liest im
L3RTT-Zweig aber `data[19]` und `memcmp(…, data+20, 6)`. Ein 15-Byte-NET/ROM-Frame
an „L3RTT" liest bis zu 11 Byte über die mbuf hinaus. Linux hat diese Stelle nicht,
weil `nr_route_frame()` kein L3RTT-Parsing macht — die Prüfung fehlt hier trotzdem.

Zusätzlich als NET/ROM-spezifisch offen: **W-19** (unbegrenzte Circuit-Allokation
per NR4-CONRQ) und **W-28**.

### W-28 — `broadcast_recv()` vertraut auf `IDENTLEN`-Grenzen ohne Frame-Prüfung

`src/netrom.c:461-501`. Die Schleife `while (pullup(bpp, buf, NRRTDESTLEN) == NRRTDESTLEN)`
ist korrekt begrenzt (`buf[21]`, gelesen wird bis `buf[20]`). Was fehlt, ist eine
Obergrenze für die Anzahl der Routing-Einträge pro Broadcast: jeder Eintrag erzeugt
über `nodeptr(buf, 1)` bei Bedarf einen neuen `struct node` samt `linkinfo`. Ein
einzelner großer NET/ROM-Nodes-Broadcast (über AXUDP praktisch beliebig groß) legt
entsprechend viele Knoten an. Das anschließende `calculate_all()` läuft im Block
„calculate new neighbor and quality values" in O(Nachbarn × Knoten) plus einer
rekursiven `calculate_qualities()`-Traversierung je Nachbar, und wird aus
`route_packet()` zusätzlich pro Paket angestoßen, sobald sich ein Link ändert.
Kombination aus Speicher- und CPU-DoS über Funk.

Nebenbefund in derselben Funktion: die Rückwärtssuche beim Entfernen ungültiger Links

```c
	for (pl1 = pn1->links; pl1->node != pn; pl1 = pl1->next) ;
```

hat keine NULL-Absicherung. Sie setzt voraus, dass zu jedem Link immer der
Gegenlink existiert. Falls diese Invariante je bricht, ist das ein
NULL-Pointer-Deref statt einer Fehlerbehandlung.

### Kontext, nicht Befund

Der AX.25/NET-ROM/ROSE-Stack **und die hamradio-Treiber wurden am 23.04.2026 aus dem
Linux-Mainline entfernt** (Commit `dd8d4bc`, „a huge bug/syzbot magnet", Pflege
out-of-tree). Für den Abgleich heißt das: die Kernel-Historie bis April 2026 bleibt
die Referenzquelle, künftige Fixes muss man im out-of-tree-Repo suchen. Für die
`libax25`/`ax25-tools`-Arbeit in diesem Projekt ist das ohnehin relevant.

---

## 2d. SNTP (`src/sntp.c`)

`start sntp` steht in **allen fünf** mitgelieferten `net.rc.*` — der SNTP-Server läuft
also per Default auf UDP/123. Der Client wird per `sntp add <host> [intervall]`
konfiguriert.

### W-29 — `unsigned long` für 32-Bit-Wire-Felder: LP64-Bruch mit Infoleak

`src/sntp.c:290-327` (`ntohntp`) und `:254-286` (`htonntp`)

```c
static int ntohntp(struct pkt *pkt, struct mbuf **bpp)
{
	int n;
	unsigned long *p;
	unsigned long buf[12];          /* auf LP64: 96 Byte */
	unsigned long w;

	n = pullup(bpp, buf, NTP_MIN_PACKET_SIZE);      /* füllt 48 Byte */
	free_p(bpp);
	if (n < NTP_MIN_PACKET_SIZE) return -1;
	p = buf;
	w = ntohl(*p++);                /* liest 8 Byte, nicht 4 */
	...
	pkt->org.i  = ntohl(*p++);      /* Read #7  -> buf[6]  */
	pkt->org.f  = ntohl(*p++);      /*          -> buf[7]  */
	pkt->rec.i  = ntohl(*p++);      /*          -> buf[8]  */
	pkt->rec.f  = ntohl(*p++);      /*          -> buf[9]  */
	pkt->xmt.i  = ntohl(*p++);      /*          -> buf[10] */
	pkt->xmt.f  = ntohl(*p++);      /*          -> buf[11] */
```

Das NTP-Wire-Format besteht aus 32-Bit-Wörtern. `unsigned long` ist auf LP64 aber
64-bittig. Konsequenzen:

* **`buf[12]` ist 96 Byte groß, `pullup()` füllt nur 48.** Die zwölf `*p++`-Lesezugriffe
  konsumieren aber 96 Byte — `buf[6..11]`, also **48 Byte uninitialisierter Stack**,
  gehen in `pkt->org`, `pkt->rec` und `pkt->xmt`.
* **Jedes Feld ab dem zweiten wird vom falschen Offset gelesen** (Schrittweite 8 statt 4).
  SNTP ist auf 64 Bit damit schlicht funktionsunfähig.
* **Der Server sendet einen Teil davon zurück.** `sntp_server()` (`:410`) setzt
  `pkt.org = pkt.xmt;` — und `pkt.xmt` stammt aus `buf[10]/buf[11]`, also aus
  uninitialisiertem Stack. `htonntp()` schreibt `org` an Wortposition 6/7; mit
  `bp->cnt = NTP_PACKET_SIZE` (60) gehen davon **8 Byte pro Antwort aufs Netz**.
  Jeder, der ein MODE_CLIENT-Paket an UDP/123 schicken kann, liest damit
  fortlaufend Stackspeicher aus — beliebig oft wiederholbar.

Das ist exakt die Klasse, die im Kernel als „uninit-value" per KMSAN gefunden wird
(vgl. CVE-2026-53082 bei 6pack), nur hier mit Ausleitung ans Netz statt nur
Fehlverhalten.

Auf der Sendeseite dasselbe Muster: `htonntp()` schreibt 13 × 8 Byte + 8 Byte
`memcpy` = 112 Byte in ein `ambufw(60)`. Kein Überlauf — aber nur, weil `ambufw()`
auf `MED_MBUF` (220) aufrundet, dieselbe Zufälligkeit wie bei W-23. Da `htonl()` einen
32-Bit-Wert liefert, der in 64 Bit nullerweitert wird, entsteht auf Little-Endian ein
Wire-Format aus „4 Byte Wert, 4 Byte Null" je Feld.

**Wichtig für die laufende Arbeit:** Deine `global.h`-Änderung (`int32`/`uint32` →
`int`/`unsigned int`) greift hier **nicht**, weil der Code rohes `unsigned long`
verwendet statt der Projekt-Typen. Auf ILP32 war alles korrekt (`buf[12]` = 48 Byte,
12 × 4 Byte = 48). Das ist eine reine LP64-Regression, und Commit `11251af`
(„changes for __x86_64__ systems (some long instead of int types) … sntp.c …") hat
diese Stelle übersehen.

**Fix:** in beiden Funktionen `unsigned long` → `uint32` (bzw. `uint32_t`), dann
stimmen Schrittweite und Puffergröße wieder. Zusätzlich `memset(buf, 0, sizeof buf)`
vor dem `pullup()` als Gürtel-und-Hosenträger.

### W-30 — Client akzeptiert Antworten von beliebigem Absender

`src/sntp.c:449-482` und `:583-593`

```c
	lsocket.address = INADDR_ANY;
	lsocket.port = Lport++;
	...
	peer->ucb = open_udp(&lsocket, sntp_client_recv);
```

Der Client-Socket ist **nicht verbunden**. In `sntp_client_recv()` wird der Absender
zwar geholt —

```c
	if (recv_udp(ucb, &fsocket, &bp) < 0) return;
```

— aber `fsocket` wird danach **nie** mit `peer->fsocket` verglichen. Jeder Host, der
den lokalen UDP-Port erreicht, kann eine Zeitantwort einspeisen.

Verschärfend: der Port kommt aus `uint Lport = 1024;` (`src/main.c:58`), einem global
hochzählenden Zähler. Er ist damit vorhersagbar und bleibt für die Lebensdauer des
Peers offen — ein Angreifer muss ein paar Dutzend Ports durchprobieren, nicht 64k.

**Fix:** in `sntp_client_recv()` nach dem `recv_udp()`

```c
	if (fsocket.address != peer->fsocket.address ||
	    fsocket.port    != peer->fsocket.port) return;
```

### W-31 — Kein Origin-Timestamp-Abgleich (Zero-Origin-Bypass)

`src/sntp.c:477-482`

```c
	if (fpiszero(pkt.org)) {
		if (fpiszero(xmt)) return;
		pkt.org = xmt;          /* akzeptiert, statt zu verwerfen */
	}
	if (fpiszero(pkt.rec)) pkt.rec = pkt.xmt;
	if (fpiszero(pkt.xmt)) return;
```

Der Origin-Timestamp der Antwort wird **nie** gegen den gesendeten `xmt` geprüft.
Kommt er als Null an, setzt der Client kurzerhand seinen eigenen Wert ein. Damit
entfällt der einzige Anti-Spoofing-Mechanismus, den SNTP ohne Krypto hat: der
Angreifer muss den gesendeten Zeitstempel nicht kennen, Null genügt.

Das ist die NTP-Klasse „Zero Origin Timestamp Bypass" (CVE-2015-8138), dort mit
derselben Begründung gefixt. Zusammen mit W-30 heißt das: **jeder Host im Netz kann
die Systemuhr stellen**, sobald ein `sntp add` konfiguriert ist.

**Fix:** `if (fpisne(pkt.org, xmt)) return;` — und den Null-Fall verwerfen statt
zu ersetzen.

### W-32 — `settimeofday()` ohne Panic-Threshold, `tv_sec` auf `int` gekürzt

`src/sntp.c:515-524`

```c
	if (gettimeofday(&tv, 0)) return;
	now = fpadd(sys_clock(), peer->offset);
	tv.tv_sec = (int) (now.i - TIMEBIAS);
	tv.tv_usec = (long) (now.f / USEC2F);
	if (!settimeofday(&tv, 0)) {
```

Oberhalb von `Step_threshold` (Default 1 Sekunde) wird die Uhr sofort per
`settimeofday()` gesetzt — **ohne obere Schranke**. NTP hat dafür die
1000-Sekunden-Panic-Schwelle, oberhalb derer der Daemon sich weigert und meldet.
Hier kann eine einzelne gespoofte Antwort die Uhr auf einen beliebigen Zeitpunkt
setzen; das schlägt auf Zertifikatsprüfungen, Logs, Cron und alles Zeitbasierte durch.

Dazu der Cast `(int)` auf `tv_sec`: `time_t` ist auf LP64 64-bittig, hier wird auf
32 Bit gekürzt → 2038-Problem, und ein präparierter Zeitstempel kann `tv_sec`
negativ machen.

### W-33 — `dumpntp()` schreibt rohe Netzwerkbytes aufs Terminal

`src/sntp.c:338-343` — bei `stratum == 1` werden die vier `refid`-Bytes ungefiltert
per `putchar()` ausgegeben. Ein Angreifer platziert dort Terminal-Escape-Sequenzen.
Nur mit `sntp trace` aktiv, daher niedrig.

### W-34 — Server antwortet ungeprüft, kein Rate-Limit

`src/sntp.c:382-419` — jede MODE_CLIENT-Anfrage wird beantwortet, ohne Rate-Limit und
ohne Prüfung, ob die Quelladresse plausibel ist. Der Verstärkungsfaktor ist mit 48 →
60 Byte gering (kein `monlist`-Äquivalent), aber gespoofte Pakete mit Quellport 123
lassen zwei WAMPES-Instanzen in einer Endlosschleife gegeneinander antworten.

---

## 2e. Service-Inventur — vollständige Abdeckung

`start`-Tabelle aus `src/config.c:155-168`, plus die Dienste, die nicht über `start`
laufen. Spalte „Default" = kommt in den mitgelieferten `net.rc.*` vor.

| Dienst | Datei | Port | Default | Status |
|---|---|---|---|---|
| `domain` | `domain.c`, `domhdr.c` | 53/udp+tcp | **5/5** | W-01, W-06, W-07, W-12, W-17, W-39 |
| `ftp` | `ftpserv.c` | 21/tcp | 2/5 | **W-35**, W-02, W-08, W-13, W-23 |
| `sntp` | `sntp.c` | 123/udp | **5/5** | W-29, W-30, W-31, W-32, W-33, W-34 |
| `time` | `timep.c` | 37/udp | **5/5** | W-42 |
| `echo` | `smisc.c` | 7/tcp | **5/5** | unauffällig |
| `discard` | `smisc.c` | 9/tcp | **5/5** | unauffällig |
| `remote` | `smisc.c` | 1234/udp | – | **W-37**, W-38, W-39 |
| `rip` | `rip.c` | 520/udp | – | **W-40**, W-39 |
| `tcpgate` | `tcpgate.c` | konfigurierbar | 2/5 | W-41 |
| `telnet` | `tnserv.c` → `login.c` | 23/tcp | 1/5 | dünn, Fläche liegt in `login.c` (W-16) |
| `ax25` | `ax25.c`, `lapb.c` | — | 1/5 | W-04, W-18, W-25 |
| `netrom` | `netrom.c` | — | 1/5 | W-14, W-19, W-28 |
| — | `remote_net.c` | Unix-Socket | immer an | **W-36** |
| — | `axip.c` | 93/ip, 10093/udp | attach | W-05 |
| — | `bbs` (setuid) | via Login | — | W-03, W-10 |
| — | `conversd` | Unix/TCP | 2/5 (gated) | W-20 |

---

### W-35 — FTP: Authentifizierungs-Bypass auf Linux

`src/ftpserv.c:707-717` in Verbindung mit `lib/configure:45`

```c
#include <pwd.h>

#if defined __386BSD__ || defined linux

#define crypt(key, salt) (key)          /* <-- */

#else

char *crypt();

#endif
```

und die Prüfung:

```c
  pw = getpasswdentry(ftp->username, 0);
  if (!pw) goto Fail;
  salt[0] = pw->pw_passwd[0];
  salt[1] = pw->pw_passwd[1];
  salt[2] = 0;
  if (pw->pw_passwd[0] &&
      strcmp(pw->pw_name, "ftp") &&
      strcmp(crypt(pass, salt), pw->pw_passwd)) goto Fail;
  ftp->uid = (int) pw->pw_uid;
  ftp->gid = (int) pw->pw_gid;
```

Auf Linux ist `crypt(pass, salt)` per `#define` einfach `pass`. Die Prüfung wird zu

```c
	strcmp(pass, pw->pw_passwd)
```

— das **Klartextpasswort wird gegen das Passwortfeld aus `/etc/passwd` verglichen**.
Auf jedem System mit Shadow-Passwörtern steht dort `"x"`. Also:

```
USER <beliebiger account>
PASS x
```

→ `pw->pw_passwd[0]` ist `'x'` (wahr), `strcmp(name,"ftp")` ist ungleich 0 (wahr),
`strcmp("x","x")` ist 0 (falsch) → die `&&`-Kette ist falsch → **kein `goto Fail`**,
Login erfolgreich. Danach `ftp->uid = pw->pw_uid`, und `AsUser` macht
`seteugid(ftp->uid, ftp->gid)`. Bei `USER root` also `seteugid(0,0)`.

Der zweite Weg ist noch direkter: `getpasswdentry()` in `login.c:342-350` legt
Ham-Accounts mit **leerem** Passwortfeld an —

```c
	fprintf(fp, "%s:%s:%d:%d:Amateur %s:%s:%s\n",
		name, secured ? "x" : "", uid, Gid, name, homedir, Shell);
```

— bei nicht gesetztem `SPASSWDFILE` ist das Feld leer, `pw->pw_passwd[0]` ist 0, die
gesamte Bedingung wird kurzgeschlossen und **jedes beliebige Passwort wird akzeptiert**.

Es gibt keine eingebaute root-Sperre. Die einzige Hürde ist `user_denied()`
(`ftpserv.c:757-773`), das `/etc/ftpusers` liest — eine **Deny-Liste**, und wenn die
Datei fehlt, `fopen()` also scheitert, wird nichts gesperrt.

**Warum das auf Linux und nicht auf macOS greift:** `lib/configure:45` setzt für
Linux/gcc explizit `-Dlinux` (nötig, weil `-ansi` das vordefinierte Makro sonst
entfernt). Auf macOS gilt `CFLAGS='-O -I../lib -D__MACOSX__'`, weder `linux` noch
`__386BSD__` sind gesetzt → der `char *crypt();`-Zweig, also echtes `crypt()`.
Linux ist aber die Plattform, auf der WAMPES tatsächlich betrieben wird.

**Fix — Referenzmodell `axspawn`.** `ax25-tools/ax25/axspawn.c` löst genau dieses
Problem in derselben Umgebung und ist die bessere Vorlage als ein reparierter
`crypt()`-Aufruf. Vier Bausteine, alle direkt übertragbar:

1. **Harte root-Sperre, nicht verhandelbar** (`axspawn.c:1633`):

   ```c
	if (!rootlogin && (pw->pw_uid == 0 || pw->pw_gid == 0))
	{
		write_ax25_static_line(MSG_NOCALL);
		syslog(LOG_NOTICE, "root login of %s (callsign: %s) denied\n", as_user, call);
		return 1;
	}
   ```

   und selbst mit `rootlogin` nur bei md5-/baycom-Passwort, nie im Klartextpfad
   (`axspawn.c:1689`). WAMPES hat gar keine solche Sperre.

2. **Gruppen-Gate statt Deny-Liste** (`axspawn.c:1860`, Konfiguration über
   `getgrnam()` in `axspawn.c:1337`):

   ```c
	|| pwcheck == 2 || (pwcheck == 3 && (pw->pw_gid == user_gid || is_guest))
   ```

   Das ist das Modell, das Thomas beschreibt: nur wer die primäre Gruppe `hams` hat,
   darf herein. Der Kommentar im Original nennt den Betriebsvorteil ausdrücklich —
   man sperrt einen Benutzer temporär, indem man seine GID in `passwd` auf `nogroup`
   setzt, ohne den Account anzufassen. Eine **Allow**-Liste also, im Gegensatz zu
   WAMPES' `/etc/ftpusers`, das bei fehlender Datei überhaupt nichts sperrt.

3. **Keine eigene Hash-Prüfung.** axspawn vergleicht nichts selbst, sondern übergibt
   an `/bin/login` (`axspawn.c:1835-1863`) und setzt `-f` nur, wenn bereits
   authentifiziert wurde oder die Richtlinie das Rufzeichen genügen lässt. Damit
   erbt es Shadow, PAM, Ablaufdaten und Sperrkonten vom System. Genau das fehlt
   WAMPES: `strcmp(crypt(pass,salt), pw->pw_passwd)` kann Shadow prinzipiell nicht
   sehen — das Feld enthält dort nur `"x"`.

4. Die historische `+`-Konvention im Passwortfeld („darf über AX.25, nicht über
   Telnet") ist in `axspawn.c:1860` ebenfalls abgebildet, falls WAMPES sie
   unterstützen soll.

Minimalvariante, falls der Umbau auf `/bin/login` zu weit geht: `#define`-Zweig
ersatzlos streichen, `#include <unistd.h>` bzw. `<crypt.h>` mit `-lcrypt` für den
echten Prototyp, `getspnam()` für den Hash, harte `uid == 0 || gid == 0`-Sperre und
`pw->pw_gid == hams_gid` als Eintrittsbedingung.

**Nebenbefund L-4:** der `else`-Zweig deklariert `char *crypt();` ohne Prototyp. Unter
C23 (clang 15+) gilt eine leere Parameterliste als `(void)`; der Aufruf mit zwei
Argumenten ist damit ein Constraint-Verstoß. `cc -Wall` meldet das
(`-Wdeprecated-non-prototype`). Da genau dieser Zweig auf macOS aktiv ist, betrifft
es deine Portierung direkt.

### W-36 — `remote_net.c`: unauthentifizierter Kommandokanal

`src/remote_net.c:161-216, 305-341`

Der Dienst nimmt Verbindungen an und bietet fünf Kommandos, davon zwei mit
Vollzugriff:

```c
static int command_command(struct controlblock *cp)
{
  cmdbuf = getarg(0, 1);
  dup2(cp->fd, 1);  dup2(cp->fd, 2);
  cmdparse(Cmds, cmdbuf, 0);            /* jedes WAMPES-Konsolenkommando */
  ...
}

static int console_command(struct controlblock *cp)
{
  dup2(cp->fd, 0);  dup2(cp->fd, 1);  dup2(cp->fd, 2);
  fkbd = 0;
  on_read(fkbd, (void (*)(void *)) keyboard, 0);   /* volle interaktive Konsole */
  printf(Prompt, Hostname);
}
```

**Keinerlei Authentifizierung.** Über `cmdparse(Cmds, …)` sind unter anderem
`attach`, `route`, `source` und alle Konfigurationskommandos erreichbar; WAMPES läuft
als root.

Entschärfend: `socketnames[]` enthält derzeit nur
`"unix:" TCPDIR "/.sockets/netcmd"`, also einen Unix-Domain-Socket, und
`hpux.c:111` legt `TCPDIR "/.sockets"` mit Modus **0700** an. Damit ist der Zugriff auf
den Eigentümer beschränkt. Die Konstruktion trägt aber ihre Absicherung nicht selbst:

* auf den Socket wird **kein** `chmod`/`umask` angewandt — die Rechte kommen allein
  aus dem Verzeichnis;
* die Liste ist als Array angelegt und `build_sockaddr()` kann ausdrücklich auch
  `AF_INET` (der Code behandelt den Fall inklusive `SO_REUSEADDR`). Ein zusätzlicher
  Eintrag `"tcp:..."` würde denselben Kanal ohne Authentifizierung ins Netz stellen.

**Empfehlung:** explizites `chmod(path, 0600)` nach dem `bind()`, ein Kommentar, der
festhält, dass hier nie ein `AF_INET`-Eintrag hinein darf, und — falls Remote-Zugriff
je gewünscht ist — vorher eine Authentifizierung.

### W-37 — `remote`-Dienst: `KICK__ME` ohne Passwort, mit fremder Ziel-IP

`src/smisc.c:136-165`

```c
	switch(command & 0xff){
	case SYS__EXIT:
		if(chkrpass(bp) == 0){ ... }        /* Passwort geprüft */
		else main_exit = 1;
		break;
	case KICK__ME:
		if(len_p(bp) >= sizeof(int32))
			addr = pull32(&bp);         /* Ziel frei wählbar */
		else
			addr = fsock.address;
		kick(addr);                         /* KEINE Passwortprüfung */
		break;
	}
```

Ein einzelnes UDP-Datagramm an den `remote`-Port genügt, um WAMPES zu einer Aktion
gegenüber einer **vom Angreifer gewählten** IP-Adresse zu veranlassen. Das ist eine
unauthentifizierte Reflection-Primitive. Nicht in den Beispielkonfigurationen aktiv,
aber `start remote` ist ein dokumentiertes Kommando.

### W-38 — `chkrpass()`: Klartextpasswort über UDP, variable Vergleichszeit

`src/smisc.c:167-186`

```c
	if(Rempass == 0 || *Rempass == 0 || strlen(Rempass) != len)
		return rval;                    /* Längen-Orakel */
	...
	if(strncmp(Rempass,lbuf,len) == 0)      /* nicht konstante Zeit */
```

Das Passwort wandert im Klartext über UDP (jeder Mitleser hat es), die Längenprüfung
verrät die Passwortlänge, und `strncmp()` bricht beim ersten abweichenden Byte ab.
Für einen Dienst, dessen einzige Funktion „Knoten herunterfahren" ist, ist das zu wenig.

### W-39 — `recv_udp()`-Rückgabewert ignoriert → uninitialisierter Zeiger

`src/smisc.c:144`, `src/rip.c:329`, `src/domain.c:797`

```c
	struct mbuf *bp;                /* uninitialisiert */
	...
	recv_udp(up,&fsock,&bp);        /* Rückgabewert verworfen */
	command = PULLCHAR(&bp);        /* bp ist Stack-Inhalt */
```

`recv_udp()` (`src/udp.c:99-136`) kehrt in **zwei** Pfaden mit `-1` zurück, ohne `*bp`
anzufassen: `up == NULL` und `up->rcvcnt == 0`. In diesen Fällen dereferenziert
`PULLCHAR` einen wilden Zeiger — und zwar schreibend (`(*bpp)->cnt--`,
`(*bpp)->data++`) — und das anschließende `free_p(&bp)` gibt ihn frei.

Im Upcall-Modell ist `rcvcnt == 0` schwer zu erreichen, unmöglich ist es nicht.
`sntp.c:392,469` und `timep.c:25` machen es richtig (`if (recv_udp(...) < 0) return;`),
diese drei nicht. Einzeiler.

### W-40 — RIP: unauthentifizierte Routen-Injektion  *(erledigt in `4d69240`)*

`src/rip.c:315-395`

**Korrektur an der ursprünglichen Fassung dieses Befunds.** Sie unterschlug zwei
Schutzmechanismen, die es schon gab, und stufte damit zu hoch ein:

* `ipfilter allow|deny <addr>/<bits>` wirkt bereits auf gelernte Routen — `rt_add()`
  prüft jede Route mit `ttl > 0` gegen `ipfilter(target)` (`iproute.c:417`), und
  RIP-Routen haben immer ein ttl. Das begrenzt die **Zielseite**, wenn auch mit
  Voreinstellung „allow".
* Permanente Routen sind unantastbar: `route add` ohne ttl gibt `ttl = 0`, der Timer
  läuft nicht, geprüft in `proc_rip` (`rip.c:513`) und in `rt_add` (`iproute.c:454`).
  Alles aus der `net.rc` ist damit sicher. Wirksam bleibt die Injektion trotzdem, weil
  `rt_lookup()` den längsten Präfix nimmt: eine /32 schlägt eine permanente /8.

Einstufung damit **mittel**, nicht hoch. Was fehlte, betraf die **Quellseite**:

* Der einzige Filter ist `Rip_refuse`, eine **Deny-Liste**. Ohne Einträge wird jeder
  Absender akzeptiert; es gibt keine Allow-Liste und keine Prüfung, ob der Absender
  überhaupt ein konfigurierter Nachbar ist.
* Wer nicht bekannt ist, wird **automatisch eingetragen**:

  ```c
		} else if((rp = rt_lookup(fsock.address)) == NULL
		 || rp->iface != iface){
			entry.addr_fam = RIP_IPFAM;
			entry.target = fsock.address;
			entry.metric = 0;
			proc_rip(iface,fsock.address,&entry,ttl);
		}
  ```

  Ein einzelnes RIP-Response-Paket legt also eine Host-Route zum Absender an.
* `while(len_p(bp) >= RIPROUTE){ pullentry(&entry,&bp); proc_rip(...); }` ist zwar
  korrekt gegen die Paketlänge begrenzt, hat aber **keine Obergrenze für die Anzahl
  der Einträge**. Ein 64-KB-UDP-Datagramm liefert rund 2700 Routeneinträge pro Paket.
* dazu W-39.

Praktische Folge: wer UDP/520 erreicht, lenkt den IP-Verkehr des Knotens um. Nicht in
den Beispielkonfigurationen aktiv.

### W-41 — `tcpgate`: kein Zugriffsschutz vor lokalen Diensten

`src/tcpgate.c` — der Weiterleiter selbst ist sauber: das Ziel kommt aus der lokalen
Konfiguration, nicht vom Client, es ist also **kein** offenes Relay. Es gibt aber
keinerlei Zugriffskontrolle: `lsocket.address = INADDR_ANY`, jede eingehende
Verbindung wird durchgereicht. Ohne Zieladresse wird auf `loopback:<port>`
zurückgefallen (`tcpgate.c:126`).

In den Beispielkonfigurationen stehen `start tcpgate smtp`, `pop3`, `imap`, `nntp`,
`finger`, `rsync`. Damit sind lokale Dienste des Hosts über HF bzw. das AMPRNet
erreichbar. Das ist die Absicht der Konstruktion — es sollte einem nur bewusst sein,
und eine Absender-Whitelist pro Gate wäre eine sinnvolle Ergänzung.

### W-42 — `time`-Server: spoofbarer UDP-Reflektor

`src/timep.c:19-31` — beantwortet jedes Datagramm mit 4 Byte, kein Rate-Limit, keine
Absenderprüfung. In **allen fünf** Beispielkonfigurationen aktiv. Der
Verstärkungsfaktor ist klein, aber es ist ein spoofbarer Reflektor, und zwei solche
Knoten lassen sich gegeneinander in eine Schleife setzen. Immerhin: der
`recv_udp()`-Rückgabewert wird hier korrekt geprüft.

---

## 2f. LP64-Sweep (systematisch, wie besprochen)

Ziel: alle Stellen finden, an denen die Umstellung von `int32 = long` auf
`int32 = int` bzw. 64-Bit-`long` das Verhalten ändert.

### L-1 — `get32()`: Signed-Shift-Overflow (Regression durch die `global.h`-Änderung)

`src/misc.c:203-216`

```c
int32
get32(uint8 *cp)
{
	int32 rval;

	rval = *cp++;
	rval <<= 8;
	rval |= *cp++;
	rval <<= 8;
	rval |= *cp++;
	rval <<= 8;          /* <-- hier */
	rval |= *cp;

	return rval;
}
```

Mit `int32 = long` (64 Bit) war das sauber. Mit `int32 = int` ist der letzte
`<<= 8` für jeden Wert, dessen oberstes Byte ≥ 0x80 ist, ein **Left-Shift-Overflow
eines vorzeichenbehafteten Typs** — undefiniertes Verhalten. Reproduziert:

```
$ cc -O1 -fsanitize=undefined g32.c && ./a.out
g32.c:6:78: runtime error: left shift of 12625921 by 8 places
            cannot be represented in type 'int32' (aka 'int')
get32(192.168.1.1) = -1062731519 / c0a80101
```

Das Ergebnis stimmt auf clang/gcc, weil beide wraparound erzeugen — aber es trifft
**jede IP-Adresse ab 128.0.0.0**, die Hälfte aller TCP-Sequenznummern und alle
SNTP-Zeitstempel. Unter UBSan oder `-ftrapv` bricht es ab, und aggressive
Optimierung darf daraus alles machen.

`get32()` ist der zentrale Wire-Konverter (`ntohip`, `ntohtcp`, `pull32`, …).

**Fix:**

```c
int32
get32(uint8 *cp)
{
	uint32 rval;

	rval = *cp++;
	rval = (rval << 8) | *cp++;
	rval = (rval << 8) | *cp++;
	rval = (rval << 8) | *cp;

	return (int32) rval;
}
```

### L-2 — `sntp.c`: das `(unsigned long *)`-Cast-Muster

Siehe W-29. Der Sweep zeigt: das ist die **einzige** Stelle im Baum, an der ein
Paketpuffer auf `unsigned long *` gecastet und wortweise durchlaufen wird —

```
$ grep -rn '(unsigned long \*)\|(long \*)' --include='*.c' .
sntp.c:262:		p = (unsigned long *) bp->data;
```

`domain.c:509 int32 *ap = (int32 *)data;` ist der zweite Cast im Baum, dort aber mit
`int32` (jetzt korrekt 32-bittig) und über einen selbst allokierten Puffer.

### L-3 — `lcsum()`: Akkumulator-Reserve geschrumpft

`src/lcsum.c:17-25`

```c
uint lcsum(register uint16 *wp, register uint len)
{
	register int32 sum = 0;
	while(len-- != 0)
		sum += *wp++;
```

Mit `int32 = long` war der Akkumulator 64-bittig. Jetzt sind es 32 Bit: der Überlauf
tritt bei 32769 × 65535 ein, also ab **65538 Byte in einer einzelnen mbuf**. Das größte
IP-Datagramm ist 65535 Byte → 32767 Wörter → 2 147 385 345, knapp unter `INT_MAX`
(2 147 483 647). Es geht auf, aber die Reserve beträgt nur noch rund 98 KB.

**Kein Befund**, aber ein Wert zum Wissen: wer irgendwo eine mbuf > 64 KB
checksummt (z. B. `slip_encode()` allokiert `2*len+2`), landet im Überlauf.
`uint32 sum` wäre die risikofreie Variante.

### L-6 — `TTL_MISSING` lässt sich nicht mehr treffen

`src/domain.h:127`, `src/domain.c:601`

```c
#define TTL_MISSING     0x80000000UL
...
	if(rrp->ttl != TTL_MISSING)
```

`rrp->ttl` ist `int32`, also jetzt 32-bittig und vorzeichenbehaftet. Im Vergleich
mit dem `UL`-Literal wird es auf `unsigned long` (64 Bit) erweitert; aus dem
Sentinel-Wert `INT32_MIN` wird dabei `0xFFFFFFFF80000000`, was nie gleich
`0x80000000UL` ist. **Die Bedingung ist immer wahr**, der „TTL fehlt"-Fall
also unerreichbar. Auf ILP32 hat es funktioniert. `clang -Wall` meldet es bei
jedem Bau (`-Wtautological-constant-out-of-range-compare`). Wirkung: beim
Schreiben der Domain-Datei wird die TTL auch dann ausgegeben, wenn sie
weggelassen werden sollte — kein Sicherheitsproblem, aber ein echter
Verhaltensfehler. Fix: `(int32) TTL_MISSING` im Vergleich oder das Makro
gleich passend typisieren.

### W-43 — `ipip_receive()`: Routen-Eintrag aus uninitialisiertem Stack

`src/ipip.c:89-130`. Beim Nachsehen der LP64-Vergleiche gefunden, gleiche
Familie wie W-05/W-29.

```c
  uint8 buf[MAX_FRAME];
  l = recvfrom(edv->fd, (char *)(bufptr = buf), sizeof(buf), 0, ...);
  if (edv->type == USE_IP) {
    if (l <= sizeof(struct ip)) goto Fail;      /* int gegen size_t */
    ...
  }
  if (l <= 0) goto Fail;
  ...
  if ((ipaddr = get32(bufptr + 12)) && ismyaddr(ipaddr) == NULL)
    rt_add(ipaddr, 32, (int32) ntohl(addr.sin_addr.s_addr), ifp, 1L, ...);
```

Im UDP-Modus ist `l > 0` die einzige Längengarantie. Ein **1-Byte-Datagramm**
genügt, damit `get32(bufptr + 12)` vier Byte liest, die nie empfangen wurden —
uninitialisierter Stack. Das Ergebnis landet als Zieladresse in `rt_add()`,
also in der IP-Routing-Tabelle. Dazu derselbe `int`-gegen-`size_t`-Vergleich
wie in `axip.c` (in `77a741e` dort behoben) und ein `int addrlen` statt
`socklen_t`.

`attach ipip` steht in der mitgelieferten `net.rc` (neben `attach axip`), und
die Beispielkonfiguration routet mehrere /16-Netze darüber.

Fix analog zu `axip_recv()`: Mindestlänge von `IPLEN` verlangen, bevor der
innere Header angefasst wird, `(int) sizeof(...)` casten, `addrlen` auf
`socklen_t`.

### Entwarnung — printf/Formatstrings

Der Verdacht auf verbliebene `%lu`/`%ld`-Mismatches nach der Typumstellung hat sich
**nicht** bestätigt. Syntaxlauf über die zehn größten Netzwerkdateien:

```
$ for f in ip.c tcpin.c udp.c rip.c iface.c axhc.c sntp.c domain.c ftpserv.c netrom.c; do
      cc -fsyntax-only -Wall -Wformat -I. -I../lib $f
  done
```

Ergebnis: **null Formatwarnungen**. Übrig bleiben nur

* 3 × `-Wformat-security` — `printf(Nospace)` (`sntp.c:588`) und `printf(Notval)`
  (`netrom.c:1687, 1857`), das sind die konstanten Strings aus W-24, harmlos;
* 1 × `-Wdeprecated-non-prototype` — `char *crypt();` in `ftpserv.c:716`, siehe L-4
  unter W-35.

Die printf-Seite der LP64-Umstellung ist damit im Wesentlichen erledigt. Was bleibt,
ist L-1 und L-2 — beides Arithmetik, nicht Formatierung, und deshalb von `-Wformat`
nicht auffindbar. Empfehlung: `-fsanitize=undefined` in den geplanten
GitHub-Actions-Lauf aufnehmen, das findet L-1 beim ersten empfangenen Paket.

---

## 3. Weitere Befunde

### Hoch

**W-06 — `dn_expand()` ohne `eom`-Grenze.** `src/domhdr.c:256-302`. Der Parameter
`eom` existiert, wird von allen fünf Aufrufern als `NULL` übergeben und im Rumpf nie
benutzt. Konsequenzen:

```c
		if((slen & 0xc0) == 0xc0){
			cp = &msg[((slen & 0x3f)<<8) + *cp];   /* Offset bis 16383, msg ist len Byte groß */
```

`msg` ist `mallocw(len_p(*bpp))` (`domhdr.c:31`) — bei einem 12-Byte-Query also 12 Byte.
Ein Kompressionszeiger auf Offset 16383 liest an beliebiger Stelle im Heap. Bis zu 512
Byte davon landen in `rrp->name` und damit in der Antwort bzw. im Cache → Heap-Disclosure.
Außerdem wird nach dem Folgen eines Zeigers nur *ein* Indirektionsschritt behandelt;
zeigt das Ziel wieder auf `0xc0…`, wird `0xc0 = 192` als Segmentlänge interpretiert und
192 Byte werden kopiert.

**W-07 — DNS-Parser ohne Längenprüfungen.** `src/domhdr.c:23-252`.
`ntohdomain()` liest `msg[0..11]` ohne Mindestlängenprüfung. `ntohrr()` liest nach dem
Namen bedingungslos 10 weitere Byte. `TYPE_HINFO` (`:183-195`) und `TYPE_TXT`
(`:238-244`) machen `memcpy(dst, cp, len)` mit `len = *cp++` (bis 255), ohne zu prüfen,
ob so viele Bytes im Paket sind — bis zu 255 Byte Heap werden in einen String kopiert,
der ausgeliefert wird. `qdcount`/`ancount`/`nscount`/`arcount` werden nicht plausibilisiert
(→ bis zu 4 × 65535 `callocw()`-Aufrufe aus einem 12-Byte-Paket, Memory-Exhaustion).

**W-08 — FTP `physname[1024]` Stack-Overflow.** `src/ftpserv.c:284-289, 300`.

```c
#define AsUser(stmt)    strcpy(physname, ftp->root);  \
			strcat(physname, file);       \
			...
	char physname[1024];
```

`file = pathname(ftp->cd, arg)` ist bis zu `strlen(cd) + strlen(arg) + 10` lang, `arg`
bis ~1018 Byte (Kommandozeile ist `LINELEN`=1024). Plus `ftp->root`. Bereits mit einem
einzelnen langen `RETR` wird `physname` überschritten; per wiederholtem `CWD` wächst
`ftp->cd` und damit `file` weiter.
Positiv: `pathname()`/`crunch()` (`src/pathname.c`) kollabieren `..` korrekt und lassen
kein Ausbrechen aus `ftp->root` zu — **kein** Path-Traversal.

**W-10 — BBS Adress-Parsing.** `bbs/bbs.c:669-682` kopiert `addr` zeichenweise in
`char buf[1024]` ohne Grenze; `translate_bangs()` (`:655-664`) baut per `sprintf()` in
ein `static char tmp[1024]` und schreibt mit `strcpy(s, tmp)` in den Aufruferpuffer zurück.

**W-11 — TCP-Options-Parser liest über den Puffer hinaus.** `src/tcphdr.c:142-168`.

```c
	uint8 options[TCP_MAXOPT];      /* 40 */
	pullup(bpp,options,optlen);
	for(cp=options,i=optlen; i > 0;){
		kind = *cp++;
		i--;
		switch(kind){ case EOL_KIND: ...; case NOOP_KIND: continue; }
		optlen = *cp++;              /* kein Test auf i > 0 */
		switch(kind){
		case MSS_KIND:    if(optlen == MSS_LENGTH)   tcph->mss = get16(cp);          break;
		case TSTAMP_KIND: if(optlen == TSTAMP_LENGTH){ tcph->tsval = get32(cp);
		                                               tcph->tsecr = get32(cp+4); }  break;
```

Steht als letztes Options-Byte ein Multi-Byte-Kind, wird `optlen` hinter `options[39]`
gelesen; bei `TSTAMP_KIND` zusätzlich 8 Byte ab dort — bis zu ~10 Byte Stack-OOB-Read.
Da `tsval` vom TCP-Peer als `tsecr` zurückgespiegelt wird, ist das ein
**Stack-Speicher-Leck über das Netz**. Linux' `tcp_parse_options()` prüft
`opsize < 2 || opsize > length` — genau das fehlt hier.

**W-12 — `Compress_table[128]` Überlauf.** `src/domhdr.c:306-350`.

```c
static struct compress_table { const char *name; int offset; } Compress_table[128];
...
    for (i = 0; Compress_table[i].name; i++) ...
    Compress_table[i].name = name;
    Compress_table[i+1].name = 0;
```

`i` ist unbegrenzt. Eine Antwort mit mehr als 127 unterschiedlichen Namens-Suffixen —
über W-01 leicht erzeugbar — schreibt hinter das Array in .bss.

### Mittel

* **W-13** `src/ftpserv.c:274-279` — `errmsg()`: `static char buf[1024]` zu klein für
  `"550 " + 1023-Byte-Pfad + strerror()`.
* **W-14** `src/netrom.c:631-632` — `route_packet()` garantiert nur `cnt >= 15`
  (`:578`), liest im L3RTT-Zweig aber `data[19]` und `memcmp(…, data+20, 6)` → bis zu
  11 Byte OOB-Read bei einem 15-Byte-NET/ROM-Frame an „L3RTT".
* **W-15** `src/iphdr.c:139-147` — `cksum()`: bei einem mbuf mit `cnt == 0` und
  ungerader Datenadresse wird `cnt--` auf `(uint)-1` unterlaufen und anschließend
  `lcsum(up, 0x7fffffff)` gerufen. Aktuell keine bekannte Quelle für Null-Längen-mbufs
  in einer Kette (`qdata(x,0)` erzeugt einen), aber die Konstruktion ist eine Zeitbombe.
  Dazu: `total += m->cnt;` sollte `total += cnt;` sein.
* **W-16** `src/login.c:808, 828, 841, 868` — `memcpy(Defaultuser, argv[1], sizeof(Defaultuser))`
  liest immer die volle Zielgröße aus `argv[1]` (OOB-Read, wenn kürzer) und terminiert
  nicht, wenn `argv[1]` länger ist. Die so unterminierten Strings landen in
  `sprintf(homedirparent, "%s/%.3s...", Homedir, name)` (`:315`, Ziel 80 Byte),
  `sprintf(homedir, "%s/%s", …)` (`:316`, Ziel 80 Byte) und
  `sprintf(filename, "%s/log.%05d.%04d", Logfiledir, …)` (`:403`, Ziel 80 Byte).
  Entschärft dadurch, dass `find_user_name()` nur alphanumerische, per `callvalid()`
  geprüfte Rufzeichen durchlässt — die Command-Injection in `sprintf(cmdbuf, "chpass -a
  '%s::…'", name)` + `system()` (`:321-323`) ist deshalb **nicht** auslösbar.
* **W-17** `src/domain.c:724, 739` — `char buffer[256]; strcpy(buffer, cp)` mit
  `cp = resolve_a(...)`; Cache-Namen sind unbegrenzt lang (aus GDBM oder
  `gethostbyaddr()`, letzteres über `strlwc(buf /*1024*/, hp->h_name)` ebenfalls ungeprüft).
* **W-18** `src/ax25.c:655` — `for (rp = ax_routeptr(idest,0); rp; rp = rp->digi)` ohne
  Tiefenbegrenzung. `axroute_add()` erzeugt für sich keinen Zyklus, die Routendatei bzw.
  manuelle `ax route`-Einträge können es. Dann Endlosschleife.
* **W-19** `src/netrom.c` `circuit_manager()` — jedes NR4-CONRQ mit neuer
  (index,id,cuser,node)-Kombination erzeugt einen Circuit; bei `server_enabled`
  unbegrenzt → Memory-Exhaustion über Funk.
* **W-20** `convers/conversd.c:675-691, 709-721, 743-752, 873-927` — durchgehend
  `sprintf()` in `char[2048]`, gespeist aus `l_ibuf[2048]` plus zusätzlichen Feldern;
  `:918-927` baut per `strcat()`-Kette unbegrenzt auf.

### Niedrig / Härtung

* **W-21** `src/slip.c:296` — `if (iface->xdev > SLIP_MAX)` ist off-by-one (`>=`);
  `Slip[SLIP_MAX]` wäre ein OOB-Zugriff.
* **W-22** `src/ax25.c:565` — `ax_routeptr()`: `calloc()` ohne NULL-Prüfung, danach
  sofort `addrcp(rp->target, call)`. Im Rest des Baums wird `callocw()` benutzt.
* **W-23** `src/ftpserv.c:112` — `alloc_mbuf(256)` gefolgt von `sprintf()` mit bis zu
  ~1050 Byte Ausgabe. Geht nur gut, weil `alloc_mbuf()` auf `LARGE_MBUF` (2012) aufrundet
  (`mbuf.c:20`). Latenter Heap-Overflow, sobald jemand an den Größenklassen dreht.
* **W-24** `printf(Nospace)` / `printf(Notval)` / `printf(Badsess)` an ~20 Stellen
  (`session.c`, `ftpcli.c`, `telnet.c`, `ipfilter.c`, `ax25cmd.c`, `sntp.c`, `netrom.c`)
  — die Strings sind konstant, also nicht ausnutzbar, aber `-Wformat-security` schlägt
  an und verdeckt echte Treffer.
* `src/ax25hdr.c:113` — jedes Frame mit gesetztem Bit 0 in `dest[1]` wird als
  FlexNet-komprimierter Header interpretiert, unabhängig davon, ob FlexNet auf dem
  Interface überhaupt aktiv ist. Speichersicher, aber ein Protokoll-Confusion-Vektor.
* `src/ax25hdr.c:158-165` — `nextdigi` zählt die *Anzahl* gesetzter REPEATED-Bits, nicht
  den Index des letzten. Bei nicht-zusammenhängenden REPEATED-Bits zeigt `digis[nextdigi]`
  auf den falschen Digi (Linux benutzt dafür `ax25_digi->lastrepeat`). Speichersicher,
  aber ermöglicht Digipeater-Loops/Spoofing.

### Geprüft und in Ordnung

* `src/pathname.c` — kein Path-Traversal im FTP-Server.
* `src/mbuf.c` — `pullup()`, `dup_p()`, `extract()`, `copy_p()`, `pushdown()` sind
  bezüglich Grenzen sauber.
* `src/ax25hdr.c:158` `ntohax25()` — `MAXDIGIS`-Grenze korrekt, kein Overflow in `digis[]`.
* `src/ax25.c:581-600` `axroute_add()` — `calls[MAXDIGIS+1]` passt exakt.
* `src/iphdr.c:92-95` `ntohip()` — `optlen` max. 40 = `IP_MAXOPT`, passt exakt.
* `src/slip.c:139` `slip_encode()` — `2*len+2` reicht exakt für Worst-Case-Escaping.
* `src/slhc.c:153` `new_seq[16]` — maximal 5 × 3 = 15 Byte, passt.
* `src/crc.c` `check_crc_16()` — terminiert korrekt.
* `src/slhc.c:127` `decode()` — durch die mbuf-API gegen Pufferende abgesichert
  (der Linux-Fix `4c1367a2` ist hier nicht nötig).

---

## 4. Systemische Themen

1. **`chmod 4555` auf `/usr/local/bin/bbs`** (`bbs/Makefile:21`). Damit ist jeder der
   129 unsicheren String-Aufrufe in `bbs.c` ein Kandidat für lokale — und über die
   BBS-Anmeldung remote — Rechteausweitung auf root. Prüfen, ob das setuid-Bit
   wirklich gebraucht wird; `seteugid()` deutet auf Zugriff auf User-Spools hin, was
   sich meist über eine dedizierte Gruppe lösen lässt.
2. **Keine Compiler-Härtung.** In `src/Makefile` / `lib/configure.mak` gibt es kein
   `-D_FORTIFY_SOURCE=2`, `-fstack-protector-strong`, `-Wformat -Wformat-security
   -Werror=format-security`, `-fPIE -pie`, `-Wl,-z,relro,-z,now`. Allein
   `-Werror=format-security` hätte W-02 beim Bauen gefunden. `-Wall -Wextra` würde die
   Signed/Unsigned-Vergleiche (W-05) melden.
3. **Netzwerkdaten wandern ungeprüft in Shell-Kommandos.** `popen()`/`system()` mit
   interpolierten Werten in `bbs.c:769, 1964, 2329`, `login.c:322, 353`,
   `tools/htmldiff.c:345,467`, `tools/mm2html.c:644`. Als Muster ersetzen durch
   `fork()`+`execv()`.
4. **Der DNS-Code ist der schwächste Teil des Baums** und in allen mitgelieferten
   `net.rc.*` aktiv. Wenn der eingebaute DNS-Server nicht wirklich gebraucht wird:
   `start domain` aus den Beispielkonfigurationen entfernen, das schaltet W-01, W-06,
   W-07 und W-12 auf einen Schlag ab. Der Resolver-Teil (`ntohdomain`/`dn_expand` auf
   *Antworten*) bleibt allerdings über einen bösartigen/gespooften Nameserver erreichbar.
5. **`uint`-Arithmetik in Längenrechnungen.** `mbuf.c`, `iphdr.c` und die Parser
   rechnen durchgehend mit `uint` und subtrahieren ohne Unterlaufprüfung
   (`cksum()`-Fall W-15, `cs->cs_ip.length - (optlen+IPLEN+TCPLEN)` in `slhc.c:445,452`).
   Die laufende LP64-Umstellung ist ein guter Zeitpunkt, hier auf `size_t` plus
   explizite Unterlaufprüfungen umzustellen.

---

## 5. Empfohlene Reihenfolge

**Sofort (ausnutzbar aus der Default-Konfiguration):**
0. **W-35** — den `#define crypt(key, salt) (key)`-Zweig in `ftpserv.c` streichen.
   Solange der drin ist, ist auf Linux jeder FTP-Account mit dem Passwort `x`
   erreichbar und die auto-angelegten Ham-Accounts ganz ohne. Das rangiert vor
   allem anderen. Bis zum Fix: `start ftp` aus den Konfigurationen nehmen.
1. W-04 — eine Zeile `comp->flags |= SLF_TOSS;` in `slhc_init()`.
2. W-01 — Längengrenze in `htondomain()`/`putq()`/`putrr()`, TC-Bit setzen.
3. W-02 — `errmsg()`-Ergebnis nicht mehr als Formatstring verwenden.
3b. **L-1** — `get32()` in `misc.c` auf `uint32` umstellen. Gehört zur laufenden
    LP64-Arbeit, nicht zum Sicherheitspaket, ist aber die Grundlage dafür, dass
    UBSan in CI überhaupt durchläuft.
4. W-05 — Grenzen im Adressfeld-Walk in `axip.c` (beide Stellen).
5. W-29 — `unsigned long` → `uint32` in `ntohntp()`/`htonntp()`. Gehört fachlich zur
   laufenden LP64-Arbeit, nicht zum Sicherheitspaket — `sntp.c` ist auf 64 Bit
   ohnehin funktionsunfähig, und der Infoleak verschwindet als Nebeneffekt.
6. W-03 — `popen()` in `bbs.c` ersetzen bzw. Adressen validieren; setuid-Bit prüfen.

**Kurzfristig:**
7. W-30/W-31 — SNTP-Client: Absenderprüfung und Origin-Timestamp-Abgleich. Zwei
   `if`-Zeilen, danach ist die Uhr nicht mehr von jedem Host im Netz stellbar.
8. W-06/W-07 — `dn_expand()` mit echtem `eom`-Parameter und Schleifenerkennung,
   Längenprüfungen in `ntohdomain()`/`ntohrr()`, Plausibilisierung der Counts.
9. W-08/W-13 — `snprintf()` statt `strcpy`/`strcat`/`sprintf` in `ftpserv.c`.
10. W-09 — Linux-Prüfungen aus `7d3fce8c` in `slhc_remember()` nachziehen,
    `initialized`-Flag aus `3f01ddb9`.
11. W-25 — Frame-Längenbegrenzung in `slip_decode()`/`nrs_decode()`. Kleiner Eingriff,
    das brachliegende `sp->rcnt` existiert bereits; Voraussetzung dafür, dass eine
    spätere 6pack-Implementierung nicht dieselbe Lücke erbt.
12. W-11 — Bounds im TCP-Options-Parser.
13. W-10/W-20 — BBS- und conversd-Puffer.
14. W-14/W-28 — NET/ROM: Längenprüfung im L3RTT-Zweig, Obergrenze für Knoten pro
    Broadcast.
15. W-32 — Panic-Threshold vor `settimeofday()`, `tv_sec` nicht auf `int` kürzen.

**Begleitend:**
16. Härtungsflags in den Makefiles; `-Werror=format-security` zuerst.
17. Ein Fuzzer auf die fünf reinen Parser-Funktionen `ntohdomain()`,
    `slhc_uncompress()`, `ntohax25()`, `slip_decode()` und `ntohntp()` — die nehmen
    einen Puffer bzw. eine mbuf-Kette und sind ohne Netzwerk-Setup ansteuerbar. Mit
    `libFuzzer` + MSan/ASan sind W-01, W-04, W-06, W-07, W-25 und W-29 innerhalb von
    Sekunden reproduzierbar. Das passt gut zu dem in TODO.txt geplanten
    GitHub-Actions-Setup.
18. Für die geplante 6pack-Implementierung die Linux-Fix-Liste aus Abschnitt 2b
    als Checkliste verwenden.
19. Baumweit nach demselben Muster wie W-29 suchen: `grep -n 'unsigned long\|(long)'`
    in allen Wire-Format-Konvertern. `sntp.c` war explizit Teil der
    x86_64-Umstellung in `11251af` und ist trotzdem durchgerutscht — andere
    Stellen können es auch sein.

---

## Anhang: Verwendete Linux-Quellen

**`drivers/net/slip/slhc.c`** — `4ab42d78` (CVE-2015-7799), `3f01ddb9`, `baf76f0c`,
`110a40df`, `7d3fce8c`, `e7660744`, `4c1367a2`

**`drivers/net/slip/slip.c`** — Längenbegrenzung `sl->rcount < sl->buffsize`
(seit jeher, kein einzelner Fix-Commit)

**`drivers/net/hamradio/6pack.c`** — `19d1532`, `3c0d2a4`, `0b91119`, `81b1d54`,
`efe4186`, `2b04495`, `bf9a388` (**CVE-2026-53082**, uninit-value in
`sixpack_receive_buf`, Autorendatum 2026-04-07)

**`net/netrom/`** — `a4fd163` (2024-12-23, Längenprüfung in `nr_route_frame()`),
`ba1096c` (2026-01-21, **CVE-2026-23098**, CVSS 8.8, Double-Free in
`nr_route_frame()`), `613d12d`, `0b91302`, `e03e7f2`, sowie die sysctl-Data-Race-Serie
vom 2024-03-07

**`dd8d4bc`** (2026-04-23) — Entfernung von AX.25/NET-ROM/ROSE und den
hamradio-Treibern aus dem Mainline; Pflege ab dann out-of-tree
