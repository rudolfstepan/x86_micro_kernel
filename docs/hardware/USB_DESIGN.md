# USB-/xHCI-Status und Design

Stand: 20. August 2026.

USB bleibt experimentell, ist aber kein reiner Probe-Platzhalter mehr. Der
aktuelle xHCI-Pfad enumeriert begrenzt HID-Boot-Tastaturen und -Mäuse, richtet
Interrupt-IN-Endpunkte ein und speist beide Geräte in die vorhandenen
Eingabeschnittstellen ein. Daraus folgt weder eine allgemeine USB-Freigabe noch
Unterstützung für Mass Storage, Audio oder beliebige Composite-Geräte.

## Implementierter Pfad

```text
PCI 0c:03:30
  -> validierter xHCI-MMIO-BAR und BIOS/OS-Handoff
  -> statische DCBAA-, Command-, Event-, EP0- und Interrupt-Ringe
  -> begrenzter Root-Port-Reset und Descriptor-Transfer
  -> HID Boot Protocol keyboard/mouse
  -> gemeinsame Keyboard-Queue beziehungsweise feste Mausreport-Queue
  -> Shell oder Desktop
```

Vorhanden sind:

- endliche Controller-, Port- und Transferdeadlines;
- statische, ausgerichtete DMA-Strukturen und spätes Bus-Mastering;
- Intel-Port-Routing vom EHCI-Begleitcontroller mit Readback;
- generationgebundene Attach-/Detach- und Reportpfade;
- kurze, endlich wartende SMP-/IRQ-Locks fuer den veroeffentlichten xHCI-
  Eventconsumer, Diagnosesnapshots und beide HID-Generationen; Enumeration und
  deadlinegebundene Control-Transfers bleiben davor BSP-only;
- gleichzeitige Tastatur- und Mausendpunkte an unterschiedlichen Root-Ports;
- persistente Diagnose über `/sbin/usbinfo.prg` in der normalen Ring-3-Shell;
- Hosttests für HID-Tastatur, Maus, Ring-/TRB-Regeln und Imagepaketierung;
- parallele Hosttests fuer HID-Tastatur und -Maus sowie QEMUs manuelles
  `make run-usb`-Profil mit `qemu-xhci` und `usb-kbd`.

## VMware- und Hardwaregrenze

Die VMware-Referenz aktiviert einen virtuellen xHCI-Controller und genau eine
virtuelle HID-Maus. Die Tastatur bleibt virtuell PS/2. Physisches Host-HID-
Passthrough ist mit `usb.generic.allowHID = "FALSE"` und
`usb.generic.allowLastHID = "FALSE"` verboten, damit der Host immer bedienbar
bleibt.

Auf dem ASUS H81M-K wurden eine einfache USB-Boot-Tastatur und die USB-Maus
erfolgreich verwendet. Das AULA/BY-Tech-Composite-Keyboard `258A:010C` mit
LED-Steuerung und Lautstärkedrehregler wird zwar erkannt, liefert aber noch
keine verwendbare Tastatureingabe. Dieser gerätespezifische Fehler bleibt offen
und darf nicht als allgemeines xHCI- oder HID-Funktionieren umgedeutet werden.

## Offene Grenzen

- kein allgemeiner HID-Report-Descriptorparser und keine Nicht-Boot-Layouts;
- keine vollständige Initialisierung vendor-spezifischer Composite-HID-Geräte;
- kein beliebig tiefer Hub-, Hotplug- und Reconnect-Lebenszyklus;
- kein EHCI/OHCI/UHCI-Backend;
- kein USB-Mass-Storage, USB Audio oder isochroner Transferpfad;
- keine IOMMU-basierte DMA-Isolation und keine breite Controller-/Gerätematrix;
- kein automatisierter QEMU-Gasttest, der ein echtes Shellkommando über
  `usb-kbd` eingibt; `make run-usb` ist derzeit ein manueller Lauf.

## Diagnose

```text
C:\> USBINFO
```

Die Ausgabe trennt Controllerzustand, verbundene Root-Ports,
Enumerationsversuche, ausgewählte Tastatur-/Mausendpunkte, Reportzähler,
abgewiesene Reports und den letzten Completion Code. Bootmeldungen sind nicht
die einzige Evidenzquelle; der Status bleibt nach dem schnellen Boot abrufbar.

`drivers/usb/usb_recommendations.md` ist eine historische Entwurfsnotiz. Der
offene Arbeitsumfang und die Sicherheitsregeln stehen zusätzlich im
[USB-Tastatur-Plan](../development/USB_KEYBOARD_IMPLEMENTATION_PLAN.md).
