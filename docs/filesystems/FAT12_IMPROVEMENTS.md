# FAT12-Status und Resilienz

Stand: 23. August 2026.

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

Die Pakete `S0.3c-6f1` bis `S0.3c-6f5` sowie der erste
`S0.3c-6f6`-Wartungsschritt sind umgesetzt:

1. verifiziertes Undo-Journal mit redundanten CRC-Headern und Recovery vor
   veränderlicher Metadatennutzung
2. begrenzte Defektbestätigung, `0xFF7`-Markierung und redundante Remaptabelle
   mit fest reservierten Ersatzsektoren
3. persistente, sequenzierte und CRC-geschützte Replikate für die feste Liste
   kritischer 8.3-Dateien
4. geordnete Mutation: Daten, beide FATs, Verzeichniseintrag,
   Replikatpublikation und erst zuletzt Journal-`CLEAN`
5. deterministische Host-Fehlermatrix über 29 stabile Persistenzbarrieren und
   QEMU-FDD-Reconnect-Nachweis ohne Veränderung des Referenzabbilds
6. capability-gebundene BPB-/FAT-Spiegelanalyse und bestätigte Reparatur genau
   einer strukturell beschädigten FAT-Kopie unter exklusivem Maintenance-Lease
7. feste Cluster-Owner-Map, begrenzte Root-/Unterverzeichnisqueue und Diagnose
   von ungültigen Links, Loops, Crosslinks, kurzen/überlangen Ketten und Orphans;
   eindeutig überlange reguläre Dateiketten können journalisiert gekürzt werden
8. konservative Reparatur kurzer, normal EOC-terminierter regulärer Dateien:
   Die Directory-Größe wird journalisiert auf die tatsächlich lesbare
   Kettenkapazität reduziert, ohne Cluster oder verlorene Daten zu erfinden

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
C:\> CHKDSK [pfad]
C:\> CHKDSK --fat12 1
C:\> CHKDSK --fat12 1 --repair --confirm
C:\> CHKDSK --fat12 1 --repair-chains --confirm
C:\> CHKDSK --fat12 1 --repair-short --confirm
FDISK
```

Der Pfadmodus von `CHKDSK.PRG` bleibt ein begrenzter read-only VFS-Scan. Der
FAT12-Modus sendet ausschließlich versionierte Check-/Repair-Requests; das
Programm besitzt weder Blockzugriff noch FDC-, DMA- oder Portautorität. Der
Storage-Dienst akzeptiert die Reparatur nur auf einem markierten REIST-FAT12-
Medium, wenn genau eine FAT-Kopie strukturell gültig ist. Nach exklusivem
Unmount und erneuter Diagnose werden alle neun alten Zielsektoren im
Undo-Journal gesichert, die beschädigte Kopie mit verifiziertem Readback
ersetzt und erst danach `CLEAN` veröffentlicht. Uneindeutigkeit lässt das
Medium unverändert beziehungsweise fail-closed zur Inspektion zurück.
Der read-only Check traversiert zusätzlich alle erreichbaren Clusterketten aus
Root und höchstens 256 Unterverzeichnissen. Eine feste Owner-Map erkennt Loops,
Crosslinks, kurze/überlange Ketten und nicht referenzierte Allokationen ohne
Heap. Der Chain-Reparaturmodus ist bewusst konservativ: Nur wenn der gesamte
Datenträger außer normal EOC-terminierten, eindeutig besessenen Überlängen
sauber ist, werden die überzähligen Tails freigegeben. Geänderte Sektoren beider
FAT-Kopien werden vorher im Undo-Journal gesichert und danach vollständig neu
gescannt.
`--repair-short --confirm` ist ebenso eng begrenzt: Der Gesamtscan darf nur
kurze Ketten enthalten, jede betroffene reguläre Datei muss eine eindeutig
besessene, normal terminierte Kette haben, und alle Directory-Sektoren werden
vor der Größenkorrektur im Undo-Journal gesichert. Startcluster null,
Crosslinks, Loops, freie/bad Links, Orphans oder gemischte Diagnosen verhindern
die Mutation. Ein sauberer Vollscan ist Voraussetzung für Journal-`CLEAN`.
`FDISK.PRG` kann auf explizit freigegebenen, leeren ATA-/AHCI-Medien eine
validierte MBR-Partition erzeugen. Eine Diskette bleibt eine partitionslose
Superfloppy und wird von `FDISK` niemals partitioniert.

## Noch offen

- reale FDD-/VMware-Power-Loss- und Reconnect-Matrix über die bereits
  deterministisch geprüften Veröffentlichungsstufen
- CHKDSK-Reparatur von Crosslinks, Loops, Orphans, allgemeinen
  Verzeichnisschäden, Journal, Remap- und Defektsektorkarte
- QEMU-Laufzeitnachweis für Maintenance-Lease, Unmount, FAT-Spiegel-Reparatur,
  Verify und kontrollierten Remount
- breitere echte FDD-/Medienfehler- und Langzeitmatrix

`FAT12_ANALYSIS.md` ist ein historischer Bericht und beschreibt nicht den
heutigen Implementierungsstand.
