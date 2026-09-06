# Userspace-SDK, Toolchain und Portabilitätsvertrag

Stand: 20. August 2026.

Dieses Dokument legt fest, wie REIST wiederverwendbare Userspace-Komponenten
entwickelt, veröffentlicht und dokumentiert. Ziel ist Quellportabilität durch
erneutes Kompilieren, nicht die Neuerfindung einer Toolchain und nicht die
vorschnelle Behauptung von POSIX-, Wayland- oder Binärkompatibilität.

## Standard-first-API-Vertrag

R3.12: `x86os_terminal_input(operation, pid, generation)` ist der abgenommene
versionierte Single-Terminal-Adapter auf Syscall 127 (24-Byte-Request).
ATTACH_CONSOLE registriert die Root-Shell; TRANSFER delegiert an ein lebendes
Kind; ACQUIRE_SERVICE ist dem ueberwachten Compositor vorbehalten; RELEASE
gibt die Eingabe zurueck; CHECK liefert 0 oder -EAGAIN ohne Tasteninhalt.
Bestehende Zeichenleser behalten ihre Signaturen. Vollstaendige Semantik,
Quoten und Abweichungen zu POSIX stehen im
[Terminalvertrag](TERMINAL_INPUT_OWNERSHIP_CONTRACT.md).

Jede wiederverwendbare Bibliothek beginnt beim nächstliegenden etablierten API-
und Datenmodellstandard. Namen, Frame-/Byte-Einheiten, Objektlebensdauer,
Zustandsübergänge, negative Fehlercodes und gewöhnliche Compiler-/Linker-
Integration bleiben vertraut, damit portable Software einen Adapter statt
einer Neuentwicklung benötigt. Beispiele sind POSIX-artige Datei- und
Prozessfehler, konventionelle `usr/include`- und `usr/lib`-Layouts,
Wayland-inspirierte Surface-Commits und ALSA-inspirierte PCM-Zustände sowie
frameorientierte Transfers.

Das ist architektonische Annäherung und keine automatische
Kompatibilitätsbehauptung. Header verwenden den `reist_`-Namensraum, bis
Quellkompatibilität vollständig implementiert und getestet ist. Kernel-ABIs
bleiben fest groß, versioniert und append-only. Jede bewusste Abweichung für
endliche Deadlines, Capability-Besitz, Prozessgenerationen oder
Fail-closed-Recovery ist in der Signatur sichtbar und direkt an der API
dokumentiert; Standardnamen werden nicht mit stillschweigend anderer Semantik
verwendet.

Damit bleibt REIST gegenüber gewachsenen Ökosystemen anschlussfähig, ohne deren
historische Implementierungslasten zum Kernelvertrag zu machen. Kompatible
Adapter dürfen verbreitete Quell-APIs abbilden; der native `reist_`-Kern bleibt
klein, explizit besessen, kapazitätsbegrenzt und mit endlichen Deadlines
versehen.

## Grundentscheidung

Verbindliche Nutzervorgabe vom 6. September 2026:
[selektiver C++-Migrationsplan](../REIST_CPP_MIGRATION_PLAN.md).
Zuerst wird TASK-0001 als `R3.16a-cpp-migration-baseline` committed;
`R3.16-ring3-cpp-runtime` liefert danach TASK-1001/1002 als opt-in C++20-
Profil ueber denselben Clang/LLD-/ELF32-/MYPR-Pfad. Noch nicht implementiert.
Bestehende C-Verbraucher behalten Startcode und Defaults. Pflichtflags sind
`-ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
-fno-use-cxa-atexit`. Globale dynamische Initialisierung, dynamische lokale
Statics, Exit-Registrierung und implizite Heapinitialisierung werden abgewiesen,
nicht als leere Stubs implementiert oder vom Linker lautlos verworfen.
Konstante Initialisierung ohne Laufzeitabhaengigkeit bleibt moeglich.
Automatische/ausdruecklich erzeugte Objekte nutzen `noexcept`-Destruktoren und
explizite Move-Semantik; Allocation ist explizit oder im Typvertrag dokumentiert.
Kein gehostetes STL, keine Exceptions/RTTI/Threads/TLS. Nothrow-OOM erhaelt den
alten Zustand; gewoehnliches new-OOM beendet im dokumentierten No-Exceptions-
Profil nur den Prozess. Bei Crash beweist generationstreues OS-Reaping die
Freigabe, nicht die Behauptung noch laufender Destruktoren.
Danach folgen die minimalen allokationsfreien `libreist++`-Typen (TASK-2001),
erst dann der Browserpilot. Dateien und unveraenderte bisherige Gastgates stehen
in `automation/reist-s03b.toml`. Der alte C++17-/Finalisierungstabellenentwurf
ist vor Implementierung durch die neue Nutzervorgabe ersetzt.

