# Fehlstellenanalyse und Implementierungsfahrplan

Stand: 15. August 2026

Dieses Dokument beschreibt den anhand des aktuellen Quellstands geprüften
Ist-Zustand, die wichtigsten noch fehlenden Betriebssystemfunktionen und eine
Reihenfolge, in der sie ohne unnötige Umbauten ergänzt werden können. Es ist
als Arbeits-Backlog gedacht: Jede Aufgabe besitzt eine feste ID, Abhängigkeiten
und überprüfbare Abnahmekriterien.

## 1. Zielbild und Abgrenzung

Das neue Projektziel ist ein **High-Assurance Research Operating System for
Fault-Tolerant and Fail-Operational Computing**, bei dem
Stabilität, Fehlerbegrenzung, nachweisbares Laufzeitverhalten und langfristige
Wartbarkeit vor Funktionsumfang und Geschwindigkeit stehen. Neue Funktionen
dürfen nur aufgenommen werden, wenn Fehlergrenzen, Diagnose, Rückfallpfad,
Ressourcenobergrenzen, Verifikation und Lebenszyklus geklärt sind. Redundanz ist
der bevorzugte Weg zu Verfügbarkeit, muss aber unabhängig sein und gemeinsame
Fehlerursachen berücksichtigen.

REIST besitzt einen generischen High-Assurance-Kern. Medizin, Raumfahrt,
Industrieautomatisierung und FPGA-Forschung sind getrennte Referenzprofile,
nicht die Identität des Betriebssystems. Der Forschungsprototyp ist weder
zertifiziert noch für einen sicherheitskritischen Produktionseinsatz
freigegeben. Der verbindliche Kernvertrag steht in
[`HIGH_ASSURANCE_CORE_CONTRACT.md`](../architecture/HIGH_ASSURANCE_CORE_CONTRACT.md);
der bisherige medizinische Vertrag ist ein optionales Referenzprofil.

Der grafische Ring-3-Launcher bleibt vorhanden, ist aber ausdrücklich
nicht-sicherheitskritisch. Desktop, Netzwerk, Dateisysteme und Diagnose dürfen
keine für das gewählte Profil wesentliche Funktion blockieren oder deren Zeitbudget
verbrauchen.

**REIST OS** steht für **Resilient Execution, Isolation and Stability
Technology**. Die aktuelle Architektur ist trotz des neuen Namens noch ein
modularer monolithischer Kernel: Scheduler, Speicherverwaltung, Dateisysteme,
Netzwerk und Treiber werden gemeinsam in `kernel.bin` gelinkt. Ring-3-Programme
sind isoliert, Hardware- und Dateisystemdienste laufen aber noch nicht als
Userspace-Server. Für das High-Assurance-Ziel werden Nachrichten-IPC,
Capabilities und isolierte, neu startbare Dienste nun in S0.3 verbindlich.

## 2. Zusammenfassung

Das OS ist kein Minimalgerüst mehr. Es bootet nativ über BIOS/MBR, besitzt
Paging, Ring-3-Prozesse mit eigenen Seitentabellen, validierte User-Pointer,
präemptives Round-Robin-Scheduling, ein VFS, schreibbares FAT12/FAT32, lesbares
EXT2, mehrere Gerätetreiber, einen kleinen IPv4-Stack und einen gebauten
Userspace. Der geprüfte Windows-Referenzbuild ist erfolgreich. Phase 0 sowie
R1.1, R1.2, R1.3 und der vor R2.1 eingeschobene Desktop-MVP R1.4 sind
abgeschlossen. Die aktuelle Hosttest-Suite und
automatisierte Ring-3-Tests prüfen neben dem normalen, LAPIC-gesteuerten Betrieb
einen eigenen PIT-Scheduler-Fallback ohne LAPIC sowie Speicherkonfigurationen
mit 32, 64, 256, 512 und 1024 MiB.

Die beim ersten Audit belegten Korrektheitslücken in Prozess-Wait,
Exception-Frames und PRG-v1-Vertrag sind in Phase 0 behoben. R1.1 hat darauf
eine allgemeine **Blockier-/Ereignisgrundlage** aufgebaut: intrusive
Wait-Queues, atomaren Prozess-Wait, blockierendes Sleep und Console-Input,
`yield`, 64-Bit-Zeit sowie einen kalibrierten Scheduler-Timer. R1.2 trennt nun
erkannten von verwaltetem Speicher, erweitert den Kernel-Heap dynamisch,
schützt statische und dynamische Kernelstacks mit Guardpages und räumt beendete
Tasks außerhalb langer IRQ-Sperrabschnitte auf. R1.3 definiert nun die
IRQ-, Präemptions-, Schlaf- und Lockverträge, serialisiert VFS und
ATA-/FDD-Zugriffe und verlagert Netzwerk- sowie HPET-Arbeit aus dem harten
IRQ-Kontext. Strukturierte Logs und vollständige Panic-Diagnosen schließen
den Meilenstein ab. R1.4 ergänzt den nativen VBE-Handoff, eine schmale
versionierte Ring-3-Display-ABI und `DESKTOP.PRG`. Vor jedem weiteren regulären
Funktionspaket steht das Sicherheits-Gate S0. Wesentliche Grundlagen aus S0.1
und S0.2 sowie S0.3a und S0.3b sind umgesetzt; aktiv ist die schrittweise
Migration des Netzwerkdienstes in S0.3c. Die noch offenen Nachweise aus S0.1
und S0.2 bleiben sichtbar und werden nicht durch spätere Teilpakete ersetzt.

### 2.1 Fortschrittsübersicht

Diese Liste ist der schnelle Einstieg in den Arbeitsstand. `[x]` bedeutet
umgesetzt und mit den im Paket genannten Tests abgenommen. `[ ]` bedeutet
offen; der Zusatz **in Arbeit** kennzeichnet genau das aktuelle Paket.
Detailbeschreibung, Restrisiken und Abnahmekriterien bleiben in Abschnitt 7
und 10 verbindlich.

#### Abgeschlossene Grundlagen

- [x] R0.1 Atomarer Wait/Wakeup-Pfad
- [x] R0.2 Einheitliche Exception-Stubs einschließlich `#AC`
- [x] R0.3 Festgeschriebener und validierter PRG-v1-Vertrag
- [x] R0.4 Automatisierter Ring-3-QEMU-Smoke
- [x] R1.1 Wait-Queues, blockierendes Sleep/Yield und monotone 64-Bit-Zeit
- [x] R1.2 Speicherverwaltung, dynamischer Heap, Guardpages und Reaping
- [x] R1.3 Synchronisations-, IRQ-, Logging- und Panic-Vertrag
- [x] R1.4 Nativer VBE-Handoff und grafischer Ring-3-Desktop-MVP

#### High-Assurance-Gate S0

- [ ] S0.1 Einsatzprofile, Gefahrenregister und vollständiger Assurance Case
  - [x] Generischer High-Assurance-Kernvertrag und getrennte Referenzprofile
  - [x] Maschinenprüfbares Gefahrenregister-v1-Schema mit eindeutigen IDs,
    FTTI, Safe-State, Restrisiko sowie existierenden Code-/Testreferenzen
  - [ ] Gefahrenregister für alle Kern-, Geräte- und Profilgefahren
  - [ ] Automatische Traceability von Gefahr bis Testergebnis
- [ ] S0.2 Vollständiges Stack-, Exception- und Panic-Containment
  - [x] Guardpages, Double-Fault-Notfallpfad, Crashrecord und QEMU-Watchdog
  - [ ] Callgraph-Gesamtbudgets und Stack-Watermarks
  - [ ] Unabhängiger Zielhardware-Watchdog mit rücklesbarem Fencing
- [x] S0.3a Begrenzte IPC-/Capability-Basis
- [x] S0.3b Überwachte, neu startbare Least-Privilege-Probedomäne
- [ ] S0.3c Reale Dienstmigration und Redundanz
  - [x] S0.3c-1 Begrenzter Ring-3-Diagnosedienst
  - [x] S0.3c-2 Freigabe delegierter Client-Capabilities ohne Quota-Leck
  - [x] S0.3c-3a bis 3r Geschützter, epochengebundener Netzwerk-Handoff
  - [x] S0.3c-4a Eng vermittelte ARP-Zustandsänderung aus Ring 3
  - [x] S0.3c-4b Geschützter ARP-Cache mit Ablaufzeit, Quellepoche,
    Redundanz und Fail-Closed-Lookup
  - [x] S0.3c-4c Dienstneustart widerruft Bindungen der alten Generation;
    begrenzter Scrub und Integritätseskalation sind nachgewiesen
  - [x] S0.3c-5 Netzwerkdatenpfad vollständig aus Ring 0 lösen
    und den Dienst unter Fehler-, Druck- und Restart-Injektion abnehmen
    - [x] S0.3c-5a Passive Gateway-Vertrauensentscheidung aus dem
      Ring-0-ARP-/IPv4-Pfad entfernt
    - [x] S0.3c-5b Lokale ARP-Auflösung und Antwortentscheidung über den
      überwachten Dienst vermitteln
      - [x] S0.3c-5b1 Lokale ARP-Antwortentscheidung mit geschützter,
        generationgebundener Einmalautorität vermittelt
      - [x] S0.3c-5b2a Deterministisch injizierten echten RX-Request samt
        vermittelter Antwort im RTL8139-Gast nachweisen
      - [x] S0.3c-5b2b Ausgehende lokale ARP-Auflösung in den Dienst migrieren
    - [x] S0.3c-5c ICMP-Echo-Antwort mit geschützter 250-ms-
      Einmalautorität, festem 32-Byte-Payloadlimit und echtem RTL8139-
      Request/Reply-Nachweis über Ring 3 vermitteln
    - [x] S0.3c-5d1 DHCP-Lease-Konfiguration über einen geschützten,
      generationgebundenen Ring-3-Entscheid und Syscall 73 publizieren
    - [x] S0.3c-5d2 UDP-Datenpfad und DHCP-Renew/Rebind schrittweise in den
      überwachten Dienst verlagern
      - [x] S0.3c-5d2a Begrenztes UDP-Echo auf Port 9000 mit 32-Byte-Limit,
        Pflichtprüfsumme und echtem RTL8139-Request/Reply vermitteln
      - [x] S0.3c-5d2b1 DHCP-Leasezeit aus dem ACK übernehmen, redundant
        schützen und die Netzkonfiguration bei Ablauf fail-closed entziehen
      - [x] S0.3c-5d2b2 Allgemeine UDP-Bindings sowie DHCP-Renew/Rebind
        generationgebunden in den Dienst verlagern
        - [x] S0.3c-5d2b2a Vier statische, generationsgebundene
          Dienst-Bindings mit 32-Byte-Datagrammen, Ablauf und Fence-Revoke
        - [x] S0.3c-5d2b2b DHCP-Renew/Rebind als begrenzten, nichtblockierenden
          Ring-3-Zustandsautomaten mit drei Versuchen je Phase, geschützter
          Einmaltransaktion und realem RTL8139-Renewal-Nachweis umsetzen
    - [x] S0.3c-5e Verbleibende IPv4-/UDP-/DHCP-Protokollzustände und den
      allgemeinen Socket-Demultiplexer aus Ring 0 in die Dienstgrenze verlagern
      - [x] S0.3c-5e1 Separate statische 8-Slot-RX-Queue und append-only
        Syscall 79 für einen vollständigen, generation-frischen 1518-Byte-
        Frame-Handoff mit realem RTL8139-Ring-3-Nachweis
      - [x] S0.3c-5e2 IPv4-/ICMP-/UDP-/DHCP-Parser und Protokollzustand über den
        neuen Handoff übernehmen und den parallelen Ring-0-Demux entfernen
        - [x] S0.3c-5e2a Begrenzten heapfreien IPv4-v1-Shadow-Parser mit
          Headerprüfsumme, Fragmentablehnung und realem RTL8139-Nachweis
          nach Ring 3 verlagern
        - [x] S0.3c-5e2b UDP-/DHCP-Demux und Protokollzustand auf dem
          Ring-3-Parser aufbauen und erst danach den Parallelpfad entfernen
          - [x] S0.3c-5e2b1 Heapfreien UDP-v1-Shadow-Parser mit Pflicht-
            prüfsumme, exakter Längenkonsistenz und RTL8139-Nachweis ergänzen
          - [x] S0.3c-5e2b2 UDP-Bindings und DHCP-Eingang aus dem validierten
            Ring-3-Ergebnis speisen und den Ring-0-UDP-Demux entfernen
            - [x] S0.3c-5e2b2a Dienstgebundene UDP-Ports über einen
              CRC-/generation-/deadlinegeschützten Ring-3-Entscheid speisen
              und deren parallele Ring-0-Zustellung unterbinden
            - [x] S0.3c-5e2b2b DHCP-Eingang und verbleibenden UDP-Demux aus
              Ring 0 lösen; Druck-, Restart- und Fehlpfade abnehmen
              - [x] S0.3c-5e2b2b1 Heapfreien DHCP-v1-Shadow-Parser mit
                begrenzten Optionen, BOOTP-/Cookie-Prüfung, optionaler
                IPv4-UDP-Prüfsumme und realem RTL8139-Nachweis ergänzen
              - [x] S0.3c-5e2b2b2 OFFER/ACK/NAK-Autorität aus dem validierten
                Ring-3-Ergebnis speisen und den Parallelpfad entfernen
                - [x] S0.3c-5e2b2b2a Renewal/Rebind-ACK/NAK über append-only
                  Syscall 81, Frame-CRC, Dienstgeneration und bestehende
                  Transaktionsautorität übernehmen; Ring-0-Queue und -Poller
                  während der Transaktion unterdrücken
                - [x] S0.3c-5e2b2b2b Boot-DISCOVER/OFFER/REQUEST/ACK als
                  begrenzten Ring-3-Zustandsautomaten übernehmen und danach
                  die dedizierte Ring-0-DHCP-Queue entfernen
                  - [x] S0.3c-5e2b2b2b1 Geschützte Boot-Transaktion mit
                    append-only Start-Syscall 82, drei endlichen
                    Dienstversuchen und realem RTL8139-Nachweis übernehmen
                  - [x] S0.3c-5e2b2b2b2 Tote synchrone Ring-0-DHCP-Routinen,
                    Queue und Poller entfernen sowie Restart-/Druckpfade ohne
                    Parallelzustellung abnehmen
        - [x] S0.3c-5e2c Heapfreien ICMP-Echo-v1-Shadow-Parser mit vollständiger
          Prüfsumme, fester 28-Byte-Ausgabe und realem RTL8139-Nachweis ergänzen
        - [x] S0.3c-5e2d ICMP-Eingangsautorität aus einem geschützten,
          CRC-/generations-/deadlinegebundenen Ring-3-Ergebnis speisen und den
          Ring-0-ICMP-Parser entfernen
        - [x] S0.3c-5e2e Verbleibenden Ring-0-IPv4-Demux und seine implizite
          ARP-Lernmutation durch validierte Ring-3-Entscheidungen ersetzen
    - [x] S0.3c-5f Verbleibenden Ring-0-ARP-Fallback und ungeschützten lokalen
      Legacy-Cache entfernen; ausschließlich überwachte ARP-Entscheide zulassen
  - [ ] S0.3c-6 Storage-/Dateisystemdienst und medienübergreifende Recovery
    - [x] S0.3c-6a Geschützte, nicht überlappende und absolut begrenzte
      Storage-/Dateisystem-Transaktionen mit Fail-Closed-Fence
    - [x] S0.3c-6b Versioniertes Block-/VFS-IPC und statische Request-Pools
    - [x] S0.3c-6c Ring-3-Storage-Service mit Capability-Profil und Restart
    - [x] S0.3c-6d Reale Power-Loss-/I/O-/Restart-Injektion in QEMU
      - [x] S0.3c-6d1 Dienstcrash bei beanspruchtem Request, generationssicherer
        Widerruf, begrenzter Restart und erfolgreicher Wiederholungsrequest
      - [x] S0.3c-6d2 Vermittelte ATA-I/O-Fehler mit definiertem Fehlerstatus,
        Quarantäne und geprüftem Weiterbetrieb
      - [x] S0.3c-6d3 Stromverlust während einer persistenten Mutation mit
        Neustart, Journal-Recovery und anschließendem Ring-3-Dienst-Selbsttest
    - [x] S0.3c-6e Automatische ATA-/FDD-Quarantäne und Requalifizierung über
      Geräteidentität, geschützten Fingerprint und zwei frische Reads; echter
      FDD-Disconnect/Reconnect mit Controllerreset und erneutem FAT12-Read;
      unklare Schreibabschlüsse nur read-only reintegrieren
    - [ ] S0.3c-6f Medienunabhängiges Undo/COW/Journal mit Flush-/Barrier- und
      Power-Loss-Nachweis für jeden beschreibbaren Datenträger; stärkere
      Wechselmedien-Identität und kontrolliertes Cache-Invalidieren/Remount
  - [ ] **S0.3c-7 in Arbeit:** Unabhängiger Standby-/Supervisor-Kanal und
    realer Handover
    - [x] S0.3c-7a Statischer Lease-/Epoch-/Fence-Protokollkern mit
      Split-Brain-, Stale-Epoch- und Integritäts-Fault-Tests
    - [ ] S0.3c-7b Plattformbackend für einen elektrisch und zeitlich
      unabhängigen Supervisor-Kanal samt rücklesbarem Fence
      - [x] S0.3c-7b1 Fest gebundener, statischer Request/Readback-Vertrag;
        Backendaufrufe außerhalb des IRQ-Locks und anschließende Revalidierung
      - [x] S0.3c-7b2a Prozessgetrennter QEMU-Host-Supervisor über einen
        dedizierten COM2-Kanal mit CRC-Frame, exaktem Epoch-Readback und
        realem Takeover-Lauf
      - [ ] S0.3c-7b2b Reales externes Transport-/Interlock-Backend auf
        Zielhardware mit eigener Stromversorgung und Zeitbasis
    - [x] S0.3c-7c Zwei reale Ausführungskanäle mit Zustandsreplikation,
      Selbsttest, Übernahme und kontrollierter Reintegration
      - [x] S0.3c-7c1 Zwei getrennte QEMU-Prozesse mit CRC-geschützter
        Epoch-Replikation, explizitem Standby-Ready, nachgewiesen beendetem
        Active vor Fence-Ack und vollständigem Ring-3-Smoke nach Übernahme
      - [x] S0.3c-7c2 Kontinuierliche Replikation des sicherheitsrelevanten
        Dienstzustands und kontrollierte Reintegration des reparierten Kanals
        - [x] S0.3c-7c2a Geschützter Referenz-Dienstzustand mit strikt
          monotonen Frames, drei Updates vor Failover, Epoch-Promotion und
          gefenceter Reintegration eines dritten QEMU-Kanals
        - [x] S0.3c-7c2b Produktionsdienstzustand mit begrenztem Catch-up,
          Selbsttest und kontrollierter Wiederaufnahme realer Ausgänge
    - [ ] S0.3c-7d Common-Cause-Analyse und wiederholte Zielhardware-Failover-Gates
