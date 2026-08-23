# REIST High-Assurance Core Contract

Stand: 23. August 2026

REIST OS ist ein **High-Assurance Research Operating System for
Fault-Tolerant and Fail-Operational Computing**. Dieser Vertrag definiert die
produkt- und branchenunabhängigen Regeln des generischen Kerns. Er ist keine
Zertifizierung und keine Behauptung, dass der heutige Forschungsstand bereits
fail-operational ist.

## Architekturmodell

```text
REIST OS
├── Generic High-Assurance Core
├── Medical reference profile
├── Spacecraft reference profile
├── Industrial-control reference profile
└── Experimental FPGA profile
```

Profile dürfen Anforderungen verschärfen, aber keine Kerninvariante lockern.
Jedes konkrete System legt sein Einsatzprofil, seine Essential Functions,
Gefahren, sicheren und degradierten Zustände sowie FTTI, RTO und RPO separat
fest. Anforderungen aus Medizin, Raumfahrt oder Industrie gelten nur, wenn
das entsprechende Profil für ein konkretes Zielsystem ausgewählt wurde.

## Verbindliche Kernprinzipien

1. **Geschützte Microkernel-Grenze:** Ring 0 enthält ausschließlich minimale
   Schutz-, Zeit-, IPC-, Capability-, Interrupt-, Fencing-, Watchdog- und
   Recoverymechanismen. Treiber, Dienste, Dateisysteme, Protokollstacks, GUI
   und Anwendungen laufen in getrennten Ring-3-Fehlerdomänen. Ihr Crash, Hang
   oder ungültiges Ergebnis darf den Kern und unabhängige Essential Functions
   nicht beenden.
2. **Fehler als Normalfall:** Jeder externe Eingang, jedes Gerät und jeder
   Dienst kann ausfallen, hängen oder inkonsistente Daten liefern.
3. **Begrenzung vor Reparatur:** `Detect -> Contain -> Recover -> Validate ->
   Reintegrate`; gelingt dies nicht innerhalb des Budgets, folgt `Degrade ->
   Safe State -> Controlled Restart`.
4. **Unabhängige Fehlerdomänen:** Redundanz auf demselben Kernel, derselben CPU,
   demselben RAM oder derselben Versorgung ist keine unabhängige Redundanz.
5. **Fail-closed Steuerung:** Gefährliche Ausgänge werden vor Diagnose,
   Restart oder Reintegration gesperrt und rücklesbar verifiziert.
6. **Gebundene Laufzeit:** Safety-Pfade besitzen feste Speicher-, CPU-, Queue-,
   Lock-, I/O- und Zeitbudgets; unbegrenztes Warten ist verboten.
7. **Keine Fortsetzung nach unbekannter Kernkorruption:** Ein beschädigter
   Rechnerkanal wird eingezäunt und ersetzt oder kontrolliert neu gestartet.
8. **Nachweisbarkeit:** Anforderung, Gefahr, Kontrolle, Code, Test und Ergebnis
   sind bidirektional rückverfolgbar; bekannte Abweichungen bleiben sichtbar.
9. **Langlebigkeit:** Formate und ABI sind versioniert; Updates sind signiert,
   atomar, stromausfallsicher und rückfallfähig; Zeit- und Zählerüberläufe
   werden über die vorgesehene Lebensdauer geprüft.

Das konkrete Resilienzversprechen, die Restartregeln und die terminalen
Degradierungsstufen definiert der
[Resilienz- und Degradierungsvertrag](RESILIENCE_AND_DEGRADATION_CONTRACT.md).

## Bootintegrität und Vertrauensgrenze

Bootartefakte müssen versioniert und ihre exakten Inhalte kryptografisch
gebunden sein. Der aktuelle BIOS-Pfad verwendet ein Manifest-v3-Format mit
SHA-256 nach NIST FIPS 180-4, eingebetteter RSA-PSS-Signatur und einen
unabhängigen, fail-closed
Hostvalidator für HDD- und Floppy-Images. Der Imageerzeuger und der Validator
teilen keine Manifestparser- oder Boundslogik; nur die standardisierte
Hashfunktion stammt aus der Laufzeitbibliothek.

Stage 2 verifiziert Digest und Signatur innerhalb fester Speicher- und
Laufzeitgrenzen: SHA-256 und die diagnostische CRC32 teilen genau einen
begrenzten Kernel-Lesedurchlauf; danach prüft eine feste 2048-Bit-Arithmetik
RSA-PSS vor ELF-Parsing und Kernelstart. CRC32 bleibt reine
Beschädigungsdiagnostik. Unbekannte Manifestversionen, fehlende Digests,
ungültige Grenzen oder widersprüchliche Imagegeometrie werden vor dem
Kernelstart geschlossen abgelehnt.