REIST übernimmt bewährte, frei verfügbare Buildbausteine, solange sie das
freistehende i386-Ziel korrekt unterstützen. Eigener Code beginnt erst an der
tatsächlichen Betriebssystemgrenze.

| Aufgabe | Verwendeter Standardbaustein | REIST-spezifischer Anteil |
|---|---|---|
| C-Frontend und Optimierung | Zig-Frontend für Clang/LLVM, C11 | freestanding i386-Zielprofil und geprüfte Flags |
| Assembler | NASM für BIOS-/Bootquellen; Clang Integrated Assembler für `.S`-Userspacequellen | hardwarespezifische Quellen, keine eigene Assemblersprache |
| Linker | LLD mit ELF32 | festes Userspace-Linkerskript und Speichergrenzen |
| Statische Bibliothek | gewöhnliches Unix-Archiv über `zig ar` | Auswahl der enthaltenen REIST-Objekte |
| Header-/Bibliothekssuche | `usr/include`, `usr/lib`, `-I`, `-L`, `-l` und pkg-config-Metadaten | REIST-Namensraum und API-Versionen |
| Zwischendarstellung | i386 ELF32 | feste Ladeadresse und streng validierte Segmente |
| ausführbare Zieldatei | — | kleine, validierte ELF32-zu-MYPR-v1-Verpackung |

Der MYPR-Konverter ist kein Compiler und kein Linker. Er akzeptiert nur das
bereits gelinkte, little-endian i386-ELF, verwirft Laufzeit-Relokationen,
prüft Segmente und Eintrittspunkt und erzeugt anschließend den vorhandenen
Loader-Container. Sobald REIST einen weiterentwickelten Prozesslader erhält,
wird diese Grenze als eigenes Arbeitspaket versioniert; Buildwerkzeuge werden
nicht stillschweigend geforkt.

## Schichten und Abhängigkeiten

```text
Anwendung
  -> öffentliche Komponenten-API (libreistgui oder libreistaudio)
     sowie formatunabhängige Codec-API (libreistimage)
     -> REIST-System-API (libreistos)
        -> versionierte Syscalls und begrenzte IPC-Protokolle
           -> Kernel und getrennte Ring-3-Systemdienste
```

Abhängigkeiten verlaufen nur nach unten. Eine allgemeine GUI-Komponente darf
weder `desktop_wm.h` noch globale Desktopkoordinaten, Fenster-Z-Order oder den
Framebuffer kennen. Der Compositor darf öffentliche Komponenten verwenden,
bleibt aber deren Host und nicht Teil ihrer Client-API.

Der aktuelle Stand trennt `crt0.o`, `libreistos.a`,
`libreistnetparse.a`, `libreistgui.a`, `libreistaudio.a` und
`libreistimage.a` als echte wiederverwendbare
Buildartefakte. Der Systemprogrammbuild kompiliert diese Module einmal und
linkt danach jedes PRG gegen dieselben Archive. Unabhängige PRGs werden mit
höchstens acht Buildworkern gebaut; der Standard nutzt bis zu acht verfügbare
logische CPUs und kann mit `--jobs` begrenzt werden. Sie teilen den
inhaltsadressierten globalen Zig-Cache, besitzen aber je Übersetzung einen
isolierten temporären lokalen Cache; Ergebnisprüfung und Ausgabe bleiben in
der festen Programmlistenfolge.
Jedes Archiv besitzt eine eigene Abhängigkeitsmenge und behält seinen
Zeitstempel, wenn nur eine andere Komponente geändert wurde. Systemprogramme
beobachten Core- und lokale Header. GUI- beziehungsweise Audioprogramme
beobachten zusätzlich nur die öffentlichen Header und das Archiv ihres Moduls.
Eine Änderung an GUI-Komponentencode oder `desktop.c` übersetzt daher
`libreistgui.a` sowie nur die tatsächlich von den geänderten Quellen oder
Headern abhängigen GUI-Programme. Unveränderte Audio-, Image- und
Consoleprogramme behalten ihre Artefakte.
Eine spätere feinere Zerlegung
der noch großen System-API oder eine dynamische Shared-Library-ABI bleibt ein
eigener, getesteter Schritt.

