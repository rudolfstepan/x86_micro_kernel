# Optionales TAP-Netzwerk unter Linux/QEMU

Stand: 16. August 2026.

TAP ist ein zusätzlicher Entwicklungsweg für QEMU unter Linux. Er wird für
den nativen Windows-/VMware-Build nicht benötigt. Die VMware-Referenzmaschine
greift über E1000 und `VMnet0` direkt auf das LAN zu.

## Voraussetzungen und Sicherheitswirkung

TAP-Erstellung und Host-Netzkonfiguration benötigen Rootrechte. Die
Makefile-Ziele verändern `tap0` und weisen ihm `10.0.2.1/24` zu. Vor dem
Ausführen sollte geprüft werden, dass kein gleichnamiges produktives Interface
existiert und dieses Netz nicht mit der Hostkonfiguration kollidiert.

## Automatischer Testweg

```bash
sudo make run-net-tap-sudo
```

Dieses Ziel erzeugt `tap0`, setzt die Hostadresse und startet QEMU mit NE2000.
Für aktuelle Treibertests sind die adapterbezogenen Ziele meist klarer:

```bash
make run-e1000-tap
make run-rtl8139-tap
make run-ne2000-tap
```

Die Rezepte setzen voraus, dass QEMU, `ip`, `sudo` und die nötigen
Kernel-TUN/TAP-Funktionen installiert sind.

## Manuelle Vorbereitung

```bash
make setup-tap
make run-net-tap
```

Die aktuelle Makefile-Konfiguration verwendet:

```text
Interface: tap0
Host-IP:   10.0.2.1/24
Gastnetz:  10.0.2.0/24
```

Ein TAP-Interface allein stellt noch keinen DHCP-Server, kein NAT und keine
Bridge zum physischen LAN bereit. Für DHCP muss ein Hostdienst vorhanden sein;
alternativ wird der Gast statisch konfiguriert:

```text
C:\> IFCONFIG 10.0.2.2 255.255.255.0 10.0.2.1
C:\> PING 10.0.2.1
```

## Beobachtung

```bash
ip link show tap0
ip addr show tap0
sudo tcpdump -i tap0 -e -n
```

In der Gastshell:

```text
NET STATUS
NET INFO
ARP
PING 10.0.2.1
```

## Aufräumen

```bash
make cleanup-tap
```

Das entfernt nur das vom Makefile erwartete Interface `tap0`. Selbst angelegte
Bridges, Firewallregeln, DHCP- oder NAT-Konfigurationen müssen separat und
gezielt zurückgenommen werden.

## Fehlerdiagnose

- **Permission denied:** TAP-Verwaltung benötigt Rootrechte.
- **Keine Pakete:** Linkstatus, `tcpdump`, Gast-IP und Netzmaske prüfen.
- **DHCP bleibt ohne Antwort:** Ein nacktes TAP besitzt keinen DHCP-Server.
- **Host erreichbar, LAN nicht:** Routing/Forwarding oder eine Bridge fehlt.
- **Falscher Treiber:** QEMU-Gerät und gewähltes `run-*-tap`-Ziel abgleichen.

Der vollständige Protokoll- und VMware-Stand ist in [NETWORK.md](NETWORK.md)
dokumentiert.
