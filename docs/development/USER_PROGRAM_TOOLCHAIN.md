# Externe Programme für die Kernel-Shell bauen

Stand: 16. August 2026.

Das Projekt enthält eine native Windows-Toolchain, die fremden C-Quelltext in
das ausführbare `MYPR`-Format übersetzt. WSL, GRUB und ein Cross-GCC werden
nicht benötigt; der Build verwendet Zig/Clang, LLD und den Python-Packer aus
diesem Repository.

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
`examples/userspace/hello.c` gebaut und als `HELLO.PRG` eingebettet. Seine
Ausgabe `USERSPACE-E2E-OK` prüft Code, Konstanten, initialisierte Daten, BSS
und den Exit-Syscall.

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
