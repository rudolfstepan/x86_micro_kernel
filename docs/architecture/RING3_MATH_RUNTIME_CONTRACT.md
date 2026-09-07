# Ring-3-Mathematikprofil 1 (R3.21)

Stand: 7. September 2026. Vor Implementierung eingefroren, jetzt abgenommen.
28 Hostfaelle, beide Referenzen, Math-Gaeste mit einer/vier CPUs, bestehende
libc-/Browsergaeste und unveraenderte Kernel-/Programmhashes bestehen.
Vollstaendige Belegkarte und erhaltene Fehler: CURRENT_WORK.md.

Additive R3.23-Erweiterung: `long lrint(double)` aus dem unveraenderten
musl-1.2.6-Member `src/math/i386/lrint.c`. Die 44 binary64-Funktionen bleiben
unveraendert; hinzu kommt eine ISO-C-7.12.9.5-Konversion fuer den JS-Kern.
Der echte x87-fistpl-Pfad beruecksichtigt alle vier Rundungen und meldet
inexact/invalid ueber fenv; NaN/Inf/ausserhalb long sind keine definierten
numerischen Erfolgswerte. Keine generische C-Konversion ausserhalb ihres
darstellbaren Bereichs. JSTEST und der i386-O0/O2-Host pruefen positive und
negative Halbwerte, LONG_MIN, NaN und Inf. Bestehende MATHTEST-Marker und
Abnahmegrenzen bleiben unveraendert. R3.23-Abnahmestatus: CURRENT_WORK.md.

## Grenze und Quellen

Nach FPU-Abnahme `0301d708` fehlte im opt-in SDK noch `math.h`/`libm`.
Das bereits inventarisierte QuickJS 2026-06-04 nutzt unter anderem sqrt,
pow, exp/log und trigonometrische/hyperbolische Funktionen. Dieser Schnitt
liefert ausschliesslich ihre numerische Ring-3-Grundlage. Engine, DOM,
Zahlen-Textkonvertierung, stdio, setjmp und Zeitadapter bleiben weitere
eigene Portierungsvertraege; keine funktionslosen POSIX-Stubs.

