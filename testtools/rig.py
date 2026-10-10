#!/usr/bin/env python3
"""Ein Teststand fuer den AX.25-Teil von wampes: Knoten, Ports, Links.

Der Aufbau ist derselbe, mit dem Thomas am 2026-09-01 die Doppelschlag-Sperre
und den Pin gemessen hat (TODO.txt, "WAS AUF EIN LABOR WARTET"), und er ist
frei von root: statt eines Funkgeraets haengen die Ports an pty-Paaren, und
statt eines Kabels schiebt dieses Programm die Bytes von einem Master zum
anderen.  Eine Preisdifferenz zum echten Funk gibt es, und sie ist in
TODO.txt aufgeschrieben: ein pty-SLAVE HAT ECHO AN, und das KISS-TYPBYTE
TRAEGT DIE CRC-MARKIERUNG - beide Fallen sind hier unten behandelt, statt
sie erneut zu lernen.

Drei Dinge, die der Aufbau leisten muss und die ein einzelner Knoten nicht
kann:

  * eine Instanz je Knoten, mit eigenem Befehlssocket und eigener
    axroute_data - deshalb startet start_node() mit "net -g -d <dir>";
  * eine STILLE, die man ein- und ausschalten kann.  Sie entsteht nicht durch
    einen Stub, sondern dadurch, dass der Port am anderen Ende ein
    RUFEZEICHEN traegt, das niemand beantwortet: der Knoten schickt seine
    XID-Anfrage ins Leere und wartet genau so, wie es ein echter digipeat
    mit verschwundener Station tut;
  * ein SEGMENT, auf dem mehrere Stationen hoeren - der Digipeat-Fall, bei
    dem A und B am selben Port sitzen.  Segment() speist denselben Rahmen in
    beide Master ein, statt ihn weiterzureichen.

Die Beweise sind die Traces der Knoten (trace <if> input output <datei>) und
deren Konsolenausgabe; dieses Modul sammelt beides, es richtet nichts ein.
Pruefungen stehen in eaxmatrix.py.
"""

import errno
import os
import pty
import re
import select
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
import tty

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NET = os.path.join(REPO, "src", "net")


class Failure(Exception):
    """Eine Erwartung des Teststands ist nicht erfuellt."""


# ---------------------------------------------------------------------------
# Ein pty-Paar: der Slave ist das, was ein Port als Geraet "/dev/ttyXY"
# oeffnet, der Master ist das Kabel.
# ---------------------------------------------------------------------------
def write_all(dst, data, tries=200):
    """Den ganzen Rahmen auf den Master, oder None, wenn es nicht geht.

    EIN PPTY-MASTER SCHREIBT NICHT IMMER ALLES.  os.write() sagt durch die
    zurueckgegebene Zahl, wie viele Bytes wirklich raus sind; was nicht in
    den Puffer passt, bleibt liegen.  Wer die Zahl wegwirft, verliert
    Rahmen - und zwar genau so, wie es der Test danach sieht:

      * im Trace des SENDERS steht der Rahmen vollstaendig, er ist ja
        wirklich hinausgegangen (dump() beim Senden),
      * im Trace des EMPFAENGERS steht gar nichts, denn ein halber KISS-
        Rahmen ist fuer kiss_recv() Muell: die Pruefsumme passt nicht, und
        verworfen wird ohne ein Wort.

    Gegen eine halbe Portion ist das hier die billigste Absicherung: die
    pty ist klein, ein Block *ist* moeglich, und die KISS-Schicht haelt
    einen halben Rahmen nicht aus.  GIERIG auf einen Fehler ist hier aber
    nicht - der Test haette sonst lange genug gesucht.  Szenario 4
    ("A, A-1 und Y auf derselben Seite") sah lange genau so aus und war
    doch keiner: B stand bei 100% CPU in start_timer() und schrieb gar
    nichts, weil del_ax25() den Vorgänger eines bereits freigegebenen
    Blocks geändert und dabei die Timerliste zerlegt hatte (ax25subr.c,
    "RAUS AUS DER LISTE, BEVOR IRGENDETWAS ANDERES LAEUFT").
    """
    view = memoryview(data)
    for _ in range(tries):
        if not view:
            return len(data)
        try:
            n = os.write(dst.fileno(), view)
        except OSError as e:
            if e.errno in (errno.EAGAIN, errno.EINTR, errno.EWOULDBLOCK):
                time.sleep(0.01)
                continue
            return None
        if n <= 0:
            return None
        view = view[n:]
    return None if view else len(data)
