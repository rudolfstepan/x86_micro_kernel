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
9. bestätigtes Zurückgewinnen unerreichbarer Clusterallokationen, ausschließlich
   bei einer reinen Orphan-Diagnose und ohne Bad- oder erreichbare Cluster zu
   verändern
10. bestätigte Reparatur reiner Schleifen in regulären Dateiketten, sofern die
    eindeutigen Cluster für die deklarierte Dateigröße ausreichen; der
    Sollpräfix bleibt erhalten und nur der Schleifensuffix wird freigegeben
11. vollständiger einmaliger Scan aller eindeutigen Cluster loopender
    Unterverzeichnisse und bestätigtes Ersetzen ausschließlich ihres letzten
    Rücksprungs durch EOC
12. atomare Reparatur zugleich kurzer und zyklischer regulärer Dateien: alle
    eindeutigen Cluster bleiben erhalten, der letzte wird EOC und die
    Directory-Größe wird auf deren lesbare Kapazität begrenzt
13. scanreihenfolge-unabhängige Referenz-/Pflichtreferenzzählung und Reparatur
    von Crosslinks, die ausschließlich aus überlangen regulären Dateitails
    stammen
14. vollständiger Inhaltsscan und bestätigte Nullsetzung unzulässiger
    Größenfelder ansonsten gültiger Unterverzeichniseinträge
