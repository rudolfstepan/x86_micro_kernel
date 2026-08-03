# VFS-Architektur

Das Virtual File System ist die gemeinsame Schnittstelle zwischen Shell,
Programmlader und den Dateisystemtreibern. FAT32, FAT12 und EXT2 sind über
Adapter registriert; frühere direkte Shellaufrufe in globale FAT-Strukturen
gehören nicht mehr zum aktuellen Design.

## Schichten

```text
Shell / Programmlader / Kernelkomponenten
                  |
       kanonischer absoluter VFS-Pfad
                  |
     vfs_open/read/write/stat/readdir/...
                  |
      Mounttabelle und Adapterauswahl
          /           |           \
       FAT32         FAT12         EXT2
          \           |           /
              Blockgerät / ATA / FDD
```

## Öffentliche Operationen

Die aktuelle Schnittstelle umfasst die für den Kernel benötigten
Datei-/Verzeichnisoperationen, unter anderem:

- Mounten und Unmounten
- `open`, `close`, `read`, `write`, `seek`
- `stat`
- `readdir`
- `mkdir`, `rmdir`, `create`, `unlink`

Adapter dürfen für nicht unterstützte Operationen einen eindeutigen
VFS-Fehler zurückgeben. Die Shell übersetzt Fehler wie „nicht gefunden“,
„bereits vorhanden“, „kein Verzeichnis“, „schreibgeschützt“ oder „Datenträger
voll“ in lesbare Meldungen.

## Mountregeln

`auto_mount_all_drives()` initialisiert VFS und registriert alle drei
Dateisystemtypen. Das erste erfolgreiche Laufwerk erhält `/`; weitere werden
unter `/mnt/<laufwerk>` eingehängt. Der Mountpunkt wird zusätzlich im
`drive_t` gespeichert, damit die Shell ihren Laufwerkspfad korrekt in einen
VFS-Pfad übersetzen kann.

Die Mountauswahl muss längste passende Präfixe berücksichtigen: `/mnt/hdd1/x`
gehört zum Mount `/mnt/hdd1`, nicht zum Root-Mount `/`.

## Pfade

VFS selbst erhält absolute, mit `/` getrennte Pfade. DOS-Syntax gehört in die
Shellschicht:

```text
D:\DOCS\A.TXT
  -> drive=hdd1, drive_path=/DOCS/A.TXT
  -> /mnt/hdd1/DOCS/A.TXT
```

Der Resolver normalisiert `.`/`..`, doppelte Separatoren und Laufwerkspräfixe,
bevor VFS aufgerufen wird. Dadurch verwenden `DIR`, `TYPE`, `COPY` und `RUN`
dieselbe Dateiidentität.

## Adapterstatus

### FAT32

- Verzeichnisauflistung, Lesen und Schreiben
- Datei- und Verzeichnisoperationen über VFS
- Clusterketten und case-insensitive 8.3-Suche
- Hostintegrationstest einschließlich `README.TXT`

### FAT12

- Diskettenabbilder und VFS-Adapter
- Datei-/Verzeichnisoperationen gemäß Adapterumfang
- Hosttest auf erzeugtem FAT12-Abbild

### EXT2

- Erkennung als Superblock direkt auf dem Gerät oder in einer Linux-MBR-Partition
- VFS-Adapter und hostseitiger Regressionstest
- Funktionsumfang bleibt kleiner als bei einem vollständigen Linux-EXT2-Treiber

Einzelheiten und bekannte Grenzen stehen in den Dateisystemdokumenten.

## Regeln für neue Kernelkomponenten

1. Niemals einen Shellpfad direkt an einen FAT-Treiber geben.
2. Erst in einen absoluten VFS-Pfad auflösen.
3. Handles auf jedem Fehlerpfad schließen.
4. Rückgabewerte vollständig prüfen.
5. Dateiinhalte nicht als NUL-terminiert annehmen.
6. Ein partiell erzeugtes Ziel bei fehlgeschlagener Kopie entfernen.
7. Dateisystemspezifische Annahmen auf den Adapter begrenzen.

## Tests

`test/test_fs_host.py` kompiliert Host-Harnesses für VFS und die
Dateisystemadapter. `test/test_shell_path_host.c` prüft die reine
Pfadnormalisierung. Die FAT32-Integration stellt unter anderem sicher, dass
`readdir` und `open` dieselbe Datei finden und dass unterschiedliche
Großschreibung akzeptiert wird.
