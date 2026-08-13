# Fehlstellenanalyse und Implementierungsfahrplan

Stand: 13. August 2026

Dieses Dokument beschreibt den anhand des aktuellen Quellstands geprüften
Ist-Zustand, die wichtigsten noch fehlenden Betriebssystemfunktionen und eine
Reihenfolge, in der sie ohne unnötige Umbauten ergänzt werden können. Es ist
als Arbeits-Backlog gedacht: Jede Aufgabe besitzt eine feste ID, Abhängigkeiten
und überprüfbare Abnahmekriterien.

## 1. Zielbild und Abgrenzung

Als nächstes realistisches Ziel wird ein **stabiles, einzelbenutzerfähiges
32-Bit-x86-System mit Ring-3-Programmen, zuverlässiger CLI, Dateiverwaltung und
IPv4-Netzwerk** angenommen. UEFI, x86-64, SMP, eine GUI, Audio, WLAN und eine
vollständige POSIX-Kompatibilität sind sinnvolle spätere Ziele, aber keine
Voraussetzung für dieses erste belastbare Systemniveau.

Der Projektname enthält „Microkernel“, die aktuelle Architektur ist jedoch ein
modularer monolithischer Kernel: Scheduler, Speicherverwaltung, Dateisysteme,
Netzwerk und Treiber werden gemeinsam in `kernel.bin` gelinkt. Ring-3-Programme
sind isoliert, Hardware- und Dateisystemdienste laufen aber nicht als
Userspace-Server. Das ist kein unmittelbarer Fehler. Vor einem großen Umbau
muss entschieden werden, ob „Microkernel“ nur der Projektname bleibt oder ob
Nachrichten-IPC, Capabilities und Userspace-Server wirklich Projektziel sind.

## 2. Zusammenfassung

Das OS ist kein Minimalgerüst mehr. Es bootet nativ über BIOS/MBR, besitzt
Paging, Ring-3-Prozesse mit eigenen Seitentabellen, validierte User-Pointer,
präemptives Round-Robin-Scheduling, ein VFS, schreibbares FAT12/FAT32, lesbares
EXT2, mehrere Gerätetreiber, einen kleinen IPv4-Stack und einen gebauten
Userspace. Der geprüfte Windows-Referenzbuild ist erfolgreich. Phase 0 sowie
R1.1, R1.2 und R1.3 sind abgeschlossen. Die aktuelle Hosttest-Suite und
automatisierte Ring-3-Tests prüfen neben dem normalen, LAPIC-gesteuerten Betrieb
einen eigenen PIT-Scheduler-Fallback ohne LAPIC sowie Speicherkonfigurationen
mit 32, 64, 256, 512 und 1024 MiB.

Die beim ersten Audit belegten Korrektheitslücken in Prozess-Wait,
Exception-Frames und PRG-v1-Vertrag sind in Phase 0 behoben. R1.1 hat darauf
eine allgemeine **Blockier-/Ereignisgrundlage** aufgebaut: intrusive
Wait-Queues, atomaren Prozess-Wait, blockierendes Sleep und Console-Input,
`yield`, 64-Bit-Zeit sowie einen kalibrierten Scheduler-Timer. R1.2 trennt nun
erkannten von verwaltetem Speicher, erweitert den Kernel-Heap dynamisch,
schützt statische und dynamische Kernelstacks mit Canaries und räumt beendete
Tasks außerhalb langer IRQ-Sperrabschnitte auf. R1.3 definiert nun die
IRQ-, Präemptions-, Schlaf- und Lockverträge, serialisiert VFS und
ATA-/FDD-Zugriffe und verlagert Netzwerk- sowie HPET-Arbeit aus dem harten
IRQ-Kontext. Strukturierte Logs und vollständige Panic-Diagnosen schließen
den Meilenstein ab. Als nächstes folgt R2.1 mit einer gemeinsamen ABI-v1-Quelle
und vollständigen Dateideskriptoren.

## 3. Verifizierter Ist-Zustand

