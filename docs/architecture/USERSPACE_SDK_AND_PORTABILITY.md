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
TASK-0001 ist als `R3.16a-cpp-migration-baseline` abgenommen;
[Baseline und Einschraenkungen](../development/CPP_MIGRATION_BASELINE.md).
`R3.16-ring3-cpp-runtime` liefert danach TASK-1001/1002 als opt-in C++20-
Profil ueber denselben Clang/LLD-/ELF32-/MYPR-Pfad; abgenommen in `478289b7`.
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

Planrevision 1.1 ist ausdruecklich freigegeben. Sie veraendert weder Profil 1
noch die eingefrorenen R3.17-Gates: Die sechs Hilfstypen bleiben allokationsfrei,
kein STL-Nachbau. Spaetere grosse Payloads nutzen ausdruecklich allokierte und
budgetierte private Puffer statt pauschal kleiner Inline-Container. Eine
geliehene Ansicht ist kein Lebensdauer- oder Autoritaetsnachweis.
Fehlschlagende Erwerbsoperationen liefern einen Ergebnistyp mit Fehlerursache;
Destruktoren sind nur begrenzter Cleanup, keine implizite Flush-/Stop-/Reap-
Zustandsmaschine. Release-Adapter muessen Fehler und verbleibenden Besitz
ausdruecklich definieren. OS-Fencing und generationstreues Reaping bleiben
unveraendert. Die Beispiele im Migrationsplan sind keine implementierten SDK-APIs.

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

## Opt-in C++20-Profil 1 (R3.16)

FP-Voraussetzung, R1.3-Kandidat vom 7. September (noch nicht abgenommen):
Der i386-Compiler verwendet fuer `double` bereits x87. Die neue kernelprivate
Kontextisolation bewahrt x87/MMX/SSE-Daten und Controls generationsgebunden
bei Taskwechseln. Frische Prozesse erhalten leere genullte Register,
x87-Control `0x037f` und MXCSR `0x1f80`; Fault/Kill/Exit behalten das normale
Prozess-Reaping. Keine neue Startkonvention, oeffentliche ABI, Math/libc-
Implementierung oder AVX-/XSAVE-/JavaScript-Freigabe. Der
[FPU-Vertrag](FPU_CONTEXT_ISOLATION_CONTRACT.md) trennt Registerisolation
von noch zu portierender Ring-3-Laufzeit und verpflichtender Gastabnahme.

