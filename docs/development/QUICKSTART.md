# Quickstart

Diese Anleitung beschreibt den bevorzugten, vollständig nativen Windows-Weg.
WSL, GRUB und ein Cross-GCC sind dafür nicht nötig.

## Voraussetzungen

- Windows 10 oder 11 mit PowerShell
- GNU Make
- NASM
- Zig
- Python 3
- MSYS2 mit `sh.exe` und den grundlegenden Unix-Werkzeugen
- VMware Workstation zum Starten der fertigen VM
- optional QEMU für den Raw-Image-Test

`scripts/build-windows.ps1` sucht Werkzeuge zunächst im `PATH`. Unterstützte
Fallbackpfade stehen direkt am Anfang des Skripts. Fehlende Werkzeuge werden
mit ihrem Programmnamen gemeldet; es findet keine automatische Installation
statt.

## Kernel, Tests und VMware-Paket bauen

Im Projektstamm:

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
```

Der Build räumt zunächst die gemeinsamen Objektdateien auf, kompiliert den
Kernel als freestanding i386-ELF, assembliert beide BIOS-Stufen, baut
`HELLO.PRG`, erzeugt das Raw-Image und verpackt die VMware-VM.

Wichtige Ergebnisse:

```text
build/kernel.bin
build/stage1_mbr.bin
build/stage2_bios.bin
build/x86-microkernel.img
build/programs/HELLO.PRG
build/vmware/x86-microkernel/x86-microkernel.vmx
build/vmware/x86-microkernel/START-VMWARE.cmd
```

## Starten

VMware:

```powershell
.\build\vmware\x86-microkernel\START-VMWARE.cmd
```

QEMU mit bereits gebautem Raw-Image:

```powershell
.\scripts\run-windows.ps1 -NoBuild
```

Ein Headless-QEMU-Start zeigt die Bootloader-Diagnose, stellt aber keine
interaktive VGA-Shell auf der Konsole bereit:

```powershell
.\scripts\run-windows.ps1 -NoBuild -Headless
```

## Erster Funktionstest

Nach dem Boot sollte der Prompt `C:\>` erscheinen. Danach:

```text
C:\> DIR
C:\> TYPE README.TXT
C:\> RUN HELLO.PRG
C:\> GETIP
C:\> NET STATUS
```

`HELLO.PRG` meldet bei Erfolg `USERSPACE-E2E-OK`. Bei einer gebridgten
VMware-Verbindung zeigt `GETIP` die per DHCP bezogene LAN-Adresse.

## Eigenes Programm

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests `
  -ProgramSource C:\Projekte\meinprog.c `
  -ProgramName MEINPRG.PRG
```

Der Name im Image muss ein eindeutiger ASCII-8.3-Name mit der Endung `.PRG`
sein. Mehrere Quellen werden als PowerShell-Array übergeben:

```powershell
.\scripts\build-windows.ps1 -Target vmware `
  -ProgramSource .\app.c,.\math.c,.\helper.S `
  -ProgramName APP.PRG
```

Nur das mitgelieferte SDK ist die öffentliche Programmierschnittstelle. Siehe
[USER_PROGRAM_TOOLCHAIN.md](USER_PROGRAM_TOOLCHAIN.md).

## Direkte Make-Ziele

Auf Linux/macOS oder in einer entsprechend eingerichteten Shell:

```bash
make kernel TARGET=qemu VIDEO=vga
make user-program
make native-image TARGET=real_hw VIDEO=vga
make run-native TARGET=qemu VIDEO=vga
make test-unit
```

`make all`, `make run` und `make native-image` verwenden den eigenen
BIOS-Bootloader. Sie entsprechen damit dem nativen Windows-/VMware-
Paket.

## Häufige Fehler

### Werkzeug nicht gefunden

Das gemeldete Programm installieren oder in den `PATH` aufnehmen. Bei
portablen Paketen kann der Fallbackpfad in `build-windows.ps1` ergänzt werden.

### VMware-VM läuft beim Neubau

Die VM sauber herunterfahren. Das Buildskript verweigert das Überschreiben
einer laufenden paketierten VM.

### Kein LAN

Im VMware Virtual Network Editor `VMnet0` dem richtigen Hostadapter zuordnen.
WLAN-Client-Isolation kann gebridgte Gäste blockieren. Danach `NET DHCP` und
`GETIP` verwenden.

### Programm wird abgelehnt

Den Packer verwenden und keine fremde ELF-Datei nur in `.PRG` umbenennen. Der
Loader akzeptiert ausschließlich geprüfte MYPR-Images für Basisadresse
`0x02100000` und maximal 8 MiB.