| Bereich | Vorhanden | Reifegrad |
|---|---|---|
| Boot | BIOS/MBR, zweistufiger Loader, E820, A20, ELF32-Prüfung, Kernel-CRC32, FAT12-Floppy | stabiler Referenzpfad |
| CPU | GDT/IDT/TSS, Ring 0/3, Exceptions, PIC, gegen PIT kalibrierter lokaler APIC-Timer, PIT-Scheduler-Fallback, `INT 0x80` | funktionsfähiger Single-Core-Pfad |
| Speicher | fail-closed normalisierte E820-Karte, 1-GiB-Directmap, Frame-Accounting, dynamischer Kernel-Heap, Stack-Canaries, getrennte Prozessadressräume, sichere User-Kopien | R1.2 abgenommen; Speicher oberhalb 1 GiB nur erkannt |
| Prozesse | Spawn mit `argc/argv`, Exit-Status, atomarer Wait, generische Wait-Queues, Sleep/Yield, Prozessliste, eigenes CWD | klein, maximal 8 Tasks |
| Dateien | VFS, Mounts, FAT12/FAT32 lesen und schreiben, EXT2 lesen | gute Basis, kleine ABI und keine atomaren Dateioperationen |
| Geräte | PCI, ATA-PIO, FDD-DMA, PS/2 und COM1 mit blockierendem Console-Wait, RTC, VGA, optionaler Framebuffer | Referenzhardware gut, moderne Geräte fehlen |
| Netzwerk | E1000, RTL8139, NE2000, Ethernet, ARP, IPv4, ICMP, DHCP, internes UDP-Senden | E1000/DHCP/Ping am besten verifiziert |
| USB | PCI-Erkennung eines xHCI-Controllers | nur Probe-Gerüst |
| Userspace | SDK, Shell, Editor, BASIC und zahlreiche Systemprogramme | brauchbare Demo-/CLI-Basis |
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

- generische Wait-Queues auf weitere Geräte- und Protokollereignisse anwenden
- Pipes, Prozessgruppen und ein kleines Signalmodell
- `waitpid(-1, ...)`, optionales nichtblockierendes Warten und saubere
  Reparenting-/Reaper-Semantik
- dynamische oder zumindest deutlich größere Tasktabelle statt `MAX_TASKS 8`
- Prioritäten erst nach korrekter Blockierung; Threads und SMP deutlich später
- echte Kernel-Stack-Guardpages zusätzlich zu den vorhandenen 64-Byte-Canaries
  und aussagekräftigere Prozessstatistiken

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
erhalten und meldet die vom Frame-Allocator verwalteten KiB.

- eine einzige gemeinsame, versionierte Quelle für Syscallnummern und
  Fehlercodes
- stabile `errno`-ähnliche Fehlersemantik; aktuell werden VFS-Fehler oft auf
  allgemeine Werte wie `-2`, `-5` oder `-9` reduziert
- `open`-Flags (`RDONLY`, `WRONLY`, `RDWR`, `CREATE`, `TRUNC`, `APPEND`)
- `lseek`, `fstat`, `truncate`, `rename`, `fsync`, später `dup`/`dup2`
- echte Deskriptoren 0/1/2 für Standard-Ein-/Ausgabe
- ABI-Fähigkeitsabfrage, damit ältere Programme kontrolliert weiterlaufen

### VFS, Dateisysteme und Blockgeräte

- atomisches `rename`; der Editor löscht beim Speichern derzeit zuerst die alte
  Datei (`userspace/bin/edit.c:250-258`) und kann sie bei einem Fehler verlieren
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

### Terminal und Shell

Die heutige Console-Eingabe vermeidet bereits Busy-Waiting für reguläre
Ring-3-Tasks: `getchar` prüft den Puffer und reiht den Task atomar auf der
gemeinsamen Input-Wait-Queue ein; PS/2 und COM1 wecken alle Leser zur erneuten
Prüfung der level-getriggerten Pufferbereitschaft.
Eine vollständige TTY-Schicht bleibt der nächste darüberliegende Ausbau.

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
- Editor: temporäre Datei plus atomisches Rename, dynamischer Puffer, Suche,
  Auswahl/Clipboard und Aufhebung des Limits von 200 Zeilen
