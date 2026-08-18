# REIST USB-Tastatur – Umsetzungsplan

Stand: 18. August 2026

## Ziel

REIST soll USB-Tastaturen über xHCI erkennen und Eingaben über denselben
Eingabepfad wie PS/2 an Shell und Konsolenprogramme liefern. Die
Implementierung bleibt statisch, begrenzt und fail-closed. Ein nicht
unterstütztes oder fehlerhaftes USB-Gerät darf weder die PS/2-Tastatur noch
den restlichen Bootvorgang beeinträchtigen.

## Zielarchitektur

```text
PCI / BIOS
    -> xHCI-Hostcontroller (Ring 0)
    -> Root-Port und USB-Geräte-Enumeration
    -> USB-HID-Boot-Keyboard (Klasse 03, Subklasse 01, Protokoll 01)
    -> 8-Byte-Interrupt-Reports
    -> semantische Tastaturereignisse
    -> gemeinsame PS/2-/USB-Eingabequeue
    -> getchar(), Shell, History und Cursorsteuerung
```

Die HID-Schicht übersetzt keine ASCII-Zeichen selbst. Sie liefert Set-1-
ähnliche semantische Scancodes an `kb_submit_key_event()`. Dadurch bleiben
Modifier, Caps-/NumLock, ANSI-Cursorfolgen, Ctrl-Kombinationen und die
bestehende Shell an einer Stelle konsistent.

## Ausgangslage

## Umsetzungsstand

Der erste Implementierungsschritt ist im Quellbaum umgesetzt:

- xHCI besitzt statische DCBAA-, Command-, Event-, EP0- und Interrupt-Ringe.
- BIOS-Ownership, Controller-Reset, Root-Port-Reset und begrenzte
  Descriptor-Enumeration sind angebunden.
- USB-HID-Boot-Keyboards werden anhand von Klasse, Subklasse, Protokoll und
  Interrupt-IN-Endpunkt geprüft.
- HID-Reports werden generationgebunden in die gemeinsame PS/2-/USB-
  Eingabequeue weitergeleitet.
- `make run-usb` stellt das QEMU-Profil mit `qemu-xhci` und `usb-kbd` bereit.
- Der HID-Hosttest und die PS/2-Quelltests sind grün; die drei geänderten
  Treiber kompilieren mit den Kernel-Warn-/Fehlerflags.

Der vollständige Kernel-/QEMU-Nachweis steht noch aus, weil in der aktuellen
Arbeitsumgebung weder `nasm` noch `qemu-system-i386` verfügbar ist. Deshalb
wird an dieser Stelle keine QEMU-Funktion behauptet.

Bereits vorhanden:

- `drivers/char/kb.c` besitzt eine gemeinsame semantische Eingabeschnittstelle.
- `drivers/char/kb.h` veröffentlicht `kb_submit_key_event()` für weitere
  Eingabetransporte.
- `drivers/usb/hid_kb.c` verarbeitet begrenzte HID-Boot-Reports mit
  Generation, Rollover-Prüfung und Freigabe gehaltener Tasten.
- `drivers/usb/hid_kb.h` definiert Attach-, Detach- und Report-Verträge.
- `kernel/init/kernel.c` ruft `usb_init()` nach der PCI-Erkennung auf.
- Das Makefile nimmt die USB-Quellen bereits in den Kernel-Build auf.

Noch offen:

- Der aktuelle xHCI-Treiber ist nur ein Probe-Platzhalter.
- USB-Core, Hub-Lifecycle und Hotplug sind noch nicht vollständig verbunden.
- Es gibt noch keinen reproduzierbaren QEMU-Test mit `qemu-xhci` und
  `usb-kbd`.

## Arbeitspakete

### 1. xHCI-Grundinitialisierung

- PCI-Klasse `0c/03`, Programming Interface `30` prüfen.
- 32-Bit-kompatiblen MMIO-BAR validieren und über
  `map_mmio_region()` abbilden.
- BIOS/OS-Ownership über die xHCI-Extended-Capability begrenzt übernehmen.
- Controller stoppen, resetten und `CNR` mit monotone Deadline prüfen.
- DCBAA, Command Ring, Event Ring, ERST und Endpoint-Rings statisch und
  ausgerichtet bereitstellen.
- Bus-Mastering erst nach erfolgreicher DMA-Strukturprüfung aktivieren.
- Bei jedem Fehler Controller deaktiviert lassen und `-1` zurückgeben.

Abnahmekriterium: QEMU ohne xHCI bleibt bootfähig; QEMU mit xHCI meldet
Controller, IRQ und erfolgreich vorbereitete Ringe.

### 2. Begrenzte USB-Enumeration

- Root-Portstatus lesen und nur einen Port im MVP bearbeiten.
- Verbindung, Reset, Geschwindigkeit und Port-Enable mit festen Grenzen prüfen.
- Slot aktivieren, Device Address setzen und Device Descriptor lesen.
- Configuration Descriptor ausschließlich innerhalb eines festen Puffers
  und einer festen Maximallänge analysieren.
