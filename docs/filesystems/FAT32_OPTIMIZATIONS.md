# FAT32-Status

FAT32 ist das primäre beschreibbare Dateisystem des nativen VMware-Images.
Die Datenpartition beginnt dort ab LBA 8192 und wird im Ein-Platten-Aufbau als
`hdd0`, VFS-Root `/` und DOS-Laufwerk `C:` bereitgestellt.

## Aktueller Funktionsumfang

- Bootsektor-, FSInfo- und Geometrieprüfung
- MBR-Partitionsoffset
- gespiegelte FAT-Kopien und aktive-FAT-Auswahl
- Lesen, Schreiben, Seek und Truncate
- Anlegen und Löschen von Dateien und Verzeichnissen
- Erweitern von Verzeichnis- und Datei-Clusterketten
- Freigabe und Rückgewinnung von Ketten auf Fehlerpfaden
- case-insensitive Suche nach 8.3-Kurznamen
- VFS-Adapter für Shell und Programmlader
- Schutz der globalen Volume-Kontexte gegen Timerpräemption

## Erzeugtes Image

`scripts/create_native_boot_image.py` schreibt eine minimale konsistente
FAT32-Partition mit:

- Haupt- und Backup-Bootsektor
- Haupt- und Backup-FSInfo
- zwei identischen FAT-Kopien
- mehrclustrigen Dateien
- einer bei vielen Einträgen wachsenden Root-Verzeichniskette
- `README.TXT` und beliebig vielen eindeutigen `--data-file NAME=PFAD`-Dateien

Eingebettete Namen müssen gültiges ASCII-8.3 sein. Leere Dateien verwenden
Startcluster 0. Größen werden als 32-Bit-FAT-Dateigröße gespeichert.

## Shelltest

```text
C:\> DIR
C:\> TYPE README.TXT
C:\> MD TEST
C:\> COPY HELLO.PRG TEST\APP.PRG
C:\> RUN TEST\APP.PRG
```

`DIR`, `TYPE` und `RUN` verwenden denselben VFS-Resolver. Dies verhindert den
früheren Fehler, dass die Verzeichnisauflistung eine Datei fand, der direkte
FAT-Open-Pfad jedoch nicht.

## Regressionstests

`test/test_fat32_host.c` prüft unter anderem Schreiben, Truncate und
Verzeichniserweiterung. `test/test_native_boot_image.py` rekonstruiert Dateien
aus FAT-Ketten, vergleicht beide FAT-Kopien und prüft Root-Kettenwachstum.
Der VFS-Integrationstest öffnet `README.TXT` sowohl mit originaler als auch
abweichender Großschreibung.

## Grenzen

- kein vollständiges VFAT-LFN
- keine Journaling- oder Transaktionsgarantie bei Stromausfall
- eingebettete Builddateien liegen derzeit nur im Rootverzeichnis
- ein globaler Volume-Kontext pro aktiver Operation, abgesichert für den
  aktuellen Uniprozessorbetrieb