- ein kleines Ring-3-`init` als PID 1 statt direktem Shellstart durch den Kernel

### Netzwerk

- ARP-Ablauf/Erneuerung, DHCP-Lease-Timer und Renew/Rebind
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
- aktuell darf jeder Prozess jeden anderen Prozess beenden; FAT kennt keine
  Berechtigungen
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
ABI/FD-Ausbau -> VFS rename/truncate/fsync -> sicherer Editor und Dateitools
Blockgeräte -> Partitionen + DMA -> AHCI/NVMe und USB-Massenspeicher
ACPI + DMA -> xHCI -> USB-Enumeration -> HID/Storage
R1.2-Directmap bis 1 GiB -> Highmem/kmap oberhalb 1 GiB -> später x86-64/SMP
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

1. Kindstatusprüfung und Registrierung des aktuellen Tasks als `TASK_WAITING`
   auf dem Single-Core-System in einem gemeinsamen IRQ-geschützten Abschnitt
   ausführen.
2. Kind-Exit, Kill und normales Exit über denselben Wakeup-Pfad führen.
3. Status genau einmal konsumieren; verwaiste Zombies kontrolliert aufräumen.
4. Einen deterministischen Test-Hook für einen Kind-Exit im bisherigen
   Race-Fenster sowie einen Gast-Stresstest mit vielen Spawn/Wait-Zyklen bauen.

**Fertig, wenn:** Kein Test hängt, jeder Exitstatus wird genau einmal geliefert
und der Elternprozess verbraucht während des Wartens keine CPU.

#### R0.2 Exception-Stubs vereinheitlichen — S

**Status (13. August 2026):** Umgesetzt. Alle Vektoren 0–31 verwenden die
Makros `ISR_NO_ERROR_CODE` oder `ISR_CPU_ERROR_CODE`; Vector 17 (`#AC`) nutzt
nun korrekt den CPU-Fehlercode. Compile-Time-Assertions sichern Offsets und
Größe des gemeinsamen `Registers`-Frames. Die vollständige Fehlercode-Matrix
ist hostseitig getestet.

1. Vektoren mit und ohne CPU-Fehlercode tabellarisch definieren und Stubs aus
   zwei Makros erzeugen.
2. Vector 17 korrigieren; 8, 10–14, 17, 21, 29 und 30 explizit prüfen.
3. Layout von Assembly-Frame und `Registers` mit statischen Offsets absichern.
4. Ring-3-Tests für Divide-by-zero, Invalid Opcode und Page Fault ergänzen;
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

1. Für Version 1 nur `base_address == USER_BASE` und
   `relocation_size == 0` akzeptieren.
2. Validator, Loader, Python-Builder und Dokumentation auf dieselben Regeln
   bringen; tote alternative ELF-Lader aus dem Laufzeitpfad entfernen.
3. Negative Tests für Relokationen, Überläufe, überlappende Bereiche und
   falsche Entry-Points ergänzen.
4. Anforderungen an ein späteres segmentbasiertes PRG v2 separat notieren.

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

1. QEMU mit serieller Konsole, festem Timeout und eindeutigem
   `BOOT_OK`/`TEST_OK`-Protokoll starten.
2. Einen Ring-3-Teststarter ins Image legen, der Userspace-Schutz,
   Spawn/Wait, Datei-I/O und Exceptions prüft; nach dem Erfolg beendet der
   Host QEMU kontrolliert.
3. Den Smoke-Test direkt an das erzeugte `build/x86-microkernel.img` binden;
   Legacy-Fixture-Tests getrennt halten.
4. Den Smoke-Test in CI ausführen und Logs als Artefakt sichern.

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

