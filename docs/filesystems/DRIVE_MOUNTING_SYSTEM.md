# Laufwerke, Mounts und Pfadzuordnung

Dieses Dokument beschreibt den aktuellen Laufwerksweg. Die frühere Shell mit
Prompt `hdd0>` und separaten FAT-Direktaufrufen wurde durch DOS-Laufwerke und
eine gemeinsame VFS-Pfadschicht ersetzt.

## Automatisches Mounten

Beim Kernelstart:

1. wird VFS initialisiert,
2. werden FAT32-, FAT12- und EXT2-Adapter registriert,
3. werden alle erkannten ATA- und Diskettenlaufwerke geprüft,
4. wird das erste erfolgreich gemountete Laufwerk unter `/` eingehängt,
5. erhalten weitere Laufwerke Mountpunkte wie `/mnt/hdd1` oder `/mnt/fdd0`,
6. wird das erste erfolgreiche Laufwerk zum aktiven Shell-Laufwerk.

ATA-Laufwerke werden anhand ihrer Metadaten als FAT32 oder EXT2 erkannt.
Disketten verwenden FAT12. `MOUNT` bleibt zur manuellen Wiederholung oder für
später erkannte Laufwerke vorhanden, ist im normalen VMware-Start aber nicht
nötig.

## Namen und Buchstaben

| Gerät | Nativer Name | DOS-Name |
|---|---|---|
| erste Diskette | `fdd0` | `A:` |
| zweite Diskette | `fdd1` | `B:` |
| erste Festplatte | `hdd0` | `C:` |
| zweite Festplatte | `hdd1` | `D:` |

Weitere Festplatten werden fortlaufend ab `E:` zugeordnet. `DRIVES` zeigt
erkannte Geräte, Typ, Modell, Mountpunkt und aktiven Zustand.

## Shell- und VFS-Sicht

Die Shell hält pro Laufwerk einen Pfad relativ zu dessen Wurzel. Für den
Benutzer erscheint dieser DOS-artig:

```text
C:\DOCS>
```

Intern verbindet der Resolver den gemerkten Laufwerkspfad mit dem echten
VFS-Mountpunkt:

```text
Benutzer: D:\TOOLS\APP.PRG
Laufwerk: hdd1
Laufwerkspfad: /TOOLS/APP.PRG
Mountpunkt: /mnt/hdd1
VFS-Pfad: /mnt/hdd1/TOOLS/APP.PRG
```

Kein Shellbefehl darf selbst Mountpunkte zusammenbauen oder direkt eine
globale FAT-Instanz auswählen. Dafür existiert
`kernel/shell/path_resolver.c`.

## Wechsel und Direktzugriff

```text
C:\DOCS> D:
D:\> hdd0:
C:\DOCS>
```

Die Schreibweisen `C:`, `hdd0:` und `hdd0` sind als reiner Laufwerkswechsel
zulässig. Pfade können ein anderes Laufwerk adressieren:

```text
C:\> DIR D:\
C:\> TYPE D:\INFO.TXT
C:\> COPY C:\HELLO.PRG D:\HELLO.PRG
C:\> CD D:\TOOLS
D:\TOOLS>
```

Nur ein erfolgreicher `CD`-Aufruf verändert den aktiven Pfad. Ein fehlerhafter
Zugriff hinterlässt Laufwerk und aktuelles Verzeichnis unverändert.

## Pfadregeln

- `/` und `\` sind Trennzeichen.
- Ein führendes Trennzeichen bedeutet Wurzel des gewählten Laufwerks.
- Ohne führendes Trennzeichen wird relativ zum gemerkten Verzeichnis
  aufgelöst.
- `.` bleibt im aktuellen Verzeichnis.
- `..` steigt eine Ebene auf und wird an der Laufwerkswurzel begrenzt.
- DOS-Buchstaben, `hddN:/...`, `fddN:/...` und `/hddN/...` werden unterstützt.
- Die maximale normalisierte Pfadlänge beträgt 255 Zeichen plus NUL.
- Dateisystemgrenzen wie FAT-8.3-Namen bleiben zusätzlich gültig.

## Bootimage

Im nativen 64-MiB-Image liegt die FAT32-Datenpartition ab LBA 8192. Sie wird
im Ein-Platten-VMware-Paket als `hdd0` erkannt und unter `/` gemountet. Damit
entspricht sie im Prompt `C:`. Das Image enthält mindestens `README.TXT` und
das beim Build erzeugte Programm, standardmäßig `HELLO.PRG`.

## Fehlerbilder

- **Kein aktives Laufwerk:** `DRIVES` prüfen; Boot- oder ATA-Meldungen im
  seriellen Log auswerten.
- **Laufwerk nicht gemountet:** `MOUNT hdd0` versuchen und Dateisystem prüfen.
- **DIR sieht Datei, TYPE nicht:** gilt als Regression; beide Befehle müssen
  denselben VFS-Resolver verwenden und werden durch Hosttests abgedeckt.
- **Pfad zu lang/ungültig:** Eingabe wird abgelehnt, nicht still gekürzt.
- **Andere Großschreibung auf FAT:** Suche ist case-insensitiv; das erzeugte
  Image schreibt 8.3-Namen in Großbuchstaben.
