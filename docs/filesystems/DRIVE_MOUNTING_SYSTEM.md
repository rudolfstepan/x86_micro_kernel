# Laufwerke, Mounts und Pfadzuordnung

Stand: 16. August 2026.

REIST trennt physische Blockressourcen, Partitionen, VFS-Mounts und
DOS-Laufwerksbuchstaben. Anwendungen arbeiten über VFS; eine Resource-ID ist
nur für kontrollierte Storage-Werkzeuge wie `FORMAT.PRG` bestimmt.

## Erkennung und Rootauswahl

1. ATA/PCI-IDE, AHCI/SATA und FDD werden mit festen Kapazitäten erkannt.
2. MBR-Partitionen erscheinen als eigene Child-Ressourcen, etwa `hdd0p1`.
3. Partitionierte ATA-/AHCI-Eltern werden nicht zusätzlich direkt gemountet.
4. FAT32, FAT12 und EXT2 werden als VFS-Adapter registriert.
5. Ein BIOS-Boot von Diskette bevorzugt genau die entsprechende FDD.
6. Andernfalls muss genau eine gültige FAT32-Partition `X86 SYSTEM` heißen.
7. Das bevorzugte Volume wird zuerst als `/` gemountet; ein Fehler erlaubt
   keinen stillen Wechsel auf ein fremdes Medium.
8. Weitere gültige Volumes erhalten `/mnt/<device>`.

Ohne bevorzugtes Volume bleibt nur der Kompatibilitätspfad der begrenzten
Erkennungsreihenfolge. Produktive Images sollen deshalb immer die eindeutige
Systemmarkierung tragen.

## Namen, Resource-IDs und Buchstaben

`DRIVES.PRG` zeigt nur veröffentlichte, gemountete Ressourcen:

```text
Resource  Drive  Device  Type
-----------------------------
2         C:     hdd0p2  PART
3         A:     fdd0    FDD
```

Die Zahlen sind Beispiele. Resource-IDs entstehen aus der erkannten
Ressourcentabelle und dürfen nicht dauerhaft angenommen werden. Das Root-
Volume wird als `C:` angezeigt, FDDs beginnen bei `A:`. Weitere Festplatten-
oder Partitionsnamen werden ab `C:` anhand ihrer Gerätenummer zugeordnet.

## Pfade

```text
C:\README.TXT
A:\TOOLS\CHKDSK.PRG
hdd0p2:/README.TXT
/mnt/fdd0/TOOLS/CHKDSK.PRG
```

DOS-Pfade werden in der Ring-3-Shell kanonisch auf VFS-Pfade abgebildet. Jedes
Laufwerk behält sein eigenes aktuelles Verzeichnis. Ein fehlgeschlagener `CD`-
oder Mountvorgang verändert weder aktives Laufwerk noch Arbeitsverzeichnis.

## Manuelles Mounten

`MOUNT <device>` ist Diagnose und Kompatibilitätsfunktion. Es darf eine
fehlgeschlagene eindeutige Rootauswahl nicht umgehen. Bei partitionierten
Medien wird die Child-Ressource, nicht der physische Elternname, verwendet.

## Fehlerdiagnose

- `DRIVES` zeigt keine Zeile: Ressource wurde nicht erfolgreich gemountet oder
  nicht über die ABI veröffentlicht.
- Mehrere `X86 SYSTEM`-Labels: Rootauswahl wird absichtlich verweigert.
- `DIR` funktioniert, Schreiben nicht: Read-only-/Quarantäne-/Journalstatus
  und Transportfehler im seriellen Log prüfen.
- SATA-Partition liest falsche Daten: Parent-Transport muss AHCI bleiben;
  partition-relative Batchzugriffe dürfen nicht auf ATA-PIO zurückfallen.
