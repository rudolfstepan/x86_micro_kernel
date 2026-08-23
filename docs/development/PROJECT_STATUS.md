# Projektstatus

Stand: 23. August 2026. Maßgeblich sind ausführbarer Code, die Tests und die
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

- eigener BIOS-/MBR-Bootloader mit Manifest-, ELF32- und CRC32-Prüfung
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
- VGA-Text als Standard und optionaler VBE-Framebuffer mit Ring-3-Desktop

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

S0 ist noch nicht abgeschlossen. S0.1 ist für die explizit abgegrenzte
`REIST-research`-Baseline abgeschlossen: Ein separates maschinenlesbares
Scope-Inventar bindet Systemgrenze, Essential Functions, Anforderungen,
Komponenten und Profilausschlüsse an das Gefahrenregister. Schema v2 prüft
vollständige Komponentenabdeckung und die SHA-256-Traceability reicht von
Anforderung über Design und Code bis Test und Ergebnis. Dieser Abschluss ist
keine Zertifizierung und keine Freigabe der ausgeschlossenen Referenzprofile
oder unqualifizierter Zielhardware.

`S0.3c-layout1` mit kleingeschriebener,
hierarchischer Systemprogrammablage ist umgesetzt. `S0.3c-admin2` mit statischer
Komponenten-Lifecycle-Steuerung und `S0.3c-admin1` mit capability-
gebundener Storage-Administration, `S0.3c-6f5` mit der FAT12-
Persistenz-Fehlermatrix und `S0.3c-hw11` mit begrenzter SATA-Hotplug-Recovery
sind abgeschlossen. Aussagen über vollständig nachgewiesene
Fail-Operationalität oder unabhängige Hardware-Failover-Domänen sind weiterhin
unzulässig.

Die Queue besitzt derzeit kein aktives Paket (`active_id = ""`). Die direkt
umgesetzten Meilensteine `R1.5-runtime-desktop`,
`R1.6-driver-domain-foundation` und `R1.7-pci-audio` sind als `done`
eingetragen. Ein neuer autonomer Paketlauf benötigt deshalb zuerst einen
explizit definierten, begrenzten Scope.

`S0.3c-admin1` stellt sichere Storage-Operationen (`device down/up`, `mount`,
`umount`) und einen festen, integritätsgeprüften 128-KiB-RAM-Rescue-Pool aus
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
- `FDISK.PRG` erzeugt auf leeren, ungeschützten ATA-/AHCI-Medien eine
  ausgerichtete und rückgelesene MBR-Partition und veröffentlicht sie ohne
  Neustart. Root- und bereits partitionierte Medien bleiben geschützt.

### FAT32

Das erzeugte Image besitzt eine Systempartition mit Label `X86 SYSTEM`.
Markierte REIST-Images verwenden ein redundantes Undo-Journal. Datei-I/O,
Verzeichnisse, `fsync`, Same-Directory-Rename und Replace sind angebunden. Der
Editor speichert über Tempdatei, `fsync`, Close und Rename. Fremde FAT32-Medien
erhalten nicht automatisch dieselbe Journalgarantie.

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

- verifiziertes redundantes Undo-Journal und Recovery vor Metadatennutzung
- begrenzte Defektbestätigung, `0xFF7`-Markierung und redundante Remaptabelle
- persistente Replikate für die feste Liste kritischer 8.3-Dateien
- geordnete Dateiänderungen: Daten, beide FATs, Verzeichniseintrag,
  Replikatpublikation und abschließendes Journal-`CLEAN`
- transaktionale Neuerzeugung durch `FORMAT.PRG` und den Storage-Service

`FORMAT.PRG` akzeptiert ausschließlich eine veröffentlichte FDD-Ressource:

```text
DRIVES
FORMAT --reist-fat12 <resource-id> --confirm
```

`CHKDSK.PRG [pfad]` führt einen begrenzten read-only VFS-Scan aus.
`FDISK.PRG --create <resource-id> <mbr-type> --confirm` richtet ein leeres,
ungeschütztes ATA-/AHCI-Medium ein. Kontrollierte CHKDSK-Reparatur und die
reale Hardware-Power-Loss-Matrix bleiben offen.

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

## Wichtigste offene Grenzen

- reale FAT12-Power-Loss-/Reconnect-Matrix und kontrollierter
  Reparatur-/Remountpfad
- medienunabhängige Persistenzgarantie für EXT2 und fremde FAT-Volumes
- unabhängige Supervisor-, Fence- und Failover-Hardware
- breite reale AHCI-/PCI-IDE-/PS/2-/BIOS-Kompatibilitätsmatrix
- allgemeiner USB/xHCI-, Composite-HID-, Mass-Storage- und Hotplug-Lebenszyklus
- Migration der verbleibenden GUI-Programme auf Surface-Clients sowie
  hardwarebeschleunigte Grafik
- SMP, IOMMU/DMA-Isolation, UEFI, Secure Boot und NVMe
- formale Nachweise, Langzeit-Stresstests und Zertifizierung
