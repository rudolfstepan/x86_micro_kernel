# ATA-PIO-Lesetransfer: Performance und Resilienz

Stand: 5. September 2026. Paket R7.1n, noch nicht abgenommen.

Performance und Resilienz sind gleichwertige, gleichzeitig zu bestehende
Abnahmekriterien. Ein unnötiger Bereitschaftswechsel je Sektor ist kein
Sicherheitsgewinn. Zeitgrenzen, vollständige Bereichsprüfung und eindeutiger
Fehlerabschluss bleiben verbindlich; Journalbarrieren werden nicht entfernt.

## Bestand und gemessener Engpass

Der Loader liest PRGs bereits zusammenhängend. FAT32 bündelt zusammenhängende
Sektoren in höchstens zwanzig Sektoren pro Backendaufruf. Der bisherige PIO-
Befehl READ SECTORS verlangt trotzdem eine DRQ-Prüfung je 512-Byte-Sektor.
Die Gastmessung des 396232-Byte-HTMLWORK ergab 4009 ms Dateiladen gegenüber
88 ms Speicheraufbau. Ein Sleep-only-Versuch verschlechterte das Laden auf
6370 ms und wurde zurückgenommen. Der bestehende Benchmark prüfte 256 KiB
vollständig, räumte seine Datei auf und kehrte zur Ring-3-Shell zurück:
101,91 KiB/s Lesen, 15,61 KiB/s Schreiben. Evidenz liegt unter
`build/codex-agent/ata-multiple-baseline.log` sowie `r39-os-*.log`.

## Standard und begrenzte Anpassung

Referenz sind [T13/1321D Revision 3](https://www.seagate.com/support/disc/manuals/ata/d1153r17.pdf)
(IDENTIFY DEVICE, READ MULTIPLE, SET MULTIPLE MODE, PIO data-in) und die
[Seagate-Befehlsreferenz](https://www.seagate.com/www-content/support-content/samsung/internal-products/spinpoint-m-series/en-us/docs/100772113c.pdf)
für READ MULTIPLE EXT. Ein DRQ-Block darf mehrere Sektoren umfassen;
Sector Count bleibt die Sektoranzahl. Der letzte Block darf kürzer sein.

Vor jeder gebündelten PIO-Lesetransaktion wird die aktuelle Gerätefähigkeit
frisch per IDENTIFY abgefragt, unter demselben bestehenden ATA-Mutex.
Wörter 47 und 59 werden auf gültige, unterstützte Blockgrößen geprüft.
Nicht geeignete Angaben wählen vor dem Transfer den bisherigen Lesebefehl.
Ein notwendiger SET MULTIPLE MODE wird begrenzt abgeschlossen; Moduszustand
wird nicht über Reset oder Mediengenerationen hinweg als gültig angenommen.
LBA28 und LBA48 behalten ihre bestehenden Bereichs- und Fähigkeitsprüfungen.
Die Gesamtgrenze des bestehenden Aufrufs bleibt zunächst unverändert;
die Zahl der Sektoren je Bereitschaftswechsel wird erhöht, nicht die Wartezeit.

Task-Warten verwendet eine absolute monotone Deadline und feste Pollgrenze,
mit Sleep statt aktivem Warten. Nicht schlafbarer Task-Kontext scheitert
geschlossen; der getrennte Bootpfad bleibt begrenzt. ERR, DF, Floating Bus,
unerwarteter DRQ-Zustand oder Timeout liefern Fehler. Kein automatischer
Befehlswechsel nach angefangenem Transfer. Erst nach vollständigem
erfolgreichem Abschluss werden Lesecache-Einträge veröffentlicht.

Es entsteht kein neuer Treiber und keine neue Geräteautorität. Die vorhandene
Ring-0-PIO-Implementierung bleibt sichtbare Migrationsschuld, nicht Vorbild
für neue Kernel-Dienste und kein Nachweis einer Ring-3-DMA-Isolation.
AHCI, DMA, Schreibbefehle, Journal und öffentliche ABIs bleiben unverändert.

## Abnahme

Hosttests führen Produktionsfunktionen gegen ein Port-/Zeitmodell aus:
Fähigkeiten, erneute Identifikation nach Zustandswechsel, exakte Blocklängen,
Restblock, LBA28/48, keine Cachepublikation bei Teilfehler, Timeout und
nicht schlafbarer Kontext. Bestehende ATA- und Journaltests bleiben erforderlich.
Der reale QEMU-Benchmark muss 256 KiB vollständig prüfen, fsync und Cleanup
abschließen und zur Shell zurückkehren. Lesen muss mindestens 400 KiB/s
erreichen; seine 120-Sekunden-Gesamtfrist bleibt unverändert. Beide
Referenzbuilds sind Pflicht. Diese Abnahme behauptet keine neue VMware-AHCI-
oder universelle Hardware-Performance.
