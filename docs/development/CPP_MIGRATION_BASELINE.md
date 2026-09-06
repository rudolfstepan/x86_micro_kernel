# C++-Migration: Ausgangsbasis (TASK-0001)

Stand: 6. September 2026. Paket `R3.16a-cpp-migration-baseline`.
Produktionsstand: `cc3d0dcbf4f49da231fa75dc0c0cb45e4476f13a`.
Vertragsstand vor Messung: `61932c14`. Keine Produktionsquelle wurde migriert.
Die unveraenderte [Nutzervorgabe](../REIST_CPP_MIGRATION_PLAN.md) hat Gitblob
`1851b3571c2b26ec664bb792a841af23b9739f4c`.

## Inventar und reproduzierbare Zaehldefinition

[Messdatensatz](cpp_migration_baseline.json), erzeugt durch
`scripts/measure_cpp_baseline.py`, Schema 1. Er enthaelt alle 72 Dateipfade,
deren Gitblobs, Einzelmetriken, Artefakt-SHA-256 und alle fuenf Timingproben.
Die Quellen werden direkt aus dem angegebenen Commit gelesen und gegen den
Worktree verglichen; nur CRLF/LF wird fuer diesen Quellvergleich vereinheitlicht.
Grenzen: 256 Dateien pro Gruppe, 2 MiB pro Quelle, feste Subprozessfristen.

| Gruppe / Wurzeln | Dateien | Zeilen physisch | Nichtleer | Codezeilen |
|---|---:|---:|---:|---:|
| Browser: `userspace/gui/apps/browser` | 22 | 5.437 | 5.381 | 5.170 |
| GUI: `userspace/gui/lib` und `include` | 28 | 9.566 | 8.905 | 8.130 |
| Compositor: `userspace/gui/compositor` | 22 | 18.264 | 17.362 | 16.920 |

Physische Zeilen sind `splitlines()` nach CRLF->LF; nichtleer bedeutet nach
`strip()` nicht leer. Fuer Codezeilen werden C-Kommentare entfernt und
String-/Zeichenliterale bei erhaltener Zeilenstruktur maskiert. Praeprozessor,
Header und deaktivierte Zweige zaehlen mit. Die eine ASM-Datei
`desktop_splash.s` verwendet ebenfalls diese lexikalische Regel, keinen
eigenen ASM-Parser. Fremdparser, SDK und generierte Buildquellen ausserhalb
der Wurzeln sind nicht in diesen LOC enthalten.

Die GUI-Quellgruppe ist nicht mit dem Archiv gleichzusetzen:
`GUI_LIBRARY_SOURCES` in `scripts/build_user_sdk.py` umfasst die 12 Module
menu, dialog, file_dialog, surface_client, control, container, tabs,
value_controls, text_editor, piece_document, font und font_catalog.
Der Quellbaum enthaelt zusaetzlich HTML-Dokument-/URL-Unterstuetzung und
oeffentliche Header; diese zaehlen im Inventar, nicht als Archivmitglieder.

| Lexikalische Indikatoren | Browser | GUI | Compositor |
|---|---:|---:|---:|
| `if (` | 1.380 | 1.049 | 2.167 |
| `goto` | 3 | 0 | 0 |
| `return -` | 373 | 78 | 117 |
| `typedef struct` | 28 | 71 | 64 |
| Vorwaerts-Typedef-Muster | 2 | 0 | 0 |
| State-/Model-/Manager-Pointer-Muster | 76 | 338 | 189 |

Das sind keine semantischen Fehlerpfade, Besitzzaehlungen oder AST-Metriken.
Insbesondere zaehlt `if` auch normale Verzweigungen; Fehlerkonstanten und
Rueckgaben von Fehlerfunktionen werden durch `return -` nicht vollstaendig
erfasst. Die beiden scheinbar opaken Typedefs sind `node` und `attribute`;
beide werden noch im selben `html_engine.h` definiert. Es gibt somit keinen
Nachweis zweier wirklich opaker Implementierungstypen. Init-/Cleanup-
Namenshaeufigkeiten im JSON umfassen Deklarationen, Definitionen und Aufrufe;
auch Namen wie `nearest_free` und `dispatch_release` sind keine Freigaben.
Diese Kennzahlen duerfen spaeter nur nach identischen Regeln verglichen werden.

## Manueller Ownership- und Lifecycle-Befund

