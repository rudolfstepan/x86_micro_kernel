# System- und Programmkonfiguration

Stand: 19. August 2026.

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

Der aktuelle Stand verpackt die drei systemweiten Standarddateien in das
FAT-Systemabbild. Der gemeinsame Ring-3-Parser, ein Systemdienst fuer
berechtigte Aenderungen sowie grafische Werkzeuge fuer Sprache, Tastatur und
Maus sind getrennte Folgeschritte. Bis dahin bleiben die bestehenden
festen Laufzeitwerte massgeblich; das Vorhandensein einer Datei behauptet noch
nicht, dass ein Treiber sie bereits auswertet.
