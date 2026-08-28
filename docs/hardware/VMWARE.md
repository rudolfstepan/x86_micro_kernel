# VMware Workstation

Stand: 27. August 2026.

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
| CPU/RAM | 4 vCPUs, 512 MiB |
| Festplatte | persistente monolithic-flat SATA-VMDK |
| Grafik | VMware SVGA, 3D aus, standardmäßig VGA-Text |
| Skalierung | Free Stretch (`gui.stretchGuestMode=fullfill`) |
| Eingabe | virtuelle PS/2-Tastatur und virtuelle USB-HID-Maus über xHCI |
| Netzwerk | Intel E1000 an VMware NAT-DHCP |
| Audio | virtuelles HDA `15ad:1977`, Slot 34, Start verbunden |
| Seriell | COM1-Ausgabe nach `vmware-serial.log` |
| Deaktiviert | physisches HID-Passthrough, VMware Tools |

Descriptor-VMDK, `-flat.vmdk` und VMX müssen im Paketordner zusammenbleiben.
Das Raw-Image enthält MBR, Bootpartition und die FAT32-Systempartition
`X86 SYSTEM`. Der Kernel erkennt die virtuelle Platte nativ über AHCI.

Die generierte VMX aktiviert VMwares „Free Stretch“. Das Gastbild füllt damit
das VMware-Fenster auch ohne VMware Tools und ohne Änderung der vom Gast
gesetzten Auflösung; das Seitenverhältnis wird dabei nicht erzwungen.

Die generierte VMX setzt `usb.generic.allowHID=FALSE`, enthält keine
`usb.autoConnect`- oder HID-Quirk-Regel und bietet keine Buildoption für
physisches Tastatur-/Maus-Passthrough. VMware darf deshalb unter keinen
Umständen die Host-Eingabegeräte übernehmen. Tastatureingabe läuft über die
virtuelle PS/2-Schnittstelle, Mausdiagnosen über VMwares virtuelles USB-HID.

VMware kann weder Intels realen `8086:8c31`-Controller noch die NVIDIA-Karte
`10de:1280` emulieren. Legacy-BIOS, SATA, ein xHCI-Pfad, EHCI-Präsenz und vier
vCPUs bilden die automatisierte Annäherung; Intel-spezifische Register-,
Timing- und physische HID-Fehler bleiben ein Nachweis auf dem ASUS-Board.

VMwares HDA-Modell implementiert das PCI-2.3-`INTx Disable`-Bit nicht. Sein
exaktes Profil verwendet daher den begrenzten Legacy-PIC-Fallback des
Device-Domain-Mediators. Die generierte VMX fixiert HDA auf PCI-Slot 34; IRQ 9
wird bis zum Ring-3-Acknowledge maskiert. Diese Ausnahme gilt nicht für andere
PCI-Audiogeräte und verändert weder DMA- noch Bus-Mastering-Prüfungen.

Der Runtime-Grafikpfad akzeptiert ausschließlich die VMware-SVGA-PCI-IDs
`15ad:0405` (SVGA II) und `15ad:0710` (Legacy SVGA) mit passendem I/O-BAR und
erfolgreicher Register-ID-Aushandlung. Ein anderer VMware-Displayadapter darf
nicht in den Legacy-VBE-Runtime-Thunk fallen: Er wird vor jeder Modusänderung
mit `VBE runtime transition suppressed` abgelehnt. Das verhindert einen
VMware-Monitor-Panic durch eine Real-Mode-Ausführung auf einer inkompatiblen
virtuellen Grafikgeneration.

## Funktionstest

Kein VMware-Image startet den Desktop automatisch. Der automatisierte
Desktop-Eingabenachweis wartet zuerst auf die Ring-3-Shell, sendet dort den
ausdrücklichen Befehl `DESKTOP` und prüft erst danach die Maus. Er startet nur
bei vollständig leerem VMware-Laufzustand ohne offene Workstation-UI und
verwendet die generierte virtuelle Basic-Mouse ohne physisches
HID-Passthrough. Die VMX bindet ihren
RFB-3.8-Eingabekanal ausschließlich an `127.0.0.1:5909`; der Runner prüft den
Port vor dem Start und sendet darüber begrenzte Standard-PointerEvents. Er
öffnet die exakte Paket-VM über Workstations sichtbaren `-x`-Pfad und beendet
ausschließlich die danach ermittelte VMX-PID sowie seine eigene Workstation-
PID. Start, Markerwartezeit und Aufräumen bleiben auf 30, 75 beziehungsweise
10 Sekunden begrenzt. Die Eingabephase verwendet höchstens zwölf Versuche:

```powershell
.\scripts\test-reist-runtime.ps1 -Mode vmware-mouse
```

