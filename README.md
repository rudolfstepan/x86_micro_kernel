# x86 Microkernel

Ein freestanding 32-Bit-x86-Betriebssystem mit eigenem BIOS-Bootloader,
Kernel-Shell, VFS, FAT-Dateisystemen, Netzwerkstack und einer kleinen
Toolchain für externe Programme. Der bevorzugte Entwicklungsweg läuft nativ
unter Windows mit dem eigenen BIOS-Bootloader, ohne WSL oder ISO.

Stand dieser Dokumentation: 3. August 2026.

## Schnellstart unter Windows

Benötigt werden GNU Make, NASM, Zig, Python und eine MSYS2-Shell. Das
Buildskript sucht die Programme zuerst im `PATH` und kennt zusätzlich die im
Skript dokumentierten portablen Verzeichnisse unter `C:\tmp`.

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
```

Das erzeugt unter anderem:

- `build/x86-microkernel.img`: bootfähiges 64-MiB-Raw-Image
- `build/x86-microkernel-floppy.img`: bootfähiges 1,44-MB-FAT12-Diskettenimage
- `build/x86-microkernel.vmdk` und `.vmx`: VMware-Artefakte
- `build/vmware/x86-microkernel/`: vollständig startbare VMware-VM
- `build/programs/`: native Ring-3-Systemprogramme, unter anderem `SYSINFO`,
  `DATE`, `UPTIME`, `MEMINFO`, `REPEAT`, `CALC`, `ASCII` und `CAT`

Native Programme erhalten klassische `argc`/`argv`-Argumente und erben das
Arbeitsverzeichnis der Shell. Beispielsweise zeigt `cat README.TXT` eine Datei
direkt aus dem aktuellen Verzeichnis an.

Die fertige VM startet über
`build\vmware\x86-microkernel\START-VMWARE.cmd`. Alternativ kann die dortige
`x86-microkernel.vmx` direkt in VMware Workstation geöffnet werden.

## Eigenen Quelltext mitliefern

Eine oder mehrere C-/Assembly-Quellen können direkt in das System-Image
aufgenommen werden:

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests `
  -ProgramSource C:\Projekte\app.c,C:\Projekte\helper.S `
  -ProgramName APP.PRG
```

Ein minimales Programm verwendet das mitgelieferte SDK:

```c
#include "x86os.h"

int main(void) {
    x86os_puts("Hallo vom eigenen Programm!\n");
    return 0;
}
```

In der Kernel-Shell:

```text
C:\> DIR
C:\> TYPE README.TXT
C:\> sysinfo
C:\> calc
C:\> APP
```

Details zu ABI, Linker, Dateiformat und Sicherheitsgrenze stehen in
[Externe Programme bauen](docs/development/USER_PROGRAM_TOOLCHAIN.md).

## Boot- und Datenträgeraufbau

Der native Standardweg ist:

```text
BIOS
  -> MBR Stage 1
  -> Manifest und Stage 2
  -> ELF32-Kernel laden und prüfen
  -> Protected Mode / Multiboot-1-Handoff
  -> Kernelinitialisierung
  -> FAT32 als Laufwerk C:
  -> DOS-artige Shell
```

Das Image enthält eine kleine RAW-Bootpartition und eine FAT32-Datenpartition.
Der Kernel wird über CRC32 und seine ELF32-Struktur geprüft. `README.TXT` und
das gebaute `.PRG` liegen auf der Datenpartition. Einen GRUB-/ISO-Buildpfad
gibt es nicht mehr.

## Shell

Die Shell verwendet einen gemeinsamen kanonischen Pfadauflöser für alle
Dateioperationen. Unterstützt werden DOS- und VFS-Schreibweisen, relative und
absolute Pfade, `.` und `..` sowie getrennte aktuelle Verzeichnisse pro
Laufwerk.

Wichtige Befehle:

| Bereich | Befehle |
|---|---|
| Navigation | `DIR`, `LS`, `CD`, `CHDIR`, `DRIVES`, `MOUNT` |
| Dateien | `TYPE`, `OPEN`, `COPY`, `DEL`, `ERASE`, `MKFILE` |
| Verzeichnisse | `MD`, `MKDIR`, `RD`, `RMDIR` |
| Programme | `RUN`, `EXEC`, `PID`, `KILL`, `BASIC` |
| Netzwerk | `GETIP`, `IFCONFIG`, `PING`, `ARP`, `NET` |
| System | `HELP`, `CLS`, `MEM`, `PCI`, `IRQ`, `DATETIME` |

Beispiele und die genaue Pfadsemantik stehen in
[Shell und Pfade](docs/features/SHELL_ENHANCEMENTS.md).

## Netzwerk und VMware

Die bereitgestellte VMware-VM verwendet einen Intel-E1000-Adapter an
`VMnet0` im Bridge-Modus. Beim Boot fordert der Kernel automatisch eine
IPv4-Konfiguration per DHCP an. Der aktuelle Stack umfasst Ethernet, ARP,
IPv4, ICMP und DHCP. DNS, TCP und Anwendungen wie HTTP oder SMB sind noch
nicht vorhanden.

```text
C:\> GETIP
C:\> NET DHCP
C:\> PING 192.168.1.1
```

Siehe [VMware](docs/hardware/VMWARE.md) und
[Netzwerk](docs/networking/NETWORK.md).

## Build- und Testbefehle

Der native Windows-Build ist der dokumentierte Referenzweg. Auf Systemen mit
passender ELF32-Toolchain bleiben Make-Ziele verfügbar:

```bash
make kernel TARGET=qemu VIDEO=vga
make native-image TARGET=real_hw VIDEO=vga
make run-native TARGET=qemu VIDEO=vga
make test-unit
```

Der vollständige Windows-Build mit `-RunTests` führt die hostseitigen
Regressionstests aus. Sie prüfen unter anderem Bootimage, FAT12/FAT32, EXT2,
VFS, Shell-Pfade, PRG-Validierung und die externe C-/Assembly-Toolchain.

## Quellbaum

```text
arch/x86/          BIOS-Boot, CPU, Interrupts und Paging
kernel/            Initialisierung, Prozesse, Scheduler, Shell und Syscalls
mm/                Kernel-Allocator
fs/                VFS sowie FAT12, FAT32 und EXT2
drivers/           Block-, Eingabe-, Video-, PCI-, USB- und Netzwerktreiber
lib/               freestanding libc/libk
userspace/sdk/     öffentliche API und Startup-Code für externe Programme
examples/userspace Beispielquellen
scripts/           Windows-, Image- und Testwerkzeuge
test/              hostseitige Regressionstests
docs/              aktuelle Anleitungen und historische Arbeitsberichte
```

Neue `.c`-Dateien in den durch Wildcards erfassten Kernel-/Treiberordnern
werden vom Makefile automatisch aufgenommen. Neue Assembly- oder besondere
Buildschritte müssen explizit ergänzt werden.

## Aktuelle Grenzen

- Externe `.PRG`-Programme sind vertrauenswürdige Ring-0-Tasks und noch nicht
  durch Ring 3 oder eigene Adressräume isoliert.
- Das erzeugte FAT32-Image verwendet für eingebettete Dateien ASCII-8.3-Namen.
- Der Netzwerkstack besitzt noch kein DNS, TCP, UDP-Socket-API oder IPv6.
- Der native Bootpfad ist BIOS/MBR-basiert; UEFI ist nicht implementiert.
- USB/xHCI und Framebuffer sind experimenteller als der VGA-/PS/2-Standardweg.

## Dokumentation

Der vollständige, nach Aktualität geordnete Einstieg befindet sich im
[Dokumentationsindex](docs/README.md). Historische Analyse- und
Implementierungsberichte sind dort ausdrücklich als solche gekennzeichnet.
