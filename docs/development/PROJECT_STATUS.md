# Projektstatus

Stand: 14. August 2026. Diese Datei beschreibt den aktuell verifizierten
Zustand. Ältere Sitzungs- und Diagnoseberichte im Repository sind historische
Arbeitsdokumente.

## Verifiziert

- Nativer BIOS-/MBR-Start ohne GRUB, ISO oder WSL
- Zweistufiger Loader mit EDD-Lesezugriff, E820-Speicherkarte, A20,
  ELF32-Segmentprüfung, BSS-Nullung und Kernel-CRC32
- Direkt startbares VMware-Paket mit einer IDE-VMDK
- VGA-Textshell mit DOS-artigem Prompt und Tastatur-Zeilenbearbeitung
- Gemeinsame kanonische Pfadauflösung für alle Shell-Dateioperationen
- VFS-Adapter und Hosttests für FAT12, FAT32 und EXT2
- FAT32-Datenpartition mit mehrclustrigen Dateien und wachsender Root-Kette
- E1000 unter VMware, DHCP-Adresse im gebridgten LAN, ARP und ICMP/Ping
- Hostseitige MYPR-Toolchain für externe C- und `.S`-Quellen
- Loaderprüfung des Program-Headers, der Größen, Basis und Einstiegspunkte
- Hostseitige Regressionstests für Image, VFS, Pfade und Toolchain
- Ring-3-Prozesse mit eigenen Seitentabellen und geprüften User-Pointern
- Schreibbarer FAT12-VFS mit Dateien, Verzeichnissen und FAT-Spiegelung
- REIST IPC v1 mit begrenzten Queues, endlichen Deadlines, geschützten
  Primary/Shadow-Metadaten und explizit abschwächender Capability-Delegation
- Reservierte Supervisor-Restartkapazität: ein Taskslot, ein Prozessslot und
  ein Admission-Budget von 32 physischen Frames

## Aktueller REIST-Ausbaustand

