# VMware Workstation

Der native Windows-Build erzeugt eine vollständige VM, die direkt geöffnet
und gestartet werden kann. Eine manuelle VM-Erstellung, ein ISO und GRUB sind
nicht erforderlich.

## Erzeugen und starten

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
.\build\vmware\reist-os\START-VMWARE.cmd
```

Alternativ wird diese Datei in VMware Workstation geöffnet:

```text
build/vmware/reist-os/reist-os.vmx
```

Die VM muss vor einem erneuten Build ausgeschaltet sein. Das Buildskript
erkennt eine über `vmrun` laufende paketierte VM und bricht ab, bevor deren
Festplatte überschrieben werden könnte.

## Mitgelieferte Konfiguration

| Einstellung | Wert |
|---|---|
| Firmware | Legacy BIOS |
| Bootreihenfolge | erste IDE-Festplatte |
| Hardwareversion | 20 |
| CPU | 1 vCPU, 1 Kern pro Socket |
| RAM | 512 MiB |
| Grafik | VMware SVGA, 3D deaktiviert, Kernel standardmäßig VGA-Text |
| Festplatte | persistente monolithic-flat IDE-VMDK |
| Netzwerk | Intel E1000, Custom `VMnet0`, beim Start verbunden |
| Seriell | COM1 in `vmware-serial.log` |
| Nicht benötigt | Diskette, Audio, USB, VMware Tools |

Descriptor-VMDK, `-flat.vmdk` und VMX müssen im selben Paketordner bleiben.
Das Raw-Image enthält den eigenen MBR-/Stage-2-Bootloader und eine mountbare
FAT32-Datenpartition.

## LAN-Zugriff

`VMnet0` ist eine VMware-Bridge. Die VM besitzt eine eigene virtuelle MAC und
bezieht beim Boot automatisch eine IPv4-Konfiguration vom DHCP-Server des
lokalen Netzes.

```text
C:\> GETIP
C:\> NET STATUS
C:\> NET DHCP
C:\> PING 192.168.1.1
```

Die Gatewayadresse kann vom Beispiel abweichen und wird in der DHCP-Ausgabe
angezeigt. Der Gast unterstützt derzeit Ethernet, ARP, IPv4, ICMP und DHCP.
Ein Ping vom Host zum Gast hängt zusätzlich von Host-Firewall, Access Point
und deren ICMP-Regeln ab.

Wenn mehrere physische Hostadapter existieren, im **Virtual Network Editor**
`VMnet0` fest dem gewünschten Ethernet- oder WLAN-Adapter zuordnen. Manche
WLANs erlauben keine zusätzliche MAC-Adresse oder aktivieren
Client-Isolation. In diesem Fall ist kabelgebundenes Ethernet der verlässlichste
Bridge-Test.

## Shell- und Programmtest

```text
C:\> DIR
C:\> TYPE README.TXT
C:\> RUN HELLO.PRG
```

`DIR` und `TYPE` müssen dieselbe Datei über den gemeinsamen VFS-Pfad sehen.
Das Beispielprogramm meldet `USERSPACE-E2E-OK`.

## Serielles Protokoll

Der VMware-Ordner enthält nach dem Start `vmware-serial.log`. Es protokolliert
die COM1-Ausgabe und ist die erste Anlaufstelle bei frühem Bootfehler,
Kernelpanic oder fehlender VGA-Ausgabe. Das Bootloader-Debugport-Protokoll und
die spätere COM1-Ausgabe sind nicht mit einer interaktiven VMware-Konsole zu
verwechseln.

## Fehlerdiagnose

### VM startet nicht

- VMware Workstation muss `vmrun.exe` enthalten; andernfalls die VMX manuell öffnen.
- BIOS statt UEFI verwenden.
- VMDK nicht von ihrem `-flat.vmdk`-Extent trennen.
- Prüfen, ob die VM noch läuft oder gesperrte `.lck`-Verzeichnisse besitzt.

### Kein Prompt

- `vmware-serial.log` auf Bootloader- oder Kernelmeldungen prüfen.
- VM-Konfiguration unverändert mit einer IDE-Platte starten.
- 3D-Beschleunigung deaktiviert lassen.
- Das Paket mit `-RunTests` neu bauen.

### Keine Tastatur

- In das VM-Fenster klicken, damit VMware die Eingabe einfängt.
- PS/2-Standardkonfiguration beibehalten; VMware Tools werden nicht benötigt.
- Die Shell muss bereits den Prompt anzeigen.

### Kein Netzwerk

- In der VMX muss `ethernet0.virtualDev = "e1000"` stehen.
- Adapter muss verbunden und `VMnet0` dem richtigen Hostadapter zugeordnet sein.
- Mit `NET DHCP` den überwachten Lease-Zustand prüfen, danach `GETIP`
  ausführen. DHCP wird beim Boot automatisch durch `REIST.PRG` ausgehandelt.
- Bei WLAN-Problemen Ethernet oder testweise eine andere VMware-Netzart verwenden.

## Manuelle Ersatzkonfiguration

Falls das Paket bewusst neu angelegt werden soll:

1. Gasttyp **Other / Other 32-bit** wählen.
2. Legacy BIOS, eine vCPU und mindestens 512 MiB RAM einstellen.
3. `build/reist-os.vmdk` als vorhandene IDE-Platte einbinden.
4. Intel E1000 an `VMnet0` konfigurieren.
5. Festplatte als erstes Bootgerät wählen.

Die generierte VMX bleibt jedoch die Referenzkonfiguration.

## Physisches USB-Diskettenlaufwerk

Ein unter Windows als `A:` eingebundenes USB-FDD kann über VMwares physisches
Floppy-Backing verwendet werden. Die VM muss dazu vollständig ausgeschaltet
sein:

```powershell
.\scripts\configure-vmware-fdd.ps1 -Mode Physical -Drive A:
```

Danach startet die normale `reist-os.vmx` zuerst von der eingelegten
physischen Diskette und nur als Fallback von der IDE-Festplatte. VMware stellt
das Hostlaufwerk dem Gast als klassischen Floppy-Controller bereit. Deshalb
bleiben `usb.present`, EHCI und xHCI deaktiviert; der noch experimentelle
USB-Mass-Storage-Pfad des Kernels wird hierfür nicht benötigt.

`build-windows.ps1 -Target vmware` trägt ein vorhandenes physisches Laufwerk
`A:` nach dem Neuaufbau automatisch wieder in beide VMX-Dateien ein. Der Build
ersetzt die Zuordnung daher nicht mehr unbemerkt durch das Image. Mit
`-VmwareFloppy Image` kann das Image erzwungen werden; ein anderes Laufwerk
wird beispielsweise mit `-VmwareFloppy Physical -FloppyDrive B:` gewählt.

Zurück zum mitgelieferten Image geht es ebenfalls nur bei ausgeschalteter VM:

```powershell
.\scripts\configure-vmware-fdd.ps1 -Mode Image
```

Explorer-Fenster und andere Hostprogramme dürfen während des VM-Betriebs nicht
gleichzeitig auf `A:` zugreifen. Das Laufwerk kann immer nur exklusiv vom Host
oder von VMware verwendet werden.