Das native HDD-Layout hält zwei getrennt prüfbare Kandidaten in festen,
nicht überlappenden Bereichen: Manifest A/B an den partitionrelativen LBAs 0
und 96 sowie Kernel A/B an 128 und 3136. Stage 1 passt in den MBR-Codebereich,
lädt ausschließlich die feste 64-Sektoren-Reserve von Stage 2 ab LBA 1 und
startet immer mit Kandidat A. Erst Stage 2 interpretiert das nicht
vertrauenswürdige Manifest. Scheitert A vor dem Kernel-Handoff, wird B genau
einmal vollständig und unabhängig geprüft; ein zweiter Fehler stoppt den Boot.
Die Rescue-Diskette bleibt absichtlich ein einzelner signierter Kandidat.

Die HDD-Sektoren 97 und 98 enthalten zwei vollständige Kopien des
REIST-Boot-Control-Records v1. Das BIOS bietet dafür kein geeignetes
standardisiertes A/B-Transaktionsformat; die Abweichung ist daher explizit
versioniert und auf 512 Byte begrenzt. Magic, Headergröße, reservierte
Nullbytes, CRC32, 64-Bit-Sequenz, Slotwerte und höchstens zwei Versuche werden
vor jeder Auswahl geprüft. Gleiche Sequenzen müssen bitidentisch sein,
benachbarte Sequenzen wählen die neuere Kopie; größere Lücken sind mehrdeutig
und stoppen den Boot. Stage 2 schreibt immer die ältere oder ungültige Kopie
zuerst und liest jeden Sektor vor dem nächsten Commit zurück.

Ein Offline-Updater prüft ELF und die policy-gebundene RSA-PSS-Signatur.
Boot-Control v1 behält die feste `A -> pending B`-Semantik; v2 erlaubt
append-only ausschließlich den zum bestätigten Active-Slot gegenüberliegenden
Pending-Slot. Der Updater schreibt nur dessen Kernel und Manifest und
veröffentlicht Pending erst nach vollständiger Revalidierung. Stage 2
persistiert die verminderte Versuchszahl vor A oder B und kehrt nach
Erschöpfung oder Kandidatenfehler zum zuvor bestätigten Slot zurück. Nach
vollständiger Kandidatenprüfung publiziert Stage 2 einen 64-Byte-v1-Handoff an
der festen Adresse `0x4E00`; Magic, Version, Nullfelder und CRC32 schützen Slot,
Control-Sequenz und Bootpartitionsgeometrie. Der Kernel kopiert ihn vor
Allocator-Nutzung, löst genau eine MBR-Partition vom Typ `0xDA` auf und gibt
Syscall 117 erst nach `BOOT_OK` ausschließlich für die gebundene
Storage-Service-Generation frei. Der Ring-3-Dienst gleicht beide
Control-Kopien erneut ab und bestätigt nur die exakt gestartete Pending-
Sequenz, ältere/ungültige Kopie zuerst, mit Flush und Read-back. Bestätigtes A
oder B bootet danach direkt; ein bestätigter B-Fehler persistiert weiterhin
zuerst den bewährten A-Fallback. Der Kernel besitzt weiterhin keine Autorität zum Schreiben dieser
Sektoren.

Ein hostseitiges REIST-Offline-Update-Bundle v1 kapselt genau einen bereits
signierten ELF32-Kernel in einem festen 512-Byte-Header und höchstens 3008
Payload-Sektoren. Producer und Consumer besitzen getrennte Strukturparser.
Exakte Länge, Nullflags/-reserven, CRC32, SHA-256, lokal gepinnter SPKI-Digest
und RSA-2048-PSS nach RFC 8017 müssen vor Erzeugung eines Update-Images gültig
sein. Das Bundle enthält bewusst keine Slot-, Sequenz-, Versuchszähler- oder
Rollbackautorität; diese bleibt beim validierten Boot-Control-Zustand und dem
bestehenden transaktionalen Updater. Das eingeschränkte Binärformat ist kein
TUF-/Uptane-Kompatibilitätsclaim und ersetzt weder vertrauenswürdige
Versionsmetadaten noch einen unveränderlichen Recovery-Anker.

