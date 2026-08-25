# VFS-Architektur

Stand: 25. August 2026.

VFS ist die einzige reguläre Dateisystemschnittstelle für Shell,
Programmlader und Ring-3-Datei-ABI. Direkte globale FAT-Sonderpfade gehören
nicht zum aktuellen Design.

```text
Ring-3-Programm / Shell
          |---------------- read-only metadata/content shadow ------------|
          |                                                               v
      Syscall-/FD-Schicht                                      Storage-Service
          |                                                     (Ring 3)
          VFS                                          FAT12/FAT32/VFAT-Parser
       /   |   \                                                   |
   FAT32 FAT12 EXT2                                      mediated Block Read
          |                                                       |
  Blockgerät / Partition
       /    |    \
   ATA-PIO AHCI  FDD
```

Der Shadowzweig ist der erste Migrationsnachweis, noch nicht der Zielpfad. Ein
normaler Ring-3-Testclient übergibt einen absoluten Pfad in einem exakt 512 Byte
großen, versionierten Frame über den bestehenden Storage-Request-Pool. Dessen
Primär-/Schattenkopie ist CRC-geschützt und an Client- sowie Dienstgeneration
gebunden. Operation 1 benutzt weiterhin ausschließlich die read-only
Legacy-Brücke `SYS_STAT`. Die append-only Operation 2 bleibt der eingefrorene
FAT32-Kompatibilitätspfad. Operation 3 löst Mountpräfix, FAT12- oder FAT32-BPB,
ASCII-8.3-/VFAT-Namen, die feste FAT12-Rootdirectory beziehungsweise
Verzeichniscluster und Metadaten selbst im Storage-Service auf. FAT16 wird
anhand des standardisierten Clusterzahlbereichs abgewiesen. Der Parser darf
höchstens 22 Ressourcen, 32 Pfadkomponenten, 128 Verzeichniscluster und 320
vermittelte Sektorreads untersuchen und verwendet keinen Heap. Sein Ergebnis
wird nur veröffentlicht, wenn Status und sämtliche öffentlichen Metadatenbytes
exakt mit `SYS_STAT` übereinstimmen; eine Abweichung liefert den
Integritätsfehler `-84`. Die append-only Operation 4 verwendet denselben
begrenzten FAT12-/FAT32-Parser autoritativ und ruft weder `SYS_STAT` auf noch
fällt sie darauf zurück. Die Operationen 1 bis 3 behalten ihre bisherige
Semantik. Append-only Operation 5 ist der autoritative generische
Dateisystempfad: Sie verwendet ausschließlich die unabhängigen FAT- und
EXT2-Parser und besitzt ebenfalls keinen `SYS_STAT`-Aufruf oder Fallback.
Operation 6 liest ab einem expliziten 32-Bit-Offset höchstens 256 Byte einer
regulären Datei; Operation 7 liefert genau einen Eintrag anhand eines
32-Bit-Index und blendet `.` sowie `..` aus. Beide verwenden ausschließlich
die FAT12/FAT32- und EXT2-Parser, veröffentlichen bei Fehlern keine Teilwerte
und rufen weder `SYS_OPEN`, `SYS_READ`, `SYS_READDIR` noch `SYS_STAT` auf.
Ein darüberliegender prozesslokaler Vier-Slot-Sessionlayer hält kanonischen
Pfad, Offset, Deadline und Slotgeneration. Er bietet read-only `open`, `read`,
`SEEK_SET`/`SEEK_CUR`/`SEEK_END`, `fstat` und `close`; stale Handles werden
abgewiesen und bei Generationserschöpfung wird der Slot stillgelegt. Weil jede
Operation den Pfad erneut auflöst, ist dies keine stabile Inode-Identität und
keine POSIX-Binärkompatibilität. Mutationen, Vererbung, Controllerzugriff und
DMA bleiben verboten.