## API, ABI und Protokoll sind verschiedene Verträge

- Eine **Quell-API** sind Header, Namen und Semantik, gegen die ein Programm neu
  kompiliert wird. `<reist/gui/types.h>`, `<reist/gui/menu.h>`,
  `<reist/gui/dialog.h>`, `<reist/gui/control.h>`,
  `<reist/gui/container.h>`, `<reist/gui/tabs.h>` und
  `<reist/gui/value_controls.h>`, `<reist/gui/text_editor.h>`,
  `<reist/gui/file_dialog.h>`, `<reist/gui/surface.h>` und
  `<reist/gui/surface_client.h>` sind heute solche APIs. Dasselbe gilt für
  `<reist/audio.h>`, `<reist/audio_wave.h>` und `<reist/image.h>` in ihren
  jeweiligen Modulen.
- Eine **binäre ABI** fixiert zusätzlich Datendarstellung, Aufrufkonvention,
  Symbolauflösung und Lebenszyklus bereits kompilierter Objekte. Ohne
  dynamischen Loader behauptet REIST keine Shared-Library-ABI.
- Ein **IPC-Protokoll** überträgt validierte Nachrichten zwischen Prozessen.
  Die implementierte Surface-/Event-Grenze ist unabhängig von der in-process
  Control-Bibliothek versioniert, generationsgebunden und auf feste
  Nachrichtengrößen sowie Kapazitäten begrenzt.

Diese Trennung verhindert, dass interne Zeigerstrukturen versehentlich als
Prozessprotokoll veröffentlicht werden. Prozessübergreifende Strukturen dürfen
keine Zeiger enthalten und benötigen Version, exakte Größe, feste Kapazitäten,
Besitzer, Generation und definierte Fehlersemantik.

## Stabilitätsregeln öffentlicher C-APIs

1. Öffentliche Namen liegen unter `reist/` und tragen den Präfix `reist_`.
2. Jede erweiterbare Struktur beginnt mit `version` und `struct_size`; alle
   reservierten Felder müssen null sein.
3. Ownership und Lebensdauer jedes Zeigers werden im Header festgelegt.
4. Kapazitäten und Laufzeit sind begrenzt. Kein öffentliches Ereignis löst
   unbeschränkte Suche, Rekursion, Allocation oder Busy-Wait aus.
5. Koordinatensystem, Einheit und Randkonvention werden ausdrücklich genannt.
   GUI-Client-APIs verwenden lokale, halb offene Rechtecke.
6. Fehler werden vor extern sichtbaren Seiteneffekten erkannt. Ein fehlerhafter
   Zustand kann über eine dokumentierte Initialisierungsfunktion geschlossen
   zurückgesetzt werden.
7. Bestehende Felder und Bedeutungen werden nicht umgedeutet. Eine inkompatible
   Semantik erhält eine neue Version oder einen neuen Namen.
8. C-Header sind C++-include-fähig, ohne C++ zur Ziel- oder Kernelanforderung
   zu machen.

## Portabilitätsziel

Das kurzfristige Ziel ist **C11-Quellportabilität** für klar abgegrenzte
Bibliotheken und Anwendungen. Ein Port soll gewöhnlich aus diesen Arbeiten
bestehen:

1. vorhandenen Quellcode gegen die installierten Header kompilieren,
2. fehlende standardisierte Bibliotheksfunktionen inventarisieren,
3. kleine Plattformadapter an einer expliziten Grenze ergänzen,
4. Verhalten mit Host- und Gasttests vergleichen.

REIST erfindet keine Funktionen mit bekannten POSIX-Namen und abweichender
Bedeutung. Eine standardisierte Schnittstelle wird entweder semantisch samt
Fehlerfällen implementiert und getestet oder als nicht verfügbar dokumentiert.
Linux-spezifische Annahmen wie `/proc`, `mmap`-Framebuffer, unbegrenzte File
Descriptors oder Wayland-Sockets werden nicht durch Attrappen kaschiert.

Binärportabilität fremder ELF-Programme, vollständige libc-/POSIX-Konformität,
Wayland-Wire-Kompatibilität und dynamisches Laden sind ausdrücklich spätere,
separat nachzuweisende Ziele.

## Installiertes SDK

### Opt-in C-Speicher-/Byte-Laufzeit (R3.8)

