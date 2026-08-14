# Fehlstellenanalyse und Implementierungsfahrplan

Stand: 14. August 2026

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
  - [ ] Versioniertes Gefahrenregister mit FTTI und Restrisiken
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
  - [ ] **S0.3c-5 in Arbeit:** Netzwerkdatenpfad vollständig aus Ring 0 lösen
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
  - [x] S0.3c-6 Storage-/Dateisystemdienst als nächste isolierte Domäne
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
  - [ ] S0.3c-7 Unabhängiger Standby-/Supervisor-Kanal und realer Handover
- [ ] S0.4 Deterministische Planung und garantierte Ressourcen
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

### Prozess, Scheduler und IPC

- [x] **S0.3a umgesetzt:** 16 statische Endpoints, acht Capabilities je Prozess,
  vier Nachrichten je Queue, 128 Byte Nutzlast, blockierendes Send/Receive,
  explizite abschwächende Delegation ohne `CONTROL` und vollständiger
  Exit-Widerruf
- [x] **umgesetzt:** ein Taskslot, ein Prozessslot und 32 Frames bleiben für
  explizite Supervisor-Spawns reserviert
- [x] **umgesetzt:** vollständiges 56-Syscall-Inventar; Default-Deny-Probeprofil
  und generation-sicheres Child-Kill vor jedem Seiteneffekt
- [ ] generische Wait-Queues auf weitere Geräte- und Protokollereignisse anwenden
- [ ] Pipes, Prozessgruppen und ein kleines Signalmodell
- [ ] `waitpid(-1, ...)`, optionales nichtblockierendes Warten und saubere
  Reparenting-/Reaper-Semantik
- [ ] dynamische oder zumindest deutlich größere Tasktabelle statt `MAX_TASKS 8`
- [ ] Prioritäten erst nach korrekter Blockierung; Threads und SMP deutlich später
- [x] echte User-Stack-Guardpages zusätzlich zu den vorhandenen Kernel-Guardpages
- [ ] aussagekräftigere Prozessstatistiken

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

- [ ] eine einzige gemeinsame, versionierte Quelle für Syscallnummern und
  Fehlercodes
- [ ] stabile `errno`-ähnliche Fehlersemantik; aktuell werden VFS-Fehler oft auf
  allgemeine Werte wie `-2`, `-5` oder `-9` reduziert
- [ ] `open`-Flags (`RDONLY`, `WRONLY`, `RDWR`, `CREATE`, `TRUNC`, `APPEND`)
- [ ] `lseek`, `fstat`, `truncate`, später `dup`/`dup2`; FAT32/ATA-`fsync` und
  Rename sind als erste persistente Spezialfälle vorhanden
- [ ] echte Deskriptoren 0/1/2 für Standard-Ein-/Ausgabe
- [ ] ABI-Fähigkeitsabfrage, damit ältere Programme kontrolliert weiterlaufen

### VFS, Dateisysteme und Blockgeräte

- [ ] Rename auf FAT12 sowie verzeichnis- und volumenübergreifendes Verschieben;
  FAT32 Same-Directory-Replace ist journalgestützt atomar
- [ ] konsistente Open-Handle-, Delete- und Unmount-Semantik unter Nebenläufigkeit
- [ ] eine generische `block_device`-Schnittstelle mit `read`, `write`, `flush`,
  Sektorgröße und Kapazität statt direkter ATA/FDD-Kopplung
- [ ] Partitionen als eigene Blockgeräte; derzeit wird pro physischem Laufwerk nur
  ein gefundenes Dateisystem automatisch gemountet
- [ ] vollständige MBR-Prüfung und später GPT; Partitionsgrenzen bei jeder I/O
  erzwingen
- [ ] ATA LBA48 und Multi-Sektor-I/O; aktuell ist der PIO-Treiber auf LBA28
  begrenzt (`drivers/block/ata.h:25`)
- [ ] FAT-Schreibreihenfolge, Fehlerpropagation und Power-Loss-Tests
- [ ] FAT Long File Names und eine definierte Zeichenkodierung
- [ ] FAT-Zeitstempel über VFS; FAT32 liefert derzeit Nullen
- [ ] mehrere FAT12-Volumes; der Adapter besitzt aktuell nur einen globalen
  `mounted_fat12_fs`
- [ ] EXT2 entweder klar dauerhaft read-only halten oder erst nach den
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

- [ ] TTY-Abstraktion mit kanonischem/raw Modus, Echo und per-Prozess
  Vordergrundgruppe
