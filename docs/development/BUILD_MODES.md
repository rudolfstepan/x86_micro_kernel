# Build-Modi

Der Kernel kennt drei Zielprofile und zwei Videoausgaben. Die Profile ändern
Timing- und Validierungsdefinitionen, nicht das CPU-Ziel: Alle Varianten sind
freestanding i386.

## Verbindlicher Windows-Build

Unter Windows ist `scripts/build-windows.ps1` der verbindliche Einstieg. Das
Skript sucht die installierte beziehungsweise portable Zig-Toolchain, setzt
Clang auf das Ziel `x86-freestanding`, verwendet `ld.lld` als ELF-Linker und
bindet NASM sowie die MSYS-Shell kontrolliert ein. Normale Wiederholungen sind
inkrementell: unveränderte Kernelobjekte und Ring-3-Programme werden
wiederverwendet. Ein Konfigurationswechsel bei Ziel, Video, Fault-Injection,
Node-ID oder Toolpfad löst automatisch genau einmal einen sauberen Neubau aus.

```powershell
.\scripts\build-windows.ps1 -Target qemu -Video vga
.\scripts\build-windows.ps1 -Target vmware -Video vga
.\scripts\build-windows.ps1 -Target real_hw -Video vga
.\scripts\build-windows.ps1 -Target qemu -Video framebuffer
```

Ein vollständiger Neubau kann jederzeit ausdrücklich angefordert werden:

```powershell
.\scripts\build-windows.ps1 -Target qemu -Video vga -Clean
```

Ein nacktes `make kernel` aus einer normalen PowerShell ist **kein**
gleichwertiger Windows-Build. Es kann `C:\msys64\mingw64\bin\gcc.exe` und
`ld.exe` auswählen. Dieser MinGW-Linker erzeugt PE-Dateien und unterstützt das
vom Kernel benötigte Ausgabeformat `elf_i386` nicht. Ein typisches Symptom ist
eine Linkerfehlermeldung zu einem nicht unterstützten oder unbekannten
`elf_i386`-Format. In diesem Fall wird weder das Linkerskript noch der
Kernelcode geändert; der Build wird über `build-windows.ps1` wiederholt.

Direkte Make-Aufrufe sind nur in einer Unix-/CI-Umgebung mit bereits korrekt
konfigurierter ELF-i386-Cross-Toolchain oder mit ausdrücklich gesetzten,
ELF-fähigen `CC`- und `LD`-Variablen zulässig.

## Zielprofile

| `TARGET` | Definitionen | Einsatz |
|---|---|---|
| `qemu` | `QEMU_BUILD`, lockeres ATA-Timing | schnelle Emulation |
| `vmware` | `VMWARE_BUILD`, mittleres ATA-Timing | VMware Workstation |
| `real_hw` | `REAL_HARDWARE`, striktes ATA-Timing und FAT32-Prüfung | physischer BIOS-PC |

Der native Windows-Build erhält das Profil so:

```powershell
.\scripts\build-windows.ps1 -Target qemu
.\scripts\build-windows.ps1 -Target vmware
.\scripts\build-windows.ps1 -Target real_hw
.\scripts\build-windows.ps1 -Target qemu -Video framebuffer
```

Für das auslieferbare VMware-Paket sollte `vmware` verwendet werden. Für ein
Raw-Image, das später auf einen echten Datenträger geschrieben wird, ist
`real_hw` vorgesehen.

## Video

| `VIDEO` | Definition | Status |
|---|---|---|
| `vga` | `USE_VGA_TEXT` | Standard und verifizierter Shell-Weg |
| `framebuffer` | `USE_FRAMEBUFFER` | experimenteller Grafikweg |

Das Windows-Skript verwendet standardmäßig VGA; `-Video framebuffer` wählt
den optionalen Grafikpfad:

```powershell
.\scripts\build-windows.ps1 -Target qemu -Video framebuffer
```

In einer bereits konfigurierten Unix-/CI-Cross-Build-Umgebung entsprechen dem
die Make-Ziele `make kernel TARGET=qemu VIDEO=framebuffer` und `make run-fb`.

## Build-Ausgaben

| Befehl | Bootweg | Ergebnis |
|---|---|---|
| `.\scripts\build-windows.ps1` | eigener BIOS-/MBR-Loader | Raw-Image, VMDK, VMX und VMware-Paket |
| `make all` / `make native-image` | eigener BIOS-Bootloader | natives Raw-Image und VMware-VM |
| `make run` / `make run-native` | eigener BIOS-Bootloader | natives Image in QEMU |

Der Bootpfad benötigt weder ein ISO noch einen externen Bootloader noch QEMUs
`-kernel`-Abkürzung. Die Multiboot-1-kompatible Übergabestruktur wird von
Stage 2 erzeugt, damit der Kernel denselben frühen Einstieg weiterverwenden
kann.

## Konfigurationswechsel

Objektpfade werden zwischen Profilen geteilt. Das Makefile verwendet deshalb
einen Konfigurationsstempel; `build-windows.ps1` speichert zusätzlich die
vollständige Windows-Buildkonfiguration unter `build/` und bereinigt nur bei
einer Änderung oder `-Clean`. Unter Windows genügt daher der erneute
Skriptaufruf mit dem gewünschten Profil. Nur in einer korrekt konfigurierten Unix-/CI-
Cross-Build-Umgebung wird ein manueller Profilwechsel so durchgeführt:

```bash
make clean
make kernel TARGET=vmware VIDEO=vga
```

## Tests

```bash
make test-unit
make test-all
```

`test-unit` ist fixture-unabhängig. `test-all` prüft zusätzlich vorhandene
klassische Datenträgerabbilder über `scripts/test_disk_images.py`. Der native
Windows-Komplettweg lautet:

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
```
