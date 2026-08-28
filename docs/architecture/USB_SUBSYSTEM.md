# USB-Subsystemvertrag

Stand: 28. August 2026.

## Referenzmodell

Der begrenzte xHCI-HID-Bootpfad folgt dem Control-Transfer-Modell aus USB 2.0
Kapitel 8.5 und 9, der HID 1.11 Klassenspezifikation sowie xHCI 1.2. Ein
Control-Transfer besteht aus Setup Stage, optionaler Data Stage und der
terminalen Status Stage. Das acht Byte große Setup-Paket wird mit
`bmRequestType`, `bRequest`, `wValue`, `wIndex` und `wLength` vollständig
diagnostiziert.

Der vorhandene Ring-0-xHCI-Treiber ist begrenzte Migrationsschuld. Er verwendet
nur statische DMA-Objekte, feste Ringgrößen und monotone Deadlines. Eine breitere
USB-Unterstützung oder ein neuer komplexer Gerätetreiber gehört in einen
generationengebundenen Ring-3-Dienst hinter validierter IRQ-, MMIO- und
DMA-Mediation.

Nach dem Controller-Reset sammelt der Treiber auf jedem xHCI-Controller für
höchstens 500 ms die sichtbar werdenden Root-Port-Verbindungen. Das Fenster
war zuvor irrtümlich nur nach Intel-Companion-Routing aktiv. Auf anderer
Hardware konnte dadurch die Erkennung vom Zeitbedarf einer unabhängigen
i8042-/PS/2-Initialisierung abhängen. Die Korrektur ändert weder Control-
Transfer-Erfolgskriterien noch die feste Kandidatenzahl.

## Verbindliche HID-Boot-Requests

Nach erfolgreicher Descriptorprüfung und `CONFIGURE_ENDPOINT` müssen mindestens
folgende Zero-Data-Control-Transfers vollständig abgeschlossen sein:

- `SET_CONFIGURATION`: Typ `0x00`, Request `0x09`, Konfigurationswert in
  `wValue`, `wIndex=0`, `wLength=0`;
- `SET_PROTOCOL`: Typ `0x21`, Request `0x0B`, Boot-Protokoll in `wValue`,
  Interface in `wIndex`, `wLength=0`.

Ein Stall, Timeout, falscher Eventzeiger, Restdaten oder anderer Completion Code
lässt diese Requests fail-closed scheitern. Der Kandidat wird nicht publiziert;
sein Slot wird mit dem begrenzten `DISABLE_SLOT`-Pfad freigegeben. Es gibt keinen
Retry auf einem ungeklärten oder nicht eingefriedeten EP0-Zustand.

## Begrenzte Completion-Code-13-Kompatibilität

xHCI Completion Code 13 bezeichnet ein Short Packet. Ein Short-Packet-Event
beweist den Abschluss eines Transfer Descriptors nur, wenn sein TRB-Zeiger auf
die letzte TRB des TD zeigt. REIST akzeptiert deshalb genau einen eng begrenzten
Controller-Sonderfall:

- der USB-Request ist host-to-device und verlangt `wLength=0`;
- es existiert keine Data-TRB;
- das Event zeigt exakt auf die terminale Status-TRB;
- die gemeldete Restlänge ist exakt null.

Nur in dieser Kombination ist der angeforderte zero-length Transfer vollständig
belegt. Der Treiber markiert die Kompatibilitätsannahme dauerhaft in den
Diagnosen. Jeder Short auf einer Data-TRB, bei einer Länge ungleich null, mit
Restlänge oder einem nichtterminalen Zeiger bleibt ein Fehler. Insbesondere
werden weder `SET_CONFIGURATION` noch `SET_PROTOCOL` pauschal erfolgreich
gemeldet.

## Append-only Diagnose-ABI

Version 7 der Ring-3-USB-Diagnose erhält den vollständigen Version-6-Präfix und
hängt folgende Felder an: Request-Typ, Request, Wert, Index, Länge, Completion
Code, Restlänge, Eventstufe und Flags. Die interne xHCI-Diagnose Version 6
verwendet denselben angehängten Control-Datensatz.

Das Setup-Paket wird vor dem Veröffentlichen der Setup-TRB und vor dem
Doorbell-Schreibzugriff gespeichert. Ein fehlgeschlagener oder über die enge
Completion-Code-13-Regel angenommener Transfer bleibt abrufbar. Normale spätere
Control-Transfers und ein danach erfolgreich enumerierter Maus- oder
Tastaturkandidat überschreiben diese Anomalie nicht. `USBINFO` und die
Rescue-Shell zeigen die Felder ohne Zugriff auf flüchtige DMA-Ringe an. Da kein
Image den Desktop automatisch startet, bleiben die Bootzeilen in der Ring-3-
Shell sichtbar, bis der Benutzer ausdrücklich `DESKTOP` aufruft.

## Nachweisgrenze

Hosttests prüfen ABI-Präfix, Setup-Persistenz und die exakte Short-Packet-Grenze.
VMware weist die virtuelle xHCI-Maus als Nichtregression nach. Der physische
ASUS-Nachtest mit dem neu erzeugten `real_hw`-Image bestätigt sowohl die
USB-Boot-Tastatur am USB-3/xHCI-Port als auch den paged `DMESG`-Zugriff. Damit
ist der gemeldete Pfad `failure=set-configuration`, `config-len=59`, `cc=13`
für diese konkrete Controller-/Gerätekombination behoben. Das ist keine breite
xHCI-Hardwarefreigabe.

Zusätzlich bestätigte der Benutzer das abschließend erzeugte VMware-Paket in
einem manuellen Lauf als funktionsfähig. Der letzte Host-Automationsversuch
hatte die VM nicht gestartet und liefert daher keine zusätzliche
Gastlaufzeitevidenz; maßgeblich für die automatische Nichtregression bleibt der
zuvor bestandene begrenzte RFB-Lauf.

Die vollständigen frühen xHCI-Ausgaben bleiben zusätzlich im festen Kernel-
Logring erhalten und können später aus der Ring-3-Shell mit dem seitenweisen
`DMESG` gelesen werden. Die Diagnose hängt damit nicht mehr vom 80x25-VGA-
Scrollback ab; der Logring erteilt weder VFS- noch Geräteautorität.