Die folgende Bestandsaufnahme benennt konkrete Lebenszyklen und deren
Fehlerbehandlung; sie ist keine kombinatorische Vollzaehlung aller Pfade.
Eine solche `cleanup_paths`-Zahl bleibt unbestimmt statt erfunden. Der spaetere
Pilot muss seine genaue semantische Vorher-/Nachher-Abgrenzung zusaetzlich
angeben. Die bestehenden C-Vertraege bleiben die Referenz.

| Bereich | Bestehender Lebenszyklus / Besitz | Cleanup und Fehlerzustand |
|---|---|---|
| `browser_response` | 0 besessene Handles/Heapobjekte, 0 Init/Destroy-Paare; Eingabebytes geliehen, Ergebnis besteht aus Werten und fester URL | Ergebnis zuerst nullgesetzt, spaetere Ablehnungen koennen bereits Status/Offsets enthalten. Rueckgabecode muss vor jeder Nutzung geprueft werden. C++-Nutzen waere gueltiger Ergebnistyp, kein erfundener Destruktor. |
| `browser_resources` | 1 Init/Reset-Operation, 0 Destroy-Operationen; inline Metadaten/1-MiB-Pool im Besitz des aufrufenden Workspace, keine eigene Datei-/Netz-/Allocatorautoritaet | Generation, dichter Ready-Praefix, exakte Offsets und Quoten. Unpack-Ausgabe muss bei Fehler verworfen werden. Typen sollen Publikationszustand kapseln; Reset gibt nicht selbst Heap frei. |
| `browser_model` | 0 besessene externe Ressourcen, 0 Init/Destroy-Paare; Layout, Bildslots und Scrollbar sind aufrufereigene Werte | Bounds-/Kapazitaetspruefungen und explizite Range-State-Konfiguration. Kein Anlass fuer eine polymorphe Objekt-Hierarchie. |
| Browser `main.c` | Workspace malloc/free; Surface create/destroy; Displaybuffer create/destroy; Fetch/CSS-IPC create/close; delegierten Endpoint release | Fruehe Admission-/Fontfehler geben Workspace und Surface zurueck. Navigation/Exit canceln das Kind; begrenztes Reaping geht der Kanalbereinigung voraus. Finaler Cleanup meldet verbliebene Kinder/Surface-/Bufferfehler. Crash-Reaping bleibt OS-Aufgabe. |
| Worker HTML/CSS | Hubbub parser create/destroy; retained document tree/release; LibCSS select context und Stylesheets create/destroy | Vier private Tree-Pools werden in document_release freigegeben und genullt. CSS bereinigt Node-Daten, Kontext, Sheets/Imports und erweiterten Tree auch bei Renderfehlern. Prozesslokale globale Zustandszeiger bleiben sichtbare Kapselungskandidaten. |
| GUI Controls/Container/Dialog/Menu/Tabs/Value/Text | Caller besitzt Models, Texte, Zustand, Callback-Kontext; initialize/configure/open sind nicht gleich Heapbesitz | Semantische Events statt eigener Eventloops. Fehler publizieren keinen zulaessigen neuen Zustand. Dialog open/cancel bzw. Benutzeraktionen sind fachliche Lebenszyklen, keine pauschalen Speicherfreigaben. |
| GUI Font/Piece Document | Font open_psf2 leiht Daten und Mappingarray; Piece Document leiht Original-Readcallback, besitzt inline Piece-/Added-Speicher | Kein Heap-/Dateibesitz, kein erforderliches destroy. Aufrufer muss geliehene Daten bis zur letzten Nutzung erhalten. |
| GUI Surface Client | create/destroy und buffer_create/buffer_destroy sind zwei Protokollpaare; init leiht delegierten Endpoint | destroy markiert disconnected nur nach erfolgreichem Senden. Bufferregistrierung ist nicht Freigabe des Kernel-Displaybuffers; die Anwendung bleibt fuer diesen verantwortlich. Bounded Deferred-Input-Queue verhindert blockierendes Gegeneinander beim Senden. |
| Compositor WM/Explorer/Drag | WM open/close, Explorer open/close und desktop_open/desktop_release; feste Arrays, Generationen, kein eigener Heap in den Controllern | Explorer staged Snapshots werden atomar publiziert; Fehler erhalten die alte Ansicht. Drag speichert generationgebundene Daten und berechtigt VFS-MOVE nicht durch LAYOUT. |
| Compositor Surface/Broker | Surface create/destroy, Buffer create/destroy; Broker reserve/bind/cancel und initialize/shutdown | Owner-PID plus Prozessgeneration, Surfacegeneration, Bounds und Messageversion werden vor Mutation geprueft. Shutdown schliesst Endpoints und prueft die Kindgeneration vor kill/wait; geliehene Buffer bleiben getrennt vom Eigentum. |
| Compositor VFS-/Desktopintegration | Bounded file open/close fuer Font, Layout, Shortcuts und File-Move; terminal acquire/release | Explizite Fristen, Close-Cleanup und gepruefte Publikation; persistente Move-/Trash-/Layout-Protokolle sind eigene Fehlerdomaenen, nicht Teil einer C++-Umbenennung. |