1. Generische Wait-Queues mit Wake-one/Wake-all einführen.
2. 64-Bit-monotone Zeit und geordnete Deadline-Liste implementieren.
3. `sleep_ms` und `yield` als Syscalls anbieten; `SYS_DELAY` kompatibel darauf
   abbilden.
4. Keyboard-, serielle und später Netzwerk-I/O auf blockierende Events
   vorbereiten.
5. APIC-Timer gegen PIT kalibrieren und bei fehlendem LAPIC einen
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

Der statische 8-KiB-Boot-/Rescue-Stack besitzt eine vom Assembler vor dem
ersten C-Aufruf initialisierte untere 64-Byte-Redzone. Dynamische 8-KiB-
Taskstacks besitzen je eine 64-Byte-Canary unter- und oberhalb. Scheduler-
Grenzen prüfen Wächter und ESP-Bereich; eine Verletzung führt kontrolliert zur
Panic. Beendete Tasks wechseln beim atomaren, owner- und
generationsvalidierten Detach in `TASK_REAPING`. Seitentabellen und Kernelstack
werden anschließend mit aktivierten Hardware-Interrupts, aber unterdrückter
Taskpräemption freigegeben; erst danach darf der Slot wiederverwendet werden.

1. Erkannte, verwaltete, reservierte und freie Frames separat zählen.
2. Framezugriff oberhalb 256 MiB durch ein konsistentes Directmap bis 1 GiB
   ermöglichen und die Grenze explizit ausweisen.
3. Kernel-Heap erweiterbar machen und belegte/freie Bytes exportieren.
4. Statische und dynamische Kernelstacks mit 64-Byte-Canaries und
   ESP-Bereichsprüfungen schützen.
5. Allokationsfehler, Fragmentierung und wiederholtes Prozess-Reaping testen.

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

Nicht Teil von R1.2 sind systematische Failure-Injection für jede
Teilallokation, ein Highmem-/`kmap`-Fenster oberhalb 1 GiB, echte nicht gemappte
Guardpages und ein IRQ-tauglicher Allocator; diese Punkte bleiben expliziter
Restumfang späterer Speicherhärtung und sind nicht Bestandteil von R1.3.

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

1. Festlegen, welche APIs in IRQ-Kontext, mit deaktivierter Präemption oder
   schlafend aufgerufen werden dürfen.
2. Lock-Reihenfolge für Scheduler, VFS, Dateisysteme und Treiber dokumentieren.
3. Assertions für IRQ-/Lock-Zustand sowie strukturierte Log-Level ergänzen.
4. Panic-Ausgabe um vollständige Register, CR2 und Build-ID erweitern.

### Phase 2 — Deskriptoren, VFS und zuverlässige Datenträger

#### R2.1 ABI v1 und vollständige Dateideskriptoren — L

1. Syscallnummern, Strukturen und Fehlercodes aus einem gemeinsamen ABI-Header
   für Kernel und SDK generieren bzw. teilen.
2. Open-Flags, Rechte je Handle und Standarddeskriptoren 0/1/2 ergänzen.
3. `lseek`, `fstat`, `truncate`, `rename` und `fsync` implementieren.
4. Teilzugriffe, EOF, ungültige Handles und Prozess-Exit vollständig testen.

#### R2.2 VFS- und FAT-Zuverlässigkeit — L

1. Atomisches Rename innerhalb eines Volumes zuerst für FAT32 umsetzen.
2. Editor auf `TEMP -> fsync -> rename` umstellen.
3. Open/Delete/Unmount-Regeln und Locking vereinheitlichen.
4. Fehler nach jedem einzelnen Sektorwrite injizieren und das resultierende
   Image mit einem Hostprüfer untersuchen.
5. Danach Zeitstempel und LFN ergänzen; EXT2 vorerst ausdrücklich read-only
   mounten.

#### R2.3 Blockgeräte und Partitionen — L

1. ATA und FDD hinter eine gemeinsame Blockgeräte-API legen.
2. MBR-Partitionen als Child-Geräte erzeugen und mehrere Partitionen mounten.
3. Bereichsprüfung und `flush` zentral erzwingen.
4. ATA LBA48 und gebündelte PIO-Transfers ergänzen.
5. GPT, AHCI und NVMe als getrennte Folgepakete behandeln.

