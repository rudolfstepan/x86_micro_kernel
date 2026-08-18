# FAT32-Status

Stand: 18. August 2026.

FAT32 ist das Systemdateisystem des nativen Festplattenimages. Die Partition
beginnt bei LBA 8192, trägt das Label `X86 SYSTEM` und wird bei eindeutiger
Erkennung als `/` beziehungsweise `C:` gemountet. Auf SATA ist dies
typischerweise die Partition `hdd0p2`, nicht das physische Elternlaufwerk.

## Implementiert

- BPB-/FSInfo- und FAT-Konsistenzprüfung
- Datei-I/O, Seek, Truncate und Freigabe von Clusterketten
- wachsende Verzeichnisse sowie konsistentes `readdir`/`open`
- Dateierzeugung, Verzeichnisse, Delete und Copy über VFS
- Batch-I/O über die gemeinsame Blockgeräteschicht
- `fsync` mit begrenztem Geräte-Flush und Readback
- Same-Directory-Rename und Replace
- redundantes Undo-Journal für markierte REIST-Images
- Recovery vor normaler Metadatenverwendung
- VFAT Long File Names bis 255 Zeichen mit checksum-validiertem 8.3-Alias

## Namensvertrag

VFS und Image-Builder veröffentlichen lange Namen aus einer vollständig
validierten, absteigenden VFAT-Slotfolge. Reihenfolge, Slotanzahl, Attribute,
Startcluster und Prüfsumme müssen zum nachfolgenden 8.3-Eintrag passen;
andernfalls bleibt ausschließlich dessen Alias sichtbar. Erzeugen, Löschen
und Same-Directory-Rename behandeln LFN-Slots und Alias als eine begrenzte
Eintragsfolge. Aliase werden kollisionsgeprüft als `~n` erzeugt.

Die aktuelle Pfad-ABI ist bewusst auf druckbares ASCII begrenzt. FAT speichert
diese Zeichen normgerecht als UTF-16LE; fremde, nicht unterstützte Unicode-
Folgen werden nicht fehlinterpretiert, sondern fail-closed auf den 8.3-Alias
zurückgeführt. Vollständiges Unicode samt Normalisierung bleibt Folgearbeit.

Der Editor verwendet eine PID-spezifische 8.3-Tempdatei und veröffentlicht das
Ziel erst nach erfolgreichem `fsync`, Close und Rename. Ein Fehler vor dem
Commit lässt die alte Zieldatei unangetastet.

## Transport

Partition-relative Einzel- und Mehrsektorzugriffe werden auf absolute LBAs
des Elternlaufwerks abgebildet. Der Elterntransport bleibt erhalten: AHCI-
Partitionen verwenden AHCI, ATA-Partitionen ATA-PIO. Dieser Vertrag wird durch
Quelltests und einen vollständigen QEMU-SATA-Gastlauf geprüft.

## Grenzen

- Das Undo-Journal gilt nur für entsprechend markierte native Images.
- Cross-Directory- und Cross-Volume-Rename sind nicht zugesichert.
- LFN-Rename auf einen bereits existierenden Zielnamen wird fail-closed
  abgelehnt; atomarer Replace bleibt für 8.3-Ziele erhalten.
- Nicht-ASCII-VFAT-Namen sind derzeit nur über ihren 8.3-Alias erreichbar.
- Fremde FAT32-Medien bleiben kompatibel, erhalten aber keine implizite
  REIST-Persistenzgarantie.
- Ein grüner QEMU-/VMware-Lauf ersetzt keine Stromausfall- oder
  Controller-Langzeitprüfung auf realer Hardware.
