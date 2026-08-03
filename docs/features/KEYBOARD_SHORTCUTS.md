# Tastatur und Shell-Zeilenbearbeitung

Der PS/2-Tastaturtreiber übersetzt erweiterte Tasten in ANSI-artige Sequenzen,
die von der Shell einheitlich ausgewertet werden. Dadurch gehören
Scancode-Decodierung und Eingabepuffer nicht mehr zwei konkurrierenden
Schichten.

## Navigation

| Taste | Wirkung |
|---|---|
| Pfeil links/rechts | Cursor bewegen |
| Pfeil hoch/runter | älteren/neueren Befehl wählen |
| Pos1 | zum Eingabeanfang |
| Ende | zum Eingabeende |
| Entf | Zeichen unter dem Cursor löschen |
| Rücktaste | Zeichen vor dem Cursor löschen |

Beim ersten Druck auf Pfeil hoch merkt sich die Shell die noch nicht
ausgeführte Eingabe. Nach dem Zurückblättern zum neuesten Eintrag stellt
Pfeil runter diesen Entwurf wieder her.

## Steuerkombinationen

| Kombination | Wirkung |
|---|---|
| `Ctrl+C` | Eingabe abbrechen und neuen Prompt anzeigen |
| `Ctrl+D` | bei leerer Zeile Hinweis auf `EXIT` |
| `Ctrl+L` | Bildschirm löschen, aktuelle Eingabe erhalten |
| `Ctrl+U` | ganze Eingabezeile löschen |
| `Ctrl+K` | ab Cursor bis Zeilenende löschen |

## Verlauf

Die Shell speichert bis zu 50 nicht leere Befehle. Direkt aufeinanderfolgende
Duplikate werden nur einmal abgelegt. `HISTORY` zeigt den Inhalt in
chronologischer Reihenfolge.

## Aktuelle Grenzen

- Tab-Vervollständigung ist noch nicht implementiert; Tab wird ignoriert.
- Insert, Page Up und Page Down werden vom Treiber erkannt, besitzen in der
  Shell aber keine Bearbeitungsfunktion.
- Der Editor arbeitet mit einer einzelnen Zeile bis maximal 255 Zeichen.
- Vollständige internationale Tastaturlayouts sind nicht dokumentiert.

Die älteren Dateien `KEYBOARD_ANALYSIS.md` und `KEYBOARD_IMPROVEMENTS.md`
sind historische Analyseprotokolle.