- [ ] S0.4 Deterministische Planung und garantierte Ressourcen
  - [x] S0.4a Heapfreier, gewichteter Klassenzyklus mit statischen
    Safety-/Service-/Ambient-Klassen und begrenzter Auswahl über `MAX_TASKS`
  - [x] S0.4b Absolute CPU-Zeitfenster, Überlast-Erkennung und definierter
    degradierter Zustand
  - [ ] S0.4c Priority Inheritance für blockierende Ressourcen und
    nachgewiesene WCET-/Speicher-/Queue-Budgets
    - [x] S0.4c-1 Generationssichere, transitive Priority Inheritance für
      blockierendes IPC mit automatischer Rücknahme bei Wakeup, Timeout und Exit
    - [ ] S0.4c-2 Statische WCET-, Stack-, Speicher- und Queue-Budgetnachweise
      - [x] S0.4c-2a Maschinenprüfbares Kapazitätsregister für Scheduler,
        Kernelstacks, IPC und Storage einschließlich Quellcode-Driftprüfung
      - [ ] S0.4c-2b Laufzeitnachweise für High-Water-Marken,
        Kapazitätserschöpfung und vollständige Rückgewinnung
        - [x] S0.4c-2b1 Saturierende IPC-/Storage-Pool-Diagnostik mit
          Erschöpfungs- und vollständigem Rückgewinnungsnachweis
        - [ ] S0.4c-2b2 Task-, Heap-, Frame- und weitere statische
          Queue-High-Water-Nachweise
          - [x] S0.4c-2b2a Memory-ABI v2 mit Frame-/Heap-Peaks,
            saturierenden Fehlerzählern, v1-Kompatibilität und Gastnachweis
          - [x] S0.4c-2b2b Taskslot-High-Water sowie deterministische
            Heap-/Frame-ENOMEM-Fault-Injection
            - [x] S0.4c-2b2b1 Versionierte Taskslot-Diagnostik mit aktiver
              Belegung, High-Water, Reserve und saturierenden Ablehnungen;
              Gastnachweis für Erschöpfung und Rückgewinnung
            - [x] S0.4c-2b2b2 Deterministische Heap-/Frame-ENOMEM-Injection
              mit vollständigem Rollbacknachweis
      - [ ] S0.4c-2c Zielhardwarebezogene WCET- und Stack-Callgraph-Nachweise
        - [x] S0.4c-2c1 Unabhängiger GCC-Analysecompile mit vollständiger
          Stack-Usage-/Callgraph-Evidenz, lokalen 4096-Byte-Gates und
          fail-closed Rekursionsprüfung
        - [ ] S0.4c-2c2 Entry-/IRQ-Gesamtpfadbudgets und WCET-Messungen auf
          jeder ausgewählten Zielplattform
          - [x] S0.4c-2c2a Kumulative Legacy-/Scheduler-IRQ- und
            CPU-Exception-Stackbudgets mit vollständigem Handlerinventar und
            fail-closed Indirektaufrufen
          - [ ] S0.4c-2c2b Syscall-Gesamtpfade sowie bounded
            WCET-Baselines auf QEMU, VMware und ausgewählter Referenzhardware
- [ ] S0.5 Signierter Boot, redundanter Zustand und atomare A/B-Updates
- [ ] S0.6 Langzeit-, Fault-Injection- und Assurance-Nachweise

#### Funktionsroadmap nach dem S0-Gate

- [ ] R2.1 Gemeinsame ABI v1 und vollständige Dateideskriptoren
- [ ] R2.2 VFS-/FAT-Zuverlässigkeit und vollständige Sync-Semantik
- [ ] R2.3 Blockgeräte, Partitionen und moderne Storage-Abstraktion
- [ ] R3.1 Pipes, Signale, Prozessgruppen und TTY
- [ ] R3.2 Userspace-Shell und Init-/Service-Management
- [ ] R4.1 Gehärtetes IPv4/UDP und Socket-ABI
- [ ] R4.2 DNS
- [ ] R4.3 TCP
- [ ] R5.1 ACPI-, DMA- und Plattformbasis
- [ ] R5.2 xHCI/USB in überprüfbaren Stufen
- [ ] R6 Optionale Modernisierung: UEFI, SMP, 64 Bit und Highmem

## 3. Verifizierter Ist-Zustand

| Bereich | Vorhanden | Reifegrad |
|---|---|---|
| Boot | BIOS/MBR, zweistufiger Loader, E820, A20, ELF32-Prüfung, Kernel-CRC32, FAT12-Floppy, optionaler nativer VBE-LFB-Handoff | stabiler Referenzpfad mit VGA-Rückfall |
| CPU | GDT/IDT/TSS, Ring 0/3, Exceptions, PIC, gegen PIT kalibrierter lokaler APIC-Timer, PIT-Scheduler-Fallback, `INT 0x80` | funktionsfähiger Single-Core-Pfad |
| Speicher | fail-closed normalisierte E820-Karte, 1-GiB-Directmap, Frame-Accounting, dynamischer Kernel-Heap, Kernel-Stack-Guardpages, getrennte Prozessadressräume, sichere User-Kopien | R1.2 plus erster S0.2-Schutz; Speicher oberhalb 1 GiB nur erkannt |
| Prozesse | Spawn mit `argc/argv`, Exit-Status, atomarer Wait, generische Wait-Queues, Sleep/Yield, Prozessliste, eigenes CWD sowie statische IPC-v1-Endpoints mit explizit delegierten generationsgebundenen Capabilities, endlichen Deadlines, geschützten Steuerdaten, reservierter Restart-Admission, versionierten Domänenprofilen und abgenommener Ring-3-Probe-Recovery | maximal 8 Tasks; produktive Dienste liegen noch im modularen Monolithen |
| Dateien | VFS, Mounts, FAT12/FAT32 lesen und schreiben, FAT32-Rename/Replace im Undo-Journal, FAT32/ATA-`fsync`, EXT2 lesen | persistenter Editor-Commit vorhanden; ABI, FAT12-Sync und breitere Rename-Semantik fehlen |
| Geräte | PCI, ATA-PIO, FDD-DMA, PS/2 und COM1 mit blockierendem Console-Wait, RTC, VGA, nativer VBE-RGB-Framebuffer | Referenzhardware gut, moderne Geräte fehlen |
| Netzwerk | E1000, RTL8139, NE2000, Ethernet, ARP, IPv4, ICMP, DHCP, internes UDP-Senden | E1000/DHCP/Ping am besten verifiziert |
| USB | PCI-Erkennung eines xHCI-Controllers | nur Probe-Gerüst |
| Userspace | SDK, Shell, Editor, BASIC, zahlreiche Systemprogramme und tastaturbedienter Ring-3-Desktop mit vier App-Karten | brauchbare CLI- und Desktop-MVP-Basis |
| Qualität | Hosttests, CI-Build, Image-Validatoren, Kontextassertions, fünf Log-Level, Panic-Kontext mit Build-ID sowie serielle QEMU-Ring-3-Tests mit LAPIC/PIT und 32-/64-/256-/512-/1024-MiB-Matrix | breitere Hardware- und Fehler-Injektionsmatrix fehlt |

Maßgebliche Quellen sind der ausführbare Code und die Tests. Der aktuelle
Architekturüberblick in `docs/architecture/ARCHITECTURE_DEEP_DIVE.md` beschreibt
die vorhandene Ring-3-Isolation, den mit R1.1 eingeführten Blockier- und
Zeitvertrag und den R1.2-Vertrag für Speicher, Kernelstacks und Reaping. Der
verbindliche R1.3-Kontext- und Lockvertrag steht ergänzend in
`docs/architecture/SYNCHRONIZATION_CONTRACT.md`.

## 4. Belegte Korrektheits- und Basislücken

### C-01: Verlorenes Aufwecken beim Prozess-Wait — P0

**Status: in R0.1 behoben und in R1.1 verallgemeinert.** `syscall_wait()`
prüft den Kindstatus und blockiert auf der kindeigenen `exit_waiters`-Queue
unter derselben IRQ-geschützten Synchronisationsgrenze. Exit und Kill
veröffentlichen zuerst den Status und wecken danach alle registrierten
Waiter. Der frühere PID-Sonderpfad ist durch den generischen Wait-Queue-Vertrag
ersetzt.

### C-02: Falscher Exception-Frame für `#AC` — P0

**Status: in R0.2 behoben.** Der Prozessor legt für Alignment Check einen
Fehlercode auf den Stack. Der frühere Stub legte zusätzlich einen künstlichen Nullwert ab.
Dadurch interpretiert der gemeinsame Rückkehrpfad den restlichen CPU-Frame
falsch. Vector 17 muss wie die anderen Exceptions mit CPU-Fehlercode behandelt
und durch einen gezielten Regressionstest abgesichert werden.

### C-03: Unklarer bzw. nicht nutzbarer PRG-Relokationspfad — P0

Der beim Audit geprüfte Loader las das Image bei `0x02100000`, rief aber vor
dem Anlegen und Befüllen des Prozessadressraums `apply_relocation()` auf.
Diese Funktion erwartete die Relokationstabelle innerhalb des noch
ungemappten Zielbereichs ab `0x40000000`. Nichtleere Tabellen konnten so nicht
funktionieren; R0.3 hat diesen toten Pfad entfernt.

Die aktuelle Toolchain lehnt Laufzeitrelokationen ab und erzeugt Images für die
feste Adresse `USER_BASE`. Der Loader weist nichtleere Relokationstabellen
deshalb ausdrücklich und getestet vor dem Mapping zurück. Eine spätere
Formatversion kann Segmente und Relokationen sauber beschreiben.

### C-04: Busy-Wait und falsche Semantik bei langen Delays — P1

**Status: in R1.1 für Ring 3 behoben.** `SYS_DELAY` behält ABI-Nummer 2,
blockiert User-Tasks aber nun auf derselben geordneten Deadline-Queue wie
`sleep_ms`; der feste 10-Sekunden-Abbruch entfällt. `pit_delay()` bleibt als
bewusst aktiver Kernel-/Hardware-Helfer für Kontexte erhalten, in denen kein
Task blockiert werden darf. Der Syscall unterscheidet beide Pfade anhand des
Aufrufer-Privilegs.

### C-05: Erkannter und tatsächlich verwalteter Speicher weichen ab — P1

**Status: in R1.2 behoben.** Der gemeinsam eingeblendete, Supervisor-only
Kernelbereich und der Frame-Allocator decken konsistent die ersten 1 GiB bis
`USER_BASE` ab. Vollständige nutzbare Frames in diesem Fenster werden
verwaltet; von E820 gemeldeter Speicher oberhalb der Grenze wird nur als
`detected_usable_bytes` gezählt und nicht vergeben. `SYS_MEMORY_KB` (13)
liefert kompatibel die verwaltete Kapazität, nicht mehr den gesamten erkannten
Wert. Der versionierte `SYS_MEMORY_STATS` (43) trennt erkannt, verwaltet,
reserviert, belegt und frei sowie Heap-Kapazität und -Fragmentierung. Ein
späteres Highmem-/`kmap`-Fenster soll Speicher oberhalb 1 GiB nutzbar machen.

### C-06: Programmseiten sind pauschal schreibbar — P1

Der Loader mappt in `kernel/proc/process.c:269-301` Programm und Stack mit
`PAGE_USER | PAGE_RW`. Es gibt keine getrennten Text-/RODATA-/Datenrechte und
keine W^X-Politik. Eine neue PRG-Formatversion sollte Segmentgrenzen und Rechte
transportieren. Auf i386 ohne PAE/NX ist vollständiges W^X nicht erreichbar,
aber schreibgeschützte Code- und RODATA-Seiten sind trotzdem sinnvoll.

### C-07: Laufzeitverhalten wurde in CI nicht automatisch geprüft — P1

**Status: Kernlücke in R0.4 behoben.** CI bootet nun das erzeugte native Image,
führt den Ring-3-Test aus und sichert bei Fehlschlägen das serielle Protokoll.
Die breitere Laufzeitabdeckung für DMA, Netzwerk und weitere Hardware sowie die
separate Bereinigung der optionalen Legacy-Image-Tests bleiben Folgearbeiten.

## 5. Fehlende Funktionen nach Subsystem

Dieser Abschnitt ist eine thematische Bestandsaufnahme und keine zweite
Statusliste. Deshalb verwendet er normale Aufzählungspunkte. Verbindliche
Erledigt-/Offen-Checkboxen stehen ausschließlich in der Fortschrittsübersicht,
in der Arbeitsreihenfolge, im unmittelbar nächsten Arbeitspaket und in echten
Abnahmechecklisten.

### Prozess, Scheduler und IPC

- **S0.3a umgesetzt:** 16 statische Endpoints, acht Capabilities je Prozess,
  vier Nachrichten je Queue, 128 Byte Nutzlast, blockierendes Send/Receive,
  explizite abschwächende Delegation ohne `CONTROL` und vollständiger
  Exit-Widerruf
- **umgesetzt:** ein Taskslot, ein Prozessslot und 32 Frames bleiben für
  explizite Supervisor-Spawns reserviert
- **umgesetzt:** vollständiges 56-Syscall-Inventar; Default-Deny-Probeprofil
  und generation-sicheres Child-Kill vor jedem Seiteneffekt
- generische Wait-Queues auf weitere Geräte- und Protokollereignisse anwenden
- Pipes, Prozessgruppen und ein kleines Signalmodell
- `waitpid(-1, ...)`, optionales nichtblockierendes Warten und saubere
  Reparenting-/Reaper-Semantik
- dynamische oder zumindest deutlich größere Tasktabelle statt `MAX_TASKS 8`
- Prioritäten erst nach korrekter Blockierung; Threads und SMP deutlich später
- echte User-Stack-Guardpages zusätzlich zu den vorhandenen Kernel-Guardpages
- aussagekräftigere Prozessstatistiken

### Syscall- und Userspace-ABI

R1.1 erweitert die bestehende ABI ohne Umnummerierung: `SYS_YIELD` (40),
`SYS_SLEEP_MS` (41) und `SYS_MONOTONIC_MS` (42) sind im SDK verfügbar.
`SYS_DELAY` (2) bleibt kompatibel und blockiert Ring-3-Tasks über dieselbe
Sleep-Infrastruktur; `SYS_UPTIME` (12) liefert weiterhin das niedrige
32-Bit-Wort. Die vollständige monotone 64-Bit-Zeit schreibt Syscall 42 nach
Pointerprüfung in den Userspace. R1.2 ergänzt `SYS_MEMORY_STATS` (43). Dessen
v1-Struktur beginnt mit `uint32_t version` und `uint32_t struct_size`, ist
88 Byte groß und enthält zehn `uint64_t`-Zähler. Der Aufrufer übergibt Größe
und Version separat; der Kernel lehnt unbekannte Versionen und zu kleine
Puffer vor dem geprüften Copyout ab. Der ältere `SYS_MEMORY_KB` (13) bleibt
erhalten und meldet die vom Frame-Allocator verwalteten KiB. R1.4 hängt
`SYS_DISPLAY_INFO` (44), `SYS_FILL_RECT` (45) und `SYS_DRAW_TEXT` (46) an.
Die versionierten Requests werden geprüft kopiert und geclippt; Ring 3 erhält
kein direktes Framebuffer-Mapping. `SYS_RENAME` (47) ergänzt atomisches Rename
innerhalb desselben FAT32-Verzeichnisses; volumen- oder
verzeichnisübergreifendes Verschieben wird fail-closed abgelehnt.
`SYS_FSYNC` (48) führt writable Deskriptoren über Prozess-FD und VFS bis zu
einem begrenzten ATA-`FLUSH CACHE`; Timeout oder Storage-Fence werden als
Fehler an Ring 3 zurückgegeben. S0.3a hängt `SYS_IPC_CREATE` (49),
`SYS_IPC_SEND` (50), `SYS_IPC_RECEIVE` (51) und `SYS_IPC_CLOSE` (52) an. Die
versionierte 128-Byte-Nachricht wird vollständig über validierte User-Kopien
übertragen; rohe Kernelpointer verlassen die Prozessgrenze nicht.
`SYS_IPC_DELEGATE` (55) bindet eine nichtleere Teilmenge von `SEND|RECEIVE`
an die unter Präemptionsschutz aufgelöste aktuelle Generation einer Ziel-PID.
`CONTROL` und Ambient-Spawn-Vererbung sind ausgeschlossen.

- eine einzige gemeinsame, versionierte Quelle für Syscallnummern und
  Fehlercodes
- stabile `errno`-ähnliche Fehlersemantik; aktuell werden VFS-Fehler oft auf
  allgemeine Werte wie `-2`, `-5` oder `-9` reduziert
- `open`-Flags (`RDONLY`, `WRONLY`, `RDWR`, `CREATE`, `TRUNC`, `APPEND`)
- `lseek`, `fstat`, `truncate`, später `dup`/`dup2`; FAT32/ATA-`fsync` und
  Rename sind als erste persistente Spezialfälle vorhanden
- echte Deskriptoren 0/1/2 für Standard-Ein-/Ausgabe
- ABI-Fähigkeitsabfrage, damit ältere Programme kontrolliert weiterlaufen

### VFS, Dateisysteme und Blockgeräte

- Rename auf FAT12 sowie verzeichnis- und volumenübergreifendes Verschieben;
  FAT32 Same-Directory-Replace ist journalgestützt atomar
- konsistente Open-Handle-, Delete- und Unmount-Semantik unter Nebenläufigkeit
- eine generische `block_device`-Schnittstelle mit `read`, `write`, `flush`,
  Sektorgröße und Kapazität statt direkter ATA/FDD-Kopplung
- Partitionen als eigene Blockgeräte; derzeit wird pro physischem Laufwerk nur
  ein gefundenes Dateisystem automatisch gemountet
