# Projektstatus

Stand: 24. August 2026. Maßgeblich sind ausführbarer Code, die Tests und die
aktive Paketqueue in `automation/reist-s03b.toml`.

REIST OS ist ein nicht zertifizierter High-Assurance-Forschungsprototyp. Die
vorhandenen Schutzmechanismen dürfen nicht als klinische, industrielle oder
sonstige sicherheitsbezogene Freigabe verstanden werden.

## Arbeitscheckpoint 16. August 2026

Dieser historische Hardware-Checkpoint basiert auf Commit `0a2c08e`. Das reale
SATA-Hotplug-Szenario wurde auf Zielhardware erfolgreich durchgeführt:

- `SATAWR.PRG` schrieb synchronisierte Testdaten, während die System-HDD
  abgezogen und wieder angeschlossen wurde.
- Der Storage-Service erkannte den I/O-Fehler, quarantänisierte die
  AHCI-Elternressource und setzte Systemvolume und Treiber fail-closed auf
  read-only.
- Nach Reconnect liefen begrenzter AHCI-COMRESET, IDENTIFY, frische
  Medienidentitätsprüfung und Undo-Journal-Recovery erfolgreich durch.
- Eine verwaiste Schreiboperation wird erst nach erfolgreicher
  Journal-Recovery ressourcengebunden beendet. Die Supervisor-IDLE-Meldung ist
  idempotent, wodurch Storage- und Filesystem-Fences wieder freigegeben werden.
- Die reale Ausgabe erreichte `RESOURCE_REINTEGRATED_RW 0`; das Volume wurde
  wieder beschreibbar und der Anwender bestätigte den erfolgreichen Lauf.
- `DRIVES.PRG` übernimmt für Partitionen den Zustand der Blockgeräte-
  Elternressource und zeigt `READY`, `READONLY`, `DEGRADED`, `QUARANTINED`,
  `RECOVERING`, `OFFLINE` oder `UNKNOWN`.

Zugehörige Commits sind `fe53ff3`, `ad89fde`, `bf6d95b`, `f55a024` und
`0a2c08e`. Das zuletzt erzeugte reale Hardware-Image ist
`build/reist-os.img` mit Build-ID
`D531CB4F2886278DC31059E36BC0B91B1BCFC74B`; normaler SATA-QEMU-Gasttest und
Hosttests waren erfolgreich. Der nächste Arbeitstag setzt bei der aktiven
Paketqueue in `automation/reist-s03b.toml` fort. Der reale Hotplug-Lauf ist
positive Hardwareevidenz für diesen getesteten Aufbau, aber keine allgemeine
SATA-Hardwarefreigabe.

## Verifizierter Systempfad

- eigener BIOS-/MBR-Bootloader mit Manifest-v3-, ELF32-, SHA-256- und
  RSA-2048-PSS-Prüfung; ein unabhängiges Imagegate validiert das signierte
  Kernelartefakt zusätzlich; native HDD-Images besitzen feste Kandidaten A/B
- 32-Bit-i386-Kernel mit Paging, Ring-3-Prozessen, präemptivem Scheduler,
  endlichen Waits und versionierter Syscall-/MYPR-ABI
- inkrementeller Windows-Build für `qemu`, `vmware` und `real_hw`
- QEMU-Regressionspfad über ATA/IDE sowie expliziter AHCI/SATA-Gastlauf
- generiertes VMware-Paket mit persistenter SATA-VMDK an `sata0:0`
- physischer BIOS-Boot über SATA sowie reale PS/2-Eingabe wurden auf
  Zielhardware beobachtet; die Hardwarematrix bleibt klein
- deterministische Rootauswahl: BIOS-Bootdiskette oder genau eine strukturell
  gültige FAT32-Partition mit Label `X86 SYSTEM`
- VFS mit FAT12, FAT32 und EXT2 sowie DOS-artiger Ring-3-Shell
- E1000, RTL8139, RTL8168/8111G und NE2000 hinter einer gemeinsamen
  Netzgeräteschicht
- VGA-Text als Standard und optionaler VBE-/VMware-SVGA-II-Framebuffer mit
  Ring-3-Desktop; der SVGA-II-Bootselbsttest gibt die Anzeige vor der Shell
  zurück und der Desktop deaktiviert sie beim Sitzungsende über denselben
  generationgebundenen Treiberkanal

Der SATA-Pfad leitet partition-relative Zugriffe anhand des Elterntransports an
AHCI statt an den ATA-PIO-Kompatibilitätspfad weiter. Der vollständige
QEMU-SATA-Gastlauf erreicht `FILE_IO_OK` und `TEST_OK`; zusätzlich wurde die
Abzieh-/Reconnect-Recovery des oben genannten Builds auf einer realen
Zielmaschine erfolgreich beobachtet. Weitere Zielmaschinen bleiben jeweils
eine eigene Hardwareabnahme.

## REIST-Ausbaustand

Die High-Assurance-Arbeit folgt dem Ablauf Detect, Contain, Recover, Validate
und Reintegrate. Bereits vorhanden sind unter anderem:

- begrenzte monotone Deadlines für IPC, Treiber- und Dienstoperationen
- geschützte, redundante kritische Steuerobjekte mit Integritätsprüfung
- überwachte Ring-3-Dienste mit generationgebundener Revocation und Recovery
- Crash-, Hang- und ungültige-Antwort-Proben für die Ring-3-Domäne
- fail-closed Storage-Quarantäne, Requalifizierung und Schreib-Fencing
- persistente Crashrecords und vorbereitete Supervisor-/Handover-Protokolle
- begrenzte Netzwerkparser, ARP-/IPv4-/ICMP-/UDP-/DHCP-Entscheidungen in der
  überwachten Ring-3-Domäne