Damit besitzt Ring 3 echte FAT12-/FAT32-Parsersemantik. Als erster
kontrollierter Cutover verwendet das kurzlebige `STAT.PRG` inzwischen
ausschließlich Operation 5 und
übernimmt deren Ergebnis ohne Rückfall auf `SYS_STAT`. Der feste Clientadapter
normalisiert relative, absolute und DOS-Pfade, validiert den vollständigen
Antwortframe und wartet höchstens bis zu einer monotonen Deadline. Bei Timeout
oder Protokollfehler beendet sich das Programm; die Prozessbereinigung widerruft
den generationsgebundenen Request. `CAT.PRG`, `LS.PRG`, `FIND.PRG` und
`TREE.PRG` sind zusätzlich auf Operation 6/7 umgestellt. Auch der
compositorinterne Desktop-Explorer validiert Verzeichnisse mit Operation 5 und
bezieht Einträge samt Leer/Voll-Ordnerprobe ausschließlich über Operation 7;
die Userspace-Shell verwendet dieselben Operationen für Programmsuche und
Tab-Vervollständigung. Andere Clients und der Kernelpfad bleiben unverändert.
Append-only Syscall 118 stellt inzwischen
eine requestbezogene Cancel-ABI bereit: queued und vollständige Requests werden
sofort widerrufen; bereits vom Dienst übernommene Requests bleiben bis zu dessen
Quittierung `cancel-pending` und können kein Ergebnis mehr publizieren. Das ist
ein Widerruf der Ergebnisautorität, kein physischer I/O-Abbruch oder Rollback.
EXT2-`stat`, Lesen in `CAT.PRG`, Verzeichnisiteration in `LS.PRG` und die
Read-only-Sessions sind damit migriert. Append-only Frameoperationen 8 bis 11
stellen `open`, objektbezogenes `read`, `fstat` und `close` bereit. Der Pfad
wird nur beim Öffnen aufgelöst. Danach adressiert FAT den validierten
Directory-Entry über Sektor/Offset, Startcluster, Erzeugungsschutz und
Bootrecord-Signatur; EXT2 adressiert Inode-Nummer und Inodegeneration unter
einer Superblock-Signatur. Medien- oder Locatorwechsel liefert `ESTALE`.
Read-only Rechte und eine generationgebundene, abschwächende Übergabe sind
über Operationen 12 bis 14 ergänzt. Operation 8 bleibt der kompatible READ-/
SEEK-/STAT-Open; DELEGATE wird nur explizit vergeben. Das Ziel übernimmt einen
eigenen Slot innerhalb von fünf monotonen Sekunden. Deskriptorvererbung beim
Spawn bleibt davon getrennt und ist noch nicht migriert. Das Storage-Rescue-
Image bleibt im festen Gesamtpool. Mit den vollständigen Unicode-15-Tabellen
beträgt die feste Einzelgrenze nun 192 KiB; der weiterhin statische
Allowlist-Gesamtpool ist auf 352 KiB begrenzt.

Append-only Frameoperation 15 und Storage-Operation 32 ergänzen einen
objektbezogenen Bulk-Lesepfad. Der 512-Byte-Kontrollframe bleibt im
redundanten Request-Pool; die Nutzdaten liegen in genau zwei kernel-eigenen,
statischen Slots mit höchstens 64 KiB. Append-only Syscall 124 erlaubt nur der
gebundenen Servicegeneration die Publikation und nur der exakten
Clientgeneration die atomare Abholung. Kernel- und Frame-CRC werden vor
Offsetfortschritt geprüft. Timeout, Cancel, Prozessende und Serviceverlust
widerrufen Slot und Handle; Erschöpfung liefert `ENOSPC`. Die große Kopie läuft
mit erlaubten Interrupts, aber ohne Schedulerwechsel, und hält den IRQ-Lock nur
für kleine Metadatenübergänge. Der bisherige 256-Byte-Pfad bleibt unverändert.
FAT ist dafür auf 128 Cluster und 320 Sektorreads, EXT2 auf 192 Sektorreads pro
Operation fest begrenzt.

Append-only Syscall 119 ergänzt nun einen getrennten, exakt 40 Byte großen
Claim-v2-Deskriptor. Nur die gebundene Storage-Servicegeneration erhält daraus
Client-PID, Clientgeneration und ihre eigene Servicegeneration direkt aus den
geschützten Request-Metadaten. Der Service gleicht beide Generationen vor der
Dispatchwirkung mit dem Prozess-Lifecycle ab; ein bereits beendeter Client
verliert seine Ergebnisautorität. Syscall 68 und sein Claim-v1-Deskriptor
bleiben Version 1 und exakt 28 Byte groß.