- [ ] `Ctrl+C` als Signal an die Vordergrundgruppe statt Sonderbehandlung direkt
  im `getchar`-Syscall
- [ ] dynamische Terminalgröße; mehrere Syscalls prüfen heute fest gegen 80x25,
  obwohl der Framebuffer andere Größen besitzen kann
- [ ] Quotes/Escapes, Umgebungsvariablen, Verlauf und Exitcodes in der
  Userspace-Shell
- [ ] Pipes, Ein-/Ausgabeumleitung und Hintergrundjobs nach Fertigstellung von
  Deskriptoren, Wait-Queues und Signalen
- [ ] Editor: das sichere `TEMP -> fsync -> close -> rename` ist umgesetzt;
  dynamischer Puffer, Suche, Auswahl/Clipboard und die Aufhebung des
  Limits von 200 Zeilen fehlen
- [ ] ein kleines Ring-3-`init` als PID 1 statt direktem Shellstart durch den Kernel
- [ ] Mausereignisse, Fokusmodell, Compositor und Windowmanager als getrenntes
  späteres Paket statt Erweiterung der schmalen Display-ABI

### Netzwerk

- [ ] ARP-Ablauf/Erneuerung, DHCP-Lease-Timer und Renew/Rebind
- [ ] robuste IPv4-Fehlerpfade und definierter Umgang mit Fragmenten; aktuell
  werden Fragmente verworfen
- [ ] nutzbares UDP-Binding: `udp_bind()` ist in
  `drivers/net/netstack.c:961-964` noch ein Stub
- [ ] Socketobjekte als Dateideskriptoren und Userspace-Syscalls
- [ ] DNS-Resolver nach funktionierenden UDP-Sockets
- [ ] TCP mit Zustandsautomat, Sequenznummern, Retransmission, Timern, Fenstern und
  sauberem Verbindungsabbau; alle vier öffentlichen TCP-Funktionen sind derzeit
  Stubs (`drivers/net/netstack.c:966-970`)
- [ ] erste Anwendungen wie `netcat` und ein kleiner HTTP/1.0-Client
- [ ] IPv6 erst nach einer belastbaren IPv4-/Socket-Schicht
- [ ] VMXNET3 nur implementieren, wenn E1000 nicht mehr als VMware-Referenz reicht;
  der vorhandene Treiber deaktiviert das Gerät absichtlich

### USB und moderne Hardware

- [ ] DMA-API für physische Adressen, Alignment, 32-Bit-Grenzen und kohärente
  Puffer
- [ ] vollständiger xHCI-Reset und Controllerstart, Command-/Event-/Transfer-Ringe,
  Doorbells und Interrupts
- [ ] Root-Port-Status, Geräteadressierung, Deskriptoren und Konfiguration
- [ ] Hub-Unterstützung, danach HID-Tastatur und USB-Massenspeicher
- [ ] der heutige Code endet nach PCI/BAR/IRQ-Ausgabe und lässt xHCI deaktiviert
  (`drivers/usb/xhci.c:4-25`); `hub.c` und `hid_kb.c` sind Platzhalter
- [ ] zentrale, validierte ACPI-Schicht statt der isolierten experimentellen
  HPET-Suche; danach HPET als optionalen Clocksource, IOAPIC und
  Poweroff/Reboot integrieren
- [ ] AHCI/NVMe erst nach Block- und DMA-Abstraktion

### Sicherheit, Diagnose und Produktreife

- [ ] dokumentiertes Bedrohungsmodell: vorerst vertrauenswürdiger Single-User oder
  später Benutzer/Rechte/Capabilities
- [x] Prozess-Kill ist generation-sicher auf eigene Kinder bzw. explizite
  Supervisor-Autorität begrenzt
- [ ] Dateirechte und ACLs; FAT besitzt derzeit keine Berechtigungsmetadaten
- [ ] keine kryptografische Boot-Authentizität: CRC32 erkennt Beschädigung, aber
  keinen absichtlichen Austausch
- [ ] Zufallsquelle/CSPRNG, ASLR und sichere Netzwerk-Defaults erst bei einem
  Sicherheitsziel
- [ ] optionale Panic-Symbolauflösung zusätzlich zum vorhandenen Register-/CR2-
  Kontext und der SHA1-Build-ID
- [ ] Debug-Buildprofil, statische Analyse und hostseitiges Sanitizer-/Fuzzing für
  Parser
- [ ] reproduzierbare Gasttests und eine kleine Hardwarematrix

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
Abhängigkeiten erfüllt sind.