Das automatisierte S0-Forschungsgate ist für die generische
`REIST-research`-Baseline auf QEMU i386 und VMware i386 abgeschlossen. S0.1
ist für diese explizit abgegrenzte Baseline abgeschlossen: Ein separates maschinenlesbares
Scope-Inventar bindet Systemgrenze, Essential Functions, Anforderungen,
Komponenten und Profilausschlüsse an das Gefahrenregister. Schema v2 prüft
vollständige Komponentenabdeckung und die SHA-256-Traceability reicht von
Anforderung über Design und Code bis Test und Ergebnis. Dieser Abschluss ist
keine Zertifizierung und keine Freigabe der ausgeschlossenen Referenzprofile
oder unqualifizierter Zielhardware.

S0.2 ist für die automatisierte QEMU/VMware-Forschungsbaseline abgeschlossen.
QEMU prüft den emulierten IB700-Watchdog; VMware bootet das frisch erzeugte
disponible Build-Paket unter festen Fristen und verlangt fail-closed das fehlende externe
Backend, überwachte Probe-Recovery, `BOOT_OK` und die Ring-3-Shell. Das
maschinenlesbare physische Profil bleibt `unbound` und kann nur mit eindeutig
gebundener Ziel-/Monitor-/Firmwareidentität, separater Strom- und Zeitbasis,
unabhängigem Reset, latched Safe-State-Ausgang, elektrischem Sense-Readback
und gehashten physischen Fault-Injection-Berichten auf `qualified` wechseln.
Diese Realhardwareprüfung führt der Benutzer manuell durch; es entsteht kein
automatischer Zielhardware- oder Fail-operational-Claim. Das physische und
produktbezogene Gesamtgate bleibt deshalb offen.

`S0.3c-layout1` mit kleingeschriebener,
hierarchischer Systemprogrammablage ist umgesetzt. `S0.3c-admin2` mit statischer
Komponenten-Lifecycle-Steuerung und `S0.3c-admin1` mit capability-
gebundener Storage-Administration, `S0.3c-6f5` mit der FAT12-
Persistenz-Fehlermatrix und `S0.3c-hw11` mit begrenzter SATA-Hotplug-Recovery
sind abgeschlossen. Aussagen über vollständig nachgewiesene
Fail-Operationalität oder unabhängige Hardware-Failover-Domänen sind weiterhin
unzulässig.

Die ausführbare Paketqueue für S0.3c ist nach dem FAT12-Persistenzabschluss
abgearbeitet. Für S0.4 ist nun auch die feste, saturierende Scheduler-/INT-80-
Zeitdiagnostik samt maschinenlesbaren QEMU-/VMware-Regressionsgrenzen
umgesetzt. Sie ist ausdrücklich keine Zielhardware-WCET; diese Abnahme führt
der Benutzer manuell durch. Die automatisierte Abnahme vom 23. August 2026
bestand auf QEMU (maximal rund 0,613 ms Scheduler und 0,102 ms INT 0x80) sowie
VMware (rund 0,051 ms und 0,034 ms), jeweils mit null Zeitquellenanomalien und
deutlich unter der festen 10-ms-Grenze. Auf diesem Workstation-Host verwendet
die Automation den vom generierten Paket vorgesehenen GUI-Start, weil VIX den
Headless-Start mit `Unknown error` ablehnt; Markerprüfung und harter Stopp
bleiben automatisiert und begrenzt. Danach folgen S0.5 und S0.6. Ein externes
Monitorgerät samt Transport, eigener
Versorgung/Zeitbasis, Reset- und Interlockverdrahtung wird erst nach einer
manuellen Auswahl angebunden; ohne diese Identität wird kein Produktionstreiber
erfunden und keine Hardwarequalifikation behauptet.

`S0.4c-2b2c` ergänzt die Laufzeitdiagnostik des kernel-eigenen mediated-DMA-
Pools. Eine append-only 32-Byte-Struktur meldet aktive und maximale Belegung
der vier 64-KiB-Pools sowie saturierende echte Kapazitätsablehnungen, ohne
physische Adressen oder Pooltokens offenzulegen. Hostseitig sind vollständige
Erschöpfung, `ENOSPC`, generationgebundene Freigabe, Wiederverwendung und die
Rückkehr auf null aktive Pools geprüft. Der QEMU-HDA-Treiber bestätigt seine
eigene gebundene Poolbelegung über einen maschinenlesbaren Marker im bereits
autorisierten, generationsgebundenen Supervisor-Diagnosekanal.

`S0.4c-3` begrenzt nun IRQ-Stürme pro aktiver Device-Domain auf 128
Aufnahmen in 100 ms. Die erste Überschreitung sowie eine rückwärts laufende
Device-Zeit fencen vor einer weiteren Ring-3-Benachrichtigung über den
vorhandenen vollständigen Mask-/Bus-Master-/DMA-Cleanup-Pfad. Eine Regression
der Scheduler-Abrechnungszeit verriegelt alle Ring-3-Klassen auch über spätere
Fensterwechsel hinweg; nur explizite Neuinitialisierung löscht den Fehler.
Die Grenzen stehen im Ressourcenregister, die Zähler saturieren, und ein
separater Compilezeit-QEMU-Build weist beide Guards vor `BOOT_OK` sowie
anschließenden normalen Ring-3-Fortschritt bis `TEST_OK` nach. Daraus folgt
keine Zielhardware-WCET- oder Hardwarequalifikationsaussage.

S0.5 umfasst nun die abgeschlossenen Pakete `S0.5a1`, `S0.5a2`, `S0.5a3a`,
`S0.5a3b`, `S0.5b1`, `S0.5b2`, `S0.5b3`, `S0.5b4` und `S0.5b5`. Die letzten fünf Pakete ergänzen
die redundante Bootstufe, deren transaktionalen Pending-Zustand und das
Ring-3-Erfolgs-Acknowledge. Das native
BIOS-Manifest v3 enthält SHA-256 und die
256-Byte-RSA-PSS-Signatur des exakten Kernelartefakts; Windows-
und Makefile-Builds validieren HDD- und Floppy-Images mit einem unabhängigen
Parser und brechen bei Versions-, Layout-, Bounds-, Prüfsummen- oder
Digestfehlern ab. Stage 2 berechnet SHA-256 und CRC32 mit festen Puffern in
einem begrenzten Kernel-Lesedurchlauf und stoppt einen Digestfehler vor dem
ELF-Parsing. Der negative QEMU-Nachweis hält CRC32 und Manifest-Prüfsumme trotz
Kerneländerung gültig und erreicht ausschließlich den SHA-Fehlerpfad.