- vollständige MBR-Prüfung und später GPT; Partitionsgrenzen bei jeder I/O
  erzwingen
- ATA LBA48 und Multi-Sektor-I/O; aktuell ist der PIO-Treiber auf LBA28
  begrenzt (`drivers/block/ata.h:25`)
- FAT-Schreibreihenfolge, Fehlerpropagation und Power-Loss-Tests
- FAT Long File Names und eine definierte Zeichenkodierung
- FAT-Zeitstempel über VFS; FAT32 liefert derzeit Nullen
- mehrere FAT12-Volumes; der Adapter besitzt aktuell nur einen globalen
  `mounted_fat12_fs`
- EXT2 entweder klar dauerhaft read-only halten oder erst nach den
  Zuverlässigkeitsarbeiten vollständig schreibbar machen

### Terminal, Shell und Desktop

Die heutige Console-Eingabe vermeidet bereits Busy-Waiting für reguläre
Ring-3-Tasks: `getchar` prüft den Puffer und reiht den Task atomar auf der
gemeinsamen Input-Wait-Queue ein; PS/2 und COM1 wecken alle Leser zur erneuten
Prüfung der level-getriggerten Pufferbereitschaft.
Eine vollständige TTY-Schicht bleibt der nächste darüberliegende Ausbau.
Der Desktop-MVP umgeht die feste Terminalgeometrie für seine Oberfläche über
Pixelrechtecke und Pixelschrift. Er startet vorhandene Apps jedoch bewusst als
einzelne Vollbild-Kindprozesse und ist noch kein Fenstersystem.

- TTY-Abstraktion mit kanonischem/raw Modus, Echo und per-Prozess
  Vordergrundgruppe
- `Ctrl+C` als Signal an die Vordergrundgruppe statt Sonderbehandlung direkt
  im `getchar`-Syscall
- dynamische Terminalgröße; mehrere Syscalls prüfen heute fest gegen 80x25,
  obwohl der Framebuffer andere Größen besitzen kann
- Quotes/Escapes, Umgebungsvariablen, Verlauf und Exitcodes in der
  Userspace-Shell
- Pipes, Ein-/Ausgabeumleitung und Hintergrundjobs nach Fertigstellung von
  Deskriptoren, Wait-Queues und Signalen
- Editor: das sichere `TEMP -> fsync -> close -> rename` ist umgesetzt;
  dynamischer Puffer, Suche, Auswahl/Clipboard und die Aufhebung des
  Limits von 200 Zeilen fehlen
- ein kleines Ring-3-`init` als PID 1 statt direktem Shellstart durch den Kernel
- Mausereignisse, Fokusmodell, Compositor und Windowmanager als getrenntes
  späteres Paket statt Erweiterung der schmalen Display-ABI

### Netzwerk

- ARP-Erneuerung sowie DHCP-Renew/Rebind
- robuste IPv4-Fehlerpfade und definierter Umgang mit Fragmenten; aktuell
  werden Fragmente verworfen
- nutzbares UDP-Binding: `udp_bind()` ist in
  `drivers/net/netstack.c:961-964` noch ein Stub
- Socketobjekte als Dateideskriptoren und Userspace-Syscalls
- DNS-Resolver nach funktionierenden UDP-Sockets
- TCP mit Zustandsautomat, Sequenznummern, Retransmission, Timern, Fenstern und
  sauberem Verbindungsabbau; alle vier öffentlichen TCP-Funktionen sind derzeit
  Stubs (`drivers/net/netstack.c:966-970`)
- erste Anwendungen wie `netcat` und ein kleiner HTTP/1.0-Client
- IPv6 erst nach einer belastbaren IPv4-/Socket-Schicht
- VMXNET3 nur implementieren, wenn E1000 nicht mehr als VMware-Referenz reicht;
  der vorhandene Treiber deaktiviert das Gerät absichtlich

### USB und moderne Hardware

- DMA-API für physische Adressen, Alignment, 32-Bit-Grenzen und kohärente
  Puffer
- vollständiger xHCI-Reset und Controllerstart, Command-/Event-/Transfer-Ringe,
  Doorbells und Interrupts
- Root-Port-Status, Geräteadressierung, Deskriptoren und Konfiguration
- Hub-Unterstützung, danach HID-Tastatur und USB-Massenspeicher
- der heutige Code endet nach PCI/BAR/IRQ-Ausgabe und lässt xHCI deaktiviert
  (`drivers/usb/xhci.c:4-25`); `hub.c` und `hid_kb.c` sind Platzhalter
- zentrale, validierte ACPI-Schicht statt der isolierten experimentellen
  HPET-Suche; danach HPET als optionalen Clocksource, IOAPIC und
  Poweroff/Reboot integrieren
- AHCI/NVMe erst nach Block- und DMA-Abstraktion

### Sicherheit, Diagnose und Produktreife

- dokumentiertes Bedrohungsmodell: vorerst vertrauenswürdiger Single-User oder
  später Benutzer/Rechte/Capabilities
- Prozess-Kill ist generation-sicher auf eigene Kinder bzw. explizite
  Supervisor-Autorität begrenzt
- Dateirechte und ACLs; FAT besitzt derzeit keine Berechtigungsmetadaten
- keine kryptografische Boot-Authentizität: CRC32 erkennt Beschädigung, aber
  keinen absichtlichen Austausch
- Zufallsquelle/CSPRNG, ASLR und sichere Netzwerk-Defaults erst bei einem
  Sicherheitsziel
- optionale Panic-Symbolauflösung zusätzlich zum vorhandenen Register-/CR2-
  Kontext und der SHA1-Build-ID
- Debug-Buildprofil, statische Analyse und hostseitiges Sanitizer-/Fuzzing für
  Parser
- reproduzierbare Gasttests und eine kleine Hardwarematrix

## 6. Abhängigkeiten

```text
P0-Korrektheit
  -> Wait-Queues + monotone Zeit [R1.1 erledigt]
       -> Pipes + TTY + Signale -> Shell-Jobs
       -> Socket-Deskriptoren -> UDP -> DNS -> TCP -> Anwendungen

R1.3-Kontext-/Lockvertrag [erledigt] -> VFS/Blockgeräte + ACPI/DMA
Native VBE + Display-ABI [R1.4 erledigt] -> Desktop-Härtung -> später Fenstersystem/Maus
ABI/FD-Ausbau -> VFS rename/truncate/fsync -> sicherer Editor und Dateitools
Blockgeräte -> Partitionen + DMA -> AHCI/NVMe und USB-Massenspeicher
ACPI + DMA -> xHCI -> USB-Enumeration -> HID/Storage
R1.2-Directmap bis 1 GiB -> Highmem/kmap oberhalb 1 GiB -> später x86-64/SMP

Generic High-Assurance Gate S0
  -> Einsatzprofil + Gefahren + Essential Functions + FTTI
  -> Fehlerdomänen + minimaler Safety-Kern + unabhängiger Supervisor
  -> Stack-/Exception-Containment + Watchdog + Knoten-Failover
  -> deterministische Ressourcen + persistente Integrität + sichere Updates
  -> Safety Case + Traceability + Fault-Injection + Langzeitnachweis
  -> erst danach weitere Funktionspakete
```

TCP oder USB vor diesen Grundlagen zu bauen würde dieselben Warte-, Timeout-,
Deskriptor- und Pufferprobleme mehrfach lösen lassen.

## 7. Schrittweiser Implementierungsplan

Die Größen `S`, `M`, `L` und `XL` sind relative Aufwände, keine Zeitangaben.
Es soll immer nur das erste noch offene Paket begonnen werden, sofern dessen
Abhängigkeiten erfüllt sind. Die folgenden Listen beschreiben Umfang und
Reihenfolge innerhalb eines Pakets; sie sind keine zweite Statusanzeige und
verwenden deshalb normale Aufzählungspunkte. Der verbindliche Erledigt-/Offen-
Status steht in den Abschnitten 2.1, 8 und 10.

### Phase 0 — Korrektheit und verlässliche Nachweise

#### R0.1 Atomarer Wait/Wakeup-Pfad — S

**Status (13. August 2026):** Kernfix umgesetzt und hostseitig validiert. Der
Statuscheck und die Registrierung als `TASK_WAITING` sind auf dem aktuellen
Single-Core-Kernel durch einen gemeinsamen IRQ-geschützten Abschnitt atomar.
Der neue Regressionstest `test/test_wait_source.py` schützt diese Invariante;
die aktuelle Gesamtabnahme umfasst 134 Hosttests, den
Windows-QEMU-Referenzbuild und 64
erfolgreiche Spawn/Wait-Zyklen im automatisierten Gasttest.

- Kindstatusprüfung und Registrierung des aktuellen Tasks als `TASK_WAITING`
   auf dem Single-Core-System in einem gemeinsamen IRQ-geschützten Abschnitt
   ausführen.
- Kind-Exit, Kill und normales Exit über denselben Wakeup-Pfad führen.
- Status genau einmal konsumieren; verwaiste Zombies kontrolliert aufräumen.
- Einen deterministischen Test-Hook für einen Kind-Exit im bisherigen
   Race-Fenster sowie einen Gast-Stresstest mit vielen Spawn/Wait-Zyklen bauen.

**Fertig, wenn:** Kein Test hängt, jeder Exitstatus wird genau einmal geliefert
und der Elternprozess verbraucht während des Wartens keine CPU.

#### R0.2 Exception-Stubs vereinheitlichen — S

**Status (13. August 2026):** Umgesetzt. Alle Vektoren 0–31 verwenden die
Makros `ISR_NO_ERROR_CODE` oder `ISR_CPU_ERROR_CODE`; Vector 17 (`#AC`) nutzt
nun korrekt den CPU-Fehlercode. Compile-Time-Assertions sichern Offsets und
Größe des gemeinsamen `Registers`-Frames. Die vollständige Fehlercode-Matrix
ist hostseitig getestet.

- Vektoren mit und ohne CPU-Fehlercode tabellarisch definieren und Stubs aus
   zwei Makros erzeugen.
- Vector 17 korrigieren; 8, 10–14, 17, 21, 29 und 30 explizit prüfen.
- Layout von Assembly-Frame und `Registers` mit statischen Offsets absichern.
- Ring-3-Tests für Divide-by-zero, Invalid Opcode und Page Fault ergänzen;
   kontrollierte Testpfade für Fehlercode-Exceptions hinzufügen.

**Fertig, wenn:** User-Exceptions beenden nur den Verursacher, Kernel-Exceptions
liefern einen korrekten Diagnoseframe und kein `iret` verwendet verschobene
Stackdaten.

#### R0.3 PRG-v1-Vertrag festziehen — S

**Status (13. August 2026):** Umgesetzt. Validator, Loader, Builder und
Dokumentation akzeptieren ausschließlich die feste Basis `0x40000000`, keine
Relokationen und keine Nachlaufbytes. Historische, unbenutzte ELF- und
Relokationspfade im Kernel wurden entfernt; negative Hosttests decken falsche
Basen, Relokationen, Überläufe, Überlappungen und ungültige Einstiegspunkte ab.

- Für Version 1 nur `base_address == USER_BASE` und
   `relocation_size == 0` akzeptieren.
- Validator, Loader, Python-Builder und Dokumentation auf dieselben Regeln
   bringen; tote alternative ELF-Lader aus dem Laufzeitpfad entfernen.
- Negative Tests für Relokationen, Überläufe, überlappende Bereiche und
   falsche Entry-Points ergänzen.
- Anforderungen an ein späteres segmentbasiertes PRG v2 separat notieren.

**Fertig, wenn:** Jedes vom Builder erzeugte Image lädt und jede nicht
unterstützte Variante vor dem Mapping eindeutig abgelehnt wird.

#### R0.4 Automatisierter Gast-Smoke-Test — M

**Status (13. August 2026):** Umgesetzt. `scripts/run_qemu_smoke.py` bootet das
native Image headless und unveränderlich in QEMU, startet `GTEST.PRG` über
COM1 und erzwingt die geordnete Markerfolge `BOOT_OK` → `TEST_OK` mit hartem
Timeout. Der Ring-3-Gasttest prüft 64 Spawn/Wait-Zyklen, genau einmal
konsumierbare Exitstatus, Datei-I/O über mehrere 512-Byte-Syscall-Blöcke sowie
kontrollierte `#DE`-, `#UD`- und `#PF`-Prozessabbrüche. Makefile und CI führen
den Test aus und bewahren das serielle Protokoll.

- QEMU mit serieller Konsole, festem Timeout und eindeutigem
   `BOOT_OK`/`TEST_OK`-Protokoll starten.
- Einen Ring-3-Teststarter ins Image legen, der Userspace-Schutz,
   Spawn/Wait, Datei-I/O und Exceptions prüft; nach dem Erfolg beendet der
   Host QEMU kontrolliert.
- Den Smoke-Test direkt an das erzeugte `build/reist-os.img` binden;
   Legacy-Fixture-Tests getrennt halten.
- Den Smoke-Test in CI ausführen und Logs als Artefakt sichern.

**Fertig, wenn:** Ein CI-Lauf nicht nur kompiliert, sondern bis Ring 3 bootet,
Tests ausführt und bei Panic, Triple Fault oder Timeout fehlschlägt.

### Phase 1 — Scheduler, Zeit und Speicher als gemeinsame Grundlage

#### R1.1 Wait-Queues, Sleep und Yield — L

**Status (13. August 2026): Abgeschlossen.** Tasks besitzen genau einen
intrusiven Wait-Knoten; FIFO-Wake-one, Wake-all, Entfernen und stabile
Deadline-Sortierung sind als generische Operationen implementiert. Der
Prozess-Wait prüft Zustand und Queue-Registrierung atomar und verwendet die
kindeigene Exit-Queue. Kill, Exit und Task-Slot-Wiederverwendung entfernen
veraltete Queue-Mitgliedschaften.

Die PIT-IRQ liefert eine atomar gelesene, 64-Bit-monotone Millisekundenzeit.
Eine Bruchteilakkumulation berücksichtigt den tatsächlich programmierten
PIT-Divisor, statt jeden IRQ ungenau als exakt eine Millisekunde zu behandeln.
Sleep verwendet eine geordnete 64-Bit-Deadline-Queue, setzt den Taskstatus auf
`TASK_SLEEPING` und gibt den Prozessor ab; `yield` wechselt ohne Pollschleife
zu einem anderen bereiten Task. Die Console-Eingabe blockiert nach atomarer
Leerprüfung auf einer gemeinsamen Wait-Queue und wird sowohl durch PS/2- als
auch COM1-Eingabe geweckt.

Der lokale APIC-Timer wird gegen die bereits laufende PIT-Zeit kalibriert und
treibt danach die Scheduler-Quanten. Fehlt der LAPIC, schedult IRQ0 nach dem
PIC-EOI über den PIT-Fallback. HPET gehört bewusst nicht zu R1.1; seine
Integration bleibt bis zur validierten ACPI-/Plattformbasis in R5.1
zurückgestellt.

- Generische Wait-Queues mit Wake-one/Wake-all einführen.
- 64-Bit-monotone Zeit und geordnete Deadline-Liste implementieren.
- `sleep_ms` und `yield` als Syscalls anbieten; `SYS_DELAY` kompatibel darauf
   abbilden.
- Keyboard-, serielle und später Netzwerk-I/O auf blockierende Events
   vorbereiten.
- APIC-Timer gegen PIT kalibrieren und bei fehlendem LAPIC einen
   Scheduler-Fallback bereitstellen.

**Abnahme erfüllt:** 134 Hosttests prüfen unter anderem Queue-Invarianten,
stabile Deadline-Reihenfolge, atomare 64-Bit-Lesezugriffe, ABI-Nummern und die
PIC-EOI-Reihenfolge des Fallbacks. `GTEST.PRG` beobachtet einen Kindprozess als
`SLEEPING`, führt währenddessen einen anderen Task aus, prüft Deadline-Wakeup,
`yield`, den kompatiblen `SYS_DELAY`, ungültige 64-Bit-Ausgabezeiger sowie
Kill und Task-Slot-Reuse. Der Gasttest ist sowohl für den kalibrierten
LAPIC-Pfad als auch separat mit `-cpu qemu32,-apic` für den PIT-Fallback in
Makefile und CI verdrahtet. Die 64-Bit-Deadline vermeidet den früheren
32-Bit-Tick-Wrap als praktische Laufzeitgrenze.

#### R1.2 Speicherverwaltung und Schutz — L

**Status (13. August 2026): Abgeschlossen und abgenommen.** Die E820-Einträge
werden sortiert und überlappungsfrei normalisiert. Nicht nutzbare Bereiche,
Multiboot-Strukturen und Module übersteuern nutzbare Einträge; beschädigte,
abgeschnittene oder wegen der festen Regionstabelle nicht vollständig
darstellbare Bootkarten werden fail-closed verworfen.

Der Supervisor-only Kernelanteil bildet die ersten 1 GiB physisch direkt ab
und endet exakt an `USER_BASE`. Der Frame-Allocator verwaltet nur vollständige
E820-Frames innerhalb dieses Fensters und sucht ab einem umlaufenden
Next-Fit-Hinweis. Speicher oberhalb 1 GiB bleibt in
`detected_usable_bytes` sichtbar, wird ohne späteres Highmem-/`kmap`-Fenster
aber nicht vergeben. Für die Framezähler gilt:

```text
managed_bytes = reserved_bytes + allocated_frame_bytes + free_frame_bytes
```

Der Kernel-Heap startet mit etwa 1 MiB und wächst bei Bedarf in mindestens
256-KiB-Schritten aus zusammenhängenden, im Directmap dauerhaft reservierten
Frames. Die adresssortierte Blockliste teilt Blöcke und vereinigt nur physisch
angrenzende Nachbarn; getrennte Arenen werden nie über Lücken hinweg
zusammengelegt. Der Heap schrumpft derzeit nicht und `k_malloc`/`k_free` sind
wegen IRQ-Sperre, Metadatensuche und möglichem Frame-Scan keine APIs für harten
IRQ-Kontext.

R1.2 führte zunächst 64-Byte-Canaries ein. S0.2 hat sie inzwischen durch echte
nicht-präsente Guardpages ersetzt: eine volle Seite unter dem Bootstack sowie
je eine Seite unter und über jedem dynamischen 8-KiB-Kernelstack. Scheduler-
Grenzen prüfen Slot, Mapping und ESP-Bereich. Beendete Tasks wechseln beim
atomaren, owner- und
generationsvalidierten Detach in `TASK_REAPING`. Seitentabellen und Kernelstack
werden anschließend mit aktivierten Hardware-Interrupts, aber unterdrückter
Taskpräemption freigegeben; erst danach darf der Slot wiederverwendet werden.

