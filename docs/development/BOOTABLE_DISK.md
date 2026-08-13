# Nativer BIOS-Boot ohne GRUB

Stand: 3. August 2026.

Das Projekt besitzt einen eigenen zweistufigen BIOS-Bootloader. QEMU und
VMware starten deshalb vom virtuellen IDE-Datenträger wie ein echter PC; weder
GRUB noch ein ISO noch QEMUs `-kernel`-Abkürzung werden verwendet.

## Windows-Schnellstart

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
.\scripts\run-windows.ps1 -NoBuild
```

Erzeugt werden:

- `build/reist-os.img`: 64-MiB-Raw-Datenträger für QEMU oder USB
- `build/vmware/reist-os/`: vollständiger, eigenständiger VMware-Ordner
- `.../START-VMWARE.cmd`: startet die fertige VM per Doppelklick
- `.../reist-os.vmx`: kann direkt in VMware Workstation geöffnet werden

Der Windows-Build verwendet native Programme. WSL wird nicht benötigt. Das
Skript findet installierte Werkzeuge im `PATH` und unterstützt zusätzlich die
portablen NASM-/Zig-/QEMU-Verzeichnisse unter `C:\tmp`.

## Tatsächlicher Bootpfad

```text
BIOS
  -> MBR Stage 1 (LBA 0)
  -> aktive RAW-Bootpartition (LBA 2048 bis 8191)
  -> geprüftes Manifest
  -> Stage 2 via BIOS EDD/INT 13h
  -> A20 + E820-Speicherkarte
  -> ELF32-PT_LOAD-Segmente des Kernels
  -> 32-Bit-Protected-Mode / Multiboot-1-Handoff
  -> Kernel und Shell
```

Auf demselben Datenträger liegt ab LBA 8192 eine 60-MiB-FAT32-Partition. Der
Kernel mountet sie beim Start als `hdd0`; sie enthält bereits `README.TXT` und
das gebaute Beispielprogramm `HELLO.PRG`. Beide Dateien sowie weitere über
`--data-file` angegebene 8.3-Dateien dürfen über mehrere FAT32-Cluster reichen.
Die Shell greift über einheitlich normalisierte VFS-Pfade darauf zu.

Stage 2 prüft ELF-Klasse, Architektur, Segmentgrenzen, Ladeadressen und Entry
Point. BSS-Bereiche werden vor dem Handoff genullt. Das Manifest schützt seine
Metadaten mit einer 32-Bit-Prüfsumme. Stage 2 prüft außerdem die CRC32 des
vollständigen Kernel-ELF und verweigert bei einer Abweichung den Start.

## QEMU

`run-windows.ps1` bootet das Raw-Image als primäre IDE-Platte. Der Schalter
`-Headless` leitet nur die Bootloader-Diagnose auf die Konsole um:

```powershell
.\scripts\run-windows.ps1 -NoBuild -Headless
```

Das äquivalente native Kommando lautet:

```powershell
qemu-system-i386 -accel tcg -machine pc -m 512M -boot c `
  -drive file=build/reist-os.img,format=raw,if=ide,index=0 `
  -device rtl8139,netdev=net0 -netdev user,id=net0 `
  -vga std -no-reboot -no-shutdown
```

## VMware Workstation

Die fertige VM befindet sich vollständig in
`build/vmware/reist-os/`. Am einfachsten wird dort
`START-VMWARE.cmd` doppelt angeklickt. Alternativ kann
`reist-os.vmx` direkt geöffnet werden.

Vorkonfiguriert sind:

- VMware-Hardwareversion 20, Legacy BIOS und feste HDD-Bootreihenfolge
- eine CPU, 512 MiB RAM und VGA ohne 3D-Beschleunigung
- persistente IDE-Festplatte mit Boot- und FAT32-Datenpartition
- Intel E1000 über die VMware-Bridge `VMnet0` mit direktem LAN-Zugriff und
  automatischer Verbindung beim Start
- COM1-Protokoll in `vmware-serial.log`
- deaktivierte Disketten-, USB- und Audiogeräte

Beim Start fordert der Kernel automatisch per DHCP eine eigene IPv4-Adresse
im LAN an. `getip` zeigt die Adresse, `net dhcp` wiederholt die Aushandlung und
`ping <LAN-IP>` prüft ARP/ICMP samt Gateway-Routing. Der aktuelle Minimal-Stack
unterstützt damit Ethernet, ARP, IPv4, ICMP und DHCP; DNS und TCP-basierte
Anwendungen wie HTTP oder SMB sind noch nicht implementiert.

Für eine manuell angelegte VM gelten dieselben Werte:

1. Eine VM vom Typ **Other / Other 32-bit** erstellen.
2. Firmware auf **BIOS**, nicht UEFI, stellen.
3. `build/reist-os.vmdk` als vorhandene **IDE**-Festplatte einbinden.
4. Einen E1000-Netzwerkadapter verwenden.
5. Von der Festplatte starten.

`VMnet0` verwendet normalerweise die automatische Bridge-Auswahl von VMware.
Bei mehreren Host-Adaptern sollte `VMnet0` im **Virtual Network Editor** fest
dem gewünschten Ethernet- oder WLAN-Adapter zugeordnet werden. Die VM erhält
eine eigene MAC- und per DHCP eine eigene LAN-Adresse; sie ist damit wie ein
separater Rechner im lokalen Netz sichtbar. Manche WLANs sperren zusätzliche
Clients oder verwenden Client-Isolation. In diesem Fall Ethernet verwenden
oder die Freigabe im Access Point anpassen.

Der Paketordner verwendet die übliche Kombination aus Descriptor-VMDK und
`-flat.vmdk`. Diese Dateien und die VMX müssen im selben Verzeichnis bleiben.

## Linux/macOS-Make-Ziele

Mit einer ELF32-fähigen Toolchain funktionieren auch:

```bash
make native-image TARGET=real_hw VIDEO=vga
make run-native TARGET=real_hw VIDEO=vga
```

Der frühere GRUB-basierte Datenträger und der ISO-Buildpfad wurden entfernt.
Alle aktuellen Build- und Laufziele verwenden dieses native Image.

## Reale Hardware

Das Image kann sektorweise auf einen Datenträger geschrieben werden. Das ist
destruktiv und sollte nur nach ausdrücklicher Kontrolle des Zielgeräts erfolgen.
Aktuell gelten folgende Grenzen:

- Legacy BIOS/CSM erforderlich; UEFI und Secure Boot werden noch nicht bedient.
- Nach dem Boot arbeitet der Kernel mit IDE/ATA. Moderne AHCI-/NVMe-Controller
  benötigen noch eigene Treiber oder einen SATA-Kompatibilitätsmodus.
- Der native Loader verwendet VGA-Textmodus. Ein eigener VBE/GOP-Pfad ist eine
  spätere Erweiterung.
