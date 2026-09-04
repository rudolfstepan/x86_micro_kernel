# REIST Userspace-Dateisystemwerkzeuge

Stand: 3. September 2026

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
| `readlink` | Ziel eines nativen symbolischen Links anzeigen | `/bin/readlink.prg`; EXT2, fester Ring-3-Storage-Frame |
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
| `notepad` | grafischer Editor | `/usr/gui/bin/notepad.prg`; große Dateien über begrenzte Piece Table und fensterweisen Ring-3-VFS-Zugriff |
| `basic` | BASIC-Interpreter | `/bin/basic.prg` |
| `rename`, `ren`, `mv` | Datei umbenennen | `/bin/rename.prg`, `ren`/`mv` Alias; FAT32 sowie gleichverzeichnisige EXT2-Symlinks und reguläre Dateien ohne Ersetzen |
| `ln -s` | nativen symbolischen Link erzeugen | `/bin/ln.prg`; begrenzter EXT2-Schnitt, keine Hard Links |

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
- Ring-3-Clients `reist_vfs_symlink`, `reist_vfs_readlink`,
  `reist_vfs_lstat`, `reist_vfs_unlink`, `reist_vfs_rename` und
  `reist_vfs_file_open_flags(..., O_NOFOLLOW, ...)`
- VFS `create`, `delete`, `rename`, `stat`, `readdir`, `mkdir`, `rmdir`
- VFS `touch`; `vfs_dir_entry_t` liefert `create_time`, `modify_time` und
  `access_time` als Sekunden seit 1970-01-01.

`rename.prg` verwendet für Lstat und die begrenzte EXT2-Namespacemutation den
Storage-Dienst. Ausschließlich dessen `EOPNOTSUPP` erlaubt den Rückfall auf
`x86os_rename()`, womit die vorhandene FAT32-Semantik erhalten bleibt.

`EDIT.PRG` liest vorhandene Dokumente über genau ein generationgebundenes
Ring-3-VFS-Objekt mit ausschließlich `READ|STAT`. Die feste 51200-Byte-Grenze,
200 Zeilen, 256 Bytes je Zeile, CRLF-Normalisierung und der 256-Byte-
Transferpuffer bleiben erhalten. Eine absolute monotone 60-Sekunden-Frist
begrenzt Open, Fstat, Reads, EOF und erfolgreichen Close; Fehlercleanup erhält
einen getrennten Ein-Millisekunden-Closeversuch. Der bestehende atomare
Tempfile-/`fsync`-/Rename-Speicherpfad verwendet weiterhin die bisherige
mutierende Deskriptor-ABI.

`LN.PRG` akzeptiert ausschließlich `ln -s <target> <link-path>` und erzeugt
keine Hard Links. `READLINK.PRG` gibt die gespeicherten Zielbytes aus, ohne den
letzten Link zu folgen. Beide Programme verwenden nur den append-only
Storage-Service-Vertrag; der alte Ring-0-EXT2-Parser erhält weder Auflösung
noch Schreiblogik. Ziele sind in dieser ersten Teilmenge auf 191 druckbare
ASCII-Bytes begrenzt. EXT2-Erzeugung setzt das provisionierte 26-Sektor-
Undo-Journal sowie vorhandenen Directory-Slack in einem einzigen 512-Byte-
Publikationssektor voraus. FAT12/32 melden vor jeder Wirkung
`EOPNOTSUPP`. Relative und absolute Ziele, Ketten, Dangling Links und Zyklen
werden unter festen Hop-, Komponenten-, I/O-, Retry- und Deadlinegrenzen
behandelt.

`DEL.PRG` und nichtrekursives `RM.PRG` entfernen über denselben
generationgebundenen Namespace-Client die finale EXT2-Symlinkkomponente, ohne
deren Ziel zu berühren, oder eine reguläre Datei mit Linkzähler eins und
höchstens 64 validierten direkten/einfach-indirekten Allokationen. Sparse-,
EA-, Sonderflag-, Double-/Triple-Indirect- und geschützte Blocklayouts werden
abgewiesen. `RENAME.PRG` erhält bei Symlinks Inode und Ziel und bei
regulären Dateien den bytegleichen Inode samt unveränderten Datenblockzeigern.
Es akzeptiert nur einen freien Namen im selben Verzeichnis. Ein längerer Name
darf vorhandenen freien EXT2-Recordplatz oder `rec_len`-Slack verwenden, wenn
Quellentfernung, Record-Split sowie vollständiger neuer Header und Name in
demselben 512-Byte-Publikationssektor liegen. Alle drei Werkzeuge
verwenden Legacy-Unlink/Rename ausschließlich nach `EOPNOTSUPP`; andere
Service-, Medien-, Deadline- oder Recoveryfehler bleiben sichtbar und lösen
keine zweite Mutation aus.

## Filesystem-Abdeckung der vorhandenen Mutation

| Operation | FAT12 | FAT32 | EXT2 | Bemerkung |
|---|---:|---:|---:|---|
| öffnen/lesen | ja | ja | ja | über VFS/SDK |
| erstellen/schreiben | ja | ja | nein | generischer Legacy-EXT2-Adapter ist read-only |
| `mkdir` | ja | ja | nein | EXT2-Adapter ist read-only |
| `rmdir` | ja | ja | nein | EXT2-Adapter ist read-only |
| `del`/unlink | ja | ja | Symlinks/Dateien, begrenzt | EXT2 finaler nativer Link oder reguläre Standarddatei bis 64 Allokationen |
| `rename` | nein | ja | Symlinks/Dateien, begrenzt | EXT2 no-replace im selben Verzeichnis, bei Wachstum nur innerhalb eines Publikationssektors |
| Zeitstempel lesen | ja | ja | ja | FAT-Auflösung und FAT-Zugriffsdatum bleiben erhalten |
| `touch` | ja | ja | nein | EXT2-Adapter ist read-only |
| `fsync` | REIST-spezifisch | REIST-spezifisch | nein | kein generischer EXT2-Schreibdeskriptor |
| symbolischer Link | `EOPNOTSUPP` | `EOPNOTSUPP` | ja, begrenzt | native Fast-/Block-Symlinks; 26-Sektor-Undo-Journal |

Wichtig: Der generische VFS-Rename-Pfad und `x86os_rename()` bleiben für FAT32
vorhanden. Der Ring-3-Schnitt deckt auf EXT2 native Symlinks und das begrenzte
Rename und Unlink regulärer Standarddateien ab; Create, Write, größere oder
nichtstandardmäßige Dateien, Verzeichnisse, Cross-Directory und Ersetzen sind
weiterhin nicht unterstützt.
FAT32 erlaubt nur die im Adapter validierten Dateioperationen;
Verzeichnisse und unsichere Zielzustände werden fail-closed abgelehnt.

## Umgesetzte Reihenfolge und verbleibende Folgearbeiten

### P0 – umgesetzt

1. `rename.prg` mit Alias `ren` und optionalem `mv`-Alias.
   - exakt zwei Pfade akzeptieren
   - Ring-3-Namespace zuerst, `x86os_rename()` nur bei `EOPNOTSUPP`
   - Quelle und Ziel vorab per `lstat` prüfen
   - Cross-Mount- und Cross-Filesystem-Rename ablehnen
   - vorhandene Ziele nicht still überschreiben
   - FAT12 sowie nicht abgedeckte EXT2-Objekte verständlich ablehnen
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
- Hard Links (`ln` ohne `-s`) bleiben ohne öffentlichen Vertrag.
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