Die Signaturstufe verwendet RSA-2048-PSS/SHA-256 gemäß RFC 8017 mit
MGF1-SHA-256 und exakt 32 Byte Salt. Die feste Research-Policy pinnt
Algorithmusparameter und den SHA-256-Fingerprint des DER-
SubjectPublicKeyInfo; Signierer und unabhängiger Prüfer müssen vor jeder
Imageveröffentlichung erfolgreich sein. Der eingecheckte private Schlüssel ist
eine absichtlich öffentliche Testfixture und in Release-Policies verboten.
Manifest v3 bettet die exakte 256-Byte-Signatur ein; Stage 2 bindet den dazu
gehörenden Modulus und Exponent 65537 fest ein und akzeptiert keine vom Medium
gewählte Schlüssel- oder Algorithmusidentität. Das authentifiziert den Kernel
relativ zu Stage 2. Da Stage 1 und Stage 2 auf demselben beschreibbaren Medium
liegen und kein unveränderlicher Plattformanker vorliegt, darf weiterhin kein
Secure-Boot-, Anti-Rollback- oder physischer Vertrauensketten-Claim entstehen.

## Medienübergreifender Schreib- und Wiederanlaufvertrag

Der Vertrag gilt für **jeden** beschreibbaren persistenten Datenträger und
nicht nur für ATA: FDD, SATA/NVMe, USB-Massenspeicher, Flash, optische und
später ergänzte Medien müssen dieselbe Zustandsmaschine verwenden.

1. Jeder Schreibvorgang wird vor dem ersten Seiteneffekt mit Ressourcen-ID,
   Generation und absoluter Deadline eröffnet und erst nach nachgewiesener
   dauerhafter Übernahme als erfolgreich beendet.
2. Timeout, Geräteverlust, Controllerreset oder unklarer Abschluss führen vor
   jedem weiteren Zugriff zu `QUARANTINED`; ein unklarer Schreibabschluss
   verriegelt zusätzlich alle Mutationen und setzt das betroffene Medium auf
   read-only. Ein Schreibvorgang wird niemals blind wiederholt.
3. Automatische Wiedererkennung ist nur nach Controllerreset, unveränderter
   Geräteidentität sowie zwei übereinstimmenden frischen Reads mit erwartetem
   Fingerprint zulässig. Ein anderes oder nicht eindeutig erkanntes Medium
   bleibt quarantänisiert.
4. Nach einem reinen Lesefehler darf eine vollständig bestandene Prüfung das
   unveränderte Medium wieder `ONLINE_RW` schalten. Nach einem unklaren
   Schreibabschluss ist höchstens `ONLINE_RO` zulässig, bis ein für dieses
   Medium nachgewiesenes Undo-/COW-/Journal-Recovery die Transaktion eindeutig
   als zurückgerollt oder dauerhaft abgeschlossen bestätigt.
5. Persistente Recovery-Metadaten sind redundant, versioniert, geordnet und
   gegen Stromverlust abgesichert. Ohne diesen Nachweis bleibt der Schreibpfad
   für das jeweilige Backend geschlossen; Datenverfügbarkeit darf niemals
   durch riskante automatische Reparatur erkauft werden.

Die gegenwärtige ATA-/AHCI-/FDD-Implementierung erfüllt Erkennung, Quarantäne,
begrenzte Requalifizierung und read-only-Degradation. Persistente
Undo-Journale decken markierte native FAT32- und REIST-FAT12-Images ab; FAT12
besitzt zusätzlich begrenzte Remaps, kritische Replikate und geordnete
Dateitransaktionen. Die deterministische FAT12-Persistenz-Fehlermatrix über 29
stabile Barrieren ist umgesetzt; reale Power-Loss-/Reconnect-Evidenz bleibt
begrenzt. Ein medienunabhängiger Nachweis für EXT2, fremde FAT-Volumes und
künftige Backends bleibt offen.
Beim FDD melden auch normale FAT12-Lesefehler die konkrete Medienressource.
Eine Requalifizierung darf die Quarantäne nur in einem internen Probezugriff
umgehen und muss den FDC zuvor resetten, ausstehende Interruptzustände leeren,
die Betriebsparameter neu setzen und das Laufwerk kalibrieren.

## Gefahrenregister und Traceability