S0.3b-1 bis S0.3b-6 sind umgesetzt und durch Host-, VMware-Referenzbuild- und
QEMU-Gasttests abgenommen. Normale Prozesse können die reservierte
Restartkapazität nicht verbrauchen; ausschließlich der explizite
Supervisor-Spawn darf sie verwenden. Prozesse besitzen versionierte
Syscallprofile; die Ring-3-Probedomäne ist default-deny und erhält nur einen
begrenzten Lifecycle-/IPC-Satz. Der Supervisor erkennt Crash, Hang und
ungültige Antworten, sperrt die Probe, widerruft ihre generationsgebundenen
Ressourcen und reintegriert erst nach Selbsttest und neuem Endpoint. Die reale
QEMU-Matrix belegt dies bei LAPIC, PIT, Watchdog sowie 32–1024 MiB RAM parallel
zu einem unabhängigen Gasttest. S0.3c-1 stellt nun zusätzlich einen echten
begrenzten Ring-3-Diagnosedienst bereit. Ein generation-sicheres
Service-Connect-Gate delegiert nur `SEND|RECEIVE`; GTEST bestätigt den
Request/Reply `DIAG -> REIST_DIAG_OK` nach vollständiger Recovery. S0.3c-2
ergänzt die separate Freigabe delegierter Client-Capabilities: GTEST prüft
Freigabe, stale Handle, erneute Verbindung und einen zweiten Request/Reply ohne
Quota-Leck. Als nächstes folgt die erste funktionale Migration ohne parallelen
Ring-0-Datenpfad. S0.3c-3a hat dafür bereits einen festen, heapfreien
Ethernet-Header-Parser in den Ring-3-Dienst verschoben und weist ARP-
Klassifikation im Gast nach. Als nächstes wird dieser Parser über einen
begrenzten Frame-Handoff an den echten RX-Pfad angebunden. S0.3c-3b hat diesen
Handoff nun implementiert: genau 14 Headerbytes, feste Queue, keine Allokation,
kein Warten und generation-sicheres Peer-Routing. S0.3c-3c weist die Übergabe
nun mit einem echten RTL8139-Gastlauf nach: Der gesunde Dienst fordert einen
festen, auf 250 ms begrenzten Gateway-ARP-Probe an und bestätigt erst den vom
NIC zurückkehrenden `NETR`-Header mit `NETWORK_HANDOFF_OK`. Ohne NIC bleibt der
Pfad definiert degradiert. Als nächstes wird die ausgewählte parallele
Kernelklassifikation entfernt. Der begrenzte Netzwerk-Bottom-Half läuft nun
garantiert im 10-ms-Supervisor-Worker statt nur opportunistisch in Shellpfaden.
S0.3c-3d hat die parallele Verarbeitung für den übernommenen ARP-Probe-Reply
entfernt: Erfolgreiche IPC-Übernahme bedeutet ausschließlich Ring 3; bei nicht
übernommenen Frames bleibt der Kernelpfad fail-closed zuständig. Pending-Zustand
wird beim Fence generationssicher verworfen.
S0.3c-3e weist diese Grenze nun per realer Fault-Injection nach: Nach einem
echten Handoff crasht der Dienst bei ausstehender Probe, der alte Kanal bricht
ab und GTEST verbindet sich begrenzt mit der neuen Generation. Diagnose und
unabhängiger Gastfortschritt erreichen danach wieder `TEST_OK`.
S0.3c-3f ergänzt einen deterministischen Vier-Slot-IPC-Drucktest. Ein echter
ARP-Reply fällt bei voller Dienstqueue einmalig zum Kernelpfad zurück; danach
werden alle vier Lastnachrichten beantwortet und `NETWORK_PRESSURE_OK`
erreicht.
S0.3c-3g korreliert Dienstanfragen zusätzlich mit einer festen 32-Bit-ID und
der Endpointgeneration. Eine absichtlich um eins verfälschte Antwort wird im
Gast verworfen; erst die nachfolgende korrekte Diagnoseantwort erzeugt
`SERVICE_CORRELATION_OK`.
S0.3c-3h übergibt den vollständigen festen Ethernet/ARP-Header und verschiebt
dessen Strukturvalidierung in den Ring-3-Dienst. Der Gast injiziert vor dem
gültigen Frame eine falsche ARP-Adresslänge und verlangt
`ARP_VALIDATION_OK` ohne Antwort auf die ungültige Eingabe.
S0.3c-3i friert Gateway-IP sowie lokale IP/MAC beim Probe-Start ein und lässt
Ring 3 nur eine semantisch dazu passende ARP-Antwort akzeptieren. Eine
verfälschte Gateway-Identität wird verworfen, bevor `ARP_IDENTITY_OK` den
gültigen Pfad bestätigt.
S0.3c-3j ersetzt die boolesche Probe-Autorität durch eine monotone ID. Die
append-only v2-API ist Syscall 60; Ingress, Dienst und Supervisor bestätigen
dieselbe ID genau einmal über `PROBE_ID_OK`. Erschöpfung und Recovery widerrufen
fail-closed, während Syscall 59 kompatibel bleibt.
S0.3c-3k begrenzt jede Probe-Autorität zusätzlich auf eine absolute monotone
250-ms-Deadline. Eine hostgetestete feste Zustandsmaschine übernimmt
Einmalverbrauch, Ablauf, Sättigung und ID-Erschöpfung; der Supervisor-Worker
räumt abgelaufene IDs unabhängig vom RX-Pfad auf.
S0.3c-3l zählt Deadline-Ablauf, Queue-Fallback und semantische Ablehnung
getrennt und saturierend. Hosttests bestätigen die drei Pfade sowie
`UINT32_MAX`; alle Updates bleiben außerhalb des Hard-IRQ-Kontexts.
S0.3c-3m stellt diese Werte über die read-only, versionierte 24-Byte-ABI von
Syscall 61 bereit. GTEST prüft ungültige Pointer, ABI-Header und den real
gestiegenen Queue-Fallback-Zähler mit `NETWORK_STATS_OK`.
S0.3c-3n hält den Snapshot redundant im Critical-Object-Format. Ein beschädigter
CRC einer Kopie wird beim Lesen korrigiert; sind beide Kopien ungültig, liefert
Syscall 61 `-84` und veröffentlicht keine möglicherweise erfundenen Werte.
S0.3c-3o schützt auch Probe-ID, Deadline und ID-Sequenz als versioniertes
Critical Object. Einzelkopiefehler werden rekonstruiert; Doppelkorruption
verhindert Begin/Take und zwingt die aktive Domäne in Isolation.
S0.3c-3p fasst zugestellte ID, Gateway, lokale IP und MAC in einem weiteren
Critical Object zusammen. Nur ein vollständig validierter Snapshot wird an
Ring 3 gesendet oder bestätigt; Doppelkorruption isoliert die Domäne.
S0.3c-3q schützt außerdem PID/Generation, Endpoint, Supervisor-Handle,
Health/Fence, Launch-Zähler und Rate-Limit-Zeit als einen Control-Snapshot.
Direkte ungeschützte Laufzeitentscheidungen existieren in der Probe nicht mehr.
S0.3c-3r bindet Control, Probe-Autorität und Identitätskontext an dieselbe
monotone Transaktionsepoche. Drei einzeln gültige Snapshots verschiedener
Probes werden vor Handoff, Ablauf oder Bestätigung fail-closed abgelehnt.
S0.3c-4a erlaubt dem Ring-3-Dienst erstmals eine reale, eng begrenzte
Netzwerkzustandsänderung: Syscall 62 übernimmt nur die epochengebundene,
bytegenau mit dem geschützten Ingress übereinstimmende ARP-Bindung.
S0.3c-4b hält diese Bindung nun getrennt vom Legacy-Cache in 32 statischen,
redundant geschützten Slots. Quellepoche und monotone 30-s-Deadline gehören
zur selben validierten Nutzlast. Ablauf bleibt als Sperreintrag erhalten;
Einzelkorruption wird rekonstruiert, Doppelkorruption und Kapazitätserschöpfung
enden fail-closed ohne Legacy-Fallback oder Verdrängung. Host-, Paket-, normaler
Gast- und echter RTL8139-Smoke sind grün.
S0.3c-4c ist nun ebenfalls umgesetzt: Jeder Slot trägt PID und konkrete
Prozessgeneration. Der Fence widerruft nur exakt passende Einträge vor dem
Prozessende;
der echte RTL8139-Gast beobachtet `ARP_BINDINGS_REVOKED` vor erfolgreicher
Recovery. Ein auf einmal pro Sekunde begrenzter 32-Slot-Scrub repariert
Einzelkopien, publiziert Ablauf und eskaliert Doppelkorruption. Die
hardwareunabhängige Frühinitialisierung hält denselben Vertrag auch ohne NIC.
S0.3c-5a entfernt die passive Gateway-Vertrauensentscheidung aus Ring 0.
Weder ARP-Absender noch IPv4-Quell-MACs dürfen die konfigurierte Gateway-IP in
den Legacy-Cache schreiben; beim Setzen einer manuellen oder per DHCP
erhaltenen Route wird eine vorherige Altbindung gelöscht. Damit kann nur noch
der geschützte Ring-3-Mediator Gateway-Autorität publizieren. Als nächstes
vermittelt S0.3c-5b auch lokale ARP-Auflösung und Antwortentscheidung über den
überwachten Dienst. 5b1 ist umgesetzt: Lokale Requests passieren einen
festen Ring-3-Parser und eine 250-ms-, generation- und requestgebundene
Einmalautorität; Syscall 63 löst erst nach geschütztem Abgleich eine Antwort
aus. Der frühere Ring-0-Responder ist entfernt und Fehler fallen nicht auf ihn
zurück. 5b2 migriert noch die ausgehende lokale Auflösung und ergänzt den
deterministisch injizierten echten RX-Request-Gastnachweis.
Die bisherige Domäne ist noch keine unabhängige Kernel-, CPU- oder
RAM-Fehlerdomäne.