### Phase 0 — Korrektheit und verlässliche Nachweise

#### R0.1 Atomarer Wait/Wakeup-Pfad — S

**Status (13. August 2026):** Kernfix umgesetzt und hostseitig validiert. Der
Statuscheck und die Registrierung als `TASK_WAITING` sind auf dem aktuellen
Single-Core-Kernel durch einen gemeinsamen IRQ-geschützten Abschnitt atomar.
Der neue Regressionstest `test/test_wait_source.py` schützt diese Invariante;
die aktuelle Gesamtabnahme umfasst 134 Hosttests, den
Windows-QEMU-Referenzbuild und 64
erfolgreiche Spawn/Wait-Zyklen im automatisierten Gasttest.

- [x] Kindstatusprüfung und Registrierung des aktuellen Tasks als `TASK_WAITING`
   auf dem Single-Core-System in einem gemeinsamen IRQ-geschützten Abschnitt
   ausführen.
- [x] Kind-Exit, Kill und normales Exit über denselben Wakeup-Pfad führen.
- [x] Status genau einmal konsumieren; verwaiste Zombies kontrolliert aufräumen.
- [x] Einen deterministischen Test-Hook für einen Kind-Exit im bisherigen
   Race-Fenster sowie einen Gast-Stresstest mit vielen Spawn/Wait-Zyklen bauen.

**Fertig, wenn:** Kein Test hängt, jeder Exitstatus wird genau einmal geliefert
und der Elternprozess verbraucht während des Wartens keine CPU.

#### R0.2 Exception-Stubs vereinheitlichen — S

**Status (13. August 2026):** Umgesetzt. Alle Vektoren 0–31 verwenden die
Makros `ISR_NO_ERROR_CODE` oder `ISR_CPU_ERROR_CODE`; Vector 17 (`#AC`) nutzt
nun korrekt den CPU-Fehlercode. Compile-Time-Assertions sichern Offsets und
Größe des gemeinsamen `Registers`-Frames. Die vollständige Fehlercode-Matrix
ist hostseitig getestet.

- [x] Vektoren mit und ohne CPU-Fehlercode tabellarisch definieren und Stubs aus
   zwei Makros erzeugen.
- [x] Vector 17 korrigieren; 8, 10–14, 17, 21, 29 und 30 explizit prüfen.
- [x] Layout von Assembly-Frame und `Registers` mit statischen Offsets absichern.
- [x] Ring-3-Tests für Divide-by-zero, Invalid Opcode und Page Fault ergänzen;
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

- [x] Für Version 1 nur `base_address == USER_BASE` und
   `relocation_size == 0` akzeptieren.
- [x] Validator, Loader, Python-Builder und Dokumentation auf dieselben Regeln
   bringen; tote alternative ELF-Lader aus dem Laufzeitpfad entfernen.
- [x] Negative Tests für Relokationen, Überläufe, überlappende Bereiche und
   falsche Entry-Points ergänzen.
- [x] Anforderungen an ein späteres segmentbasiertes PRG v2 separat notieren.

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

- [x] QEMU mit serieller Konsole, festem Timeout und eindeutigem
   `BOOT_OK`/`TEST_OK`-Protokoll starten.
- [x] Einen Ring-3-Teststarter ins Image legen, der Userspace-Schutz,
   Spawn/Wait, Datei-I/O und Exceptions prüft; nach dem Erfolg beendet der
   Host QEMU kontrolliert.
- [x] Den Smoke-Test direkt an das erzeugte `build/reist-os.img` binden;
   Legacy-Fixture-Tests getrennt halten.
- [x] Den Smoke-Test in CI ausführen und Logs als Artefakt sichern.

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

- [x] Generische Wait-Queues mit Wake-one/Wake-all einführen.
- [x] 64-Bit-monotone Zeit und geordnete Deadline-Liste implementieren.
- [x] `sleep_ms` und `yield` als Syscalls anbieten; `SYS_DELAY` kompatibel darauf
   abbilden.
- [x] Keyboard-, serielle und später Netzwerk-I/O auf blockierende Events
   vorbereiten.
- [x] APIC-Timer gegen PIT kalibrieren und bei fehlendem LAPIC einen
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

- [x] Erkannte, verwaltete, reservierte und freie Frames separat zählen.
- [x] Framezugriff oberhalb 256 MiB durch ein konsistentes Directmap bis 1 GiB
   ermöglichen und die Grenze explizit ausweisen.