Die maschinenlesbare
[`safety/assurance_scope.toml`](../../safety/assurance_scope.toml) legt die
Systemgrenze der ausgewählten Baseline `REIST-research` fest: Einsatzzweck,
Umgebung, vorhersehbaren Fehlgebrauch, Essential Functions, Anforderungen,
Komponentenbestand sowie ausgewählte und ausgeschlossene Profile. Der Status
`complete` bedeutet ausschließlich, dass dieser erklärte Forschungsumfang
vollständig inventarisiert und mit Gefahren belegt ist. Er ist weder
Zertifizierung noch Produkt- oder Zielhardwarefreigabe. Medizin, Raumfahrt,
Industrieautomation und FPGA bleiben nicht ausgewählte Referenzprofile.

Das maschinenlesbare Register
[`safety/hazards.toml`](../../safety/hazards.toml) verwendet das append-only
erweiterte Schema v2; gültige Schema-v1-Register bleiben weiterhin lesbar.
Jeder v2-Eintrag bindet eine gefährliche Situation an Ursachen, Auswirkungen,
Betriebsphasen, normalen/degradierten/sicheren Zustand, positive FTTI samt
Begründung, Essential Functions, Anforderungen, Design, Codekontrollen,
ausführbare Verifikation, objektive Abnahmekriterien, Annahmen, Eigentümer und
Restrisiko. `scripts/validate_hazard_register.py` lehnt unbekannte Versionen,
doppelte IDs, unsichere Pfade, unbekannte Referenzen und insbesondere jede
Lücke zwischen Scope-Inventar und Gefahrenabdeckung fail-closed ab.
`scripts/run_hazard_traceability.py` führt jede referenzierte Verifikation aus
und bindet Scope, Register, Anforderungen, Design-/Code-/Testquellen und
Ergebnisse per SHA-256 an eine maschinenlesbare JSON-Baseline. Ein
fehlgeschlagener Test lässt alle davon abhängigen Gefahren und die
Gesamtbaseline fail-closed fehlschlagen. Sichtbare `partially_verified`- und
Restrisiko-Felder verhindern, dass Registervollständigkeit mit vollständiger
Risikobeherrschung verwechselt wird.

## Externer Watchdog und rücklesbares Interlock

Der [External Safety Monitor Contract](EXTERNAL_SAFETY_MONITOR_CONTRACT.md)
trennt Emulator-Watchdog, Zielreset, physisches Ausgangs-Fence und dessen
elektrische Rückleseprüfung. Das maschinenlesbare Profil
[`safety/external_safety_monitor.toml`](../../safety/external_safety_monitor.toml)
steht auf `unbound`, solange kein konkretes Ziel, kein separat versorgter und
getakteter Monitor sowie keine gehashte physische Fault-Injection-Kampagne
gebunden sind. Hostmodelle, QEMU IB700, ein Kommandoecho oder ein
Softwarelatch können den Status `qualified` nicht erzeugen.

Die generische 1-s-Fatal-FTTI ist vollständig auf Heartbeatverlust,
Fence-Anwendung, unabhängiges Sense-Readback und Zielreset aufgeteilt. Jede
Abweichung hält das Fence geschlossen und entzieht Reintegration. S0.2 ist für
die automatisierte QEMU/VMware-Forschungsbaseline abgenommen. Das physische
Profil bleibt `unbound`; Hardwarebackend und Kampagne sind manuelle
Nutzerevidenz und Voraussetzung jeder späteren Zielhardwareaussage.

## Ressourcenregister

Das versionierte Register
[`safety/resource_budgets.toml`](../../safety/resource_budgets.toml) bindet
statische Ressourcenobergrenzen an ihre autoritativen C-Makros und konkrete
Verifikationstests. `scripts/validate_resource_budgets.py` wertet keine
beliebigen Ausdrücke aus, sondern nur einen begrenzten ganzzahligen
Konstantenvertrag. Quellcode-Drift, doppelte IDs oder Symbole, Pfadflucht und
fehlende Evidenz führen zum Fehler. Der Status bleibt `partial`, bis zusätzlich
Laufzeit-High-Water-Marken aller kritischen Pools sowie Zielhardware-WCET-,
Callgraph- und vollständige Speicherbudgets nachgewiesen sind. IPC und der
Storage-Request-Pool liefern bereits versionierte, saturierende Belegungs- und
Erschöpfungsdiagnostik. Die Memory-Statistik v2 ergänzt Frame-/Heap-Peaks und
saturierende Allokationsfehler bei kompatiblem v1-Präfix. Taskslots besitzen
eine versionierte, allokationsfreie Momentaufnahme mit Kapazität, Supervisor-
Reserve, aktueller/maximaler Belegung und saturierenden Ablehnungen; sie scannt
höchstens `MAX_TASKS` Einträge. Deterministische Heap-/Frame-ENOMEM-Injection
ist ausschließlich in einem getrennten Testimage enthalten. Sie prüft
fehlgeschlagene Erst- und Teilallokationen auf exakte Rückgewinnung und darf in
keinem Produktionsprofil aktiv sein.