`S0.5a3a` ergänzt eine hostseitige Kernelsignatur nach RFC 8017 mit
RSA-2048-PSS/SHA-256, MGF1-SHA-256 und 32-Byte-Salt. Windows- und Makefile-
Builds erzeugen eine feste 256-Byte-Signatur und prüfen sie unabhängig gegen
eine versionierte Policy sowie den gepinnten SHA-256-Fingerprint des Public
Keys, bevor das Image veröffentlicht wird. Die private Research-Testfixture
ist öffentlich und wird im Release-Modus abgelehnt. `S0.5a3b` bettet die
Signatur in Manifest v3 ein und prüft sie in Stage 2 mit festem Modulus,
Exponent 65537 und begrenzter RFC-8017-PSS-/MGF1-SHA-256-Logik vor dem
ELF-Parsing. Damit ist der Kernel relativ zur Stage-2-Vertrauensgrenze
authentifiziert. Stage 1 und Stage 2 bleiben auf dem beschreibbaren Medium
ersetzbar; Secure Boot, Anti-Rollback und ein unveränderlicher Plattformanker
werden weiterhin nicht behauptet.

`S0.5b1` legt im nativen HDD-Image Manifest A/B an den
partitionrelativen LBAs 0/96 und Kernel A/B an 128/3136 ab. Die 446-Byte-
MBR-Stufe lädt ohne Manifestparser die feste Stage-2-Reserve. Stage 2 startet
mit A und prüft B nach einem A-Fehler genau einmal vollständig und unabhängig.
Der Hostvalidator verlangt beide Kandidaten. Persistente Slotwahl,
Bootversuchszähler, Erfolgsbestätigung und atomare Updateumschaltung bleiben
offen; die Rescue-Diskette bleibt single-slot.

`S0.5b2` ergänzt zwei Boot-Control-Sektoren an den partitionrelativen LBAs
97/98 und einen Offline-Updater. Der Updater prüft den signierten ELF-Kernel,
schreibt ausschließlich Slot B und veröffentlicht Pending B erst nach
vollständiger Revalidierung. Stage 2 dekrementiert zwei Testboots persistent
vor der B-Ausführung und schreibt bei Erschöpfung oder B-Fehler Rollback auf A
zuerst. Die Kopien sind CRC-/sequenzgeschützt und werden ältere zuerst mit
Read-back aktualisiert.

`S0.5b3` ergänzt den CRC-geschützten Stage-2-Handoff an `0x4E00`. Der Kernel
kopiert ihn vor Allocator-Nutzung, löst genau eine MBR-`0xDA`-Bootpartition auf
und gibt append-only Syscall 117 erst nach `BOOT_OK` an die gebundene
Storage-Service-Generation frei. Der Ring-3-Dienst revalidiert Manifest,
Sequenz und beide Records, bestätigt B mit älterer Kopie, Flush und Read-back
zuerst und heilt benachbarte Kopien idempotent. Bestätigtes B startet direkt;
eine später ungültige B-Signatur führt erst nach persistentem Control-Commit
zurück zu A. QEMU deckt Bestätigung, Neustart und diesen Rollback ab.
Updateverteilung, unveränderliches Recovery und Anti-Rollback bleiben offen.

`S0.5b4` führt Boot-Control v2 append-only ein. v1 bleibt strikt auf
bestätigt A mit Pending B begrenzt; v2 darf nur den jeweils gegenüberliegenden
inaktiven Slot wählen. Der Offline-Updater schreibt und verifiziert Kernel und
Manifest von A oder B vollständig, bevor die ältere Control-Kopie Pending-
Autorität erhält. Stage 2 persistiert Dekrement und dynamischen Rollback vor
der Kandidatenausführung. Der unveränderte 64-Byte-Handoff bleibt read-only;
der generationsgebundene Ring-3-Storage-Dienst bestätigt nur `selected ==
pending != active` und validiert dabei das tatsächlich ausgewählte Manifest.
Hostseitige Power-Loss-Matrizen decken beide Richtungen ab; der persistente
QEMU-Lauf bestätigt B, aktualisiert danach A, bestätigt A und erhält den
bestehenden Rollback eines beschädigten bestätigten B.

`S0.5b5` ergänzt die hostseitige Offline-Verteilung als festes binäres
REIST-Update-Bundle v1. Sein 512-Byte-Header bindet exakte Gesamt-/Kernelgröße,
RSA-PSS/SHA-256-Algorithmus, Kernel-Digest, 256-Byte-Signatur und den lokal
gepinnten SPKI-Fingerprint; Flags und Reserven müssen null sein, CRC32 erkennt
Transportkorruption. Der Producer prüft ELF, Policy und Signatur vor atomarem
Publish. Ein strukturell unabhängiger Consumer begrenzt das gesamte Bundle auf
die feste Slotkapazität, verwirft Truncation und Nachlaufdaten und authentifiziert
erneut, bevor der bestehende A/B-Updater ein Output-Image erzeugen darf. Der
persistente QEMU-Lauf nutzt dasselbe Bundle für A nach B und B nach A.
Online-Verteilung, TUF-/Uptane-Metadaten, unveränderliches Recovery,
Release-Key-Verwahrung und Anti-Rollback bleiben ausdrücklich offen.

`S0.3c-admin1` stellt sichere Storage-Operationen (`device down/up`, `mount`,
`umount`) und einen festen, integritätsgeprüften 272-KiB-RAM-Rescue-Pool mit
112 KiB Einzelgrenze aus
Shell, Anzeige-, Diagnose-, Dienst- und Adminprogrammen bereit. `S0.3c-admin2`
ergänzt eine statische, abhängigkeitsbewusste Lifecycle-Steuerung für
ausdrücklich unterstützte Treiber und überwachte Dienste. Der reale QEMU-Lauf
weist geschützte Kernkomponenten, Abhängigkeitsreihenfolge sowie Down/Up und
Restart der unterstützten Komponenten nach. Ein universelles dynamisches
Entladen von Kernel-Treibern ist nicht vorgesehen.