class Pty:
    def __init__(self):
        self.master, self.slave = pty.openpty()
        # FALLE 1 (TODO.txt): ein Slave hat Echo an.  Was der Master
        # einspeist, kaeme sonst als vermeintlicher Ausgang des Knotens
        # zurueck - man prueft dann die Leitung gegen sich selbst.
        tty.setraw(self.slave)
        # Und die Disziplin muss RAW bleiben, auch wenn der Port darauf
        # selbst umstellt: die haengt am Slave.
        self.name = os.ttyname(self.slave)

    def fileno(self):
        return self.master

    def close(self):
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass


# ---------------------------------------------------------------------------
# Zwei Enden, die ein Leitungspaar bilden.  Reicht die Bytes von einem Master
# zum anderen und sonst nichts: kein socat, kein zweiter Prozess, und der
# Aufbau kann mitten in der Sitzung abgeschnitten werden - was "der Weg ist
# tot" heisst und genau so behandelt wird.
# ---------------------------------------------------------------------------
class Wire:
    def __init__(self, a, b, name="wire"):
        self.a = a
        self.b = b
        self.name = name
        self.alive = True
        self.bytes = 0
        self.reason = ""          # warum die Leitung aufgehoert hat
        self._lock = threading.Lock()
        self._thread = threading.Thread(target=self._pump, daemon=True)
        self._thread.start()

    def _move(self, src, dst):
        """Eine Richtung der Leitung, bis sie tot ist oder der Test vorbei.

        DER RUECKGABEWERT VOM SCHREIBEN IST DIE GANZE WAHRHEIT: wer
        "os.write(...)" schreibt und die Zahl wegwirft, baut eine Leitung,
        die Rahmen verkauft, ohne es zu merken - siehe write_all() oben.
        """
        while self.alive:
            try:
                r, _, _ = select.select([src.fileno()], [], [], 0.2)
            except (OSError, ValueError):
                self._note("%s: select" % src.name)
                return
            if not r:
                continue
            try:
                data = os.read(src.fileno(), 4096)
            except OSError as e:
                self._note("%s: lesen: %s" % (src.name, e))
                return
            if not data:
                continue
            # FALLE 2 (TODO.txt): das KISS-TYPBYTE bleibt unangetastet.  Es
            # traegt die CRC-Markierung (0x80 SMACK, 0x20 RMNC), und
            # kiss_recv() prueft und entfernt die Pruefsumme nur daran.
            # Wer es umschreibt, haengt zwei Nutzlastbytes an jeden Rahmen.
            with self._lock:
                if not self.alive:
                    return
                sent = write_all(dst, data)
                if sent is None:
                    self.alive = False
                    self.reason = ("%s -> %s: der Master hat den Rahmen "
                                   "nicht genommen" % (src.name, dst.name))
                    return
                self.bytes += sent

    def _pump(self):
        t = threading.Thread(target=self._move, args=(self.a, self.b), daemon=True)
        t.start()
        self._move(self.b, self.a)
        self.alive = False

    def _note(self, why):
        """Warum eine Richtung aufgehoert hat - fuer den Fehlerbericht.

        Eine Leitung, die still stirbt, ist das Schlimmste an diesem
        Aufbau: der Knoten hat nichts verschuldet, und der Test haelt ihm
        das Gegenteil vor.  Also wird der Grund notiert, statt ihn zu
        schlucken (last_problem() in eaxmatrix.py holt ihn ab).
        """
        with self._lock:
            if not self.reason:
                self.reason = "%s (%s)" % (why, self.name)

    def cut(self):
        """Die Leitung mitten in der Sitzung abbrechen."""
        with self._lock:
            self.alive = False
        for p in (self.a, self.b):
            try:
                os.write(p.fileno(), b"")
            except OSError:
                pass

    def close(self):
        self.cut()
        self.a.close()
        self.b.close()


