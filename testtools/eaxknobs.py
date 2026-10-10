#!/usr/bin/env python3
"""Die zwei Knoepfe (eax25, ax25-v2.2) und was auf der Leitung steht.

Dies ist die kleine, direkte Antwort auf die Frage "was macht der Knoten
eigentlich bei welcher Einstellung".  Statt eines vollen Szenariumsatzes
haengen hier zwei (bzw. drei) echte net-Instanzen an pty-Paaren, der Trace
ist an, und es wird systematisch durchgerastert:

  eax25      off | accept | initiate | always
  ax25-v2.2  off | accept | initiate | always

und zwar in vier Aufbauten:

  direct        A -- B            A verbindet direkt zu B (B ist das Ende)
  direct-offfar A -- B            wie direct, nur B.b0 auf ax25-v2.2 off
                                  (die Frage: antwortet B dann noch XID?)
  relay         A -- B -- C       A verbindet zu C ueber B; C lebt
  relay-dead    A -- B -- (nichts) derselbe Relais, aber hinter b1 ist nichts

Der Raster laeuft ueber den Port UNTER TEST:

  direct*,  A's a0   (der Anrufer)
  relay*,   B's b1   (das zweite Bein, der Weg zum fernen Ende)

Ausgegeben wird je Kombination, welche Steuerrahmen in welche Richtung
gingen - XID(P) (Frage), XID(F) (Antwort), SABME, SABM, DM, UA - damit ein
Blick genuegt: sendet der Anrufer SABME oder SABM, und antwortet das ferne
Ende ueberhaupt.

Aufruf:  testtools/eaxknobs.py [gruppen] [--knobs E[,E...]] [--wait S]
                                 [basisverzeichnis]

  gruppen   Untermenge von direct,direct-offfar,relay,relay-dead
            (Vorgabe: alle).  "all" ist ebenfalls erlaubt.
  --knobs   welche eax25-Werte gerastert werden (Vorgabe: alle vier)
  --wait    Wartezeit je Kombination in Sekunden (Vorgabe 22: deckt T1 5 s,
            XID_LEG_TIMEOUT 10 s und die volle Rufer-T1 ~15 s ab)

Die Traces aller Knoten liegen unter <basisverzeichnis>, damit ein
Auffaelligkeit nachgelesen werden kann.
"""

import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rig import Rig, Pty, Wire

CALL_A = "db0aaa"
CALL_B = "db0bbb"
CALL_C = "db0ccc"

# Die beiden Achsen.  ax25-v2.2 hat dieselben vier Werte wie eax25.
EAX25_VALUES = ("off", "accept", "initiate", "always")
V2_VALUES = ("off", "accept", "initiate", "always")

GROUPS = ("direct", "direct-offfar", "relay", "relay-dead")

# Wartezeit je Kombination.  Muss die laengste Frist eines Aufbaus abdecken:
# T1 (5 s) fuer eine unbeantwortete SABM-Frage, XID_LEG_TIMEOUT (10 s, ax25.h)
# fuer ein unbeantwortetes Relais-Bein, und die volle T1 des Rufers (mit
# einem Digi rund 15 s), denn auf dem Relais-Bein ist SEIN naechstes SABM der
# Rahmen, der die Rueckfall-Entscheidung ausloest (doc/EAX25.md).
WAIT = 22.0


# ---------------------------------------------------------------------------
# Trace lesen: Richtung + Steuerrahmen
# ---------------------------------------------------------------------------
def events(node, label, since=0):
    """Die Rahmen eines Ports als Liste (richtung, token, zeile).

    Der Trace schreibt vor jeden Rahmen eine Zeile " - <port> sent:" bzw.
    "recv:" (trace.c).  Die Richtung gehoert dazu: "A hoert XID(F)" und "A
    sendet XID(F)" sind zwei verschiedene Befunde, und wer nur die Zeile
    liest, verwechselt sie.

    since: Zeilennummer, ab der gelesen wird - die Traces haengen an
    (trace.c oeffnet mit APPEND), und ohne das zaehlte die Kombination
    davor mit.
    """
    out = []
    direction = None
    for line in node.trace(label).splitlines()[since:]:
        if " sent:" in line:
            direction = "out"
            continue
        if " recv:" in line:
            direction = "in"
            continue
        if direction and "AX25: " in line:
            out.append((direction, token(line.strip()), line.strip()))
            direction = None
    return out