- [x] Kernel-Heap erweiterbar machen und belegte/freie Bytes exportieren.
- [x] Statische und dynamische Kernelstacks mit 64-Byte-Canaries und
   ESP-Bereichsprüfungen schützen.
- [x] Allokationsfehler, Fragmentierung und wiederholtes Prozess-Reaping testen.

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

- [x] Festlegen, welche APIs in IRQ-Kontext, mit deaktivierter Präemption oder
   schlafend aufgerufen werden dürfen.
- [x] Lock-Reihenfolge für Scheduler, VFS, Dateisysteme und Treiber dokumentieren.
- [x] Assertions für IRQ-/Lock-Zustand sowie strukturierte Log-Level ergänzen.
- [x] Panic-Ausgabe um vollständige Register, CR2 und Build-ID erweitern.

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

- [ ] Zielsystem, Einsatzprofil, Umgebung und vorhersehbaren Fehlgebrauch festlegen.
- [ ] Essential Functions, sichere/degradierte Zustände und je Gefahr die FTTI
   definieren.
- [ ] Ein versioniertes Gefahrenregister mit Ursache, Kontrolle, Restrisiko und
   Verifikationsnachweis anlegen.
- [ ] Traceability `Gefahr -> Anforderung -> Design -> Code -> Test -> Ergebnis`
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

- [ ] Nicht gemappte Guardpages für jeden Kernel- und Userstack, statische
   Stackbudgets, Watermarks und Rekursionsverbote einführen.
- [ ] Einen reservierten Exception-/Double-Fault-/NMI-Notfallstack mit
   vorallokiertem, beschränktem Crashdatensatz bereitstellen.
- [ ] Wiederherstellbare Prozess-/Dienstfehler von möglicher globaler
   Kernelkorruption trennen; betroffene Domänen einfrieren und aus bekannt
   gutem Zustand neu starten.
- [ ] `panic()` darf nicht nur `halt()` ausführen: Ausgänge zuerst in den
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

- [x] S0.3a um IPC-Deadlines, Metadatenintegrität, explizite Delegation,
   Service-Taskreservierung und Capability-Gates ergänzen.
- [x] S0.3b als überwachte, neu startbare Least-Privilege-Probedomäne abnehmen;
   danach Netzwerk, Storage und komplexe Treiber schrittweise migrieren.
- [ ] Fortschritts-/Deadline-Watchdogs, Restart-Budgets, Fencing, Selbsttest und
   sichere Reintegration für jede migrierte Domäne nachweisen.
- [ ] Hot-Standby oder Dual-Controller-Handover mit regelmäßigem realem
   Failover-Test aufbauen.
- [ ] Common-Cause-Fehler bewerten; wo nötig unabhängige Hardware,
   Stromversorgung, Takte, Sensorpfade oder diverse Implementierungen nutzen.

#### S0.4 Determinismus und garantierte Ressourcen — L

- [ ] Kritische Tasks erhalten feste Prioritäten, CPU-/Speicher-/Queue-Budgets,
   Admission Control und nachgewiesene Worst-Case-Laufzeiten.
- [ ] Im kritischen Modus nur reservierte Pools verwenden; unbeschränkte
   Allokation, Rekursion, Retries und Warteschlangen sind dort unzulässig.
- [ ] Überlast, Priority Inversion, Interruptstürme und Zeitquellenausfall müssen
   einen getesteten degradierten Zustand auslösen.
- [ ] Kritische Kernelobjekte selektiv über den `critical_object`-Umschlag mit
   wortweisem SECDED, CRC32, Version/Sequenz, semantischem Validator und
   Primary/Shadow schützen; Bitflip-Injection misst Korrektur und Eskalation.

#### S0.5 Datenintegrität, Boot und unterbrechungsarme Updates — XL

- [ ] Sicherheitsrelevanten Zustand transaktional, checksummiert, versioniert und
   redundant speichern; Stromausfall an jeder Commitstelle injizieren.
- [ ] Verifizierten Boot, signierte Artefakte, reproduzierbare Builds, Provenienz
   und SBOM einführen.
- [ ] Updates als atomaren A/B-Wechsel mit Selbsttest und automatischem Rollback
   ausführen; Standby-Kanäle nacheinander statt gleichzeitig aktualisieren.
- [ ] Kernel-Livepatching bleibt eine eng begrenzte Ausnahme mit Quieszenzpunkt,
   Zustandskompatibilität, Vorabnachweis und sicherem Rollback.

