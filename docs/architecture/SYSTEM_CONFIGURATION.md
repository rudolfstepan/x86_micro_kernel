# System- und Programmkonfiguration

Stand: 20. August 2026.

REIST OS verwendet fuer systemweit veraenderbare Einstellungen das Verzeichnis
`/etc/reist`. Die Konfiguration bleibt damit von Programmen unter `/usr`,
Laufzeitdaten unter `/var` und spaeteren benutzerspezifischen Einstellungen
getrennt.

## Verzeichnisvertrag

| Pfad | Zweck | Schreibzugriff |
|---|---|---|
| `/etc/reist/system.conf` | Sprache, Ersatzsprache und Zeitzone | Systemverwaltung |
| `/etc/reist/input.conf` | Tastatur, Maus und Eingabeverhalten | Systemverwaltung |
| `/etc/reist/desktop.conf` | Desktop-, Theme- und Explorer-Vorgaben | Systemverwaltung |
| `/etc/reist/filetypes.conf` | Dateiendungen und zugeordnete GUI-Programme | Systemverwaltung |
| `/usr/share/reist/defaults/` | spaetere unveraenderliche Herstellerwerte | nur Systemabbild |
| `/var/lib/reist/` | spaetere dauerhafte Laufzeitdaten, keine Konfiguration | jeweiliger Dienst |
| `$HOME/.config/reist/` | spaetere benutzerspezifische Ueberschreibungen | jeweiliger Benutzer |

Programmbezogene systemweite Dateien erhalten spaeter einen eindeutigen,
stabilen Namen unter `/etc/reist`, zum Beispiel
`/etc/reist/editor.conf`. Programme duerfen keine privaten Formate direkt in
`/etc`, `/usr` oder `/var` verteilen.

## Dateiformat Version 1

Die mitgelieferten Dateien verwenden ein absichtlich kleines, menschenlesbares
ASCII-Format:

```text
# Kommentar
schema=reist.input/1
keyboard.layout=de
```

- Eine Zeile enthaelt genau `schluessel=wert`; Leerzeichen sind Bestandteil
  des Werts und werden nicht stillschweigend entfernt.
- Die erste wirksame Zeile muss `schema=<namespace>/<version>` sein.
- Leere Zeilen und mit `#` beginnende Kommentarzeilen sind erlaubt.
- Doppelte Schluessel, ungueltige Zeichen, unbekannte Schema-Versionen und
  abgeschnittene Zeilen machen die gesamte Datei ungueltig.
- Der gemeinsame Parser muss Zeilen-, Schluessel-, Wert- und Dateigroessen
  fest begrenzen und erst nach vollstaendiger Validierung Werte veroeffentlichen.
- Unbekannte Schluessel einer bekannten Version werden fuer Vorwaerts-
  kompatibilitaet ignoriert und bei einer spaeteren Aenderung nicht geloescht.
- Einstellungswerkzeuge schreiben ueber eine neue Datei und einen atomaren
  Austausch. Eine teilweise geschriebene Konfiguration darf nie aktiv werden.

## Aufloesung und sichere Rueckfallwerte

Die vorgesehene Reihenfolge lautet Herstellerwert, `/etc/reist`, danach eine
optionale benutzerspezifische Datei. Eine spaetere Ebene ueberschreibt nur
validierte, bekannte Schluessel. Fehlerhafte oder fehlende Dateien fuehren zu
fest eingebauten sicheren Rueckfallwerten und einer begrenzten Diagnose, nicht
zu teilweise uebernommenen Einstellungen.

## Dateizuordnungen

`filetypes.conf` verwendet `schema=reist.filetypes/1`. Danach bildet jede
Zeile genau eine kleingeschriebene Erweiterung auf einen kanonischen absoluten
PRG-Pfad ab:

```text
schema=reist.filetypes/1
.txt=/usr/gui/bin/notepad.prg
.log=/usr/gui/bin/notepad.prg
.conf=/usr/gui/bin/notepad.prg
.md=/usr/gui/bin/notepad.prg
.wav=/usr/gui/bin/soundplayer.prg
.bmp=/usr/gui/bin/imageviewer.prg
.gif=/usr/gui/bin/imageviewer.prg
```

Der Desktop liest hoechstens 4096 Bytes und veroeffentlicht maximal 16
Zuordnungen erst nach vollstaendiger Validierung. Erweiterungen werden beim
Lookup ASCII-unabhaengig von Gross-/Kleinschreibung verglichen. Programme
muessen absolute `.prg`-Pfade ohne Traversal oder leere Pfadsegmente sein. Eine
ungueltige Datei aktiviert keine teilweise gelesene Zuordnung und erzeugt
einen modalen Desktopfehler. Ausfuehrbare `.prg`-Dateien bleiben eine feste
Dateiklasse und werden nicht durch Konfiguration umgedeutet. Das zugeordnete
Programm erhaelt den kanonischen Dateipfad als `argv[1]`.

Der aktuelle Stand verpackt die vier systemweiten Standarddateien in das
FAT-Systemabbild. `libreistos` enthaelt einen gemeinsamen Parser mit 4096 Byte
Dateigroesse, 160 Byte Zeilenlaenge, 32 Eintraegen sowie festen Schluessel- und
Wertgrenzen. `config.prg` ist die einzige von der grafischen Systemsteuerung
verwendete Mutationsgrenze. Jede Anforderung laeuft in einer frischen
Ring-3-Prozessgeneration und akzeptiert nur die dokumentierten Dateien,
Schluessel und Werte. Sie validiert zuerst die vollstaendige vorhandene Datei
und publiziert dann ueber `TEMP -> fsync -> close -> rename`. Unbekannte
Eintraege einer gueltigen Version bleiben beim Umschreiben erhalten.

`control.prg` zeigt Tastatur, Maus, System und Desktop als vier feste Applets
in einem getrennten Surface-Fenster. Ohne gueltige Konfiguration oder
erfolgreiche Schreibberechtigung bleibt es im Nur-Lese-Modus. Die aktuell
ausgewaehlten Werte werden persistent geaendert, aber noch nicht von allen
laufenden Treibern und Diensten dynamisch neu geladen; das Fenster bezeichnet
solche Aenderungen daher ehrlich als nach einem Neustart wirksam. Das
Vorhandensein einer Datei behauptet keine bereits erfolgte Live-Anwendung.
