# Externe Programme für die Kernel-Shell bauen

Stand: 20. August 2026.

Das Projekt enthält eine native Windows-Toolchain, die fremden C-Quelltext in
das ausführbare `MYPR`-Format übersetzt. WSL, GRUB und ein Cross-GCC werden
nicht benötigt; der Build verwendet Zig/Clang, LLD und den Python-Packer aus
diesem Repository.

Compiler, Assembler, Linker und statisches Archivformat sind unveränderte
Upstream-Werkzeuge. Der Python-Anteil validiert und verpackt ausschließlich das
fertig gelinkte ELF32 in den vorhandenen MYPR-v1-Container. Der vollständige
Schichten- und Portabilitätsvertrag steht unter
[Userspace-SDK, Toolchain und Portabilität](../architecture/USERSPACE_SDK_AND_PORTABILITY.md).

## Schnelltest unter Windows

Eine einzelne Programmdatei bauen:

```powershell
.\scripts\build-program-windows.ps1 `
  -Source C:\Projekte\hello.c `
  -Output .\build\programs\HELLO.PRG
```

Das Programm direkt in eine neue bootfähige VMware-VM einbauen:

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests `
  -ProgramSource C:\Projekte\hello.c `
  -ProgramName HELLO.PRG
```

Danach in der Shell:

```text
C:\> DIR
C:\> RUN HELLO.PRG
```

Ohne eigene Parameter wird automatisch
`userspace/programs/hello.c` gebaut und als `HELLO.PRG` eingebettet. Seine
Ausgabe `USERSPACE-E2E-OK` prüft Code, Konstanten, initialisierte Daten, BSS
und den Exit-Syscall.

## Begrenzter SATA-Abzieh-/Reconnect-Test

`SATAWR.PRG` prüft das beschreibbare Systemvolume `C:` auf realer ATA-/AHCI-
Hardware. Das Programm schreibt nach dem Start höchstens zehn Sekunden lang
maximal 2.048 Datensätze zu je 512 Byte nach `C:\SATAWR.TST`. Jeder Datensatz
enthält Sequenz, monotone Zeit, deterministische Nutzdaten und CRC32; jeder
Write wird mit `fsync` abgeschlossen. Während `SATA_WRITE ACTIVE` darf die
Test-HDD abgezogen und wieder verbunden werden:

```text
C:\>SATAWR
SATA_WRITE ACTIVE
```

Nach einem I/O-Fehler wartet das Programm höchstens 65 Sekunden auf die
Wiederverfügbarkeit von Systempartition und einem erneut vom Medium gelesenen
`SHELL.PRG`-Header. Danach akzeptiert es nur eine vollständige Rücknahme der
Dateierzeugung oder ein lückenloses, CRC-gültiges Präfix der geschriebenen
Datensätze. Ein zusätzlicher erzeugter, synchronisierter und zurückgelesener
Datensatz bestätigt `RECOVERY_RW_OK`. Erfolg endet mit
`SATA_WRITE TEST_OK`; Timeout, Teilrecord oder CRC-/Sequenzfehler endet mit
`SATA_WRITE TEST_FAIL ...`. Der Test beweist keine Stromausfallsicherheit und
darf nur auf einem entbehrlichen Testdatenträger ausgeführt werden.
Bleibt das Volume nach Ablauf read-only, hat mindestens eine der zwingenden
Prüfungen – Identität, frische Doppelreads, AHCI-Reinitialisierung oder
Journal-Recovery – nicht bestanden. Das ist ein Testfehler und darf nicht durch
manuelles Entsperren umgangen werden.

`DRIVES.PRG` zeigt neben Resource-ID, Laufwerk, Gerät und Typ auch den vom
Storage-Service gelieferten Zustand an: `READY`, `READONLY`, `DEGRADED`,
`QUARANTINED`, `RECOVERING`, `OFFLINE` oder `UNKNOWN`. `RECOVERING` bedeutet,
dass die Medienidentität wieder bestätigt ist, die Journal- und Fence-Recovery
aber noch nicht vollständig abgeschlossen wurde. Der Status wird über den
versionierten, nur lesenden Syscall 89 abgefragt; die bestehende
`DRIVE_INFO`-ABI bleibt unverändert.

## Öffentliche C-Schnittstelle

Ein minimales Programm sieht so aus:

```c
#include "x86os.h"

