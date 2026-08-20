# Userspace-SDK, Toolchain und Portabilitätsvertrag

Stand: 20. August 2026.

Dieses Dokument legt fest, wie REIST wiederverwendbare Userspace-Komponenten
entwickelt, veröffentlicht und dokumentiert. Ziel ist Quellportabilität durch
erneutes Kompilieren, nicht die Neuerfindung einer Toolchain und nicht die
vorschnelle Behauptung von POSIX-, Wayland- oder Binärkompatibilität.

## Standard-first-API-Vertrag

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
Eine Änderung an GUI-Komponentencode oder
`desktop.c` übersetzt daher ausschließlich `libreistgui.a` sowie die
tatsächlich abhängigen `DESKTOP.PRG`/`GUIDEMO.PRG`.
Eine spätere feinere Zerlegung
der noch großen System-API oder eine dynamische Shared-Library-ABI bleibt ein
eigener, getesteter Schritt.

## API, ABI und Protokoll sind verschiedene Verträge

- Eine **Quell-API** sind Header, Namen und Semantik, gegen die ein Programm neu
  kompiliert wird. `<reist/gui/types.h>`, `<reist/gui/menu.h>`,
  `<reist/gui/dialog.h>`, `<reist/gui/control.h>`,
  `<reist/gui/container.h>`, `<reist/gui/tabs.h>` und
  `<reist/gui/value_controls.h>` sind heute solche APIs.
- Eine **binäre ABI** fixiert zusätzlich Datendarstellung, Aufrufkonvention,
  Symbolauflösung und Lebenszyklus bereits kompilierter Objekte. Ohne
  dynamischen Loader behauptet REIST keine Shared-Library-ABI.
- Ein **IPC-Protokoll** überträgt validierte Nachrichten zwischen Prozessen.
  Die geplante Surface-/Event-Grenze wird unabhängig von der in-process
  Control-Bibliothek versioniert und generationsgebunden.

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
