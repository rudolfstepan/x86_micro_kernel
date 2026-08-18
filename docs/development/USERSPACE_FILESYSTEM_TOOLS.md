# REIST Userspace-Dateisystemwerkzeuge

Stand: 18. August 2026

Diese Liste beschreibt den tatsächlich vorhandenen Userspace-Stand aus
`userspace/programs`, `userspace/bin`, `scripts/build_system_programs.py`,
dem Shell-Dispatcher und der VFS-/SDK-ABI. Ein Quellprogramm gilt erst dann
als Systemwerkzeug, wenn es auch in die Systemprogrammliste und damit in das
Boot-Image aufgenommen wird.

## Bereits vorhanden und als Systemprogramm gepackt

### Navigation und Anzeige

| Befehl | Funktion | Status |
|---|---|---|
| `cd`, `chdir` | Arbeitsverzeichnis wechseln | Shell-Builtin |
| `pwd` | Arbeitsverzeichnis anzeigen | `/bin/pwd.prg` |
| `ls`, `dir` | Verzeichnisinhalt anzeigen | `/bin/ls.prg`, `dir` Alias |
| `cat`, `type` | Datei lesen/anzeigen | `/bin/cat.prg`, `type` Alias |
| `more` | eigenständiges Pager-Programm | fehlt; `ls` besitzt nur begrenzte Ausgabe-Pause |
| `echo` | Text ausgeben | `/bin/echo.prg` |
| `cls`, `clear` | Bildschirm löschen | `/bin/cls.prg`, `clear` Alias |
| `history` | Shell-Verlauf anzeigen | Shell-Builtin, 32 Einträge |
| `help` | Shell-Hilfe anzeigen | Shell-Builtin |

### Datei- und Verzeichnisänderungen

| Befehl | Funktion | Status |
|---|---|---|
| `mkdir`, `md` | Verzeichnis anlegen | `/bin/mkdir.prg`, `md` Alias |
| `rmdir`, `rd` | leeres Verzeichnis entfernen | `/bin/rmdir.prg`, `rd` Alias |
| `del`, `erase` | einzelne Datei entfernen | `/bin/del.prg`, `erase` Alias |
| `copy` | einzelne Datei blockweise kopieren | `/bin/copy.prg`; kein rekursives Kopieren |
| `save` | neue Datei aus Argumenttext erzeugen | `/usr/bin/save.prg` |
| `edit` | interaktiver Editor | `/bin/edit.prg` |
| `basic` | BASIC-Interpreter | `/bin/basic.prg` |
| `rename`, `ren`, `mv` | Datei umbenennen | `/bin/rename.prg`, `ren`/`mv` Alias; FAT32 |

### Dateisystem-, Laufwerks- und Wartungswerkzeuge

| Befehl | Funktion | Status |
|---|---|---|
| `drives` | Ressourcen, Mounts und Laufwerksstatus | `/sbin/drives.prg` |
| `mount` | Filesystem auf Mountpoint einhängen | `/sbin/mount.prg` |
| `umount` | Mountpoint entfernen | `/sbin/umount.prg` |
| `fdisk` | MBR-Partition auf leerem Medium anlegen | `/sbin/fdisk.prg` |
| `format` | REIST-FAT12/FAT32 Quick-/Fullformat | `/sbin/format.prg` |
| `chkdsk` | begrenzter read-only Filesystemscan | `/sbin/chkdsk.prg` |
| `devctl` | Geräte-/Ressourcen-Lifecycle | `/sbin/devctl.prg` |
| `svcctl` | überwachte Dienste steuern | `/sbin/svcctl.prg` |
| `stat` | Typ, Größe und Unix-Zeitstempel anzeigen | `/bin/stat.prg` |
| `df` | freien Speicherplatz anzeigen | `/bin/df.prg` |
| `touch` | Zeitstempel aktualisieren oder Datei anlegen | `/bin/touch.prg` |
| `tree` | begrenzten Verzeichnisbaum anzeigen | `/bin/tree.prg` |
| `find` | begrenzte Dateisuche | `/bin/find.prg` |
| `rm -r` | rekursives Löschen mit Grenzen | `/bin/rm.prg` |

## Vorhandene SDK-/Kernel-Grundlagen

Die Userspace-ABI besitzt bereits die für mehrere fehlende Programme nötigen
Aufrufe:

- `x86os_open`, `x86os_read`, `x86os_write`, `x86os_close`
- `x86os_create`, `x86os_unlink`, `x86os_rename`
- `x86os_stat`, `x86os_readdir`, `x86os_readdir_batch`
- `x86os_getcwd`, `x86os_chdir`, `x86os_mkdir`, `x86os_rmdir`
- `x86os_fsync` und `x86os_space`
- `x86os_touch` (Syscall 108) für FAT-Zeitstempel
- VFS `create`, `delete`, `rename`, `stat`, `readdir`, `mkdir`, `rmdir`
- VFS `touch`; `vfs_dir_entry_t` liefert `create_time`, `modify_time` und
  `access_time` als Sekunden seit 1970-01-01.