class Bus:
    """Ein Leitungspaar, an das sich mehrere Stationen haengen.

    Jeder Anschluss bekommt seinen eigenen Master und sieht jeden Frame, den
    ein anderer sendet - so wie Stationen auf einem gemeinsamen Segment.  Der
    Aufbau schreibt einen Frame genau einmal in jeden der anderen Master; ein
    Knoten darf ihn nicht zweimal sehen.
    """

    def __init__(self, name="segment"):
        self.name = name
        self.reason = ""
        self.ports = []          # Liste von Pty
        self._lock = threading.Lock()
        self._stop = False
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def attach(self):
        """Einen weiteren Anschluss anmelden.  Mehrere Stationen an EINEM
        Segment teilen sich diesen Thread - deshalb hier keiner je Anschluss.
        """
        p = Pty()
        with self._lock:
            self.ports.append(p)
        return p

    def _run(self):
        """Ein Rahmen geht genau einmal in jeden anderen Master.

        EIN Thread fuer alle Anschluesse, nicht einer je Anschluss: mit einem
        Thread je Port wuerde jedes Byte dreimal zustellt - einmal direkt vom
        Absender und noch einmal ueber jeden der beiden anderen Knoten, die es
        ebenfalls sehen.  Ein Knoten, der seinen eigenen Rahmen zum zweiten
        Mal saehe, wuerde ihn fuer ein Echo halten (ax_dup_recent in
        ax25.c) und fallen lassen: der Aufbau liefe eine Strecke weiter und
        zeigte die falsche Ursache - und die Ansage, die eine Route lehren
        soll, waere genau die, die dabei verschwaende.
        """
        while not self._stop:
            with self._lock:
                ports = list(self.ports)
            if not ports:
                time.sleep(0.05)
                continue
            try:
                ready, _, _ = select.select(ports, [], [], 0.1)
            except (OSError, ValueError):
                continue          # ein Port wurde zugemacht
            for src in ready:
                try:
                    data = os.read(src.fileno(), 4096)
                except OSError:
                    continue
                if not data:
                    continue
                for dst in ports:
                    if dst is src:
                        continue
                    if write_all(dst, data) is None:
                        self.reason = ("%s: der Master hat den Rahmen nicht "
                                       "genommen" % dst.name)

    def close(self):
        self._stop = True
        with self._lock:
            ports, self.ports = self.ports, []
        for p in ports:
            p.close()


