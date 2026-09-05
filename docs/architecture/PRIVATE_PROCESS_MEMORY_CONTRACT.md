# Private Prozessspeicher – R1.2c

Stand: 5. September 2026. Vertrag definiert, noch nicht implementiert/abgenommen.

## Grenze und Standards

ISO C11, Abschnitt 7.22.3 (WG14 N1570), bestimmt Alignment, Objektlebensdauer,
`calloc`-Nullung und den Erhalt des alten Objekts bei fehlgeschlagenem `realloc`.
Die vorhandene Nullgroessenentscheidung bleibt bestehen: `malloc(0)` liefert
NULL, `realloc(p,0)` gibt frei. Gewoehnlicher Mangel liefert NULL/ENOMEM;
ungueltige Heapmetadaten beenden ausschliesslich den betroffenen Ring-3-Prozess.
Es wird weder POSIX-`mmap` noch vollstaendige libc-Kompatibilitaet behauptet.

Der Kernel vermittelt ausschliesslich private physische Frames, Seitentabellen,
Ressourcenadmission und Lifecycle. Unterteilung in C-Objekte, Anwendungsdaten und
Cachepolitik bleiben Ring 3. Ein Kernel kann weiterhin auf Prozessframes zugreifen;
die Isolation gilt gegen andere unberechtigte Ring-3-Prozesse, nicht gegen Ring 0.

## Admission und Fortschritt

Der explizite alte Arena-Modus bleibt auf seine bisherige Kapazitaet begrenzt.
Ein neuer versionierter, opt-in Backingvertrag erlaubt bedarfsgerecht bezogene,
nicht zusammenhaengende Regionen mit einem gewaehlt festen Budget bis 512 MiB.
Das Budget wird nicht pauschal physisch allokiert. Vollstaendig leere Regionen
koennen zurueckgegeben werden; OOM darf keine noch lebenden Objekte zerstoeren.

Automatische Rueckgewinnung bedeutet hier: vollstaendiges Reaping bei Exit,
Fault oder Kill und Rueckgabe leerer Backingregionen durch die C-Laufzeit.
Ein tracing Garbage Collector fuer noch lebende Objekte benoetigt dagegen
sprachspezifische Wurzeln und Referenzinformationen und gehoert in die jeweilige
Ring-3-Laufzeit (spaeter beispielsweise JavaScript). Der Kernel darf keine
vermeintlich unbenutzten C-Objekte erraten und freigeben. Cacheverdrängung muss
ebenfalls mit dem jeweiligen Besitzer vereinbart werden, nicht dessen live
Speicher unbemerkt widerrufen.

Pro Prozess werden hoechstens die Haelfte des verwalteten RAM und maximal
512 MiB privater Heap zugelassen. Die globale Frameadmission bewahrt eine
Reserve fuer Kernel-/Recoverymechanismen. Freigegebene virtuelle Luecken werden
wiederverwendet, fehlgeschlagene Transaktionen hinterlassen weder Frames noch
dauerhaft verbrauchte Regionen. Bestehende Syscallnummern bleiben unveraendert.

Ein noch unveroeffentlichter Frame darf nicht durch Beendigung des allokierenden
Tasks verloren gehen. Kurze Praeemptionsgrenzen umfassen Erwerb, Nullung und
Mapping; IRQs bleiben ausserhalb der kleinen Metadatentransaktionen moeglich.
Zwischen endlichen Batches duerfen andere Tasks laufen. Dasselbe gilt fuer
Rollback, Freigabe und Reaping grosser Adressraeume. Jeder Cleanupfortschritt
bleibt in kernel-eigenem Lifecyclezustand erhalten, auch wenn der aufrufende
Prozess beendet wird. Keine lokale Variable darf zum letzten Besitzer werden.

## Speicherresilienz bleibt unveraendert

Firmware-reservierte Bereiche, Kernel-/Stackguards, Rescue-Reserven sowie die
bestehenden redundanten `resilient_page`-/`critical_object`-Objekte und deren
Validierung werden nicht umgewidmet. Neue Prozessseiten stammen nur aus dem
bereits validierten physischen Allocator. Normale private Heaps sind nicht
automatisch gespiegelt und beweisen keine physische DIMM-Fehlertoleranz.

Die heutige i386-Direct-Map verwaltet hoechstens 1 GiB physisches RAM. Diese
Hardware-/Paginggrenze bleibt in R1.2c bestehen. Die spaetere Nutzung weiterer
GiB benoetigt einen eigenen High-Memory-/64-Bit-Adressierungsnachweis; ein
groesseres Heapbudget kann sie nicht ersetzen. Browser- und Dateicaches sind
nachfolgende Ring-3-Nutzer, nicht Teil dieses Speicher-Lifecycle-Pakets.

## Nachweis

Die unveraenderliche Befehlsliste steht in `automation/reist-s03b.toml`.
Hosttests fuehren echten Allocatorcode mit kontrollierten Backends aus und
pruefen Ueberlauf, Quoten, Nullung, Freigabe, Wiederverwendung, Fehlerrollbacks,
Provideradmission sowie Adressen im gesamten bisherigen Userfenster.
QEMU muss grosse private Allokationen, OOM ohne Verlust vorhandener Daten,
Peer-Fortschritt, Fault/Kill/Reap und eine saubere Folgegeneration beweisen.
Die bisherigen libc- und Memory-Resilience-Gastnachweise bleiben Pflicht.
