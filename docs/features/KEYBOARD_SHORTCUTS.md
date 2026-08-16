# Tastatur und Shell-Zeilenbearbeitung

Stand: 16. August 2026.

Der i8042-Treiber initialisiert die PS/2-Tastatur begrenzt, dekodiert rohes
Scan-Set 2 und speist ausschließlich echte PS/2-Ereignisse in die Console-
Eingabe. IRQ1 ist der Normalpfad; ein Polling-Fallback verhindert verlorene
Wakeups auf problematischer realer Hardware. COM1 injiziert keine Tastencodes
mehr und bleibt Diagnoseausgabe.

NumLock, CapsLock und die zugehörigen Make-/Break-Zustände werden im Treiber
geführt. NumLock steuert Ziffern beziehungsweise Navigation des Nummernblocks;
die LED-Aktualisierung ist Teil der i8042-Kommunikation.

## Zeilenbearbeitung

| Taste | Wirkung |
|---|---|
| Links/Rechts | Cursor bewegen |
| Hoch/Runter | Verlauf durchsuchen |
| Pos1/Ende | Anfang/Ende der Zeile |
| Entf/Rücktaste | Zeichen löschen |
| Tab | Befehl, Programm, Datei oder Verzeichnis vervollständigen |
| `Ctrl+C` | aktuelle Eingabe abbrechen |
| `Ctrl+L` | Bildschirm löschen und Eingabe neu zeichnen |
| `Ctrl+U` | gesamte Zeile löschen |
| `Ctrl+K` | ab Cursor bis Zeilenende löschen |

Tab durchsucht am Zeilenanfang Built-ins, Aliase und `.PRG`-Programme im
aktuellen Verzeichnis und in `PATH`. In Argumenten werden VFS-/DOS-Pfade
vervollständigt; eindeutige Verzeichnisse erhalten einen Backslash.

Die Shell speichert bis zu 50 nicht leere Befehle und entfernt unmittelbar
aufeinanderfolgende Duplikate. Beim ersten Pfeil-hoch merkt sie sich den noch
nicht ausgeführten Entwurf.

## Verifikation und Grenzen

`scripts/run_qemu_ps2_smoke.py` prüft NumLock und Texteingabe bis in die
Ring-3-Shell. Die frühere `PS2 TRACE`-Ausgabe bei jedem Tastendruck ist entfernt.
Reale PS/2-Eingabe wurde auf Zielhardware beobachtet. Internationale Layouts,
Insert, Page Up und Page Down besitzen noch keinen vollständigen
Shell-Bearbeitungsvertrag.

`KEYBOARD_ANALYSIS.md` und `KEYBOARD_IMPROVEMENTS.md` sind historische
Arbeitsberichte.