#### S0.6 Verifikation und Langzeitbetrieb — XL

- [ ] Statische Stack-/Code-/WCET-Analyse, Fuzzing, modellbasierte Tests und
   unabhängige Reviews als Gates einführen.
- [ ] Fault-Injection für Bitfehler, Speichererschöpfung, Timingfehler,
   Geräteverlust, beschädigte Eingaben, Stromausfall und Updates automatisieren.
- [ ] Soak-/Alterungstests, ECC/EDAC, Medien-Scrubbing und Hardwaretausch über die
   geplante Produktlebensdauer nachweisen.
- [ ] Toolchain, Schlüssel, Abhängigkeiten, Feldtelemetrie, Schwachstellen,
   Beschwerden, Patches und Rückrufe kontrolliert über den Lebenszyklus führen.

### Phase 2 — Deskriptoren, VFS und zuverlässige Datenträger

#### R2.1 ABI v1 und vollständige Dateideskriptoren — L

- [ ] Syscallnummern, Strukturen und Fehlercodes aus einem gemeinsamen ABI-Header
   für Kernel und SDK generieren bzw. teilen.
- [ ] Open-Flags, Rechte je Handle und Standarddeskriptoren 0/1/2 ergänzen.
- [ ] `lseek`, `fstat` und `truncate` implementieren; die bestehenden Rename- und
   `fsync`-Syscalls in den gemeinsamen ABI-Header überführen.
- [ ] Teilzugriffe, EOF, ungültige Handles und Prozess-Exit vollständig testen.

#### R2.2 VFS- und FAT-Zuverlässigkeit — L

- [ ] **Teilstatus:** Atomisches Same-Directory-Rename/Replace ist für FAT32
   umgesetzt; Cross-Directory, FAT12 und offene Handle-Semantik fehlen.
- [ ] **Teilstatus:** Der Editor nutzt `TEMP -> fsync -> close -> rename`; FAT12
   und künftige Blockgeräte benötigen noch einen gleichwertigen Sync-Vertrag.
- [ ] Open/Delete/Unmount-Regeln und Locking vereinheitlichen.
- [ ] Fehler nach jedem einzelnen Sektorwrite injizieren und das resultierende
   Image mit einem Hostprüfer untersuchen.
- [ ] Danach Zeitstempel und LFN ergänzen; EXT2 vorerst ausdrücklich read-only
   mounten.

#### R2.3 Blockgeräte und Partitionen — L

- [ ] ATA und FDD hinter eine gemeinsame Blockgeräte-API legen.
- [ ] MBR-Partitionen als Child-Geräte erzeugen und mehrere Partitionen mounten.
- [ ] Bereichsprüfung und `flush` zentral erzwingen.
- [ ] ATA LBA48 und gebündelte PIO-Transfers ergänzen.
- [ ] GPT, AHCI und NVMe als getrennte Folgepakete behandeln.

### Phase 3 — Unix-artige CLI-Grundfunktionen

#### R3.1 Pipes, Signale und TTY — XL

- [ ] Pipeobjekt mit blockierendem Ringpuffer auf Wait-Queues bauen.
- [ ] `dup`/`dup2` und Deskriptorvererbung beim Spawn ergänzen.
- [ ] Minimale Signale `SIGINT`, `SIGTERM`, `SIGKILL`, `SIGCHLD` implementieren.
- [ ] Prozessgruppen, Vordergrundgruppe und TTY-Modi hinzufügen.
- [ ] `waitpid` einschließlich `WNOHANG` bereitstellen.

#### R3.2 Userspace-Shell und Init — L

- [ ] Parser für Quotes und Escapes erstellen.
- [ ] `<`, `>`, `>>` und `|` auf Deskriptoren/Pipes abbilden.
- [ ] Hintergrundjobs, Verlauf, Umgebungsvariablen und Exitcodes ergänzen.
- [ ] Ein kleines `INIT.PRG` als Reaper und Starter der Shell einführen.

### Phase 4 — Netzwerk bis zu Anwendungen

#### R4.1 IPv4/UDP härten und Sockets einführen — L

- [ ] ARP-Ablauf, DHCP-Lease/Renew/Rebind und ICMP-Fehler ergänzen.
- [ ] Paketparser mit aufgezeichneten Frames, Grenzfällen und Fuzzing testen.
- [ ] Socketobjekte in die FD-Schicht integrieren: `socket`, `bind`, `sendto`,
   `recvfrom`, `close` und Timeouts.
- [ ] UDP-Echo zwischen Gast und Host als automatisierten Test betreiben.