`HTTPD.PRG` ist der erste vollständig umgestellte lang laufende Client dieser
read-only ABI. Metadaten, Datei-Sessions und Verzeichnisiteration verwenden
ausschließlich Operationen 5 bis 7. Zwölf QEMU-HTTP-Transaktionen wechseln
zwischen Dateiinhalt und Listing; der Server bleibt bis `Ctrl+C` aktiv und
kehrt danach zur Shell zurück. Die read-only Shell-Walker `find` und `tree`
verwenden ebenfalls ausschließlich Operationen 5 und 7. Ihre vorhandenen
256-Byte-Pfad-, 16-Level- und 512-Node-Grenzen werden durch eine absolute
monotone Fünf-Sekunden-Gesamtdeadline ergänzt; jeder Einzelrequest erhält nur
die Restzeit, höchstens eine Sekunde. Der Desktop-Explorer veröffentlicht je
Snapshot höchstens 32 sichtbare Einträge, scannt höchstens 128 und verwendet
ebenfalls eine absolute monotone Fünf-Sekunden-Deadline mit einsekündigen
Restbudgets. Fehler verändern das bereits veröffentlichte Fenster und seine
Generation nicht. Mutierende Shell-/Papierkorbpfade sowie große Desktop-
Ressourcenstreams bleiben unverändert. Programmsuche und Completion der Shell
teilen pro Aktion eine absolute monotone Fünf-Sekunden-Deadline, höchstens
einsekündige Requests und eine feste Grenze von 128 akzeptierten Einträgen.
Fehler oder Kapazitätserschöpfung publizieren kein Teilergebnis in die
Eingabezeile; der geschützte Resident-Fallback bleibt davon unabhängig. Der FAT12-Nachweis
erfolgt separat mit dem paketierten `STAT.PRG` auf einer realen
QEMU-Hotplug-Diskette. Der FDD-Ressourceneintrag publiziert dazu seine bereits
erkannte CHS-Geometrie als 2880 LBA-Sektoren; der vermittelte Blockread prüft
weiterhin jede angeforderte LBA gegen diese feste Grenze.

## Operationen

Die Adapter stellen die jeweils unterstützten Varianten von `open`, `close`,
`read`, `write`, `seek`, `stat`, `readdir`, `create`, `mkdir`, `unlink`,
`rmdir`, `truncate`, `fsync`, `rename` und `replace` bereit. Nicht unterstützte
Operationen liefern einen eindeutigen Fehler und dürfen keine Teilwirkung
veröffentlichen.

Der prozesslokale Legacy-Deskriptorpfad besitzt zusätzlich append-only Syscall
120 und `x86os_open_flags()`. Seine POSIX-nahen Werte sind `RDONLY=0`,
`WRONLY=1`, `RDWR=2`, `CREAT=0x40`, `TRUNC=0x200` und `APPEND=0x400`.
Unbekannte Bits, der reservierte Zugriffsmodus 3 und `APPEND` ohne Schreibrecht
liefern vor Wirkung `EINVAL`. Ein voller fester Deskriptorpool liefert vor
einem möglichen `CREAT` `EMFILE`. `TRUNC` verlangt Schreibrechte und führt vor
Descriptorpublikation die interne node-basierte `truncate(..., 0)`-Operation
aus. Journalmarkierte REIST-FAT12-/FAT32-Adapter implementieren diesen
Nullschnitt als Spezialfall der allgemeinen Größenoperation. FAT12 schreibt
beide FAT-Kopien und den Directory-Eintrag in einer Undo-Transaktion, FAT32
trennt den Directory-Eintrag vor der Kettenfreigabe. EXT2,
fremde/read-only FAT-Medien und kritische FAT12-Replikate liefern vor Erfolg
einen Fehler. Alte `open`-/`create`-Syscalls und die serviceeigenen read-only
Objekt-Handles werden dadurch nicht verändert.

Append-only Syscalls 121 und 122 stellen denselben Prozessdeskriptoren
`lseek` und `fstat` bereit. `SEEK_SET=0`, `SEEK_CUR=1` und `SEEK_END=2`
entsprechen der üblichen POSIX-Terminologie; gerechnet wird mit signierten
64-Bit-Zwischenwerten und veröffentlicht wird nur ein nichtnegativer,
darstellbarer 32-Bit-Offset. Positionen hinter EOF sind erlaubt. Ein Fehler
belässt den alten Offset, gültige Terminal-/Socket-Deskriptoren liefern
`ESPIPE`. `SEEK_END` und `fstat` verwenden die node-basierte
`vfs_fstat()`-Operation ohne Pfadauflösung. FAT12 revalidiert den beim Öffnen
gebundenen Directory-Slot, FAT32 aktualisiert seine gehaltene
Directory-Identität und EXT2 liest den gehaltenen Inode erneut. Die Ausgabe
behält das bestehende feste `x86os_file_info_t`-Layout; ungültige
Userspacebereiche werden vor Dateisystemarbeit abgewiesen.

