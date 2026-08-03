# USB-/xHCI-Status und Design

USB ist im aktuellen Kernel ein experimenteller Entwicklungsbereich. Der
Build nimmt die Quellen unter `drivers/usb/` auf und `kernel_main()` ruft
`usb_init()` nach der PCI-Erkennung auf. Die generierte VMware-Referenzmaschine
deaktiviert USB bewusst und verwendet PS/2-Tastatur sowie IDE-Datenträger.

## Vorhandene Bausteine

- USB-Core-Strukturen und Initialisierung
- PCI-Probing für USB-Hostcontroller
- xHCI-Quellen und MMIO-/Controllergrundlagen
- Hub- und HID-Tastaturquellen
- Einbindung aller `drivers/usb/*.c` in den Kernelbuild

Diese Existenz ist nicht mit einem vollständig verifizierten USB-Stack
gleichzusetzen. Insbesondere werden Mass Storage, beliebige Hubs,
Fehlererholung und eine breite Controller-/Gerätematrix nicht als fertig
dokumentiert.

## Zielarchitektur

```text
PCI-Erkennung
   -> Host Controller Driver (zunächst xHCI)
      -> USB-Core: Geräte, Endpunkte und Transfers
         -> Root-/externe Hubs
            -> Klassentreiber
               -> HID-Tastatur
               -> Mass Storage / Blockgerät / VFS
```

## Technische Anforderungen

- MMIO-BAR korrekt aktivieren und mappen
- PCI Bus Mastering für DMA setzen
- xHCI-Strukturen physisch zusammenhängend und korrekt ausrichten
- Command-, Transfer- und Event-Ringe mit Cycle Bits verwalten
- Controller- und Transfer-Timeouts statt unendlicher Pollschleifen
- Port Reset, Address Device und Deskriptorabfragen spezifikationsgemäß
- Interruptpfad sowie kontrollierter Polling-Fallback
- USB-Fehler nicht als ungebremste VGA-Ausgabe in die Shell schreiben

## Verifikationsstufen

1. PCI-Controller mit BAR und IRQ eindeutig erkennen.
2. Controllerreset und Ringinitialisierung mit Timeouts testen.
3. Root-Port-Status und Geräteanschluss erkennen.
4. Device-/Configuration-Deskriptor lesen und Adresse setzen.
5. HID-Bootkeyboard in die bestehende Eingabequeue einspeisen.
6. Bulk-Only-/SCSI-Mass-Storage als Blockgerät anbinden.
7. FAT über VFS mounten und Fehler-/Abziehpfade testen.

Jede Stufe benötigt reproduzierbare serielle Logs und nach Möglichkeit einen
hostseitigen Test für parser- oder datenstrukturbezogene Teile.

## QEMU-Entwicklung

Für gezielte Tests kann QEMU um einen xHCI-Controller und passende Geräte
erweitert werden. Diese Optionen sind derzeit nicht Bestandteil des nativen
Windows-Startskripts oder des fertigen VMware-Pakets. Änderungen an der
Referenz-VM sollten USB erst aktivieren, wenn Boot und Eingabe mit den
gewählten virtuellen Geräten reproduzierbar getestet sind.

## Offene Grenzen

- kein als stabil freigegebener xHCI-End-to-End-Pfad
- kein dokumentierter USB-Mass-Storage-Mount
- kein Hotplug-/Disconnect-Lebenszyklus
- keine IOMMU- oder DMA-Isolation
- keine EHCI/OHCI/UHCI-Kompatibilitätszusage

Die Datei `drivers/usb/usb_recommendations.md` ist eine historische
Entwurfsnotiz. Für den normalen VMware-Betrieb ist USB nicht erforderlich.
