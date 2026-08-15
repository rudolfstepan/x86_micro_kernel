# Projektstatus

Stand: 15. August 2026. Diese Datei beschreibt den aktuell verifizierten
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
zurück. S0.3c-5b2a ist ebenfalls abgenommen: Ein echter, über den QEMU-Hub in
RTL8139 injizierter Broadcast-Request erreicht die geschützte Supervisor-
Queue, wird vom Ring-3-Dienst validiert und erzeugt genau die autorisierte
Antwort. Der Lauf verlangt `ARP_REQUEST_QUEUED`, `ARP_REPLY_MEDIATED` und
anschließend `TEST_OK`; verlorene/coalesced RX-Interrupts werden durch
begrenztes Ring-Polling aufgefangen. Dabei wurden Prozessgeneration und
Request-ID als unabhängige Namensräume korrigiert sowie der echte
Ethernet-Broadcast-Offset im Parser abgesichert. S0.3c-5b2b ist nun ebenfalls
abgenommen: Cache-Misses laufen über eine feste `NETA`-Nachricht, einen
geschützten 250-ms-Einmalvertrag und den ausschließlich dem Ring-3-Dienst
erlaubten Syscall 64. Der reale RTL8139-Lauf beobachtet den ausgesendeten
Request für `10.0.2.99` am QEMU-Socket. Es gibt keinen direkten Ring-0-Fallback
mehr. S0.3c-5c vermittelt nun auch ICMP Echo: Der Kernel übergibt höchstens
32 Payloadbytes als festes `NETI`-Objekt; ein geschützter Requestkontext und
eine generationgebundene 250-ms-Einmalautorität erlauben ausschließlich dem
gesunden Ring-3-Dienst Syscall 72. Autorität und Kontext werden vor dem
einzigen Sendepunkt verbraucht. Der reale RTL8139-Lauf injiziert den Request
und prüft den vollständigen Echo-Reply samt IP-/ICMP-Identität und Checksumme
am QEMU-Socket. Es gibt keinen Ring-0-Antwortfallback. Der nächste
Netzwerkbaustein S0.3c-5d1 ist ebenfalls abgenommen: DHCP ACKs werden im
Kernel nur noch transportiert und vorvalidiert. Ein geschützter Kontext, der
explizite Kernel-zu-Owner-Ingress und eine generationgebundene 1-s-
Einmalautorität übergeben die endgültige Lease-Entscheidung an Ring 3. Erst
Syscall 73 publiziert IP, Netzmaske, Gateway und DNS; der reale RTL8139-Lauf
beweist `DHCP_CONFIG_QUEUED -> DHCP_CONFIG_MEDIATED -> BOOT_OK`. Ohne gesunden
Dienst bleibt das Interface nach festen Deadlines unkonfiguriert. Der erste
Dataplane-Schnitt S0.3c-5d2a ist inzwischen ebenfalls abgenommen: Port 9000
akzeptiert nur prüfsummengeschützte Datagramme bis 32 Byte, bindet den festen
`NETU`-Kontext an eine 250-ms-Einmalautorität und erlaubt ausschließlich dem
gesunden Ring-3-Dienst Syscall 74. Der echte RTL8139-Lauf prüft Request,
vermittelte Antwort, Ports, Payload und UDP-Prüfsumme. S0.3c-5d2b2a ergänzt
nun vier statische, generationsgebundene Dienst-Bindings mit je eigener
geschützter 250-ms-Autorität, 32-Byte-Limit und vollständigem Revoke bei
Unbind, Fence oder Dienstneustart. Append-only Syscalls 75–77 bilden Bind,
Unbind und Reply ab. Ein zweiter realer RTL8139-Lauf über Port 9001 beweist,
dass der Pfad nicht mehr auf den Echo-Port fest verdrahtet ist. Socket-FDs für
allgemeine Anwendungen bleiben offen.
S0.3c-5d2b1 begrenzt nun auch die Lebensdauer der DHCP-Autorität. Option 51
wird auf 60 Sekunden bis sieben Tage validiert und gemeinsam mit
Dienstgeneration, IP und absoluter monotoner Deadline redundant geschützt.
Ablauf, Integritätsfehler oder Service-Fence entziehen IP, Maske, Gateway und
DNS fail-closed. Ein separates Testprofil verkürzt nur die Testdeadline auf
2500 ms; der echte RTL8139-Lauf bestätigt den Entzug nach `BOOT_OK` und eine
weiter laufende Shell. S0.3c-5d2b2b ist nun ebenfalls abgenommen: Ein
heapfreier Ring-3-Zustandsautomat plant T1, T2 und Ablauf über absolute
Monotonzeit und begrenzt Renew und Rebind auf jeweils drei Versuche. Der neue
append-only Syscall 78 sendet pro Aufruf genau einen DHCPREQUEST. Eine
redundant geschützte, generationgebundene 1,5-s-Transaktion korreliert ACK/NAK
mit Operation und erwarteter IP; Timeout, Fence und Neustart widerrufen sie.
Der reale RTL8139-Lauf mit fünf Sekunden Testlease bestätigt
`DHCP_RENEW_REQUESTED -> DHCP_RENEWED` bei weiter laufender Shell. Damit ist
S0.3c-5d2 abgeschlossen. Als nächstes verlagert S0.3c-5e die verbleibenden
IPv4-/UDP-/DHCP-Protokollzustände und den allgemeinen Socket-Demultiplexer aus
Ring 0.
S0.3c-5e1 liefert dafür den ersten vollständigen Schatten-Handoff: Eine eigene
statische Acht-Slot-Queue hält Frames bis 1518 Byte getrennt von Monitor und
Legacy-Demux. Der append-only Syscall 79 ist nur der aktuellen gesunden
Dienstgeneration erlaubt, blockiert nie und prüft den gesamten 1536-Byte-
Userbereich vor dem Dequeue. Neustart verwirft alte Frames. Ring 3 revalidiert
Header und Grenzen; nur nach erfolgreichem Copy-out kann es die korrelierte
Einmalbestätigung abgeben. Der reale RTL8139-Lauf erreicht
`REIST_NETWORK FRAME_HANDOFF`. Als nächstes übernimmt S0.3c-5e2 die
IPv4-/UDP-/DHCP-Parser und entfernt danach den noch parallelen Ring-0-Demux.
S0.3c-5e2a ist abgeschlossen: Ein fester, heapfreier IPv4-v1-Parser validiert
in Ring 3 Version, IHL, Gesamtlänge, TTL und Headerprüfsumme und lehnt
Fragmente fail-closed ab. Der generationsgebundene Shadow-Nachweis
`REIST_NETWORK IPV4_PARSED_RING3` wurde mit RTL8139 bestätigt. Der Parser hat
noch keine Ausgabeautorität; als nächstes übernimmt S0.3c-5e2b UDP-/DHCP-
Demultiplex und Protokollzustand, bevor der Ring-0-Parallelpfad entfernt wird.
S0.3c-5e2b1 ist ebenfalls abgeschlossen: Der Ring-3-Dienst validiert UDP-Länge,
Portpaar und verpflichtende Pseudoheader-Prüfsumme heapfrei und liefert ein
festes 20-Byte-Ergebnis. Der reale RTL8139-Lauf bestätigt
`REIST_NETWORK UDP_PARSED_RING3` und einen vermittelten UDP-Echo-Request.
S0.3c-5e2b2a speist nun dienstgebundene UDP-Ports aus genau diesem validierten
Ergebnis. Der append-only Syscall 80 bindet die Entscheidung über CRC32,
Dienstgeneration und eine absolute 250-ms-Deadline an den zuletzt gelieferten
Frame. Nur ein aktives Binding darf eine geschützte Antwortautorität erzeugen;
ungebundene oder ungültige Datagramme müssen kanonisch verworfen werden. Für
dienstbesessene Ports ist die parallele Legacy-Zustellung in Ring 0
unterbunden. Der echte RTL8139-Lauf bestätigt `UDP_INGRESS_RING3`, die
vermittelte Antwort und `TEST_OK`. Der heapfreie DHCP-v1-Parser validiert
BOOTP-Reply, XID,
Client-MAC, Cookie, Ports, IPv4-/UDP-Längen sowie eine begrenzte Optionsliste
mit genau einem OFFER-, ACK- oder NAK-Typ. Kritische Optionsduplikate,
Abschneidung und fehlendes END werden verworfen; eine vorhandene UDP-
Prüfsumme ist verpflichtend korrekt, der IPv4-DHCP-Nullwert wird separat
markiert. Der generations- und Frame-CRC-korrelierte Ring-3-Nachweis
`DHCP_PARSED_RING3` wurde mit RTL8139 vor `BOOT_OK` und `TEST_OK` bestätigt.
Renewal/Rebind ist dienstautorisiert: Syscall 81 bindet das validierte
Ergebnis an Frame-CRC,
250-ms-Lieferdeadline, Dienstgeneration, lokale MAC und die bestehende
geschützte Transaktions-ID. Der reale RTL8139-Lauf bestätigt
`DHCP_RENEW_REQUESTED -> DHCP_RENEW_INGRESS_RING3 -> DHCP_RENEWED`. Der
Bootpfad ist nun ebenfalls dienstgesteuert: Syscall 82 eröffnet eine
geschützte 1.500-ms-Transaktion, Ring 3 validiert OFFER/ACK und Ring 0 sendet
nur die beiden vermittelten DISCOVER-/REQUEST-Frames. Drei Dienstversuche und
eine gesamte Kernelwartezeit von sechs Sekunden begrenzen den Start. Der reale
RTL8139-Lauf bestätigt `DHCP_BOOT_DISCOVER_RING3 -> DHCP_BOOT_OFFER_RING3 ->
DHCP_BOOT_ACK_RING3 -> DHCP_CONFIG_QUEUED -> DHCP_CONFIG_MEDIATED -> BOOT_OK`.
Die früheren synchronen Ring-0-DHCP-Routinen, die dedizierte Vier-Slot-Queue
und der alte Poller sind entfernt. Der statische Service-Frame-Handoff ist der
einzige DHCP-Eingang; Boot und Renewal sind erneut mit RTL8139 grün, wobei der
Bootlauf Dienst-Crash, Restart und Queue-Druck einschließt. Der allgemeine
Ring-0-UDP-Parser und seine Legacy-Einspeisung sind ebenfalls entfernt. UDP-
Eingang fällt im Kernel geschlossen aus; nur der validierte Ring-3-Ingress darf
für aktive Bindings Antwortautorität erzeugen. Reale RTL8139-Läufe für Port
9000, Port 9001 und Boot-DHCP sind grün. Als nächstes wird der verbleibende
Ring-0-IPv4/ICMP-Fallback außerhalb des gesunden Dienstpfads untersucht und
schrittweise geschlossen. Der ICMP-Teil ist nun vollständig abgenommen: Ein
heapfreier Ring-3-v1-Parser validiert ausschließlich Echo Request/Reply, Code,
IPv4-Grenzen und die vollständige ICMP-Prüfsumme. Das feste 28-Byte-Ergebnis
wird bei Fehlern kanonisch genullt. Ein geschütztes Delivery-Ticket bindet
Syscall 83 zusätzlich an PID, Prozessgeneration, Frame-CRC und eine absolute
250-ms-Deadline. Nur ein validiertes `ECHO_REQUEST` erzeugt die bestehende
Einmalautorität; `ECHO_REPLY` darf ausschließlich einen exakt erwarteten Ping
abschließen, und der kanonische Drop bleibt wirkungslos. Der Ring-0-ICMP-
Parser ist entfernt. Der RTL8139-Lauf belegt Parser, Ingress und vermittelten
Reply bis `TEST_OK`. Der Ring-0-IPv4-Demux ist ebenfalls entfernt: Fallback-
Frames werden dort nicht mehr geparst, demultiplext oder zum impliziten ARP-
Lernen verwendet. S0.3c-5e2 ist damit geschlossen. Als nächster Netzwerkrest
folgt der Ring-0-ARP-Fallback samt ungeschütztem Legacy-Cache.
Der erste Teilschritt S0.3c-6a ist abgeschlossen: Storage- und
Dateisystemtransaktionen besitzen einen geschützt gespeicherten Aktivzustand,
eine absolute Deadline und lehnen Überlappung vor Seiteneffekten ab; Fehler
führen zum Schreib-Fence beziehungsweise Read-only-Zustand. Als nächstes folgt
S0.3c-6b ist ebenfalls abgeschlossen: Ein gemeinsamer statischer 8-Slot-Pool
trägt versionierte Block-/VFS-Requests über generationssichere Handles. Die
maximal 512 Byte Nutzdaten sind CRC-geschützt redundant, Metadaten und
Dienstidentität über `critical_object`; Prozessende widerruft alle betroffenen
Slots. Als nächstes wird dieser Dataplane an den restartbaren Ring-3-Dienst
gebunden (S0.3c-6c).
S0.3c-6c ist umgesetzt: `STORAGE.PRG` besitzt ein separates Default-Deny-
Syscallprofil, bindet sich generationssicher an den statischen Dataplane und
wird bei Starttimeout oder Prozessverlust höchstens dreimal neu gestartet.
Danach verriegelt der Supervisor Storage und VFS. Requests laufen höchstens
fünf Sekunden und höchstens zwei gleichzeitig je Client. Der QEMU-Gast liest
den realen MBR über den Ring-3-Dienst und bestätigt dessen `0x55AA`-Signatur.
S0.3c-6d wurde anschließend in drei getrennten Fehlergates abgenommen.
Der erste Teil S0.3c-6d1 ist abgenommen: Ein isoliertes QEMU-Testimage beendet
den Ring-3-Storage-Dienst nach einem realen ATA-Read, aber vor Abschluss des
beanspruchten Requests. Exit-Cleanup verwirft die alte Generation, der
Supervisor startet begrenzt neu, und der Client liest anschließend über einen
neuen Request erneut den MBR. Der Lauf endet mit `STORAGE_RESTART_OK` und
`TEST_OK`.
S0.3c-6d2 ist ebenfalls abgenommen: Ein isoliertes QEMU-Image erzwingt zwei
aufeinanderfolgende ATA-Lesefehler. Der Client erhält `-EIO`, Ressource 0 wird
in der geschützten Storage-Kontrolle quarantänisiert und der nächste Request
endet vor erneutem ATA-Zugriff mit `-EHOSTDOWN`. Dienst, Scheduler und übrige
Gasttests laufen bis `TEST_OK` weiter.
S0.3c-6d3 ist abgeschlossen und schließt S0.3c-6 ab. Der persistente
QEMU-Harness bootet eine ACTIVE-Undo-Transaktion mit zwei überschriebenen
Sektoren und beschädigter primärer Headerkopie. Der Kernel restauriert die
alten Daten, repariert beide Header zu CLEAN und startet anschließend die
vollständige Probe-Reintegration. Erst danach muss der neu gebundene
Ring-3-Storage-Dienst den realen MBR-Selbsttest und der Gast `TEST_OK`
erreichen. Ein dabei reproduzierter Race zwischen Supervisor-Worker und
explizitem Storage-Start wurde durch einen getrennten Aktivierungszustand und
IRQ-serialisierte Kontrollzugriffe beseitigt. Als nächstes folgt S0.3c-7.
S0.3c-7a liefert dafür den ersten, noch plattformneutralen Protokollbaustein:
Aktiv- und Standby-Knoten teilen eine geschützt gespeicherte Lease, Epoche,
Fence-Epoche und Transitionssequenz. Takeover ist nur nach Leaseablauf und
verifiziertem Fence derselben Epoche erlaubt; danach sind alle alten Tokens
stale. Hosttests prüfen Split-Brain-Abwehr, Überlauf und ECC/CRC-Recovery. Das
ist ausdrücklich noch kein unabhängiger Kanal. Als nächstes muss S0.3c-7b ein
externes Supervisor-/Fence-Backend mit eigener Fehlerdomäne anbinden.
S0.3c-7b1 härtet dafür die Softwaregrenze: Der Handover-Kern startet ohne
fest gebundenes Fence-Backend nicht. Request und rücklesbare Bestätigung sind
getrennte Callbacks; beide laufen außerhalb des IRQ-Locks, anschließend wird
der komplette Epoch-/Lease-Zustand atomar revalidiert. Ein bloßer Request oder
ein Readback einer alten Epoche kann keine Übernahme autorisieren. Offen bleibt
7b2 mit realem externem Transport, eigener Zeitbasis/Stromversorgung und
rücklesbarem Hardware-Interlock.
S0.3c-7b2a liefert inzwischen die ausführbare Zwischenstufe: Ein separater
Host-Supervisor bedient in QEMU einen dedizierten COM2-Kanal. CRC-geschützte,
versionierte 24-Byte-Frames binden Fence-Request und Readback an Active-ID und
64-Bit-Epoche; alle UART-Wartepfade enden nach spätestens einer Sekunde. Der
Gastlauf bestätigt die geordnete Folge von Fence-Request, Readback, Takeover,
Boot und Ring-3-Smoke. Da Host und Gast weiterhin keine elektrisch getrennten
Controller mit eigener Strom- und Zeitversorgung sind, bleibt 7b2b auf realer
Zielhardware offen.
Fünf aufeinanderfolgende Handover-Gastläufe bestanden. Dabei wurde außerdem
ein Startreihenfolge-Race entfernt: Probe und Storage-Dienst werden jetzt
vollständig veröffentlicht, bevor der Supervisor-Worker sie prüfen darf. Der
Scheduler-Gasttest wartet auf den beobachtbaren `SLEEPING`-Zustand statt eine
bestimmte Round-Robin-Position vorauszusetzen.
S0.3c-7c1 betreibt nun Active und Standby als zwei getrennte QEMU-Prozesse.
Ein CRC-geschützter Ready/Replica-Handshake verhindert UART-FIFO-Verlust vor
Standby-Initialisierung. Der Host beendet den Active nach der Fence-Anforderung
und bestätigt das Fence erst nach geprüftem Prozessende. Anschließend übernimmt
der Standby und besteht den vollständigen Ring-3-Test. Drei Wiederholungsläufe
des finalen Standes waren erfolgreich.
S0.3c-7c2a repliziert jetzt zusätzlich einen `critical_object`-geschützten
Referenz-Dienstzustand über drei streng monotone CRC-Frames. Nach dem
nachgewiesenen Active-Ausfall erhöht der neue Active Epoche und Sequenz und
publiziert den promovierten Zustand. Ein drittes QEMU-Image tritt danach als
reparierter Kanal bei, validiert den Zustand und bleibt nach negativen
Lease-/Takeover-Prüfungen gefenceter Standby. Drei vollständige Drei-Kanal-Läufe
erreichten im übernommenen Kanal `TEST_OK`. Die folgende Produktionsbindung
schließt Catch-up und kontrollierte Ausgangsfreigabe im QEMU-Referenzprofil;
physisch unabhängige Zielhardware bleibt offen.
S0.3c-7c2b ersetzt den synthetischen Wert durch den CRC32-Fingerprint des
realen ATA-Bootvolumes. Standby und Rejoin-Kanal halten überwachte
Storage-Schreibausgänge bereits vor dem Mount. Drei lückenlose Checkpoints und
ein lokaler MBR-Selbsttest sind Voraussetzung für die Übernahme. Erst nach
Active-Ende, Fence-Ack, Epoch-Promotion und erfolgreicher Zustandspublikation
wird der neue Active freigegeben; danach beweist `FILE_IO_OK` reale
VFS-Mutationen bis `TEST_OK`. Der reparierte Kanal bleibt gehalten. Drei
vollständige finale Läufe waren grün. Das QEMU-Referenzpaket 7c ist damit
abgeschlossen; echte unabhängige Zielhardware und Common-Cause-Gates bleiben
offen.
Die bisherige Domäne ist noch keine unabhängige Kernel-, CPU- oder
RAM-Fehlerdomäne.

Das neue versionierte Gefahrenregister unter `safety/hazards.toml` bindet die
ersten fünf zentralen REIST-Gefahren an positive FTTI, Safe-State, konkrete
Kontrollen, vorhandene Tests und Restrisiken. Ein Hostvalidator prüft Schema,
IDs und Referenzen fail-closed. Das Register ist bewusst als `partial`
gekennzeichnet; vollständige Abdeckung und releasegebundene Testergebnisse
bleiben Teil von S0.1.

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