- Erkannte, verwaltete, reservierte und freie Frames separat zählen.
- Framezugriff oberhalb 256 MiB durch ein konsistentes Directmap bis 1 GiB
   ermöglichen und die Grenze explizit ausweisen.
- Kernel-Heap erweiterbar machen und belegte/freie Bytes exportieren.
- Statische und dynamische Kernelstacks mit 64-Byte-Canaries und
   ESP-Bereichsprüfungen schützen.
- Allokationsfehler, Fragmentierung und wiederholtes Prozess-Reaping testen.

**Abnahme erfüllt:** `MEMINFO` nutzt die versionierte 88-Byte-v1-Struktur von
`SYS_MEMORY_STATS` (43); `SYS_MEMORY_KB` (13) meldet weiterhin kompatibel die
verwalteten KiB. Boottests prüfen Allokation, Reallokation, Freigabe,
Fragment-Reuse, Heapwachstum und einen schreibbaren Frame ab 256 MiB. Der
Ring-3-Test prüft die Zählerinvariante, ungültige User-Pointer, User-
Allokation/Freigabe und 64 Spawn/Exit/Wait/Reap-Zyklen ohne Frame- oder
Heapdrift. Der PRG-Loader hält Images nicht mehr in einem festen physischen
8-MiB-Stagingbereich, sondern lädt sie in einen passend großen temporären
Kernelheap-Puffer, kopiert sie in private Userseiten und gibt den Puffer auf
jedem Erfolgs- und Fehlerpfad frei. Dadurch bootet CI den vollständigen
Ring-3-Test mit 32, 64, 256, 512 und 1024 MiB; bei 512 MiB werden sowohl der
kalibrierte LAPIC-Pfad als auch der PIT-Fallback ohne APIC ausgeführt.

Nicht Teil von R1.2 waren systematische Failure-Injection für jede
Teilallokation, ein Highmem-/`kmap`-Fenster oberhalb 1 GiB, Guardpages und ein
IRQ-tauglicher Allocator. Die Kernel-Guardpages sind nun der erste umgesetzte
Teil von S0.2; die übrigen Punkte bleiben offen.

#### R1.3 Synchronisations- und Diagnosevertrag — M

**Status (13. August 2026): Abgeschlossen und abgenommen.** Der Kernel
unterscheidet harten IRQ-Kontext, IRQ-deaktivierten Foreground-Kontext,
präemptionsgeschützten Foreground-Kontext und schlaffähigen Taskkontext.
Benannte Assertions prüfen die erlaubten IRQ-, Interrupt-, Präemptions- und
Schlafzustände. IRQ-Verschachtelung endet vor jedem Scheduler-Tail; blockierende
Operationen dürfen keine Präemptionsgrenze überschreiten.

Die globale Lockordnung lautet `VFS -> DATEISYSTEM -> TREIBER -> SCHEDULER`,
innerhalb der Speicherverwaltung gilt `HEAP -> FRAME`. VFS-Operationen und die
ATA-/FDD-Datenpfade sind entsprechend serialisiert. Netzwerk- und HPET-ISRs
beschränken sich auf Quittierung und Pending-Markierung; die eigentliche Arbeit
läuft außerhalb des harten IRQ-Kontexts.

Der Logger unterstützt `TRACE`, `DEBUG`, `INFO`, `WARN` und `ERROR` mit
Komponentenpräfix und Mindestlevel. Panic- und Exceptionausgaben enthalten den
vollständigen Registerframe, CR2 und die 40-stellige SHA1-Build-ID des Kernels.
Vertrags-/Regressionstests, Windows-Referenzbuild und QEMU-Ring-3-Smoke sichern
die Umsetzung ab.

- Festlegen, welche APIs in IRQ-Kontext, mit deaktivierter Präemption oder
   schlafend aufgerufen werden dürfen.
- Lock-Reihenfolge für Scheduler, VFS, Dateisysteme und Treiber dokumentieren.
- Assertions für IRQ-/Lock-Zustand sowie strukturierte Log-Level ergänzen.
- Panic-Ausgabe um vollständige Register, CR2 und Build-ID erweitern.

#### R1.4 Grafischer Desktop-MVP — M

**Status (13. August 2026): Abgeschlossen und abgenommen.** Dieser Meilenstein
wurde auf Wunsch bewusst vor R2.1 eingeschoben. Der native Stage-2-Loader
wählt in Framebuffer-Builds bevorzugt VBE 1024x768x32 und danach 800x600x32.
Ein fehlender oder ungültiger Modus fällt ohne gesetztes Framebuffer-Flag auf
BIOS-Modus 03h und VGA-Text zurück.

Die angehängten Syscalls 44 bis 46 bilden eine versionierte Ring-3-Display-ABI
für Modusinformationen, geclippte Rechtecke und geclippte Pixelschrift. Farben
verwenden `0x00RRGGBB`; alle Userdaten werden geprüft kopiert und der LFB bleibt
Supervisor-only. Framebuffer-Console-Ausgaben erscheinen zusätzlich auf COM1.

`DESKTOP.PRG` wird nur bei einem echten Framebuffer vor `SHELL.PRG` gestartet.
Vier tastaturbediente Karten öffnen Shell, Dateiliste, Editor oder
Systeminformationen als Vollbild-Kindprozess. Der Desktop wartet auf dessen
Ende und zeichnet sich danach neu. Ein realer QEMU-Lauf erreicht den seriellen
Marker `DESKTOP_OK`. Maus, Compositor, Windowmanager und Fokusmodell gehören
ausdrücklich nicht zu diesem MVP.

### Sicherheits-Gate S0 — vor weiterer Funktionsentwicklung

**Status (13. August 2026): begonnen, nicht abgenommen.** S0 ist die
Eintrittsbedingung für alle folgenden Phasen. Bis S0.1 abgenommen ist, sind nur
Änderungen zulässig, die Sicherheit, Isolation, Diagnose, Verifikation oder
Reproduzierbarkeit erhöhen.

#### S0.1 Einsatzprofil, Gefahren und Assurance Case — M

- Zielsystem, Einsatzprofil, Umgebung und vorhersehbaren Fehlgebrauch festlegen.
- Essential Functions, sichere/degradierte Zustände und je Gefahr die FTTI
   definieren.
- Ein versioniertes Gefahrenregister mit Ursache, Kontrolle, Restrisiko und
   Verifikationsnachweis anlegen.
- Traceability `Gefahr -> Anforderung -> Design -> Code -> Test -> Ergebnis`
   automatisiert prüfen.

#### S0.2 Stack-, Exception- und Panic-Containment — L

**Teilstatus:** Kernel-Taskstacks besitzen beidseitige nicht-präsente
Guardpages; der Bootstack eine volle untere Guardpage. `#DF` läuft über eine
dedizierte TSS und einen unabhängigen Emergency-Stack in einen begrenzten,
heap-/lockfreien Crashrecord-/COM1-Pfad. Explizite beidseitige
User-Stack-Guardpages samt Ring-3-Fault-Test sind umgesetzt. Noch offen sind
Dynamische Frames und compilerseitig erkannte statische Einzelframes über 4096
Byte brechen jeden Kernelbuild ab. Ein prüfsummengeschützter, redundant in
reserviertem RAM und CMOS/NVRAM gespeicherter Crashrecord überlebt den nativen
Reset, wird beim Boot einmal gemeldet und
der #DF-Pfad fordert begrenzt einen Reset an. Das QEMU-Profil besitzt nun einen
echten IB700-Watchdog, der nur nach Schedulerfortschritt gefüttert wird und im
Fatalpfad ausläuft. Noch offen sind Callgraph-Gesamtbudgets, ein von CPU und
Versorgung unabhängiger Zielhardware-Watchdog samt Fencing. Der echte
Double-Fault-Task-Gate-Pfad wird inzwischen in einem isolierten Testimage bis
zum Watchdog-Warmstart, Crashrecord-Recovery und anschließenden Gasttest geprüft.

- Nicht gemappte Guardpages für jeden Kernel- und Userstack, statische
   Stackbudgets, Watermarks und Rekursionsverbote einführen.
- Einen reservierten Exception-/Double-Fault-/NMI-Notfallstack mit
   vorallokiertem, beschränktem Crashdatensatz bereitstellen.
- Wiederherstellbare Prozess-/Dienstfehler von möglicher globaler
   Kernelkorruption trennen; betroffene Domänen einfrieren und aus bekannt
   gutem Zustand neu starten.
- `panic()` darf nicht nur `halt()` ausführen: Ausgänge zuerst in den
   gefahrenspezifisch sicheren Zustand bringen, Diagnose begrenzt sichern und
   einen unabhängigen Supervisor Failover oder Neustart ausführen lassen.
   In-Place-Weiterlauf nach unbekannter Kernelkorruption bleibt verboten.

#### S0.3 Fehlerdomänen, Supervisor und Redundanz — XL

**Teilstatus:** Ein fester, allokationsfreier Supervisor-Kern verwaltet acht
ECC-geschützte Domänenzustände mit Deadlines, Generation/Epoche,
Restartbudgets und der zwingenden Reihenfolge `timeout -> fence -> restart ->
self-test -> reintegrate`. Eine statische, bis zum Reboot verriegelte
Output-Fence-Registry ist mit dem Fatalpfad verbunden. Ihr erster realer Hook
sperrt Netzwerk-TX logisch und schaltet die Sender der unterstützten NICs
best-effort ab. Die Migration realer Dienste in eigene Fehlerdomänen sowie
rücklesbare externe Interlocks und unabhängige Supervisorhardware bleiben
offen.

**Architektur-Gate:** Der Supervisor innerhalb des heutigen `kernel.bin`
verbessert Erkennung und Fencing, erzeugt aber noch keine unabhängige
Failure Domain. S0.3a stellt nun die erste begrenzte IPC-/Capability-Basis
bereit; Storage, Netzwerk und komplexe Treiber verbleiben trotzdem in Ring 0.
Erst getrennte, capability-beschränkte und neu startbare Dienste schließen das
Gate. Die Abnahme verlangt Fault-Injection, die einen ganzen Dienst beendet
oder korrumpiert, während nicht betroffene Essential Functions innerhalb ihrer
Profilbudgets weiterlaufen.

##### S0.3a Bounded IPC/Capabilities v1 — umgesetzt

Der Kernel besitzt 16 statische Endpoints, acht lokale Capabilities je Prozess,
vier Nachrichtenplätze je Endpoint und eine maximale Nutzlast von 128 Byte.
Nach der Initialisierung benötigt der Pfad keinen Heap. Ein 32-Bit-Handle
verbindet einen Endpoint-Slot im unteren Byte mit einer 24-Bit-Generation;
zusätzlich werden Halter und Eigentümer über PID und Prozessgeneration geprüft.
Die Rechte sind `SEND`, `RECEIVE` und `CONTROL`. Der Erzeuger behält `CONTROL`.
Eine explizite Delegation bindet eine nichtleere Teilmenge von `SEND|RECEIVE`
an die aktuelle Prozessgeneration der Ziel-PID; Spawn selbst vererbt keine
IPC-Rechte.
Mehrparteienrouting ist nicht Bestandteil von v1. Send und Receive blockieren
auf festen Wait-Queues und besitzen endliche, auf `pit_monotonic_ms` basierende
Deadlines. Timeout null liefert ohne Blockierung `EAGAIN`, eine abgelaufene
positive Deadline `ETIMEDOUT`. Die kompatiblen Syscalls 50/51 verwenden einen
endlichen Standard; die angehängten Syscalls 53/54 nehmen den Timeout explizit
entgegen. Ein fester `MAX_TASKS`-Scan weckt abgelaufene Warter, ohne den
einzigen intrusiven Wait-Knoten doppelt einzureihen.
Close oder Eigentümer-Exit widerrufen den Endpoint, entfernen alle abgeleiteten
Einträge und wecken blockierte Peers. Host- und Ring-3-Gasttests decken
Nachrichtenaustausch, Rechteabschwächung, Ressourcenlimits, Close-Wakeup und
Exit-Revoke ab.

S0.3a ist ausdrücklich nur der Mechanismus-Unterbau. Endliche Deadlines,
CRC-/`critical_object`-Schutz und abschwächende Delegation sind umgesetzt.
Das versionierte Probeprofil ist default-deny und lässt nur die begrenzten
Lifecycle-, Zeit-, Diagnose- und IPC-Operationen zu. Datei-, Display-, Spawn-,
Prozesslisten-, Delegations- und Kill-Autorität bleiben gesperrt. Bestehende
Programme laufen ausschließlich über ein explizites Kompatibilitätsprofil;
auch dort ist Kill auf generation-sicher gebundene eigene Kinder begrenzt.

##### S0.3b Supervised Userspace Probe Domain — abgeschlossen

Die statisch profilierte Ring-3-Probe ist umgesetzt. Eine deterministische
Testsequenz injiziert Crash, Heartbeat-Hang und ungültige Antwort. Der
Supervisor führt jeweils `fence -> revoke/reap -> recreate -> self-test ->
reintegrate` mit einer 2-s-Heartbeat- und 1-s-Recovery-Deadline sowie einem
Budget von vier Restarts aus. Ein
versionierter Health-Syscall 56 bindet Meldungen an PID und Generation; jeder
Ersatzprozess muss einen neuen IPC-Endpoint nachweisen. QEMU bestätigt die
geordnete Markerfolge und parallelen GTEST-Fortschritt mit LAPIC, PIT,
IB700-Watchdog sowie 32/64/256/1024 MiB RAM. Das ist ein belastbarer
Prozess-Failure-Domain-Nachweis, jedoch keine Unabhängigkeit von Kernel, CPU
oder RAM. S0.3c migriert nun echte Dienste aus Ring 0.

Das Restart-Gate akzeptiert keine vertrauensbasierte Fence-Bestätigung mehr:
Jede Domäne muss einen Apply- und einen separaten Verify-Hook bereitstellen.
Der atomar beanspruchte Zustand `FENCING` verhindert Doppelaufrufe; nur eine
positive Rückleseprüfung führt zu Restart, jeder Fehler zu `SAFE_STATE`.
Deadlineprüfungen laufen zusätzlich in einem festen 10-ms-Raster aus der
monotonen PIT-Zeit. Der Clockpfad persistiert ausschließlich `ISOLATED`; die
potenziell blockierende Hardwareaktion verbleibt im Foreground. Ein Aufruf von
`supervisor_service_one()` verarbeitet maximal eine Fence-/Verify-Aktion;
Restart und Selbsttest bleiben explizite Folgeereignisse. Der Executor läuft in
einem reservierten Kernel-Worker alle 10 ms; dieser beansprucht einen der acht
Task-Slots. Zusätzlich bleiben ein Taskslot, ein Prozessslot und 32 Frames per
Admission Control exklusiv für einen Supervisor-Restart verfügbar.
Restart-/Safe-State-Ereignisse bleiben level-triggered.
Beim Safe State wird derzeit konservativ das globale Output-Fence verriegelt.
Mit `network-tx` ist die erste reale Domäne registriert. Ihre 250-ms-Deadline
gilt ausschließlich während einer Sendetransaktion; Idle erzeugt daher keinen
falschen Ausfall. Der 64-Bit-Fortschritt vermeidet ein Langzeit-Wrap. Nach
Timeout folgen Software-Latch, NIC-Abschaltung und Register-Rückleseprüfung;
der Restart-Budgetwert null erzwingt bis zu einem implementierten,
qualifizierten Reinitialisierungspfad den Safe State.
`storage-write` überwacht nun als zweite reale Domäne jede physische ATA-/FDD-
Schreibtransaktion mit einer 10-s-Deadline und explizitem Idle. Timeout oder
fehlgeschlagene Ruhestellung sperren weitere Writes ohne Restartversuch. ATA
liest `BSY/DRQ`, FDD Motorbits und Controller-Busy zurück; ein fehlgeschlagener
ATA-Flush wird als Schreibfehler weitergereicht. Das Storage-Fence allein kann
einen Teilwrite nicht zurückrollen; für markierte native FAT32-Images übernimmt
dies inzwischen das nachfolgende Undo-Journal v2 mit Flush-Barrieren und
Boot-Recovery. Größere Transaktionen/COW und die Power-Cut-Matrix auf
Zielhardware bleiben S0.5.
Als dritte Domäne überwacht `filesystem-write` alle mutierenden öffentlichen
VFS-Aufrufe. I/O-Fehler oder Deadlineverletzung verriegeln das VFS Read-only,
während Lesen und Diagnose weiter möglich bleiben. Fatal-Fencing sperrt VFS-
und physische Storage-Writes gemeinsam. Markierte FAT32-Images koppeln diese
Schranke an die Journaltransaktion; FAT12, EXT2 und fremde Medien besitzen noch
keine entsprechende atomare Mehrsektorgarantie.
Fortschrittssequenz und Interlockzustand beider Persistenzdomänen sind als
SECDED-/CRC-geschützte Primary/Shadow-Objekte ausgeführt. Korrigierbare Fehler
werden repariert; unbrauchbare Kopien führen fail-closed zur Schreibsperre.
Das native FAT32-Image enthält nun außerdem ein explizit markiertes
Einzelsektor-Undo-Journal in reservierten BPB-Sektoren. Die Reihenfolge
`old-data flush -> ACTIVE flush -> target flush -> CLEAN flush` ermöglicht
Boot-Recovery nach Abbruch an jeder Barriere. Journal v2 fasst bis zu 20
unterschiedliche Sektoren zu einer vollständigen VFS-Mutation zusammen und
rollt sie beim Boot in umgekehrter Reihenfolge zurück; wiederholte Zielsektoren
werden dedupliziert. CRC und Volumegrenzen werden vor Rollback geprüft;
fremde Medien ohne Marker werden nicht verändert. Kapazitätsüberschreitung
führt fail-closed zu Read-only. Offen bleiben größere Transaktionen/COW und
eine Power-Cut-Matrix auf Zielhardware.
Journal-v2-Metadaten besitzen nun zwei CRC-geschützte Superblöcke in den
reservierten Sektoren 8 und 31. Sequenzwahl, konservatives `ACTIVE` bei einem
unterbrochenen Mirror-Update und automatische Einzelkopie-Reparatur beseitigen
den bisherigen Header-Single-Point-of-Failure. Der persistente QEMU-Test
zerstört absichtlich die Primärkopie und verlangt Rollback plus Mirror-Reparatur.
Die Apply-/Verify-Callbacks samt Kontext sind ebenfalls redundant über
`critical_object` geschützt. Single-Bit-Fehler werden korrigiert; bei zwei
unbrauchbaren Kopien wird kein Funktionszeiger ausgeführt und unmittelbar zum
Safe State eskaliert. Noch ungeschützt sind Slot-Belegung und Domänenname; die
Belegung, Generation und Name liegen inzwischen ebenfalls in einem
versionierten Primary/Shadow-Descriptor. Dieser wird bei Registrierung zuletzt
publiziert; unkorrektierbare Scanfehler erzeugen fail-closed ein Safe-State-
Ereignis. Offen bleibt die unabhängige externe Kopie der gesamten
Supervisor-Konfiguration über eine zweite Fehlerdomäne.