int main(void) {
    x86os_puts("Hallo vom externen Programm!\n");
    return 0;
}
```

`userspace/sdk/include/x86os.h` stellt versionierte, syscall-basierte
Funktionen bereit. Dazu gehören:

- `x86os_putchar`, `x86os_puts`, `x86os_print_number`
- `x86os_delay`, `x86os_sleep_ms`, `x86os_yield`, `x86os_getchar`
- `x86os_malloc`, `x86os_realloc`, `x86os_free`
- `x86os_open`, `x86os_read`, `x86os_create`, `x86os_write`, `x86os_close`
- `x86os_stat`, `x86os_readdir_batch`, `x86os_unlink`, Verzeichnisoperationen,
  `x86os_fsync`, Rename und Replace
- `x86os_touch`; `x86os_file_info_t` liefert Erstellungs-, Änderungs- und
  Zugriffszeit als Unix-Sekunden. FAT begrenzt die Auflösung auf zwei Sekunden
  beziehungsweise ein Zugriffsdatum.
- `x86os_getpid`, `x86os_spawn`, `x86os_wait` und Prozessdiagnose
- begrenzte IPC-/Capability-, Display-, Drive-Info- und Storage-Service-ABIs
- `x86os_exit`

Der Startup-Code übergibt `argc` und `argv` an `main` und reicht dessen
Rückgabewert als Prozessstatus an `x86os_exit` weiter. `x86os_spawn` startet
ein Kindprogramm im geerbten Arbeitsverzeichnis; `x86os_wait` wartet auf das
Kind, blockiert den aufrufenden Task ohne Polling und liefert dessen
Exit-Status. Kernel-Header und Kernel-Treiber dürfen von externen Programmen
nicht eingebunden werden.

Mehrere C- oder präprozessierte Assembly-Quellen (`.S`) können gemeinsam
übergeben werden:

```powershell
.\scripts\build-program-windows.ps1 `
  -Source .\app.c,.\helper.c,.\start.S `
  -Output .\build\programs\APP.PRG
```

## Modulares SDK und statische Bibliotheken

Das SDK wird als übliches Sysroot erzeugt:

```powershell
python scripts/build_user_sdk.py --output-dir build/sdk
```

Es enthält die öffentlichen Header unter `usr/include`, das Startobjekt
`usr/lib/crt0.o` sowie `libreistos.a`, `libreistnetparse.a`,
`libreistgui.a`, `libreistaudio.a` und `libreistimage.a` unter `usr/lib`.
`pkgconfig`-Metadaten beschreiben die öffentlichen Basis-, GUI-, Audio- und
Imagebibliotheken. Der Systemprogrammbuild kompiliert
diese gemeinsamen Module einmal und linkt alle PRGs danach gegen dieselben
Archive. Höchstens acht feste Buildworker teilen den inhaltsadressierten
globalen Zig-Cache, während jeder Lauf einen getrennten temporären lokalen
Cache besitzt; `--jobs` begrenzt die Parallelität bei Bedarf weiter.

Die Inkrementalgrenze folgt den Modulabhängigkeiten: `crt0.o`, Core-, Parser-
GUI- und Audioarchiv werden unabhängig geprüft und nur bei eigener Änderung ersetzt.
Console-PRGs hängen nicht von GUI-Headern oder `libreistgui.a` ab. Ein Wechsel
zwischen QEMU, VMware und realer Hardware invalidiert die Kernelkonfiguration,
löscht aber nicht mehr das zielunabhängige SDK und alle Ring-3-Programme.

Ein GUI-API-Beispiel lässt sich ohne private Compositorheader bauen:

```powershell
python scripts/build_user_program.py userspace/gui/examples/menu_controller.c `
  --output build/programs/MENUDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/dialog_controller.c `
  --output build/programs/DIALOGDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/basic_controls.c `
  --output build/programs/CONTROLDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/nested_containers.c `
  --output build/programs/CONTAINERDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/tab_sheet.c `
  --output build/programs/TABSDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/value_controls.c `
  --output build/programs/VALUESDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/programs/audioinfo.c `
  --output build/programs/AUDIOINFO.PRG `
  --sysroot build/sdk -l reistaudio