## Storage und Dateisysteme

### Blockgeräte

- ATA-PIO unterstützt Legacy- und begrenzt erkannte PCI-IDE-Kanäle.
- AHCI erkennt PCI-Klasse `01/06/01`, validiert BAR5 und verwendet feste,
  adressgeprüfte DMA-Strukturen mit endlichen Deadlines.
- MBR-Partitionen werden als eigene Child-Ressourcen veröffentlicht.
- ATA, AHCI, Partitionen und FDD verwenden die gemeinsame Blockgeräteschicht.
- Der Storage-Service vermittelt generationgebundene Requests, Quarantäne,
  Requalifizierung, Flush, Schreib-Fencing sowie FAT12-/FAT32-Formatierung.
- Der erste VFS-Migrationspfad transportiert `stat` als exakt 512 Byte großen,
  voll-duplexen Shadow-Frame zum Storage-Service. Der normale QEMU-Gast
  vergleicht Typ, Größe und Zeitfelder mit dem weiterhin autoritativen Kernel-
  VFS und publiziert `STORAGE_VFS_SHADOW_STAT_OK`. Der Dienst erhält dafür nur
  `SYS_STAT`.
- Der zweite Shadowmodus parst den längsten Mountpräfix, vollständig geprüfte
  FAT12-/FAT32-BPBs, ASCII-8.3-/VFAT-Namen, die feste FAT12-Rootdirectory und
  begrenzte Verzeichniscluster im
  Ring-3-Storage-Service selbst. Maximal 64 vermittelte Sektorreads und feste
  Stackpuffer begrenzen die Arbeit. Nur eine bytegenaue Übereinstimmung mit dem
  Legacy-`SYS_STAT` wird publiziert; der QEMU-Gast markiert dies mit
  `STORAGE_VFS_FAT32_PARSER_OK`. Für bestehende Clients bleibt der Kernel
  autoritativ.
- `STAT.PRG` ist der erste kontrolliert umgestellte Client. Sein separater,
  heapfreier Adapter normalisiert relative, absolute und DOS-Pfade, verwendet
  ausschließlich die append-only autoritative FAT-Parseroperation 4, wartet mit monotoner
  Deadline und
  validiert den vollständigen Antwortframe ohne Legacy-Fallback. Der normale
  QEMU-Gast startet das paketierte Programm auf `/GUEST.TMP` und markiert den
  Erfolg mit `STORAGE_VFS_STAT_CLIENT_OK`. Andere Clients bleiben bis zu einer
  getrennten Umstellung am Kernel-VFS.
- Append-only Syscall 118 und `x86os_storage_cancel` widerrufen genau ein
  owner- und generationsgebundenes Requesthandle. Queued und vollständige
  Requests geben ihren Slot sofort frei. Bereits geclaimte Requests bleiben
  bis zur Quittierung der gebundenen Dienstgeneration `cancel-pending`, belegen
  die Statistik weiterhin und publizieren weder Status noch Daten. Der normale
  Gast markiert den ABI-Nachweis mit `STORAGE_REQUEST_CANCEL_OK`. Dies ist kein
  physischer I/O-Abbruch und kein Rollbackvertrag.
- Operation 2 bleibt ABI-kompatibel FAT32-spezifisch. Operation 3 ergänzt
  FAT12 mit standardisierter Clusterzahl-Typauswahl, 12-Bit-FAT-Einträgen und
  fester Rootdirectory; FAT16 und EXT2 werden abgewiesen. Der QEMU-FDD-Test
  führt nach echter Medienreintegrierung das paketierte `STAT.PRG` auf
  `/mnt/fdd0/HOTPLUG.TXT` aus und prüft Name, Größe und Shell-Rückkehr. Die
  erkannte 80x2x18-Geometrie wird dabei als feste Grenze von 2880 Sektoren an
  Ring 3 publiziert.
- Append-only Operation 4 macht denselben begrenzten FAT12-/FAT32-Parser zum
  autoritativen read-only `stat`-Ergebnisweg. Sie ruft `SYS_STAT` nicht auf und
  besitzt keinen Legacy-Fallback; Operationen 1 bis 3 bleiben unverändert. Der
  normale QEMU-Gast vergleicht Operation 4 außerhalb dieses Produktionspfads
  bytegenau mit Legacy-`stat` und markiert den Erfolg mit
  `STORAGE_VFS_FAT_STAT_AUTHORITY_OK`.
- Append-only Operation 5 ergänzt einen unabhängigen, heapfreien EXT2-Parser
  und ist der autoritative generische FAT12/FAT32/EXT2-`stat`-Pfad. Der
  unterstützte EXT2-Subset umfasst Revision 0/1, 1--4-KiB-Blöcke, lineare
  Directories sowie direkte und einfach-indirekte Directory-Blöcke unter 128
  Sektorreads. HTree, Extents, Symlinks und 64-Bit-Größen bleiben fail-closed.
  Der QEMU-Nachweis hängt eine deterministische zweite IDE-Platte ein und führt
  das paketierte `STAT.PRG` auf `/mnt/hdd1/readme.txt` aus.
- Append-only Operationen 6 und 7 liefern autoritatives, pfadbasiertes
  `read-at` mit höchstens 256 Byte beziehungsweise genau einen indexierten
  Verzeichniseintrag. FAT12/FAT32 und EXT2 nutzen nur vermittelte Sektorreads
  mit festen Parsergrenzen; Fehler veröffentlichen keine Teilbytes. `CAT.PRG`
  und `LS.PRG` besitzen in diesem Pfad keinen Kernel-VFS-Fallback. Der normale
  Gast prüft den FAT-Pfad, der zweite QEMU-IDE-Datenträger `stat`, `cat`, `ls`
  und die jeweilige Rückkehr zur Userspace-Shell.