- S0.3a um IPC-Deadlines, Metadatenintegrität, explizite Delegation,
   Service-Taskreservierung und Capability-Gates ergänzen.
- S0.3b als überwachte, neu startbare Least-Privilege-Probedomäne abnehmen;
   danach Netzwerk, Storage und komplexe Treiber schrittweise migrieren.
- Fortschritts-/Deadline-Watchdogs, Restart-Budgets, Fencing, Selbsttest und
   sichere Reintegration für jede migrierte Domäne nachweisen.
- Hot-Standby oder Dual-Controller-Handover mit regelmäßigem realem
   Failover-Test aufbauen.
- Common-Cause-Fehler bewerten; wo nötig unabhängige Hardware,
   Stromversorgung, Takte, Sensorpfade oder diverse Implementierungen nutzen.

#### S0.4 Determinismus und garantierte Ressourcen — L

- S0.4a ersetzt die ungewichtete Taskauswahl durch drei statische Klassen. Der
   feste Zyklus `Safety, Safety, Service, Ambient` bildet die Gewichte 2:1:1
   direkt und in konstanter Klassenlänge ab. Jede Klasse besitzt einen eigenen
   Round-Robin-Cursor; blockierte Tasks werden in höchstens `MAX_TASKS`
   Schritten übersprungen. Damit kann ein Klassen- oder Slotwechsel keine
   laufbereite niedrigere Klasse verhungern lassen. Der Pfad allokiert nicht.
- S0.4b begrenzt jede Klasse zusätzlich in absoluten 100-ms-Fenstern auf
   60 ms Safety, 25 ms Service und 15 ms Ambient. Die monotone Abrechnung
   erkennt auch übersprungene Fenster nach langen nicht präemptierbaren
   Abschnitten ohne unbeschränkte Schleife. Eine erschöpfte Klasse wird bis zur
   nächsten absoluten Fenstergrenze aus der Taskauswahl entfernt; der
   Kernelkontext bleibt für Diagnose und Recovery ausführbar. Rückläufige Zeit
   sperrt alle Klassen fail-closed. Zähler und aktueller Drosselzustand sind in
   der Taskdiagnose sichtbar. Host-Verhaltenstest, vollständiger i386-Build und
   realer Scheduler-Gastlauf sind grün.
- S0.4c-1 hebt bei einem blockierenden IPC-Send/Receive den Gegenprozess auf
   die effektive Klasse des Wartenden. Die Beziehung bindet Taskslot und
   Prozessgeneration, propagiert in höchstens `MAX_TASKS` Durchläufen transitiv
   und wird bei Wakeup, Timeout, Cancel oder Exit entfernt. Mehrere IPC-Peers
   oder beschädigte Capability-Metadaten werden vor dem Blockieren fail-closed
   abgewiesen. Spinlocks und Präemptions-Guards erhalten bewusst keine
   Inheritance, weil ihr Kontextvertrag Blockieren verbietet.
- S0.4c-2a bindet die statischen Task-, Stack-, IPC- und Storage-Grenzen im
   versionierten Register `safety/resource_budgets.toml` an ihre konkreten
   C-Makros und Verifikationstests. Der begrenzte Validator wertet nur sichere
   ganzzahlige Konstantenausdrücke aus und lehnt Drift, doppelte Einträge,
   Pfadflucht und fehlende Evidenz fail-closed ab. Die Compiler-Gates gegen VLA
   und Kernel-Stackframes über 4096 Byte bleiben verbindlich. Laufzeit-High-
   Water-Marken und zielhardwarebezogene WCET-Nachweise folgen in 2b/2c.
- S0.4c-2b1 ergänzt versionierte, lockgeschützte und saturierende
   Laufzeitdiagnostik für aktive beziehungsweise maximale IPC-Endpunkte,
   Capabilities, Nachrichten und Storage-Requests. Die C-Verhaltenstests
   erzwingen die jeweilige statische Kapazität, prüfen den fail-closed
   Fehlercode und belegen danach aktive Zähler von null bei erhaltenem
   High-Water-Wert. Die Zähler sind Diagnose, keine Autoritätsentscheidung.
- S0.4c-2b2a erweitert Syscall 43 append-only um Memory-Statistik v2. Der
   unveränderte 88-Byte-v1-Präfix bleibt verhandelbar; v2 ergänzt historische
   Frame-/Heap-Peaks und saturierende Allokationsfehlerzähler. Ein realer
   Ring-3-Test belegt Peak-Monotonie und Frame-Rückgewinnung. Der dabei
   entdeckte inkrementelle ABI-Mischbuild ist ebenfalls geschlossen: jeder
   C-Compile erzeugt jetzt explizit eine `.d`-Datei, und der Kernel-Link bricht
   bei fehlender oder falscher Dependency-Evidenz ab.
- S0.4c-2b2b1 ergänzt append-only Syscall 84. Seine feste 32-Byte-v1-Struktur
   meldet aktuelle und maximale Taskslot-Belegung, Kapazität, reservierten
   Supervisor-Slot und saturierende Ablehnungen. Der Gast füllt die Ambient-
   Kapazität bis zur definierten Ablehnung und weist danach Rückgewinnung bei
   erhaltenem High-Water nach.
- S0.4c-2b2b2 ergänzt ausschließlich für getrennte Testimages begrenzte Heap-
   und Frame-Countdowns. Der Boot-Selbsttest erzwingt Heap-ENOMEM ohne
   Belegungsänderung sowie einen Framefehler nach der ersten Kernelstackseite,
   prüft die exakte Framebilanz und verwendet den Stackslot anschließend
   erneut. Das QEMU-Image erreicht danach weiterhin den vollständigen
   Ring-3-`TEST_OK`-Marker.
- S0.4c-2c1 erzeugt mit einem unabhängigen GCC-Analysecompile für alle 75
   Kernel-C-Objekte `.su`- und `.ci`-Evidenz. Der Validator lehnt fehlende oder
   ungepaarte Dateien, dynamische beziehungsweise über 4096 Byte große lokale
   Frames und direkte oder transitive Rekursionszyklen ab. Der erste Lauf
   erfasste 1.204 Stackdatensätze und 5.767 Callgraph-Kanten; dabei wurde der
   rekursive PCI-Topologiescan durch feste Bus-/Slot-/Funktionsschleifen
   ersetzt.
- S0.4c-2c2a summiert nun die statischen Stackkosten entlang der direkten
   Legacy- und Scheduler-IRQ-Callgraphen. Das maschinenlesbare Register
   `safety/stack_budgets.json` bindet alle acht registrierten IRQ-Handler, die
   drei CPU-Exception-Handler und die im Exitpfad erreichbaren VFS-Callbacks
   ein; neue oder entfernte Registrierungen, unbekannte indirekte Aufrufe,
   fehlende Kosten und Budgetüberschreitungen stoppen das Gate. Der
   Referenzcompile belegt 1.744 von 7.168 Byte für den Legacy-IRQ-Pfad, 720 von
   4.096 Byte für den Scheduler-IRQ-Pfad und 2.000 von 7.168 Byte für CPU-
   Exceptions. Konservative Reserven für Assembly- und Validator-/Fence-
   Callbacks sind explizit im Register begründet. Syscall-Gesamtpfade und
   Zielplattform-WCET bleiben S0.4c-2c2b.
- Kritische Tasks erhalten feste Prioritäten, CPU-/Speicher-/Queue-Budgets,
   Admission Control und nachgewiesene Worst-Case-Laufzeiten.
- Im kritischen Modus nur reservierte Pools verwenden; unbeschränkte
   Allokation, Rekursion, Retries und Warteschlangen sind dort unzulässig.
- Priority Inversion, Interruptstürme und Zeitquellenausfall müssen noch einen
   getesteten degradierten Zustand auslösen.
- Kritische Kernelobjekte selektiv über den `critical_object`-Umschlag mit
   wortweisem SECDED, CRC32, Version/Sequenz, semantischem Validator und
   Primary/Shadow schützen; Bitflip-Injection misst Korrektur und Eskalation.

#### S0.5 Datenintegrität, Boot und unterbrechungsarme Updates — XL

- Sicherheitsrelevanten Zustand transaktional, checksummiert, versioniert und
   redundant speichern; Stromausfall an jeder Commitstelle injizieren.
- Verifizierten Boot, signierte Artefakte, reproduzierbare Builds, Provenienz
   und SBOM einführen.
- Updates als atomaren A/B-Wechsel mit Selbsttest und automatischem Rollback
   ausführen; Standby-Kanäle nacheinander statt gleichzeitig aktualisieren.
- Kernel-Livepatching bleibt eine eng begrenzte Ausnahme mit Quieszenzpunkt,
   Zustandskompatibilität, Vorabnachweis und sicherem Rollback.

#### S0.6 Verifikation und Langzeitbetrieb — XL

- Statische Stack-/Code-/WCET-Analyse, Fuzzing, modellbasierte Tests und
   unabhängige Reviews als Gates einführen.
- Fault-Injection für Bitfehler, Speichererschöpfung, Timingfehler,
   Geräteverlust, beschädigte Eingaben, Stromausfall und Updates automatisieren.
- Soak-/Alterungstests, ECC/EDAC, Medien-Scrubbing und Hardwaretausch über die
   geplante Produktlebensdauer nachweisen.
- Toolchain, Schlüssel, Abhängigkeiten, Feldtelemetrie, Schwachstellen,
   Beschwerden, Patches und Rückrufe kontrolliert über den Lebenszyklus führen.

### Phase 2 — Deskriptoren, VFS und zuverlässige Datenträger

#### R2.1 ABI v1 und vollständige Dateideskriptoren — L

- Syscallnummern, Strukturen und Fehlercodes aus einem gemeinsamen ABI-Header
   für Kernel und SDK generieren bzw. teilen.
- Open-Flags, Rechte je Handle und Standarddeskriptoren 0/1/2 ergänzen.
- `lseek`, `fstat` und `truncate` implementieren; die bestehenden Rename- und
   `fsync`-Syscalls in den gemeinsamen ABI-Header überführen.
- Teilzugriffe, EOF, ungültige Handles und Prozess-Exit vollständig testen.

#### R2.2 VFS- und FAT-Zuverlässigkeit — L

- **Teilstatus:** Atomisches Same-Directory-Rename/Replace ist für FAT32
   umgesetzt; Cross-Directory, FAT12 und offene Handle-Semantik fehlen.
- **Teilstatus:** Der Editor nutzt `TEMP -> fsync -> close -> rename`; FAT12
   und künftige Blockgeräte benötigen noch einen gleichwertigen Sync-Vertrag.
- Open/Delete/Unmount-Regeln und Locking vereinheitlichen.
- Fehler nach jedem einzelnen Sektorwrite injizieren und das resultierende
   Image mit einem Hostprüfer untersuchen.
- Danach Zeitstempel und LFN ergänzen; EXT2 vorerst ausdrücklich read-only
   mounten.

#### R2.3 Blockgeräte und Partitionen — L

- ATA und FDD hinter eine gemeinsame Blockgeräte-API legen.
- MBR-Partitionen als Child-Geräte erzeugen und mehrere Partitionen mounten.
- Bereichsprüfung und `flush` zentral erzwingen.
- ATA LBA48 und gebündelte PIO-Transfers ergänzen.
- GPT, AHCI und NVMe als getrennte Folgepakete behandeln.

### Phase 3 — Unix-artige CLI-Grundfunktionen

#### R3.1 Pipes, Signale und TTY — XL

- Pipeobjekt mit blockierendem Ringpuffer auf Wait-Queues bauen.
- `dup`/`dup2` und Deskriptorvererbung beim Spawn ergänzen.
- Minimale Signale `SIGINT`, `SIGTERM`, `SIGKILL`, `SIGCHLD` implementieren.
- Prozessgruppen, Vordergrundgruppe und TTY-Modi hinzufügen.
- `waitpid` einschließlich `WNOHANG` bereitstellen.

#### R3.2 Userspace-Shell und Init — L

- Parser für Quotes und Escapes erstellen.
- `<`, `>`, `>>` und `|` auf Deskriptoren/Pipes abbilden.
- Hintergrundjobs, Verlauf, Umgebungsvariablen und Exitcodes ergänzen.
- Ein kleines `INIT.PRG` als Reaper und Starter der Shell einführen.

### Phase 4 — Netzwerk bis zu Anwendungen

#### R4.1 IPv4/UDP härten und Sockets einführen — L

- ARP-Erneuerung, DHCP-Renew/Rebind und ICMP-Fehler ergänzen.
- Paketparser mit aufgezeichneten Frames, Grenzfällen und Fuzzing testen.
- Socketobjekte in die FD-Schicht integrieren: `socket`, `bind`, `sendto`,
   `recvfrom`, `close` und Timeouts.
- UDP-Echo zwischen Gast und Host als automatisierten Test betreiben.

#### R4.2 DNS — M

- DNS-Namen sicher kodieren/dekodieren, Kompressionszeiger begrenzen.
- A-Records, CNAME-Ketten, Timeouts und Caching implementieren.
- Lokalen deterministischen Testserver statt öffentliches Internet verwenden.

#### R4.3 TCP — XL

- Zustandsautomat und Connection Control Block erstellen.
- Sequenz-/ACK-Prüfung, Retransmission, RTO und Empfangsfenster ergänzen.
- Verbindungsaufbau, geordnete Daten, Reset und aktiven/passiven Close testen.
- Erst danach einen kleinen HTTP-Client oder `NETCAT.PRG` bauen.

### Phase 5 — USB und Plattform

#### R5.1 ACPI- und DMA-Basis — L

- RSDP/RSDT/XSDT mit Checksummen und Längengrenzen zentral parsen.
- MADT und HPET aus dieser Schicht beziehen; Poweroff/Reboot ergänzen.
- DMA-Puffer mit physischer Adresse, Alignment und Below-4-GiB-Grenze
   bereitstellen.

#### R5.2 xHCI in überprüfbaren Stufen — XL

- Controller stoppen/resetten und Capability-/Operational-Register validieren.
- DCBAA, Command Ring, Event Ring und Interrupter initialisieren.
- Root-Port-Anschluss erkennen, Slot aktivieren und Control Transfers testen.
- Deskriptoren lesen, Adresse und Konfiguration setzen.
- Erst HID-Boot-Tastatur, dann Hub und Mass Storage implementieren.

Jede Stufe benötigt einen QEMU-xHCI-Test und darf bei unbekannter Hardware das
Gerät nur deaktivieren, nicht den Bootvorgang blockieren.

### Phase 6 — optionale Modernisierung

Erst nach den vorherigen Meilensteinen einzeln entscheiden:

- UEFI-Boot und GPT
- x86-64-Port mit neuem ABI
- SMP, IOAPIC/MSI und per-CPU-Daten
- AHCI/NVMe, USB-Massenspeicher und Hotplug
- IPv6
- Mehrbenutzer-Identitäten, Dateirechte/ACLs und kryptografisch verifizierter
  Boot; dies ist getrennt von der bereits begonnenen Kernel-Capability-Basis
- dynamischer Linker, Shared Libraries, Paketverwaltung
- vollständiges Grafik-/Fenstersystem mit Compositor und Maus sowie Audio und
  WLAN

Diese Punkte sind groß genug für eigene Entwurfsdokumente und sollten nicht
nebenbei in die 32-Bit-Basis eingebaut werden.

## 8. Empfohlene Arbeitsreihenfolge

- [x] **1 · R0.1 Wait/Wakeup** — Größe S; keine Abhängigkeit
- [x] **2 · R0.2 Exception-Frames** — Größe S; keine Abhängigkeit
- [x] **3 · R0.3 PRG-v1-Vertrag** — Größe S; keine Abhängigkeit
- [x] **4 · R0.4 Gast-Smoke-Test** — Größe M; abhängig von R0.1–R0.3
- [x] **5 · R1.1 Wait-Queues/Sleep/Zeit** — Größe L; abhängig von R0.1
- [x] **6 · R1.2 Speicherverwaltung** — Größe L; abhängig von R0.4
- [x] **7 · R1.3 Synchronisation/Diagnose** — Größe M; abhängig von R1.1
- [x] **8 · R1.4 Grafischer Desktop-MVP** — Größe M; abhängig von R0.4 und R1.1
- [ ] **9 · S0.1 Profil/Gefahren/Assurance Case (teilweise)** — Größe M;
  abhängig von R1.3
- [ ] **10 · S0.2 Stack/Exception/Panic-Containment (teilweise)** — Größe L;
  abhängig von S0.1
- [x] **11 · S0.3a Bounded IPC/Capabilities v1** — Größe L; abhängig von
  S0.1 und S0.2
- [x] **12 · S0.3b Supervised Userspace Probe Domain** — Größe L; abhängig
  von S0.3a
- [ ] **13 · S0.3c Dienstmigration/Redundanz (in Arbeit)** — Größe XL;
  abhängig von S0.3b
- [ ] **14 · S0.4 Determinismus/Ressourcengarantie** — Größe L; abhängig von
  S0.1 und S0.3
- [ ] **15 · S0.5 Integrität/Boot/A-B-Updates** — Größe XL; abhängig von S0.1
  und S0.3
- [ ] **16 · S0.6 Verifikation/Langzeitbetrieb** — Größe XL; abhängig von
  S0.1–S0.5
- [ ] **17 · R2.1 ABI und FDs** — Größe L; abhängig vom abgenommenen S0-Gate
- [ ] **18 · R2.2 VFS/FAT-Zuverlässigkeit** — Größe L; abhängig von R2.1 und
  S0.5
