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
Die bisherige
Domäne ist noch keine unabhängige Kernel-, CPU- oder RAM-Fehlerdomäne.

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
