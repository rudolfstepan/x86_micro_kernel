# VFS-Architektur

Stand: 24. August 2026.

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
höchstens 22 Ressourcen, 32 Pfadkomponenten, 32 Verzeichniscluster und 64
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
den generationsgebundenen Request. `CAT.PRG` und `LS.PRG` sind zusätzlich auf
Operation 6/7 umgestellt; andere Clients und der Kernelpfad bleiben
unverändert. Append-only Syscall 118 stellt inzwischen
eine requestbezogene Cancel-ABI bereit: queued und vollständige Requests werden
sofort widerrufen; bereits vom Dienst übernommene Requests bleiben bis zu dessen
Quittierung `cancel-pending` und können kein Ergebnis mehr publizieren. Das ist
ein Widerruf der Ergebnisautorität, kein physischer I/O-Abbruch oder Rollback.
EXT2-`stat`, Lesen in `CAT.PRG`, Verzeichnisiteration in `LS.PRG` und die
pfadgebundenen Read-only-Sessions sind damit migriert; stabile Objekt-Handles
und Vererbung sind noch nicht migriert.

`HTTPD.PRG` ist der erste vollständig umgestellte lang laufende Client dieser
read-only ABI. Metadaten, Datei-Sessions und Verzeichnisiteration verwenden
ausschließlich Operationen 5 bis 7. Zwölf QEMU-HTTP-Transaktionen wechseln
zwischen Dateiinhalt und Listing; der Server bleibt bis `Ctrl+C` aktiv und
kehrt danach zur Shell zurück. Shell und Desktop bleiben unverändert. Der FAT12-Nachweis
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

## Mountvertrag

- Mountpfade und Tabellen sind fest begrenzt.
- Der längste passende Mountpfad gewinnt; `/mnt/hdd1/X` gehört nicht zu `/`.
- Die bevorzugte Rootressource wird vor Hilfsmedien gemountet.
- Ein fehlgeschlagener bevorzugter Mount wird nicht durch ein beliebiges
  späteres Laufwerk ersetzt.
- Dateisystemspezifische Aktivierung während weiterer Mounts darf die
  tatsächliche Root-/Defaultressource nicht überschreiben.

## Fehler- und Schreibgrenze

Userpointer, Größen, Deskriptoren und Pfade werden vor Wirkung validiert.
Storage-Quarantäne und globales Write-Fencing werden unterhalb von VFS
durchgesetzt. Markierte FAT32- und FAT12-Volumes besitzen eigene
Persistenzprotokolle. Fremde FAT12- und FAT32-Medien bleiben lesbar, sind aber
ohne gültigen REIST-Journalmarker grundsätzlich read-only; EXT2 bleibt
ebenfalls read-only. Ein unklarer Commit darf nicht als Erfolg erscheinen.

## Adapterstatus

- FAT32: Lesen auf validen Standardvolumes; Schreiben, Verzeichnisse, Truncate,
  `fsync`, Rename/Replace ausschließlich mit exakt gebundenem Undo-Journal
  markierter REIST-Images. Ein Volumewechsel erzwingt Rebinding vor Mutation.
- FAT12: Lesen auf validen Standardmedien; Schreiben, Verzeichnismutationen,
  beide FAT-Kopien, REIST-Journal, Remap und kritische Replikate ausschließlich
  auf explizit markierten und erfolgreich wiederhergestellten Medien.
- EXT2: grundlegende VFS- und indirekte Blockpfade; kein REIST-Journal.

Hosttests prüfen Mountpräfixe, Lebenszyklen und Adapterinvarianten. QEMU-
Gasttests bleiben erforderlich, weil nur sie Treiber, Partitionstransport,
VFS, Syscalls und Ring 3 gemeinsam ausführen.

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
11. [ ] Als kleinsten Mediationsschritt Claim v2 mit kernelgeschützter Client-
    und Servicegeneration ergänzen; Claim v1 bleibt bytegenau erhalten.
12. Danach stabile Objekt-Handles und Deskriptorvererbung migrieren.
13. Mutationen erst nach eigenem Journal-, Flush-, Restart- und Power-Loss-
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