Referenz: [ISO-C11-Entwurf N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf),
Abschnitte 7.6/7.12, IEEE-754 binary64 und i386 SysV-C-Aufrufkonvention.
[musl 1.2.6, offizielle Freigabe](https://www.openwall.com/lists/musl/2026/03/20/1):
Originalarchiv SHA256 `d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a`.
Es werden nur unveraenderte generische numerische Quellen, Hilfstabellen,
private Header, der originale i386-fsqrt-Helfer und Lizenztexte extrahiert.
Der Helfer dient ausschliesslich acosh bei erweiterter x87-Auswertung und
wird intern umbenannt, ohne oeffentliche sqrtl-Familie. Der veraltete drem-
Alias wird nicht exportiert. Keine Linux-, Thread-, Allocator-
oder Signalimplementierung. Kleine REIST-Header begrenzen die angebotene API;
der separate x87/MXCSR-Adapter arbeitet ausschliesslich im aufrufenden Ring-3-
Prozess unter der abgenommenen FPU-Isolation. Keine neuen Syscalls.

## Vollstaendiger Funktionsumfang dieses Teilprofils

`fabs sqrt cbrt floor ceil trunc round rint nearbyint sin cos tan asin acos
atan atan2 sinh cosh tanh asinh acosh atanh exp exp2 expm1 log log2 log10
log1p pow hypot fmod remainder remquo frexp ldexp scalbn scalbln modf
copysign fmin fmax fdim nextafter`: 44 binary64-Funktionen als ein Paket.
Standard-Klassifikationsmakros unterscheiden NaN, Inf, normal/subnormal und
vorzeichenbehaftete Null. Keine float-/long-double-Funktionsfamilie oder
vollstaendige libm-/ECMAScript-Kompatibilitaetszusage.

`fegetround`, `fesetround`, `feclearexcept`, `fetestexcept` bilden das benoetigte
ISO-C-fenv-Teilprofil. Vier Standardrundungsrichtungen; ungueltige Rundungswerte
geben Fehler vor Aenderung zurueck. x87 und MXCSR werden zusammen behandelt,
ohne Kontrollmasken/Precision/FTZ/DAZ zu veraendern. Nur deklarierte
Ausnahmeflags werden geloescht/gelesen. `math_errhandling=MATH_ERREXCEPT`;
keine errno-Zusage, Trap-Enable-API oder nachgebauter Kernel-FPU-Vertrag.

## Begrenzung, Build und Recovery

Keine Heapallokation, I/O, Threads oder globale dynamische Initialisierung.
Schleifen der ausgewaehlten Algorithmen sind durch binary64-Exponent und
Praezision begrenzt; Argumentreduktion verwendet statische Tabellen und
feste lokale Arrays. Keine schnelle Ersatzapproximation oder fast-math.
Das i386-Profil bleibt x87 ohne compilererzeugtes SSE/MMX.

Opt-in `usr/include/reist/math`, `usr/lib/libm.a` und `reistmath.pc`; bestehende
SDK-Include-/Linkdefaults bleiben unveraendert. Archivextraktion prueft Pin,
exakte Member, regulare Dateien, 128-Datei-/256-KiB-Member-/2-MiB-Gesamtgrenze.
Vier parallele Objektcompiler maximal, begrenzte Prozesse, unveraenderte
Artefaktzeitstempel bei unveraenderten Inputs. COPYRIGHT und einzelne
Quelllizenzen werden mitgeliefert.
Das finale Archiv besitzt keine offenen externen Symbole. MATHTEST linkt
fuer seine IPC-Byteinitialisierung die bestehende libreistc, ohne Heapstart;
ein arithmetisches Compilerarchiv ersetzt diese Byte-Laufzeit nicht. Sein
Build-Rezept ist ausdruecklicher inkrementeller Input, damit Linkaenderungen
nicht an einem alten Programm vorbeigehen.

`/usr/bin/mathtest.prg` ist ueber `mathtest` in der normalen Ring-3-Shell
erreichbar, identisch in Windows- und Make-Layout. Normale mathematische
Bereichsfehler bleiben IEEE-Sonderwerte/Flags. Absichtlich entmaskierter #MF
betrifft nur das Testkind; Wait, generationstreues Reap, Kill und frischer
Folgeprozess pruefen die vorhandene OS-Fehlergrenze. Kein Kind bekommt neue
Geraete-/Netzwerkrechte. Gastdeadline180s, vier CPUs/1024MiB im SMP-Lauf.

## Eingefrorene Abnahme und Leistungsschutz

Echte i386-Algorithmen/Fenv O0/O2, alle44 Funktionen, endliche Referenzvektoren
mit maximal4ULP fuer deklarierte nearest-Transzendentalsamples, exakte Bits
fuer exakte Ergebnisse/Vorzeichen/Klassen; unabhängige deterministische
Hostvergleichssamples und Sonderwerte. Kein universeller Correct-Rounding-
Claim aus Stichproben. Public C/C++-Header, Linkabschluss, Pin-/Extraktions-
Fehler und inkrementelle Builds werden geprueft. Neue Hostausfuehrungen30s,
Compiler90s maximal. Alle Befehle/Stopps stehen vorab in der Queue.

Vorherhashes fuer beide Kernelprofile, BROWSER, HTMLWORK, GTEST und BENCHMARK
werden vor Implementierung gesichert. Diese gemessenen Artefakte muessen
bytegleich bleiben. Der abgenommene VMware-Vergleich und seine Vorherimages
werden weder ersetzt noch erneut gemessen, solange kein darin ausgefuehrter
Code geaendert wird. Keine Behauptung, dass neue Math-Funktionen eine bereits
gemessene Latenz oder WCET besitzen. Kernel-/ABI-/Quota-/Benchmarkaenderungen
waeren ein separater Auftrag, kein Portierungsworkaround.