### Phase 3 — Unix-artige CLI-Grundfunktionen

#### R3.1 Pipes, Signale und TTY — XL

1. Pipeobjekt mit blockierendem Ringpuffer auf Wait-Queues bauen.
2. `dup`/`dup2` und Deskriptorvererbung beim Spawn ergänzen.
3. Minimale Signale `SIGINT`, `SIGTERM`, `SIGKILL`, `SIGCHLD` implementieren.
4. Prozessgruppen, Vordergrundgruppe und TTY-Modi hinzufügen.
5. `waitpid` einschließlich `WNOHANG` bereitstellen.

#### R3.2 Userspace-Shell und Init — L

1. Parser für Quotes und Escapes erstellen.
2. `<`, `>`, `>>` und `|` auf Deskriptoren/Pipes abbilden.
3. Hintergrundjobs, Verlauf, Umgebungsvariablen und Exitcodes ergänzen.
4. Ein kleines `INIT.PRG` als Reaper und Starter der Shell einführen.

### Phase 4 — Netzwerk bis zu Anwendungen

#### R4.1 IPv4/UDP härten und Sockets einführen — L

1. ARP-Ablauf, DHCP-Lease/Renew/Rebind und ICMP-Fehler ergänzen.
2. Paketparser mit aufgezeichneten Frames, Grenzfällen und Fuzzing testen.
3. Socketobjekte in die FD-Schicht integrieren: `socket`, `bind`, `sendto`,
   `recvfrom`, `close` und Timeouts.
4. UDP-Echo zwischen Gast und Host als automatisierten Test betreiben.

#### R4.2 DNS — M

1. DNS-Namen sicher kodieren/dekodieren, Kompressionszeiger begrenzen.
2. A-Records, CNAME-Ketten, Timeouts und Caching implementieren.
3. Lokalen deterministischen Testserver statt öffentliches Internet verwenden.

#### R4.3 TCP — XL

1. Zustandsautomat und Connection Control Block erstellen.
2. Sequenz-/ACK-Prüfung, Retransmission, RTO und Empfangsfenster ergänzen.
3. Verbindungsaufbau, geordnete Daten, Reset und aktiven/passiven Close testen.
4. Erst danach einen kleinen HTTP-Client oder `NETCAT.PRG` bauen.

### Phase 5 — USB und Plattform

#### R5.1 ACPI- und DMA-Basis — L

1. RSDP/RSDT/XSDT mit Checksummen und Längengrenzen zentral parsen.
2. MADT und HPET aus dieser Schicht beziehen; Poweroff/Reboot ergänzen.
3. DMA-Puffer mit physischer Adresse, Alignment und Below-4-GiB-Grenze
   bereitstellen.

#### R5.2 xHCI in überprüfbaren Stufen — XL

1. Controller stoppen/resetten und Capability-/Operational-Register validieren.
2. DCBAA, Command Ring, Event Ring und Interrupter initialisieren.
3. Root-Port-Anschluss erkennen, Slot aktivieren und Control Transfers testen.
4. Deskriptoren lesen, Adresse und Konfiguration setzen.
5. Erst HID-Boot-Tastatur, dann Hub und Mass Storage implementieren.

Jede Stufe benötigt einen QEMU-xHCI-Test und darf bei unbekannter Hardware das
Gerät nur deaktivieren, nicht den Bootvorgang blockieren.

### Phase 6 — optionale Modernisierung

Erst nach den vorherigen Meilensteinen einzeln entscheiden:

- UEFI-Boot und GPT
- x86-64-Port mit neuem ABI
- SMP, IOAPIC/MSI und per-CPU-Daten
- AHCI/NVMe, USB-Massenspeicher und Hotplug
- IPv6
- Benutzer, Dateirechte, Capabilities und kryptografisch verifizierter Boot
- dynamischer Linker, Shared Libraries, Paketverwaltung
- Grafik-/Fenstersystem, Maus, Audio und WLAN

