# VFS-Architektur

Stand: 24. August 2026.

VFS ist die einzige reguläre Dateisystemschnittstelle für Shell,
Programmlader und Ring-3-Datei-ABI. Direkte globale FAT-Sonderpfade gehören
nicht zum aktuellen Design.

```text
Ring-3-Programm / Shell
          |--------------------- read-only stat shadow --------------------|
          |                                                               v
      Syscall-/FD-Schicht                                      Storage-Service
          |                                                     (Ring 3)
          VFS                                              FAT32/VFAT-Parser
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
Legacy-Brücke `SYS_STAT`. Die append-only Operation 2 löst dagegen Mountpräfix,
FAT32-BPB, ASCII-8.3-/VFAT-Namen, Verzeichniscluster und Metadaten selbst im
Storage-Service auf. Sie darf höchstens 22 Ressourcen, 32 Pfadkomponenten, 32
Verzeichniscluster und 64 vermittelte Sektorreads untersuchen und verwendet
keinen Heap. Ihr Ergebnis wird nur veröffentlicht, wenn Status und sämtliche
öffentlichen Metadatenbytes exakt mit `SYS_STAT` übereinstimmen; eine Abweichung
liefert den Integritätsfehler `-84`. `open`, Mutationen, Controllerzugriff und
DMA bleiben verboten.

Damit besitzt Ring 3 nun echte FAT32-Parsersemantik, aber noch keine
autoritative VFS-Entscheidung. `SYS_STAT` bleibt Vergleichsorakel und der
Kernelpfad bleibt bis zu einem getrennten Cutoverpaket maßgeblich. FAT12,
EXT2, Handles, Lesen und Verzeichnisiteration sind noch nicht migriert.

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
3. Umschalten read-only Operationen bei getesteter Äquivalenz; Legacy-Pfad nur
   als explizit begrenzter Degradationsmodus.
4. Handles, Lesen und Verzeichnisiteration migrieren.
5. Mutationen erst nach eigenem Journal-, Flush-, Restart- und Power-Loss-
   Nachweis aus Ring 0 entfernen.
