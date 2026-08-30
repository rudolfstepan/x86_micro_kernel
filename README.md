# REIST-OS

**Resilient Execution, Isolation and Stability Technology**

Projektwebsite: [https://reist-os.intracom.at](https://reist-os.intracom.at)

Ein freestanding 32-Bit-x86-Betriebssystem mit eigenem BIOS-Bootloader,
Ring-3-Shell, VFS, FAT-Dateisystemen, Netzwerkstack, Audio- und Grafikdiensten
sowie einer kleinen Toolchain für externe Programme. Der bevorzugte
Entwicklungsweg läuft nativ unter Windows mit dem eigenen BIOS-Bootloader,
ohne WSL oder ISO.

![REIST Workspace mit Explorer und windowed Ring-3-Image-Viewer in QEMU](docs/assets/screenshots/reist-desktop-apps.png)

*Automatisch aus dem aktuellen QEMU-Gast aufgenommen; kein Mock-up.*

> **Safety-Status:** REIST verfolgt einen generischen High-Assurance-Kern mit
> getrennten Referenzprofilen. Das System ist ein Forschungsprototyp, nicht
> zertifiziert und nicht für medizinische, industrielle oder andere
> sicherheitskritische Produktion freigegeben. Verbindlich sind der
> [High-Assurance-Core-Vertrag](docs/architecture/HIGH_ASSURANCE_CORE_CONTRACT.md),
> der [Resilienz- und Degradierungsvertrag](docs/architecture/RESILIENCE_AND_DEGRADATION_CONTRACT.md)
> und die [Roadmap](docs/development/OS_GAP_ANALYSIS_AND_ROADMAP.md). Das
> medizinische Dokument ist ausschließlich ein optionales Referenzprofil.

Die bestehende öffentliche SDK-ABI behält vorerst ihre `x86os_*`-Symbolnamen,
damit vorhandene PRG-Programme binär und quelltextlich kompatibel bleiben.

Stand dieser Dokumentation: 20. August 2026.

## Schnellstart unter Windows

Benötigt werden GNU Make, NASM, Zig, Python und eine MSYS2-Shell. Das
Buildskript sucht die Programme zuerst im `PATH` und kennt zusätzlich die im
Skript dokumentierten portablen Verzeichnisse unter `C:\tmp`.

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
```

Das erzeugt unter anderem:

- `build/reist-os.img`: bootfähiges 64-MiB-Raw-Image
- `build/reist-os-floppy.img`: bootfähiges 1,44-MB-FAT12-Diskettenimage
- `build/reist-os.vmdk` und `.vmx`: VMware-Artefakte
- `build/vmware/reist-os/`: vollständig startbare VMware-VM
- `build/programs/`: native Ring-3-Systemprogramme, unter anderem `SHELL`,
  `REIST`, `STORAGE`, `DRIVES`, `CHKDSK`, `FDISK`, `FORMAT`, `SYSINFO`,
  `MEMINFO`, `CAT`, `LS`, `SAVE`, `BASIC`, `EDIT`, `RENAME`, `STAT`, `DF`,
  `TOUCH`, `TREE`, `FIND`, `RM`, `SPAWN`, `PS` und `KILL`
- `build/programs/DESKTOP.PRG`, `NOTEPAD.PRG`, `IMAGEVIEWER.PRG`,
  `SOUNDPLAYER.PRG` und `GUIDEMO.PRG`: grafische Session und Anwendungen

Native Programme erhalten klassische `argc`/`argv`-Argumente und erben das
Arbeitsverzeichnis der Shell. Beispielsweise zeigt `cat README.TXT` eine Datei
direkt aus dem aktuellen Verzeichnis an.

FAT32 unterstützt VFAT Long File Names mit bis zu 255 druckbaren ASCII-
Zeichen. Verzeichnisauflistung, Pfadauflösung, Datei-I/O, Erzeugen, Löschen und
Same-Directory-Rename verwenden den langen Namen; ein kollisionsgeprüfter
8.3-`~n`-Alias bleibt für ältere Werkzeuge und Rettungspfade erhalten.

`EDIT` bleibt der kleine Console-Texteditor. Der grafische
`/usr/gui/bin/notepad.prg` läuft aus dem Desktop als separates, verschieb- und
skalierbares Surface-Fenster, besitzt Öffnen-/Speichern-Dialoge und speichert
über temporäre Datei, `fsync`, Close und atomaren Same-Directory-Rename.

Nach der Hardware- und Dateisysteminitialisierung startet der Kernel
`SHELL.PRG` automatisch vom BIOS-Bootlaufwerk. Die ältere Kernel-Shell wird
nur noch als Rettungskonsole verwendet, falls die Userspace-Shell fehlt oder
beendet wird.

`desktop` kann direkt aus dem VGA-Textbetrieb den validierten QEMU-, VMware-
oder vorbereiteten VBE-Grafikpfad aktivieren. Explorerfenster bleiben dabei im
Desktop; Notepad und Image Viewer laufen als getrennte Ring-3-Surface-Clients.
Noch nicht migrierte GUI-/Console-Programme verwenden vorübergehend den
dokumentierten Vollbild-Kompatibilitätspfad.

Die Userspace-Shell zeigt DOS-kompatible Laufwerksbuchstaben (`A:`/`B:` für
Disketten und ab `C:` für gemountete ATA-/AHCI-Volumes). Ein Laufwerk wird beispielsweise
mit `A:` oder `C:` gewechselt; interne VFS-Mountpfade bleiben verborgen.
Dateiverwaltung steht als Ring-3-Programme `MKDIR`, `RMDIR`, `DEL`, `COPY`,
`RENAME`, `STAT`, `DF`, `TOUCH`, `TREE`, `FIND` und `RM`
zur Verfügung; DOS-Pfade können dabei auch laufwerksübergreifend verwendet
werden, etwa `copy A:\README.TXT C:\README.TXT`.

`SHELL.PRG` durchsucht bei Programmnamen zuerst das aktuelle Verzeichnis und
danach `PATH`. Beim Start enthält `PATH` `/bin`, `/sbin`, `/usr/bin` und
`/usr/gui/bin`; interne Dienste unter `/libexec/reist` werden nur über ihre
festen Pfade gestartet. `path` zeigt den Suchpfad an; `path C:\;A:\TOOLS`
setzt ihn neu.

`TOUCH` aktualisiert bei FAT12/FAT32 die Änderungs- und Zugriffszeit oder legt
eine leere Datei an. `STAT` zeigt die drei Dateizeiten als Unix-Sekunden.
FAT speichert die Änderungszeit mit Zwei-Sekunden-Auflösung und das
Zugriffsdatum ohne Uhrzeit.

Die DOS-Aliase `DIR`, `TYPE`, `MD`, `RD` und `ERASE` starten die passenden
Ring-3-Programme. Auch `ECHO`, `CLS` und `DRIVES` sind normale Programme und
keine Kernel-Shell-Befehle.

`LS` beziehungsweise `DIR` pausiert lange Verzeichnislisten automatisch mit
`-- More --`. Eine Taste zeigt die nächste Bildschirmseite, `Q` oder `Esc`
bricht die Ausgabe ab.

Die fertige VM startet über
`build\vmware\reist-os\START-VMWARE.cmd`. Alternativ kann die dortige
`reist-os.vmx` direkt in VMware Workstation geöffnet werden.

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
  -> eindeutig markierte FAT32-Systempartition als Laufwerk C:
  -> DOS-artige Shell
```

Das Image enthält eine kleine RAW-Bootpartition und eine FAT32-Datenpartition
mit dem Label `X86 SYSTEM`. QEMU kann den ATA/IDE- oder den expliziten
AHCI/SATA-Pfad verwenden; das generierte VMware-Paket verwendet AHCI/SATA.
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
| Dateien | `TYPE`, `OPEN`, `COPY`, `DEL`, `ERASE`, `MKFILE`, `EDIT` |
| Verzeichnisse | `MD`, `MKDIR`, `RD`, `RMDIR` |
| Programme | `RUN`, `EXEC`, `PS`, `KILL`, `BASIC` |
| Netzwerk | `GETIP`, `IFCONFIG`, `PING`, `ARP`, `NET` |
| System | `HELP`, `CLS`, `MEMINFO`, `SYSINFO`, `USBINFO`, `AUDIOINFO`, `DATETIME` |

Beispiele und die genaue Pfadsemantik stehen in
[Shell und Pfade](docs/features/SHELL_ENHANCEMENTS.md).

## Netzwerk und VMware

Die bereitgestellte VMware-VM verwendet einen Intel-E1000-Adapter an
`VMnet0` im Bridge-Modus. Für das ASUS H81M-K ist zusätzlich der Realtek-
RTL8111G/RTL8168-Treiber für PCI-ID `10EC:8168` enthalten. Der überwachte
Ring-3-Dienst `REIST.PRG` verarbeitet die begrenzten Netzwerkentscheidungen
einschließlich DHCP. Der aktuelle Stack umfasst Ethernet, ARP, IPv4, ICMP,
DHCP, prozessgebundene UDP-/TCP-Sockets, DNS sowie aktives und passives TCP.
`/sbin/httpd.prg` stellt Dateien und begrenzte Directory-Listings aus `/htdocs`
bereit. `/usr/bin/curl.prg` unterstützt HTTP und authentisiertes HTTPS über die
wiederverwendbare Ring-3-Bibliothek `libreisttls.a`; IPv6, SMB und
Zertifikatswiderruf sind noch nicht vorhanden.

```text
C:\> GETIP
C:\> NET DHCP
C:\> PING 192.168.1.1
C:\> httpd 8080
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
Regressionstests aus. Die REIST-Paket- und Laufzeitgates ergänzen dies um
echte QEMU-Gastläufe für Ring 3, SATA/AHCI, PS/2, Storage-Recovery,
FDD-Hotplug und ausgewählte Fault-Injection-Pfade.

## Quellbaum

```text
arch/x86/          BIOS-Boot, CPU, Interrupts und Paging
kernel/            Initialisierung, Prozesse, Scheduler, Shell und Syscalls
mm/                Kernel-Allocator
fs/                VFS sowie FAT12, FAT32 und EXT2
drivers/           Block-, Eingabe-, Video-, PCI-, USB- und Netzwerktreiber
lib/               freestanding libc/libk
userspace/sdk/     öffentliche API und Startup-Code für externe Programme
userspace/gui/     Compositor, GUI-SDK, Controls und grafische Anwendungen
userspace/audio/   öffentliche Audio-API und WAV-Hilfsbibliothek
userspace/image/   öffentliche BMP-/GIF-Rasterbibliothek
userspace/programs Console- und Systemprogramme
scripts/           Windows-, Image- und Testwerkzeuge
test/              hostseitige Regressionstests
docs/              aktuelle Anleitungen und historische Arbeitsberichte
```

Neue `.c`-Dateien in den durch Wildcards erfassten Kernel-/Treiberordnern
werden vom Makefile automatisch aufgenommen. Neue Assembly- oder besondere
Buildschritte müssen explizit ergänzt werden.

## Aktuelle Grenzen

- Externe `.PRG`-Programme laufen in Ring 3 mit eigenen Adressräumen. Die
  Prozess- und Syscall-API ist jedoch noch klein und besitzt beispielsweise
  keine Pipes, Signale oder allgemeine Socket-Schnittstelle.
- FAT32-LFN ist derzeit auf druckbares ASCII begrenzt; vollständiges Unicode
  samt Normalisierung ist noch nicht implementiert.
- Der Netzwerkstack besitzt noch kein IPv6 oder vollständiges POSIX-Socket-API;
  die vorhandene UDP-/TCP-/TLS-ABI ist bewusst klein und begrenzt. TLS besitzt
  keine Widerrufsprüfung und keine gegen manipulierte Hardware geschützte Uhr.
- Der native Bootpfad ist BIOS/MBR-basiert; UEFI ist nicht implementiert.
- USB/xHCI-HID für Boot-Tastatur und -Maus ist experimentell. VMware nutzt
  eine virtuelle xHCI-Maus ohne physisches HID-Passthrough; einfache reale
  Boot-Keyboards und die Maus wurden auf dem ASUS-System beobachtet. Das
  AULA/BY-Tech-Composite-Keyboard `258A:010C` bleibt ein offener Gerätebug.
  PS/2 ist der robuste Eingabefallback; Grafik und VBE besitzen noch keine
  breite reale Hardwarematrix.
- Der Desktop besitzt eine versionierte, generationsgebundene Surface-/Event-
  Grenze. Notepad und Image Viewer sind migriert; Control Gallery, Sound
  Player, Terminal und Systemwerkzeuge benötigen noch eigene Surface-Clients.
- AHCI/SATA ist in QEMU, VMware und auf realer SATA-Hardware gebootet worden;
  das ist noch keine allgemeine Controller- oder Langzeitqualifikation.

## Dokumentation

Der vollständige, nach Aktualität geordnete Einstieg befindet sich im
[Dokumentationsindex](docs/README.md). Historische Analyse- und
Implementierungsberichte sind dort ausdrücklich als solche gekennzeichnet.
