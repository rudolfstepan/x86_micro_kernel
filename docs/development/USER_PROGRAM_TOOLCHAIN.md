# Externe Programme für die Kernel-Shell bauen

Stand: 3. August 2026.

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

`userspace/sdk/include/x86os.h` stellt derzeit diese syscall-basierten
Funktionen bereit:

- `x86os_putchar`, `x86os_puts`, `x86os_print_number`
- `x86os_delay`, `x86os_getchar`
- `x86os_malloc`, `x86os_realloc`, `x86os_free`
- `x86os_exit`

Der Startup-Code ruft `main(void)` auf und beendet den Task anschließend mit
`x86os_exit`. Kernel-Header und Kernel-Treiber dürfen von externen Programmen
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