- [ ] **19 · R2.3 Blockgeräte/Partitionen** — Größe L; abhängig von R1.3 und
  S0.5
- [ ] **20+ · R3 bis R6** — Größe L–XL; abhängig vom abgenommenen S0-Gate und
  der jeweiligen Basis

R2 bis R6 bleiben hinter dem S0-Gate. Erst danach können voneinander
unabhängige Pakete parallel laufen, sofern ihre Ressourcen-, Fehler- und
Nachweisgrenzen getrennt sind.

## 9. Definition of Done für jedes Paket

Ein Paket gilt nur dann als fertig, wenn alle folgenden Punkte erfüllt sind:

- [ ] öffentliche API, Fehlerfälle und Nebenläufigkeitsregeln sind dokumentiert
- [ ] positive, negative und mindestens ein Ressourcenfehler-Test existieren
- [ ] Hosttests laufen erfolgreich
- [ ] Windows-Referenzbuild läuft erfolgreich
- [ ] betroffene Laufzeitfunktion wird im automatisierten Gast geprüft
- [ ] kein Test beweist eine Laufzeiteigenschaft ausschließlich durch Quelltextsuche
- [ ] relevante QEMU-, VMware- oder Hardwarematrix ist dokumentiert
- [ ] alte Stubs, widersprüchliche Dokumentation und tote Pfade sind entfernt oder
  ausdrücklich als nicht unterstützt markiert
- [ ] betroffene Gefahren, Essential Functions, FTTI und sicherer/degradierter
  Zustand sind identifiziert; das Restrisiko ist begründet
- [ ] Anforderungen, Design, Code, Testfall und Ergebnis sind bidirektional
  rückverfolgbar und unabhängig geprüft
- [ ] Laufzeit-, Stack-, Speicher-, Queue- und I/O-Grenzen werden unter Überlast
  sowie durch Fault-Injection geprüft
- [ ] jeder Dienst besitzt Health-Monitoring, einen begrenzten Fehlerpfad und einen
  getesteten Restart-, Failover- oder Safe-State-Mechanismus
- [ ] Releaseartefakte sind signiert, reproduzierbar und ihrer SBOM sowie exakten
  Toolchain zuordenbar

Aktuelle Referenzbefehle:

```powershell
make test
.\scripts\build-windows.ps1 -Target qemu -RunTests
.\scripts\build-windows.ps1 -Target qemu -Video framebuffer -RunTests
```

Zusätzliche und geplante Ziele:

```text
make test-smoke
make test-smoke-pit
make test-smoke-memory
make test-generated-images
make test-fuzz
```

## 10. Unmittelbar nächster Schritt

Phase 0, R1.1 bis R1.4, **S0.3a Bounded IPC/Capabilities v1** und
**S0.3b Supervised Userspace Probe Domain** sind umgesetzt und abgenommen.
Die dafür geschlossenen IPC-/Isolationsinkremente sind:

- [x] endliche Send-/Receive-Deadlines mit eindeutigem Timeoutstatus — umgesetzt,
- [x] CRC- und `critical_object`-Schutz für Queue-, Endpoint- und
   Capability-Metadaten einschließlich deterministischer Bitflip-Injection —
   umgesetzt; unkorrektierbare Objekte quarantänisieren den Endpoint und
   wecken beide begrenzten Warteschlangen mit eigenem Integritätsstatus,
- [x] explizite selektive Delegation mit ausschließlich abschwächbaren Rechten —
   umgesetzt; Ziel-PID und Prozessgeneration werden atomar gebunden, Spawn
   vererbt keine IPC-Autorität mehr,
- [x] mindestens ein reservierter Service-/Restart-Taskslot mit Admission Control
   — umgesetzt; normale Spawns können weder den letzten Task-/Prozessslot noch
   das 32-Frame-Restartbudget verbrauchen,
- [x] Capability-/Domänen-Gates für `kill` und alle ambienten Datei-, Display-,
   Prozess- und sonstigen Syscalls der Probedomäne — umgesetzt; Autorisierung
   erfolgt zentral vor Seiteneffekten, das Probeprofil ist default-deny.

- [x] überwachte Ring-3-Probe mit begrenztem Ablauf `fence -> revoke -> reap ->
   recreate -> self-test -> reintegrate` — umgesetzt und in realem QEMU mit
   unabhängiger Prozess-/Zeitfortschrittsmessung abgenommen.

Der nächste Schritt ist **S0.3c Dienstmigration/Redundanz**: zuerst einen
unkritischen echten Dienst hinter die bestehende Capability-/Supervisorgrenze
verschieben, danach Netzwerk und Storage schrittweise aus Ring 0 lösen. Jede
Migration benötigt Fault-Injection, Ressourcenbudgets und einen nachweisbaren
degradierten Betrieb ohne Rückfall auf ambienten Kernelzugriff.

**S0.3c-1 ist umgesetzt:** Die gesunde Ersatzdomäne stellt einen begrenzten
Diagnosedienst bereit. Ein neuer append-only Service-Connect-Syscall 57 prüft
den Userpuffer vor Delegation, validiert die aktuelle Dienst-PID und
-Generation und vergibt ausschließlich `SEND|RECEIVE`. `CONTROL` verbleibt
beim Dienst. Der reale Gast sendet `DIAG`, erhält `REIST_DIAG_OK` innerhalb
endlicher IPC-Deadlines und läuft danach bis `TEST_OK`.

**S0.3c-2 ist umgesetzt:** Der append-only Syscall 58 gibt eine delegierte
Client-Capability frei, ohne den Endpoint des Dienstbesitzers zu zerstören.
Freigabe und Exit-Cleanup entfernen den generation-gebundenen Datensatz atomar
und wecken blockierte Peers. Der reale Gast prüft Freigabe, Ablehnung des stale
Handles, erneute Verbindung und einen zweiten Diagnose-Request/Reply ohne
Verbrauch zusätzlicher Capability-Slots.

**S0.3c-3a ist umgesetzt:** Der Ring-3-Dienst klassifiziert einen begrenzten
Ethernet-v1-Header als ARP, IPv4 oder sonstigen EtherType. Mindestlänge,
Nachrichtengröße und Antwort sind fest begrenzt; der Pfad allokiert nicht und
besitzt keine Hardware- oder Ausgabeautorität. GTEST überträgt einen
synthetischen ARP-Frame und der QEMU-Runner verlangt `NETWORK_PARSER_OK`.

**S0.3c-3b ist umgesetzt:** Der echte `netdev`-RX-Pfad spiegelt genau den
14-Byte-Ethernet-Header als feste `NET1`-Nachricht an den gesunden Dienst. Der
Ingress ist nichtblockierend, heapfrei und verwirft bei Queue-Druck oder ohne
aktiven Client. IPC bindet den Absender an den einzigen generation-geprüften
Peer, sodass der Client die eigene Ingress-Nachricht nicht konsumieren kann.

**S0.3c-3c ist umgesetzt:** Der gesunde Dienst kann über den ausschließlich im
Default-Deny-Profil erlaubten Syscall 59 einen festen Gateway-ARP-Probe
anfordern. Die Supervisorgrenze prüft PID plus Generation und begrenzt Aufrufe
auf einen pro 250 ms. Nur ein realer `NETR`-RX-Header erzeugt
`NETWORK_HANDOFF_OK`; ohne NIC antwortet der Dienst definiert mit
`REIST_NET_UNAVAILABLE`. Der QEMU-Runner besitzt dafür ein separates striktes
Abnahmeflag. Der 10-ms-Supervisor-Worker ruft den begrenzten `netdev_poll()`
als garantierten Bottom-Half auf; RX-Fortschritt hängt nicht mehr von einem
opportunistischen Shell-/Netzwerkkommando ab.

**S0.3c-3d ist umgesetzt:** Nur während einer ausstehenden, rate-limitierten
Probe kann ein ARP-Header übernommen werden. Nach erfolgreicher IPC-Publikation
wird dieses Frame nicht zusätzlich in die Kernel-Netstack-Queue gestellt. Bei
fehlendem Dienst, falschem EtherType oder Queue-Druck bleibt der Kernelpfad
zuständig; Fence/Restart löscht die Pending-Autorität. Damit existiert für das
übernommene Frame kein paralleler autoritativer Klassifikationspfad mehr.

**S0.3c-3e ist umgesetzt:** Nach einem echten NIC-Handoff fordert GTEST einen
zweiten ARP-Probe an; der Dienst führt unmittelbar danach absichtlich `UD2`
aus. Der Fence löscht Pending-Autorität, Exit-Cleanup widerruft den alten
Endpoint und der Client erwartet einen Kanalfehler. Innerhalb von höchstens
100 × 20 ms verbindet er sich mit der Ersatzgeneration, wiederholt den
Diagnose-Request und erreicht `NETWORK_RECOVERY_OK` sowie `TEST_OK`.
Die Basis-Recovery gilt im Runner über `RECOVERY_SEQUENCE_OK` als abgeschlossen;
dieser kumulative Marker entsteht ausschließlich nach der vierten erfolgreich
selbstgetesteten Generation und ersetzt flüchtige Zwischenzeilen als Gate.

**S0.3c-3f ist umgesetzt:** Der Diagnoseclient füllt nach einem begrenzten
`NETPRESSURE`-Handshake alle vier statischen IPC-Slots, während der Dienst
kurz schläft und anschließend eine echte Gateway-ARP-Probe auslöst. Der
nichtblockierende Ingress meldet Queue-Druck, verbraucht die einmalige
Probe-Autorität und überlässt das Frame dem bestehenden Kernelpfad. Erst nach
vier korrekt beantworteten Lastnachrichten wird `NETWORK_PRESSURE_OK`
ausgegeben; der strikte RTL8139-Smoke verlangt die geordnete Fallback- und
Fortschrittskette.

**S0.3c-3g ist umgesetzt:** Das versionierte Dienstprotokoll bindet jede
Anfrage und Antwort an eine von Null verschiedene 32-Bit-ID. Zusammen mit der
Endpointgeneration bildet sie die Korrelationsidentität; ein Restart zerstört
die alte Queue, und der Client akzeptiert auf der neuen Generation nur seine
aktuelle ID. Der Gast lässt den Dienst absichtlich `request_id + 1` senden,
verwirft diese Antwort und beweist anschließend mit einer korrekt korrelierten
Diagnoseanfrage weiteren Fortschritt über `SERVICE_CORRELATION_OK`.

**S0.3c-3h ist umgesetzt:** Der exklusive Probe-Handoff transportiert jetzt
den vollständigen festen 42-Byte-Ethernet/ARP-Header. Die restartbare
Ring-3-Domäne validiert Hardwaretyp Ethernet, Protokolltyp IPv4,
Adresslängen 6/4 und den begrenzten Request/Reply-Opcode ohne Heap oder
variable Schleifen. Ein absichtlich falscher Hardware-Adresslängenwert erzeugt
keine Antwort; erst danach wird ein gültiges ARP-Frame klassifiziert. Der Gast
und Runner verlangen `ARP_VALIDATION_OK`.

**S0.3c-3i ist umgesetzt:** Beim Start einer Gateway-Probe friert der
Supervisor Gateway-IP sowie lokale IP/MAC generationsgebunden ein und hängt
sie an den 42-Byte-Frame an. Ring 3 akzeptiert ausschließlich ARP-Reply,
dessen Sender-IP der Gateway-IP entspricht, dessen Ziel-IP/MAC lokal sind und
dessen Ethernet-Quell-/Zieladressen mit dem ARP-Inhalt übereinstimmen. Eine
synthetisch verfälschte Gateway-Identität bleibt unbeantwortet; die korrekte
Identität erzeugt `ARP_IDENTITY_OK`, bevor der echte NIC-Handoff folgt.

**S0.3c-3j ist umgesetzt:** Syscall 60 ergänzt append-only eine Probe-v2-API,
die nach vollständiger Pointerprüfung eine von Null verschiedene monotone
32-Bit-Probe-ID liefert. Der 64-Byte-Ingress trägt diese ID; Ring 3 akzeptiert
das Frame nur für seine aktuell ausstehende ID. Der Supervisor bestätigt
genau einen passenden Dienstbericht mit `PROBE_ID_OK`, verwirft Replay und
fenced die ID bei Recovery. Nach Erschöpfung wird mit `-EOVERFLOW` gestoppt,
nicht auf Null zurückgesprungen; Syscall 59 bleibt kompatibel erhalten.

**S0.3c-3k ist umgesetzt:** Jede Probe-ID besitzt eine saturierend berechnete
absolute 250-ms-Deadline. Die heapfreie Autoritätszustandsmaschine erlaubt nur
eine aktive ID, konsumiert sie genau einmal und verwirft sie bei `now >=
deadline`; der 10-ms-Supervisor-Worker räumt auch ohne eingehendes Frame auf.
Der Hosttest deckt Frühzugriff, exakten Ablauf, Einmalverbrauch,
`UINT64_MAX`-Sättigung und endgültige 32-Bit-ID-Erschöpfung ab. Dadurch kann
eine späte semantisch gleiche ARP-Antwort keine alte Autorität verwenden.

**S0.3c-3l ist umgesetzt:** Saturierende 32-Bit-Zähler unterscheiden
abgelaufene Autoritäten, Queue-Fallback und semantisch abgelehnte Ingress-
Frames. Ablauf wird im 10-ms-Worker, Queue-Druck im Foreground-Handoff und
semantische Ablehnung durch den generation-validierten Dienstbericht erfasst;
kein Zählerpfad läuft im IRQ. Der Hosttest beweist getrennte Inkremente und
Sättigung bei `UINT32_MAX`, sodass Diagnosewerte nie still zurückspringen.

**S0.3c-3m ist umgesetzt:** Syscall 61 liefert eine feste 24-Byte-v1-Struktur
mit Version, Strukturgröße und den drei saturierenden Zählern. Pointer,
Schreibbereich, Version und Mindestgröße werden vor dem Snapshot geprüft; die
ABI bietet absichtlich keine Reset- oder Mutationsoperation. GTEST prüft
EFAULT, Header/Reserved-Feld, monotone Werte und beim echten Vier-Slot-Druck
einen gestiegenen Queue-Fallback-Zähler über `NETWORK_STATS_OK`.

**S0.3c-3n ist umgesetzt:** Der Diagnose-Snapshot liegt als versioniertes,
redundantes Critical Object vor. Lesen repariert eine beschädigte Kopie aus der
gültigen Replica. Bei Doppelkorruption liefert Syscall 61 `-84`, bevor Daten in
den Userbereich kopiert werden. Deterministische Hostinjektion beweist beide
Pfade und den Erhalt eines zuvor gezählten Ereignisses nach Einzelkorrektur.

**S0.3c-3o ist umgesetzt:** Probe-ID, absolute Deadline und monotone ID-Sequenz
liegen in einem versionierten Critical Object. Begin, Take, Expire und Cancel
lesen und publizieren ausschließlich validierte Snapshots. Ein beschädigter
CRC wird aus der zweiten Kopie rekonstruiert; Doppelkorruption liefert `-84`,
erteilt keine Autorität und isoliert eine aktive Probe-Domäne im Worker.

**S0.3c-3p ist umgesetzt:** Zugestellte Probe-ID, Gateway, lokale IP und MAC
bilden einen einzigen versionierten Critical-Object-Snapshot. Prepare,
Snapshot, Publish, Consume und Clear sind atomar unter der Supervisor-Sperre;
unvollständige Identitäten und falsche Bestätigungs-IDs werden abgelehnt. Eine
beschädigte Kopie wird rekonstruiert, Doppelkorruption erzeugt `-84` und führt
vor einem Handoff zur Isolation. Der Hosttest injiziert beide Fehlerklassen.

**S0.3c-3q ist umgesetzt:** PID/Generation, Endpoint, Supervisor-Handle,
Health/Fence, Launch-Zähler und Rate-Limit-Zeit bilden einen versionierten
Control-Snapshot. Registrierung, Restart, Self-Test, Delegation, Handoff und
Worker lesen und publizieren ausschließlich validierte Kopien. Einzelkorruption
wird rekonstruiert; Doppelkorruption sperrt alle Dienste und Probes, der Worker
fenced sämtliche Ausgänge. Hosttests beweisen Korrektur und fail-closed Read/
Write; direkte ungeschützte Probe-Control-Felder wurden entfernt.

**S0.3c-3r ist umgesetzt:** Die monotone Probe-ID dient zugleich als gemeinsame
Transaktionsepoche von Control, Autorität und Identitätskontext. Begin/Prepare/
Control-Publish sowie Snapshot/Take/Delivery-Publish laufen jeweils unter einer
kurzen gemeinsamen Supervisor-Sperre. Ablauf und Dienstbestätigung verlangen
dieselbe Epoche. Hosttests kombinieren absichtlich einzeln gültige Snapshots
verschiedener Epochen; Take, Publish und Consume lehnen sie ohne Mutation ab.

**S0.3c-4a ist umgesetzt:** Der append-only Syscall 62 übernimmt eine feste,
versionierte 24-Byte-ARP-Bindung ausschließlich von der generation-validierten
Probe-Domäne. Probe-ID, Gateway-IP und Sender-MAC müssen bytegenau zu Control-
Epoche und geschütztem Ingress-Kandidaten passen. Der Mediator verbraucht die
Autorität vor dem begrenzten 32-Slot-Cache-Update; Replay, falsche Epoche,
falsche IP/MAC, Broadcast/Null-MAC und fremde Prozesse scheitern vor der
Mutation. Der RTL8139-Gast bestätigt den realen Pfad mit `ARP_BINDING_OK`.

**S0.3c-4b ist umgesetzt:** Vermittelte ARP-Bindungen liegen in einem eigenen,
festen 32-Slot-Cache. Jeder Slot ist als versioniertes Critical Object mit
SECDED/CRC-geschützter Primär- und Schattenkopie gespeichert und bindet IP/MAC,
Quellepoche und eine saturierend berechnete monotone 30-s-Deadline zusammen.
Einzelkopiefehler werden rekonstruiert. Eine unlesbare Doppelkopie sperrt den
gesamten Lookup fail-closed, statt möglicherweise den beschädigten Slot zu
übersehen. Bei Ablauf wird die Bindung zu einem bleibenden Sperreintrag; der
Lookup fällt für diese IP nicht auf den unvalidierten Legacy-Cache zurück.
Auch Kapazitätserschöpfung verdrängt keine frühere Vertrauensentscheidung,
sondern lehnt die neue Mutation ab. Hosttests prüfen Ablaufgrenze, Sättigung,
Einzel-/Doppelkorruption und Poolgrenze; Paket-, normaler Gast- und echter
RTL8139-Smoke sind grün.

