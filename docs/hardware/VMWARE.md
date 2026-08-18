# VMware Workstation

Stand: 16. August 2026.

Der native Windows-Build erzeugt eine vollständige Legacy-BIOS-VM. ISO, GRUB
und manuelles Anlegen einer VM sind nicht erforderlich.

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
.\build\vmware\reist-os\START-VMWARE.cmd
```

Alternativ wird `build/vmware/reist-os/reist-os.vmx` in VMware Workstation
geöffnet. Vor einem Neubau muss die VM ausgeschaltet sein; der Build verweigert
das Überschreiben einer über `vmrun` laufenden Paket-VM.

## Referenzkonfiguration

| Einstellung | Wert |
|---|---|
| Firmware | Legacy BIOS |
| Boot | `sata0:0` |
| CPU/RAM | 1 vCPU, 512 MiB |
| Festplatte | persistente monolithic-flat SATA-VMDK |
| Grafik | VMware SVGA, 3D aus, standardmäßig VGA-Text |
| Eingabe | virtuelle PS/2-Tastatur |
| Netzwerk | Intel E1000 an VMware NAT-DHCP |
| Seriell | COM1-Ausgabe nach `vmware-serial.log` |
| Deaktiviert | USB, EHCI, xHCI, Audio, VMware Tools |

Descriptor-VMDK, `-flat.vmdk` und VMX müssen im Paketordner zusammenbleiben.
Das Raw-Image enthält MBR, Bootpartition und die FAT32-Systempartition
`X86 SYSTEM`. Der Kernel erkennt die virtuelle Platte nativ über AHCI.

## Funktionstest

```text
C:\> DRIVES
C:\> DIR
C:\> TYPE README.TXT
C:\> RUN HELLO.PRG
C:\> GTEST
```

`HELLO.PRG` meldet `USERSPACE-E2E-OK`. `GTEST` prüft Ring-3-, VFS- und
Recoverypfade und muss bis `TEST_OK` laufen.

## Netzwerk

Die Standard-VM verwendet VMware NAT mit dem VMware-DHCP-Server. Dadurch ist
die DHCP-Prüfung unabhängig von der Bridge-Konfiguration des Hosts.
`REIST.PRG` verwaltet den überwachten DHCP-Lease. Für eine direkte Präsenz im
physischen LAN kann `ethernet0.connectionType = "custom"` und zusätzlich
`ethernet0.vnet = "VMnet0"` gesetzt werden. Dann muss VMnet0 im Virtual
Network Editor dem gewünschten Adapter zugeordnet sein; WLAN-Client-Isolation
oder das Verbot zusätzlicher MAC-Adressen kann Bridging verhindern.

```text
C:\> NET STATUS
C:\> NET DHCP
C:\> GETIP
```

## Serielles Protokoll

`vmware-serial.log` enthält die COM1-Diagnose für Boot, Treiber, REIST-Marker
und Panic-Kontext. COM1 ist nicht die interaktive Shell-Eingabe. Bei einem
Panic sind Phase, Komponente, Operation, Subject, Result, Details, Sequenz und
Build-ID zu sichern.

## Fehlerdiagnose

- kein Boot: Legacy BIOS, `bios.hddOrder = "sata0:0"` und zusammengehörige
  VMDK-Dateien prüfen
- kein Root: AHCI-Probe, MBR-Children und eindeutiges `X86 SYSTEM` prüfen
- keine Tastatur: VM-Fenster fokussieren; PS/2 ist erforderlich, VMware Tools
  nicht
- kein LAN: `e1000`, Verbindungsstatus und VMnet0-Zuordnung prüfen
- frühe Panic: `vmware-serial.log` und erweiterten Panic-Screen vergleichen

Eine manuelle Ersatz-VM muss die VMDK als SATA-Festplatte einbinden. Eine IDE-
Platte ist nur noch ein separater QEMU-/Kompatibilitäts-Regressionspfad.

## Physisches USB-Diskettenlaufwerk

Ein vom Host als `A:` bereitgestelltes USB-FDD kann VMware als klassisches
FDC-Gerät durchreichen; der Gast benötigt dafür keinen USB-Stack:

```powershell
.\scripts\configure-vmware-fdd.ps1 -Mode Physical -Drive A:
```

Zurück zum Image:

```powershell
.\scripts\configure-vmware-fdd.ps1 -Mode Image
```

Die VM muss dabei ausgeschaltet sein, und das Hostlaufwerk darf nicht zugleich
von Explorer oder einem anderen Prozess geöffnet sein. Beim FDD-Boot bleibt
die SATA-Platte nur Fallback.