S0.4c-2b2c bindet zusätzlich die vier kernel-eigenen mediated-DMA-Pools an das
Ressourcenregister. Device-Control-Kommando 18 liefert einer bereits
autorisierten Treibergeneration ausschließlich aggregierte aktive und maximale
Belegung, Kapazität, Poolgröße und saturierende Kapazitätsablehnungen. Die
32-Byte-v1-Struktur enthält weder physische Adressen noch Pooltokens oder
Eigentümer. Zähleränderung und ein Konsistenzscan aller vier Slots laufen unter
dem bestehenden Device-Domain-Lock; die Werte sind Diagnose und keine
Autoritätsentscheidung.

Die IRQ-Aufnahme jeder aktiven Device-Domain ist zusätzlich auf 128 Ereignisse
je festem 100-ms-Fenster begrenzt. Überschreitung oder rückläufige Zeit fencen
das betroffene Gerät vor einer weiteren Ring-3-Benachrichtigung über denselben
vollständigen, idempotenten Cleanup-Pfad. Eine Regression der Scheduler-
Abrechnungszeit verriegelt analog alle Ring-3-Klassen und bleibt über normale
Fensterwechsel erhalten. Beide Diagnosezähler saturieren. Nur ein getrenntes,
zur Compilezeit ausgewähltes QEMU-Profil führt die festen lokalen Selbsttests
aus; es existiert dafür keine Laufzeitsteuerung und kein neuer Syscall.

Jeder Kernel-C-Buildpfad wird zusätzlich in einem unabhängigen Analysecompile
mit Stack-Usage und Callgraph-Information übersetzt. Fehlende Artefakte,
dynamische oder über 4096 Byte große lokale Frames und Rekursionszyklen sind
Gatefehler. Die lokale Framegrenze ist kein Gesamtstacknachweis; Entry-/IRQ-
Pfade benötigen zusätzlich explizite kumulative Budgets und Zielhardware-WCET.
Legacy-/Scheduler-IRQ, CPU-Exceptions und INT-80-Syscalls besitzen diese
Budgets über `safety/stack_budgets.json`: registrierte Handler, sämtliche VFS-
Operationstabellen, weitere indirekte Callbacks und Assembly-Reserven sind
vollständig zu inventarisieren; jede unbekannte Kante scheitert geschlossen.
Der Syscallvertrag reserviert zusätzlich 1.024 Byte des realen 8-KiB-
Taskstacks außerhalb des zulässigen 7.168-Byte-Pfads. Plattform-WCET bleibt
ein offener Abnahmepunkt. Als begrenzte Vorstufe erfasst eine feste,
allokationsfreie v1-Diagnose die Maxima des Scheduler-Entscheidungspfads und
eines nicht blockierenden INT-80-Diagnosepfads. Die Messung endet vor jedem
Kontextwechsel, ist saturierend und darf keine Steuerentscheidung beeinflussen.
QEMU und VMware werden gegen `safety/wcet_budgets.json` mit mindestens 64
Samples, null Zeitquellenanomalien und höchstens 10 ms je Pfad geprüft. Nur der
generationsprüfende Supervisor veröffentlicht den Marker; ein Messfehler darf
den Probe-Dienst nicht beenden. Diese
Werte sind ausschließlich empirische Emulator-Regressionsgrenzen;
Zielhardware-WCET und Zertifizierung werden ausdrücklich nicht behauptet.

Headerabhängigkeiten sind Teil der Build-Evidenz. Jeder Kernel-C-Compile muss
eine explizite Dependency-Datei erzeugen; fehlende, falsche oder nicht zum
Quellobjekt gehörende Evidenz verhindert den Link. Damit darf eine ABI- oder
Strukturänderung keinen inkrementellen Mischbuild aus alten und neuen
Objektlayouts erzeugen.

