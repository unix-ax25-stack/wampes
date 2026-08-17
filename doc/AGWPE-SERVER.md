# AGWPE-Server für WAMPES — Implementierungs-Spec

Status: **eingefrorenes Planungsdokument** (noch nichts gebaut). Stand 2026-08-12;
überarbeitet nach `AGWPE-BEWERTUNG.md` (§7/§9): die offenen Entscheidungen 1–5 sind
dort geprüft (Abschnitt „Entscheidungen" unten), die Rufzeichenfrage (7.3/1) ist
bewusst entschieden, und die Umsetzungs-Constraints aus §7.2 sind ergänzt.

## Ziele & Nicht-Ziele

- Wampes wird AGWPE-**Server** (TCP 8000, später IPv6 + Unix-Socket), wie
  `TODO.txt:20`. Ergänzt netd: netd = Multiplexer **vor** AGWPE-Servern,
  wampes = einer dieser Server (Direwolf-Alternative mit vollem AX.25 + NET/ROM).
- **Kein IP**: AGWPE ist reine AX.25-Sache; IP-Daten über den Port (PID 0xcc)
  fließen automatisch durch den vorhandenen `axlink[]`-Dispatcher.
- **Auth = Phase 3, eigene Liste**: `login.c` nutzt den **unix-login**
  (`/bin/login`, `getpwnam`/`/etc/shadow`) und macht **keine direkte Auth** —
  es gibt also **keine eigene user/passwort-Liste** und nichts davon ist für
  AGWPE wiederverwendbar (unix-login ist interaktiv, passt nicht auf den
  AGWPE-Draht). AGWPE-Auth braucht eigene Credential-Datei + Validierung.

## Dateistruktur (neu, ~3 Dateien + kleine Edits)

```
src/agwpe.h         — agwpe_cb, Port-Mapping, Prototypen
src/agwpe.c         — Server-Core: Listener, Client-Zustandsmaschine, Codec (Header 22/23 B, netle), Login 'P'
src/agwpe_sess.c    — Verbindungs-Mapping: 'C'/'c'/'v'/'D' ↔ open_ax25, Datenebene 'R'/'K'/'U'/'I', Monitoring 'Y'
config.c            — Attab-Eintrag "attach agwpe …" (~3 Zeilen)
lapb.c              — eingehender Connect-Hook (~10 Zeilen, siehe §6b)
```

## `attach`-Kommando (Einstiegspunkt `config.c:119` Attab)

```
attach agwpe <port> [<iface> …]
```

- `<port>` = TCP-Listener (default 8000). Alle derzeit und künftig attachten
  AX.25-fähigen Interfaces (`iface->output == ax_output`) sind **Channels**
  des Ports.
- Ohne Liste: **alle** AX.25-Interfaces in Attach-Reihenfolge. Mit Liste: nur
  die genannten.
- Jedes Interface = **1 Channel** = 1 AGWPE-Portnummer (0-basiert,
  `'G'`-Reply). Passt exakt zur netd-Konvention (Upstream-Index 0 →
  Ports 0..N-1).
- Start/Stop: `start agwpe` / `stop agwpe` (Startcmds/Stopcmds, wie
  telnet/remote).

## Server-Core (`agwpe.c`)

- Listener via `open_tcp(TCP_SERVER)` (`tcpuser.c:73` klont pro Client) —
  exakt das `tnserv.c`-Muster; Unix-Socket später via `on_read` (`hpux.c:303`).
- `struct agwpe_client`: Buffers, Client-Call, Port-Scope-Flags, Liste der
  Sessions, ob `'A'`-Register offen, Auth-Zustand.
- **Inkrementeller Parser** in der `recv`-Upcall: Header dekodieren (port,
  pid, call_from/call_to), Rest an den Codec. Kein Blockieren im Upcall
  (Einzelthread!).
- Login `'P'` (+ `'x'`/`'X'` Version, eigener Server-Ident-String) —
  Drahtverhalten gespiegelt zu unserem libax25-`agwpe_client_login` und dem
  netd-Auth. Phase 2: Call+Password gegen eigene Credential-Datei
  (kein unix-login, siehe Ziele).

## Verbindungsmodell

### a) Ausgehend — `'C'`/`'c'`/`'v'` → `open_ax25`

```
struct ax25 hdr = {0};
setcall(hdr.dest,   call_to + ssid);    // aus AGWPE-Header
if (call_from gesetzt) setcall(hdr.source, call_from + ssid); // sonst leer
hdr.ndigis = n; hdr.digis[i] = via[i];  // aus 'v', nur wenn angegeben
open_ax25(&hdr, AX_ACTIVE, agwpe_recv_upcall, agwpe_send_upcall,
          agwpe_state_upcall, client);
```

**Regel (gewollt, Default = "client"-Modus):**
- **Src-Call gesetzt** → honorieren: geht mit SSID als Source auf die
  Leitung. **Kein Src-Call** → Fallback: Source = iface-hwaddr (wie bisher).
- **Digis gesetzt** → agwpe-gesetzten Pfad honorieren (via-Liste 1:1).
  **Keine Digis** → wampes-Routentabelle greift (Interface-Wahl via
  `ax_routeptr`/`Axroute_default_ifp`).

**Zwei Modi, konfigurierbar** (`param agwpe_mode client|via`, global;
per-Port in Phase 2):
- **client** (Default): Src-Call + via-Liste erscheinen auf dem Kanal wie
  angegeben; wampes tritt nach außen NICHT als via-Digi auf.
- **via**: wampes setzt sich selbst (Source = iface-hwaddr, ggf. erster
  Hop), lauscht auf allen exponierten ifaces, stellt sich nach innen wie
  außen als via-Digi dar.

**Technik "client"-Modus:** `axroute()` (`ax25.c:682`) überschreibt
`hdr->source` bei `nextdigi==0` bedingungslos mit `ifp->hwaddr` — genau das
würde den Client-Source überkleben. Änderung: Ersetzung **nur wenn
`hdr->source` leer ist** (bzw. Flag "source nicht überschreiben"). Die
via-Liste bleibt unangetastet; ohne Digis wählt weiterhin die
wampes-Routentabelle das Interface.
Micro-Entscheidung: bei gesetzter Client-Digi-Liste keine zusätzlichen
Routentabellen-Digis einfügen („Pfad honorieren").

**Validierung:** Source, die mit einem Call kollidiert, auf dem wampes selbst
lauscht (Interface-Calls wie DB0AAA-8), abweisen — wampes kann nicht mit
einer Source senden, die es selbst beantwortet.

**Die Gegenrichtung fehlt noch — ohne sie funktioniert "client" nicht.**
(Nachgetragen 2026-08-12.) Senden mit dem Client-Call ist nur die halbe
Sache: die Gegenstelle antwortet **an dieses Rufzeichen**, und der
Empfangspfad kennt es nicht. Zwei Stellen prüfen heute gegen die eigene
Adresse:

```
ax25.c:445   if (!mcast && !addreq(idest, iface->hwaddr)) {
                     /* Not a broadcast, and not addressed to us. */
                     free_p(bpp); return;
             }
lapb.c:72    digipeat = (ismyax25addr(hdr->dest) == NULL);
```

Das UA auf unser SABM hat `idest` = Client-Call, `iface->hwaddr` ist das
Interface-Call → **kein Treffer, Frame verworfen**. Käme es durch, hielte
`lapb.c:72` es für ein fremdes, zu digipeatendes Frame.

Drei Wege, mit ihren Nebenwirkungen:

1. **Client-Calls als eigene Adressen registrieren**, sodass
   `ismyax25addr()` sie kennt. Löst beide Stellen auf einen Schlag,
   **beißt sich aber mit der Validierungsregel oben** — dann *sind* es
   Calls, auf denen wampes lauscht. Zusätzlich: wampes antwortete darauf
   auf *allen* Interfaces, und ein Client-Call in einer fremden Digi-Liste
   verschöbe `nextdigi` in `axroute()` (`ax25.c:638-641`).
2. **Filter um die offenen Verbindungen erweitern:** annehmen, wenn `idest`
   die Source einer bestehenden `axp` auf diesem Interface ist. Chirurgisch,
   ohne Wirkung auf `nextdigi` und Digipeating. Im "client"-Modus ist die
   `axp` immer schon da, weil nur Antworten auf unser eigenes SABM
   hereinkommen.
3. **Kleine Tabelle "Calls, mit denen wir für Clients senden"**, gepflegt
   vom AGWPE-Server, in beiden Prüfungen mitgefragt.

**Empfohlen: 3, mit minimalem Fußabdruck im Kern.** Die Tabelle ist ohnehin
nötig — die Validierungsregel oben braucht sie — und sie gehört nach
`agwpe.c`, nicht in den AX.25-Kern. Der Kern bekommt dann genau **zwei**
geänderte Zeilen, beide ein Prädikat:

```
int agwpe_owns_call(const struct iface *ifp, const uint8 *call);
```

das ohne angehängten AGWPE-Server konstant 0 liefert, also am bisherigen
Verhalten nichts ändert:

```
ax25.c:445   if (!mcast && !addreq(idest, iface->hwaddr)
                        && !agwpe_owns_call(iface, idest)) { ... }
lapb.c:72    digipeat = (ismyax25addr(hdr->dest) == NULL
                        && !agwpe_owns_call(axp->iface, hdr->dest));
```

Vorteil gegenüber 2: das Konzept „dieses Rufzeichen gehört einem Client,
nicht uns" steht im Code, statt aus offenen Kontrollblöcken abgeleitet zu
werden — und es trägt auch dort, wo noch keine `axp` existiert.

**Noch nicht entschieden.** Betrifft nur Modus "client"; im Modus "via"
bleibt alles wie bisher, weil dort das Interface-Call die Source ist.

- Zustands-Upcall → `'C'`-Ack (LAPB_CONNECTED) / `'D'` (DISCONNECTED, mit
  `reason`). `'D'`-Kommando → `disc_ax25`.

### b) Eingehend — Connect an Interface-Call

Status quo: `cr_ax25()` setzt default `r_upcall=axserv_open`; SABM wird erst
**beim ersten Datenframe** an den Server übergeben (`lapb.c:122`, nur bei
`jumpstart`), ein passiver AX_SERVER-Clone ist in diesem Baum toter Code
(`flags.clone` nirgends konsumiert).

⇒ **Minimaler Hook in `lapb.c:110-137`** (SABM-Accept-Zweig, bei
`LAPB_DISCONNECTED`, `!digipeat`): wenn `axp->iface` über AGWPE exponiert ist
**und** ein Client den Port für Incoming geöffnet hat → cb übernehmen (Upcalls
setzen, `'C'`-Indikation an den Client senden, `s_upcall` installieren). Sonst
unverändertes Verhalten (login/jumpstart). Das liefert die
**SSID-fähige Selbst-Ziel-Funktion** ("connect an WAMPES-Interface-SSID
selbst").

**Format der Indikation an die Linux-Seite** (nachgetragen 2026-08-12).
Weitergereicht wird, von wem und über welchen Pfad connectet wurde. Dafür
**das Trace-Format benutzen, das die Nutzer ohnehin kennen** — `trace` in
wampes (`ax25dump.c:44-53`) und `listen` aus ax25-apps
(`listen/ax25dump.c:139-147`) geben beide dasselbe aus:

```
DB0XXX->DB0AAA-8 via DB0GGG* DB0BBB
```

Quelle vor dem Ziel, danach `via` und die Digis; der Stern markiert einen
bereits durchlaufenen Digi. Gegen die APRS-Kommaform
(`DB0XXX>DB0AAA-8,DB0GGG,DB0BBB`) spricht, dass sie das Sternchen nicht
kennt — die Mailbox sähe den Pfad, aber nicht, wo das Frame darin steht.
Gegen den Knotenstil `*** connected to XXX` spricht, dass er die Richtung
einer *ausgehenden* Verbindung beschreibt und keinen Pfad trägt.

**Was davon bei der Mailbox ankommt, hängt an der Gegenseite** — und heute
kommt der Pfad nicht durch:

* Der AGWPE-Kopf (`libax25/netax25/agwpe.h:52-67`) hat `call_from` und
  `call_to`, **kein Digi-Feld**. Ausgehend liegen die Digis im Datenbereich
  des `'v'`-Rahmens; für eine eingehende Indikation ist nichts vorgesehen.
  Das Protokoll soll nicht erweitert werden (Entscheidung des Autors).
* Der Shim könnte ihn ohnehin nicht ausdrücken: `getpeername()`
  (`libax25/axsock.c:2136-2159`) füllt eine **kurze** `struct
  sockaddr_ax25` mit einem einzigen Rufzeichen und setzt `*addrlen` auf
  deren Länge zurück — auch dann, wenn der Aufrufer eine
  `full_sockaddr_ax25` angeboten hat. Eine Mailbox, die `fsa_digipeater[]`
  ausliest, sieht null Digis, ohne Fehler.

⇒ **Der Src-Call kommt durch** (`call_from`), der Pfad nur als **Text** im
Datenbereich der `'C'`-Indikation, dort wo AGWPE üblicherweise
`*** CONNECTED To Station …` ablegt. Viele Mailboxen lesen diesen Text und
zeigen ihn an; über `getpeername()` ist er nicht erreichbar. Das ist die
Grenze, die aus „AGWPE nicht erweitern" folgt, und sie ist bewusst
akzeptiert.

**Timing: sofort bei SABM, nicht erst beim ersten Datenframe.** Ohne
Durchreichen beim SABM kann die Mailbox nicht unmittelbar mit ihrem Prompt
antworten; sieht die Gegenseite keinen Prompt, startet sie evtl. kein
Store-and-Forward — der Dienst wäre unbenutzbar.

## Datenebene + PID (`agwpe_sess.c`)

- Ausgehend: `'R'`/`'K'` (Daten) → `send_ax25(axp, bp, pid)`. `'U'`/`'I'`
  (UI/Status) → `axsend`/`ax_output` mit PID.
- Eingehend: `handleit` (`lapb.c:906`) zieht den PID ab, bevor `r_upcall`
  läuft. ⇒ **`uint8 pid` im `struct ax25_cb`** (kleiner Zusatz), in
  `handleit`/`axnl3` gefüllt (für AGWPE-Sessions; default aus `'c'`/0xf0).
  `agwpe_recv_upcall` baut daraus das `'R'`-Frame.
- NET/ROM (0xcf) & Co. auf **Client-Sessions**: laufen durch `axlink[]` →
  kein Sonderfall auf Empfangsweg (Daten kommen an, PID vermerkt). Siehe
  Gate §8.

## Monitoring pro Interface (`'Y'`) + Heard (`'H'`)

- **Pro Interface = "Kanal einschalten"**: `'Y'`-Subdaten + Port-Scope →
  Client bekommt nur Frames der angewählten Ports.
- RX-Hook: zentraler Punkt ist `src/ax25.c:394-406` — **vor** `ntohax25()`
  und **vor** dem „for us"-Test (`:445-450`), also inklusive Fremdverkehr
  (nicht der Interface-Eingang/`ax_recv`). Frame duplizieren → als `'Y'`
  an die Monitore des Ports.
- TX-Hook: **`ifp->raw` pro exponiertem Interface** (wie `kiss_raw`) —
  zentral, statt elf verteilter `dump()`-Stellen (`slip.c:117`, `axip.c:79`,
  `ethertap.c:110/155`, `krnlif.c:338/359`, `ipip.c:58`, `ni.c:57`,
  `nrs.c:72`, `netrom.c:716`, `tun.c:67`) → `'Y'`.
- Rohes AX.25-Frame inkl. Digis — Re-Encoding über vorhandene
  `ntohax25`/`htonax25`.
- `'H'`/Heard: aus vorhandenen `axheard`-Strukturen je Interface filtern.

## NET/ROM-Zugang (Gate, default AUS)

- TODO-Liste unten (user-NODES, Lernen, INP3) ist das wampes-**Knoten**-Thema
  — davon getrennt: ob ein **AGWPE-Client** NET/ROM nutzen darf.
- Konfig: `param agwpe_netrom 0|1` (global; pro-Client in Phase 2 mit Auth).
  Bei 0: PID-0xcf-Frames auf AGWPE-Sessions werden im `'R'`→`send_ax25`-Pfad
  verworfen (und `'c'`-Connects mit NET/ROM-PID abgewiesen); eingehende
  NET/ROM an die Interfaces bleibt unberührt (wampes-eigene
  Knoten-Funktion).

## Phasen & Aufwand

| Phase | Inhalt | Aufwand |
|---|---|---|
| 1 | Listener, Login, `'G'`/Port-Info, ausgehende Connects (Modus **client**, inkl. axroute-Fix „source nicht überschreiben"), Datenebene R/K/U/I, `'D'`, PID-Feld | ~1400 Z. |
| 2 | Eingehende Connects (`lapb.c:110-137`-Hook), `'Y'`-Monitoring pro Port (`ax25.c:394-406` RX / `ifp->raw` TX), `'H'` | ~500 Z. |
| 3 | Auth (eigene Credential-Liste, kein unix-login), **via-Modus**, IPv6, Unix-Socket, NET/ROM-Gate, `'Q'`-Param, Kollisionsfall (2 Clients, gleicher Port) | ~500 Z. |
| Test | E2E-Harness aus netd wiederverwenden (`mock_upstream.py`, `agwpe_client` als Client) | mittel |

Aufwandseinschätzung aus der Bewertung (§7.4): realistisch bis leicht
optimistisch; TX-Monitor und Rufzeichenfrage sind unterschätzt.

## Entscheidungen (Stand 2026-08-12, nach AGWPE-BEWERTUNG.md §7/§9)

Die fünf offenen Punkte der Vorfassung sind geprüft. Die Bewertung bestätigt
die Richtung des Specs und präzisiert drei Stellen; wo sie etwas entschieden
hat, ist es unten festgehalten.

1. **Port-Exponierung — entschieden: alle AX.25-Interfaces (Default).**
   1 Interface = 1 Channel = 1 AGWPE-Portnummer (0-basiert, `'G'`-Reply).
   Das ist exakt die netd-Konvention (Upstream-Index 0 → Ports 0..N-1) und
   der Grund, wampes als Direwolf-Ersatz *hinter* netd zu betreiben.
   Explizite `attach agwpe <port> <ifaces…>`-Liste bleibt als
   Einschränkung. Listening läuft auf dem Interface-Call, Port = iface;
   zusätzliche Calls pro Interface (call+ssid) gibt es erst mit einer
   hwaddr-Liste — die wir bewusst **nicht** bauen (siehe 2).

2. **Src-Call / via-Modus — entschieden: `param agwpe_mode client|via`,
   hwaddr bleibt ein Feld.** Der Bericht (§7.3/1) bestätigt den
   konzeptionellen Bruch: `axroute()` (`src/ax25.c:679-682`) überschreibt
   `hdr->source` bedingungslos mit `ifp->hwaddr`, und AGWPEs beliebig
   viele Calls pro Client (`'X'`) sind ohne hwaddr-Liste nicht abbildbar.
   Die hwaddr-Liste (`ismyax25addr()` `ax25subr.c:232-243` + `axroute()`,
   geschätzt 8–10 Stellen) wird **bewusst nicht** gebaut — die
   Rufzeichenfrage wird entschieden, nicht implementiert:
   - **client** (Default, Phase 1): axroute-Ersetzung nur, wenn
     `hdr->source` leer ist (bzw. Flag „source nicht überschreiben");
     Src-Call und via-Liste erscheinen 1:1 auf dem Kanal, wampes tritt
     nach außen nicht als Digi auf.
   - **via** (Phase 3): wampes setzt sich selbst als via-Digi, lauscht auf
     allen exponierten ifaces, stellt sich nach innen wie außen als Digi
     dar.
   Validierung aus §6a bleibt (Source-Kollision mit eigenen
   Interface-Calls abweisen).

3. **Eingehende Connects / mehrere Clients — bestätigt, Hook lokalisiert.**
   Der Bericht (§7.3/2) bestätigt: `AX_SERVER`/`flags.clone` ist toter
   Code; der reale Weg läuft über `axserv_open` als Default-`r_upcall`
   (`src/ax25subr.c:162-163`) und greift erst beim **ersten Datenframe**,
   nur `jumpstart` schon beim SABM. ⇒ der Hook in `lapb.c:110-137`
   (siehe §6b) ist unvermeidlich. Zustellung pro (Client, Port/iface);
   mehrere Clients auf verschiedenen Ports = Normalfall;
   Kollisionsfall (2 Clients, gleicher Port) → Phase 3.

4. **NET/ROM — Gate bleibt default AUS, Design offen (Phase 3).** Der
   Bericht ändert daran nichts; PID 0xcf ist mehrdeutig (legitime
   L4-NetRom-Connects von libax25-Nutzern/`call`, userspace-netromd,
   ebenso ROSE), ein pauschales PID-Gate bricht das. Verfeinerung
   (Zielcall = NetRom-Knoten? Connect- vs. Daten-Intent?) bleibt offen.
   Eingehender NET/ROM an die Interfaces bleibt unberührt (wampes-eigene
   Knoten-Funktion).

5. **Incoming-SABM — sofort, abgeschlossen.** Ohne Durchreichen beim SABM
   kein unmittelbarer Mailbox-Prompt; die Gegenseite startet dann evtl.
   kein Store-and-Forward, der Dienst wäre unbenutzbar. Der Bericht
   (§7.3/2) bestätigt die Notwendigkeit des sofortigen Hooks.

## Umsetzungs-Constraints (aus AGWPE-BEWERTUNG.md §7.2)

Beim Bau einzuhalten, sonst läuft der Server in die wampes-Fallen:

- **Watchdog:** `hpux.c:44` (`TIMEOUT 120`), scharf `:167-171`, `abort()`
  `:93-97`. Ein Upcall, der länger als 120 s nicht nach `eihalt()`
  zurückkehrt, killt den ganzen Daemon ⇒ strikt nichtblockierend — kein
  blockierendes `send()` im Upcall (Gegenteil von netds
  `loop_send_client`).
- **O_NONBLOCK:** `remote_net.c:285-292` setzt es **nicht** — die eine
  Stelle der Vorlage, die man nicht kopieren darf (`conversd.c:608-613`
  macht es richtig).
- **`kwait()` bei leerer Ready-Liste → `exit(0)`** (`kernel.c:407-409`).
- **Byte-Order:** AGWPE ist Little-Endian; WAMPES hat nur BE-Helfer
  (`misc.c:175-200`) ⇒ eigener LE-Helfer nötig.
- **mbuf-Ketten** statt flacher Puffer (`mbuf.h:13-21`, `pullup()`
  `mbuf.h:50`).
- **Kein `printf()` in Datenpfaden:** als Daemon liegen fd 0/1/2 auf
  `/dev/null` (`hpux.c:148-164`).
- **Keine `usock`-Abstraktion:** `socket.h` ist reine Port-Defines; nichts
  kollidiert, aber auch nichts wiederverwendbar (`global.h:178`).
- **Aufwand:** Schätzung in „Phasen & Aufwand" ist „realistisch bis leicht
  optimistisch" (§7.4); TX-Monitor und die Rufzeichenfrage sind darin
  unterschätzt.

## Referenzierte Stellen (wampes-git/src)

- `tcpuser.c:73` — `TCP_SERVER`-Clone (`flags.clone = 1`), Listener-Muster
- `tnserv.c` — TCP-Server-Präzedenzfall (Login + Upcalls pro Client)
- `ax25user.c` — `open_ax25`/`send_ax25`/`recv_ax25`/`disc_ax25`
- `axclient.c` — `open_ax25`-Muster mit r/t/s-Upcalls
- `ax25.c:624-682` — `axroute()`: Interface-Wahl + Source-Ersetzung
- `ax25.c:394-406` — RX-Hook-Zentrum für Monitoring (vor `ntohax25()`,
  vor „for us"-Test `:445-450`, inkl. Fremdverkehr)
- `lapb.c:110-137` — SABM-Accept-Zweig: eingehender Connect-Hook (§6b)
- `lapb.c:906-923` — `handleit()` PID-Abzug → `axlink[]`
- `lapb.c:971-1000` — `build_path()`
- `ax25subr.c:163` — default `r_upcall = axserv_open`
- `ax25subr.c:232-243` — `ismyax25addr()` (für hwaddr-Liste nötig — nicht gebaut)
- `iface.h:99` — ein `hwaddr` pro Interface (der konzeptionelle Bruch, §7.3/1)
- `hpux.c:44,93-97,167-171` — Watchdog 120 s → `abort()` (nichtblockierend!)
- `kernel.c:407-409` — `kwait()` mit leerer Ready-Liste → `exit(0)`
- `misc.c:175-200` — nur Big-Endian-Helfer (eigener LE-Helfer nötig)
- `mbuf.h:13-21,50` — mbuf-Ketten, `pullup()`
- `config.c:119` — `Attab[]` (attach-Kommandos), `Startcmds`/`Stopcmds`
- `hpux.c:303`/`eihalt` — `on_read`/Select-Loop (Unix-Socket, Phase 3)
- `kiss.c` — `iface->raw`-Muster für TX-Hook (Monitoring)
- `login.c` — nutzt unix-login (`/bin/login`, `getpwnam`/`/etc/shadow`),
  KEINE direkte Auth, keine eigene user/passwort-Liste; für AGWPE-Auth
  (Phase 3) nicht wiederverwendbar — eigene Liste nötig