- Nur vollständige HID-Boot-Keyboard-Interfaces akzeptieren:
  `class=03`, `subclass=01`, `protocol=01`.
- Interrupt-IN-Endpunkt mit gültiger Adresse, Paketgröße und Intervall
  validieren.
- `SET_CONFIGURATION` und `SET_PROTOCOL(boot)` mit begrenzten Control-
  Transfers ausführen.

Abnahmekriterium: Ein USB-Massenspeicher oder eine Maus wird abgewiesen,
ohne dass Speicher außerhalb der festen Deskriptorgrenzen gelesen wird.

### 3. HID-Reportpfad

- Einen festen 8-Byte-Boot-Report pro Gerät verwenden.
- Report-Cycle und Gerätegeneration gegen veraltete IRQs absichern.
- Rollover-Reports (`1..3`) verwerfen.
- Neue Usages als Press und entfernte Usages als Release veröffentlichen.
- Beim Disconnect alle noch gehaltenen Modifier und Tasten idempotent lösen.
- Interrupt-Transfers nach jedem gültigen Report erneut posten.
- Keine VFS-Zugriffe, Heap-Allokation oder unbounded loops im IRQ-Handler.

Abnahmekriterium: USB-`a`, Shift-`a`, Enter, Backspace, Cursor-Tasten und
Ctrl-Kombinationen erzeugen dieselben Queue-Daten wie PS/2.

### 4. Eingabe-Lifecycle und Hotplug

- Gemeinsame Queue als alleinige Übergabestelle beibehalten.
- USB-Disconnect im IRQ nur markieren und Generation invalidieren.
- Re-Enumeration ausschließlich im begrenzten Task-/Polling-Kontext starten.
- Während einer Enumeration keine neue Geräteidentität veröffentlichen.
- PS/2 unabhängig weiter betreiben, wenn xHCI ausfällt.
- Reconnect erst nach vollständigem Descriptor-, Endpoint- und Report-Setup
  akzeptieren.

Abnahmekriterium: Abziehen einer USB-Tastatur beendet keine Shell und setzt
keine alten Reports nach einem Reconnect fort.

### 5. Tests und Nachweis

Hosttests:

- HID-Report-Parser mit Press, Release, Modifier, Rollover und Stale-
  Generation.
- Detach setzt alle gehaltenen Tasten frei.
- Quelltest für statische Ringgrößen, Alignment, Deadlines und spätes
  Bus-Mastering.

QEMU-Test:

```text
-device qemu-xhci,id=xhci
-device usb-kbd,bus=xhci.0
```

Der Lauf muss folgende Reihenfolge nachweisen:

1. Bootmarker und USB/xHCI-Erkennung.
2. USB-HID-Keyboard-Enumeration.
3. Eingabe eines Shell-Kommandos über den USB-Pfad.
4. Cursor-Up/Down in der Shell-History.
5. Kontrolliertes Ende des QEMU-Laufs.

Die Tests dürfen keine physische USB-/xHCI-Kompatibilität behaupten. Für das
ASUS-H81M-K bleibt eine eigene Hardwareabnahme erforderlich.

## Sicherheits- und Stabilitätsregeln

- Alle Ringe, Kontexte und Reportpuffer sind feste statische Speicherbereiche.
- Jede Warteoperation kombiniert monotone Deadline und Poll-Limit.
- DMA-Adressen müssen unterhalb der vom Treiber unterstützten 4-GiB-Grenze
  liegen und korrekt ausgerichtet sein.
- Unbekannte TRBs, Descriptor-Typen, Endpunkte und HID-Usages werden verworfen.
- IRQs dürfen nur Status quittieren, Reports weiterleiten und Transfers
  erneut posten.
- Fehler führen zu Geräte-Detach oder Controller-Fence, nie zu partieller
  Veröffentlichung eines Geräts.
- Die bestehende PS/2-Eingabe bleibt der unabhängige Fallback.

## Bewusste Abgrenzung

Der erste Abschluss umfasst nur xHCI und USB-HID-Boot-Keyboard. EHCI, OHCI,
UHCI, USB-Mass-Storage, HID-Report-Deskriptoren für Nicht-Boot-Layouts,
SMP-/IOMMU-DMA-Isolation und USB-Hubs hinter externen Hubs sind separate
Arbeitspakete.

## Definition of Done

- xHCI initialisiert in QEMU mit festen Ringen und funktionierendem IRQ.
- Ein USB-Boot-Keyboard kann die REIST-Shell bedienen.
- PS/2 funktioniert unverändert ohne USB-Gerät.
- Fehlerhafte, fremde oder entfernte USB-Geräte werden begrenzt abgewiesen.
- Hosttests und der QEMU-USB-Smoke sind reproduzierbar grün.
- Architektur-, Projektstatus- und Testdokumentation spiegeln den tatsächlich
  verifizierten Stand wider.