`libreistc.a` ergänzt einen getesteten ISO-C11-Teilsatz: `malloc`, `calloc`,
`realloc`, `free`, `memcpy`, `memmove`, `memset`, `memcmp`, `memchr`, `strlen`,
`strcmp`, `strncmp`, `strchr` und `strrchr`. Standardheader liegen bewusst
unter `usr/include/reist/libc/`, nur ein zusätzliches `-I` aktiviert sie.
Der öffentliche Adapter `<reist/libc.h>` ist dort ebenfalls verfügbar.
`pkg-config reist-c` beschreibt Header und Archive; bestehende SDK-Verbraucher
und `libreisttls.a` werden nicht umgestellt.

Vor Allocation bindet `reist_libc_init` caller-owned, `max_align_t`-ausgerichteten
Speicher. Maximal 4 MiB und 4096 gleichzeitig lebende Objekte, eine Ausführungs-
und Allocation-Domäne je Prozess, keine Threads, IRQ-Nutzung oder Reentranz.
Die maximal 8312 Out-of-band-Deskriptoren bilden eine vor jeder Änderung
validierte, lückenlose Partition. Bereiche, Ausrichtung, Prüfwörter und
Live-Zähler werden ohne Dereferenzierung fremder Freigabezeiger geprüft.
Im alten Arena-Modus fuehrt Speicherwachstum nicht zu impliziten Syscalls.
R1.2c ergaenzt den expliziten versionierten Backingvertrag
`reist_libc_init_backing` sowie den SDK-Adapter `reist_libc_init_process(budget)`.
Bis zu 120 private Regionen wachsen bedarfsgerecht innerhalb des gewaehlt festen
Budgets (maximal 512 MiB). Der Prozessadapter verwendet 256-KiB-Wachstumsschritte
und die vorhandenen Heap-Syscalls. Vollstaendig leere Regionen gehen bei `free`
sofort an den Provider zurueck; Statistik-`capacity` meldet tatsaechliches
Backing, nicht reserviertes Budget. 4096 gleichzeitig lebende C-Objekte und
ein Ausfuehrungskontext bleiben die dokumentierte Metadatengrenze.
Der Kernel begrenzt privaten Heap zusaetzlich auf die Haelfte verwalteten RAM,
hoechstens 512 MiB, und bewahrt eine globale Frame-Reserve. MYPR und die
oeffentliche Syscall-ABI bleiben unveraendert. Der neue Modus ist mit den
R1.2c-Host- und QEMU-Gastnachweisen abgenommen; Details und Nachweisgrenzen im
[Prozessspeichervertrag](PRIVATE_PROCESS_MEMORY_CONTRACT.md).

Allocation-Fehler liefern NULL und POSIX-artiges `ENOMEM`; `calloc` prüft
Multiplikation, gescheitertes `realloc` erhält das alte Objekt. Bewusst gewählte
ISO-C11-Nullgrößenvariante: `malloc(0)`/`calloc(0,n)` liefern NULL,
`realloc(p,0)` gibt frei und liefert NULL. `free(NULL)` hat keine Wirkung.
Init eines gebundenen Heaps und Reset bei lebenden Objekten liefern `-EBUSY`.
`errno` ist pro Prozess, nicht Thread-local. Ungültige Ownership oder erkannte
Metadatenkorruption beendet nur den Prozess mit begrenzter Diagnose und Status 70.
REIST-Abweichung des `abort`-Adapters: endgültiger Prozess-Exit mit Status 134,
keine SIGABRT-Zustellung oder Signalhandler, da dieser Teilsatz keine Signale
implementiert. `assert` ist im Debugbuild aktiv und bei `NDEBUG` ohne Auswertung.
Keine vollständige Hosted-C-/POSIX- oder Signal-Kompatibilität wird behauptet.

Alle C-Zeiger bleiben gewöhnliche prozesslokale Zeiger: gleiche Adresse nach
Wiederverwendung ist nicht von einer alten Referenz unterscheidbar. Prüfwörter
erkennen Korruption, schützen aber nicht gegen absichtliche Manipulation im
selben Adressraum. Prozess-Reap und die neue Generation sind die eigentliche
OS-Isolationsgrenze. Byte-/Stringfunktionen behalten die normalen C-Vorbedingungen;
sie validieren keine untrusted IPC-Zeiger oder unbegrenzt terminierte Netztexte.