**S0.3c-4c ist umgesetzt:** Neben der Transaktionsepoche speichert jeder Slot
PID und Prozessgeneration des erzeugenden Dienstes. Der Fence wandelt vor dem
Prozessabbruch alle noch gültigen Einträge genau dieser vollständigen Identität
in bleibende Sperreinträge um. Der Supervisor-Worker scrubbt
hardwareunabhängig höchstens
einmal pro Sekunde alle 32 Slots; Ablauf wird publiziert, Einzelkorruption
gezählt und repariert, Doppelkorruption löst Isolation bzw. den globalen
Output-Fence aus. Die Cachebasis wird vor Hardwareerkennung initialisiert,
sodass auch ein No-NIC-System sicher recovern kann. Der echte RTL8139-Gast
erzwingt nach einer vermittelten Bindung einen Dienstcrash und akzeptiert
`NETWORK_RECOVERY_OK` erst nach `ARP_BINDINGS_REVOKED`. Hosttests prüfen zudem
falsche/richtige Generation sowie Scrub-Ablauf und Integritätsfehler.

S0.3c-5 verschiebt als nächsten Schritt die nächste echte ARP-/IPv4-
Verarbeitungsentscheidung vollständig in den isolierten Dienst und entfernt
den entsprechenden Ring-0-Parallelpfad. Fehler-, Queue-Druck- und
Restart-Injektion müssen weiterhin unabhängigen Gastfortschritt beweisen.

**S0.3c-5a ist umgesetzt:** Der Legacy-Cache darf die konfigurierte
Gateway-IP weder aus einem ARP-Paket noch implizit aus der Quell-MAC eines
IPv4-Pakets lernen. Eine zentrale, hostgetestete Policy blockiert diese
Mutation vor jedem Cache-Seiteneffekt. Wird eine Route manuell oder durch DHCP
publiziert, entfernt der Kernel außerdem eine möglicherweise zuvor gelernte
Legacy-Bindung. Gateway-Autorität kann damit ausschließlich über den
generation- und epochengebundenen Ring-3-Mediator in den geschützten Cache
gelangen. Nicht-Gateway-Peers verbleiben bis S0.3c-5b im klar bezeichneten
Kompatibilitätspfad.

**S0.3c-5b1 ist umgesetzt:** Ein an die lokale IP gerichteter ARP-Request wird
bei gesundem Dienst als festes 60-Byte-`NETQ`-Objekt exklusiv nach Ring 3
übergeben. Ring 3 validiert Ethernetziel, ARP-Struktur, Absenderidentität,
lokale Zielidentität und Request-ID, bevor Syscall 63 eine einzige Antwort
anfordert. Der Kernel gleicht PID, Prozessgeneration, 250-ms-Einmalautorität
und einen redundant geschützten Request-Kontext ab; erst nach atomarem
Verbrauch darf der NIC-Sendemechanismus laufen. Dienst-, Queue- oder
Sendefehler verwerfen den Request und zählen Degradation, statt den alten
Ring-0-Responder zu reaktivieren oder den Dienst zu beenden. Hosttests prüfen
ABI, Autorität, Einzelkorrektur und Doppelkorruption.

**S0.3c-5b2a ist umgesetzt:** Der QEMU-Runner verbindet User-Netzwerk,
Socket-Injektor und RTL8139 über einen virtuellen Hub. Nach der expliziten
Gastbereitschaft sendet er höchstens drei einzeln bestätigte, korrekt gerahmte
ARP-Requests und verlangt die geordnete Kette `ARP_REQUEST_QUEUED ->
ARP_REPLY_MEDIATED -> TEST_OK`. Der Lauf deckte zwei zuvor synthetisch
verdeckte Fehler auf: Request-ID und Dienstgeneration waren unzulässig
gleichgesetzt, und der Ring-3-Parser prüfte die Quell- statt der
Broadcast-Zieladresse. Hostvertrag, Paketbuild und der echte Runtime-Modus
`arp-reply` sind grün. **S0.3c-5b2b ist ebenfalls umgesetzt und abgenommen:**
Ein Cache-Miss veröffentlicht eine feste `NETA`-Nachricht an den überwachten
Ring-3-Dienst. Eine geschützte, generationgebundene 250-ms-Einmalautorität
bindet Request-ID und Zieladresse; erst Syscall 64 darf nach vollständigem
Abgleich den echten ARP-Request senden. Fehler aktivieren keinen alten
Ring-0-Fallback. Der RTL8139-QEMU-Lauf fordert die Auflösung über Syscall 65 an
und prüft `ARP_RESOLUTION_QUEUED`, `ARP_RESOLUTION_MEDIATED` sowie den am
QEMU-Socket ausgesendeten ARP-Frame für `10.0.2.99`. Damit ist S0.3c-5b
geschlossen.

**S0.3c-5c ist umgesetzt und abgenommen:** Gültige IPv4-/ICMP-Prüfsummen und
Paketgrenzen werden noch im Kernel geprüft; ein Echo-Request erzeugt danach
ausschließlich eine feste `NETI`-Nachricht für den gesunden Ring-3-Dienst.
Request-ID, Prozessgeneration, Quell-IP/-MAC, Identifier, Sequenz und höchstens
32 Payloadbytes liegen in einem redundanten Critical Object und einer auf
250 ms begrenzten Einmalautorität. Nur der neue append-only Syscall 72 darf
nach vollständigem Abgleich Kontext und Autorität atomar verbrauchen; erst
außerhalb der Supervisor-Sperre wird gesendet. Es existiert kein Ring-0-
Antwortfallback. Der Runtime-Modus `icmp-echo` injiziert einen echten Request
in RTL8139, verlangt `ICMP_ECHO_QUEUED -> ICMP_ECHO_MEDIATED -> TEST_OK` und
prüft Zieladressen, Identifier, Sequenz, Nutzdaten und Checksumme des wirklich
am QEMU-Socket beobachteten Reply. Als nächstes vermittelt S0.3c-5d die noch
im Kernel liegenden UDP-/DHCP-Entscheidungen schrittweise.

**S0.3c-5d1 ist umgesetzt und abgenommen:** Der begrenzte Kerneltransport
parst DHCP weiterhin und validiert die angebotenen IPv4-Werte, darf die aktive
Netzkonfiguration aber nicht mehr selbst publizieren. Stattdessen legt er
Request-ID, IP, Netzmaske, Gateway und DNS in einen redundant geschützten
Kontext und sendet ein festes 28-Byte-`NETD`-Objekt einschließlich der
Leasezeit direkt an den exakten
Endpoint-Besitzer. Dieser Kernel-zu-Owner-Ingress verwendet die reservierte
Absenderidentität `(0,0)` und benötigt weder einen erfundenen Prozess noch
eine vorher delegierte Client-Capability. Der Ring-3-Dienst prüft Maske,
Hostbereich und Gateway-Subnetz erneut; nur sein append-only Syscall 73 darf
die generationgebundene 1-s-Einmalautorität verbrauchen. Kontext und Autorität
werden vor der Netzmutation gelöscht. Der reale RTL8139-Modus `dhcp-config`
verlangt `DHCP_CONFIG_QUEUED -> DHCP_CONFIG_MEDIATED -> BOOT_OK`; Normalbetrieb,
ARP und ICMP bleiben zusätzlich grün. UDP-Transport sowie Renew/Rebind
verbleiben bewusst als S0.3c-5d2 im Kernel.

**S0.3c-5d2a ist umgesetzt und abgenommen:** Als erster echter UDP-
Dataplane-Schnitt akzeptiert Ring 0 ausschließlich Datagramme an Port 9000,
mit gültiger IPv4-UDP-Prüfsumme und höchstens 32 Nutzdatenbytes. Quell-IP,
Quell-MAC, beide Ports und Nutzdaten liegen in einem redundanten Critical
Object; eine generationgebundene 250-ms-Einmalautorität schützt die Antwort.
Der Ring-3-Dienst validiert das feste `NETU`-Objekt erneut und darf nur über
den append-only Syscall 74 antworten. Kontext und Autorität werden vor dem
einzigen NIC-Sendepunkt verbraucht; es gibt keinen Ring-0-Echo-Fallback. Der
Runtime-Modus `udp-echo` injiziert ein echtes Datagramm über RTL8139 und prüft
am QEMU-Socket Ports, IP-Adressen, Payload und Antwortprüfsumme. Allgemeine
Weitere Bindings, größere Nutzdaten, DHCP-Renew/Rebind und eine Socket-ABI
blieben zu diesem Stand S0.3c-5d2b2/R4.1; 5d2b2a schließt inzwischen den
statisch begrenzten Dienst-Binding-Vertrag.

**S0.3c-5d2b1 ist umgesetzt und abgenommen:** DHCP-Option 51 ist nun
verpflichtend und wird auf 60 Sekunden bis sieben Tage begrenzt. Vorschlag,
Ring-3-Prüfung und Commit tragen denselben Wert. Nach dem Commit speichert der
Supervisor Dienstgeneration, IP, Leasezeit und absolute monotone Deadline als
redundantes Critical Object. Ablauf, Integritätsfehler oder Fence entziehen
IP, Maske, Gateway und DNS einschließlich der alten Gateway-Bindung; eine
veraltete Generation kann die Autorität nicht behalten. Das dedizierte
Buildprofil verkürzt ausschließlich die Testdeadline auf 2500 ms. Der reale
RTL8139-Lauf verlangt `BOOT_OK -> DHCP_LEASE_EXPIRED` und weist danach noch
laufenden Kernel und Shell nach.

**S0.3c-5d2b2a ist umgesetzt und abgenommen:** Der überwachte Ring-3-Netzdienst
kann bis zu vier Ports ab 1024 binden. Ein 24-Bit-Generationsanteil im Handle,
die gebundene Dienstgeneration und eine pro Slot geschützte Einmalautorität
verhindern stale Antworten und Portübernahme. Jeder Slot besitzt einen
`critical_object`-geschützten Descriptor und Transaktionskontext; Payload,
Antwortfenster und Pool sind auf 32 Byte, 250 ms und vier Slots begrenzt.
Unbind, Deadline, Dienst-Fence und Neustart räumen Kontext und Autorität
idempotent auf, während die Generation monoton bleibt. Die append-only
Syscalls 75–77 validieren beide Userbereiche vor Publikation und rollen einen
Bind bei fehlgeschlagenem Copy-out zurück. Der echte RTL8139-Lauf bindet neben
Port 9000 auch Port 9001 und prüft dort Request, Ring-3-Revalidierung, Reply,
Ports, Payload und UDP-Prüfsumme. Das ist bewusst noch keine allgemeine
Anwendungs-Socket-ABI.

**S0.3c-5d2b2b ist umgesetzt und abgenommen:** Nach jedem erfolgreichen
Lease-Commit erhält der Ring-3-Netzdienst ein festes `NETL`-Objekt mit T1,
T2 und Ablaufdeadline. Ein heapfreier Zustandsautomat verwendet ausschließlich
absolute monotone Zeit, höchstens drei Versuche pro Renew-/Rebind-Phase und
keine Polling-Schleife. Der append-only Syscall 78 veröffentlicht genau einen
DHCPREQUEST; Prozessgeneration, erwartete IP, Operation, Transaktions-ID und
1,5-s-Deadline liegen bis ACK/NAK in geschützten Supervisorobjekten. Der
Kernel-Worker verarbeitet pro Durchlauf höchstens ein Reply und übergibt ein
gültiges ACK erneut an die bestehende Ring-3-Commit-Grenze. NAK, Deadline,
Fence oder Dienstneustart widerrufen die Transaktion fail-closed. Das
Testprofil verkürzt nur die effektive Lease auf fünf Sekunden; der reale
RTL8139-Lauf bestätigt `DHCP_RENEW_REQUESTED -> DHCP_RENEWED` bei weiter
laufender Shell. S0.3c-5e migriert als Nächstes den verbliebenen allgemeinen
IPv4-/UDP-/DHCP-Protokollzustand aus Ring 0.

**S0.3c-5e1 ist umgesetzt und abgenommen:** Eine vom Monitor- und Legacy-
Demux getrennte statische Acht-Slot-Queue spiegelt vollständige Ethernetframes
mit maximal 1518 Byte an den gesunden Netzdienst. Syscall 79 ist ausschließlich
im Default-Deny-Profil der aktuellen Dienstgeneration freigegeben, prüft den
gesamten User-Zielbereich vor dem Dequeue und liefert bei leerer Queue sofort
`EAGAIN`. Jeder Dienstneustart verwirft alte Queueinhalte. Ring 3 revalidiert
ABI, Länge, Padding und EtherType; erst ein erfolgreicher Copy-out erzeugt die
einmal konsumierbare Bestätigung für `FRAME_HANDOFF`. Der reale RTL8139-Lauf
weist diesen Weg bis Ring 3 nach. Der bestehende Ring-0-Demux bleibt in dieser
Schattenphase absichtlich aktiv. S0.3c-5e2 übernimmt darauf IPv4, UDP und DHCP
und entfernt erst nach äquivalenten Fault-/Drucktests den Parallelpfad.

**S0.3c-5e2a ist umgesetzt und abgenommen:** Der Netzdienst verarbeitet den
rohen Frame nun zusätzlich mit einem heapfreien IPv4-v1-Parser. Er begrenzt
Ethernetframe und IPv4-Header auf 1518 beziehungsweise 60 Byte, prüft Version,
IHL, Total Length, TTL und Headerprüfsumme und verwirft Fragmente sowie fremde
EtherTypes fail-closed. Das Ergebnisobjekt ist fest 28 Byte groß und wird vor
jeder Prüfung genullt. Ein generationsgebundener Liefernachweis erlaubt genau
den ersten ICMP- oder UDP-Parserreport; der RTL8139-Lauf bestätigt
`IPV4_PARSED_RING3`. Dies ist bewusst nur ein Shadow-Nachweis: Ausgabe und
Legacy-Demux bleiben bis S0.3c-5e2b im Kernel, und der Report erteilt keinerlei
Netzwerkautorität.

**S0.3c-5e2b1 ist umgesetzt und abgenommen:** Ein zweiter fester Ring-3-
Parser akzeptiert nur vom IPv4-v1-Parser validierte UDP-Datagramme. Er verlangt
ein nichtleeres Portpaar, eine UDP-Länge ab acht Byte, exakte Übereinstimmung
mit der IPv4-Nutzlast und eine nichtnull, über den Pseudoheader validierte
Prüfsumme. Ungerade Nutzdatenlängen sind abgedeckt; das 20-Byte-Ergebnis wird
bei jedem Fehler vollständig genullt. Der generationsgebundene UDP-
Liefernachweis ist weiterhin reine Diagnose. Ein realer RTL8139-Lauf bestätigt
`UDP_PARSED_RING3` sowie im selben Lauf einen vermittelten UDP-Echo-Request.
S0.3c-5e2b2a ist ebenfalls umgesetzt: Syscall 80 übernimmt ausschließlich
einen zum zuletzt ausgelieferten Frame passenden, CRC32-, generation- und
deadlinegebundenen Ring-3-Entscheid. Gültige Datagramme für aktive
Dienstbindings erzeugen eine geschützte Einmalautorität; ungültige oder
ungebundene Datagramme werden kanonisch verworfen. Der Kernel unterdrückt für
diese dienstbesessenen Ports die parallele Legacy-Zustellung. Der reale
RTL8139-Lauf bestätigt `UDP_INGRESS_RING3 -> UDP_ECHO_MEDIATED -> TEST_OK`.
S0.3c-5e2b2b muss als Nächstes DHCP-Eingang und verbleibenden UDP-Demux über
denselben validierten Ring-3-Pfad führen. Der erste Teil S0.3c-5e2b2b1 ist
abgenommen: Ein festes 52-Byte-Ergebnis entsteht ausschließlich aus einem
vollständig validierten BOOTP/DHCP-Reply mit Ports 67 nach 68, Ethernet/IPv4-
Grenzen, BOOTREPLY-Typ, Hardwaretyp/-länge, Transaktions-ID, Client-MAC,
Magic-Cookie und begrenzt durchlaufener Optionsliste. OFFER, ACK und NAK sind
die einzigen Nachrichtentypen; fehlendes END, abgeschnittene oder doppelte
kritische Optionen werden fail-closed verworfen. Eine vorhandene UDP-
Prüfsumme wird vollständig geprüft; der nur für DHCP/IPv4 zulässige Nullwert
„keine Prüfsumme“ bleibt explizit erkennbar. Ein CRC32- und generations-
korrelierter Diagnosebericht erzeugt keinerlei Konfigurationsautorität. Der
reale RTL8139-Lauf bestätigt `DHCP_CONFIG_QUEUED -> DHCP_CONFIG_MEDIATED ->
DHCP_PARSED_RING3 -> BOOT_OK -> TEST_OK`. S0.3c-5e2b2b2 ist vollständig
umgesetzt: Der feste 52-Byte-Syscall 81 akzeptiert für Renewal/Rebind nur die
aktuelle gesunde Dienstgeneration und korreliert Frame-CRC, absolute
Lieferdeadline,
Client-MAC sowie die geschützte DHCP-Transaktions-ID. ACK benötigt die
vollständigen Netzmasken-, Gateway-, DNS- und Lease-Optionen; NAK entzieht die
Lease fail-closed. Der RTL8139-Test bestätigt `DHCP_RENEW_REQUESTED ->
DHCP_RENEW_INGRESS_RING3 -> DHCP_RENEWED`. S0.3c-5e2b2b2b1 ist ebenfalls
abgenommen: Der append-only Syscall 82 startet nur für die aktuelle gesunde
Dienstgeneration eine geschützte 1.500-ms-Boot-Transaktion. Ring 3 validiert
OFFER und ACK, während Ring 0 ausschließlich je ein DISCOVER beziehungsweise
REQUEST sendet. Transaktions-ID, Frame-CRC, Client-MAC, Server-ID, angebotene
Adresse und Lieferdeadline werden vor jedem Zustandswechsel geprüft. Der
Dienst führt höchstens drei Versuche aus; der Kernel wartet insgesamt höchstens
sechs Sekunden und bleibt danach ohne IP fail-closed. Der RTL8139-Lauf bestätigt
`DHCP_BOOT_DISCOVER_RING3 -> DHCP_BOOT_OFFER_RING3 -> DHCP_BOOT_ACK_RING3 ->
DHCP_CONFIG_QUEUED -> DHCP_CONFIG_MEDIATED -> BOOT_OK`. S0.3c-5e2b2b2b2 hat
anschließend die synchronen Ring-0-Parserroutinen, die dedizierte Vier-Slot-
DHCP-Queue und den Supervisor-Poller entfernt. Der statische Service-Frame-
Handoff ist damit der einzige DHCP-Eingang. Boot und Renewal wurden erneut mit
RTL8139 abgenommen; der Bootlauf enthält zusätzlich Dienst-Crash, Restart und
Queue-Druck. Der allgemeine Ring-0-UDP-Parser, seine Legacy-Einspeisung und die
unbenutzte direkte Echo-Sendehilfe sind ebenfalls entfernt. Ring 0 verwirft
UDP-Eingang fail-closed; ausschließlich der CRC-/generation-/deadlinegebundene
Ring-3-Ingress darf für ein aktives Binding Antwortautorität erzeugen. Reale
RTL8139-Läufe bestätigen den primären und einen zweiten gebundenen Port sowie
Boot-DHCP. Damit ist S0.3c-5e2b abgeschlossen. S0.3c-5e2c ergänzt nun einen
heapfreien ICMP-Echo-v1-Shadow-Parser. Er akzeptiert ausschließlich vom
IPv4-v1-Parser validierte Echo-Requests oder -Replies, verlangt Code null,
prüft die vollständige ICMP-Prüfsumme einschließlich ungerader Nutzdaten und
publiziert ein kanonisches 28-Byte-Ergebnis. Ein PID-/generations- und
Frame-CRC-gebundener Liefernachweis erzeugt nur den Diagnosemarker
`ICMP_PARSED_RING3`; er verleiht keine Ausgabeautorität. S0.3c-5e2d bindet
darauf jede ICMP-Eingangsentscheidung an ein redundanzgeschütztes Ticket mit
PID, Prozessgeneration, Frame-CRC und absoluter 250-ms-Deadline. Syscall 83
akzeptiert ausschließlich kanonisches `DROP`, `ECHO_REQUEST` oder
`ECHO_REPLY`; erst danach darf eine Echo-Autorität entstehen beziehungsweise
ein erwarteter Ping abgeschlossen werden. Der alte Ring-0-ICMP-Parser ist
entfernt und ICMP fällt dort geschlossen aus. Der reale RTL8139-Lauf bestätigt
`ICMP_PARSED_RING3 -> ICMP_ECHO_QUEUED -> ICMP_INGRESS_RING3 ->
ICMP_ECHO_MEDIATED -> TEST_OK`; der bisherige ICMP-Echo-Lauf bleibt grün.
S0.3c-5e2e entfernt anschließend auch `handle_ip_packet`: Fallback-IPv4-Frames
werden in Ring 0 weder geparst noch demultiplext und dürfen keine implizite
ARP-Lernmutation mehr auslösen. S0.3c-5f schließt danach den gesamten
Legacy-Eingang: Die separate 64-Slot-Ring-0-RX-Queue, `netstack_process_packet`,
der ARP-Parser, der ungeschützte ARP-Cache und seine Lernrichtlinie sind
entfernt. Alle ARP-Lookups verwenden ausschließlich den redundant geschützten,
generations- und leasegebundenen Cache; Routenwechsel widerrufen alte und neue
Gateway-Bindungen vor der Konfigurationspublikation. Vollständige Frames gehen
nur noch an die statische Ring-3-Servicequeue, während die Monitorqueue rein
diagnostisch bleibt. Damit ist S0.3c-5 abgeschlossen.