```

`--sysroot` wählt öffentliche Header, Startobjekt und Basisbibliothek aus.
Weitere standardmäßige Suchpfade und Archive werden mit `-I`, `-L` und `-l`
angegeben. Die öffentlichen Header `<reist/gui/types.h>`,
`<reist/gui/menu.h>`, `<reist/gui/dialog.h>`, `<reist/gui/control.h>`,
`<reist/gui/container.h>`, `<reist/gui/tabs.h>`,
`<reist/gui/value_controls.h>`, `<reist/gui/text_editor.h>`,
`<reist/gui/file_dialog.h>`, `<reist/gui/surface.h>` und
`<reist/gui/surface_client.h>`
dokumentieren Felder, Ownership, Lebensdauer, Capture, Fokus, Modalität,
Responses, Fehler und Rückgabewerte inline. Controls bilden eine in-process
C-Quell-API; das getrennte, bereits implementierte Surface-/Event-Protokoll
transportiert ausschließlich pointerfreie, versionierte Nachrichten zwischen
Client und Compositor. Eine dynamische Shared-Library-ABI oder
Wayland-Wire-Kompatibilität wird nicht behauptet.

Der Systemprogrammbuild erzeugt außerdem `GUIDEMO.PRG` aus
`userspace/gui/apps/control_gallery/main.c`. Beide Imagepfade installieren es
als `/usr/gui/bin/guidemo.prg`; der Standardpfad der Userspace-Shell erlaubt
den direkten Start mit `guidemo`.

## Format und ABI

Der Packer liest das gelinkte i386-ELF und schreibt die dateigestützten Teile
aller `PT_LOAD`-Segmente hinter einen geprüften 28-Byte-`MYPR`-Header.
Uninitialisierte `.bss`-Daten belegen keinen Platz in der PRG-Datei; ihre
Speichergröße steht im Header und der Loader legt sie als genullte Seiten an.
Programme werden für den 8-MiB-Bereich ab `0x40000000` gelinkt. Der aktuelle
Loader lädt exakt an diese Adresse; deshalb werden keine Laufzeit-Relokationen
benötigt oder erzeugt.

### Verbindlicher MYPR-v1-Vertrag

„v1“ bezeichnet den bestehenden, versionslosen Little-Endian-Header
`<4s6I>`. Seine 28 Bytes sind fest definiert:

| Offset | Feld | Vorgabe für v1 |
|---:|---|---|
| 0 | `identifier` | ASCII `MYPR` |
| 4 | `magic_number` | `0xDEADBEEF` |
| 8 | `entry_point` | Offset relativ zu `base_address` |
| 12 | `program_size` | Speicher-Payload ohne Header, inklusive BSS |
| 16 | `base_address` | exakt `0x40000000` |
| 20 | `relocation_offset` | Dateigröße und Ende des gespeicherten Images |
| 24 | `relocation_size` | exakt `0`; in v1 reserviert |

Auf den Header folgen die dateigestützten Payload-Bytes und höchstens drei
Padding-Bytes zur Vierbyteausrichtung. Nachlaufbytes und
Relokationstabellen sind nicht erlaubt. Fehlende Bytes zwischen Dateiende und
`28 + program_size` bilden BSS und werden vom Loader genullt. Der Einstieg
muss in gespeicherten Payload-Bytes liegen; der gesamte Speicherbereich muss
in `[0x40000000, 0x40800000)` passen.

MYPR v1 speichert keine Segmentgrenzen oder Seitenrechte. Ein späteres v2
benötigt deshalb einen eindeutig versionierten neuen Header, eine
Segmenttabelle mit Datei-/Speichergrößen und R/W/X-Rechten sowie – falls
wirklich erforderlich – typisierte relative Relokationen. Ein v2-Image darf
nicht als v1 mit `relocation_size != 0` getarnt werden.

Das Buildskript lehnt unter anderem folgende Fehler ab:

- falsche Architektur oder ein falsches ELF-Format
- ungelöste Symbole und verbliebene ELF-Relokationen
- Segmente außerhalb des 8-MiB-Programmbereichs
- Einstiegspunkte außerhalb eines ausführbaren Segments
- überlappende Ladesegmente

Der Kernel prüft Header, Größen, Einstieg, Basis und Relokationsgrenzen noch
einmal, bevor ein Task angelegt wird. `RUN`, `EXEC` und der Loader verwenden
denselben kanonischen VFS-Pfad wie `DIR`, `TYPE` und `CD`.

## Wichtige Sicherheitsgrenze

PRG-Tasks laufen in Ring 3 mit eigenen Seitentabellen, User-Stacks und
validierten Syscall-Zeigern. Der Kernelanteil des Adressraums bleibt in jeder
Prozessseitentabelle ausschließlich im Supervisor-Modus eingeblendet.
