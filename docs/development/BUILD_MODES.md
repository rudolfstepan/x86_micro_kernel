# Build-Modi

Der Kernel kennt drei Zielprofile und zwei Videoausgaben. Die Profile ändern
Timing- und Validierungsdefinitionen, nicht das CPU-Ziel: Alle Varianten sind
freestanding i386.

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
```

Für das auslieferbare VMware-Paket sollte `vmware` verwendet werden. Für ein
Raw-Image, das später auf einen echten Datenträger geschrieben wird, ist
`real_hw` vorgesehen.

## Video

| `VIDEO` | Definition | Status |
|---|---|---|
| `vga` | `USE_VGA_TEXT` | Standard und verifizierter Shell-Weg |
| `framebuffer` | `USE_FRAMEBUFFER` | experimenteller Grafikweg |

Das Windows-Skript baut derzeit bewusst mit VGA-Textmodus. Mit Make kann der
Framebuffer gewählt werden:

```bash
make kernel TARGET=qemu VIDEO=framebuffer
make run-fb
```

## Build-Ausgaben

| Befehl | Bootweg | Ergebnis |
|---|---|---|
| `build-windows.ps1` | eigener BIOS-/MBR-Loader | Raw-Image, VMDK, VMX und VMware-Paket |
| `make all` / `make native-image` | eigener BIOS-Bootloader | natives Raw-Image und VMware-VM |
| `make run` / `make run-native` | eigener BIOS-Bootloader | natives Image in QEMU |

Der Bootpfad benötigt weder ein ISO noch einen externen Bootloader noch QEMUs
`-kernel`-Abkürzung. Die Multiboot-1-kompatible Übergabestruktur wird von
Stage 2 erzeugt, damit der Kernel denselben frühen Einstieg weiterverwenden
kann.

## Konfigurationswechsel

Objektpfade werden zwischen Profilen geteilt. Das Makefile verwendet deshalb
einen Konfigurationsstempel; `build-windows.ps1` führt zusätzlich einen
sauberen Neubau aus. Bei einem manuellen Profilwechsel ist ebenfalls ein
Clean-Build sinnvoll:

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