Diese Punkte sind groß genug für eigene Entwurfsdokumente und sollten nicht
nebenbei in die 32-Bit-Basis eingebaut werden.

## 8. Empfohlene Arbeitsreihenfolge

| Reihenfolge | Paket | Abhängigkeit | Größe |
|---:|---|---|:---:|
| 1 | R0.1 Wait/Wakeup (erledigt) | keine | S |
| 2 | R0.2 Exception-Frames (erledigt) | keine | S |
| 3 | R0.3 PRG-v1-Vertrag (erledigt) | keine | S |
| 4 | R0.4 Gast-Smoke-Test (erledigt) | 1–3 für Regressionen | M |
| 5 | R1.1 Wait-Queues/Sleep/Zeit (erledigt) | R0.1 | L |
| 6 | R1.2 Speicherverwaltung (erledigt) | R0.4 | L |
| 7 | R1.3 Synchronisation/Diagnose (erledigt) | R1.1 | M |
| 8 | R2.1 ABI und FDs | R1.1 | L |
| 9 | R2.2 VFS/FAT-Zuverlässigkeit | R2.1 | L |
| 10 | R2.3 Blockgeräte/Partitionen | R1.3 | L |
| 11 | R3.1 Pipes/Signale/TTY | R1.1, R2.1 | XL |
| 12 | R3.2 Shell/Init | R3.1 | L |
| 13 | R4.1 UDP-Sockets | R1.1, R2.1 | L |
| 14 | R4.2 DNS | R4.1 | M |
| 15 | R4.3 TCP | R4.1 | XL |
| 16 | R5.1 ACPI/DMA | R1.2, R1.3 | L |
| 17 | R5.2 xHCI/USB | R5.1 | XL |
| 18 | Phase-6-Entscheidung | stabile vorherige Meilensteine | XL |

R2.2 und R2.3 können nach Fertigstellung der gemeinsamen
Synchronisationsregeln parallel bearbeitet werden. R4 und R5 können ebenfalls
parallel laufen, sobald Wait-Queues, FD- und DMA-Grundlagen stabil sind.

## 9. Definition of Done für jedes Paket

Ein Paket gilt nur dann als fertig, wenn alle folgenden Punkte erfüllt sind:

- öffentliche API, Fehlerfälle und Nebenläufigkeitsregeln sind dokumentiert
- positive, negative und mindestens ein Ressourcenfehler-Test existieren
- Hosttests laufen erfolgreich
- Windows-Referenzbuild läuft erfolgreich
- betroffene Laufzeitfunktion wird im automatisierten Gast geprüft
- kein Test beweist eine Laufzeiteigenschaft ausschließlich durch Quelltextsuche
- relevante QEMU-, VMware- oder Hardwarematrix ist dokumentiert
- alte Stubs, widersprüchliche Dokumentation und tote Pfade sind entfernt oder
  ausdrücklich als nicht unterstützt markiert

Aktuelle Referenzbefehle:

```powershell
make test
.\scripts\build-windows.ps1 -Target qemu -RunTests
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

Phase 0 sowie **R1.1 Wait-Queues, Sleep und Yield**, **R1.2 Speicherverwaltung
und Schutz** und **R1.3 Synchronisations- und Diagnosevertrag** sind umgesetzt
und abgenommen. Als nächstes folgt **R2.1 ABI v1 und vollständige
Dateideskriptoren**: gemeinsame ABI-Header, stabile Fehlercodes und Open-Flags,
Standarddeskriptoren 0/1/2 sowie `lseek`, `fstat`, `truncate`, `rename` und
`fsync`.

Systematische Allocation-Failure-Injection, ein IRQ-tauglicher Allocator,
weitere Reaper-Stresstests, Highmem/`kmap` und echte nicht gemappte Guardpages
bleiben ausdrücklich spätere Speicherhärtung und gehören nicht zu R2.1.