Damit benötigt `rename.prg` keine neue Syscall- oder SDK-ABI.
Die Implementierung muss nur Argumente, absolute/relative Pfade,
Laufwerksgrenzen und Fehlercodes sauber behandeln.

## Filesystem-Abdeckung der vorhandenen Mutation

| Operation | FAT12 | FAT32 | EXT2 | Bemerkung |
|---|---:|---:|---:|---|
| öffnen/lesen | ja | ja | ja | über VFS/SDK |
| erstellen/schreiben | ja | ja | begrenzt | EXT2 ohne REIST-Persistenzgarantie |
| `mkdir` | ja | ja | ja | Adapter vorhanden |
| `rmdir` | ja | ja | ja | nur leere Verzeichnisse |
| `del`/unlink | ja | ja | ja | Adapter vorhanden |
| `rename` | nein | ja | nein | FAT12/EXT2 liefern unsupported |
| Zeitstempel lesen | ja | ja | ja | FAT-Auflösung und FAT-Zugriffsdatum bleiben erhalten |
| `touch` | ja | ja | nein | EXT2-Adapter ist read-only |
| `fsync` | REIST-spezifisch | REIST-spezifisch | begrenzt | kein allgemeines Persistenzversprechen |

Wichtig: Der generische VFS-Rename-Pfad und `x86os_rename()` sind vorhanden.
`rename.prg` meldet FAT12/EXT2 als nicht unterstützt, weil deren Adapter
keinen atomaren Rename-Vertrag besitzen. FAT32 erlaubt nur die im Adapter
validierten Dateioperationen; Verzeichnisse und unsichere Zielzustände werden
fail-closed abgelehnt.

## Umgesetzte Reihenfolge und verbleibende Folgearbeiten

### P0 – umgesetzt

1. `rename.prg` mit Alias `ren` und optionalem `mv`-Alias.
   - exakt zwei Pfade akzeptieren
   - `x86os_rename()` verwenden
   - Quelle und Ziel vorab per `stat` prüfen
   - Cross-Mount- und Cross-Filesystem-Rename ablehnen
   - vorhandene Ziele nicht still überschreiben
   - FAT12/EXT2-`unsupported` verständlich melden
2. `stat.prg` für Typ, Größe und die drei VFS-Zeitstempel.
3. `df.prg` auf Basis von `x86os_space()` und Laufwerks-/Ressourcenstatus.
4. `touch.prg` für neue leere Dateien und die Aktualisierung von mtime/atime.

### P1 – umgesetzt oder als Alias abgedeckt

1. `mv` ist ein Shell-Alias für `rename`; ein separates Programm ist nicht
   erforderlich, solange FAT32 dieselbe Rename-Semantik anbietet.
2. `cp` ist ein Shell-Alias für `copy`; rekursives Kopieren bleibt offen.
3. `tree.prg` mit fester Tiefe und fester Ausgabegrenze.
4. `find.prg` mit fester Traversierungsgrenze, ohne unbounded Rekursion.
5. `rm.prg` mit explizitem `--recursive` und Abbruch bei Fehlern.
6. `more.prg`/`head.prg`/`tail.prg` für große Dateien.

### P2 – verbleibend

- `chmod`, `chown` und `umask`: Es gibt noch kein ausformuliertes
  Userspace-Rechte-/Ownership-Modell.
- `ln`/Symlinks: VFS- und Filesystem-ABI stellen keinen Symlink-Vertrag bereit.
- `truncate`: Es fehlt ein sicherer Größenänderungsaufruf.
- Zeitstempelpflege ist für FAT12/FAT32 umgesetzt; FAT liefert Sekunden seit
  1970, wobei Schreibzeiten auf zwei Sekunden und Zugriffszeiten auf einen
  Kalendertag gerundet sind. Eine echte UTC-/Zeitzonenverwaltung bleibt offen.
- `fsck` mit Reparatur: `chkdsk` ist derzeit bewusst read-only.
- Archive (`tar`, `cpio`, `gzip`): noch kein priorisiertes Basiswerkzeug.

## Empfohlene Reihenfolge

```text
rename -> stat -> df -> touch -> mv/cp -> tree/find -> rm --recursive
```

Die erste Werkzeuggruppe ist implementiert und wird über das gemeinsame
Systemprogramm-Build sowie das Boot-Image gepackt. Die Timestamp-ABI ist
append-only als Syscall 108 ergänzt; alte Syscall-Nummern bleiben unverändert.
