# FAT12-Status und Resilienz

Stand: 16. August 2026.

FAT12 dient vor allem bootfähigen 1,44-MB-Disketten und läuft vollständig über
VFS und die gemeinsame Blockgeräteschicht. Klassische fremde FAT12-Medien
bleiben auf einem Kompatibilitätspfad; zusätzliche REIST-Garantien werden nur
für explizit markierte, neu formatierte Medien aktiviert.

## Implementierter Grundumfang

- strenge BPB-/Geometrie- und Clusterbereichsprüfung
- Lesen und Schreiben von Dateien und Verzeichnissen
- beide FAT-Kopien, Kettenallokation/-freigabe und begrenzte Scans
- Einzelsektor-Fallback, wenn echte FDD-Hardware einen Batchzugriff ablehnt
- Quarantäne und kontrollierte Requalifizierung nach Medienfehlern/Hotplug

## REIST-FAT12

Die Pakete `S0.3c-6f1` bis `S0.3c-6f4` sind umgesetzt:

1. verifiziertes Undo-Journal mit redundanten CRC-Headern und Recovery vor
   veränderlicher Metadatennutzung
2. begrenzte Defektbestätigung, `0xFF7`-Markierung und redundante Remaptabelle
   mit fest reservierten Ersatzsektoren
3. persistente, sequenzierte und CRC-geschützte Replikate für die feste Liste
   kritischer 8.3-Dateien
4. geordnete Mutation: Daten, beide FATs, Verzeichniseintrag,
   Replikatpublikation und erst zuletzt Journal-`CLEAN`

Kapazität, Sektorarithmetik, Retryzahlen und Recoveryarbeit sind fest begrenzt.
Uneindeutige Header, erschöpfte Tabellen oder fehlgeschlagener Readback führen
zu Write-Fencing statt zu blindem Weiterarbeiten.

## Werkzeuge

`DRIVES` liefert die Resource-ID. Formatierung ist absichtlich explizit und
akzeptiert derzeit nur eine verfügbare FDD-Ressource:

```text
C:\> DRIVES
C:\> FORMAT --reist-fat12 1 --confirm
```

Die `1` ist nur ein Beispiel. `FORMAT.PRG` reicht einen generationgebundenen,
30 Sekunden begrenzten Auftrag an `STORAGE.PRG` weiter. Der Dienst erzeugt
Metadaten und beide FATs, schreibt den Bootsektor zuletzt und bestätigt Erfolg
erst nach Readback. Bei unbekanntem Abschluss muss das Medium read-only
bleiben.

```text
CHKDSK [pfad]
FDISK
```

`CHKDSK.PRG` ist ein begrenzter read-only VFS-Scan; es repariert nichts.
`FDISK.PRG` zeigt Inventar, verändert aber keine Partitionen. Eine Diskette
besitzt ohnehin keine MBR-Partitionstabelle.

## Noch offen

- aktives Paket `S0.3c-6f5`: deterministische Write-/Power-Loss-Fehlermatrix
  für jede persistente Veröffentlichungsstufe
- kontrollierter, journalisierter CHKDSK-Reparaturmodus
- vollständiger Maintenance-Lease-, Unmount-, Repair-, Verify- und
  Remountnachweis
- breitere echte FDD-/Medienfehler- und Langzeitmatrix

`FAT12_ANALYSIS.md` ist ein historischer Bericht und beschreibt nicht den
heutigen Implementierungsstand.