Append-only Syscall 123 stellt `x86os_ftruncate(fd, size)` für schreibbare
reguläre Deskriptoren bereit und verändert den aktuellen Offset nicht. Die
node-basierte VFS-Operation akzeptiert das vollständige `uint32_t`-Ziel,
delegiert aber nur an einen expliziten Adapter. FAT12 prüft Mediengröße,
Clusterbedarf und den vollständigen festen Undo-Journalumfang vor der ersten
Wirkung; zu große Transaktionen liefern `ENOSPC`. Erweiterungen werden vor
Directorypublikation vollständig genullt, Schrumpfungen trennen nur den
Suffix. FAT32 hält bei Erweiterungen die alte Größe sichtbar, bis Daten und
neuer Kettensuffix bereitstehen. Beim Schrumpfen publiziert es zuerst die
kleinere Größe und gibt danach ausschließlich den privaten Suffix frei. Ein
Fehler bei dieser nachgelagerten Rückgewinnung liefert `EIO` und aktiviert den
VFS-Schreibzaun, ohne veraltete Node-Metadaten wiederherzustellen. EXT2 liefert
`EROFS`; 64-Bit-Größen und Sparse-Extents sind nicht Teil dieser ABI.

Der feste Metadatenvertrag veröffentlicht `create_time`, `modify_time` und
`access_time` als Unix-Sekunden, ohne das ABI zu erweitern. FAT12 und FAT32
validieren Kalenderfelder vor der Konvertierung; ungültige Werte werden null.
Neue Directory-Einträge erhalten Create-, Write- und date-only Access-Felder
vor ihrer Publikation. Write und Truncate aktualisieren mtime in demselben
journalgeschützten Directory-Write wie Größe und Clusteridentität. Explizites
`touch` bewahrt Create-Felder, Inhalt, Größe und Identität und setzt mtime sowie
date-only atime. Read, `stat`, `fstat` und `readdir` bleiben ohne Medienwirkung,
insbesondere auf fremden oder read-only FAT-Medien. FAT-Zeit ist lokal und
ohne spezifizierte Zeitzone; mtime hat Zwei-Sekunden-, atime Tagesauflösung.

Offene Nicht-Root-Nodes belegen zusätzlich genau einen von 256 statischen
VFS-Registrierungsslots. Vor `unlink`, `rmdir` sowie Quell- und Zielseite von
`rename` löst VFS den aktuellen Pfad einmal begrenzt auf und vergleicht die
adaptereigene Objektidentität mit allen offenen Nodes desselben Mounts. FAT12
verwendet Directory-Sektor und -Slot, FAT32 Elterncluster und kanonischen
8.3-Eintragsnamen; Groß-/Kleinschreibung und VFAT-Aliase umgehen die Sperre
daher nicht. Ein Treffer liefert `EBUSY` vor Namespace- oder Kettenwirkung.
Nach erfolgreichem `close` wird genau der zugehörige Slot freigegeben;
Close-Fehler behalten Slot und Mountzähler. Das ist bewusst keine
POSIX-Orphan-Lebensdauer: Löschen oder Ersetzen einer noch offenen Datei wird
fail-closed verweigert. Unmount und exklusive Wartung bleiben bei jedem offenen
Node vollständig gesperrt.

## Mountvertrag

- Mountpfade und Tabellen sind fest begrenzt.
- Der längste passende Mountpfad gewinnt; `/mnt/hdd1/X` gehört nicht zu `/`.
- Die bevorzugte Rootressource wird vor Hilfsmedien gemountet.
- Ein fehlgeschlagener bevorzugter Mount wird nicht durch ein beliebiges
  späteres Laufwerk ersetzt.
- Dateisystemspezifische Aktivierung während weiterer Mounts darf die
  tatsächliche Root-/Defaultressource nicht überschreiben.
- Die frühe Mountzusammenfassung ist portabler Klartext ohne ANSI-Steuerbytes,
  da serielle Ausgabe und Framebuffer-Konsole denselben Strom erhalten. Die
  Anzahl gemounteter, vorhandener und fehlgeschlagener Laufwerke bleibt
  vollständig sichtbar; ein Terminalparser gehört nicht in den VFS-Pfad.

## Fehler- und Schreibgrenze

