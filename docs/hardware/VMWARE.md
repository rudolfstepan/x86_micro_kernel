# VMware Workstation

Stand: 20. August 2026.

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
| Eingabe | virtuelle PS/2-Tastatur und virtuelle USB-HID-Maus über xHCI |
| Netzwerk | Intel E1000 an VMware NAT-DHCP |
| Audio | virtuelles Intel HDA (`hdaudio`), Start verbunden |
| Seriell | COM1-Ausgabe nach `vmware-serial.log` |
| Deaktiviert | physisches HID-Passthrough, VMware Tools |

Descriptor-VMDK, `-flat.vmdk` und VMX müssen im Paketordner zusammenbleiben.
Das Raw-Image enthält MBR, Bootpartition und die FAT32-Systempartition
`X86 SYSTEM`. Der Kernel erkennt die virtuelle Platte nativ über AHCI.

Die generierte VMX setzt `usb.generic.allowHID=FALSE`, enthält keine
`usb.autoConnect`- oder HID-Quirk-Regel und bietet keine Buildoption für
physisches Tastatur-/Maus-Passthrough. VMware darf deshalb unter keinen
Umständen die Host-Eingabegeräte übernehmen. Tastatureingabe läuft über die
virtuelle PS/2-Schnittstelle, Mausdiagnosen über VMwares virtuelles USB-HID.

VMware kann weder Intels realen `8086:8c31`-Controller noch die NVIDIA-Karte
`10de:1280` emulieren. Legacy-BIOS, SATA, ein xHCI-Pfad, EHCI-Präsenz und eine
einzelne CPU bilden die sicher testbare Annäherung; Intel-spezifische Register-,
Timing- und physische HID-Fehler bleiben ein Nachweis auf dem ASUS-Board.

Der Runtime-Grafikpfad akzeptiert ausschließlich die VMware-SVGA-PCI-IDs
`15ad:0405` (SVGA II) und `15ad:0710` (Legacy SVGA) mit passendem I/O-BAR und
erfolgreicher Register-ID-Aushandlung. Ein anderer VMware-Displayadapter darf
nicht in den Legacy-VBE-Runtime-Thunk fallen: Er wird vor jeder Modusänderung
mit `VBE runtime transition suppressed` abgelehnt. Das verhindert einen
VMware-Monitor-Panic durch eine Real-Mode-Ausführung auf einer inkompatiblen
virtuellen Grafikgeneration.

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

Der Audiofunktionstest verwendet ausschließlich die virtuelle Soundkarte; er
übernimmt keine Host-Maus oder -Tastatur:

```text
C:\> AUDIOINFO
C:\> AUDIOTEST
```

`AUDIOINFO` muss den überwachten Intel-HDA-Ring-3-Backendstatus melden.
`AUDIOTEST` spielt einen begrenzten 440-Hz-Testton und muss mit
`Audio test complete.` enden. Ein erfolgreicher VM-Boot allein ist noch kein
Nachweis hörbarer Hostausgabe.

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
- keine Maus: `usb_xhci.present`, `mouse.vusb.enable` und die Meldung
  `USB: xHCI HID mouse ready` im seriellen Log prüfen
- `connected` zeigt den virtuellen Maus-Port, aber `failure=port-reset`:
  keine Host-HID- oder VMX-Ausweichregel ergänzen. Der xHCI-Treiber muss die
  begrenzten 20 ms nach Port-Power und 10 ms nach USB2-Reset über die monotone
  PIT-Zeit vollständig einhalten; ein CPU-abhängiger Poll-Zähler ist dafür
  unzulässig.
- VMware meldet beim Start von `desktop` oder `guidemo` einen vCPU-Fehler:
  `vmware-serial.log` muss nun entweder einen validierten SVGA-Erfolg oder
  `VBE runtime transition suppressed` enthalten; ein VBE-Aufruf nach einer
  erkannten, nicht unterstützten VMware-Grafik-ID ist ein Kernelregressionsfehler.
- kein LAN: `e1000`, Verbindungsstatus und VMnet0-Zuordnung prüfen
- kein Audio: `sound.present`, `sound.virtualDev = "hdaudio"`, danach
  `AUDIOINFO` und die HDA-/Audio-Service-Meldungen im seriellen Log prüfen
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
