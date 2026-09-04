# VFS-Architektur

Stand: 3. September 2026.

VFS ist die einzige reguläre Dateisystemschnittstelle für Shell,
Programmlader und Ring-3-Datei-ABI. Direkte globale FAT-Sonderpfade gehören
nicht zum aktuellen Design.

```text
Ring-3-Programm / Shell
          |---------------- read-only metadata/content shadow ------------|
          |                                                               v
      Syscall-/FD-Schicht                                      Storage-Service
          |                                                     (Ring 3)
          VFS                                      FAT12/FAT32/EXT2-Parser
       /   |   \                                                   |
   FAT32 FAT12 EXT2                             mediated bounded Block I/O
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
keine POSIX-Binärkompatibilität. Mit Ausnahme der unten beschriebenen nativen
EXT2-Symlinkerzeugung bleiben Mutationen, Vererbung, Controllerzugriff und DMA
verboten.

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
beträgt die feste Einzelgrenze nun 224 KiB; der weiterhin statische
Allowlist-Gesamtpool ist auf 448 KiB begrenzt. Diese Kapazität enthält die
aktuell 387088 Byte großen elf Images mit festem Wachstumsspielraum; eine
erneute Überschreitung meldet nun ausdrücklich `resident rescue cache / capacity`
statt den Namen des zuletzt erfolgreich validierten Programms irreführend als
Imagefehler anzuzeigen.

Append-only Frameoperation 15 und Storage-Operation 32 ergänzen einen
objektbezogenen Bulk-Lesepfad. Der 512-Byte-Kontrollframe bleibt im
redundanten Request-Pool; die Nutzdaten liegen in genau zwei kernel-eigenen,
statischen Slots mit höchstens 128 KiB. Append-only Syscall 124 erlaubt nur der
gebundenen Servicegeneration die Publikation und nur der exakten
Clientgeneration die atomare Abholung. Kernel- und Frame-CRC werden vor
Offsetfortschritt geprüft. Timeout, Cancel, Prozessende und Serviceverlust
widerrufen Slot und Handle; Erschöpfung liefert `ENOSPC`. Die große Kopie läuft
mit erlaubten Interrupts, aber ohne Schedulerwechsel, und hält den IRQ-Lock nur
für kleine Metadatenübergänge. Der bisherige 256-Byte-Pfad bleibt unverändert.
FAT-Verzeichnisläufe sind dafür auf 128 Cluster und alle FAT-Operationen auf
320 Sektorreads begrenzt; nur Dateiinhalte dürfen bis zu 6400 Cluster besuchen.
Eine konstante Brent-Zykluswache ersetzt dabei das proportional zur Dateigröße
wachsende Clusterarray. Ein operationseigener FAT-Sektorcache macht auch späte
128-KiB-Lesezugriffe auf 3-MiB-Dateien innerhalb dieses Budgets möglich. Der
direkte EXT2-Kompatibilitätswrapper bleibt auf 192 Sektorreads begrenzt; der
serviceeigene Pfad einschließlich einer möglicherweise nötigen
Symlink-Journal-Recovery besitzt ein festes Gesamtlimit von 384 Reads.

Storage-Operation 33 und die append-only Frameoperationen 16 bis 19 ergänzen
`symlink`, `readlink`, `lstat` und Objekt-`open` mit `O_NOFOLLOW`. Normales
`stat`, Lesen und Öffnen folgen nativen EXT2-Links; `lstat`/`readlink` und
`O_NOFOLLOW` behandeln die letzte Komponente selbst. Relative Ziele werden am
Link-Elternverzeichnis zusammengesetzt, absolute am globalen `/`; `..` darf
den globalen Namespace nicht verlassen. Die erste feste Teilmenge akzeptiert
maximal 191 druckbare ASCII-Bytes je Ziel, acht Linkhops und 64 insgesamt
gelaufene Komponenten. Zyklen liefern `ELOOP`, Dangling Links `ENOENT`,
malformed oder nicht darstellbare Ziele einen Fehler ohne Objektpublikation.
Ein absoluter Link wird vom globalen Root neu gemountet, muss in diesem ersten
Schnitt aber wieder auf einem validierbaren EXT2-Medium landen; ein Übergang zu
FAT scheitert geschlossen.

EXT2 speichert Ziele bis 60 Byte im Inode und längere Ziele blockgestützt. Für
die Erzeugung reserviert das Image die reguläre Datei
`/.reist-symlink-journal` mit 26 Sektoren. Zwei redundante CRC-Header führen
`CLEAN -> ACTIVE -> COMMITTED -> CLEAN`; bis zu 24 Before-Images erlauben nach
jeder Schreib- oder Flush-Unterbrechung entweder die vollständige Rückkehr zum
alten Namespace oder die Bestätigung eines vollständigen Links. Der Dienst
begrenzt die gesamte Transaktion auf 384 Reads, 64 Writes, acht Flushes und 32
untersuchte Allokationsgruppen. In Version 1 wird kein neuer Directory-Block
angelegt: ausreichender vorhandener Slack und ein einzelner 512-Byte-
Publikationssektor sind Voraussetzung. Der Client wiederholt nach einer
abgeschlossenen Recovery höchstens einmal unter derselben absoluten Deadline.
FAT12/32 liefern für die Erzeugung `EOPNOTSUPP`. Der Legacy-Ring-0-EXT2-Adapter
bleibt read-only und erhält weder Linkauflösung noch Mutation.

Storage-Operation 34 transportiert zusätzlich einen eigenen, exakt 512 Byte
großen Namespace-Frame. Seine append-only Operationen 20 und 21 entsprechen
den POSIX-Begriffen `unlink` und `rename`, sind aber bewusst auf native
EXT2-Symlinkobjekte begrenzt. `unlink` bearbeitet ausschließlich den finalen
Directory-Eintrag und folgt dem Ziel nicht. Es entfernt einen Link mit
Linkzähler eins, setzt den Inode frei und gibt bei einem blockbasierten Ziel
genau dessen validierten direkten Block zurück. `rename` ist no-replace,
erhält Inode und Linkziel und akzeptiert nur Quell- und Zielnamen desselben
Verzeichnisses, wenn der neue Name in den vorhandenen Record und denselben
512-Byte-Sektor passt. Cross-Directory, Verzeichnisse, reguläre Dateien,
vorhandene Ziele und wachsender Layoutbedarf werden vor dem ersten Write
abgewiesen.

Beide Mutationen verwenden das vorhandene 26-Sektor-Undo-Journal und genau
einen Directory-Publikationssektor. Nicht sichtbare Inode-, Bitmap- und
Zählermetadaten werden zuerst geschrieben und verifiziert; erst danach wird
der Namespace publiziert. `ACTIVE`-Recovery stellt sämtliche Before-Images
wieder her, `COMMITTED`-Recovery behält den vollständig geprüften Endzustand,
anschließend werden beide Header `CLEAN`. Der generationgebundene Client führt
höchstens einen Recovery-Retry unter einer gemeinsamen absoluten Deadline aus.
FAT und alle nicht unterstützten Objektarten liefern `EOPNOTSUPP`; nur in
diesem Fall dürfen `del`, `rm` und `rename` den bisherigen Legacy-Pfad nutzen.
Allgemeines EXT2-Create/Write/Replace sowie Verzeichnis- und
Cross-Directory-Mutationen bleiben spätere N3-Schritte.

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
Eingabezeile; der geschützte Resident-Fallback bleibt davon unabhängig.
Der Dokument-Ladepfad von `NOTEPAD.PRG` verwendet ebenfalls ein stabiles
Objekthandle, jedoch ausdrücklich nur mit `READ|STAT`. Pfadauflösung und
Metadaten stammen vom selben generationgebundenen Storage-Objekt; Inhalt wird
erst nach vollständig geprüftem EOF und erfolgreichem Close an das
Editormodell publiziert. Ein fehlender Pfad erzeugt weiterhin ein neues leeres
Dokument. Speichern, `fsync`, Rename und Journalmutation bleiben auf dem
bisherigen VFS-Mutationspfad und sind nicht Bestandteil dieses Cutovers.
Die vier Applets von `CONTROL.PRG` lesen ihre drei Konfigurationsdateien über
denselben Objektvertrag mit `READ|STAT`. Typ, feste Dateigröße, vollständiger
Inhalt, EOF und Close werden geprüft, bevor der bestehende begrenzte
Konfigurationsparser einen neuen sichtbaren Wert liefern darf. Mutation bleibt
ausschließlich Aufgabe des getrennten `CONFIG.PRG`-Dienstprozesses.
`COPY.PRG` trennt die Autoritäten: Die Quelle ist ein read-only
Storage-Objekt mit `READ|STAT`, während ausschließlich der Zielpfad die
bestehende mutierende VFS-Deskriptorautorität behält. Ein Quellfehler oder
stale Objekt kann daher keine erfolgreiche Kopie publizieren.
Auch BASIC `LOAD` verwendet ein einzelnes `READ|STAT`-Objekt. Die bestehende
Maximalgröße wird vor der Allokation geprüft; exakt gebundener Inhalt, EOF und
Close müssen vor dem atomaren Austausch des aktuellen Programms erfolgreich
sein. Der Notepad-Dateidialog besitzt keine getrennte Legacy-Stat-Vorprüfung
mehr, sondern überlässt Existenz, Typ, Größe und Inhalt demselben bereits
objektgebundenen Ladepfad.
Große Notepad-Dokumente bleiben nun über ein einziges generationgebundenes
`READ|STAT|SEEK`-Objekt als unveränderliche Originalquelle geöffnet. Eine
feste Piece Table mit 256 Einträgen und 65536 Byte append-only Add-Speicher
materialisiert jeweils nur ein UTF-8-gültiges Controllerfenster. Der atomare
Savepfad streamt Original- und Add-Pieces in die Tempdatei und bindet erst nach
Fsync, Close und Rename die neue Objektgeneration; ein Fehler lässt die alte
Datei maßgeblich.
Der Ladepfad von `EDIT.PRG` folgt demselben Objektvertrag. Ein vorhandenes
Dokument wird genau einmal mit `READ|STAT` geöffnet; Fstat, die auf 51200 Byte
begrenzten Vorwärtsreads, exakter EOF und Close gehören zu derselben
Generation und zu einer absoluten monotonen 60-Sekunden-Frist. Erst danach
werden die höchstens 200 Zeilen veröffentlicht und der Laufzeitmarker
ausgegeben. Fehlende Pfade bleiben neue leere Dokumente. Tempfile, Schreiben,
`fsync`, Close und atomarer Rename des Speicherpfads bleiben unverändert.
Auch der `.trashinfo`-Lader des Desktops verwendet fuer Restore und
Empty-Validierung genau ein `READ|STAT`-Objekt. Fstat muss eine regulaere Datei
kleiner als die feste 640-Byte-Metadatenkapazitaet liefern; danach muessen
exakt diese Generation und Groesse, EOF und Close innerhalb einer gemeinsamen
absoluten monotonen Fuenf-Sekunden-Frist erfolgreich sein. Erst dann erreicht
der Inhalt den bestehenden Format-v2-Parser. Fehler-Close ist separat auf eine
Millisekunde begrenzt. Katalogpruefung, Create, Write, `fsync`, Rename, Unlink,
Restore und Empty-Mutationen bleiben auf dem bisherigen Pfad.
Der generische Pfadmodus von `CHKDSK.PRG` traversiert ebenfalls ausschließlich
über die Ring-3-Stat- und Readdir-at-Clients. Reguläre Dateien werden mit
`READ|STAT` geöffnet; Fstat, exakt größenbegrenztes Lesen, EOF und Close müssen
am selben generationgebundenen Objekt erfolgreich sein. Eine gemeinsame
absolute monotone Frist von 60 Sekunden begrenzt den vollständigen Scan. Der
Objektclient erlaubt dafür eine additive lokale Aktualisierung seines
Request-Timeouts, sodass jede folgende Operation nur das noch verbleibende
Budget erhält. Bei Frist- oder Kapazitätsfehler wird kein Erfolg publiziert;
ein bereits geöffnetes Objekt wird mit einem getrennt begrenzten Close-Versuch
freigegeben. Die `--fat12`-Maintenance-Aufträge verwenden unverändert ihren
versionierten Storage-Service-Vertrag. Das Maintenance-Prozessprofil besitzt
dazu keine direkten Legacy-Open-/Read-/Close-/Stat-/Readdir-Syscalls und keine
Bulk-Collect-Autorität mehr. Der Kernel akzeptiert aus dieser Domain neben den
exklusiven FAT12-Operationen 11 bis 30 nur den bestehenden geschützten
VFS-Shadow-Umschlag; Bulk-Reads, generische Writes, Formatierung und
Blockzugriff bleiben verweigert.
Der gemeinsame WAV-Preview-Lader von `WAVPLAY.PRG` und `SOUNDPLAYER.PRG`
verwendet ebenfalls genau ein `READ|STAT`-Objekt. Die unveränderten 512-Byte-
Header-, 16-Chunk-, 512-Byte-Transfer- und 15360-Frame-Grenzen liegen unter
einer gemeinsamen absoluten monotonen 60-Sekunden-Frist. Fstat, alle
vorwärtsgerichteten Reads und der erfolgreiche Close erhalten nur deren
Restbudget; Fehler publizieren keine Wave-Infostruktur. `libreistaudio.a`
enthält Objektclient und kanonische Pfadauflösung als feste interne
Linkabhängigkeit, aber weder Parser- noch Mutationsautorität: Beide bleiben im
generationgebundenen Ring-3-Storage-Service.
Der FAT12-Nachweis
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
ohne gültigen REIST-Journalmarker grundsätzlich read-only. Der Legacy-EXT2-
VFS-Adapter bleibt ebenfalls read-only; ausschließlich Storage-Operation 33
darf die oben begrenzte, eigene Symlinktransaktion ausführen. Ein unklarer
Commit darf nicht als Erfolg erscheinen.
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
Unicode 15.0.0 mit Full Default Case Folding, vollständiger kanonischer
Zerlegung, stabiler Combining-Class-Sortierung und NFC-Komposition samt
algorithmischem Hangul. Die generierten Tabellen sind reproduzierbar
eingecheckt; Laufzeitdaten, Locale und Heap werden nicht benötigt. Die
255-Byte-Grenze beweist feste Maxima von 382 Zwischenskalaren und 763
Schlüsselbytes. Eine iterative Tiefensuche ersetzt die frühere C-Rekursion;
die vollständige Unicode-15-Tabelle beweist höchstens vier gleichzeitig
wartende Zerlegungsskalare. Gespeicherter Name und Readdir bewahren die
Originalbytes.

R7.1k bindet den gemeinsamen FAT32-Verzeichnisscan an einen einzigen festen
Arbeitsbereich. Der vorherige GCC-Frame von 3168 Byte enthielt Sektorpuffer,
LFN-Leser, sichtbaren und kurzen Namen sowie beide Unicode-Vergleichsschlüssel
und machte dadurch insbesondere den vollständigen Rename-Syscallpfad größer
als sein zulässiges Stackbudget. Der Arbeitsbereich liegt nun statisch und
wird über eine verschachtelte Ebene der bereits deadline-begrenzten,
taskrekursiven FAT32-Operationsmutex gehalten, auch während der Scan auf I/O
blockiert. Ein rekursiver Scan desselben Besitzers wird vor Datenzugriff
abgewiesen. Jeder Found-, Not-found- und Fehlerausgang gibt Belegtstatus und
Mutexebene genau einmal frei; nur der äußere Operationsabschluss darf den
Context-Sync-Hook ausführen. Damit bleiben Scanresultat, VFAT-/Unicode-
Identität, Journalformat und öffentliche VFS-ABI unverändert, ohne den
8-KiB-Taskstack oder seine Guardpage zu vergrößern.

R7.1l erweitert denselben Besitzvertrag auf die zusammenhängende
Metadatenmutation. Zwei auflösbare Rename-Pfade gehören dem aktuellen
Task-Slot und seiner Generation. LFN-Publikation, FAT-Update sowie Rename-
Tombstone besitzen getrennte feste Puffer unter der FAT32-Operationsmutex;
Journalheader und Recoverysektor gehören dem einzelnen Journalobjekt unter der
ATA-Transaktionsmutex. Jeder Belegtstatus wird vor Datenzugriff geprüft und
auf jedem Ausgang gelöscht. Die Laufzeitarbeitsbereiche sind kein Teil des
gepackten Journal-v2-Medienformats; zwanzig Slots, vier Durabilitätsbarrieren,
Readback, Cacheinvalidierung und Storage-Fence bleiben unverändert.

R7.1m wendet denselben Vertrag auf den FAT12/FDD-Pfad an. Die globale
VFS-Serialisierung wird durch eine deadline-begrenzte rekursive FAT12-
Workspace-Mutex ergänzt. Core-Write, Verzeichnisscan, Pfadauflösung,
Clusterallokation, Entry-Update, Write, Truncate, Mkdir und Fstat halten
unterschiedliche feste Slots, sodass verschachtelte Journal-I/O keinen noch
lebenden Eingabepuffer überschreibt. Ein zweiter Eintritt in denselben Slot
scheitert vor Payloadzugriff; Freigabe löscht den vollständigen Slot. Das
FAT12-Undo-Journal verwendet vier instanzgebundene Runtime-Sektoren für zwei
Inputs, Header und Readback. Seine Version-2-Medienstrukturen, CRCs,
Zielausschlüsse, Sequenz- und Recoveryreihenfolge ändern sich nicht. Die
Quarantäneentscheidung bleibt synchron im Storage-Sicherheitsvertrag; nur die
Konsolenformatierung erfolgt später im Supervisor-Poll und niemals unter der
FDD-Transaktionsmutex. Der saubere 104-Objekt-Stacknachweis blieb mit
6820/7168 Byte innerhalb des unveränderten Syscallbudgets. Reale
Vier-vCPU-VMware-Läufe bestätigten Rename und bytegeprüften Benchmark mit
Cleanup, Shell-Rückkehr und je zehn stabilen Sekunden; der Benchmark erreichte
314,88 KiB/s Schreiben und 450,70 KiB/s Lesen.

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
17. [x] Zwei feste 128-KiB-Bulk-Slots mit CRC, ownergebundener Publikation und
    atomarer Sammlung für Objektoperation 15 ergänzen.
18. [~] Allgemeine Mutationen erst nach eigenem Journal-, Flush-, Restart- und
    Power-Loss-Nachweis aus Ring 0 entfernen. Die native EXT2-
    Symlinkerzeugung ist der erste isolierte Ring-3-Schnitt; Create, Write,
    Rename, Replace und Reparatur bleiben offen.

### Begrenzter EXT2-Subset in Ring 3

Der Parser folgt dem Linux-EXT2-On-Disk-Format, akzeptiert aber nur Revision 0
oder 1 mit 1, 2 oder 4 KiB großen Blöcken und festen, potenz-of-two großen
Inodes. Er verarbeitet lineare Verzeichnisse über zwölf direkte und einen
einfach-indirekten Blockzeiger. Höchstens 22 Ressourcen, 16 Komponenten je
materialisiertem Pfad, 64 Komponenten über eine vollständige Linkkette, acht
Linkhops und 32 Verzeichnisblöcke werden besucht. Der direkte
Kompatibilitätspfad besitzt 192 vermittelte 512-Byte-Sektorreads; der
serviceeigene Pfad einschließlich Recovery höchstens 384. Der größte feste
Stackpuffer ist 4096 Byte. Namen und Linkziele sind im öffentlichen REIST-
Pfadvertrag druckbares ASCII und werden EXT2-konform case-sensitive verglichen.
Unbekannte Incompat-/Read-only-Compat-Features, HTree-Verzeichnisse, Extents,
doppelt oder dreifach indirekte Verzeichnisblöcke und 64-Bit-Dateigrößen werden
fail-closed abgewiesen. Allgemeine Schreibautorität entsteht daraus nicht;
nur der oben beschriebene journalisierte Symlinkschnitt darf Inode-, Block- und
Directory-Metadaten innerhalb seiner festen Transaktion verändern.