`libwapcaplet.a` enthält den unveränderten C-Code der MIT-lizenzierten
NetSurf-Bibliothek 0.4.3. Archiv und Lizenz sind gepinnt; nur das unbenutzte
`sys/types.h`-Include entfällt im generierten Header (siehe `third_party/README.md`).
Upstream-Aufrufer benötigen weiterhin begrenzte Stringlängen und balancierte
Referenzen; ein späterer Parser-/IPC-Adapter muss diese Grenzen selbst erzwingen.
`lwc_iterate_strings` gibt nach Freigabe aller Strings auch den Upstream-Kontext
frei, bevor der Heap zurückgesetzt wird. C-Locale-/ctype-Funktionen werden nicht
vorgetäuscht: Diese Abhängigkeit benötigt keine davon. Stdio, Dateistreams, DOM/CSS
und JavaScript bleiben außerhalb dieses Speicherschnitts.

R3.9 ergänzt `strncpy` und `bsearch` gemäß ISO C11 sowie C-Locale-`tolower`
und POSIX-`strncasecmp`. Die normalen C-Objekt-/Zeigervorbedingungen bleiben
erhalten; `bsearch` weist zusätzlich einen überlaufenden Größenbereich ab.
`ctype.h` und `strings.h` deklarieren nur diese tatsächlich implementierten
Teilsätze, keine vollständige Locale-/POSIX-Laufzeit. Der SDK-Build installiert
auch gepinnte `libparserutils.a`/`libhubbub.a`, Header, MIT-Lizenzen und
pkg-config-Dateien. Die Archive bleiben opt-in; allein HTMLWORK linkt diese
Parserabhängigkeiten. Host-Generatorvoraussetzungen: Perl und GNU gperf.
Der Parserprozess und sein privater geprüfter Dateiadapter sind in
`BROWSER_ENGINE_PORT_PLAN.md` beschrieben; keine Erweiterung des Kernel-ABI.

`scripts/build_user_sdk.py` erzeugt ein Sysroot mit konventionellem Aufbau:

```text
<sysroot>/usr/include/...
<sysroot>/usr/lib/crt0.o
<sysroot>/usr/lib/libreistos.a
<sysroot>/usr/lib/libreistnetparse.a
<sysroot>/usr/lib/libreistgui.a
<sysroot>/usr/lib/libreistaudio.a
<sysroot>/usr/lib/libreistimage.a
<sysroot>/usr/lib/pkgconfig/reist-gui.pc
<sysroot>/usr/lib/pkgconfig/reist-audio.pc
<sysroot>/usr/lib/pkgconfig/reist-image.pc
```

Ein Client verwendet normale Suchoptionen:

```powershell
python scripts/build_user_program.py userspace/gui/examples/menu_controller.c `
  --output build/programs/MENUDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/dialog_controller.c `
  --output build/programs/DIALOGDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/programs/audioinfo.c `
  --output build/programs/AUDIOINFO.PRG `
  --sysroot build/sdk -l reistaudio
```

`--sysroot` wählt installierte Header, Startobjekt und Basisbibliothek aus.
Zusätzliche Suchpfade und Archive verwenden weiterhin die konventionellen
Optionen `-I`, `-L` und `-l`. Der Desktop selbst wird über denselben
Bibliotheksweg gebaut. Damit verhindert der Build, dass nur ein Beispiel
funktioniert, während das reale System eine private Kopie derselben Logik
verwendet.

Die ausführbare Referenzanwendung `GUIDEMO.PRG` wird vom Systemprogrammbuild
gegen dasselbe installierte GUI-Archiv gelinkt und unter
`/usr/gui/bin/guidemo.prg` paketiert. Sie zeigt alle derzeit freigegebenen
Menü- und Dialogfunktionen, ohne private Compositorheader einzubinden.

## Dokumentationsvertrag

Öffentliche Header sind die normative API-Referenz und verwenden
Doxygen-kompatible Kommentare. Sie dokumentieren mindestens:

- Zweck und Nicht-Ziele des Moduls,
- Ownership, Lebensdauer und Mutierbarkeit,
- Versionen, Größen, Flags und reservierte Felder,
- Einheiten, Koordinaten und Kapazitäten,
- Seiteneffekte, Capture-, Fokus- und Fehlerverhalten,
- Parameter, Rückgabewerte und Wiederherstellung nach Fehlern.

Implementierungen erläutern Validierungsgrenzen, Zustandsautomaten und
nicht-offensichtliche Sicherheitsentscheidungen. Kommentare wiederholen keine
trivialen Zuweisungen. Zu jedem öffentlichen Modul gehören ein kleines,
kompilierbares Beispiel, ein Host-Verhaltenstest und mindestens ein Buildtest
gegen das installierte Sysroot.