TASK-1001/1002 verwendet den vorhandenen Clang/LLVM-i386-Pfad, ELF32,
gewoehnliche statische Archive und unveraendertes MYPR v1. Referenzen sind
ISO C++20 (Objektlebensdauer, Placement-/nothrow-/aligned-new), die
[Itanium C++ ABI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html) fuer
Clangs Namensbildung/Array-Cookies und die
[Clang-Diagnostik](https://clang.llvm.org/docs/DiagnosticsReference.html)
fuer `-Wglobal-constructors` und `-Wexit-time-destructors`.
Das ist ein versioniertes freestanding Teilprofil, keine hosted C++-/STL-,
POSIX- oder fremde Binaerkompatibilitaetszusage.

`build_user_sdk.py` installiert additiv `usr/lib/libreistcpp.a`,
`usr/include/reist/cpp/new`, `usr/include/reist/cpp/reist/cpp.h` und
`reist-cpp.pc`. C-Programme bekommen weder diese Include-Pfade noch das Archiv
automatisch. `build_user_program.py --cpp --sysroot build/sdk` aktiviert
C++20, `-ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
-fno-use-cxa-atexit -nostdinc++` sowie beide genannten Diagnosen als Fehler.
Gemischte `.c`-/`.cpp`-/Assemblerquellen werden getrennt uebersetzt.
`reist-cpp.pc` beschreibt Includes/Flags/Archive; fuer dieselbe Zulassungspruefung
ist weiterhin der PRG-Builder erforderlich, pkg-config allein ist kein Gate.

Der vorhandene C-Startcode bleibt unveraendert: Einstieg ist ausdruecklich
`extern "C" int main(int argc, char **argv)`. Das ist die dokumentierte
freestanding-Startkonvention, keine neue C++-ABI ueber Prozessgrenzen.
`x86os.h` kapselt Funktionen fuer C++ mit C-Linkage; Strukturfelder,
Groessen, Syscalls, alte Wrapper und MYPR-Grenzen bleiben unveraendert.

Vor Linker-GC, Strip und DISCARD werden alle ELF-Objekte und alle Mitglieder
jedes Archivs untersucht, auch unreferenzierte. Nichtleere Init-/Fini-Arrays,
Konstruktor-/Destruktortabellen, Unwind-/Exceptionabschnitte und TLS werden
abgewiesen; Symbolpruefung erkennt insbesondere lokale dynamische Statics
(`_ZGV` auch bei abgeschalteten thread-safe Guards), RTTI, Exit-Registrierung,
Exception- und Threadlaufzeit. Binaereingaben sind auf 64 MiB, 4096 Abschnitte
bzw. Archivmitglieder und 65536 Symbole je Tabelle begrenzt. Thin Archives,
Bitcode/LTO und Nicht-i386-ELF sind nicht zugelassen. Zusaetzlich wird das
gelinkte ELF vor der bisherigen MYPR-Segmentpruefung kontrolliert.
Zeitstempel allein ueberspringen diese Pruefung bei C++ nicht.
Dies verhindert unbeabsichtigte Laufzeitabhaengigkeiten, ist aber keine
Sicherheitsgrenze gegen absichtlich handgeschriebenen Maschinencode:
Speicher-/Autoritaetsisolation bleiben Aufgabe des Ring-3-Loaders und Kernels.

Konstante Initialisierung, automatische Objekte, `noexcept`-Destruktoren,
explizite Move-Uebergabe, virtuelle Rollen ohne RTTI und Placement-new sind
moeglich. Globale dynamische Initialisierung, dynamische lokale Statics,
Exit-Registrierung, Exceptions und TLS sind nicht Teil dieses Profils.
Es gibt keine Konstruktorliste, keinen automatischen Heapstart und keinen
globalen Garbage Collector. Die TASK-2001-Hilfstypen sind unten getrennt
beschrieben; sie aendern weder dieses Runtime-Profil noch den C-Allocator.

Vor dynamischen Allokationen initialisiert die Anwendung ausdruecklich
`reist_libc_init_process(budget)` oder einen vorhandenen caller-owned Provider.
`new`/`new[]` und alle Delete-Varianten nutzen ausschliesslich diesen bereits
begrenzten privaten C-Allocator. Ohne Initialisierung oder bei Erschoepfung
liefert nothrow-new NULL; bereits lebende Objekte bleiben unveraendert.
Null-Delete ist wirkungslos. Zero-new fordert mindestens ein Byte an;
Array-Cookies verwaltet der Compiler. Aligned-new prueft Zweierpotenz,
Groessenueberlauf und Platz fuer einen privaten Rohzeiger-Praefix vor malloc.
Aligned-delete gibt exakt dessen urspruengliche Allocation zurueck. Ungueltige
Delete-Zeiger bleiben C++-UB; das Profil verspricht weder Schutz vor gefaelschten
Zeigern im selben Prozess noch Destruktoren nach einem Prozessabsturz.

Explizite Profilabweichung: Gewoehnliches new wirft bei Erschoepfung kein
`std::bad_alloc`, sondern beendet nur den aufrufenden Prozess mit Status 71,
ohne Unwinding. Pure-/deleted-virtual-Aufrufe beenden ihn mit Status 72;
keine erfolgreichen ABI-Stubs. Wiederherstellbare Aufrufer verwenden
`new(std::nothrow)`. Normale Destruktion/free gibt leere Backingregionen zurueck;
bei Fault, Kill oder fatalem new greift unveraendert das generationsgebundene
OS-Reaping. Kein neuer Heap-Syscall, kein Reserveabbau, kein Ring-0-C++.

`/usr/bin/cpptest.prg` ist in Windows- und Makefile-Layouts ueber `cpptest`
aus der normalen Userspace-Shell erreichbar. Hosttests pruefen echte Runtime
und echten C-Allocator, C-Linkage/Layout, Verbotserkennung sowie den externen
SDK-Build. Das `cpp-client`-Gastgate prueft normale Objekt-/Arraylebensdauer,
exakte Frame-Rueckgabe, OOM/Fault/Kill eines Kindes, Parent-Canary, frisches
Kind und anschliessende Shellbedienung unter endlichen Fristen.

## Allokationsfreie C++-Hilfstypen, Adapterprofil 1 (R3.17)

Die Header unter `usr/include/reist/cpp/reist/` werden ueber den bestehenden
SDK-Kopierer installiert; es gibt kein weiteres Runtime-Archiv. Referenzen sind
die C++-Objektlebensdauer und die Wert-/View-/Unique-Owner-Modelle von
[optional](https://eel.is/c++draft/optional),
[expected](https://eel.is/c++draft/expected),
[span](https://eel.is/c++draft/views.span) und
[unique_ptr](https://eel.is/c++draft/unique.ptr).
Dies sind explizite `reist`-Adapter im C++20-Teilprofil, kein `std`-Ersatz und
keine Behauptung eines C++23-/spaeteren Standardbibliotheksumfangs.

| Header / Typ | Vertrag und bewusste Einschraenkung |
|---|---|
| `result.h`: `Result<T,E>`, `Result<void,E>` | Nur `success(...)`/`failure(...)` erzeugen das Ergebnis; genau eine Alternative lebt, bei void-Erfolg keine Payload. `value_if()`/`error_if()` liefern null fuer die falsche Alternative. |
| `optional.h`: `Optional<T>` | Leer ohne T-Konstruktion. `try_emplace` konstruiert nur im leeren Zustand und lehnt andernfalls vor Argumentverbrauch ab. `reset` ist idempotent. `get()` ist ein gepruefter Borrow. |
| `span.h`: `Span<T>` | Geliehener Arraybereich mit const-korrekter Konversion, ohne Derived-Array-zu-Base-Array-Konversion. `try_from` prueft Null/Laenge, Alignment und Byte-/Adressueberlauf; `at` und `subspan` pruefen Bounds. Ablehnung laesst den Ausgabebereich unveraendert. |
| `fixed_string.h`: `FixedString<N>` | N Nutzbytes plus NUL; `assign`/`append` pruefen Platz vor Mutation und unterstuetzen Ueberlappung. Array-Overloads verlangen abschliessendes NUL, Span-Inputs erlauben eingebettete NUL. Laenge ist in Bytes, nicht Unicode-Zeichen. |
| `fixed_vector.h`: `FixedVector<T,N>` | N einzeln ausgerichtete Objektslots, nur der belegte Praefix lebt. `try_emplace_back` prueft Platz vor Konstruktion/Move. `pop_back`/`clear` zerstoeren rueckwaerts. Geprueftes `at`, kein Array-/Contiguous-Iterator-/`data()`-Versprechen ueber Union-Slots. N=0 ist zulaessig. |
| `unique_handle.h`: `UniqueHandle<T,Traits>` | Move-only Besitz eines bereits erworbenen trivialen C-Handles. Traits definieren `invalid`, `is_valid`, `equal`, `close` explizit und noexcept. `equal` muss die ganze Identitaet einschliesslich Generation vergleichen. `release` uebertraegt Verantwortung; `reset` schliesst den alten gueltigen Besitz genau einmal, gleiche Identitaet bleibt unveraendert. Kein Default-Policy fuer rohe Handles. |
| `utility.h` | Nur notwendige Move/Forward-/Objektslot- und Clang-Profiltraits; keine allgemeine Metaprogrammierungsbibliothek. |

Besessene T/E sind unqualifizierte Nicht-Array-Objekte mit noexcept-Destruktor.
Konstruktion/Copy/Move ist nur fuer passende nothrow-Konstruktoren verfuegbar;
Move-only Payloads bleiben nicht kopierbar. Optional/Result-Moves erhalten den
Quelldiskriminator und hinterlassen dessen Payload moved-from. Vector-/String-
Moves leeren die Quelle; UniqueHandle-Moves entziehen ihr den Besitz.
Selbstzuweisung/Self-Move ist wirkungslos. Zuweisung von Optional/Result/Vector
rekonstruiert die Payload statt einen Zuweisungsoperator von T/E vorauszusetzen.

Borrow-Zugriffe von temporaeren Besitzern sind abgewiesen. Zeiger/Views bleiben
nur bei lebendem Besitzer und gueltigem Zustand benutzbar; Reset, Entfernung,
Zuweisung, Move oder Navigation koennen sie invalidieren. Span selbst prueft
keine Seitentabellen oder Objektlebensdauer. Echte Arrayausdehnung, gegenseitige
Lebensdauer und bestehende Autoritaet bleiben Aufruferpflicht; numerisch
gueltige fremde Zeiger werden dadurch nicht sicher. Keine Thread-/Reentry-Zusage.

Kein Helper allokiert selbst Heap. Payload-Konstruktoren/-Destruktoren und
Release-Traits muessen ebenfalls ihren dokumentierten begrenzten Vertrag
erfuellen; `noexcept` beweist weder Heapfreiheit noch Terminierung beliebigen
Nutzercodes. Handle-close konsumiert Cleanup-Verantwortung auch auf seinem
ausdruecklich definierten Fehler-/Fencingpfad; der Wrapper ignoriert keinen
Rueckgabefehler und erfindet weder Queue noch Retry. Er ersetzt keine fallible
Flush-/Stop-/Reap-Schnittstelle oder OS-Crashbereinigung. Doppelte Adoption
und gefaelschte Handles bleiben Vertragsverletzungen; der Kernel validiert
weiterhin die eigentliche Autoritaet. Die Typen vergroessern keine RAM-Quote
und erzwingen keine kleinen Inline-Puffer fuer spaetere grosse Browserpayloads.

Verifikation: dieselben realen Templates mit nichttrivialen/move-only und
64-Byte-ausgerichteten Werten in O0/O2-Hosttests; Kapazitaets-/Ueberlauf-/Alias-
und 4096 deterministische Zustandswechsel, gezaehlte Freigaben einschliesslich
voller Handleidentitaet und modelliertem Fencing. i386-Objektsymbole und Link
ohne Allocator/C++-Runtime (nur C-Byteprimitiven), externe installierte SDK-
Integration und CPPTEST mit echten IPC-Endpunkten, altem Handle und neuem
Endpunkt. Alle bisherigen OOM/Fault/Kill/Reap-/Shellmarker bleiben erforderlich.
Aktueller Abnahmestatus und Logs stehen in CURRENT_WORK; keine Browsermigration.

## Abgenommener Response-Pilot R3.18

R3.18 verwendet die bereits akzeptierten Hilfstypen erstmals im Browser:
`browser_response.cpp` mit privatem `browser_response.hpp`. Das ist kein
neues oeffentliches SDK-/Wire-API. Die benannte Factory liefert ausschliesslich
vollstaendig validierte Metadaten als Erfolg; Fehler tragen getrennt die
bisherigen C-Diagnosen. Die drei bestehenden C-Funktionen und Datenlayouts
bleiben unveraendert. Keine Response-Heapallokation oder gespeicherten
Bodyzeiger; Metadatenkopien sind auf Result-Konstruktion und C-Ausgabe begrenzt.
Feste Parser-/Metadaten-Scratchfelder liegen konstant initialisiert in der
privaten BSS und zaehlen zum Loader-Payloadbudget. Die bereits vom URL-Resolver
geforderte Serialisierung bleibt Voraussetzung; zurueckgegebene Werte sind
unabhaengige Kopien. So liegen keine zusaetzlichen grossen Parser-Scratchfelder
auf dem weiterhin 32-KiB-grossen bewachten Userspace-Stack. Kein Schutzabbau.
88 Hosttests, beide Referenzbuilds und alle fuenf Gastgates bestehen.
Stack-/Laufzeit-/Groessennachweise und das verbleibende intermittierende
Worker-Timingrisiko stehen in CURRENT_WORK. Profil 1 wird nicht gelockert.

## Abgenommener Modell-Pilot R3.20: private Werte hinter der C-Grenze

Die sechs bisherigen Modellfunktionen werden aus einer einzigen
`browser_model.cpp` gebaut; `browser_model.h` behaelt alle C-Layouts und
Signaturen mit expliziter C-Linkage. Parser und native Range-Controls bleiben
C. `main.c`, dessen Navigations-/Workerbesitz, private Speicherbudgets und
das eingefrorene UI-Messverfahren bleiben unveraendert.

Die privaten ISO-C++20-Profil-1-Adapter in `browser_model.hpp` kapseln:

- `AddressEdit`: erst nach geprueften Zeigern und `cursor <= length < capacity`
  zulaessiger kurzlebiger Borrow fuer genau eine serialisierte Bearbeitung;
- `TextRange`: ueberlauffrei gepruefter Offset-/Laengenwert im bereits
  zugelassenen Dokument, ohne Textkopie oder neue Unicode-/Layoutalgorithmen;
- `ScrollExtent`: gemeinsame Normalisierung der bestehenden View-/Dokument-
  und Positionsgrenzen, ohne vergroesserte Kapazitaet oder neue Scrollpolicy.

Result/Werte bleiben jeweils <=64 Bytes. Keine Heap-/Handleownership,
virtuelle Hierarchie, Runtimeinitialisierung oder Cleanup-Operation wird
erfunden. Externe Mutation, Navigation oder Freigabe invalidiert Adress-Borrows;
numerische Bereichspruefung beweist weder Speicherlebensdauer noch Autoritaet.
Die standardmaessigen C++-Wert-/View-/Result-Semantiken entsprechen den oben
dokumentierten privaten `reist`-Adaptern, nicht einem neuen SDK-/Wirestandard.

Ein messbar unnoetiger Methodenaufruf materialisierte den kurzlebigen Borrow
im Eingabepfad. Das lokale Clang-Attribut `gnu::always_inline` beseitigt diesen
Aufruf, ohne globale Compilerflags, ABI, Fehlerpruefungen oder Testgrenzen zu
aendern. Es ist kein allgemeiner Durchsatz-/WCET-Claim. Typisierte Zulassung
ist der Nutzen; bestehende manuelle Cleanup-Pfade werden hier nicht reduziert,
weil das Modul keine externen Ressourcen besitzt.

Der aktuelle CSS-Pfad projiziert bereits Szenengeometrie in C statt den
alten `browser_build_layout` aufzurufen. Dessen sechster C-Einstieg bleibt
kompatibel und wird gegen den Original-C-Oracle geprueft; es wird kein neuer
Produktionsaufruf oder zweiter Layoutalgorithmus fuer die Migration erfunden.
Die anderen fuenf Funktionen bleiben im echten Browserpfad.

93 Hostfaelle, beide Referenzbuilds und alle sieben Gastgates bestehen.
Datei-/Loadergroessen bleiben gleich, Stackdelta maximal 12 Bytes. Der
gepaarte QEMU-Gast misst C/C++-p95 60.0902/60.4288 ms beim Tippen und
114.6334/125.0032 ms beim Scrollen; alle absoluten und relativen Grenzen
bleiben eingehalten. Rohdaten und Abnahmezuordnung stehen in CURRENT_WORK. Der
Stacknachweis untersucht echte i386-Objekte mit unveraenderten Profilflags.
Compiler-Switches gelten nur nach Bounds-/Relokationspruefung aller Ziele
innerhalb derselben Funktion als lokale Spruenge ohne Stackkante. Unbekannte
oder indirekte Aufrufe, externe Sprungziele und Rekursion bleiben abgewiesen;
mutierte Sprungtabellen muessen scheitern. Keine abgesenkten Laufzeit-,
Stack-, Binaer-, UI- oder Resilienzgrenzen.

## Abgenommener Ressourcen-Pilot R3.19

Browser und HTMLWORK verwenden dieselbe `browser_resources.cpp`-Admission
hinter den elf unveraenderten C-Funktionen. Die private `ValidatedResources`-
Factory liefert einen geprueften geliehenen Snapshot oder einen Fehler.
Pack serialisiert nur einen zugelassenen Snapshot; Unpack behaelt beide alten
Wireprofile und muss vor Erfolg erneut validieren. Fehlgeschlagene C-Ausgabe
bleibt verwerfbare Teilausgabe, kein typisierter Erfolg.

Die Ansicht haelt nur einen const-Zeiger, keinen Bundle-/Payloadbesitz und
keine Datei-, Netzwerk- oder Prozessautoritaet. Ihre geprueften Entry-/Byte-
Zugriffe bleiben an lebenden, unveraenderten Besitzerspeicher gebunden.
Reset, Mutation, Navigation oder Freigabe invalidieren alle abgeleiteten
Ansichten; Generationspruefungen ersetzen diese Lebensdauerpflicht nicht.
Keine Whole-Bundle-Kopie, neue Allokation oder Destruktorregistrierung.
Der bestehende serialisierte URL-Scratch-Vertrag bleibt erhalten.
Auch die bestehende Zig-Compilerhilfsbibliothek wird im SDK mit
`-fno-unwind-tables` erzeugt. Das passt ihre Erzeugung an das bereits geltende
Profil an; keine nachtraegliche Abschnittsentfernung oder Ausnahme von der
vollstaendigen Archiv-Admission. Der externe SDK-Test prueft alle Mitglieder
der sechs HTMLWORK-Abhaengigkeiten, einschliesslich Compilerhilfen.
Alle quantitativen Grenzen und die Browser-/Worker-Gastabnahme stehen in der
Queue; 89 Hosttests, beide Referenzen und alle fuenf Gastgates bestehen.
Die gezielte Ladewartekorrektur wartet nur bei noch nicht ausfuehrbarer Arbeit
auf einen Timer; sie aendert keine Frist oder Kind-/IPC-Grenze.
Ergebnisse, Messdaten und verbleibende Risiken siehe CURRENT_WORK.

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