Userpointer, Größen, Deskriptoren und Pfade werden vor Wirkung validiert.
Storage-Quarantäne und globales Write-Fencing werden unterhalb von VFS
durchgesetzt. Markierte FAT32- und FAT12-Volumes besitzen eigene
Persistenzprotokolle. Fremde FAT12- und FAT32-Medien bleiben lesbar, sind aber
ohne gültigen REIST-Journalmarker grundsätzlich read-only; EXT2 bleibt
ebenfalls read-only. Ein unklarer Commit darf nicht als Erfolg erscheinen.
Für FAT12 schneidet ein fest begrenzter Hosttest eine vollständige
Cross-Cluster-VFS-Erweiterung nach jedem tatsächlich abgeschlossenen
512-Byte-Write ab. Vor Recovery darf jeder Nicht-Journalsektor nur seinem
exakten alten oder finalen Wert entsprechen. Ein erfolgreicher frischer Mount
muss anschließend das ganze alte oder ganze neue Abbild liefern; eine
intrinsisch uneindeutige redundante Headerlage bleibt sichtbar fail-closed.
FAT32/ATA erfüllt denselben vollständigen Abbildvertrag über den auch im
Produktionscontroller verwendeten transportneutralen Journal-v2-Kern. Eine
feste 20-Sektor-Pending-Ablage beantwortet Reads innerhalb der Transaktion und
publiziert pro Zielsektor erst beim Commit dessen endgültige 512 Bytes unter
Storage-Supervision. Die Hostkampagne prüft jeden gemessenen Rohwrite-Cut,
beide FAT-Kopien, die Zweicluster-Nullerweiterung und eine unabhängige Datei.

## Adapterstatus

- FAT32: Lesen auf validen Standardvolumes; Schreiben, Verzeichnisse, Truncate,
  `fsync`, Rename/Replace ausschließlich mit exakt gebundenem Undo-Journal
  markierter REIST-Images. Ein Volumewechsel erzwingt Rebinding vor Mutation.
  Ein bestehendes VFAT-Langnamenziel kann durch eine reguläre Datei desselben
  Verzeichnisses ersetzt werden: Ziel-LFN und Zielalias bleiben die
  Namensidentität, der Alias übernimmt die Quellmetadaten, die Quellfolge wird
  danach vollständig tombstoned und erst dann wird die alte Zielkette
  freigegeben. Die gesamte Reihenfolge liegt in einer VFS-Journaltransaktion.
- FAT12: Lesen auf validen Standardmedien; Schreiben, Verzeichnismutationen,
  beide FAT-Kopien, REIST-Journal, Remap und kritische Replikate ausschließlich
  auf explizit markierten und erfolgreich wiederhergestellten Medien.
- EXT2: grundlegende VFS- und indirekte Blockpfade; kein REIST-Journal.

Hosttests prüfen Mountpräfixe, Lebenszyklen und Adapterinvarianten. QEMU-
Gasttests bleiben erforderlich, weil nur sie Treiber, Partitionstransport,
VFS, Syscalls und Ring 3 gemeinsam ausführen.

Native FAT32-HDD-Images enthalten bereits im frisch erzeugten Zustand die
leeren Verzeichnisse `/trash/files` und `/trash/info`. Der begrenzte
Imagebaum nimmt solche expliziten Verzeichnisse unter denselben Regeln wie
Dateipfade an: kleingeschriebenes kanonisches VFAT, höchstens vier Ebenen,
begrenzte Directory-Slots und Ablehnung jeder Datei-/Verzeichniskollision.
Dadurch hängt der Desktopstart nicht von einer frühen Mutation des
Bootmediums ab; `desktop_trash_prepare` bleibt als idempotenter Fallback für
ältere, schreibbare Images bestehen. Das Rettungs-Floppy erhält diese
HDD-Verzeichnisse nicht.

VFAT-Langnamen verwenden in der bestehenden 256-Byte-Pfad-ABI validiertes
RFC-3629-UTF-8 und auf dem Medium UTF-16LE. Ein gemeinsamer fester Codec
verwirft Overlong-Sequenzen, Surrogatskalare, Werte über U+10FFFF und
unvollständige UTF-16-Paare. Vor Directory-Allokation müssen sowohl die
255-Byte-Komponentengrenze als auch höchstens 255 UTF-16-Codeunits feststehen;
die maximal 20 LFN-Slots werden nach Codeunits berechnet. BMP-Zeichen und
Supplementary Planes werden verlustfrei zurückgegeben. ASCII bleibt
case-insensitive. Für alle Unicode-Skalarwerte verwendet die FAT32-Identität
Unicode 15.0.0 mit Full Default Case Folding, rekursiver kanonischer
Zerlegung, stabiler Combining-Class-Sortierung und NFC-Komposition samt
algorithmischem Hangul. Die generierten Tabellen sind reproduzierbar
eingecheckt; Laufzeitdaten, Locale und Heap werden nicht benötigt. Die
255-Byte-Grenze beweist feste Maxima von 382 Zwischenskalaren und 763
Schlüsselbytes. Gespeicherter Name und Readdir bewahren die Originalbytes.