15. bestätigtes Auflösen reiner, mehrfach benötigter regulärer Dateiketten
    durch vollständig verifizierte Kopien in höchstens 48 freie Cluster;
    Directory-Beteiligung und Mischdiagnosen bleiben gesperrt

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
C:\> CHKDSK --fat12 1 --reclaim-orphans --confirm
C:\> CHKDSK --fat12 1 --repair-loops --confirm
C:\> CHKDSK --fat12 1 --repair-dir-loops --confirm
C:\> CHKDSK --fat12 1 --repair-short-loops --confirm
C:\> CHKDSK --fat12 1 --repair-crosslinks --confirm
C:\> CHKDSK --fat12 1 --repair-dir-size --confirm
C:\> CHKDSK --fat12 1 --repair-volume-label --confirm
C:\> CHKDSK --fat12 1 --repair-zero-files --confirm
C:\> CHKDSK --fat12 1 --repair-zero-start --confirm
C:\> CHKDSK --fat12 1 --repair-dot-size --confirm
C:\> CHKDSK --fat12 1 --repair-dot-cluster --confirm
C:\> CHKDSK --fat12 1 --repair-required-crosslinks --confirm
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
`--reclaim-orphans --confirm` verwirft die Inhalte nicht referenzierter
Allokationen ausdrücklich. Der Dienst akzeptiert nur die exakte Diagnose
`ORPHAN_CLUSTER`, erstellt zuerst die vollständige feste Owner-Map und setzt
dann ausschließlich allokierte Cluster mit Owner null auf frei. `0xFF7`-Bad-
Cluster und alle erreichbaren Cluster bleiben erhalten. Beide FAT-Kopien
werden vollständig journalisiert, geschrieben und neu geprüft. Eine
Wiederanbindung verlorener Inhalte an `FOUND.000` findet nicht statt.
`--repair-loops --confirm` akzeptiert nur die reine Diagnose `CHAIN_LOOP` und
nur reguläre Dateien, deren Kette vor der ersten Wiederholung mindestens die
aus der Dateigröße berechnete Anzahl eindeutiger Cluster erreicht. Der Dienst
markiert diesen Sollpräfix fest begrenzt, setzt dessen letzten Cluster auf EOC
und gibt danach ausschließlich unmarkierte Schleifencluster frei. Eine
Directory-Schleife, eine gleichzeitig kurze Kette oder irgendeine weitere
Diagnose verhindert die gesamte Transaktion.
`--repair-dir-loops --confirm` setzt ebenfalls eine reine Loop-Diagnose voraus.
Vor der Kandidatenfreigabe liest der Scanner jeden eindeutigen Cluster des
betroffenen Unterverzeichnisses genau einmal und diagnostiziert dessen
Einträge. Bei der Mutation wird erneut bewiesen, dass der letzte eindeutige
Cluster auf einen bereits markierten Cluster derselben Kette zurückzeigt; nur
dieser FAT-Eintrag wird auf EOC gesetzt. Kein Directory-Cluster wird
freigegeben oder inhaltlich verändert.
`--repair-short-loops --confirm` behandelt ausschließlich die exakte
Gesamtdiagnose `CHAIN_LOOP|CHAIN_SHORT`, wenn jede Loop- und jede Short-Meldung
zu derselben Menge regulärer Dateien gehört. Alle eindeutigen Cluster werden
behalten, der letzte Rücksprung wird EOC und die Dateigröße auf
`eindeutige Cluster * Clustergröße` reduziert. Die geänderten Sektoren beider
FATs und alle Directory-Sektoren liegen vor dem ersten Write gemeinsam im
Undo-Journal. Zero-Starts, Crosslinks und weitere Diagnoseflags verhindern die
Transaktion.
`--repair-crosslinks --confirm` akzeptiert nur die exakte Diagnose
`CHAIN_CROSSLINK|CHAIN_EXCESS`. Der vollständige Scan zählt pro Cluster alle
Kettenverweise und getrennt jene Verweise, die innerhalb der deklarierten
Dateilänge oder einer Directory-Kette liegen. Sobald ein mehrfach
referenzierter Cluster von mehr als einer Sollkette benötigt wird, bleibt das
Medium unverändert. Andernfalls werden alle überlangen Dateien am Sollende
getrennt, genau einmal benötigte Cluster bewahrt und ausschließlich reine
Excess-Tail-Cluster freigegeben.
`--repair-required-crosslinks --confirm` behandelt getrennt die exakte
Diagnose `CHAIN_CROSSLINK`, wenn alle mehrfach benötigten Cluster ausschließlich
zu vollständig beschriebenen, normal EOC-terminierten regulären Dateien
gehören. Die erste Datei in deterministischer Scanreihenfolge behält ihre
Kette; jede später kollidierende Datei wird vollständig in eine eigene Kette
aus freien Clustern kopiert. Höchstens 48 Kloncluster sind zulässig. Vor dem
ersten Write sichert das 64-Einträge-Undo-Journal sämtliche Ziel-Datensektoren,
beide geänderten FAT-Spiegel und alle Directory-Sektoren. Erst nach
verifiziertem Daten-Readback werden FATs und zuletzt ausschließlich die
niedrigen Startcluster der geklonten Dateien publiziert. Nicht mehr
referenzierte eindeutige Präfixcluster werden freigegeben; gemeinsame
Quellcluster und Dateiinhalte bleiben unverändert. Fehlender Freiraum,
Directory-Beteiligung, Kandidaten- oder Journalerschöpfung verweigern die
Transaktion vor Seiteneffekten.
`--repair-dir-size --confirm` korrigiert ausschließlich Unterverzeichnisse mit
gültigen Attributen und gültigem Startcluster, deren FAT-Größenfeld entgegen
der Spezifikation nicht null ist. Der Scanner traversiert ihren Inhalt trotz
der Diagnose vollständig. Bei einer reinen `DIRECTORY_INVALID`-Diagnose und
vollständiger Kandidatenabdeckung werden alle betroffenen Directory-Sektoren
journalisiert und nur das jeweilige 32-Bit-Größenfeld auf null gesetzt.
`--repair-volume-label --confirm` normalisiert ausschließlich ansonsten
gültige Volume-Label-Einträge, deren reservierter niedriger Startcluster oder
Größenwert nicht null ist. Der geleaste Rescan muss als einzige Diagnose
`DIRECTORY_INVALID` und für jeden Fehler genau einen festen Kandidaten liefern.
Nach Undo-Journalisierung des Directory-Sektors werden nur diese beiden Felder
auf null gesetzt; Labelname, Attribute, FAT und Daten bleiben unverändert.
`--repair-zero-files --confirm` behandelt reguläre Dateien mit Größe null und
noch zugewiesener Clusterkette. Nur eine reine
`CHAIN_EXCESS|DIRECTORY_INVALID`-Diagnose mit vollständiger Kandidatenabdeckung
ist zulässig. Jeder Cluster muss genau einmal referenziert, von keiner
Sollkette benötigt und normal EOC-terminiert sein. Beide FATs und alle
Directory-Sektoren werden vorab gemeinsam journalisiert; danach wird die Kette
freigegeben und der niedrige Startcluster null. Der alte Inhalt wird bewusst
verworfen und nicht als Datenrettung ausgegeben.
`--repair-zero-start --confirm` behandelt den umgekehrten eindeutigen Fall:
Eine reguläre Datei deklariert eine positive Größe, besitzt aber Startcluster
null und damit keinen lesbaren Datencluster. Nur wenn die Gesamtdiagnose exakt
`CHAIN_SHORT` lautet und jede Meldung einen solchen Kandidaten hat, wird nach
Undo-Journalisierung ausschließlich das 32-Bit-Größenfeld nullgesetzt. FAT,
Startcluster, Name, Attribute, Zeitfelder und Datenbereiche bleiben
unverändert; fehlende Daten werden weder erfunden noch allokiert.
`--repair-dot-size --confirm` validiert exakte, leerzeichenaufgefüllte `.`- und
`..`-Namen gegen den aktuellen beziehungsweise übergeordneten
Verzeichniscluster. Dot-Einträge im Root und falsche Beziehungen bleiben
unreparierbare Diagnosen. Nur wenn alle Directory-Fehler korrekte Dot-Verweise
mit Größe ungleich null sind, wird nach Undo-Journalisierung ausschließlich
das jeweilige 32-Bit-Größenfeld nullgesetzt.
`--repair-dot-cluster --confirm` nutzt dieselbe Parent-Beziehung für den
komplementären Einzelfehler: Ein exakt benannter Dot-Eintrag mit Größe null,
aber falschem niedrigem Startcluster wird auf den deterministischen Self- oder
Parent-Cluster gesetzt. Root-Dot-Einträge und kombinierte Cluster-/Größenfehler
erhalten keinen Kandidaten. Namen, Attribute, FAT und Daten bleiben
unverändert.
`FDISK.PRG` kann auf explizit freigegebenen, leeren ATA-/AHCI-Medien eine
validierte MBR-Partition erzeugen. Eine Diskette bleibt eine partitionslose
Superfloppy und wird von `FDISK` niemals partitioniert.

## Noch offen

- reale FDD-/VMware-Power-Loss- und Reconnect-Matrix über die bereits
  deterministisch geprüften Veröffentlichungsstufen
- CHKDSK-Reparatur von Directory-Crosslinks und allgemeinen
  Verzeichnisschäden jenseits der eng begrenzten Feldreparaturen, Journal-,
  Remap- und Defektsektorkarte sowie Datenrettung von Orphans statt ihres
  expliziten Verwerfens
- QEMU-Laufzeitnachweis für Maintenance-Lease, Unmount, FAT-Spiegel-Reparatur,
  Verify und kontrollierten Remount
- breitere echte FDD-/Medienfehler- und Langzeitmatrix

`FAT12_ANALYSIS.md` ist ein historischer Bericht und beschreibt nicht den
heutigen Implementierungsstand.
