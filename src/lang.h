/* lang.h
 * Copyright (C) 2026 Thomas Osterried <dl9sau@darc.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* @(#) $Id$ */

/* SPRACHE DER MELDUNGEN AN DEN BEDIENER.
 *
 * WAMPES spricht mit einem Menschen, der die Software nicht auf Deutsch
 * gelesen hat.  Englisch ist deshalb der Default, und zwar ohne jede
 * Ruecksicht auf die Umgebung, aus der der Daemon gestartet wurde:  die
 * Sprache, in der ein Netzdienst antwortet, darf nicht davon abhaengen,
 * welche Shell es gerade war.  Wer sie umgestellt haben will, baut mit
 * -DLANG_USE=LANG_DE, und es ist dann fuer alle Ports dieselbe.
 *
 * Die deutsche Fassung steht im Quelltext daneben und geht dadurch nicht
 * verloren - das ist der ganze Zweck dieses Aufbaus.  Sie ist nicht
 * automatisch gepflegt und muss mitgeaendert werden, wenn sich der
 * englische Text aendert.
 *
 * DARAUF ACHTEN, DASS DER DEUTSCHE TEXT ZUERST STEHT:  er ist der, der
 * im Quelltext schon da war, und er soll an seiner Stelle bleiben.
 *
 * UEBERARBEITET werden muss hier wenig, weil der Aufbau an den
 * Aufrufstellen liegt:  _LANG(deutsch, englisch) waehlt zwischen zwei
 * Ausdruecken, die der Compiler beide uebersetzt und zurueckhaelt, aber
 * nur einen in das fertige Programm traegt.  Zur Laufzeit wird nichts
 * geprueft und nichts entschieden.
 *
 * WER HIER EINE DRITTE SPRACHE EINTRAEGT, muss an zwei Stellen nach:
 * in der Liste der Sprachen hier unten und in der Liste der Texte in
 * _LANG() - dort ist die Reihenfolge der Argumente die Reihenfolge der
 * Sprachen.  Sonst haengt #else still an der letzten Sprache.
 */

#ifndef _LANG_H
#define _LANG_H

#define LANG_EN 0
#define LANG_DE 1

#ifndef LANG_USE
#define LANG_USE LANG_EN
#endif

#if LANG_USE == LANG_DE
#define _LANG(de, en) (de)
#else
#define _LANG(de, en) (en)
#endif

#endif	/* _LANG_H */