- Ein fester prozesslokaler Read-only-Sessionlayer verwaltet vier
  generationcodierte Slots mit beim Öffnen kanonisiertem Pfad, 32-Bit-Offset,
  `read`, `SEEK_SET`/`SEEK_CUR`/`SEEK_END`, `fstat` und `close`. Fehler ändern
  den Offset nicht; stale Handles bleiben ungültig und Generationen laufen
  nicht über. Das ist bewusst keine stabile Inode- oder POSIX-Deskriptor-
  Identität und besitzt keine Vererbung.
- `HTTPD.PRG` nutzt für `/htdocs` ausschließlich Operation 5, die Sessions
  über Operation 6 und Listings über Operation 7. Der QEMU-Modus `http-server`
  führt zwölf abwechselnde echte Datei- und Verzeichnisanfragen aus, verlangt
  die Ring-3-Marker und gewinnt nach `Ctrl+C` die Userspace-Shell zurück.
- Der feste Rescue-Programmpool umfasst nun 272 KiB für weiterhin genau elf
  geschützte Programme; die Einzelgrenze beträgt 112 KiB. Das schafft begrenzten
  Raum für den isolierten Parser, ohne dynamische Cacheallokation einzuführen.
- Der Windows-Build wertet die Exitcodes der System- und Beispielprogramm-
  Builder über explizite Child-Prozesse aus. Ein fehlgeschlagener PRG-Build kann
  daher kein scheinbar erfolgreiches Image aus veralteten Artefakten erzeugen.
- Aktiv ist nun die append-only Claim-v2-Mediation der vom Kernel geschützten
  Client- und Servicegeneration. Sie ist die fehlende Besitzergrenze vor
  stabilen serviceeigenen VFS-Objekt-Handles; Claim v1 bleibt unverändert.
- `FDISK.PRG` erzeugt auf leeren, ungeschützten ATA-/AHCI-Medien eine
  ausgerichtete und rückgelesene MBR-Partition und veröffentlicht sie ohne
  Neustart. Root- und bereits partitionierte Medien bleiben geschützt.

### FAT32

Das erzeugte Image besitzt eine Systempartition mit Label `X86 SYSTEM`.
Markierte REIST-Images verwenden ein redundantes Undo-Journal. Datei-I/O,
Verzeichnisse, `fsync`, Same-Directory-Rename und Replace sind angebunden. Der
Editor speichert über Tempdatei, `fsync`, Close und Rename. Fremde FAT32-Medien
bleiben kompatibel lesbar, sind ohne gültigen REIST-Journalmarker jedoch
read-only. Jede Mutation prüft vor dem ersten Sektorwrite die exakte
Journalbindung an Gerät, Partition und Volumegrenze erneut; eine durch ein
anderes Mount verdrängte globale Bindung wird sicher neu aufgebaut.

VFAT Long File Names sind für druckbares ASCII bis 255 Zeichen implementiert.
LFN-Slotfolge, Reihenfolge und 8.3-Prüfsumme werden vor Veröffentlichung
validiert; ungültige oder nicht unterstützte Unicode-Folgen fallen auf den
checksum-gebundenen 8.3-Alias zurück. Create, Lookup, `readdir`, Datei-I/O,
Delete, lange Verzeichnispfade und Same-Directory-Rename sind abgedeckt. Ein
LFN-Replace auf ein bereits bestehendes Ziel bleibt fail-closed unsupported.

`FORMAT.PRG` unterstützt auf einer veröffentlichten, nicht gemounteten
Partition zwei explizit bestätigte Modi:

```text
format --reist-fat32 --quick <resource-id> --confirm
format --reist-fat32 --full  <resource-id> --confirm
```

Quickformat invalidiert zuerst den alten Bootsektor, leert beide FAT-Kopien in
begrenzten Chunks und veröffentlicht erst danach BPB, FSInfo, Root und das
redundante REIST-Journal. Fullformat prüft danach jeden Datencluster durch
wiederholtes Schreiben und Readback. Reproduzierbar isolierte Defekte werden
in beiden FATs als `0x0FFFFFF7` markiert; Kontroll- oder Transportfehler
quarantänisieren das Medium.

### Userspace-Dateisystemwerkzeuge und Zeitstempel

Die erste Linux-artige Werkzeuggruppe ist als Ring-3-Programm in der festen
Systemhierarchie verfügbar: `/bin/rename.prg`, `/bin/stat.prg`,
`/bin/df.prg`, `/bin/touch.prg`, `/bin/tree.prg`, `/bin/find.prg` und
`/bin/rm.prg`. `ren` und `mv` aliasieren `rename`, `cp` aliasiert `copy`.
`tree`, `find` und `rm --recursive` verwenden feste Grenzen von 16 Ebenen und
512 besuchten Einträgen; Root-Pfade werden von `rm` nicht akzeptiert.

`vfs_dir_entry_t` und die Userspace-Dateiinformation liefern
`create_time`, `modify_time` und `access_time` als Sekunden seit
1970-01-01. FAT12 und FAT32 übersetzen ihre Directory-Felder über den
gemeinsamen Konverter `fs/vfs/vfs_time.h`; `x86os_touch()` verwendet den
append-only Syscall 108, um mtime und atime zu aktualisieren. FAT begrenzt
mtime auf zwei Sekunden und atime auf ein Datum ohne Uhrzeit. EXT2 liefert
seine vorhandenen Inode-Zeiten, bleibt für `touch` jedoch read-only.

### FAT12

Für explizit markierte REIST-FAT12-Medien sind umgesetzt:

- zentrale Schreibzulassung nach erfolgreicher Journal-, Remap- und
  Replikatvalidierung; fremde FAT12-Medien bleiben lesbar, aber VFS-, FAT- und
  Sektormutationen werden vor der ersten Zustandsänderung abgewiesen
