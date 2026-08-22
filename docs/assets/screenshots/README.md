# Automatisch erzeugte QEMU-Screenshots

Stand: 22. August 2026.

Dieser Ordner enthält bewusst versionierte Laufzeitaufnahmen für die
öffentliche Repository-Dokumentation. Sie sind keine Mock-ups und werden nur
verwendet, wenn eine sichtbare Systemeigenschaft dadurch schneller erfassbar
wird als durch Text.

| Datei | Belegter Zustand |
|---|---|
| `reist-desktop.png` | `desktop.prg` aktiviert Grafik aus der normalen VGA-Shell und zeigt den Explorer-Arbeitsbereich. |
| `reist-desktop-apps.png` | Der Surface-Probelauf veröffentlicht echte, getrennte Ring-3-Fenster; sichtbar ist der Image Viewer vor dem Explorer. |
| `reist-notepad.png` | Der Notepad-Probelauf öffnet `/readme.txt` als echtes, verschieb- und skalierbares Surface-Fenster. |
| `reist-trash-context.png` | Der Snapshot-Probelauf verschiebt ein reales, restorable Dateiobjekt über den Produktionspfad in den Papierkorb, öffnet dessen Explorerfenster und zeigt die aktive Wiederherstellungsaktion samt Rechtsklick-Kontextmenü. |
| `reist-trash-confirm.png` | Derselbe reale Papierkorbzustand mit applikationsmodaler Sicherheitsfrage; die nicht-destruktive Antwort `Nein` besitzt den Standardfokus. |

Alle Bilder entstehen mit einem begrenzten QEMU-Lauf. Der Runner verlangt
`DESKTOP_OK` und prüft sichtbare Menüschrift, bevor er QEMUs P6-Ausgabe in PNG
konvertiert. Der normale Desktop-Probelauf weist zusätzlich die Rückkehr in
die VGA-Shell nach; die Surface- und Papierkorb-Probeläufe beenden den mit
QEMUs `-snapshot` gestarteten, unveränderlichen Gast unmittelbar nach der
gültigen Aufnahme. Die Papierkorb-Probes erzeugen ihre Datei begrenzt im Gast
und führen `desktop_trash_move` aus; sie zeichnen keinen künstlichen
Katalogeintrag.

```powershell
.\scripts\capture-documentation.ps1
```

Für ein bereits aktuelles QEMU-VGA-Image kann der inkrementelle Build
übersprungen werden:

```powershell
.\scripts\capture-documentation.ps1 -SkipBuild
```

Neue Bilder erhalten einen stabilen, beschreibenden Namen und genau einen
fachlich passenden Hauptverweis. Status- oder ABI-Informationen dürfen nicht
allein in einem Screenshot stehen.