# ---------------------------------------------------------------------------
# Ein Knoten: ein Prozess, ein Verzeichnis, eine Konsole, ein Befehlssocket.
# ---------------------------------------------------------------------------
class Node:
    def __init__(self, rig, name, call, ports, conf_extra=(), quiet=False,
                 default_if=None, debug=True):
        """ports: Liste von (label, Pty, Zusatzzeilen fuer die Konfiguration).

        default_if ist der Port fuer "ax25 route add <port> default".  Ohne
        einen solchen FANG hat der Knoten keinen Weg zu einem Ziel, dessen
        Ort er nicht kennt - und genau das ist bei einer Sonde der Fall:
        B fragt C ueber den Port, auf dem C liegt, und ohne FANG weiss der
        Knoten nicht, welcher das ist.  In einer echten net.rc steht diese
        Zeile aus demselben Grund, und sie steht hier auch.
        """
        self.rig = rig
        self.name = name
        self.call = call
        self.dir = os.path.join(rig.outdir, "node-" + name)
        os.makedirs(self.dir, exist_ok=True)
        self.conf = os.path.join(self.dir, "net.rc")
        self.console_path = os.path.join(self.dir, "console.log")
        self.sock_path = os.path.join(self.dir, ".sockets", "netcmd")
        self.proc = None
        self.tty = None
        self.log = None
        self.labels = [p[0] for p in ports]
        self.ports = ports
        self.conf_extra = list(conf_extra)
        self.default_if = default_if
        self.quiet = quiet
        # "-g" heisst Debug, und Debug heisst: keine Dateien lesen, keine
        # schreiben (axroute_loadfile(), axroute_savefile() in ax25file.c,
        # dasselbe in arpfile.c und ipfile.c).  Fuer die meisten Szenarien ist
        # das genau richtig - der Knoten faengt dann bei jedem Lauf mit
        # derselben leeren Tabelle an, und der Test muss sich nicht mit dem
        # Zustand des vorigenlaufs herumschlagen.  Fuer ein Szenario, das
        # gerade die Datei beim Start prueft, ist es das Gegenteil: dort wird
        # debug=False gesetzt, weil der Knoten sonst nie etwas laesst.
        self.debug = debug
        self._console_thread = None

    # -- Konfiguration ----------------------------------------------------
    def write_conf(self):
        lines = [
            "# von testtools/rig.py erzeugt - %s" % time.strftime("%Y-%m-%d %H:%M:%S"),
            "hostname     %s" % self.name,
            "ax25 mycall  %s" % self.call,
            "ip address   %s" % self.call,
            "",
        ]
        for label, pty, extra in self.ports:
            # "attach asy <base> <irq> <encapsulation> <device>|<label>
            #        <buffers> <mtu> <speed>"
            # Das Geraet ist ein pty-Slave; die Trennung mit "|" gibt dem Port
            # einen kurzen Namen, statt ihn nach /dev/ttyXY zu benennen, und
            # asy_attach() zerlegt sie selbst.  "kissi" ist der AX.25-Typ:
            # die Uebertragung enthaelt I-Frames, und ein Trace, der eine
            # Modulo-128-Verhandlung zeigen soll, muss sie sehen.
            lines.append("attach asy 0 0 kissi %s|%s 2048 1500 115200"
                         % (pty.name, label))
            lines.append("ifconfig %s linkaddress %s" % (label, self.call))
            for line in extra:
                lines.append(line)
            # "trace <if> <maske> file=<datei>" - EIN Optionswort, kein Wort pro
            # Richtung: 0x11 ist IF_TRACE_IN|IF_TRACE_OUT (iface.h).  Zwei
            # "trace"-Zeilen wuerden sich gegenseitig ersetzen, weil es nur
            # eine Zieldatei je Port gibt.  Seit maindev verlangt trace.c den
            # Dateinamen als "file=<pfad>"; ein nackter Pfad wird verweigert.
            lines.append("trace %s 0x11 file=%s" %
                         (label, os.path.join(self.dir, "trace-" + label)))
            lines.append("")
        for line in self.conf_extra:
            lines.append(line)
        if self.default_if:
            # Es gibt genau EINen FANG im Knoten (Axroute_default_ifp), auch
            # bei mehreren Ports - der letzte Aufruf gewinnt.  Deshalb wird
            # er hier einmal und nach allen Ports gesetzt.
            lines.append("ax25 route add %s default" % self.default_if)
        with open(self.conf, "w") as fp:
            fp.write("\n".join(lines) + "\n")
        return self.conf

    # -- Start ------------------------------------------------------------
    def start(self):
        if not os.path.exists(NET):
            raise Failure("%s fehlt - erst \"make -C src\"" % NET)
        self.write_conf()
        # Die Konsole ist ein pty, und zwar ein echtes: der Knoten braucht
        # isatty(0), um seine eigene Befehlszeile zu behaupten, und "connect"
        # nimmt sie danach in eine AX.25-Sitzung mit.  Der lesende Ende ist
        # der Master, und was von dort kommt, landet in console.log - sonst
        # waere die Konsole genau das, wonach die Startwarnungen suchen.
        self.tty = Pty()
        self.log = open(self.console_path, "wb", buffering=0)
        self.proc = subprocess.Popen(
            [NET] + (["-g"] if self.debug else []) + ["-d", self.dir, self.conf],
            stdin=self.tty.slave, stdout=self.tty.slave, stderr=self.tty.slave,
            close_fds=True, start_new_session=True)
        self._console_thread = threading.Thread(target=self._drain, daemon=True)
        self._console_thread.start()
        self.wait_ready()
        return self

    def _drain(self):
        """Die Konsole leer lesen und mitschreiben.

        Eine pty hat einen Puffer.  Lässt man ihn volllaufen, steht der
        Knoten beim ersten printf - und ein Test, der auf eine Antwort wartet,
        wartet dann auf nichts mehr.
        """
        fd = self.tty.fileno()
        while self.proc is not None and self.proc.poll() is None:
            try:
                r, _, _ = select.select([fd], [], [], 0.2)
            except (OSError, ValueError):
                return
            if not r:
                continue
            try:
                data = os.read(fd, 4096)
            except OSError:
                return
            if not data:
                return
            try:
                self.log.write(data)
            except OSError:
                return

    def wait_ready(self, timeout=20.0):
        """Warten, bis der Befehlssocket antwortet - nicht nur existiert."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise Failure("%s ist beim Start gestorben:\n%s" %
                              (self.name, self.console_text()))
            if os.path.exists(self.sock_path):
                try:
                    self.cmd("ax25 route stat", quiet=True)
                    return
                except (OSError, Failure):
                    pass
            time.sleep(0.1)
        raise Failure("%s war nach %.0f s nicht bereit:\n%s" %
                      (self.name, timeout, self.console_text()))

    def wait_gone(self, timeout=15.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                return
            time.sleep(0.1)

    # -- Befehle ----------------------------------------------------------
    def cmd(self, line, quiet=False):
        """Ein Befehl ueber den Befehlssocket, mit Antwort.

        Der Kanal kennt kein Endezeichen: der Knoten schreibt die Ausgabe des
        Befehls auf den Socket und haelt die Verbindung danach offen.  Gelesen
        wird deshalb, bis eine Weile nichts mehr kommt.  250 ms Ruhe sind
        unkritisch, weil die Antwort auf einem lokalen Socket in einem
        Durchlauf steht.
        """
        if not os.path.exists(self.sock_path):
            raise Failure("%s: kein Befehlssocket (%s)" % (self.name, self.sock_path))
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(10.0)
        try:
            s.connect(self.sock_path)
            s.sendall(("command " + line + "\n").encode())
            s.shutdown(socket.SHUT_WR)
            out = b""
            s.settimeout(0.25)
            while True:
                try:
                    chunk = s.recv(4096)
                except socket.timeout:
                    break
                if not chunk:
                    break
                out += chunk
        finally:
            s.close()
        text = out.decode("utf-8", "replace")
        if not quiet and not self.quiet:
            for l in text.splitlines():
                print("      %s| %s" % (self.name, l))
        return text

    def console_cmd(self, line):
        """Ein Befehl auf die Konsole, ohne auf sein Ende zu warten.

        "connect" nimmt die Konsole in eine AX.25-Sitzung mit: danach ist die
        Konsole nicht mehr die Konsole.  Genau das ist hier richtig - die
        Sitzung laeuft im Hintergrund weiter, der Befehlssocket bleibt
        bedienbar, und die Beweise kommen aus den Traces.
        """
        os.write(self.tty.fileno(), (line + "\n").encode())

    # -- UI-Rahmen und Sitzungen ueber den Befehlskanal -------------------
    def _open(self):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(10.0)
        s.connect(self.sock_path)
        return s

    def ui(self, label, dest, text="hallo", digis=(), wait=0.3):
        """Einen UI-Rahmen auf einen Port senden, mit Digipeat-Pfad.

        Der Befehlskanal hat dafuer den Modus "datagram <port>: <ziel> [via
        <digi>...]" - der Weg steht einmal im Befehl, danach ist jede Zeile
        ein Rahmen.  Der Kopf ist der eines "connect", nur ohne Sitzung: das
        ist der Weg, den ein APRS-Programm auch geht, und der Weg, den der
        Test braucht, um eine Station ueber einen Digipeat zu erreichen.

        Das Leerzeichen hinter dem Doppelpunkt ist nicht Kosmetik: ohne ihn
        liest der Knoten "b0:db0aaa" als EIN Argument und stirbt mit einer
        Parse-Meldung, statt den Rahmen zu senden.
        """
        target = dest
        for d in digis:
            target += " via " + d
        s = self._open()
        try:
            s.sendall(("datagram %s: %s\n" % (label, target)).encode())
            time.sleep(0.1)
            s.sendall((text + "\n").encode())
            time.sleep(wait)
        finally:
            s.close()

    def session(self, label, dest, digis=()):
        """Einen Verbindungsversuch starten und die Sitzung offen halten.

        "connect" nimmt die Konsole in die Sitzung mit, und die Konsole ist
        dann fuer nichts mehr zu gebrauchen - deshalb laeuft er hier ueber
        den Befehlskanal, und die Verbindung bleibt offen, bis close() sie
        beendet: haengt der Befehlssocket, haengt auch der Verbindungsversuch,
        und der Test kann in Ruhe zusehen, was der Digipeat macht.
        """
        s = self._open()
        target = dest
        if digis:
            target += " via " + " ".join(digis)
        s.sendall(("connect %s: %s\n" % (label, target)).encode())
        s.settimeout(1.0)
        try:
            s.recv(4096)         # die Meldung, falls es eine gab
        except socket.timeout:
            pass
        return Session(self, s)

    def hold(self, line):
        """Einen Befehl absetzen und die Verbindung offen halten.

        Fuer alles, was im Betrieb eine offene Verbindung braucht - vor allem
        "ax25 route del" gefolgt von einem Blick in die Datei.
        """
        s = self._open()
        s.sendall(("command " + line + "\n").encode())
        s.settimeout(1.0)
        out = b""
        try:
            out = s.recv(65536)
        except socket.timeout:
            pass
        return Session(self, s, out.decode("utf-8", "replace"))

    def route(self, call=None):
        """ax25 route list als Liste von Zeilen."""
        text = self.cmd("ax25 route list" + (" " + call if call else ""),
                        quiet=True)
        return [l for l in text.splitlines() if l.strip()]

    def route_row(self, call):
        """Die Zeile zu einem Rufzeichen, oder None.

        "ax25 route list <call>" ist auskunftsfreudig: fuer ein Rufzeichen,
        das nicht in der Tabelle steht, schreibt es "*** Not in table ***
        <call>" und beendet sich mit 1.  Das ist eine Zeile, die das
        Rufzeichen enthaelt, aber KEINE Route - wer sie fuer eine haelt,
        prueft eine Tabelle, in der der Knoten selbst sagt, dass es sie nicht
        gibt.

        Eine gelernte Route hat drei Zeichen: E (modulo-128 ging), e (ging
        nicht), Leer (nie gefragt) - siehe ax25 route list ?
        """
        for line in self.route(call):
            if "Not in table" in line:
                continue
            if re.search(r"\b%s\b" % re.escape(call.upper()), line.upper()):
                return line
        return None

    def route_mark(self, call):
        """Nur das E/e/Leer einer Route, ohne Leerzeichen drumherum."""
        row = self.route_row(call)
        if row is None:
            return None
        # "2-Sep 07:31  b1  Ee   DB0CCC" bzw. "2-Sep 07:31  b1   e  DB0CCC"
        m = re.search(r"\s(b1|b0|[a-z0-9]+)\s+([Ee ])([Ee ])?\s", row)
        if not m:
            m = re.search(r"([Ee])([Ee])?\s", row)
        return m.group(0).strip() if m else None

    def stop(self):
        if self.proc is None:
            return
        if self.proc.poll() is None:
            try:
                self.cmd("exit", quiet=True)
            except (OSError, Failure):
                pass
            if not self.wait_gone(5.0):
                try:
                    os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
                except OSError:
                    self.proc.terminate()
                if not self.wait_gone(5.0):
                    try:
                        os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
                    except OSError:
                        self.proc.kill()
                    self.wait_gone(3.0)
        try:
            self.tty.close()
        except (OSError, AttributeError):
            pass
        try:
            self.log.close()
        except (OSError, AttributeError):
            pass

    # -- Beweise ----------------------------------------------------------
    def console_text(self):
        try:
            with open(self.console_path, "r", errors="replace") as fp:
                return fp.read()
        except OSError:
            return ""

    def trace(self, label):
        path = os.path.join(self.dir, "trace-" + label)
        try:
            with open(path, "r", errors="replace") as fp:
                return fp.read()
        except OSError:
            return ""

    def trace_lines(self, label):
        return [l for l in self.trace(label).splitlines() if l.strip()]


class Rig:
    def __init__(self, outdir):
        # Ein frischer Lauf faengt mit einem leeren Verzeichnis an: die
        # Knoten schreiben ihre Traces im Append-Modus (trace.c: fopen
        # mit APPEND_TEXT), und ein alter Trace wuerde gezaehlt, wo nur
        # die Rahmen dieses Laufs gezaehlt werden sollen.  Fuer eine
        # Kompatibilitaets-Suite, die oft auf demselben Rechner laeuft,
        # ist das der Unterschied zwischen "eins zu viel" und "genau eins".
        shutil.rmtree(outdir, ignore_errors=True)
        self.outdir = outdir
        os.makedirs(outdir, exist_ok=True)
        self.nodes = []
        self.links = []
        self.buses = []

    # -- Aufbau -----------------------------------------------------------
    def node(self, name, call, ports, conf_extra=(), quiet=False,
             default_if=None, debug=True):
        n = Node(self, name, call, ports, conf_extra, quiet, default_if, debug)
        self.nodes.append(n)
        return n

    def start(self):
        for n in self.nodes:
            if n.proc is None:
                n.start()
        return self

    def stop(self):
        for n in reversed(self.nodes):
            n.stop()
        for w in self.links:
            w.close()
        for b in self.buses:
            b.close()
        self.nodes = []
        self.links = []
        self.buses = []

    def problems(self):
        """Was die Leitungen themselves zu beanstanden haben.

        Ein Fehlschlag, der nicht im Knoten liegt, sondern in der Leitung
        darunter, ist der teuerste Befund dieses Aufbaus: der Test meldet
        dann "B sieht Y nicht", und der Leser geht im AX.25-Code auf die
        Suche.  Deshalb haengt der Grund am Aufbau und wird bei jedem
        Warten mit ausgegeben - nicht als Entschuldigung, sondern als
        Gegenbeweis: steht hier etwas, ist der Knoten nicht der Schuldige.
        """
        out = []
        for w in list(self.links) + list(self.buses):
            why = getattr(w, "reason", "")
            if why:
                out.append(why)
        return out

class Session:
    """Eine offene Befehlssocket-Verbindung zu einem Knoten."""

    def __init__(self, node, sock, out=""):
        self.node = node
        self.sock = sock
        self.out = out

    def send(self, data):
        try:
            self.sock.sendall(data.encode() if isinstance(data, str) else data)
        except OSError:
            pass

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass
        # Der Knoten raeumt die Sitzung ab, wenn der Kanal weg ist; das
        # braucht einen Moment, sonst arbeitet der Test weiter gegen eine
        # Verbindung, die es gerade nicht mehr gibt.
        time.sleep(0.2)


# ---------------------------------------------------------------------------
# Wartetest
# ---------------------------------------------------------------------------
def wait_until(pred, timeout, what):
    """Warten, bis pred() True liefert; sonst Failure mit dem, was war."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if pred():
            return
        time.sleep(0.1)
    raise Failure("nach %.1f s: %s" % (timeout, what))


def count(text, needle):
    return text.count(needle)


def frames(node, label):
    """Die AX.25-Rahmen aus einem Trace, als Liste von Zeilen.

    Der Trace beginnt jede Richtung mit " - <port> sent:" bzw. "recv:"; die
    ax25_dump-Ausgabe ist die Zeile danach.  Was gezaehlt wird, sind Rahmen,
    keine Zeilen mit dem Wort - deshalb diese Klammer.
    """
    out = []
    armed = False
    for line in node.trace(label).splitlines():
        if " sent:" in line or " recv:" in line:
            armed = True
            continue
        if armed and "AX25: " in line:
            out.append(line.strip())
            armed = False
    return out