Die drei geplanten Piloten haben zusammen mit ihrem jeweiligen Header:

| Pilot | Physische Zeilen | Codezeilen | `if` | `return -` |
|---|---:|---:|---:|---:|
| response | 152 | 143 | 30 | 12 |
| resources | 232 | 215 | 47 | 33 |
| model | 359 | 341 | 79 | 11 |

Reihenfolge bleibt TASK-1001/1002, TASK-2001, response, resources, model.
RAII kommt nur bei tatsaechlichem Besitz zum Einsatz. C-Wire-/SDK-Grenzen,
Generationen, Quoten und der Ring-3-Fehlercontainment-Vertrag bleiben erhalten.

## Artefakte und Herkunft

| Artefakt | Dateibytes | MYPR-deklarierte Payloadbytes |
|---|---:|---:|
| BROWSER.PRG | 2.801.692 | 6.162.241 |
| HTMLWORK.PRG | 845.868 | 2.752.100 |
| DESKTOP.PRG | 753.688 | 8.278.156 |
| libreistgui.a | 431.500 | nicht anwendbar |

Payloadbytes sind das MYPR-Headerfeld, nicht gesamter RAM, Resident Set oder
privates Heapbudget. Vollstaendige SHA-256 stehen im Messdatensatz.
Die drei PRGs stimmen bytegenau mit dem bestehenden Release-SBOM und den
Dateien im tatsaechlich gebooteten FAT32-Image ueberein (beide FAT-Kopien
ebenfalls gleich). Nachweis: `cpp-baseline-artifact-provenance.log`.
Alle Produktions-/SDK-/Buildquellen sind gegen `cc3d0dcb` unveraendert.

Das vorhandene Hauptbuild meldet in `.windows-build-config.json` **vmware/vga**,
Zeitstempel 17:22:55; es ist kein neuer QEMU-Referenzbuild dieser Baseline.
Die neue Gastmessung bootet dieses BIOS-Image ausdruecklich in QEMU TCG,
Standard-VGA, 1 vCPU, 1.024 MiB RAM, ohne NIC und ohne sichtbares Fenster.
Der Runner verwendet `-snapshot`; der Imagehash blieb vor/nach der Probe
`828d36397badfcc6f3cdaecadfae358f9973a346a9dfea7eb077d01caa8e7f8c`.
Dieser Ganzimagehash stimmt nicht mit dem aelteren SBOM-Imagehash ueberein;
eine unveraenderte komplette Release-Scheibe wird deshalb nicht behauptet.
Die gezielte PRG-Herkunftspruefung ersetzt keine komplette Image-Neuabnahme.

R3.15-Referenzen und Browsergates werden als historische Nachweise des
unveraenderten Produktionscodes wiederverwendet, nicht als neue Messungen:
`20260906-170338-package-vmware-vga.log` (47 s),
`20260906-170510-package-qemu-vga.log` (49 s), acht Hostgruppen und die fuenf
`r315-runtime-{forms,public,input,browser,resources}.log` samt `.browser.log`.
Die jetzige Fontkatalog-Korrektur betrifft nur Lizenzmetadaten mit
[Originalnachweis](../../assets/fonts/README.md), nicht die unveraenderten
Font-/Lizenzpayloads oder ausfuehrbaren Programme. Das vorhandene Image
enthaelt noch den vorherigen Katalog; ein spaeterer Build uebernimmt ihn.

## Leistungsmessungen

Host: Windows 11 10.0.26200, AMD64, Zig 0.16.0 C11,
`-O2 -UNDEBUG -Wall -Wextra -Werror`, kein LTO. Pro Messung 200.000 Aufrufe,
fuenf frische Prozesse. QueryPerformanceCounter misst ausserhalb der Schleife;
alle Rueckgaben und Zielzustaende werden innerhalb der Schleife geprueft.

| Reale Operation, festes Szenario | Median ns/Aufruf |
|---|---:|
| browser_response_open_document: HTTP 200, UTF-8, 8-Byte-Body | 2.011,685 |
| GUI control_dispatch: Pointerbewegung innerhalb eines Buttons | 42,492 |
| desktop_wm_dispatch: Pointerbewegung innerhalb eines Fensters | 32,492 |