**S0.3c-6a ist umgesetzt:** Storage-Schreiboperationen und VFS-Mutationen
besitzen nun jeweils einen redundant geschützten Aktivzustand und eine
saturierend gebildete absolute Deadline. Überlappende Operationen werden vor
dem ersten Seiteneffekt abgewiesen. Progress-, Integritäts- oder Idle-Fehler
verriegeln den Hardware-Schreibpfad beziehungsweise den VFS-Zustand
fail-closed. **S0.3c-6b ist umgesetzt:** Der gemeinsame Dataplane-Vertrag
besitzt acht statische Slots, 24-Bit-Generationshandles und versionierte
Block- sowie VFS-Operationen. Nutzdaten sind auf 512 Byte begrenzt und liegen
als CRC-geschützte Primär-/Schattenkopie vor; Metadaten und gebundene
Dienstidentität verwenden `critical_object`. Claim, Complete und Collect sind
generationgebunden, Rechte werden vor Zustandsänderungen geprüft und
Prozessende widerruft offene Requests. Der 128-Byte-IPC-Kanal muss damit nur
Handle und Status transportieren. S0.3c-6c bindet diesen Pool als Nächstes an
einen restartbaren Ring-3-Storage-Service.

**S0.3c-6c ist umgesetzt:** `STORAGE.PRG` läuft in einem eigenen
Default-Deny-Profil und besitzt ausschließlich Zeit-/Fortschritts- sowie
Storage-Bind/Claim/Block-Read/Complete-Syscalls. Eine geschützt gespeicherte
Dienstidentität bindet den Dataplane generationsgenau. Der Supervisor erkennt
Starttimeout oder Prozessverlust, widerruft alte Slots und startet höchstens
drei Ersatzinstanzen; danach werden Blockschreibpfad und VFS-Mutationen
fail-closed gefenct. Requests besitzen zusätzlich eine monotone Maximaldauer
von fünf Sekunden und ein Zwei-Slot-Clientlimit. Der reale Gasttest liest über
Client -> Pool -> Ring-3-Dienst -> kernelvermittelten ATA-Zugriff -> Pool den
MBR und validiert `0x55AA`. S0.3c-6d ergänzt als Nächstes echte
Crash-/I/O-/Power-Loss-Injektion.

**S0.3c-6d1 ist umgesetzt:** Ein ausschließlich im separaten Testimage
kompilierter Hook beendet genau die erste Storage-Dienstgeneration nach einem
erfolgreichen ATA-Read und vor Copyout/Complete. Der beanspruchte Request wird
beim Exit generationssicher widerrufen. Der Supervisor erkennt den Verlust,
startet innerhalb seines festen Budgets eine neue Instanz und bindet nur deren
neue Generation. Der Client akzeptiert den alten Handle nicht erneut, stellt
höchstens einen Ersatzrequest und validiert danach wieder den echten MBR. Der
QEMU-Gate verlangt die geordnete Folge `TEST_CRASH_INJECTED ->
SERVICE_FAILURE_DETECTED -> SERVICE_RESTARTED -> SERVICE_READY ->
STORAGE_RESTART_OK -> TEST_OK`. S0.3c-6d2 ergänzt als Nächstes einen realen,
kontrollierten ATA-I/O-Fehler; Power-Loss bleibt S0.3c-6d3.

**S0.3c-6d2 ist umgesetzt:** Der vermittelte Block-Read führt höchstens zwei
ATA-Versuche aus. Scheitern beide, erhält der Client `-EIO` und die Ressource
wird in der redundant geschützten Dienstkontrolle quarantänisiert. Bis zur in
S0.3c-6e eingeführten vollständigen Requalifizierung endet jeder Folgezugriff
derselben Ressource vor dem Hardwareaufruf mit `-EHOSTDOWN`; andere Kernel- und
Dienstfunktionen laufen weiter. Ein separater Testbuild erzwingt beide
Fehlschläge. Der normale Build enthält keinen Injektionspfad.

**S0.3c-6d3 ist umgesetzt:** Der persistente QEMU-Test
erzeugt eine ACTIVE-Undo-Transaktion, verändert zwei Zielsektoren, hält die
alten Daten redundant vor und beschädigt zusätzlich die primäre
Journal-Metadatenkopie. Beim Neustart wählt der Kernel konservativ die gültige
Kopie, restauriert beide Sektoren, repariert die Header und schreibt CLEAN.
Erst nach vollständiger Probe-Reintegration startet GTEST; der neu gebundene
Ring-3-Storage-Dienst muss danach den echten MBR-Selbsttest bestehen. Der Lauf
prüft abschließend die persistenten Sektoren und beide identischen Header. Das
Gate deckte außerdem einen Start-Race auf: Der Supervisor-Worker konnte den
noch nicht explizit aktivierten Dienst vor `storage_service_start()` starten.
Ein eigener Aktivierungszustand trennt nun „noch nicht gestartet“ von
„ausgefallen“, und IRQ-serialisierte Kontrollzugriffe verhindern konkurrierende
Reparatur der redundanten Kopien.

**S0.3c-6e ist umgesetzt:** ATA- und FDD-Ressourcen erhalten beim Boot einen
redundant geschützten Fingerprint. Nach einem I/O-Ausfall prüft ein begrenzter
Hintergrundlauf Controller- und Medienidentität sowie zwei frische, identische
Bootsektor-Reads. Ein reiner Lesefehler darf das unveränderte Medium wieder
`ONLINE_RW` schalten; der QEMU-Gate verlangt dafür `RESOURCE_QUARANTINED ->
RESOURCE_REINTEGRATED_RW -> STORAGE_MEDIA_REINTEGRATED_OK -> TEST_OK`.
Unsicher abgeschlossene Schreibzugriffe werden nie blind wiederholt: Sie
fencen Storage und VFS und erlauben höchstens `ONLINE_RO`.

Das VMware-A:-Reconnect-Problem ist zusätzlich als echter QEMU-QMP-Hotplug-
Lauf reproduziert und geschlossen. Ein normaler FAT12-Lesefehler meldet die
FDD-Ressource und quarantänisiert sie. Nach dem Wiedereinlegen setzt die Probe
den FDC zurück, leert die Reset-Interrupts, programmiert ihn neu, kalibriert
das Laufwerk und liest das Medium zweimal außerhalb des normalen gesperrten
Pfads. Erst danach folgen `RESOURCE_REINTEGRATED_RW 1`, eine erneut
erfolgreiche Lektüre von `HOTPLUG.TXT` und `TEST_OK`.

**S0.3c-6f bleibt offen:** Der Vertrag gilt für alle persistenten Medien, doch
das persistente Undo-Journal ist derzeit auf markierte FAT32/ATA-Images
begrenzt. FDD/FAT12, EXT2, fremde sowie künftige USB-/Flash-/NVMe-Backends
benötigen ein gemeinsames Undo/COW/Journal-Protokoll, geordnete Flush-/Barrier-
Semantik und echte Power-Loss-Injektion. Vor diesem Nachweis darf ein Medium
nach unklarem Schreibabschluss nicht automatisch wieder beschreibbar werden.
Bei Wechselmedien fehlen außerdem noch eine stärkere Ganzmedien-Identität und
kontrollierte Cache-Invalidierung beziehungsweise ein Remount, wenn sich der
Inhalt außerhalb von REIST bei unverändertem Boot-Fingerprint geändert hat.
S0.3c-6 bleibt deshalb teilweise offen; S0.3c-7 kann parallel fortgesetzt
werden.

**S0.3c-7a ist umgesetzt:** Ein statischer, `critical_object`-geschützter
Zwei-Knoten-Protokollkern verwaltet aktive und Standby-ID, monotone Lease,
64-Bit-Epoche, Fence-Epoche und Transitionssequenz. Nur der aktive Knoten darf
seine aktuelle, noch nicht abgelaufene Epoche verlängern. Eine Übernahme ist
erst nach Leaseablauf und expliziter Bestätigung möglich, dass genau diese
Epoche extern gefenct wurde. Der Rollenwechsel erhöht die Epoche; alter Active,
alte Fence-Bestätigungen und alte Kandidaten verlieren damit dauerhaft ihre
Autorität. Alle Kapazitäten sind statisch, Überläufe enden fail-closed, und
Host-Fault-Tests decken Split-Brain, verfrühte Übernahme, stale Epoch sowie
einfach und doppelt beschädigte Kontrollkopien ab. Das ist nur der
Protokollbaustein: Ohne S0.3c-7b existieren weder unabhängiger Transport noch
rücklesbares Hardware-Fence; ein fail-operationaler Claim bleibt unzulässig.

**S0.3c-7b1 ist umgesetzt:** Der Protokollkern kann erst initialisiert werden,
nachdem genau ein statisches Fence-Backend mit getrennten Request- und
Readback-Funktionen gebunden wurde. Leaseablauf allein erzeugt keine
Bestätigung. `handover_request_fence()` fordert das externe Fence für Active-ID
und Epoche an; `handover_confirm_fenced()` akzeptiert den Zustand nur, wenn das
Backend exakt dieselbe ID/Epoche rückmeldet. Die potenziell langsamen
Backendoperationen laufen nicht mit deaktivierten IRQs. Danach werden Epoche,
Active-ID, Lease und Transitionssequenz unter Lock erneut geprüft, sodass ein
zwischenzeitlicher Rollen- oder Leasewechsel die Bestätigung verwirft. Das
Hostbackend beweist Negativpfade und idempotentes Readback. S0.3c-7b2b bleibt
offen, weil noch kein physisch unabhängiger Transport oder Interlock existiert.

**S0.3c-7b2a ist umgesetzt:** Ein isoliertes QEMU-Testprofil bindet den
Handover-Kern an COM2 statt an den Konsolenkanal. Der Kern sendet ein festes,
heapfreies 24-Byte-Request mit Version, Active-ID, 64-Bit-Epoche und CRC32. Ein
getrennter Hostprozess validiert den vollständigen Frame und antwortet nur mit
einem CRC-geschützten Ack für exakt dasselbe Tupel. Alle UART-Wartepfade haben
eine monotone 1-s-Deadline. Der reale Gastlauf beweist geordnet
`REQUEST_SENT -> FENCE_CONFIRMED -> TAKEOVER_OK -> BOOT_OK -> TEST_OK`.
Das trennt Transport und Supervisorprozess, aber nicht Strom, CPU oder
Zeitquelle; daher bleibt S0.3c-7b2b auf Zielhardware offen und ein
Fail-operational-Claim weiterhin unzulässig.

**S0.3c-7c1 ist umgesetzt:** Zwei separat gebaute Images laufen gleichzeitig
in zwei QEMU-Prozessen. Der Active sendet seinen versionierten Epoch-Snapshot;
der Host leitet ihn erst nach einem CRC-geschützten `STANDBY_READY` an den
Standby weiter. Nach Leaseablauf fordert ausschließlich der Standby das Fence
an. Der Host beendet den Active-Prozess, prüft dessen Ende und sendet erst dann
den Ack. Danach übernimmt der Standby, startet seine überwachten Dienste und
besteht den vollständigen Ring-3-Gasttest. Drei Läufe des finalen Standes bestätigten
die Reihenfolge. Zum Stand von 7c1 waren kontinuierliche Nutzdaten-Replikation
und Reintegration noch nicht nachgewiesen; 7c2a/7c2b schließen diese beiden
Lücken. Physisch unabhängige Hardware bleibt weiterhin offen.

**S0.3c-7c2a ist umgesetzt:** Ein fester Referenz-Dienstzustand liegt als
redundantes `critical_object` mit ECC, CRC und semantischem Validator vor. Der
Active veröffentlicht vor dem Failover drei CRC-geschützte 52-Byte-Frames mit
identischer Quelle/Epoche und lückenlos steigender 64-Bit-Sequenz. Der Standby
akzeptiert ausschließlich den unmittelbaren Nachfolger; Replay, Lücke,
Quellen-/Epochenwechsel und doppelte Kopienkorruption enden fail-closed. Nach
dem extern bestätigten Fence übernimmt er mit erhöhter Epoche und publiziert
den neuen Zustand. Ein drittes, separat gestartetes QEMU-Image erhält diesen
Zustand, besteht die Integritätsprüfung, darf aber weder die alte Lease
verlängern noch ohne neues Fence übernehmen. Drei vollständige Läufe bewiesen
parallel den Weiterbetrieb des übernommenen Kanals bis `TEST_OK`. Das ist ein
prozessgetrennter Referenznachweis. Das folgende Paket 7c2b bindet diesen
Vertrag an den Storage-Produktionszustand; physisch unabhängige Zielhardware
bleibt unter 7b2b offen.

**S0.3c-7c2b ist umgesetzt:** Der replizierte Produktionszustand ist der
CRC32-Fingerprint des tatsächlich erkannten ATA-Bootvolumes einschließlich
gültiger MBR-Signatur. Standby und reparierter Kanal aktivieren vor dem Mount
ein separates Storage-Handover-Gate; jede überwachte ATA-/FDD-Schreiboperation
prüft dieses Gate. Der Standby übernimmt drei lückenlos sequenzierte
Fingerprint-Checkpoints und liest seinen eigenen Datenträger zur lokalen
Gegenprüfung. Erst nach nachgewiesen beendetem Active, extern bestätigtem
Fence, Epoch-Promotion, erfolgreicher Publikation des promovierten Zustands und
erneutem Volume-Selbsttest wird das Gate freigegeben. Der anschließende
Ring-3-Test führt reale VFS-Mutationen aus und erreicht `TEST_OK`. Der reparierte
dritte Kanal validiert denselben Zustand, bleibt jedoch ausdrücklich gehalten
und ohne Takeover-Autorität. Falscher Fingerprint, I/O-Fehler, Replay/Lücke oder
vorzeitige Freigabe bleiben fail-closed. Drei vollständige Läufe des finalen
Stands waren erfolgreich. Damit ist das QEMU-Referenzpaket 7c abgeschlossen;
das elektrisch unabhängige Interlock auf Zielhardware (7b2b) und
Common-Cause-/Hardware-Failover-Gates (7d) bleiben offen.

Ein einzelner monolithischer Kernel kann nach unbekannter Eigenkorruption nicht
glaubwürdig störungsfrei weiterlaufen. Unterbrechungsfreie Essential Functions
bei Kernel-Panic benötigt S0.3: eine unabhängige Supervisor-/Standby-Domäne,
die Ausgänge einzäunt und innerhalb der FTTI übernimmt. S0.3a allein erfüllt
diese Forderung ausdrücklich nicht. Bereits vorgezogene Funktionsinkremente
gelten daher nicht als Abnahme des S0-Gates.

Systematische Allocation-Failure-Injection, ein IRQ-tauglicher Allocator,
weitere Reaper-Stresstests und Highmem/`kmap` bleiben zusätzliche
Speicherhärtung.