- verifiziertes redundantes Undo-Journal und Recovery vor Metadatennutzung
- begrenzte Defektbestätigung, `0xFF7`-Markierung und redundante Remaptabelle
- persistente Replikate für die feste Liste kritischer 8.3-Dateien
- geordnete Dateiänderungen: Daten, beide FATs, Verzeichniseintrag,
  Replikatpublikation und abschließendes Journal-`CLEAN`
- transaktionale Neuerzeugung durch `FORMAT.PRG` und den Storage-Service
- capability-gebundene BPB-/Spiegel- und Clusterkettenanalyse sowie bestätigte,
  journalisierte Reparatur einer eindeutig beschädigten FAT-Kopie,
  überlanger regulärer Dateiketten und eindeutig kurzer EOC-Dateien sowie
  bestätigtes Freigeben vollständig unerreichbarer Nicht-Bad-Allokationen und
  reine, ausreichend lange Schleifen regulärer Dateiketten sowie reine
  Rücksprungschleifen in vollständig gescannten Unterverzeichnissen und
  zugleich kurze reguläre Dateischleifen mit atomarer Größenbegrenzung sowie
  Crosslinks, die ausschließlich aus überlangen Dateitails entstehen, und
  unzulässige Größenfelder ansonsten gültiger Unterverzeichnisse sowie
  reservierte Nichtnull-Felder ansonsten gültiger Volume-Label-Einträge und
  eindeutig besessene Restallokationen regulärer Dateien der Größe null sowie
  positive Größen regulärer Dateien ohne Startcluster und unzulässige Größen
  korrekt verknüpfter `.`-/`..`-Einträge sowie falsche niedrige Clusterfelder
  ansonsten gültiger Dot-Beziehungen sowie reine mehrfach benötigte
  reguläre Dateiketten durch vollständig verifiziertes Klonen späterer Dateien
  und Same-Parent-Aliase strikt leerer einclusteriger Unterverzeichnisse
- versionierte Prüfung von Journal v2 und Remap v1 sowie bestätigtes,
  readback-verifiziertes Remapping von höchstens acht FAT-/Root-
  Metadatensektoren; unbekannte Versionen, unklare Daten und erschöpfte Spares
  setzen die Ressource fail-closed read-only

`FORMAT.PRG` akzeptiert ausschließlich eine veröffentlichte FDD-Ressource:

```text
DRIVES
FORMAT --reist-fat12 <resource-id> --confirm
```

`CHKDSK.PRG [pfad]` führt einen begrenzten read-only VFS-Scan aus. Die
FAT12-Modi `--repair`, `--repair-chains`, `--repair-short`,
`--reclaim-orphans`, `--repair-loops`, `--repair-dir-loops` und
`--repair-short-loops`, `--repair-crosslinks`, `--repair-dir-size` und
`--repair-volume-label`, `--repair-zero-files`, `--repair-zero-start` sowie
`--repair-dot-size`, `--repair-dot-cluster` und
`--repair-required-crosslinks`, `--repair-directory-crosslinks`,
`--repair-directory-topology`, `--salvage-orphans` sowie
`--record-bad-sector <sektor>`
benötigen jeweils `--confirm`, laufen ausschließlich im Storage-Dienst unter
Maintenance-Lease und melden Erfolg erst nach Undo-Journal, Readback und
sauberem Vollscan. Der Reclaim-Modus verwirft unerreichbare Inhalte
ausdrücklich. Der getrennte Salvage-Modus veröffentlicht vollständig gültige
Orphan-Ketten unter `FOUND.000` als `FILEnnnn.CHK`; `nnnn` hält den
ursprünglichen Startcluster fest. Reine Pflicht-Crosslinks regulärer
Dateien werden durch vollständige Kopien in höchstens 48 freie Cluster
getrennt. Same-Parent-Aliase strikt leerer einclusteriger Unterverzeichnisse
werden ebenfalls verifiziert kopiert und umgebunden. Der gebündelte
Topologiepfad entfernt darüber hinaus eindeutig attribuierbare nichtleere,
mehrclusterige, Same-Parent- und parentübergreifende Alias-Einträge samt streng
gebundenen VFAT-LFN-Slots, ohne die gemeinsame Kette oder FAT zu verändern;
mehrdeutige Parentbeziehungen, Teilketten und gemischte Datei-/Directory-Fälle
bleiben gesperrt.
`FDISK.PRG --create <resource-id> <mbr-type> --confirm` richtet ein leeres,
ungeschütztes ATA-/AHCI-Medium ein. Nicht eindeutig attribuierbare
Verzeichnisschäden und die reale Hardware-Power-Loss-Matrix bleiben offen.

### EXT2

EXT2 ist über VFS lesend und in den explizit unterstützten Operationen
verwendbar. Es besitzt kein REIST-Persistenzjournal und darf nach unklarer
Schreibunterbrechung nicht automatisch als wieder schreibsicher gelten.

## Shell und Systemprogramme

`/bin/shell.prg` ist der reguläre Ring-3-Command-Interpreter; die Kernel-Shell ist
nur Rettungskonsole. DOS-Laufwerksbuchstaben, kanonische VFS-Pfade,
laufwerksbezogene Arbeitsverzeichnisse, `PATH`, Verlauf und Tab-Vervollständigung
sind implementiert. Der flüchtige Verlauf ist als fester Ring mit 32 Einträgen
ausgeführt; Cursor-Up/Down navigiert darin und stellt hinter dem neuesten
Eintrag den begonnenen Eingabeentwurf wieder her. Die feste Standardsuche ist
`/bin`, `/sbin`, `/usr/bin`, `/usr/gui/bin`; interne Dienste liegen unter
`/libexec/reist`.
FAT12 und FAT32 speichern die Hierarchie begrenzt und zeigen ihre kanonischen
Namen kleingeschrieben an; FAT32 erhält dabei validierte lange Namen. Exakte alte Root-Pfade bleiben über eine feste
Kompatibilitätstabelle nutzbar. `/sbin/drives.prg` zeigt Resource-ID, Laufwerksbuchstaben,
Gerätenamen, Typ und den von der Elternressource geerbten Recovery-Zustand.