def token(line):
    """Der Steuerrahmen einer Trace-Zeile, kurz.

    Reihenfolge ist wichtig: "SABME" enthaelt "SABM", und "XID(F)" enthaelt
    "XID" - wer ungeordnet sucht, haelt jede SABME fuer eine SABM.
    """
    if " XID" in line:
        return "XID(F)" if "(F)" in line else "XID(P)"
    if " SABME" in line:
        return "SABME"
    if " SABM" in line:
        return "SABM"
    if " FRMR" in line:
        return "FRMR"
    if " DM" in line:
        return "DM"
    if " UA" in line:
        return "UA"
    if " DISC" in line:
        return "DISC"
    return "?"


def only(ev, direction):
    return [t for d, t, _ in ev if d == direction]


def _line(node, label):
    return len(node.trace(label).splitlines())


# ---------------------------------------------------------------------------
# Aufbauten
# ---------------------------------------------------------------------------
def build_direct(rig):
    """A -- B.  A ruft B direkt; B ist das ferne Ende."""
    pa, pab = Pty(), Pty()
    rig.links.append(Wire(pa, pab, "A--B"))
    a = rig.node("a", CALL_A, [("a0", pa, [])], default_if="a0")
    b = rig.node("b", CALL_B, [("b0", pab, [])], default_if="b0")
    return a, b


def build_relay(rig, dead):
    """A -- B -- C.  A ruft C ueber B; fehlt C, haengt b1 ins Leere.

    b1 bekommt bewusst KEIN Kabel, wenn `dead`: eine pty, an deren Master
    niemand liest, ist genau der Fall "da ist nichts" - kein Stub, kein
    Modell, sondern ein Port, dessen Gegenueber nicht existiert.  B sendet
    hinein und hoert nur die eigene Stille.
    """
    pa, pab = Pty(), Pty()
    pb1, pbc = Pty(), Pty()
    rig.links.append(Wire(pa, pab, "A--B"))
    a = rig.node("a", CALL_A, [("a0", pa, [])], default_if="a0")
    b = rig.node("b", CALL_B,
                 [("b0", pab, ["ax25 route add b0 " + CALL_A]),
                  ("b1", pb1, [])], default_if="b1")
    c = None
    if not dead:
        rig.links.append(Wire(pb1, pbc, "B--C"))
        c = rig.node("c", CALL_C, [("c0", pbc, [])], default_if="c0")
    return a, b, c


def set_knobs(node, label, eax25=None, v2=None):
    if eax25 is not None:
        node.cmd("ifconfig %s eax25 %s" % (label, eax25), quiet=True)
    if v2 is not None:
        node.cmd("ifconfig %s ax25-v2.2 %s" % (label, v2), quiet=True)


# ---------------------------------------------------------------------------
# Ein Durchlauf je Kombination
# ---------------------------------------------------------------------------
def run_direct(outdir, E, V, far_v2):
    """A ruft B, a0 auf (E,V), b0 auf far_v2.  Liefert Port-Ereignisse."""
    rig = Rig(outdir)
    a, b = build_direct(rig)
    rig.start()
    set_knobs(a, "a0", E, V)
    set_knobs(b, "b0", None, far_v2)
    na, nb = _line(a, "a0"), _line(b, "b0")
    s = a.session("a0", CALL_B)
    time.sleep(WAIT)
    res = {"a0": only_dir(events(a, "a0", na)),
           "b0": only_dir(events(b, "b0", nb))}
    s.close()
    rig.stop()
    return res


