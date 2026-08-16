# EXT2-Unterstützung

Stand: 16. August 2026.

EXT2 ist als VFS-Adapter vorhanden. Es kann direkt auf einer veröffentlichten
Blockressource oder auf einer erkannten Partition liegen; ATA und AHCI werden
über denselben Blockgerätevertrag angesprochen.

## Verifiziert

- Superblock- und Signaturerkennung
- Partitionsoffsets
- Verzeichnisauflösung
- direkte und indirekte Blockadressierung im Host-Harness
- Mount und die explizit implementierten VFS-Operationen

Zusätzliche EXT2-Volumes werden unter `/mnt/<device>` veröffentlicht und über
den zugeordneten DOS-Buchstaben erreicht. Die eindeutige FAT32-Systempartition
bleibt Root und wird nicht durch ein früher erkanntes EXT2-Medium verdrängt.

## Grenzen

- kein Journal und keine EXT3-/EXT4-Funktionen
- keine REIST-Persistenzgarantie nach unklarem Schreibabbruch
- keine zugesicherten Symlinks, ACLs oder Extended Attributes
- kein Hotplug- oder Online-Resize-Lebenszyklus

EXT2 darf deshalb nach einem unklaren Schreibfehler nicht automatisch als
`ONLINE_RW` reintegriert werden.