Er verlangt in dieser Reihenfolge xHCI-HID-Bereitschaft, die SMP-
Schedulerfreigabe, den Start der Userspace-Shell, den erst danach explizit
eingegebenen Desktopstart, `DESKTOP_OK`, `DESKTOP_EXPLORER_OK` und
`DESKTOP_MOUSE_OK`. Er bricht zusätzlich ab, falls ein Desktopmarker bereits
vor der Shell erscheint. Der Legacy-PIC-xHCI-IRQ und der
Produktionscompositor
bleiben auf CPU 0; andere getrennt geprüfte Dienste und Treiber dürfen die
verfügbaren APs weiterhin verwenden. Die frühere AP-Compositor-Abnahme bleibt
historische SMP-Evidenz, ist wegen des gemessenen Retained-Paint-IPC-Aufwands
aber keine Vorgabe des normalen VMware-Profils mehr. Panic oder Compositor-
Degradation vor dem Mausmarker brechen den Lauf geschlossen ab.

Am 28. August 2026 bestand der R5.2x-Kandidat diesen Lauf in 14 Sekunden nach
dem VMware-VGA-Paketbuild. Damit ist die virtuelle xHCI-Maus gegen die engere
Control-Short-Packet-Regel abgesichert; die getrennte physische USB-Abnahme
wird dadurch nicht ersetzt.

Nach dem finalen Imagebuild scheiterte ein zusätzlicher Host-Automationslauf
noch vor dem Start von `vmware-vmx`. Das war kein Gastfehler. Der Benutzer
startete das generierte Paket anschließend manuell und bestätigte den
fehlerfreien VMware-Betrieb. Diese manuelle Bestätigung ersetzt keinen
automatisierten Marker-Nachweis, schließt aber die praktische Image-Abnahme ab.

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
C:\> WAVPLAY
C:\> SOUNDPLAYER /usr/share/sounds/440hz.wav
```

`AUDIOINFO` muss den überwachten Intel-HDA-Ring-3-Backendstatus melden.
`AUDIOTEST` spielt einen begrenzten 440-Hz-Testton und muss mit
`Audio test complete.` enden. Ein erfolgreicher VM-Boot allein ist noch kein
Nachweis hörbarer Hostausgabe. `WAVPLAY` verwendet zusätzlich die unveränderte
PCM-Testdatei `/usr/share/sounds/440hz.wav` und muss mit
`WAV playback complete.` enden. Dieser manuelle Hörtest ist der maßgebliche
VMware-Nachweis; die VM wird durch den automatischen Build nicht gestartet.
Am 20. August 2026 wurde die Wiedergabe mit hörbarem 440-Hz-Ton und passendem
Pegel bestätigt. Der HDA-Treiber aktiviert dazu neben DAC und Pin auch den
tatsächlich verbundenen Eingang eines dazwischenliegenden Mixers oder
Selectors; das PCM selbst bleibt ohne digitales Clipping.
Die grafische Variante verwendet denselben WAV-Loader und Audiopfad; sie kann
im Desktop durch Doppelklick auf `440hz.wav` oder mit obigem Shell-Aufruf
gestartet werden. Der VMware-Hörtest bleibt eine manuelle Prüfung.

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

`START-VMWARE.cmd` beginnt automatisch eine neue Diagnosesitzung und entfernt
vor dem Start ausschließlich das zuvor erzeugte `vmware-serial.log`. Dadurch
erscheint der VMware-Dialog zum Ersetzen oder Anhängen der Datei nicht.

## Fehlerdiagnose

- kein Boot: Legacy BIOS, `bios.hddOrder = "sata0:0"` und zusammengehörige
  VMDK-Dateien prüfen
- kein Root: AHCI-Probe, MBR-Children und eindeutiges `X86 SYSTEM` prüfen
- keine Tastatur: VM-Fenster fokussieren; PS/2 ist erforderlich, VMware Tools
  nicht
- keine Maus: `usb_xhci.present`, `mouse.vusb.enable`,
  `mouse.vusb.useBasicMouse` und die Meldung `USB: xHCI HID ready` mit einem
  `mouse-port` im seriellen Log prüfen; fehlt `DESKTOP_MOUSE_OK`, den
  Compositor-Pointerpfad prüfen
- Explorer meldet `/` könne nicht geöffnet werden: `DESKTOP_EXPLORER_OK`
  prüfen. Der überwachte Compositor benötigt die eng begrenzten VFS-Shadow-
  Submit-/Collect-/Cancel-Rechte, aber keine Raw-Storage-Autorität.
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
  `AUDIOINFO` und die HDA-/Audio-Service-Meldungen im seriellen Log prüfen.
  Erwartet werden `legacy INTx PIC fallback`, `HDA_PROFILE pci=15AD:1977`
  und `REIST_AUDIO SERVICE_READY`; `HDA_REJECTED result=-5` ist ein Fehler.
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