Die Buildliste enthält unter anderem `/libexec/reist/reist.prg`,
`/libexec/reist/storage.prg`, `/bin/shell.prg`, `/sbin/drives.prg`,
`/sbin/chkdsk.prg`, `/sbin/fdisk.prg`, `/sbin/format.prg`, `/bin/basic.prg`,
`/bin/edit.prg`, `/bin/rename.prg`, `/bin/stat.prg`, `/bin/df.prg`,
`/bin/touch.prg`, `/bin/tree.prg`, `/bin/find.prg` und `/bin/rm.prg`.
`/bin/basic.prg` ist ein
normales Ring-3-Programm, keine Kernelkomponente.

Grafische Programme sind getrennt unter `/usr/gui/bin`: Der Desktop ist der
Session-Compositor, Notepad und Image Viewer sind eigenständige
Ring-3-Surface-Clients, Sound Player und Control Gallery verwenden derzeit
noch die Vollbild-Kompatibilitätsbrücke. `/etc/reist/filetypes.conf` ordnet
Text-, WAV-, BMP- und GIF-Dateien ihren Anwendungen zu.

## Eingabe, Diagnose und Panic

Der i8042-Treiber arbeitet mit rohem Scan-Set 2, IRQ1 und einem begrenzten
Polling-Fallback. NumLock und die Tastatur-LEDs werden vom Treiber verwaltet.
Die frühere per COM1 injizierte Tastatureingabe ist entfernt; COM1 dient nur
der begrenzten Diagnoseausgabe. Der Panic-Screen zeigt Phase, Komponente,
Operation, Subjekt, Ergebnis, Details, Sequenz, Panic-Aufrufadresse, Build-ID
und – falls vorhanden – den Registerrahmen.

USB/xHCI unterstützt begrenzt HID-Boot-Tastatur und -Maus; PS/2 bleibt der
unabhängige Fallback. Allgemeine USB-Unterstützung und das
AULA/BY-Tech-Composite-Keyboard `258A:010C` bleiben offen. Verifizierte
Evidenz und VMware-Sicherheitsgrenze stehen in
[USB-Design](../hardware/USB_DESIGN.md) und [VMware](../hardware/VMWARE.md).

## Grafik, Audio und Medien

- Laufzeitgrafik, Explorer und Surface-Compositor sind umgesetzt; Notepad und
  Image Viewer laufen als echte externe Fensterclients. Details und offene
  Migrationen stehen ausschließlich im
  [Desktop-Workflow](GRAPHICAL_DESKTOP_WINDOW_MANAGER_WORKFLOW.md), in der
  [Framebuffer-Referenz](../features/FRAMEBUFFER.md) und im
  [Image-Vertrag](../architecture/IMAGE_SUBSYSTEM.md).
- VMware SVGA-II `15ad:0405`/`15ad:0710` besitzt einen überwachten Ring-3-
  2D-Treiber mit festem Kernelmediator. QEMU und VMware Workstation bestätigen
  `RECT_COPY`, Treiber-READY und `BOOT_OK`; der Compositor behält bei jeder
  Ablehnung den CPU-/Shadow-Framebuffer-Pfad. DMA, GMR, 3D und beliebige FIFO-
  oder BAR-Autorität sind nicht Bestandteil des Profils. Details stehen im
  [Videovertrag](../architecture/VIDEO_SUBSYSTEM.md).
- PCI-HDA läuft über getrennte überwachte Ring-3-Domänen; QEMU prüft den
  PCM-Pfad, VMware-Wiedergabe und Pegel wurden manuell bestätigt. Format,
  Lifecycle und Hardwaregrenzen stehen im
  [Audiovertrag](../architecture/AUDIO_SUBSYSTEM.md).

## Netzwerk

Der überwachte Ring-3-Dienst `REIST.PRG` übernimmt validierte
Netzwerkentscheidungen. Vorhanden sind Ethernet, ARP, IPv4, ICMP, DHCP,
prozessgebundene UDP-/TCP-FD-Sockets, DNS-A/CNAME-Auflösung, aktives und passives
TCP mit `listen`/`accept` sowie ein begrenzter HTTP/1.0-Dateiserver mit
Directory-Listing. Der neue RTL8168/8111G-Treiber bindet
die H81M-K-PCI-ID `10EC:8168` über MMIO, feste TX-/RX-DMA-Ringe und denselben
Netdev-Fence-Vertrag ein. Link und DHCP wurden auf dem physischen H81M-K
beobachtet; der ARP-/ICMP-Retest des aktuellen Builds bleibt dort noch offen.
Die QEMU-Referenz emuliert den Chip nicht. Die Socket-, DNS-, TCP- und HTTP-Pfade
sind hostseitig sowie deterministisch in QEMU getestet: DHCP und ARP, aktiver
TCP-Handshake/Daten/Close (`pong`), DNS-A über UDP (`test.reist` auf
`10.0.2.77`) und drei aufeinanderfolgende passive HTTP-Verbindungen mit
`/htdocs`-Directory-Listing laufen im selben Standard-Serverprozess; erst der
anschließend injizierte `Strg+C`-Abbruch führt zurück zur Shell und zu
`guest-smoke: PASS`. Begrenzte Einzel-Timeouts und fehlerhafte Clients beenden
den Listener nicht. Nicht vorhanden sind IPv6, TLS/HTTPS oder SMB.

## Verifikation

Der Referenznachweis besteht aus mehreren Ebenen:

```powershell
python -m unittest discover -s test -p "test_*.py" -v
.\scripts\test-reist-package.ps1 -Target qemu -Video vga
.\scripts\test-reist-runtime.ps1 -Mode normal
python .\scripts\run_qemu_smoke.py --image build\reist-os.img --sata --expect-reist-probe
```

Paketabhängig kommen FDD-Hotplug, PS/2, Netzwerkparser, Storage-Recovery,
Fault-Injection, Framebuffer/Surface, PCI-Audio, Watchdog und Handover hinzu.
Ein grüner Host- oder QEMU-Test ersetzt keine Langzeit-, EMV-, Stromausfall-
oder breite Zielhardwarequalifikation.