## Migrationsreihenfolge

1. [x] `stat`-Shadowtransport und vollständige Metadatenäquivalenz.
2. [x] Ring-3-eigene read-only Mount- und FAT32-Metadatenparser.
3. [x] Kontrollierter `STAT.PRG`-Cutover ohne Legacy-Fallback.
4. [x] Generation- und handlegebundene Cancel-ABI mit sicherer
   Dienstquittierung ergänzen.
5. [x] `HTTPD.PRG` als ersten lang laufenden FAT32-Metadatenclient umstellen.
6. [x] Append-only Operation 3 und FAT12-Parser einschließlich fester Root-
   Directory und 12-Bit-Clusterketten; EXT2 bleibt offen.
7. [x] Append-only Operation 4 als parser-autoritatives FAT-`stat`; der
   Äquivalenzvergleich findet nur noch im Gasttest statt.
8. [x] Begrenzter read-only EXT2-Metadatenparser und append-only Operation 5;
   die reale QEMU-Abnahme läuft auf einer zweiten IDE-Platte.
9. [x] Append-only Operationen 6/7 für begrenztes `read-at` und indexiertes
   `readdir-at`; kontrollierter `CAT.PRG`-/`LS.PRG`-Cutover.
10. [x] Vier feste generationcodierte, kanonisch pfadgebundene Read-only-
    Sessions mit Seek/Fstat/Close und vollständigem `HTTPD.PRG`-Cutover.
11. [x] Als kleinsten Mediationsschritt Claim v2 mit kernelgeschützter Client-
    und Servicegeneration ergänzen; Claim v1 bleibt bytegenau erhalten.
12. [x] Stabile, ownergebundene read-only Objekt-Handles für
    FAT und EXT2; Deskriptorvererbung bleibt ein getrenntes Folgepaket.
13. [x] Explizite read-only Rechte und abschwächende Übergabe
    an eine exakte Prozessgeneration; keine ambiente Spawn-Vererbung.
14. [x] Read-only-Baumläufe von `FIND.PRG` und `TREE.PRG` ohne Legacy-Fallback
    und mit einer absoluten Gesamtdeadline auf Operationen 5/7 umstellen.
15. [x] Desktop-Explorer-Snapshots einschließlich Leer/Voll-Ordnerprobe ohne
    Legacy-Namespace-Fallback und mit atomarer Fünf-Sekunden-Grenze umstellen.
16. [x] Userspace-Shell-Programmsuche und Tab-Vervollständigung mit einer
    gemeinsamen begrenzten Deadline auf Operationen 5/7 umstellen.
17. [x] Zwei feste 64-KiB-Bulk-Slots mit CRC, ownergebundener Publikation und
    atomarer Sammlung für Objektoperation 15 ergänzen.
18. Mutationen erst nach eigenem Journal-, Flush-, Restart- und Power-Loss-
   Nachweis aus Ring 0 entfernen.

### Begrenzter EXT2-Subset in Ring 3

Der Parser folgt dem Linux-EXT2-On-Disk-Format, akzeptiert aber nur Revision 0
oder 1 mit 1, 2 oder 4 KiB großen Blöcken und festen, potenz-of-two großen
Inodes. Er verarbeitet lineare Verzeichnisse über zwölf direkte und einen
einfach-indirekten Blockzeiger. Höchstens 22 Ressourcen, 16 Pfadkomponenten,
32 Verzeichnisblöcke und 128 vermittelte 512-Byte-Sektorreads werden besucht;
der größte feste Stackpuffer ist 4096 Byte. Namen sind im öffentlichen REIST-
Pfadvertrag druckbares ASCII und werden EXT2-konform case-sensitive verglichen.
Unbekannte Incompat-/Read-only-Compat-Features, HTree-Verzeichnisse, Extents,
Symlinkauflösung, doppelt oder dreifach indirekte Verzeichnisblöcke und
64-Bit-Dateigrößen werden fail-closed abgewiesen. Schreibautorität entsteht
daraus nicht.
