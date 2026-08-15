# Netzwerkstack

Der verifizierte VMware-Weg verwendet einen Intel-E1000-Adapter und eine
Bridge zum lokalen Netz. Der Kernel fordert beim Start per DHCP eine eigene
IPv4-Konfiguration an. Frühere NE2000-QEMU-Loopback-Probleme sind nicht der
aktuelle Referenzzustand.

## Architektur

```text
Shell: GETIP / IFCONFIG / PING / ARP / NET
                |
Ethernet + ARP + IPv4 + ICMP + DHCP
                |
       gemeinsame netdev-Schnittstelle
                |
       E1000 / RTL8139 / NE2000
                |
          PCI-Netzwerkadapter
```

`drivers/net/netdev.c` entkoppelt den Protokollstack vom aktiven Treiber.
Empfangene Pakete werden gepollt und verarbeitet; IRQ-Handler geben keine
permanenten Paket-Debugzeilen mehr auf dem VGA-Terminal aus.

## Unterstützt

- PCI-Erkennung der vorhandenen Netzwerktreiber
- Ethernet-Senden und -Empfangen
- ARP-Anfragen, Antworten und Cache
- IPv4-Paketverarbeitung
- ICMP Echo Request/Reply
- DHCP Discover/Offer/Request/ACK
- statische IPv4-Konfiguration über die Shell
- E1000 in der generierten VMware-VM

Nicht implementiert sind derzeit DNS, TCP, ein allgemeines UDP-Socket-API,
IPv6, Routing zwischen mehreren Gastinterfaces sowie Anwendungen wie HTTP,
FTP oder SMB.

## Shellbefehle

Status und DHCP:

```text
C:\> NET STATUS
C:\> NET INFO
C:\> NET DHCP
C:\> GETIP
```

`NET DHCP` zeigt nur noch den Zustand der durch `REIST.PRG` überwachten Lease.
DISCOVER, OFFER, REQUEST, ACK sowie Renewal/Rebind laufen automatisch über den
Ring-3-Netzwerkdienst; die Kernel-Shell startet keinen parallelen DHCP-Client.

Statische Konfiguration:

```text
C:\> IFCONFIG 192.168.1.50 255.255.255.0 192.168.1.1
```

Erreichbarkeit:

```text
C:\> PING 192.168.1.1
C:\> ARP
C:\> ARP SCAN 192.168.1.20
```

Diagnose:

```text
C:\> NET DEBUG
C:\> NET RECV
C:\> NET LISTEN 10
C:\> NET SEND
```

`NET DEBUG` ist treiberspezifisch und vor allem für E1000 vorgesehen.

## VMware-LAN

Das erzeugte Paket setzt:

```text
ethernet0.connectionType = "custom"
ethernet0.vnet = "VMnet0"
ethernet0.virtualDev = "e1000"
```

Damit ist der Gast ein eigener Teilnehmer im LAN. Bei mehreren
Hostschnittstellen muss `VMnet0` im VMware Virtual Network Editor dem
gewünschten Adapter zugeordnet werden. WLAN-Client-Isolation oder eine
Zugangskontrolle für zusätzliche MAC-Adressen kann Bridge-Verkehr verhindern.

## QEMU

Der native QEMU-Weg in `scripts/run-windows.ps1` verwendet aktuell RTL8139
mit User-Mode-Networking. Die Makefile-Ziele bieten daneben E1000, RTL8139 und
NE2000 sowie optionale TAP-Varianten:

```bash
make run-native TARGET=qemu
make run-e1000
make run-rtl8139
make run-e1000-tap
```

Die TAP-Ziele sind Linux-spezifisch und benötigen Rootrechte sowie eine
passende Host-Bridge/Weiterleitung. Sie gehören nicht zum nativen
Windows-/VMware-Referenzweg.

## Grenzen der Aussage „LAN funktioniert“

Verifiziert sind Initialisierung, DHCP-Lease und das Erreichen des
Shell-Prompts im gebridgten VMware-Netz. Ob ein konkreter Zielrechner auf Ping
antwortet, hängt auch von Netzsegment, Gateway, Firewall und WLAN-Regeln ab.
Der Kernel implementiert noch keinen vollständigen Internet- oder
Dateifreigabe-Stack.

## Fehlerdiagnose

1. `NET STATUS` – ist ein Treiber aktiv und Link vorhanden?
2. `NET DHCP` – ist eine überwachte Lease aktiv?
3. `GETIP` – sind IP, Netzmaske und Gateway gesetzt?
4. `ARP` – wird der Gateway-Nachbar aufgelöst?
5. `PING <gateway>` – funktioniert ICMP im lokalen Netz?
6. Bei VMware `VMnet0` und E1000-Konfiguration kontrollieren.
7. Bei früher Initialisierung `vmware-serial.log` auswerten.
