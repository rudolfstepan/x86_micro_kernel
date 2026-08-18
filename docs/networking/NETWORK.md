# Netzwerkstack

Stand: 18. August 2026.

Der verifizierte VMware-Weg verwendet einen Intel-E1000-Adapter und VMware
NAT-DHCP. Der überwachte Ring-3-Dienst `REIST.PRG` führt die
begrenzten DHCP-/Netzwerkentscheidungen aus; der Kernel vermittelt Hardware,
Capabilities, validierte Übergaben und Commit. Frühere NE2000-QEMU-
Loopback-Probleme sind nicht der aktuelle Referenzzustand.

## Architektur

```text
Ring-3: ifconfig / ping / netstat / udp / nslookup / nc / httpd
                |
überwachter REIST.PRG-Netzdienst
                |
begrenzte FD-Sockets + DNS + TCP-Zustandsautomat
                |
Ethernet + ARP + IPv4 + ICMP + UDP + DHCP + TCP
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
- prozessgebundene UDP- und TCP-Socketdeskriptoren mit Cleanup bei Prozessende
- UDP `bind`/`sendto`/`recvfrom`, vier Datagramme je Queue, 512 Byte je Paket
  und eine begrenzte `sendto`-ARP-Wartezeit von maximal zehn Sekunden
- DNS-A-/CNAME-Auflösung mit begrenzten Kompressionszeigern und vier Cacheplätzen
- aktiver und passiver TCP-Verbindungsaufbau mit `listen`/`accept`, kleinem
  Backlog, ACK-/Sequenzprüfung über 32-Bit-Wrap, begrenzter Retransmission,
  Empfangsfenster sowie aktivem/passivem Close
- begrenzter HTTP/1.0-Server für `/htdocs`: `GET`, `HEAD`, statische Dateien
  bis 4096 Byte und Directory-Listings bis 32 Einträge beziehungsweise 1024 Byte
- E1000 in der generierten VMware-VM und RTL8168/8111G auf dem ASUS H81M-K

Ein Ping auf die eigene konfigurierte IPv4-Adresse wird lokal beantwortet und
benötigt weder ARP noch einen Ethernet-Loopback. Fremde oder vor einer
validierten Lease eintreffende ICMP-Pakete werden kanonisch verworfen, ohne den
überwachten Netzwerkdienst neu zu starten.

Noch nicht implementiert sind IPv6, Routing zwischen mehreren Gastinterfaces,
TLS/HTTPS sowie Anwendungen wie FTP oder SMB. Die DNS-/TCP-Schicht und der
HTTP-Server sind hostseitig sowie in deterministischen QEMU-Gasttests gegen
lokale DNS-/TCP-Testpeers verifiziert.

## Shellbefehle

Die folgenden Befehle sind eigenständige Ring-3-Programme unter `/sbin`:

```text
C:\> ifconfig
C:\> ping 192.168.1.1
C:\> netstat
C:\> udp send 192.168.1.20 9000 9001 hello
C:\> udp recv 9001 3000
C:\> nslookup example.test
C:\> nc example.test 80 "GET / HTTP/1.0"
C:\> httpd 8080
```

`udp send`, `udp recv`, DNS, `nc` und `httpd` verwenden monotone, begrenzte
Deadlines. `httpd [port]` läuft standardmäßig bis `Strg+C`; ein Leerlauf- oder
Client-Timeout beendet den Listener nicht. Das optionale Argument
`httpd [port] [requests]` begrenzt ausschließlich Testläufe auf höchstens 32
erfolgreiche Anfragen.
`netstat` zeigt
neben dem Interfacezustand auch aktive UDP-/TCP-Sockets, Queuefüllung, Drops
und TCP-Retransmissionen. Die bisherigen Shell-Built-ins bleiben aus
Kompatibilitätsgründen verfügbar.

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
ethernet0.connectionType = "nat"
ethernet0.virtualDev = "e1000"
```

Damit ist DHCP ohne zusätzliche Hostkonfiguration verfügbar. Für einen
direkten Teilnehmer im physischen LAN kann die VMX-Datei auf
`ethernet0.connectionType = "custom"` und `ethernet0.vnet = "VMnet0"`
umgestellt werden. Dann muss VMnet0 im VMware Virtual Network Editor dem
gewünschten Adapter zugeordnet werden; WLAN-Client-Isolation oder eine
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
Dateifreigabe-Stack. Der deterministische QEMU-Test belegt jedoch DHCP, ARP,
aktiven und passiven TCP-Handshake, zwölf aufeinanderfolgende HTTP-Verbindungen
mit Directory-Listing, Datenübertragung und Close sowie DNS-A-Auflösung über UDP.

## Fehlerdiagnose

1. `NET STATUS` – ist ein Treiber aktiv und Link vorhanden?
2. `NET DHCP` – ist eine überwachte Lease aktiv?
3. `GETIP` – sind IP, Netzmaske und Gateway gesetzt?
4. `ARP` – wird der Gateway-Nachbar aufgelöst?
5. `PING <gateway>` – funktioniert ICMP im lokalen Netz?
6. Bei VMware `VMnet0` und E1000-Konfiguration kontrollieren.
7. Bei früher Initialisierung `vmware-serial.log` auswerten.