#### R4.2 DNS — M

- [ ] DNS-Namen sicher kodieren/dekodieren, Kompressionszeiger begrenzen.
- [ ] A-Records, CNAME-Ketten, Timeouts und Caching implementieren.
- [ ] Lokalen deterministischen Testserver statt öffentliches Internet verwenden.

#### R4.3 TCP — XL

- [ ] Zustandsautomat und Connection Control Block erstellen.
- [ ] Sequenz-/ACK-Prüfung, Retransmission, RTO und Empfangsfenster ergänzen.
- [ ] Verbindungsaufbau, geordnete Daten, Reset und aktiven/passiven Close testen.
- [ ] Erst danach einen kleinen HTTP-Client oder `NETCAT.PRG` bauen.

### Phase 5 — USB und Plattform

#### R5.1 ACPI- und DMA-Basis — L

- [ ] RSDP/RSDT/XSDT mit Checksummen und Längengrenzen zentral parsen.
- [ ] MADT und HPET aus dieser Schicht beziehen; Poweroff/Reboot ergänzen.
- [ ] DMA-Puffer mit physischer Adresse, Alignment und Below-4-GiB-Grenze
   bereitstellen.

#### R5.2 xHCI in überprüfbaren Stufen — XL

- [ ] Controller stoppen/resetten und Capability-/Operational-Register validieren.
- [ ] DCBAA, Command Ring, Event Ring und Interrupter initialisieren.
- [ ] Root-Port-Anschluss erkennen, Slot aktivieren und Control Transfers testen.
- [ ] Deskriptoren lesen, Adresse und Konfiguration setzen.
- [ ] Erst HID-Boot-Tastatur, dann Hub und Mass Storage implementieren.

Jede Stufe benötigt einen QEMU-xHCI-Test und darf bei unbekannter Hardware das
Gerät nur deaktivieren, nicht den Bootvorgang blockieren.

### Phase 6 — optionale Modernisierung

Erst nach den vorherigen Meilensteinen einzeln entscheiden:

- [ ] UEFI-Boot und GPT
- [ ] x86-64-Port mit neuem ABI
- [ ] SMP, IOAPIC/MSI und per-CPU-Daten
- [ ] AHCI/NVMe, USB-Massenspeicher und Hotplug
- [ ] IPv6
- [ ] Mehrbenutzer-Identitäten, Dateirechte/ACLs und kryptografisch verifizierter
  Boot; dies ist getrennt von der bereits begonnenen Kernel-Capability-Basis
- [ ] dynamischer Linker, Shared Libraries, Paketverwaltung
- [ ] vollständiges Grafik-/Fenstersystem mit Compositor und Maus sowie Audio und
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
geschlossen; als nächstes folgt S0.3c-6.

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
wird in der redundant geschützten Dienstkontrolle dauerhaft für diesen Boot
quarantänisiert. Jeder Folgezugriff derselben Ressource endet vor dem
Hardwareaufruf mit `-EHOSTDOWN`; andere Kernel- und Dienstfunktionen laufen
weiter. Ein separater Testbuild erzwingt beide Fehlschläge und verlangt
`TEST_IO_ERROR_INJECTED -> RESOURCE_QUARANTINED ->
STORAGE_IO_QUARANTINE_OK -> TEST_OK`. Der normale Build enthält keinen
Injektionspfad. Offen bleibt S0.3c-6d3: Stromverlust während einer persistenten
Mutation und anschließende Recovery über den Ring-3-Dienst.

**S0.3c-6d3 und damit S0.3c-6 sind umgesetzt:** Der persistente QEMU-Test
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
Reparatur der redundanten Kopien. Nächster Dienstmigrationsschritt ist S0.3c-7.

Ein einzelner monolithischer Kernel kann nach unbekannter Eigenkorruption nicht
glaubwürdig störungsfrei weiterlaufen. Unterbrechungsfreie Essential Functions
bei Kernel-Panic benötigt S0.3: eine unabhängige Supervisor-/Standby-Domäne,
die Ausgänge einzäunt und innerhalb der FTTI übernimmt. S0.3a allein erfüllt
diese Forderung ausdrücklich nicht. Bereits vorgezogene Funktionsinkremente
gelten daher nicht als Abnahme des S0-Gates.

Systematische Allocation-Failure-Injection, ein IRQ-tauglicher Allocator,
weitere Reaper-Stresstests und Highmem/`kmap` bleiben zusätzliche
Speicherhärtung.