Architekturdokumente halten zusätzlich Motivation, verworfene Alternativen,
bewusste Standardabweichungen und reproduzierbare Nachweise fest. Diese Form
ist zugleich die Quellenbasis für das geplante Buch **Writing a Graphical
Operating System from Scratch**; das Buch ersetzt jedoch niemals den
versionierten Vertrag im Repository.

## Definition of Done für ein neues SDK-Modul

R3.10 ergänzt `abs` (ISO C11; nur wenn der positive Wert als int darstellbar
ist) und `strdup` (POSIX, Allocation aus derselben begrenzten Prozessarena,
NULL/ENOMEM bei Erschöpfung). Es gibt keine globale Host-libc-Allokation.
Das opt-in SDK installiert LibCSS 0.9.2 samt MIT-Lizenz, Headern und pkg-config-
Abhängigkeiten auf LibParserUtils und LibWapcaplet. Standard-i386-Arithmetik-
Builtins der ausgewählten Zig-Toolchain stehen separat in
`libclang_rt.builtins-i386.a`; CSS linkt sie für 64-Bit-Zwischenwerte. Das fügt
keine neue OS- oder Shared-Library-ABI hinzu. Der CSS-Adapter bleibt privat im
Browser-Worker; seine Grenzen stehen in BROWSER_ENGINE_PORT_PLAN.md.

R3.11 verwendet denselben privaten R1.2c-Prozessprovider fuer HTMLWORK mit
32 MiB explizitem Heapbudget, sobald ein validiertes Stylesheetbundle vorliegt.
Legacy-Auftraege behalten die 4-MiB-Arena. Keine neuen Syscalls, allgemeinen
C-String-Stubs oder versteckten VFS-/Netzwerkcallbacks. Das private CSS2-Format
und seine Prozess-/Navigationgenerationen sind kein oeffentliches SDK-ABI.

R3.13 linkt im Browser dieselbe opt-in C-Byte-/Stringlaufzeit, damit Formulare
keine zweite Implementierung von memcpy/memmove/strcmp benoetigen. Browser-
und Decoderallokationen behalten ihren privaten Prozessprovider bzw. ihre
feste Decoderarena; das Einbinden der Bytefunktionen aktiviert keinen Heap.
Das private CSS3-Szenenformat ergaenzt validierte Formulare, Controls, Optionen
und Stringoffsets. Grenzen und Abnahme: `BROWSER_FORM_INTERACTION_CONTRACT.md`.

R3.14 abgenommen: Der additive caller-owned URL-Arbeitsbereich und
`reist_html_url_resolve_wide` erweitern nur den Resolver auf 8192 Byte;
der bisherige Resolver und die semantischen HTML-Strukturen bleiben bei ihren
bisherigen Grenzen. Keine implizite Allocation oder neue Transportautoritaet.
Die bestehende Spawn-ABI erhaelt begrenzte 8193-Byte-Argumentstrings einschliesslich
NUL und hoechstens 16 KiB fuer den gesamten Initial-Argumentframe, bei weiterhin
16 oeffentlichen Argumenten und mindestens 16 KiB freier Laufzeit-Stackreserve.
E2BIG/EFAULT/ENOMEM und Rollback vor Publikation stehen im
`PROCESS_ARGUMENT_CONTRACT.md`; Host-/Gastnachweise stehen in CURRENT_WORK.
Auch der Legacy-HTML-Worker nutzt jetzt den bestehenden demand-backed Provider
mit unveraendertem 4-MiB-Budget; das caller-owned Arena-API bleibt erhalten.
Damit wird keine unbenutzte 4-MiB-Arena mehr in jedes Workerimage eingebaut.

- [ ] Öffentlicher Namensraum, Layer und Owner sind festgelegt.
- [ ] Vorhandener Standard oder etablierte Bibliothekssemantik wurde geprüft.
- [ ] Abweichungen sind begründet; es wird keine falsche Kompatibilität behauptet.
- [ ] Header dokumentiert alle öffentlichen Felder und Funktionen inline.
- [ ] Implementierung ist freestanding, kapazitätsbegrenzt und heap-frei oder
      besitzt einen ausdrücklich begrenzten Allocatorvertrag.
- [ ] Build erzeugt ein gewöhnliches statisches Archiv im SDK-Sysroot.
- [ ] Ein Beispiel inkludiert ausschließlich installierte öffentliche Header.
- [ ] Host-Verhaltenstest, Sysroot-Buildtest und relevanter Gasttest bestehen.
- [ ] Architektur- und Workflow-Dokumentation stimmen mit dem Code überein.