Dies sind Mikrobenchmarks inklusive Assertionkosten und keine Netzwerk-,
Seitenaufbau-, Eingabelatenz- oder Target-WCET-Zahlen. Ein spaeterer Vergleich
muss dieselben Quelldateien, Flags, Szenarien, Plattform und Lastbedingungen
verwenden; bei Hostwechsel ist eine neue gepaarte Referenz erforderlich.

Neue Gastprobe: `test-reist-runtime.ps1 -Mode runtime-desktop-metrics
-Target qemu -Video vga`, PASS, 31,485 s Hostlaufzeit. Autoritativer neuer
Messdatensatz `20260906-180136-runtime-desktop-metrics.log`, SHA-256
`7e075c0176f367752d9bcad864cb569a08bac08a03326683c4b11c25e3e85ffe`:

```text
DESKTOP_METRICS version=1 full_frames=1 full_total_ms=15 full_max_ms=15 dirty_frames=16 dirty_total_ms=260 dirty_max_ms=91 drag_frames=8 drag_total_ms=151 drag_max_ms=79 resize_frames=8 resize_total_ms=109 resize_max_ms=91 fallback_frames=0 damage_regions=17 damage_max=1 clock_errors=0 probe_errors=0
```

Das bestehende Gate schreibt diesen Datensatz erst nach gueltigen Metriken,
`DESKTOP_EXIT_OK` und Rueckkehr zur VGA-Shell. Kindstatus war 0; Prozess beendet.
Die zusaetzliche stdout-Umleitung `cpp-baseline-runtime.log` blieb unter dem
versteckten PowerShell-Kind leer und ist allein kein Nachweis. Der neue
timestampgebundene Guest-Messdatensatz ist erhalten. Keine Wiederholung nur
fuer eine schoenere stdout-Ausgabe. Keine VMware-Pointer- oder WCET-Abnahme;
die Maxima sind Beobachtungen fuer diese 17 Frames, keine garantierten Fristen.

## Regression und Reproduktion

Alle Logs liegen unter ignoriertem `build/codex-agent/`. Windows-Hosttests
nutzen `python scripts/measure_cpp_baseline.py --host-test` als Prefix fuer
dieselben Python-Argumente. Prozesslokale SetErrorMode-Vererbung ist mit einem
harmlosen Kind getestet; keine Registry-/globale WER-Aenderung. MinGW-Binpfad
liegt im lokalen PATH, Pillow 12.1.0 nur in `cpp-baseline-deps`/PYTHONPATH.

| Eingefrorener Host-/Messbefehl (nach `python`) | Ergebnis | Test-/Laufzeit | Log |
|---|---|---:|---|
| `test/test_cpp_baseline.py -v` | 5 PASS | 0,069 s | cpp-baseline-noninteractive-verified.log |
| `-m unittest discover -s test -p test_desktop_*source.py -v` | 81 PASS | 3,669 s | cpp-baseline-desktop-final.log |
| `-m unittest discover -s test -p test_gui_*source.py -v` | 75 PASS | 9,378 s | cpp-baseline-gui.log |
| `test/test_gui_font.py -v` | 8 PASS | 16,912 s | cpp-baseline-font-final.log |
| `scripts/measure_cpp_baseline.py --revision cc3d0dcb --artifacts build --output build/codex-agent/cpp-baseline.json` | PASS, 72 Dateien/5 Samples | 38,781 s | cpp-baseline-measure-final.log |

Keine Skips. Alte Fehlversuche bleiben erhalten: Desktop (veraltete Dragmaske
und nichtkonforme main-Umbenennung), Fonts (fehlendes Pillow und falsche
Lizenzpins), Benchmark (Zig hatte unter -O2 Assertions deaktiviert).
Die freigegebenen Reparaturen aendern keine Produktions-C/ASM-Datei und keine
Deadline. Der Benchmark erzwingt nun aktive Assertions mit -UNDEBUG und einem
Compilefehler, falls NDEBUG dennoch gesetzt ist.

Fuer eine spaetere Messung den gespeicherten Referenzdatensatz nicht
ueberschreiben: neue Commit-ID und neuen Outputpfad verwenden, zunaechst
gleichen Manifestumfang/Zaehler vergleichen und neue Dateien explizit ausweisen.
Keine C++-Kompatibilitaet, JS-/vollstaendige Webkompatibilitaet oder pauschale
Komplexitaetsreduktion aus dieser Baseline ableiten. Die naechste aktive
Umsetzung bleibt der getrennte SDK-/Runtime-Schnitt R3.16.
