# EXT2-Unterstützung

EXT2 ist über einen VFS-Adapter in den Kernel eingebunden. Der automatische
Mountcode erkennt sowohl ein direkt auf dem ATA-Gerät liegendes
EXT2-Superblock-Magic als auch Linux-MBR-Partitionen vom Typ `0x83`.

## Aktueller Umfang

- Superblock- und Geometrieprüfung
- Pfad- und Verzeichnisauflösung
- Datei-Lesezugriff
- direkte und indirekte Blockadressierung gemäß Hosttest
- VFS-Mount und Verzeichnisoperationen im Umfang des Adapters

Der Regressionstest `test/test_ext2_host.c` prüft Partitionserkennung,
Verzeichnisse und indirekte Blöcke ohne einen Emulatorstart.

## Verwendung

Beim Boot werden EXT2, FAT32 und FAT12 registriert. Wird ein zusätzliches
ATA-Laufwerk als EXT2 erkannt, erscheint es über `DRIVES` und wird unter einem
VFS-Mountpunkt wie `/mnt/hdd1` verfügbar. In der Shell kann es mit seinem
DOS-Buchstaben angesprochen werden:

```text
C:\> DRIVES
C:\> DIR D:\
C:\> TYPE D:\DOCS\INFO.TXT
```

## Grenzen

- kein Journaling; EXT2 ist bewusst kein EXT3/EXT4
- keine dokumentierte Unterstützung für Symlinks, ACLs oder Extended Attributes
- Schreiboperationen sind nur soweit verlässlich, wie der jeweilige VFS-
  Adapter sie explizit anbietet
- keine Hotplug- oder Online-Resize-Unterstützung

Frühere QEMU-/ISO-Beispiele mit manuellen Mountbefehlen sind nicht mehr der
aktuelle Standardweg.