def run_relay(outdir, E, V, dead, caller=("off", "off")):
    """A ruft C ueber B, b1 (das zweite Bein) auf (E,V).

    caller ist (eax25, ax25-v2.2) fuer A's a0.  Vorgabe "off"/"off": A klopft
    schlicht mit SABM an.  So entsteht auf B das Bein ueber est_link() und
    xid_leg_open() - der Weg, den die gemeldete Beobachtung beschreibt.  Mit
    "initiate"/"initiate" fragt A zuerst per XID, und dann laeuft stattdessen
    der Punkt-D-Auftrag in ax25.c (ohne link_to) - ein anderer Weg.
    """
    rig = Rig(outdir)
    a, b, c = build_relay(rig, dead)
    rig.start()
    set_knobs(a, "a0", caller[0], caller[1])
    set_knobs(b, "b1", E, V)
    na, nb = _line(a, "a0"), _line(b, "b1")
    nb0 = _line(b, "b0")
    nc = _line(c, "c0") if c is not None else 0
    s = a.session("a0", CALL_C, digis=(CALL_B,))
    time.sleep(WAIT)
    res = {"a0": only_dir(events(a, "a0", na)),
           "b0": only_dir(events(b, "b0", nb0)),
           "b1": only_dir(events(b, "b1", nb))}
    if c is not None:
        res["c0"] = only_dir(events(c, "c0", nc))
    s.close()
    rig.stop()
    return res


def only_dir(ev):
    return {"out": only(ev, "out"), "in": only(ev, "in")}


# ---------------------------------------------------------------------------
# Ausgabe
# ---------------------------------------------------------------------------
def render(label, res):
    print("  %-4s out: %-28s in: %s"
          % (label, " ".join(res["out"]) or "-", " ".join(res["in"]) or "-"))


def verdict(res):
    """Ein Wort zur Sitzung: kam eine UA zurueck?"""
    for port in res.values():
        if "UA" in port["in"]:
            return "UA gesehen"
    return "keine UA"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default="/tmp/eaxknobs",
                    help="Basisverzeichnis fuer die Knoten (Vorgabe /tmp/eaxknobs)")
    ap.add_argument("groups", nargs="*", default=list(GROUPS),
                    help="direct direct-offfar relay relay-dead (oder all)")
    ap.add_argument("--knobs", default=",".join(EAX25_VALUES),
                    help="eax25-Werte, komma-getrennt")
    ap.add_argument("--v2", default=",".join(V2_VALUES),
                    help="ax25-v2.2-Werte, komma-getrennt")
    ap.add_argument("--wait", type=float, default=WAIT,
                    help="Wartezeit je Kombination in Sekunden")
    ap.add_argument("--caller", default="off/off",
                    help="eax25/ax25-v2.2 fuer A's a0 im Relais (Vorgabe "
                         "off/off = schlichtes SABM; ask = initiate/initiate)")
    args = ap.parse_args(argv)

    globals()["WAIT"] = args.wait

    caller = ("off", "off")
    if args.caller in ("ask", "initiate", "initiate/initiate"):
        caller = ("initiate", "initiate")
    elif "/" in args.caller:
        ce, cv = args.caller.split("/", 1)
        caller = (ce, cv)

    groups = args.groups
    if not groups or "all" in groups:
        groups = list(GROUPS)
    for g in groups:
        if g not in GROUPS:
            ap.error("unbekannte Gruppe %r (erlaubt: %s)" % (g, ", ".join(GROUPS)))

    eaxs = [x for x in args.knobs.split(",") if x]
    v2s = [x for x in args.v2.split(",") if x]

    for g in groups:
        far_v2 = "off" if g == "direct-offfar" else "accept"
        dead = (g == "relay-dead")
        how = {"direct": "A--B, a0 gerastert",
               "direct-offfar": "A--B, a0 gerastert, b0 ax25-v2.2 off",
               "relay": "A--B--C, b1 (zweites Bein) gerastert",
               "relay-dead": "A--B--(nichts), b1 gerastert"}[g]
        print("\n================ %s  (%s) ================" % (g, how))
        for E in eaxs:
            for V in v2s:
                print("--- eax25=%-8s ax25-v2.2=%s" % (E, V))
                outdir = os.path.join(args.dir, "%s-%s-%s" % (g, E, V))
                if g.startswith("direct"):
                    res = run_direct(outdir, E, V, far_v2)
                else:
                    res = run_relay(outdir, E, V, dead, caller)
                for port in sorted(res):
                    render(port, res[port])
                print("  => %s" % verdict(res))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