Der zuletzt ausgeführte vollständige Windows-Build bootete in VMware bis zum
Prompt `C:\>`, mountete `hdd0` als `/`, initialisierte E1000 und erhielt per
DHCP eine LAN-Adresse. Das eingebettete `HELLO.PRG` wurde bytegenau gegen das
Buildartefakt geprüft.

## Shell und Dateisystem

Die frühere Inkonsistenz, bei der `DIR` eine Datei sah, `OPEN`/`TYPE` sie aber
nicht fand, ist beseitigt. Alle betroffenen Befehle verwenden VFS und denselben
Resolver:

```text
DIR/LS   CD/CHDIR   TYPE/OPEN
MD/MKDIR RD/RMDIR  DEL/ERASE/RMFILE
COPY     RUN/EXEC
```

Akzeptiert werden `/` und `\`, relative und absolute Pfade, `.` und `..`,
DOS-Laufwerke (`C:\...`), native Namen (`hdd0:/...`) und die ältere
VFS-Schreibweise (`/hdd0/...`). FAT-Dateinamen werden ohne Beachtung der
Groß-/Kleinschreibung gesucht.

## Netzwerk

Aktuell implementiert:

- gemeinsame `netdev`-Schnittstelle
- Treiber für E1000, RTL8139 und NE2000; VMware verwendet E1000
- Ethernet-Frame-Verarbeitung
- ARP-Cache und ARP-Auflösung
- IPv4
- ICMP Echo Request/Reply
- DHCP-Client mit automatischem Versuch beim Start

Nicht als fertig dokumentiert werden DNS, TCP, eine UDP-Socket-API, IPv6,
HTTP, SMB oder ein allgemeines Userspace-Netzwerk-API.

## Externe Programme

Zig/Clang und LLD übersetzen freestanding i386-C/Assembly in ein geprüftes
MYPR-Image. SDK und Startup-Code stellen eine kleine Syscall-API bereit. Das
Beispiel testet Code, Read-only-Daten, initialisierte Daten, BSS und Exit.

PRG-Tasks laufen unprivilegiert in Ring 3 und besitzen eigene Seitentabellen.
Syscalls kopieren und prüfen Zeiger über die User-/Kernel-Grenze. Der
Kernelbereich bleibt für die für Interrupts und Syscalls notwendigen
Kernelpfade in den Prozessadressräumen abgebildet, ist für Usercode aber nicht
zugreifbar.

## Experimentell oder offen

- Pipes, Signale und eine allgemeinere Prozess-/IPC-Schnittstelle
- UEFI-Boot
- lange FAT-Dateinamen im erzeugten Image
- TCP/DNS/IPv6 und Anwendungen oberhalb des Minimalstacks
- umfassend getesteter USB-/xHCI-Betrieb
- Framebuffer als gleichwertiger Standard zur VGA-Textausgabe
- reproduzierbarer Laufzeittest jedes Shellbefehls innerhalb der VMware-GUI

## Qualitätsnachweis

Der Referenzbefehl ist:

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
```

Zusätzliche reine Hosttests:

```powershell
python -m unittest discover -s test -p "test_*.py" -v
```

Tests beweisen die jeweils geprüften Invarianten, ersetzen aber keine
Speicherisolation, Hardwarematrix oder Langzeit-/Fuzztests.
