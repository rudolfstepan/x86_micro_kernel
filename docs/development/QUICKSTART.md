# Quickstart

Stand: 16. August 2026.

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

Der Build arbeitet inkrementell: unveränderte Kernelobjekte und Ring-3-
Programme bleiben erhalten. Ein Konfigurationswechsel löst gezielt einen
sauberen Neubau aus; `-Clean` erzwingt ihn ausdrücklich. Danach werden beide
BIOS-Stufen, das Raw-Image und das VMware-Paket aktualisiert.

Wichtige Ergebnisse:

```text
build/kernel.bin
build/stage1_mbr.bin
build/stage2_bios.bin
build/reist-os.img
build/programs/HELLO.PRG
build/programs/DESKTOP.PRG
build/vmware/reist-os/reist-os.vmx
build/vmware/reist-os/START-VMWARE.cmd
```

## Starten

VMware:

```powershell
.\build\vmware\reist-os\START-VMWARE.cmd
```

QEMU mit bereits gebautem Raw-Image:

```powershell
.\scripts\run-windows.ps1 -NoBuild
```

QEMU über AHCI/SATA:

```powershell
.\scripts\run-windows.ps1 -NoBuild -Sata
```

Ein Headless-QEMU-Start zeigt die Bootloader-Diagnose, stellt aber keine
interaktive VGA-Shell auf der Konsole bereit:

```powershell
.\scripts\run-windows.ps1 -NoBuild -Headless
```

## Grafischen Desktop starten

Der Desktop benötigt einen nativen Framebuffer-Build:

```powershell
.\scripts\build-windows.ps1 -Target qemu -Video framebuffer -RunTests
.\scripts\run-windows.ps1 -NoBuild
```

Stage 2 bevorzugt VBE 1024x768x32 und versucht danach 800x600x32. Bei Erfolg
startet `DESKTOP.PRG`; `Tab` oder die Pfeiltasten wählen eine der vier
App-Karten, `Enter` startet sie und `Esc` öffnet die Shell. Die gestartete App
läuft im Vollbild. Nach ihrem Ende zeichnet der Desktop sich neu.

Ist kein geeigneter VBE-Modus vorhanden, fällt der Bootvorgang sicher auf
VGA-Text und `SHELL.PRG` zurück. `DESKTOP_OK` im seriellen Log bestätigt einen
erfolgreichen ersten Desktop-Renderdurchlauf.

## Erster Funktionstest

Beim standardmäßigen VGA-Build sollte nach dem Boot der Prompt `C:\>`
erscheinen. Danach:

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
make run-fb
make test-unit
make test-smoke TARGET=qemu VIDEO=vga
```

`make all`, `make run` und `make native-image` verwenden den eigenen
BIOS-Bootloader. Sie entsprechen damit dem nativen Windows-/VMware-
Paket.

`make test-smoke` startet das QEMU-Image ohne Anzeige, führt den automatischen
Ring-3-Test aus und schreibt das serielle Protokoll nach
`build/guest-smoke.log`. Der Test benötigt `qemu-system-i386` im `PATH`.

## Häufige Fehler

### Werkzeug nicht gefunden

Das gemeldete Programm installieren oder in den `PATH` aufnehmen. Bei
portablen Paketen kann der Fallbackpfad in `build-windows.ps1` ergänzt werden.

### VMware-VM läuft beim Neubau

Die VM sauber herunterfahren. Das Buildskript verweigert das Überschreiben
einer laufenden paketierten VM.

### Kein LAN

Im VMware Virtual Network Editor `VMnet0` dem richtigen Hostadapter zuordnen.
WLAN-Client-Isolation kann gebridgte Gäste blockieren. Danach mit `NET DHCP`
den automatisch durch `REIST.PRG` verwalteten Lease-Zustand und mit `GETIP`
die aktive Konfiguration prüfen.

### Programm wird abgelehnt

Den Packer verwenden und keine fremde ELF-Datei nur in `.PRG` umbenennen. Der
Loader akzeptiert ausschließlich geprüfte MYPR-Images für Basisadresse
`0x40000000` und maximal 8 MiB. Der Kernel liest die Datei in einen
dynamischen Heap-Puffer, kopiert sie in private Prozessseiten und gibt den
Puffer danach wieder frei; es gibt keine zweite Link- oder Ausführungsadresse.