`S0.6a` führt die erste fest begrenzte Update-Parser-Kampagne aus. Mit einem
expliziten 32-Bit-Seed werden 16 strukturierte und 48 Einbitfehler gegen den
echten Offline-Bundle-Consumer und Inactive-Slot-Einstieg geprüft. Alle 64
Fälle scheitern vor einem Output-Image; Quellimage, signierter Kernel und
Signatur bleiben SHA-256-identisch. Die Kampagne ist deterministisch und auf
höchstens 128 Fälle begrenzt. Sie ersetzt weder Coverage-gesteuertes Fuzzing
noch Soak-, VMware- oder Hardwareevidenz.

`S0.6b` ergänzt beide nativen Image-Builds um ein begrenztes SPDX-2.3-JSON-
SBOM. Es erfasst Kernel, detached Signatur, BIOS-Image und die nichtrekursiv
paketierten Ring-3-Programme mit exakter Größe im SPDX-Kommentarfeld sowie
SHA-1 und SHA-256. Ein strukturell unabhängiger Validator prüft Pfadgrenzen,
Eindeutigkeit, Kapazitäten,
Dokumentstruktur, Beziehungen und jedes aktuelle Artefakt erneut; der QEMU-
Paket-Gate verlangt das Ergebnis. Die Grenzen sind 160 Dateien, 128 MiB je
Datei, 512 MiB Gesamteingang und 2 MiB Dokumentgröße. `NOASSERTION` markiert
weiter ungeklärte Lizenz- und Copyrightdaten. Reproduzierbarkeit, vollständige
Quellen-/Abhängigkeitsabdeckung, Lizenzfreigabe, Vulnerability-Analyse und
signierte Provenienz bleiben offen.

`S0.6c` bindet den Abschluss der automatisierten Forschungsbaseline an den
versionierten Vertrag `safety/automated_s0_gate.toml`. Der unabhängige
Validator akzeptiert ausschließlich `REIST-research`, QEMU i386 und VMware
i386 sowie die feste Host-, Paket- und Laufzeitmatrix. Am 23. August 2026
bestanden 1001 Hosttests, beide frischen Referenzpakete, QEMU-PIT, Watchdog,
Storage-Recovery, vier Speichergrößen, Framebuffer und das begrenzte VMware-
Containment. Der Status lautet bewusst `automated-emulator-complete`.
Zielhardware-WCET, externe Monitor-/Fence-Hardware, physische Fault-Injection,
reproduzierbare Builds, signierte Provenienz, Langzeit-Soak, Online-Verteilung,
Anti-Rollback, unveränderliche Recovery, Produktionsschlüssel und
Zertifizierung bleiben offene manuelle oder produktbezogene Nachweise.

## Wichtigste offene Grenzen

- `CHKDSK.PRG` besitzt capability-gebundene, bestätigte und journalisierte
  Reparaturpfade für genau eine eindeutig beschädigte FAT12-Spiegelkopie und
  eindeutig überlange reguläre Dateiketten. Bei eindeutig kurzen, normal
  EOC-terminierten Dateien kann es außerdem die Directory-Größe auf die
  lesbare Kettenkapazität begrenzen und reine unerreichbare Allokationen
  explizit verwerfen. Ausreichend lange reine reguläre Dateiloops werden am
  Sollende getrennt; reine Directory-Rücksprünge werden nach vollständigem
  eindeutigen Inhaltsscan beendet. Kombinierte Short-Loops begrenzen zusätzlich
  atomar die Directory-Größe; reine Excess-Tail-Crosslinks werden ohne Änderung
  der einzigen Sollkette getrennt; reine ungültige Unterverzeichnisgrößen
  werden nach vollständigem Inhaltsscan nullgesetzt. Reservierte Startcluster-
  und Größenfelder ansonsten gültiger Volume-Label-Einträge werden ebenfalls
  ausschließlich auf null normalisiert. Eindeutig besessene, normal
  terminierte Restketten von Nullgrößendateien können bestätigt freigegeben
  und ihre Startcluster nullgesetzt werden. Positive Größen ohne Startcluster
  werden bei reiner Short-Diagnose auf null begrenzt. Reine mehrfach benötigte
  reguläre Dateiketten können vollständig in freie Cluster kopiert und so
  getrennt werden. Reine Same-Parent-Crosslinks strikt leerer einclusteriger
  Unterverzeichnisse lassen sich ebenso kopieren und umbinden. Eindeutig
  attribuierbare nichtleere, mehrclusterige und parentübergreifende
  Directory-Aliase werden ohne Kettenänderung auf genau einen kanonischen
  Parent reduziert. Vollständig gültige Orphan-Ketten lassen sich unter
  `FOUND.000` retten. Die feste Journal-/Remap-/Defektkartenprüfung und der
  QEMU-Maintenance-/Remountnachweis sind automatisiert; mehrdeutige
  Verzeichnisreparaturen bleiben gesperrt
- die reale FAT12-Power-Loss-/Reconnect-Matrix auf Zielhardware bleibt eine
  manuelle Benutzerabnahme; VMware-Reconnect ist automatisiert, ersetzt diese
  Hardwareevidenz aber nicht
- journalisiertes Schreiben für EXT2 und weitere Backends; fremde FAT12- und
  FAT32-Medien sind bis zu einem nachgewiesenen Vertrag bewusst read-only
- unabhängige Supervisor-, Fence- und Failover-Hardware
- breite reale AHCI-/PCI-IDE-/PS/2-/BIOS-Kompatibilitätsmatrix
- allgemeiner USB/xHCI-, Composite-HID-, Mass-Storage- und Hotplug-Lebenszyklus
- Migration der verbleibenden GUI-Programme auf Surface-Clients sowie
  allgemeine 3D-/Multi-Monitor-Grafikbeschleunigung
- SMP, IOMMU/DMA-Isolation, UEFI, Secure Boot und NVMe
- formale Nachweise, Langzeit-Stresstests und Zertifizierung
