# VFS-Architektur

Stand: 16. August 2026.

VFS ist die einzige reguläre Dateisystemschnittstelle für Shell,
Programmlader und Ring-3-Datei-ABI. Direkte globale FAT-Sonderpfade gehören
nicht zum aktuellen Design.

```text
Ring-3-Programm / Shell
          |
      Syscall-/FD-Schicht
          |
          VFS
       /   |   \
   FAT32 FAT12 EXT2
          |
  Blockgerät / Partition
       /    |    \
   ATA-PIO AHCI  FDD
```

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