Hostseitige Parser-Fault-Kampagnen besitzen ebenfalls feste Ressourcenbudgets.
Die S0.6a-Referenz akzeptiert ausschließlich 16 bis 128 Fälle und einen
expliziten 32-Bit-Seed. Sie kombiniert einen festen strukturierten Korpus mit
genau einer Einbitmutation je Zusatzfall, verwendet nur ein automatisch
entferntes Temp-Verzeichnis und ruft den realen Bundle-zu-Update-Einstieg auf.
Ein erwarteter Parserfehler genügt nicht: Es darf kein Output-Image existieren,
und Quellimage sowie signierte Eingaben müssen nach der Gesamtkampagne
SHA-256-identisch sein. Daraus folgt keine Aussage über vollständige Coverage,
Langzeitbetrieb oder Hardwarefehlerabdeckung.

Native Release-Builds erzeugen nach der Imagevalidierung genau ein
SPDX-2.3-JSON-SBOM für Kernel, detached Signatur, BIOS-Image und unmittelbar
paketierte Ring-3-Programme. Zulässig sind ausschließlich kanonische reguläre
Dateien unter dem Repository-Build-Root; Symlinks, Duplikate, Nachlaufdaten und
das Ausgabedokument als Eingabe scheitern geschlossen. Die festen Budgets sind
160 Dateien, 128 MiB pro Datei, 512 MiB Gesamteingang und 2 MiB JSON. Jeder
Eintrag trägt die exakte Bytegröße im standardisierten Kommentarfeld, das von
SPDX 2.3 geforderte SHA-1 und zusätzlich SHA-256. Ein unabhängiger Validator
hasht die Live-Artefakte erneut und verlangt die vollständigen `DESCRIBES`- und
`CONTAINS`-Beziehungen, bevor das Paket akzeptiert wird. Der Generator
veröffentlicht nur durch Flush, `fsync` und atomaren Austausch; ein Fehler darf
ein vorhandenes SBOM nicht ersetzen. `NOASSERTION` bleibt für ungeklärte
Lizenz- und Copyrightfelder zwingend sichtbar. Das SBOM ist nicht signiert und
ist kein Nachweis für Reproduzierbarkeit, vollständige Abhängigkeiten,
Lizenzfreigabe, Provenienz oder bekannte Schwachstellenfreiheit.

## Ziel-Failure-Domains

Der aktuelle Kernel ist noch ein modularer Monolith. Das zentrale
Architektur-Gate ist daher nicht ein weiterer Gerätetreiber, sondern die
Verkleinerung der privilegierten Trusted Computing Base:

```text
Heute                         Ziel

kernel.bin                    REIST Safety Kernel
├── Scheduler                 ├── MMU / Scheduler / IPC
├── Memory                    ├── Capabilities
├── VFS / Filesystems         └── Supervisor primitives
├── Network                         ^       ^       ^
├── Drivers                         IPC     IPC     IPC
└── Supervisor                       |       |       |
                                  Storage Network Drivers
                                   Domain  Domain  Domain
```

Dateisystem, Netzwerk, GUI und komplexe Treiber müssen schrittweise in eigene
Adressräume mit Capability-beschränktem IPC, festen Quoten, Health-Vertrag,
Restartbudget und validierter Reintegration migrieren. Erst danach kann REIST
nachweisen, dass die absichtliche Zerstörung eines Dienstes seine Failure
Domain nicht verlässt.

## Quantitative Leitfrage

Für jede Release-Baseline wird nicht nur gefragt, ob Tests bestehen, sondern:

> Wie viel von REIST OS kann gezielt gestört oder zerstört werden, bevor die
> für das gewählte Profil definierten Essential Functions verloren gehen?

Die Antwort wird mit Fault-Injection, Fehlerabdeckung, FTTI, maximaler
Degradationsdauer, Recovery-Erfolgsrate und Common-Cause-Analyse gemessen.

## Safety Profiles

- Das **Medical reference profile** ordnet den Core-Regeln medizinische
  Safety-, Security-, QMS- und regulatorische Evidenz zu. Es ist optional und
  keine klinische Freigabe.
- Das **Spacecraft reference profile** ergänzt Strahlungs-/SEU-Modelle,
  autonome Recovery, Telemetrie, lange Kommunikationsausfälle und
  missionsphasenspezifische Safe Modes.
- Das **Industrial-control reference profile** ergänzt deterministische I/O,
  Anlagen-Interlocks, funktionale Sicherheit und kontrollierte Degradation.
- Das **Experimental FPGA profile** dient reproduzierbaren Hardware-Faults,
  diversitären Watchdogs, ECC-Injektion und beobachtbaren Recovery-Versuchen.

Profile werden in eigenen Dokumenten gepflegt. Keine Profilklassifikation
wird automatisch auf eine andere Branche oder ein anderes Produkt übertragen.
