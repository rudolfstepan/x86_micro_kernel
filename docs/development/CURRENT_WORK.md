# REIST OS – aktueller Arbeitsstand

Stand: 9. September 2026

## R3.39 eingefroren: Storage-Generation vor Ersatz wirklich stilllegen

Auf08c8c364 sauber inventarisiert. poll/up ignorieren die Ablehnung einer
Terminierung und koennen die noch vorhandene Identitaet durch einen Neustart
ersetzen. Der naechste zusammenhaengende Schnitt schliesst automatische und
manuelle Stilllegung, Fristablauf, Reap und fehlgeschlagenen Start gemeinsam.
Vertrag: STORAGE_GENERATION_RETIREMENT_CONTRACT.md;12 eingefrorene Gruppen.
Noch keine Implementierungsabnahme. R3.6b bleibt ausdruecklich zurueckgestellt;
persistenter Ring-3-Backend und JS-Schreibrechte folgen erst nach dieser Grenze.

## R3.38 abgenommen: gemeinsame Dateiobjekt-Lebensdauer

Alle20 eingefrorenen Abnahmegruppen bestanden. VFS und Ring-3-Storage teilen
jetzt generationsgebundene, aliasfeste Dateisperren; Syscall129 ist nur fuer
die aktuelle Storage-Generation zugelassen. Leseseitige EXT2-Recovery und
Namespace-Writes reservieren ihre Wirkung explizit. Unbekannter Ausgang,
Ownerverlust oder Fristablauf sperren das Medium vor einem Dienstneustart.
Keine neuen JS-Schreibrechte, kein neues persistentes Format.

Die konkret freigegebene Scheduler-Reparatur reserviert vor Process.terminating
den unbesessenen Task, entfernt dessen Wait-Node und sichert genau einen
Cleanup-Aufrufer. Wiederanlauf, Wake/Timeout und vorzeitiges Reap sind ausgeschlossen;
FINISHED folgt erst nach Cleanup. process_begin_exit bleibt eine Einmalzulassung.
Native O0/O2 pruefen echte Prozess-, Admission-, Terminate-, Dispatch-, Wait-
und Reap-Funktionen; bereits CPU-besessene und stale Targets bleiben unveraendert.
Keine neue Remote-Kill-/SMP-Quieszenzgarantie: Die realen Paketgaeste verwenden
eine CPU. Eine fremde noch CPU-besessene Generation muss der Supervisor zuerst
quieszent machen und eine abgelehnte Terminierung behandeln; das wird hier
nicht als allgemeiner AP-Recoverynachweis ausgegeben.

Abschliessende Belege unter `build/codex-agent/r338-file-lifetime/`;
exakte Befehle in automation/reist-s03b.toml und den results.json-Dateien:

| Gruppe | PASS / Sekunden | Beleg |
| --- | --- | --- |
| test_file_object_guard.py |8 Tests /18.114 |guard-final.log |
| generate_syscall_abi.py --check |0.109 |targeted-final/01.log |
| test_syscall_abi_source.py |0.262 |targeted-final/02.log |
| test_script_domain.py |1.514 |targeted-final/03.log |
| test_open_namespace_locks.py |0.260 |targeted-final/04.log |
| test_reist_vfs_file_client.py |0.724 |targeted-final/05.log |
| test_reist_vfs_symlink.py |1.038 |targeted-final/06.log |
| test_reist_vfs_namespace.py |0.684 |targeted-final/07.log |
| test_smp.py |7.839 |targeted-final/08.log |
| test_wait_source.py |0.271 |targeted-final/09.log |
| test_reist_ipc.py |3.026 |targeted-final/10.log |
| test_scheduler_slack.py |1.319 |targeted-final/11.log |
| test_terminal_input.py |37.797 |targeted-final/12.log |
| VMware -Video vga |80 |../20260908-234524-package-vmware-vga.log |
| QEMU -Video vga |75 |../20260908-234659-package-qemu-vga.log |
| Image-/Payloadabgleich |1.371 |artifacts-final/protected.json |
| Dateilebensdauer-Gast |3 Faelle,13 Shellbefehle /71.007 |guest-prompt/result.json |
| JS-Runner --file-capabilities |122.670 |js-runner.log; compatibility-final/00.log |
| JS-Service --restricted-worker |36.047 |js-service-resumed.log; compatibility-resumed/00.log |
| Externer Browser |91.989 |browser.log; compatibility-resumed/01.log |

Die neun bestehenden Hostgruppen mit nativen Hilfsprogrammen laufen ueber
measure_cpp_baseline.py; die neue Guard-Gruppe unterdrueckt Windowsdialoge
selbst. Fristen/Erfolgskriterien wurden nicht gelockert. `guest-final/` bleibt
als Fehlbeleg erhalten: eine asynchrone Quarantaenemeldung folgte unmittelbar
auf C:\>, weshalb der Pruefer sie uebersah. Nur die beiden vollstaendigen
Storage-Recoverymarker werden jetzt auch nach genau diesem Prompt akzeptiert;
Prefix-/Suffix-/Reihenfolgen-Negativtests und anschliessender voller Gastlauf
bestehen. Keine Erweiterung der Terminalrechte des Storage-Prozesses.
Der erste JS-Service-Lauf wurde durch eine naechtliche Zeitunterbrechung
ungueltig (11490.240s/Deadline; erster Durchlauf erfolgreich, zweiter unvollstaendig).
Sein Log bleibt erhalten; nur dieser Test wurde mit unveraenderter180s-Frist
wiederholt. Browser danach einmal erfolgreich; kein fehlgeschlagener Lauf als PASS.

`guest-prompt/normal.log` bestaetigt FAT32/FAT12-Ownerfault, EXT2-Aliase und
Service-Neustart. Fault und Hang zeigen automatische Quarantaene und Neustart
VOR svcctl; erneuter manueller Restart hebt die Sperre nicht auf. Unbeteiligte
Datei und Shell bleiben benutzbar. Beide privaten EXT2-Abbilder entsprechen
bytegenau dem erwarteten Recovery-Ergebnis SHA256
d2549e5371ec94698d3aa3eb4f1babe5cd851b2d5db4988e677cc6432a69872d.
FAT12-Rename bleibt ausdruecklich UNSUPPORTED; keine falsche BUSY-Abnahme.

Alle93 Programme und beide jeweiligen Kernel stimmen mit den Imagebytes
ueberein. Gegen R3.37 aendern sich STORAGE.PRG und zwei Bytes in JSWORK.PRG
(0x1193/0x11fd:129 ->130, vorhandener nativer Syscall-Deny-Selbsttest).
OBJGDTST.PRG kommt hinzu. Alle fuenf Schutzprogramme bleiben bytegleich;
acht echte Auswahl-/Abrechnungs-/Handoff-/Yield-Funktionen sind quelltextgleich.
Kein neuer VMware-Performance-/Hardware-WCET- oder Stromausfallnachweis.
`accepted-reference.json` prueft135 archivierte Dateien einschliesslich beider
Images/Kernel, aller Programme, Gastmedien und Logs; alte Fehlerbelege bleiben
unveraendert. Scope:40 eigene Kandidatenpfade innerhalb47 freigegebener Pfade.

R3.38 ist abgeschlossen. Der formale Queueuebergang aktiviert den einzigen
verbliebenen Eintrag R3.6b, dessen ausdrueckliche Ausfuehrungszurueckstellung
jedoch erhalten bleibt: kein Start dieses Pakets vor der Browser-/JS-Folgearbeit.
Als naechstes wird ausschliesslich der naechste persistente Ring-3-Backendschnitt
gemaess RING3_FILE_WRITE_CONTRACT inventarisiert/eingefroren; in diesem Lauf
keine spaetere Paketimplementierung. Keine Pushes.

### Historische Umsetzungsschritte (vor der obigen Abnahme)

**Freigegebene Scheduler-Korrektur in Pruefung:** Das Nutzer-„mach weiter“
beantwortet die konkrete Scope-Anfrage. allowed_files und Vertrag enthalten
jetzt den kalten Terminierungspfad und fuenf zusaetzliche Lifecycle-Gates.
Admission reserviert den quieszenten Task unter Prozess -> Task-Lock VOR
Process.terminating; CPU-Besitz wird ohne Wirkung abgewiesen. Ein separater
Cleanupzustand verhindert Dispatch, Wake/Timeout, doppeltes Cleanup und Reap.
Der erweiterte native O0/O2-Nachweis mit echtem Prozess-/Scheduler-/Waitcode
besteht in `terminate-green.log` (0,989s). Keine Hotpathaenderung oder
abgeschwaechte process_begin_exit-Assertion. Frische Gesamtgates laufen;
noch KEINE Abnahme oder Implementierungscommit.

**Historischer Blocker, 8. September:** Der wiederholte normale Gast in
`build/codex-agent/r338-file-lifetime/guest-owner-witness/normal.log` erreicht
FAT32/FAT12/EXT2, scheitert aber beim anschliessenden Dienstneustart an
`KERNEL ASSERTION FAILED: process_begin_exit(process, process_generation)`
in scheduler.c:1660. Der vorherige Lauf `guest-fat12/normal.log` bestand
diese Sequenz einmal; damit ist gerade KEINE verlaessliche Laufzeitabnahme
nachgewiesen. Keine weiteren Gastgates/Commit/Queuefreigabe erfolgt.

Die Ursache ist mit echtem extrahiertem Scheduler-Code deterministisch
reproduziert: scheduler_terminate_task gibt Locks/Preemption fuer schlafendes
Cleanup frei, ohne den READY-Task vorher aus der Dispatch-Zulassung zu nehmen.
claim_task_for_current_cpu kann denselben Task noch waehrend des fremden
Cleanups uebernehmen. O0 UND O2 melden
`FILE_OBJECT_GUARD_TERMINATE_FAIL target-redispatched-during-cleanup=1 assertions=0`
in `terminate-red.log` und den terminate-UUID-Unterverzeichnissen.
Der damals neue Test `test_terminating_owner_cannot_redispatch` blieb bis zur
Scope-Freigabe rot, ohne Scheduler oder Assertions zu aendern. Die damaligen
sechs gruenen Guard-Tests belegten NICHT die komplette aktuelle Gruppe.

Eine sichere Korrektur braucht die atomare Task-Quieszenz und den zugehoerigen
Scheduler-/Wait-Lifecycle-Nachweis; bloss process_begin_exit idempotent zu
machen wuerde paralleles Cleanup zulassen und die Assertion verdecken.
kernel/sched/scheduler.c/.h sowie zugehoerige Scheduler-/Wait-Tests liegen
damals ausserhalb allowed_files. Die oben dokumentierte Nutzerfreigabe
erlaubt jetzt gezielt die kalte Lifecycle-Reparatur; Hotpaths bleiben geschuetzt.

Zusaetzliche Zwischenbelege: `guest-fat12/fault.log` zeigt Quarantaene nach
echter Medienwirkung; die Diskbytes entsprechen bereits exakt dem erwarteten
Recovery-Ergebnis (SHA256 d2549e5371ec94698d3aa3eb4f1babe5cd851b2d5db4988e677cc6432a69872d).
Die Fixture hatte eine Terminalmeldung aus einem absichtlich nicht dazu
berechtigten Storage-Profil erwartet. Die private Fehlerkompilierung benutzt
jetzt stattdessen den vorhandenen Registerdiagnosepfad (EAX338FA017), ohne neue
Rechte. Der Pruefer verlangt zusaetzlich automatischen Neustart VOR svcctl,
exakten Medienbytevergleich und beim Hang Abbruch vor dem verspaeteten UD2.
Dieser strengere Fault/Hang-Nachweis ist noch OFFEN. Live-Logs und fruehe
Erkennung eingerueckter Panic-/Assertion-Header verhindern blinde Timeoutwartezeit.

Implementierungskandidat auf Vertragscommit945ebf5c; noch kein Implementierungscommit.
Alle sichtbaren Aenderungen gehoeren zur direkten Umsetzung dieses Pakets.
Queue unveraendert: R3.38 aktiv, keine spaeteren JS-Schreibrechte umgesetzt.

- Gemeinsame Admission unter bestehendem VFS-Mutex:256 Legacy-Nodes und16
  geschuetzte Service-Pins, vier pro Client, monotone Epochen/Generationen,
  feste SMP-Try-Locks, kein Parser/I/O/Heap unter dem Metadatenlock.
- FAT12/FAT32/EXT2-Keyprojektionen aus vorhandenen Locatoren; physische
  Medien-/Partitionsnormalisierung, Ablehnung ueberlappender Zweitmounts.
  Medienwiderruf sperrt alte Legacy-Mounts bis Close/Unmount/Remount und
  widerruft Servicepins; unbeteiligte Namespace-Epochen widerrufen nichts.
- Syscall129 angehaengt (130 Eintraege): nur aktuelle Storage-Generation,
  komplette Bereichs-/Nullfeldvalidierung und fehlgeschlagener Copyout mit
  Ruecknahme. Compatibility/Script/Browser erhalten diese Autoritaet nicht.
- Storage: Snapshot vor Lookup, Pin vor Publikation, VERIFY vor Zugriff und
  Ergebnispublikation, Delegation mit eigenem Pin, fehlgeschlagener Close
  bleibt fuer begrenztes Reap vorgemerkt; verlorene Open/Adopt-Antworten geben
  ihren Pin frei. Keine Wiedereroeffnung ueber einen gespeicherten Pfad.
- EXT2: explizite Guard-Adapter bei Namespace UND leseseitiger Journal-Recovery.
  Alte sechsteilige IO-Struktur, Wrapper und v1-Journal bleiben bestehen.
  Atomare Reservation vor Medienwirkung; NO_EFFECT/DURABLE_COMMIT/UNKNOWN
  kommen aus dem Backendablauf, nicht aus einer errno-Vermutung.
- Direkte Block-Write-/Flush-Syscalls koennen den Schutz nicht umgehen:
  eigene laufende Mutation oder bisheriger exklusiver Wartungs-/Unmountpfad.
  Unsicherer Mutationsausgang wird zuerst quarantainisiert; erst danach wird
  ein verlorener/haengender Dienst zurueckgenommen und neu gestartet.
  Ein Restart hebt die Mediensperre nicht auf.
- OBJGDTST.PRG in beiden /bin-Layouts; Image- und Gastpruefer ergaenzt.
  Separate private Storage-Testkompilierungen verlieren eine echte Reservation
  nach Medienwirkung durch Fault/Hang. Kein solcher Branch im normalen Image.

Acht gezielte eingefrorene Gruppen bestanden auf diesem Kandidaten. Belege
unter `build/codex-agent/r338-file-lifetime/targeted-c26157191bee44aa87bdd4d30999eeaf/`:

| Befehl | Ergebnis / Sekunden | Log |
| --- | --- | --- |
| python test/test_file_object_guard.py -v |5 Tests PASS; echter Kern/VFS/Service/FAT/EXT2, O0/O2, je128 Threads /13.206 |0.log |
| python scripts/generate_syscall_abi.py --check |PASS,130 Nummern /0.099 |1.log |
| python test/test_syscall_abi_source.py -v |PASS /0.256 |2.log |
| python test/test_script_domain.py -v |PASS /1.378 |3.log |
| python scripts/measure_cpp_baseline.py --host-test test/test_open_namespace_locks.py -v |PASS /0.241 |4.log |
| python scripts/measure_cpp_baseline.py --host-test test/test_reist_vfs_file_client.py -v |PASS /0.703 |5.log |
| python scripts/measure_cpp_baseline.py --host-test test/test_reist_vfs_symlink.py -v |PASS /1.016 |6.log |
| python scripts/measure_cpp_baseline.py --host-test test/test_reist_vfs_namespace.py -v |PASS /0.678 |7.log |

Fruehere isolierte Komponentenbelege bleiben erhalten:
`targeted-472ae04d48ed4a41a6ead1bab19c22f2/` (damals129 Syscalls),
`review-4503e350996b49f28b50b26479e2e24a/`,
`compile-7ce09095ec994db8bb4e6d1a08c72fcc/` sowie alle roten/gruenen
host-/vfs-/service-/ext2-/image-Unterverzeichnisse. Die acht geaenderten
Kernelmodule wurden zwischenzeitlich fuer i386 kompiliert; der neue Storage-
Zwischenlink ist196608 Byte, unter224KiB Recovery-Cache. OBJGDTST laesst sich
mit dem bestehenden SDK plus aktueller Header-/libc-Anbindung linken.
Das ersetzt keinen aktuellen Image- oder Performancebeweis.

Abschlussreview fand eine fehlende direkte Lifecycle-Anbindung: bisher nur
Spawnfehler statt gemeinsamer Normal-/Fault-/Terminate-Pfad. Echter extrahierter
process_close_all_files-Aufruf gegen VFS/Pins ist zuerst rot (cleanup-red.log),
dann nach Korrektur O0/O2 gruen (cleanup-green.log). Contention behaelt den
begrenzten deny-only Poll-Backstop. Ausserdem korrigiert die neue Gastfixture
ihren svcctl-Pfad auf das vorhandene /sbin-Layout (command-path-red.log).
Die letzte komplette Guard-Gruppe ist command-path-green.log:5 Tests/13.349s.
Die sieben anderen unveraenderten gezielten Gruppen bleiben wie oben gueltig.

Zwischenstaende der Referenzbuilds PASS: VMware78s
`build/codex-agent/20260908-230610-package-vmware-vga.log`, QEMU75s
`build/codex-agent/20260908-230747-package-qemu-vga.log`.
Fruehere erfolgreiche Buildlogs bleiben als Zwischenstaende erhalten.
`artifacts/protected.json` bestaetigt beide neu gebauten Kernel in ihren Images,
alle gepackten Programme und die fuenf unveraenderten Schutzprogramm-Hashes.
Diese Imagepruefung ist nach der folgenden Gastkorrektur NICHT die finale
Abnahme. Alle Zwischenbelege bleiben erhalten.

Gast `guest/` scheiterte am Boot: die vorhandenen Backend-Offsets sind relativ
zum gemounteten Drive, nicht zum physischen Parent. Der VFS-Mediator addiert
jetzt den geprueften Partitionsoffset exakt einmal. `partition-red.log` zeigt
den reproduzierten Openfehler; `partition-green.log` beweist O0/O2 auch rohe
I/O-Grenzen und Ablehnung des ueberlappenden Parent-Alias.
Die private Floppy-Fixture serialisiert die bestehenden gepackten32-/28-Byte-
Journal-/Remapheader jetzt korrekt. Native echte Decoder zeigen den bisherigen
Vorlagenfehler (`floppy-decoder-red.log`) und die Korrektur. Der generische
Imagebuilder bleibt unberuehrt; kein Datentraegerformat wurde geaendert.
Letzte komplette Guard-Gruppe:6 Tests/15.505s PASS in
`partition-floppy-green.log`. Neues QEMU-Image gebaut in
`build/codex-agent/20260908-231724-package-qemu-vga.log` (26s PASS).
`guest-partition/` bootet/mountet; die neue Fixture erwartete faelschlich EBUSY
direkt aus alten Syscalls. Deren eingefrorene Mappingwerte bleiben -2/-5;
EBUSY wird am echten VFS plus Dateierhalt im Gast bewiesen. `guest-legacy-abi/`
findet danach den fehlenden IPC-Receive-Header der Fixture; ebenfalls korrigiert.
Die Gastabnahme wird fortgesetzt, danach VMware/Imageabgleich erneuert. Kein
Queueuebergang/Commit vor allen Gates, direkter Diffpruefung und Archiv.

### Ausgang und eingefrorener Vertragscheckpoint

Sauberer Ausgang024e4ce7. Die weitergefuehrte FAT-/Ownership-Inventur findet
zwei getrennte Tabellen:256 Legacy-VFS-Nodes versus16 Ring-3-Serviceobjekte.
Der vorhandene aliasfeste BUSY-Schutz erfasst nur die erste Tabelle. Aus einer
erneuten Directory-/Inode-Signaturpruefung folgt kein Schutz gegen vollstaendige
Wiederverwendung. Auch lesende EXT2-Einstiege koennen Recovery-Writes ausloesen.

Der vollstaendige Folgevertrag steht in
[FILE_OBJECT_LIFETIME_CONTRACT.md](../architecture/FILE_OBJECT_LIFETIME_CONTRACT.md).
Er konkretisiert feste Identitaetsschluessel, atomare Open-Admission, beide
Besitzer, generationengebundenen Widerruf, kurze Mutationsreservationen und
Fencing beim Verlust des Besitzers. Keine volumeweite Langzeitsperre; keine
neue Kernel-Pfad-/Dateisystempolitik. Der vorhandene FAT32-Journalkern bleibt
vorerst beim aktuellen Besitzer; FAT12-Remaps/Replikate werden nicht umgangen.

Queue und15 Abnahmegruppen sind eingefroren. Es ist genau R3.38 aktiv;
R3.6b bleibt ausdruecklich zurueckgestellt. In diesem Vertragscheckpoint wurden
nur diese Dokumentation und die Queue geaendert. Kein Produktionscode, keine
Images und keine JS-Rechte geaendert. Struktur/Dateiliste/Queue,32-/112-Byte-
Layoutrechnung und git diff --check sind geprueft; Laufzeitgates
wurden nicht ausgefuehrt und werden nicht als bestanden gebucht.

Der oben dokumentierte Implementierungsstand setzt diesen Vertragscheckpoint
fort; sein vollstaendiger vertikaler Schnitt ist noch offen. Die Schreibbackend-
migration und JS-Schreibdelegation sind damit weiterhin nicht abgeschlossen.

## R3.37 abgenommen: EXT2-Commit-Recovery vor JS3-Schreibrechten

Sauberer Ausgang eb4dcc12, Vertragscheckpoint868ca285. Inventur und Reihenfolge:
[RING3_FILE_WRITE_CONTRACT.md](../architecture/RING3_FILE_WRITE_CONTRACT.md).
Der reale Fehler ist behoben: Widerspruechliche COMMITTED-Zielsektoren duerfen
nicht in den ACTIVE-Undo-Pfad fallen. Die einzige Produktionsaenderung liegt
in ext2_journal_recover: COMMITTED mit nicht finalen Daten liefert EIO vor
jedem Write/Flush. Vollstaendige Commits bereinigen nur die Journalheader;
ACTIVE behaelt seine Undo-Autoritaet. Formatv1, Auswahl redundanter Header,
Quoten, ABI, Kernel und JS-Rechte sind unveraendert.

Alle elf eingefrorenen Gruppen bestanden. Exakte Befehle stehen in der Queue;
abschliessende Belege relativ zu `build/codex-agent/r337-ext2-commit/`:

| Gruppe | Ergebnis / Sekunden | Beleg |
| --- | --- | --- |
| test_ext2_commit_recovery.py |2 Tests, echte Journalpfade O0/O2 und Gastpruefer-Negativfaelle /3.023 |host-commit-final.log |
| test_reist_vfs_symlink.py |4 Tests inkl. bestehender Unterbrechungsmatrix /1.049 |host-symlink.log |
| test_reist_vfs_namespace.py |5 Tests /0.770 |host-namespace.log |
| test_reist_vfs_shadow_ext2.py |2 Tests /0.988 |host-ext2.log |
| test_reist_vfs_file_client.py |6 Tests /0.721 |host-vfs.log |
| test-reist-package.ps1 -Target vmware -Video vga |PASS /79.984 |package-vmware.log |
| test-reist-package.ps1 -Target qemu -Video vga |PASS /75.776 |package-qemu.log |
| verify_js_runner_artifacts.py |92 Programme je Image, Kernel/Beispiele/Schutzhashes /1.517 |artifact-gate.log, artifacts/protected.json |
| run_qemu_ext2_commit_recovery.py |beide echte Gastfaelle,16 Shellbefehle /48.990 |commit-guest-gate.log, guest/result.json |
| run_qemu_ext2_symlink.py |bestehende Namespace-/Restart-Pruefung /88.044 |namespace-gate.log, namespace.log |
| run_qemu_js_runner.py --file-capabilities |bestehende volle JS-/Datei-/Cleanup-Pruefung /119.926 |js-runner-gate.log, js-runner.log |

Die vier bestehenden Hostgruppen laufen ueber measure_cpp_baseline.py mit
unterdrueckten nativen Windows-Fehlerdialogen. Die neue Gruppe setzt denselben
Prozessmodus selbst, mit90s Compiler-/30s Laufzeitgrenzen. Buildvolltexte:
20260908-202909-package-vmware-vga.log und20260908-203029-package-qemu-vga.log.

O0/O2 faengt die tatsaechlich geschriebenen COMMITTED-Header der Produktions-
transaktion vor CLEAN ab. Fuenf geaenderte Zielsektoren werden einzeln und
zusammen auf alte Bytes gesetzt: zweimalige Recovery liefert EIO bei null
Writes/Flushes und identischem Abbild. Korrupte Before-Images, widerspruechliche
gueltige Header, gueltige redundante Einzelkopien, alle drei CLEAN-Write-/Flush-
Unterbrechungen, zweite Recovery, ACTIVE-Rollback und Deadline sind geprueft.

Im echten1024MiB-QEMU-Gast bleibt das widerspruechliche Commit-Abbild nach
Lesen, abgelehnter Mutation und svcctl restart5 bytegleich. Der gueltige Commit
erhaelt alle Nichtheaderbytes und wird zu zwei exakten CLEAN-Headern. Normale
Userspace-Shell und unabhaengige FAT-Datei bleiben vor/nach Restart benutzbar.
Systemimage ist jeweils ein Snapshot. Keine sichtbaren VMs oder Hostdialoge.

Nur STORAGE.PRG unterscheidet sich unter den92 Programmpayloads gegen R3.36;
beide Kernel und die fuenf eingefrorenen Schutzprogramme bleiben bytegleich.
Artefaktpruefer verwendet dieselben geerbten c9bf94ba-Hashpins; auch die neu
gesicherten beiden Kernel sind separat dagegen geprueft. Kein neuer VMware-
Benchmark-/WCET-Nachweis. Images:

- QEMU: d355e963e81db46ab6e74824e3bed1f52e69756896a8c1a67d026ca2f45ab5aa
- VMware: ff4cc0237d524247f9683647d0e038912a490ed50ba05925e95d29a1c6960d33

accepted-reference/ enthaelt105 hashgepruefte Dateien: beide Images/Kernel,
92 Programme, VMware-Descriptor/VMX, JS-Beispiele/Artefaktbericht und drei
EXT2-Beweisimages samt Gastbericht. Hashindex: archive-sha256.json.
Fruehere Belege bleiben unveraendert. host-red.log reproduziert den Fehler vor
der Korrektur in O0/O2; host-commit.log ist der erste erfolgreiche reine
C-Nachweis, host-commit-final.log ergaenzt den Gastpruefer. Keine gelockerten
Anforderungen und keine weiteren fehlgeschlagenen Abnahmegates.

Dies schliesst nur die eigenstaendige Persistenzreparatur. Keine neue
Schreib-Capability, kein allgemeiner Ring-3-Dateischreibpfad, kein persistentes
Kernel-Quarantaenebit und kein Hardware-Power-Loss-Claim. Die dokumentierte
Backend-/Objekt-/Namespace-Grenze ist der naechste JS3-Schritt; kein JS4 und
keine Kernel-VFS-Abkuerzung. Formaler Queueruecksprung auf R3.6b erhaelt dessen
explizite Zurueckstellung; Scripting bleibt priorisiert. Kein Push/Agent.

## R3.36 / JS3 abgenommen: explizite Lese-Capabilities

Sauberer Ausgang c9bf94ba, Vertragscheckpoint fcd0a84c:
[OS_JAVASCRIPT_FILE_CAPABILITY_CONTRACT.md](../architecture/OS_JAVASCRIPT_FILE_CAPABILITY_CONTRACT.md).
Eine vollständige Lese-Autoritätsgrenze, kein allgemeiner Schreib- oder
Verzeichnisbroker. Die Inventur fand nur READ/SEEK/STAT/DELEGATE im vorhandenen
Ring-3-Objektclient. Persistente reguläre Dateiobjekt-Schreiboperationen benötigen
ein eigenes JS3-Paket mit Crash-/Commit-/Quarantänenachweis, bevor JS4 beginnt.

`js --read /htdocs/hello.js /htdocs/readfile.js` delegiert genau die ausgewählte
reguläre Datei. Bis zu vier explizite Grants; Quelltext-/Argumentprüfung vor
Grant-Open, O_NOFOLLOW für den letzten Pfadteil, keine dot-dot-Komponenten.
Intermediate Symlinks können beim vertrauenswürdigen Open aufgelöst werden;
dies ist ausdrücklich keine Verzeichnis-/Subtree-Sandbox. Danach folgen keine
Pfadauflösung und kein automatisches Reopen: Der Host hält generationsgebundene
VFS-Objekte; Worker erhalten weder Pfade noch VFS-Deskriptoren als Autorität.

`reist.files[0]` ist ein natives opakes Objekt: `read(n)` -> ArrayBuffer,
`readText(n)` -> separat UTF-8-dekodierter Chunk, `seek(offset)`, `size()` und
idempotentes `close()`. Kein allgemeiner Decoder, Node fs, open/write, Prozess-,
Netzwerk- oder Adminrecht. Zahlenargumente ohne implizite Konvertierung;
Prototypkopien, Proxy, JSON und fremde Receiver können kein Objekt fälschen.
Browser EVAL und SCRIPT ohne Grants erhalten keinerlei Datei-Binding.

Append-only private Operationen7/8 mit voller Eltern-/Kind-/Dokument-/Anfrage-
Identität:80-Byte-Manifest,32-Byte-Hostanfrage/-Antwort, monotone innere Sequenz.
Slot/Lease sind nur Selektoren innerhalb des autorisierten, generationgeprüften
Endpoints und der expliziten Granttabelle, keine geheimen Bearertokens.
JsSession.poll bleibt auf acht nichtblockierende IPC-Operationen begrenzt;
VFS arbeitet ausschließlich im vertrauenswürdigen CLI-Host außerhalb von poll.
128KiB je Read,16MiB Gesamtdaten,256 Aufrufe, vier Capabilities, unveränderte
5s Ausführungs-/32MiB Engine-/64MiB Worker-Grenzen. Gewöhnliche Dateifehler sind
fangbar; Quoten-/Protokoll-/Zeitfehler vergiften die Ausführung unwiderruflich.
Explizites Close nach Reap vor Konsolenpublikation; GC schließt keine OS-Objekte.
Unklare Freigabe beendet den Host für den vorhandenen Owner-Reap-Pfad.

Alle zwölf Gruppen bestanden. Befehle stehen in der Queue, abschließende Belege relativ zu
`build/codex-agent/r336-js-files/`:

| Gruppe | Ergebnis / Sekunden | Beleg |
| --- | --- | --- |
| Engine/SDK |6 Tests; bestehende Sprache/Konsole und15 Datei-Vektoren je O0/O2 /76.407 | host-engine.log |
| Broker/nativer Worker-Bridge/Transport |O0/O2 mit gefälschten Frames/Leases/Receivern, Quoten und Cleanup /1.874 | host-files-bridge-version.log |
| CLI/Admission/Validator |4 Tests /3.091 | host-runner-grants.log |
| Owner/Transport/4 echte Ziellinks |3 Tests /9.650 | host-service.log |
| Native Script-Domäne |2 Tests /1.282 | host-domain.log |
| Vorhandener VFS-Objektclient |6 Tests /1.533 | host-vfs.log |
| VMware-Referenz |PASS /22.741 | package-vmware-fixture.log |
| QEMU-Referenz |PASS /73.990 | package-qemu.log |
| Tatsächliche Image-Inhalte |92 PRGs je Image, beide Beispiele/Kernel /1.351 | artifacts/protected.json |
| Runner-/Datei-Gast |zweimal vollständige Fälle /122.062 | runner.log, runner-gate.log |
| Eingeschränkter Dienstgast |zweimal Fault/Hang/Orphan/Recovery /39.570 | service.log, service-gate.log |
| Browserregression |externe Skripte/Redirect/Cache/Reflow/Cancel/Recovery /97.464 | browser.log, browser-gate.log |

Der neue Gast prüft je Runde die exakten Dateifall-Status0/1/71/124/0,
Read-/Seek-/Stat-/EOF-Inhalt, fehlende Schreib-/Open-APIs, vier gleichzeitige
Dateiobjekte, Abbruch während einer echten Hostanfrage und erneute Vergabe aller
vier Slots. Normale Shell meldet zweimal JS_FILE_SHELL_OK238/64. Die bisherigen
Runner-/Realm-/Source-/Console-Fälle bleiben verpflichtend und bestehen.

Beide Kernel sowie BENCHMARK, MATHTEST, TEXTTEST, CURL und JSTEST sind bytegleich
zu c9bf94ba. Kein neuer VMware-Leistungs-/WCET- oder vollständiger OS-Scripting-
Nachweis. QEMU-Image:
`78ceb876b7114741fedf0e4e734b28f2169ecd4d6a2965cb36c5e9c7e05d3a29`.
VMware-Image:
`7448e05d3abfb324f8c203223c5ebe6524a4b314e8878997c934840b69d1e1cb`.
`accepted-reference/` sichert101 Dateien: beide Images/Kernel,92 Programme,
VMware-Descriptor/VMX, beide JS-Beispiele und den unabhängigen Imagebericht.
Alle Kopien hashgeprüft; frühere Referenzen und Fehlerbelege unverändert erhalten.

Erhaltene rote Belege: r336-js-files-red.log im Elternordner (Broker zunächst
nicht implementiert); host-files-bridge.log (C-Bridge im C++-Fixture brauchte
expliziten void*-Cast); host-files-bridge-cast.log (falscher negativer Manifest-
Vektor: jede Nichtnull-Lease ist strukturell gültig, Bindung prüft erst Broker);
host-files-bridge-lease.log (Fixture verwendete v1 statt Bulk-IPC-Version2);
package-vmware.log (Gastfixture nutzte im SDK nicht deklariertes strcpy).
Gezielte Korrekturen, verschärfte Host-Bridge-/CLI-Vektoren und nur betroffene
Wiederholungen; keine gelockerten Rechte, Quoten oder Prüferanforderungen.

Nächster fachlicher Schritt ist weiterhin JS3: stabile schreibbare Ring-3-
Dateiobjekte mit explizitem Persistenz-/Recoveryvertrag inventarisieren und
einfrieren, dann Schreib-Capabilities anbinden. Kein späteres Paket in diesem
Lauf implementiert. Der formale Rücksprung auf R3.6b erhält dessen ausdrückliche
VMware-Zurückstellung; Scripting bleibt priorisiert. Kein Push/Agent.

## R3.35 / JS2 abgenommen: allgemeiner isolierter JavaScript-Runner

Ausgang ef9fb2de, Vertragscheckpoint23fce927, ausdrücklich genehmigte
Prüferkorrektur888ab9ab. Vertrag:
[OS_JAVASCRIPT_RUNNER_CONTRACT.md](../architecture/OS_JAVASCRIPT_RUNNER_CONTRACT.md).
Protokoll, nichtcopybarer C++-Owner und Worker liegen jetzt in `userspace/js`;
dünne Browseradapter erhalten bestehende Verbraucher. JS.PRG und JSRUNTST.PRG
sind in beiden Images unter `/usr/bin` aus der normalen Ring-3-Shell erreichbar.

`js -e print(42)` und `js /htdocs/hello.js test` führen Skripte in einem eigenen,
nativ eingeschränkten JSWORK aus. Argumente über `scriptArgs`, `print`,
`console.log/error` und `reist.setExitCode(0..125)`. Nur der Host liest die
angeforderte Quelldatei und schreibt vollständig validierte Konsolenjournale
an seine stdout/stderr-Deskriptoren. Keine Datei-, Netzwerk-, Prozess-, GUI-
oder administrativen Skriptrechte. Keine neue Kernel-/SDK-Core-Autorität.
Browser und CLI teilen Code, niemals Realm, Heap oder implizite Hostrechte.

Alle10 Gruppen bestanden, einschließlich der abschließenden Fixturekorrektur
und der betroffenen Referenz-/Runner-/Image-Gates. Logs relativ zu
`build/codex-agent/r335-js-runner/`:

| Gruppe | Ergebnis / Sekunden | Beleg |
| --- | --- | --- |
| Engine/Console/SDK |6 Tests, Sprache und11 neue Konsolenfälle je O0/O2 /75.288 | host-engine.log |
| Runner/Admission/Output |2 Tests mit echten O0/O2-Pfaden /21.076 | host-runner-cpp.log |
| ergänzende Runner-/Browser-Validatorfälle |0.018/0.066/0.061 | host-validator.log, host-browser-trace-identity.log, host-browser-legacy.log |
| Transport/Owner/4 Ziel-Links |3 Tests /16.432 | host-service.log |
| Native Script-Domäne |2 Tests /7.932 | host-domain.log |
| VMware-Referenz |PASS /86.304 | package-vmware-identity.log |
| QEMU-Referenz |PASS /84.450 | package-qemu-identity.log |
| Tatsächliche Image-Inhalte |92 PRGs je Image, Beispiel, beide Kernel /1.401 | artifacts-final/protected.json |
| Neuer Runner-Gast |zweimal vollständig /77.566 | runner-identity.log, runner-identity-gate.log |
| Eingeschränkter Dienst-Gast |zweimal Fault/Hang/Orphan/Recovery /50.161 | service.log, service-gate.log |
| Browserregression |PASS /103.094 | browser-trace-fixed.log, browser-trace-fixed-gate.log |

Die ergänzenden Validatorfälle gehören zur Runner-/freigegebenen Prüfergruppe,
nicht zu einem weiteren Paket. Nachträglich ergänzte reine Validatorfälle
liefen gezielt einzeln; unveränderte Kompilier-/Gastgruppen wurden nicht wiederholt.
Der erste Ziel-Linkbeleg enthält noch eine harmlose fehlende Nullinitialisierer-
Warnung; beide abschließenden Referenzen enthalten die explizite Null.

Die Schlussprüfung korrigiert eine falsche Testannahme: Generationen sind
slotgebunden, nicht global eindeutig. Direkt aus einer frischen Shell bestehen
zwei gleichzeitig lebende Worker mit PID8/9 und jeweils Generation1. Nur
JSRUNTST.PRG änderte sich gegenüber dem ersten bestandenen Kandidaten; alle91
anderen Programme und beide Kernel sind hashgleich. Damit bleiben dessen
Dienst-/Browser-Gastbelege gültig. Referenzen, Imageprüfung und Runnergast wurden
für die korrigierte Testversion erneut bestanden; frühere Belege bleiben erhalten.

Der neue Gast prüft parallele Script-/Browser-Realms, fehlende Fremdglobals,
frischen Worker, tatsächliche Deadline/Cancel und Reap, stdout/stderr, normale
Exit7, Syntax-/Scriptfehler1, nicht wegfangbares Konsolenlimit71, Argumente als
Daten und1MiB Quelle. Bestehender Dienstgast bestätigt zweimal alle nativen
Verbote, Fault142, bestätigten Hang/Reap143, Stale/Cancel, frische Generationen
und Orphan-Freigabe. Browser prüft lokale/große HTTP-Skripte, Redirect, Cache,
Reflow, echte CURL-Terminierung143, Recovery, Titelpixel und lebende Shell.

Kernel beider Profile sowie BENCHMARK, MATHTEST, TEXTTEST, CURL und JSTEST
sind exakt bytegleich zu ef9fb2de. Kein neuer VMware-Durchsatz-/WCET-Nachweis.
`accepted-reference-final/` sichert100 Dateien: beide Images/Kernel,92 PRGs, VMware-
Descriptor/VMX, hello.js und Imagebericht; Kopien vollständig hashgeprüft.
Das erste Archiv `accepted-reference/` bleibt ebenfalls unverändert erhalten.
QEMU-Image: `28bd70edc7fc685d9958e44456cd3de6e51013cd9c4996b62c9818ec6b299903`.
VMware-Image: `29eeca0afbac0b5e322f94fcf35efe08b0b91085a69fda7a231098555e3da64e`.

Erhaltene Entwicklungsfehler: red.log (Runner fehlt zunächst), host-runner.log
(Host-Zig-Cache außerhalb Workspace), host-runner-cache.log (C++-memchr-Cast),
package-vmware.log (fehlendes extern-C an beiden neuen main-Einstiegen),
browser.log (vollständiger Browserlauf mit durch zwei ARP-Diagnosen unterbrochener
Worker-Startzeile). Der Rohbeleg bleibt fehlgeschlagen und unverändert.
red-browser-trace.log reproduziert dies. host-browser-trace.log enthielt einen
falschen Negativvektor, der Start UND Reap gemeinsam umnummerierte; der
korrigierte Vektor verändert nur die Reap-Generation und wird sicher abgelehnt.
Die genehmigte Auswertung entfernt höchstens128 vollständige Instanzen genau
zweier bekannter Diagnosen nur in einer abgeleiteten Identitätssicht; alle14
Identitäten/Reaps, Status-/Fehler-/Zeit- und Browseranforderungen bleiben erhalten.

Grenzen: Shell hat noch kein Quoting; längere Quellen aus Dateien laden.
Konsole ist begrenzte, verzögert publizierte Textausgabe, keine Streaming-/
Binärkonsole. Kein REPL, Modulloader, Node/qjs/std/os oder administrativer Host.
Nächste Etappe JS3: explizite Capability-Objekte und dateigebundener Broker;
vor Umsetzung dessen Autoritäts-/Persistenzvertrag einfrieren. R3.6b bleibt
trotz formalem Queue-Rücksprung ausdrücklich zurückgestellt. Kein Push/Agent.

## R3.34 abgenommen: Script-Prozessdomäne und OS-Scripting-Arbeitspapier

[OS_JAVASCRIPT_SCRIPTING_WORK_PAPER.md](OS_JAVASCRIPT_SCRIPTING_WORK_PAPER.md)
legt die sechs Etappen für gemeinsame Sprachimplementierung und getrennte
Browser-/Benutzer-/System-Hosts fest. Ausgang270754bd, Vertragscommit3a0a3148.
Nur die erste Etappe R3.34 ist umgesetzt und abgenommen; noch kein allgemeines
JS.PRG und keine administrativen JS-Bindings. Direkte Hauptworktree-Ausführung,
keine Agenten, sichtbaren Test-VMs, Windows-Fehlerdialoge oder Pushes.

Syscall128 PROCESS_RESTRICT entzieht dem aufrufenden Prozess unwiderruflich
Ambient-Rechte. Versionierter16-Byte-Request, Profil SCRIPT; nur Compatibility
-> Script und idempotente erneute Einschränkung. Ungültige Anfragen oder ein
bereits zu großer Heap verändern nichts. Interne Profilversion2 mit fünf
Bitmapwörtern; bisherige öffentliche Nummern und neun Profile bleiben erhalten.
Nur14 Syscalls: privater Heap, eigene Lebensdauer, monotone Zeit, begrenztes
gerichtetes IPC auf delegierten Handles, eigene Prozessinfo sowie eigene/
generationengeprüfte Elternidentität. Kein VFS, Netzwerk, Spawn/Kill, Geräte,
GUI, Terminal, Service-Connect oder selbständige IPC-Erzeugung/-Delegation.

JSWORK beschränkt sich vor dem Engine-Start, scheitert ohne Kernelunterstützung
geschlossen und gibt delegierte Endpoints mit IPC_RELEASE frei.64MiB privater
Workerheap/32MiB Engine wie bisher; niedrigeres RAM-abhängiges Budget und
physische Recoveryreserve bleiben wirksam. Keine neue Allokation im
Rechteübergang, keine Änderung der Scheduler-/Framebuffer-Hotpaths. Der neue
SDK-Wrapper ist inline: BENCHMARK, MATHTEST, TEXTTEST, CURL und JSTEST bleiben
byteidentisch. Dies ist kein neuer VMware-Durchsatz- oder Zertifizierungsnachweis.

Alle11 eingefrorenen Gruppen bestanden. Belege relativ zu
`build/codex-agent/r334-script-domain/`:

| Gruppe | Ergebnis / Sekunden | Abschließender Beleg |
| --- | --- | --- |
| ABI/Projektionen | 5 Tests /0.117 | abi-inline-sdk.log |
| Native Domäne | 2 Tests, echte C-Pfade O0/O2 /47.635 | domain-crt-corrected.log |
| Private Speicherpfade | 10 Tests /57.025 | memory-host.log |
| JS-Protokoll/Owner/Ziel-Link | 3 Tests /3.993 | js-host-ipc-errno.log |
| VMware-Referenzpaket | PASS /83.951 | package-vmware-ipc-errno.log |
| QEMU-Referenzpaket | PASS /79.881 | package-qemu-ipc-errno.log |
| Tatsächliche Image-Inhalte | 90 Programme je Image, beide Kernel /1.662 | artifacts-final/protected.json |
| Eingeschränkte JS-Prozesse | zweimal vollständiger Zyklus /48.430 | js-runtime-ipc-errno.log |
| Externe Browser-Skripte | große Ressourcen, Redirect/Cache/Reflow/Cancel/Recovery /99.848 | browser-external.log |
|2560x1440 Browser | drei Max/Normal-, zwei Radzyklen, Fault/Ersatz/Shell /157.981 | guest2560/status.json |
| Memory-Resilience | PASS /143.833 einschließlich Build; Gast46s | memory-resilience-gate.log |

Der native Test versucht alle verbotenen Syscallnummern einschließlich
ungültiger Pointer, Profilaufweitung, fremder Identität, falscher IPC-Richtung
und Fake-Handles. Beide Gästezyklen erhalten DOMAIN_OK, echte PF142 an Adresse4,
IPC-bestätigten Hang mit Timeout/Reap143, stale/cancel-Reap143, frische leere
Realms und vollständiges Orphan-Reap.12 unterschiedliche Workeridentitäten;
keine unerlaubten direkten Worker-Terminalmarker. Der Browser-Gast ersetzt
PID9 nach isoliertem Fehler durch PID22, Desktop und Shell bleiben nutzbar.

`accepted-reference/` sichert100 Dateien: beide Images/Kernel,90 Programme,
VMware-Descriptor/VMX, unabhängigen Imagebericht und separates Memory-
Beweisimage/-Kernel/-Log. Archivkopien gegen alle90 Programmhashes und beide
Image-/Kernelhashes geprüft; R3.33 und frühere Belege bleiben unberührt.
QEMU-Image: `d03cd7eb6c498f21f0657881121fdb254cb6bb1723c62c18a6b5dd3f6ae7062b`.
VMware-Image: `362e699ea4665066b6889a59ec9e29001666125abc43022743896826d561c844`.
Fehlversuche, Testkorrekturen und SDK-Codeverschiebung sind im Arbeitspapier
offengelegt und als rote Belege erhalten; keine Schutz-Hashes gelockert.

Nächster fachlicher Schritt: JS2-Vertrag für allgemeinen Shell-Runner und
wiederverwendbaren Host-Transport einfrieren. Keine spätere Etappe in diesem
Lauf implementiert. Formaler Queue-Rücksprung auf R3.6b erhält dessen bisherige
Zurückstellung und sämtliche offenen VMware-Gates; Scripting hat weiter Vorrang.

## R3.33 abgenommen: Browserinhalt auf großen Desktops

Sauberer Ausgang `bd421a50`, Vertragscheckpoint `4ef25ec5`. Der größere
Fensterrahmen traf auf unabhängige1024x768-Grenzen in Surface-Konfiguration,
Kernelpuffer und CSS/Layout/Raster sowie nur8MiB gemeinsamen Pufferspeicher.
Ein zusammenhängendes Paket gemäß HIGH_RESOLUTION_SURFACE_CONTRACT;
direkte Hauptworktree-Ausführung, keine Agenten, sichtbaren VMs oder Pushes.

Geometrieprofil2 erlaubt positive Dimensionen bis4096 je Achse UND maximal
4.194.304 XRGB8888-Pixel/16MiB, entsprechend der vorhandenen Displaygrenze.
Wire-Strukturen, Opcodes und Configure/ACK-/Generationsprotokoll bleiben
unverändert. Das ist keine3840x2160-Unterstützung und keine Zusage für
gemischte alte Clients. Beide Images enthalten passend neu gebaute Clients.

Der vorhandene Kernel-Puffer-Mediator reserviert einmalig bei erster Nutzung
einen begrenzten64MiB-Cache über den bestehenden Heap, nach Prüfung des
freien physischen Speichers einschließlich1/16 Recoveryreserve. Keine64MiB
zusätzliche ELF-BSS; Loadergrenze bleibt64MiB. Acht Slots, Bitmap, unveränderliche
Publikation, Generationen und Referenzfreigabe bleiben erhalten. Der globale
Cache behält sein Backing; Reap gibt die instanzgebundenen Blöcke zur
Wiederverwendung frei. Kein neuer Treiber/Parser oder Allocator-/Schedulerumbau.

Browserpixel werden separat im privaten Prozessspeicher bedarfsgerecht auf
höchstens16MiB erweitert, bei gleicher/kleinerer Fläche wiederverwendet und
beim Beenden freigegeben. Fehlgeschlagenes Wachstum erhält den alten Puffer.
Der feste Dokument-/Bild-/Font-Workspace bleibt unter36MiB. CSS-Anfrage,
Szene, Formulare und Raster verwenden dieselbe Flächengrenze. Rasterarbeit
bleibt vor Schreibzugriff begrenzt: vier Viewport-Pässe,4Mi-Untergrenze und
16Mi-Obergrenze. Keine Erhöhung der DOM-, Skript-, Font- oder Bildquoten;
Tippen verursacht weiterhin keine zusätzliche Seitenrasterung/Allokation.

Belege und sämtliche Fehlversuche: `build/codex-agent/r333-resolution/`.
Alle15 eingefrorenen Gruppen bestanden:

- Neue Geometrie-/Mediatorgruppe4 Tests, echte C-Pfade O0/O2:
  `geometry-query-corrected.log`,2.236s. Configure/ACK, Größenwechsel,
  OOM/Reserve,64MiB-Auslastung, ungültige Pixel/Stride/Fläche, Owner-/Consumer-
  Fencing, Referenzfreigabe, Generation-Reuse und Diagnoseparser.
- Browser-Publikation3 Tests: `browser-publication-query-corrected.log`,3.052s;
  alte Eingabe-/Damage-/Fehlerfälle sowie große Pixelpuffer, Wiederverwendung
  und fehlgeschlagenes Wachstum. Surface10: `surface.log`,2.530s;
  Surface-Runtime: `surface-runtime.log`, Exit0; Desktop64:
  `desktop-record-corrected.log`,1.645s.
- CSS/Layout32 Fälle jeweils O0/O2, einschließlich echter TrueType-Glyphen
  bei1600x900,2560x1440,4096x1024 und800x600:
  `css-layout-truetype-corrected.log`,107.221s. Kernel-Link-/Memorygruppe4:
  `memory-layout.log`,6.030s.
- VMware-Referenzpaket `package-vmware-query-corrected.log`,71s;
  QEMU danach `package-qemu-query-corrected.log`,68s. Tatsächliche Imageprüfung
  `artifact-query-corrected-gate.log` / `artifacts-query-corrected/protected.json`:
  aktuelle Kernel und reparierte Payloads; BENCHMARK, MATHTEST, TEXTTEST, CURL,
  JSTEST und JSWORK bleiben byteidentisch. Keine neue VMware-Performancezusage.
- Gast1600x900: `guest1600-query-corrected/status.json`,152.693s. Drei echte
  Max/Normal-Zyklen, volle rechte/untere Browserfläche und CSS-Zentrierung aus
  Scanout, breites Eck-Resize, zwei Radzyklen, echter Browser-UD2, bedienbarer
  Desktop/frische Shell und anschließend neuer Browser mit erneutem Maximieren.

- Gast2560x1440: `guest2560/status.json`,159.010s, dieselben vollständigen
  Größen-/Pixel-/Rad-/Fault-/Shell-/Ersatzbrowserprüfungen. Beide Modi verwenden
  echte Eingaben, akzeptierte Szenen UND konfigurierte/ACK-bestätigte Fenster;
  Browser-PID9 wird nach isoliertem Abbruch durch PID22 ersetzt. Die Normal-/
  Max-/Ersatzbrowserbilder sind erhalten; maximierter Scanout visuell geprüft.
- Unveränderter kleiner Layout-/Resize-/Fault-/Hang-/Recoverygast:
  `resize-gate.log` / `resize.log`,81.944s.
- Unveränderter Browser-Eingabegast: `browser-input-gate.log`,102.525s;
  URL-Bearbeitung, Navigation, Crash, Neustart und Konsole. Nur die beiden
  frisch erzeugten kanonischen Eingabebelege wurden nach `input-final/` kopiert.
- Memory-Resilience: `memory-resilience-gate.log`,216.577s einschließlich
  separatem Beweisimage-Build; eigentlicher Gast44s. Bestehende injizierte
  Speicherschutz-/Recoveryfälle enden mit EXCEPTIONS_OK, TEST_OK und Shell.
  Frühere Speicherbelege vom6. September unter `prior-memory/` erhalten.

`accepted-reference/` enthält26 Dateien: beide geprüften Images und Kernel,
VMware-Descriptor/VMX,14 Programme, aktuelle Eingabebelege, separates
Memory-Beweisimage/-Kernel/-Log und den Imageprüfbericht. Archivkopien wurden
gegen die akzeptierten Image-/Kernelhashes geprüft:
QEMU `76004365078092f46c43dd319399e22a8c87bcbd1e37d7b8a4a271d39a877b6b`,
VMware `62f8dd472f61ae54d6cc382d60fcbf321af7e17adffa52421ac98934c3ef763c`.
Vorherige R3.31/R3.32-Referenzen und sämtliche roten Belege bleiben erhalten.
Formaler Queueübergang nur auf R3.6b mit unveränderter Zurückstellung und
Browserpriorität; keine Implementierung dieses späteren Pakets.

Gezielte Testkorrekturen mit erhaltenem roten Beleg: Hostpfade/Stub-Linkage;
CSS-Host hat wie HTMLWORK genau eine Dokumentlebensdauer je Prozess, nicht
mehrere Renderings auf demselben verworfenen Testheap. Serielle Ausgaben
können auch bei einem einzigen Schreibaufruf ineinanderlaufen. Der neue
Gast akzeptiert nur vollständige Datensätze und kann eine rein lesende
Viewport-Abfrage wiederholen. Separater opt-in Beobachter für wiederholtes
Resize statt des alten Einmal-Reflow-Testzustands; dessen Gate bleibt
unverändert. Testannahmen zu18px-Scrollbar, sechs Geometriefeldern und
ausreichender Scrollstrecke korrigiert. Keine Produktionsgeometrie oder
Erfolgsmeldung wird vom Host erfunden. Alte VMware-Zurückstellung bleibt.

## R3.32 abgenommen: rechte Fensterknöpfe und Taskleisten-Minimierung

Sauberer Ausgang `ef7f7d2f`, Vertragscheckpoint `343b4782`. Genau ein
privater Ring3-Compositor-Zustands-/Capture-/Geometriepfad; keine Agenten,
sichtbaren VMs, Hostfehlerdialoge, öffentlichen ABI- oder Kerneländerungen.

Rechts stehen Minimieren und Maximieren/Wiederherstellen mit wechselndem
Fenstersymbol. Schließen bleibt links. Die komplette Knopffläche erfasst
die Maus; außerhalb oder auf einem anderen Knopf loslassen bricht ab.
Maximieren nutzt den Arbeitsbereich oberhalb der Taskleiste, Wiederherstellen
die gespeicherte Normalgeometrie. Maximierte Fenster werden nicht gezogen
oder über ihre Kanten vergrößert. Dialoge haben keine Zustandsknöpfe;
lebende Dialoge sperren diese Aktionen am zugehörigen Elternfenster.

Minimieren erhält Prozess, Surface, Inhalte, Geometrie und Taskleistenknopf,
entzieht aber Kompositions-/Eingabefokus. Kein CLOSE oder Neustart;
Configure/ACK läuft begrenzt weiter, versteckte Paint-Schäden werden ohne
Desktop-Neuzeichnen konsumiert. Taskleistenklick stellt den vorherigen
Normal-/Maximiert-Zustand wieder her; Klick auf das fokussierte sichtbare
Fenster minimiert. Instanzgebundener Capture und idempotentes Schließen/
Retirement verhindern Zustandsübernahme durch wiederverwendete Fensterslots.
Rechter Live-Resize beschädigt zusätzlich nur den überstrichenen Titelbereich
für die mitwandernden Knöpfe, nicht pauschal den ganzen Client.

Alle10 eingefrorenen Gruppen bestanden; Belege inklusive Fehlversuchen unter
`build/codex-agent/r332-window/`:

- Desktop-Host64 Tests/O0+O2: `desktop-host-focus-corrected.log`, PASS2.136s.
  Alle Knopfpixel, Abbruch/Überdeckung, winzige/extreme Geometrie, acht Slots,
  Fokus, wiederholte Zustandswechsel, Dialogsperre, Reuse und Damage.
- Unveränderte Surface-Hostgruppe10: `surface-host.log`, PASS1.949s;
  Surface-Runtime-Host: `surface-runtime-host.log`, Exit0.
- Referenzpakete: `package-vmware-focus-corrected.log`, PASS78s;
  danach `package-qemu-focus-corrected.log`, PASS69s.
- Imageprüfung: `artifact-hash-corrected.log`, PASS;
  `artifacts-hash-corrected/protected.json` vergleicht beide tatsächlichen
  Kernel und13 Programme bytegenau mit der akzeptierten Referenz.
- Neuer Fenster-Gast: `window-title-corrected.log` und
  `guest-title-corrected/status.json`, PASS28.389s. Reale Knopf-/Taskleisten-
  Eingaben, Paint/Configure/ACK, gleiche PID9 und privater Bearbeitungsstand
  nach Min/Max/Normal; echter maximierter Applet-UD2, saubere Ersatz-PID10
  mit neuer Fensterinstanz, Explorer-Wechsel und frische Shellantwort.
  Normal-/Maximiert-Screenshots visuell geprüft; Glyphenwechsel und exakte
  Wiederherstellung werden zusätzlich aus dem tatsächlichen Scanout geprüft.
- Unveränderter Maus-Applet-Gast: `mouse-gate.log`, PASS36.913s.
- Unveränderter innerer Resize-/Layout-/Rad-/Fault-/Hang-/Recoverygast:
  `resize-gate.log`, PASS89.192s.
- Unveränderter Browser-Input-/Navigation-/Crash-/Restart-/Konsolengast:
  `browser-input-gate.log`, PASS; Wrapper106.694s.

Gezielte Korrekturen mit erhaltenem roten Beleg: ursprünglicher Caption-
Treffer war Move/Resize; Dialogsperre wird nun erst nach Veröffentlichung
aller neuen Eltern-/Dialogfenster berechnet, aber vor der nächsten Eingabe.
Ein gesperrter Knopf darf bereits beim Drücken keinen Fokus stehlen.
Kleine Titel werden ohne unsigned-Unterlauf geclippt. Der neue Image-Harness
enthielt beim CONFIG-Referenzhash ein zusätzliches Zeichen: anhand beider
archivierter Images und des archivierten PRG korrigiert, keine Byteänderung.
Der neue Gast-Harness nahm zunächst22 statt der tatsächlichen24 Titelpixel
an; korrigierte Client-Geometrie, unveränderte ACK-/Pixelanforderungen.
Nur betroffene Gates gezielt wiederholt, vorherige Fehlschläge nicht gelöscht.

`accepted-reference/` sichert beide Images, QEMU-Kernel,14 PRGs, VMware-
Deskriptor/VMX und ausschließlich die zwei frisch erzeugten kanonischen
Browser-Inputbelege (21 Dateien); Kopierhashes und Imagehashes gegen das
bestandene Gate geprüft. Vorherige Referenzen bleiben erhalten. Formaler
Queueübergang auf R3.6b mit unveränderter Zurückstellung, Browserpriorität
und Gates; kein weiteres Paket implementiert. Keine neue VMware-Laufzeit-
oder Benchmarkmessung behauptet. Doppelklick-Maximierung, Snap und
Drag-to-Restore sind nicht Teil dieses Pakets.

## R3.31 abgenommen: eigenes Maus-Applet in der Systemsteuerung

Sauberer Ausgang `e5255931`, Vertragscheckpoint `7d1d6504`; freigegebene
Inventartest-Ergaenzung `3f1f7dfa`. Direkte Umsetzung, keine Agenten,
sichtbaren VMs, Fehlerdialoge oder Pushes.

MOUSE.PRG bietet native Schieberegler, Tasten-/Profilwahl, natuerliches
Scrollen, Doppelklicktest, Standard/Speichern/Schliessen und Tastaturbedienung.
Alle5 vorhandenen Keys werden in einer validierten CONFIG-Transaktion
gespeichert. Ein eigener Kindprozess mit Identitaetskontrolle, begrenztem
Abbruch und vollstaendigem Ruecklesen verhindert falsche Erfolgsmeldungen.
Start ueber den generationgebundenen Control-Panel-Broker; Shell `mouse --list`
und beide Image-Layouts sind eingebunden. Bestehender Display-Aufruf bleibt.

Wirksam beim naechsten Desktopstart, ausdruecklich kein Livewechsel. Einmaliges
Konfigurationslesen, keine neue Allokation/Dateizugriffe im Eingabepfad.
Flat100/links/normal/500ms erhaelt das bisherige effektive 1:1-Verhalten;
adaptive war zuvor ein unbenutzter Konfigurationswert. Fraktionale Skalierung,
Generationreset, begrenzte adaptive Verstaerkung, Tasten-/Radabbildung und alle
Desktop-/Explorer-Doppelklickverbraucher nutzen das validierte Profil.
Keine Kernel-/Treiber-Aenderung oder Behauptung einer VMware-Hardwareabnahme.

Alle14 eingefrorenen Gruppen bestanden. Vollstaendige Belege einschliesslich
Fehlversuchen unter `build/codex-agent/r331-mouse/`:

- Maus-Host: `mouse-host-i386-corrected.log`,4 Tests/O0+O2,4.293s.
  Reale UI/Controller, CONFIG-Writer/Fehler, Kindprozess-/Broker-Grenzen sowie
  34.496 Vergleiche pro Optimierung fuer Skalierung/Rest/Saettigung.
- Bestehende Hostgruppen: Config3/0.803s, Desktop63/2.820s,
  Explorer2/0.824s, Surface10/2.049s, Surface-Runtime PASS,
  Shell33/1.139s, Display6/4.303s. Finaler Desktopbeleg
  `desktop-host-inventory-corrected.log`, Surfacebeleg
  `surface-host-section-corrected.log`; uebrige `test_*.py.log`.
- VMware-Paket91s und QEMU-Paket75s:
  `package-vmware-section-corrected.log`,
  `package-qemu-section-corrected.log`.
- Image-Gate: `artifact-fat-alias-corrected.log`, PASS; unabhaengiger
  FAT-/Kernelvergleich `artifacts-fat-alias-corrected/protected.json`.
  BROWSER/HTMLWORK plus7 geschuetzte Programme und beide Kernel bytegleich
  mit der akzeptierten R3.30-Referenz. Auch MOUSE/CONTROL und gespeicherte
  Defaultbytes stimmen in beiden Images mit ihren Buildquellen ueberein.
- Echter Mausgast: `guest-right-button-corrected/status.json`,
  PASS36.582s. Native Eingabe/Paint, Save/Close/Reopen, isolierter UD2-Absturz,
  neue Instanz, Desktopneustart; 20/12 HID-Schritte ergeben anhand der Pixel
  30/18 Zeigerpixel, rechte physische Primaertaste, Rad -120,750ms-Profil,
  Defaultwiederherstellung und frische Shellantwort. Screenshot visuell geprueft.
- Unveraenderter innerer Resize-/Layout-/Rad-/Fault-/Hang-/Recoverygast:
  `resize-gate.log`, PASS84.641s.
- Unveraenderter Browser-Inputgast: `browser-input-gate.log`,
  PASS keyboard-edit-navigation-crash-restart-console, ca.100.204s
  einschliesslich Wrapper, gemessen ueber dessen Logzeitstempel.

Gezielte Reparaturen nach belegten Fehlschlaegen, keine Gateabsenkung:
normalisierte Tastaturereignisse ohne Pointer-Pressed-Flag, kleiner Paintclip,
neunter GUI-Pfad im freigegebenen Inventartest; Core ohne optionale libc und
ohne i386-64bit-Divisionshelfer (gleichwertige native Division mit breitem
Produkt). Der optionale Maus-Clientaufruf liegt angehaengt in eigener
ELF-Section: GC entfernt ihn aus unbeteiligten Clients, statt deren Browser-
Bytes mitzuveraendern. Image-Harness verwendet den bestehenden FAT-Alias
INPUT~1.CON; echter Gast liest weiterhin input.conf. Der unveraenderte
Browser-Testhelfer erlaubt nur links, daher sendet ausschliesslich der neue
Maus-Harness rechte Tasten direkt als native QMP-Ereignisse.

Akzeptierte Referenz unter `accepted-reference/`: beide Images,
QEMU-Kernel,14 PRGs sowie die beiden frisch erzeugten kanonischen
Browser-Inputbelege. Kopierte Image-/Kernel-/Programmhashes nachgeprueft.
Alte R3.30-Belege bleiben erhalten. Queueuebergang auf R3.6b ausschliesslich
nach Paketprotokoll; dessen Zurueckstellung/Prioritaeten/Gates bleiben
unveraendert, keine Umsetzung eines weiteren Pakets in diesem Lauf.

### Historie vor der Abnahme

Freigegebene Umfangsergaenzung nach expliziter Rueckfrage: der bestehende
`test/test_desktop_source.py` zaehlt exakt acht GUI-Programme. MOUSE ist das
neunte; der Test wird um dessen konkreten Pfad ergaenzt. Alle14 Gates bleiben
unveraendert. Implementierung sichtbar, aber noch nicht abgenommen: neue
Maus-Hosttests bestehen; SDK-Buildfehler (`string.h` im freestanding Core)
und Desktop-Inventartest werden gezielt korrigiert, Gastgates stehen noch aus.

Sauberer Ausgang `e5255931`. Nutzer verlangt Fortsetzung mit dem Maus-Applet.
Inventar: alle5 Keys bereits vorhanden, aber nur generische Geschwindigkeits-
Umschaltung in Control Panel; keine Desktopverbraucher, Doppelklick fest500ms.
R3.31 friert den gesamten zusammenhaengenden Einstellungs-/Persistenzschnitt
einschliesslich nativer UI, bestehendem atomarem CONFIG-Writer, autorisiertem
Appletstart und naechstem Desktopstart ein. Details und Grenzen im
`MOUSE_SETTINGS_CONTRACT.md`;14 feste Gruppen. Default flat100 entspricht
vorherigem effektiv unskaliertem Verhalten, adaptive war bisher wirkungslos.
Kein Livewechsel, Kernelumbau oder allgemeiner VMware-Abschluss. Zuerst
Vertragscheckpoint, danach genau dieses Paket im sichtbaren Worktree.

## R3.30 abgenommen: ganze Resize-Ecke; Maus-Applet als Naechstes

Neuer Nutzerauftrag auf sauberem `814fc7b7`: ganze sichtbare Resize-Ecke
benutzbar machen. Ursache im Ring3-WM bestaetigt:16px-Griff, aber nur6px-
Randstreifen im Hit-Test. Vertragscheckpoint `aa43fce7`, danach genau R3.30
im sichtbaren Worktree. Gemeinsamer privater16px-Extent fuer Dekoration und
Hit-Test: komplette innere Ecken waehlen beide Kanten, winzige Abmessungen
werden je Achse auf die halbe Groesse begrenzt. Margin0 bleibt deaktiviert,
Randstreifen6px und Vorrang Close/oberstes Fenster bleiben erhalten.
Keine neue Allokation, I/O, oeffentliche Struktur, Kernel-/Treiber-Aenderung
oder zusaetzliche Arbeit im Motion-/Renderloop; Grip-Pixel unveraendert.

Gefrorene7 Gruppen bestanden. Belege, einschliesslich Fehlversuchen,
unter `build/codex-agent/r330-resize/`:

- `python scripts/measure_cpp_baseline.py --host-test test/test_desktop_source.py -v`:
  `desktop-host-cache-corrected.log`, PASS63 Tests/2.124s. Vollstaendige
  Eckpixel und Nachbarpixel in4 verschobenen/extremen Geometrien, kleine
  Dimensionen0..33, unsichtbar/verdeckt/Close/Client, Capture und exakter
  Anker ohne Sprung sowie bestehende Damagefaelle, nativ O0/O2.
- `python scripts/measure_cpp_baseline.py --host-test test/test_browser_runtime_source.py -v`:
  `browser-runtime-host.log`, PASS29 Tests/8.163s.
- `.\scripts\test-reist-package.ps1 -Target vmware -Video vga`:
  `package-vmware.log`, PASS26s.
- `.\scripts\test-reist-package.ps1 -Target qemu -Video vga`:
  `package-qemu.log`, PASS74s.
- `python scripts/run_qemu_browser_layout.py --verify-artifacts --resize-inset 12 --image build/reist-os.img --log build/codex-agent/r330-resize/artifacts.json`:
  PASS, Kommando1.892s. Unabhaengig extrahierte Kernel und9 geschuetzte
  Programme beider Images, einschliesslich BROWSER/HTMLWORK/BENCHMARK,
  bytegleich `814fc7b7`.
- `python scripts/run_qemu_browser_layout.py --qemu 'C:/Program Files/qemu/qemu-system-i386.exe' --resize-inset 12 --image build/reist-os.img --log build/codex-agent/r330-resize/guest.log`:
  PASS83.530s. Tatsachlicher Zeiger(899,668),16px innerhalb dekorierter
  rechter/unterer Aussenkante, Client800x600 ->480x600 mit korrektem Reflow
  und Scanout. Wheel192px und zurueck, Workerfault134/Hang143 korrekt
  gereapt, alte Seitenpixel erhalten, Recovery, Close und Shell intakt.
- `.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-browser-input -Target qemu -Video vga`:
  `browser-input-command.log`, PASS102.145s; bestehender Gast fuer
  Tastatureditierung/Navigation/Crash/Restart/Konsole unveraendert.

Rote Regression `wm-red.log` bestaetigt vor der Korrektur den Eckfehler
in O0/O2. Erster Desktop-Gatelauf `desktop-host.log` scheitert nur am
Zugriff des vorhandenen Menu-Tests auf den Standard-Zig-Cache ausserhalb
des Workspace; eigener neuer WM-Test bereits gruen. Beide Zig-Cachepfade
explizit nach `build/` gesetzt und nur diese betroffene Gruppe wiederholt.
Keine Assertion abgeschwaecht, keine Fehlerdialoge/sichtbaren VMs/Agenten.

Getestete Images, Kernel,10 Programme und188 Eingabebelege sind unter
`accepted-reference/` gesichert. Vorherige188 Eingabebelege wurden vor
dem Lauf bytegleich mit dem R3.29-Archiv abgeglichen und bleiben dort erhalten.
Die alte VMware-Abnahme wird nur formal als naechstes queued Paket aktiv;
ihre Gates/Deferral bleiben unveraendert, keine neue VMware-Laufzeitabnahme
oder Benchmarkmessung behauptet. Nutzerprioritaet bleibt das Maus-Applet.

Zusaetzlicher Nutzerauftrag waehrenddessen: eigenes Maus-Widget/Applet in der
Systemsteuerung. Direkte naechste Prioritaet nach diesem Fix, vor dem alten
VMware-Paket. Vorhandenes `reist.input/1` hat bereits Geschwindigkeit,
Primaertaste, Beschleunigung, Scrollrichtung und Doppelklickzeit. Das Applet
und die tatsaechliche Anwendung dieser Werte benoetigen einen eigenen
Einstellungs-/Persistenzvertrag nach Bestandsaufnahme der Verbraucher;
kein blosses Speichern wirkungsloser Optionen. Noch nicht implementiert;
separater Folgeschnitt entsprechend Ein-Paket-Regel, ohne erneute
routinemaessige Bestaetigung.

## R3.29 abgenommen: echte TTF-Rasterisierung

Sauberer Ausgang `f2dbc2d5`. Nutzer fordert Fortsetzung und konkretisiert
fehlende TTF-/Antialiasing-Unterstuetzung. Inventar: Browser skaliert aktuell
PSF-Bitmapzeichen; vorhandene Editor-TTFs werden nur offline vorgerastert.
Freigabe wird deshalb als echter Runtime-FreeType-Schnitt umgesetzt, nicht
als weiterer Bitmapfont. Neue gefrorene R3.29-Grenze/Gates im Browserplan und
Queue; altes VMware-Mauspaket wieder mit unveraenderter Deferral queued.
Vertragscommit `a555af1f`, danach genau R3.29 im sichtbaren Worktree.
Echter FreeType2.14.3-TrueType/sfnt/smooth-Pfad im HTMLWORK mit acht originalen
Liberation2.1.5-Faces; keine offline vorgerasterten Ersatzfonts. CSS-Familie,
Groesse und echte Schnitte steuern gemeinsame proportionale Advances und
Graustufen. Privates Szenenprofil6 uebertraegt gepruefte, dicht sortierte
Glyphen/Alpha-Daten, nie Fontdateien oder Parserautoritaet ins Browser-UI.
PSF-/Unicode-Fallback bleibt erhalten; Webfonts/Shaping/Bidi/Kerning und
allgemeine Desktop-TTF bleiben ausserhalb dieses Browserpakets.

Auditkorrekturen: private FreeType-Allokationsquote zaehlt auch die eigene
Allokationsmetadatenstruktur; partielle Initialisierung wird vollstaendig
aufgeraeumt. Rasterquote zaehlt jede sichtbare Glyphen-Ueberlappung separat,
auch Zeichen mit Advance0, plus Linkunterstreichung vor jeder Pixelmutation.
Der schon bisher alternative HTML-/Bild-/Stylesheet-Empfangspuffer benoetigt
das Maximum, nicht die Summe seiner Kapazitaeten. Kein Verbraucher nutzt
einen angehaengten Bereich; ein Kind/Job bleibt exklusiv. Dadurch bleibt das
Workspace unter36MiB, ohne eine Empfangs-/Atlasquote zu verringern. Host und
i386-Compile pruefen alle Kapazitaeten.32MiB Worker/4MiB FreeType/5s/4M bleiben.

Belege und Fehlversuche erhalten unter `build/codex-agent/r329-fonts/`.
Gezielte Wiederholungen nach nachgewiesener lokaler Korrektur:

- `font-host-initial.log`: private FreeType-Standardadapter fehlten.
  `font-host-platform.log`: COFF statt ELF-Section fuer native Windows-Tests.
  `font-host-coff.log`: Include auf existierendes `ftdrv.h` korrigiert.
  Danach `font-host-header.log` PASS43.643s.
- `layout-initial.log` PASS98.726s; nach separater Overlap-Admission
  `layout-overlap.log` PASS98.644s. Original25 Vektoren erhalten, drei neue
  TTF-/Fallback-/Wire-/Pixel-/Overlap-Vektoren jeweils O0/O2.
- `package-vmware-initial.log`: Symbolkollision mit bestehendem PSF-Endsymbol
  und36MiB-Compilegrenze. Neue Worker-Cleanupfunktion umbenannt und redundante
  Pufferkapazitaet korrigiert. `package-vmware-workspace.log`: Imagepfad zu
  tief; Lizenzen in vorhandenes `/usr/share/fonts` gelegt. Keine VFS-Grenze
  aufgeweicht. `package-vmware-license.log` PASS20s.
- Nach Metadaten-Quotenkorrektur und erweitertem echten kaputten cmap-Test:
  `test_browser_fonts-allocation.log` PASS20.987s;
  `test_browser_layout-allocation.log` PASS106.859s;
  `test_css_engine-allocation.log` PASS58.225s.
  `package-vmware-allocation.log` PASS29s. Die drei betroffenen
  FreeType/CSS-Hostgates sind auf genau diesem Quellstand erneut geprueft;
  andere gruen gebliebene Gates werden nicht pauschal wiederholt.

Die Testhost-Prozesse nutzen unter Windows den bestehenden Zig-Fallback und
prozesslokale Fehlerdialog-Unterdrueckung, keine systemweite WER-Aenderung.
Keine Agenten, sichtbaren VMs, SDK-/Kernel-Quellaenderungen oder Pushes.

Gefrorene Abnahmekarte (Kommandooptionen unveraendert, nur benoetigte
Wiederholungspfade wie unten angegeben; alle Pfade relativ zum Belegordner):

- `python test/test_browser_fonts.py -v`: `test_browser_fonts-allocation.log`,
  PASS20.987s, echte FreeType-Glyphen in8 Faces/5 Groessen, Graustufen,
  Originalfont vs. abgeschnittener/kaputter cmap,96 Allokationsfehlerstellen,
  Atlaserschoepfung und Cache, alles O0/O2 und vollstaendiges Cleanup.
- `python test/test_browser_layout.py -v`: `test_browser_layout-allocation.log`,
  PASS106.859s,28 Vektoren O0/O2, darunter genau die bisherigen25.
- `python test/test_css_engine.py -v`: `test_css_engine-allocation.log`,
  PASS58.225s, unveraenderte Bestandsfaelle am echten FreeType/CSS-Build.
- `python test/test_html_engine.py -v`: `test_html_engine-final.log`, PASS,
  Kommando5.36s.
- `python test/test_browser_scripting.py -v`: `test_browser_scripting-final.log`,
  PASS3 Tests/50.762s.
- `python test/test_browser_external_scripts.py -v`:
  `test_browser_external_scripts-final.log`, PASS, Kommando2.48s.
- `python test/test_browser_runtime_source.py -v`:
  `test_browser_runtime_source-final.log`, PASS29 Tests/8.713s.
- `python test/test_gui_browser_source.py -v`:
  `test_gui_browser_source-final.log`, PASS8 Tests/44.686s.
- `python test/test_benchmark_source.py -v`: `test_benchmark_source-final.log`,
  PASS, Kommando0.20s.
- `.\scripts\test-reist-package.ps1 -Target vmware -Video vga`:
  `package-vmware-allocation.log`, PASS29s.
- `.\scripts\test-reist-package.ps1 -Target qemu -Video vga`:
  `package-qemu.log`, PASS107s. Wrapper verweisen auf volle Buildlogs.
- `python scripts/run_qemu_browser_fonts.py --verify-artifacts --image
  build/reist-os.img --log build/codex-agent/r329-fonts/artifacts.json`:
  erster Lauf erkennt Lizenz nicht, weil der bestehende unabhaengige Leser
  LFN absichtlich ueberspringt. Nur neuen Guard auf tatsaechlichen8.3-Alias
  `libera~1.txt` korrigiert; keine Image-/Verifier-/Gastquellen geaendert.
  Wiederholung `--log build/codex-agent/r329-fonts/artifacts-alias.json`:
  PASS1.959s Kommando. Beide tatsaechlichen FAT-Images: originale8 TTF-Faces
  nur im HTMLWORK, Lizenzbytes/Fixtures korrekt; beide Kernel und7
  geschuetzte Programme bytegleich akzeptiertem `f2dbc2d5`.
- `python scripts/run_qemu_browser_fonts.py --qemu
  'C:/Program Files/qemu/qemu-system-i386.exe' --image build/reist-os.img
  --log build/codex-agent/r329-fonts/guest.log`: PASS83.667s.
  Reale800/480px Fensterbreiten,24/40px TTF; `iiii`28/44px und `WWWW`92/152px,
  alle vier Glyphenbilder mit echten Graustufen.161 Glyphen/34799 Alphabytes,
  identische Glyphenpixel nach Resize/Wheel192/zurueck. Sieben eindeutige
  Worker-Generationen vollstaendig gereapt, Fault134/Hang143 mit identischen
  alten Seitenpixeln, frische Recovery und Ring3-shell help. Kein JavaScript
  in der Fixture. PPMs unveraendert erhalten; `guest-wide.png` ist nur eine
  verlustfreie Formatkonvertierung des echten Scanouts, visuell kontrolliert.
- `python scripts/run_qemu_browser_layout.py --qemu
  'C:/Program Files/qemu/qemu-system-i386.exe' --image build/reist-os.img
  --log build/codex-agent/r329-fonts/layout-guest.log`: PASS82.766s,
  unveraenderte bisherige CSS-Geometrie-/Pixel-/Wheel-/Fault-/Recovery-Abnahme.
- `python scripts/run_qemu_browser_external.py --qemu
  'C:/Program Files/qemu/qemu-system-i386.exe' --image build/reist-os.img
  --log build/codex-agent/r329-fonts/external-guest.log`: PASS100.576s,
  unveraenderte lokale/grosse HTTP-Script-/Redirect-/Cache-/Resize-/Abbruch-
  und Recovery-Abnahme samt Pixeln/HTTP-Beleg.
- `.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-browser-input
  -Target qemu -Video vga`: `input-guest-command.log`, PASS,
  rund99.3s (UTC-Erzeugungs-/Schreibzeitfenster des Kommandologs, kein
  Benchmark). Bestehende Keyboard/Edit/Navigation/Crash/Restart/Console-
  Abnahme unveraendert. Voller Gastlog `build/runtime-desktop.browser.log`.

Vorhandene188 kanonische Desktop-Belegdateien vor dem Inputlauf nach
`previous-input-evidence/` kopiert; alte R3.28-Referenzen bleiben erhalten.
Nach allen16 Gate-Gruppen Image-SHA256 nochmals unveraendert zum Byteguard.
Neue Referenzen/Inputbelege unter `accepted-reference/` gesichert.
Direkte Scope-/Diff-/ABI-/Cleanuppruefung; nur aktive R3.29-Dateien.
Queue: R3.29 done, naechstes altes VMware-Paket formal active mit unveraendertem
Deferral; kein Folgepaket implementiert. Lokaler Implementierungscommit als
direktes Kind von `a555af1f`, kein Push.

Offene Grenze/Tradeoff: HTMLWORK enthaelt die acht originalen Schriften und
waechst von890924 auf4143168 Bytes; BROWSER bleibt2854940 Bytes gross und
enthaelt keinen FreeType-Parser. Cache vermeidet Fontberechnung beim Paint/
Scroll, nicht das erstmalige Laden des groesseren Workers. Keine Zusage
unveraenderter Kaltstartlatenz und kein neuer VMware-Performancebeleg;
geschuetzte Kernel-/Benchmarkprogramme sind nachweislich bytegleich.

## R3.28 abgenommen: statisches CSS-Layout

Direkt im sichtbaren, anfangs sauberen Worktree auf `bbeffe56`, genau das
aktive Paket; keine Agenten, sichtbaren VMs oder nativen Fehlerdialoge.
Neue C++-Werte-/Geometrieadapter hinter der bestehenden C-Grenze, echte
LibCSS-Tokens/Kaskade, Boxmodell, Flex/Grid, Link-Buttonboxen und begrenzte
Rundungen/Schatten. Privates append-only Szenenprofil5, alte Profile erhalten.
Getesteter Teilumfang und bewusste Grenzen: BROWSER_ENGINE_PORT_PLAN.md,
Abschnitt R3.28-Adapter. Kein Kernel-/JSWORK-/CURL-/Schrift-/Eventumbau.

Belege ausschliesslich unter `build/codex-agent/r328-css-layout/`:

- `python test/test_browser_layout.py -v`: PASS,25 echte Vektoren jeweils
  O0/O2, `host-layout-subset.log`,115.855s. Parser/Kaskade/Tokenfallbacks,
  Vererbung/Zyklen/leere Werte, Quoten/Overflow, Box/Flex/Grid-Verschachtelung,
  Geometrie, Raster und atomare Szenenannahme, skriptfreie Referenz800/480px.
- `python test/test_css_engine.py -v`: PASS, vorhandene34 Modi,
  `css-final.log`,63.487s.
- `python test/test_html_engine.py -v`: PASS, `test_html_engine-gate.log`,4.327s.
- `python test/test_browser_scripting.py -v`: PASS3,
  `test_browser_scripting-gate.log`,51.233s.
- `python test/test_browser_external_scripts.py -v`: PASS2,
  `test_browser_external_scripts-gate.log`,3.626s.
- `python test/test_browser_runtime_source.py -v`: PASS29,
  `runtime-source-final.log`,11.501s.
- `python test/test_gui_browser_source.py -v`: PASS8,
  `gui-source-final.log`,45.738s.
- `python test/test_benchmark_source.py -v`: PASS9,
  `test_benchmark_source-zig-gate.log`,0.031s.
- VMware-Referenzpaket: PASS77s, `package-vmware.log` verweist auf den
  vollstaendigen unverkuerzten Buildlog.
- QEMU-Referenzpaket: PASS67s, `package-qemu.log`; beide Builds nacheinander.
- `python scripts/run_qemu_browser_layout.py --verify-artifacts --image
  build/reist-os.img --log build/codex-agent/r328-css-layout/artifacts.json`:
  PASS,1.344s Kommando; beide echten FAT-Images und Kernel/Programme/Fixtures
  geprueft. Alle7 geschuetzten Programme sowie beide Kernel bytegleich zum
  akzeptierten Stand; BROWSER/HTMLWORK identisch zu den neuen Buildartefakten.
- Gefrorener Layout-Gast: erster `guest.log` nach44.221s an ungueltigem
  Host-QMP-Radwert gestoppt, beide echten Breiten/Nav-/Button-/Gridgeometrien
  bereits korrekt. QMP erwartet einzelne +/-1-Impulse, nicht +/-4; nur die
  Hostinjektion korrigiert, keine Gast-/Produkt-/Deadlineaenderung.
  Betroffener Wiederholungslauf mit ansonsten gleichem Kommando und neuem
  `--log build/codex-agent/r328-css-layout/guest-wheel-fixed.log`:
  PASS58.136s.800/480px echte Fensterbreiten (Szenen782/462px), zwei Spalten
  zu einer Spalte,162x44px Linkbutton, Nav-Rechtskante, beide Kartenfarben.
  Rad+192/zurueck ohne Reparse; nativer Invalid-Opcode-Workerstatus134,
  Hang nach bestehender5s-Grenze mit Status143 gereapt, alte Seitenpixel
  exakt erhalten, frisches Laden erfolgreich. Alle7 HTMLWORK-Generationen
  exakt gereapt, kein Kernel-/Desktopausfall, anschliessend Ring3-shell help.
  Unveraenderte PPM-Scanouts und SHA256-Liste `.scanout.json` erhalten.
- `python scripts/run_qemu_browser_external.py --qemu
  'C:/Program Files/qemu/qemu-system-i386.exe' --image build/reist-os.img
  --log build/codex-agent/r328-css-layout/external-guest.log`:
  PASS74.531s, unveraendertes Gate fuer lokale/grosse HTTP-Skripte, Redirect,
  Cache/Reflow ohne Refetch, laufenden CURL-Abbruch und Recovery; echte
  Scanouts und `.http.json` erhalten.
- `.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-browser-input
  -Target qemu -Video vga`: PASS75.281s, `input-guest-command.log`,
  unveraenderte Tastatur-/Edit-/Navigations-/Crash-/Restart-/Konsolenabnahme.
  Vorherige188 kanonische Belege vor dem Lauf nach `previous-input-evidence/`
  kopiert; aktuelle Inputbelege nach `accepted-reference/input/` gesichert.

Alle14 gefrorenen Gate-Gruppen bestanden. Queue nur status/evidence/active_id
fortgeschaltet: R3.28 done, formal naechstes R3.6b active; dessen dokumentierte
VMware-Deferral-/Abnahmebedingungen unveraendert, kein spaeteres Paket in
diesem Lauf umgesetzt. Verbleibende Browsergrenzen sind proportionale Fonts
und die dynamischen DOM-/Event-/fetch-APIs, keine Vollbrowser-Zusage.
Referenzimages/Kernel/neun Programme unter `accepted-reference/` erhalten,
Imagekopien SHA256-identisch zum Artefaktgate. BROWSER2854940 Bytes SHA256
`faaeeadbc63005115ee4c320416c3ee57bca42897df937d23ad91a1a5610085d`,
HTMLWORK890924 Bytes SHA256
`50aaabfe640dde417ca2b4dd9e0f442f05dfe09c6e0ac7a578c8234394a2976a`.
Geschuetzte Kernel-/Benchmarkbytes sind unveraendert; daraus wird kein neuer
VMware-Performance- oder Vollkompatibilitaetsnachweis abgeleitet.

Entwicklungsfehler sind erhalten, nicht als Abnahme umgedeutet: erste
Hostlaeufe fanden den alten Double-Dash-Lexer, undefinierte Bloom-Bitshifts
(O0 Selektoren), geliehenen Bloomfilter bei Quotenabbruch, anonyme Flex-
Textmetriken, sequenzielles statt simultanes Grid-Minimum-Freeze sowie leere
Custom-Property-Werte. Exakte In-Scope-Korrekturen und negative Regressionen;
alle `host-layout-*.log` und `geometry-/values-diagnostic*` bleiben erhalten.
Testfehler (fehlendes Host-strstr, falsch erwartete weisse Schattenecke,
zunaechst falsch platzierter Reap-Probelog) wurden gezielt berichtigt.
Der erste Runtime-Source-Lauf erfasste zudem unveraenderte GCC-
misleading-indentation-Diagnosen in Altcode. Wie die angenommene Referenz
verwenden die finalen Runtime-/GUI-/Benchmark-Hostgates den vorhandenen
Zig/Clang-Fallback (gcc/clang-Verzeichnisse nur aus dem Gate-PATH entfernt),
ohne Warnungen/Assertions abzuschalten oder browser_forms.c zu veraendern.
Nur betroffene Gates nach Korrektur wiederholt; keine Evidenz ueberschrieben.

## R3.27 abgenommen: externe klassische JavaScript-Dateien

Nutzerauftrag zur Fortsetzung nach `361beac4`. Sauberer Worktree vor Beginn.
Vertragscommit `c8ff5e54`: ausschliesslich den geschuetzten Kernelguard vor
der Umsetzung explizit auf abgenommene R3.27a-Hashes umgestellt;
geschuetzte Programme bleiben3eab01ab-bytegleich.
Quoten/Gates unveraendert, insbesondere5s Fetch und >256KiB HTTP-Skript.
Der zugeordnete Stash `cb130c7b1b777e2f0e2c2afd7b42348b4c2713e2` ist
verlustfrei wiederhergestellt und nicht geloescht. Alle alten Belege bleiben.
In diesem Lauf ausschliesslich R3.27, keine R3.28-Implementierung.
Alle15 gefrorenen Gate-Gruppen bestanden, Queue R3.27 done/R3.28 active.
Direkte Diff-/ABI-/Cleanup-/Scope-/Queue-Pruefung; keine Kernel-, SDK-,
Engine- oder CURL-Aenderung. Keine Agenten, sichtbaren VMs/Dialoge oder Push.

Referenzgates VMware19s/QEMU67s PASS, nacheinander gebaut. Beidseitige
Artefaktguard `r327-external-js/artifacts-network-baseline.json` PASS0.971s:
beide echten Kernel und erhaltenen R3.27a-Referenzen stimmen ueberein,
alle7 geschuetzten Programme bytegleich; BROWSER/HTMLWORK und lokale
Script-Fixtures in beiden Images exakt zum gemeinsamen Build.
Externer Browsergast `guest-network-baseline.log` PASS78.169s samt
`.http.json/.pcap` und echten Scanoutbildern.325231-Byte-HTTP-Quelle nach
Redirect erfolgreich, lokale/externe Skripte im selben Realm parsergeordnet;
fuenf Ausfuehrungen, beim Reflow weiterhin fuenf ohne neue HTTP-Anfrage.
Reload zehn, echter laufender CURL mit PID27/Generation10 beendet und
Status143 gereapt, alte Seite erhalten; frische Recovery siebzehn.
Alle14 CURL-Generationen exakt gereapt, dreizehn Status0/eine Status143.
404/HTML-MIME uebersprungen, keine Fehlerkoerper-/inert-Script-Ausfuehrung,
gruene Ergebnispixel, Schliessen und Shell bestaetigt. Kein groesseres Timeout.

Die21 Browser-/Parser-/Test-/Fixturedateien sind nach Normalisierung exakt
zum bereits hostgeprueften Stash identisch; nur drei Image-/Buildlisten mit
dem abgenommenen NETTEST zusammengefuehrt und der Gast-Artefaktkernelguard
wie vorab eingefroren angepasst. Die60 verschiedenen Hostmethoden bleiben
deshalb mit den unten kartierten erfolgreichen Nachweisen gueltig; keine
wiederholte Vollkompilierung unveraenderter Hostfaelle.
`inline-guest.log`/`inline-guest-gate.log` PASS91.224s: DOM, Attribute,
quelltextgenauer Reflow, frische Navigation, echter JS-Hang und Pagefault,
Fencing/Reap/alte Seite/Recovery, Titel-/Attributpixel und Browserneustart.
`browser-input-gate.log` PASS80.604s: echte Tastatureingabe/Bearbeitung,
Navigation, Browserabsturz, Neustart und funktionierende Konsole.
Endgueltige `.img/.vmdk/.vmx`, Kernel und9 Programme unter
`r327-external-js/accepted-reference/`, Inputserial/-bild als
`browser-input.log/.ppm`. Beide vorherigen Netzwerk- und R3.26-Referenzen
sowie alle Fehlversuchslogs und der Browser-Stash bleiben erhalten.
Die Gate-Identitaeten bleiben in der Queue unveraendert; die Wiederholungen
des Artefakt-/Extern-Gates tragen `-network-baseline`, letzteres zusaetzlich
mit begrenztem96-Byte-PCAP-Praefix. Keine Timeout-/Fixturelockerung.

Abgenommene BROWSER.PRG:
`528e407075bb32dab2e8c4d9f5d792b29c3f172cdfea03a16587d62e21571fa1`,
HTMLWORK.PRG:
`ed527d1cae294c5f708f9b0676cc64d07e2e6c5689ad09c4601a87d255ccfe07`.
Kernel unveraendert361beac4, geschuetzte Programme3eab01ab.
HTTP(S)-Lader verwendet unveraendertes CURL/TLS; der neue echte Netzgast
beweist HTTP, kein neuer oeffentlicher HTTPS-/VMware-Laufzeitnachweis.
Modul-/async-/defer-Skripte, Events/Timer, dynamische DOM-Lebensdauer und
fetch bleiben ausdruecklich offen. Naechstes Paket R3.28: statisches CSS-Layout;
intracom.at wird durch externe Scriptquellen allein noch nicht voll kompatibel.

## R3.27a abgenommen: Netzwerk-Fortschritt und RTL-RX-Ringkorrektur

Nutzerfreigabe vom8.September: alle erforderlichen Schritte einschliesslich
des gemeldeten Netzwerk-Prerequisites. Der nicht abgenommene R3.27-Code liegt
wiederherstellbar in Stash `cb130c7b1b777e2f0e2c2afd7b42348b4c2713e2`;
25 explizite Quell-/Test-/Fixturepfade, keine fremden Aenderungen entfernt.
Alle bisherigen Images/Logs bleiben erhalten. Ausschliesslich R3.27a wurde
in diesem Lauf umgesetzt. Alle gefrorenen Gates bestanden; Queue wechselt
R3.27a auf done und R3.27 auf active. R3.28 bleibt queued. Vertrag:
NETWORK_RECEIVE_PROGRESS_CONTRACT.md. Kein Agent, Push oder sichtbares VM-Fenster.

Abschliessende Belege unter `build/codex-agent/r327a-network/`:

- 43 verschiedene Hostmethoden kumulativ PASS: echtes TCP/Parser/Cadence O0/O2,
  RTL-Drain O0/O2 mit aktivierten Assertions, Packaging/negative Evidenztests,
  Legacy-TCP, Frame-Handoff, Netzwerk-/Service-Domain und Benchmark-Guard.
  Bestehende erfolgreiche Methoden nicht unnoetig wiederholt; gezielte
  Reparaturen und ihre fehlgeschlagenen Vorlaeufe sind unten dokumentiert.
- Referenzgates VMware89s (`vmware-rtl-wrap.log`), QEMU76s
  (`qemu-rtl-wrap.log`) PASS; deren vollstaendige Logs im uebergeordneten
  Logordner mit Zeitstempeln100044 bzw.100214. Beidseitiger Imageguard
  `artifacts-rtl-wrap.json` PASS1.285s: alle9 geschuetzten Programme bytegleich
  3eab01ab; neue NETTEST/REIST-Payloads in beiden Images identisch zum Build.
- Netzgate `guest-rtl-wrap.log` PASS130.111s, E1000/RTL jeweils eigene
  `.log/.json/.pcap`: E1000 1MiB946ms/Slow1331ms/Timeout255ms/RST184ms/
  Fresh994ms; RTL8139 781/1062/251/152/856ms. Jedes Datenbyte geprueft,
  alle vier Socket-Slots wiederverwendbar, echtes blockiertes Kind mit
  generationgeprueftem Kill/Reap143, unveraendertes CURL schreibt1MiB,
  NETTEST verifiziert die Datei. Peersequenz jeweils swtrcsG, keine Fehler;
  Fenster32768 bis32 bzw.256Bytes, je ein Peer-RST. Keine Peer-Drosselung.
- `recovery.log`/`recovery-gate.log` PASS: Queue-Druck, Bindungswiderruf,
  Netzwerk-Service-Crash und erneute Frame-Uebergabe in geforderter Reihenfolge.
- `slack.log`/`slack-gate.log` PASS22.863s:814/869 benachbarte1ms-Ticks,
  jeweils >=400, echte Userexception/Reap und funktionierende Shell.
- `browser-input-gate.log` PASS82.476s: Tastatureingabe, Bearbeitung,
  Navigation, echter Browserabsturz, Neustart und Konsole. Serial-/Bildbeleg
  `browser-input.log/.ppm`; vorherige Desktopbelege in `pre-input-evidence/`.

Die Queue-Evidence nennt unveraendert die eingefrorenen Gate-Identitaeten;
gezielte Wiederholungen schreiben die obigen neuen Suffixe, damit fruehere
`guest.log`/`artifacts.json`-Vorstaende nicht ueberschrieben werden.
Direkte Diff-/Scope-/ABI-/Cleanup-Pruefung, keine fremden Aenderungen.
Gepruefte Images/Kernel/Programme in `accepted-reference/` gesichert;
`interim-reference/` und der Browser-Stash bleiben unveraendert erhalten.
Kein VMware-Runtimenachweis fuer das10x-Benchmarkergebnis; geschuetzte
Programme sind bytegleich, der echte Scheduler-Gasttest besteht.

Naechster aktiver Schritt ist R3.27: vor Wiederaufnahme den Kernelguard
explizit auf die unten dokumentierten abgenommenen R3.27a-Hashes umstellen,
sonst alle bisherigen Browser-Gates/Quoten unveraendert lassen. Stash erst
dann paketweise wiederherstellen. Externe Scripts und statisches CSS-Layout
sind mit diesem Netzwerkabschluss ausdruecklich noch nicht abgenommen.

### Erhaltener Verlauf und negative Vorlaeufe

**Zwischenstand vor erfolgreicher Laufzeitabnahme:**
Vertragscommit `54bc08ee` friert die Nutzerfreigabe vor dem Treibereingriff
ein. Der echte Hosttest reproduziert den Defekt bei Offset65520/Laenge14,
Byte12 (`rtl-wrap-regression.log`,6.897s). Jetzt werden nur umgebrochene
Payloads vor CAPR-Freigabe in einen statischen1518-Byte-Puffer kopiert.
Normalpfad ohne Zusatzkopie,64er-Batch, FCS, IRQ/DMA-Rechte unveraendert.
O0/O2-Grenz-/Negativ-/Pendingtest besteht in1.273s; O2-Assertions explizit
aktiv (erster O2-Build hatte NDEBUG und scheiterte geschlossen an Werror).
43 verschiedene Hostmethoden bestehen kumulativ. Referenzbuilds VMware89s,
QEMU76s pass; beide Image-Payloadguards1.285s pass. Neuer QEMU-Kernel:
`b8add76174cb003e06383079285af61c4b707e892cef9b65f1c5aaf13332b49d`,
VMware: `2f561825a91362f357f019d1e1a770e53b9fabfb8e3306ed923d8a679393b810`.
Keine Gate-/Durchsatzlockerung. Noch kein Implementierungscommit oder
Paketabschluss. Der folgende fehlgeschlagene Zwischenstand bleibt erhalten.
E1000 besteht die vollstaendige neue Abnahme (`guest-output-dir-e1000.json`,
62.489s):1MiB964ms, langsamer Leser1226ms, Timeout250ms, RST194ms,
generationgepruefter Parent-Kill/Reap143, alle vier Slots frei und frischer
1MiB-Transfer986ms. Unveraendertes CURL laedt1MiB ueber HTTP in eine Datei,
NETTEST prueft jedes Byte. Tatsaechliches Fenster32768 bis32Bytes,1 Peer-RST,
11003 beobachtete TCP-Ausgaben. Das ist ein QEMU-E1000-Nachweis, kein
VMware-Runtimenachweis oder Vollbrowserabschluss.

Zweites Pflichtprofil RTL8139 scheitert in19.479s:118541Bodybytes bis zum
unveraenderten5s-Abbruch; Gesamtgate81.980s FAIL. Peer liefert1MiB ohne Fehler.
PCAP: nach0.2s ACK64801, dann Luecke bis1.5s; weitere Retransmissionsluecke
1.7--4.5s; zuletztACK118542 inklusive SYN. Keine ueberlappenden, noch teilweise
neuen TCP-Segmente im Mitschnitt; normales Zurueckweisen von TCP-Fragmenten
nicht als Loesung umgehen. RTL-RX-Ring ist bereits64KiB, also kein8KiB-Limit.

Read-only Quellbefund ausserhalb allowed_files: `drivers/net/rtl8139.c`
konfiguriert64KiB mitRCR_WRAP. `rtl8139_drain_rx` liest `entry + 4` linear,
auch wenn `offset + 4 + frame_length > 65536`. Im
[QEMU-RTL8139-Modell](https://github.com/qemu/qemu/blob/master/hw/net/rtl8139.c)
teilt `rtl8139_write_buffer` einen DMA-Write am Ringende: lineares Ueberlaufen
bei gesetztemWRAP gilt ausdruecklich nur fuer Ringe kleiner65536. REISTs
Slackbereich ist bei64KiB daher nicht der Paketrest. Erforderlich ist eine
begrenzte, validierte Ring-zu-Frame-Kopie im vorhandenen RX-Bottom-Half mit
echtem Hosttest fuer normalen/umgebrochenen/ungueltigen Descriptor und dem
unveraenderten1MiB-RTL-Gast. Keine neue DMA-Autoritaet, keine komplexere
Ring0-Protokollverarbeitung, kein geringeres TCP-Fenster als Workaround.
`rtl8139.c` war beim Fehlbefund noch nicht freigegeben und nicht geaendert;
die gezielte Reparatur ist jetzt autorisiert. Weitere Ursache nicht
ausgeschlossen; erster konkreter Treiberdefekt ist eindeutig.

Damals noch offen: Wiederholung des betroffenen Netzgates und Netzwerk-Service-
Recovery, Scheduler-Slack, Browser-Input/Fault/Restart. Diese Restgates waren
noch nicht ausgefuehrt; Queue blieb R3.27a active. R3.27 blieb im genannten Stash.
Zwischenimages/Programme werden unter `r327a-network/interim-reference/`
gesichert, ausdruecklich nicht als abgenommen bezeichnet.

Vertragscommit `2e686e71`. Implementiert: feste32KiB-Ringe jeTCP-Slot,
unveraenderte2048/512-Byte-ABI-/Kopiergrenzen, traffic-sensitive1/40ms-
Steuerungswartezeit mit100ms Aktivitaetsfenster im Ring3-Dienst. Alte
Idle-Operation bleibt explizit; keine Scheduler-/Supervisorquelle geaendert;
NIC-Ausnahme ausschliesslich die jetzt freigegebene RTL-RX-Kopie.
NETTEST prueft die echte Socket-ABI und CURL-Datei bytegenau.

Zwischenstand Abnahme unter `build/codex-agent/r327a-network/`:
Host-TCP/Parser O0/O2 pass4.563s; Erweiterungs-/Evidenztests pass.
Legacy-TCP1.094s, Frame-Handoff4, Netdomain4, Service19 (zunaechst alte
40ms-Quellassertion; expliziter Idle-Zweig erhalten, betroffene Methode
pass0.013s), Benchmark9 pass. Erste Regression scheitert wie erwartet am
2048-Byte-Fenster; zuvor behobener lokaler Zig-Cachezugriff dokumentiert.
VMware98s/24s, QEMU71s, tatsaechliche beidseitige Imageguard1.167s pass:
alle9 geschuetzten Programme bytegleich3eab01ab. Neu:
QEMU-Kernel fd72353fd1a205b76f3cae36b9561f8b111d10516269e00fe23a34a54ee610cc,
VMware-Kernel0254d70d3abe5d2967f7344f44bfc3b588eef07b298595f716abed77f3c4faa5.

Erste echte E1000-Gaeste:1MiB bytegenau830/796ms; langsamer Leser919/1082ms,
Timeout312/252ms. Noch keine Gesamtabnahme. Fehler des neuen Testablaufs:
sofortiger Peer-RST konnte bereits den Trigger-Send treffen (28.567s Lauf);
Reset nun gezielt in Receive-Phase. Zweiter Lauf110.521s erreichte den
Abbruchtest: die Shell bietet keine automatische Ctrl-C-Kindbeendigung.
Ersetzt durch expliziten generationgeprueften Parent-Kill, begrenzte
Zombie-Beobachtung/Reap143 und frischen Vier-Slot-/Transfernachweis.
Die vorhandene verifizierte Windows-QEMU-Timerpolicy wird wiederverwendet;
kein Gastclock-/Timeoutwechsel. Der waehrend des Laufs noch24Byte grosse
PCAP war gepuffert, kein Beleg eines Bootstillstands.

Der neue PCAP-Validator verlangte zuerst unnoetig ein exakt auf0 gefuelltes
Fenster. Der echte Peer liess6848Byte frei; ein Sender muss den letzten
Teil nicht fuellen. Korrektur innerhalb unveraenderter eingefrorener DoD:
reale Fensterbelegung unterhalb der Haelfte, echte RSTs und bytegenaue
Recovery; Zero/Overflow/Reopen bleiben verpflichtend im echten TCB-Hosttest.
Negative Validatorfaelle erweitert, keine Quelldateigrenze oder Laufzeitgate
entfernt. NETTEST-Nachbuild VMware74s/QEMU71s, aktualisierte beidseitige
Artefaktguard1.090s pass (`artifacts-cancel-owner.json`), identische Kernel.
Neuer Diagnosecode NETTEST aeb97e9e9223c9dac5f18d677cf42ed8623c96f0e78487f7333f39f3b816e2c0,
REIST7d33f0eacdacb30e34769ff12e4933dd3780f7ca82d36625f33a0fca81055735.
42 verschiedene Hostmethoden bestehen kumulativ. Zusaetzlicher Testfixture-
Fehler in `guest-cancel-owner`31.517s: /tmp fehlte im Referenzimage; der Runner
legt es jetzt per normalem mkdir nur im Snapshot an. Keine CURL-Aenderung.
Alle Fehlerlogs bleiben erhalten, Restgates oben offen.

## Erhaltener R3.27-Verlauf vor dem Netzwerk-Prerequisite

**Noch nicht abgenommen, kein Implementierungscommit.** Gesicherte R3.27-
Aenderungen gehoeren dieser interaktiven Umsetzung auf040ed90f/3eab01ab.
Die gefrorenen Gates/Allowed-Files wurden nicht gelockert. R3.28 ist nur
vorbereitet und bleibt bis zum erfolgreichen R3.27-Abschluss zurueckgestellt.

Implementiert: privates Script-Profil3, echte parsergeordnete src-Auftraege,
lokaler/HTTP-Lader mit direkt besessenem CURL, MIME/UTF8-/URL-/Redirect-
Admission, Dokumentcache, gleiche JS-Umgebung und exakte Journalwiedergabe.
Der alte Kernel, SDK, CURL und JSWORK wurden nicht geaendert.

Hoststand:60 verschiedene Testmethoden bestehen, einschliesslich echter
QuickJS-O0/O2-, Parser-, Owner-/Transport- und Cache-Fehlerfaelle. Die
fehlgeschlagenen Entwicklungslaeufe bleiben unter
`build/codex-agent/r327-external-js/` erhalten; nur betroffene Methoden wurden
nach konkreten Korrekturen wiederholt:

- external-gate6.457s; quote/cache regression1.724s -> external-fixed4.469s;
  redirect alias regression1.258s -> redirect-cache-fixed3.315s;
  neuer Gast-Evidenzvalidator besteht im Cache-Regressionslauf.
- scripting-gate40.562s: QuickJS/Validator bestanden, Host-Narrowing korrigiert
  -> owner-fixed6.569s; verlorene Realm regression1.957s -> realm-loss-fixed4.182s.
- html-gate5.151s, css-gate56.292s, js-service6.172s, navigation2.932s,
  GUI59.522s, benchmark0.004s. runtime-source12.508s:28 Methoden bestanden,
  fehlende OS-Dateimocks im Host ergaenzt -> transport-fixed2.745s besteht.
- VMware20s/QEMU72s, nach Cache/Realm-Fix VMware77s/QEMU70s.
  Die beiden letzten Nachbuilds ueberlappten kurz; `artifacts.json`1.056s
  prueft anschliessend erfolgreich **beide tatsaechlichen Image-Inhalte**,
  nicht nur gemeinsame Builddateien. Alle geschuetzten Kernel-/Programm-
  Hashes stimmen mit3eab01ab ueberein; CURL wurde auch aus dem erhaltenen
  R3.26-QEMU-Image gegengeprueft.

Historisches offenes Pflichtgate vor R3.27a: `run_qemu_browser_external.py` scheiterte dreimal beim
325231-Byte-HTTP-Skript nach erfolgreichem Redirect, nicht beim lokalen Script.
`guest.log`47.108s, `guest-diagnostic.log`48.538s und
`guest-network.log`48.539s samt .http.json/.pcap sind erhalten.
Die Diagnoseversion (nur zusaetzliche Lader-Fehlerzaehler) wurde fuer QEMU
in15s gebaut (`qemu-diagnostic.log`); diese neue BROWSER-Datei hat noch keine
beidseitige Artefaktabnahme. `first-guest-image/reist-os.img` sichert das
vorherige getestete Image. R3.26-Referenzen bleiben unberuehrt.

Gesicherte Beobachtung: Der lokale HTTP-Peer liefert das gesamte325231-Byte-
Skript ohne Serverfehler ab. Bis zum begrenzten Abbruch bestaetigt der Gast
nur103873 TCP-Bytes in5.653 Hostsekunden. Das angebotene Empfangsfenster
betraegt durchgehend maximal2048 Bytes; ~1440-Byte-Netzsegmente werden im
mediierten Empfang in512-Byte-Schritten quittiert, neue Daten folgen vielfach
erst nach50--70ms. Der Browser-Lader meldet `received=0 total=0`: CURL
publiziert erst die vollstaendige HTTP-Antwort, die IPC-Uebergabe ist noch
nicht begonnen. Redirect und Worker-Reap funktionieren. Timeout endet
fenced mit Exit143; die bisherige Seite bleibt erhalten.

Read-only Quellbefund ausserhalb des Pakets: `drivers/net/tcp_socket.h`
setzt RECEIVE_CAPACITY=2048/MAX_SEGMENT=512; der TCP-Empfangspuffer und das
beworbene Fenster sind daran gekoppelt. `kernel/init/supervisor.c` ruft
`netdev_poll()` im gemeinsamen Supervisor-Durchlauf auf, mit anschliessendem
10ms-Sleep plus anderer Supervisorarbeit. Der Mitschnitt belegt einen
Engpass im Netzwerkpfad, keine erfolgreiche grosse JS-Uebertragung.

Damals geltender Stopgrund: weitere Korrektur erforderte ein gesondert eingefrorenes
Netzwerk-/TCP-/Bottom-Half-Paket ausserhalb R3.27 und damit eine neue
Abnahme des bisher geschuetzten Kernelbestands. Kein Erhoehen der5s-Grenze,
keine kleinere Gastfixture und kein langsamer gepaceter Testserver als
Gate-Umgehung. Keine Netzwerk-/Kernelquelle geaendert. Inline-/Input-
Gastgates der aktuellen Version wurden deshalb noch nicht ausgefuehrt;
kein Queue-Uebergang zu done, kein Implementierungscommit oder Push.

Fortsetzung auf3eab01ab. Eingefrorener Ressourcen-/Skriptschnitt: echter
Parser-src-Callback, direkt besessener asynchroner CURL-Auftrag, lokale
Dateien, URL-/MIME-Admission, begrenzter Dokumentcache und quelltextgenaues
Replay. Kernel, JSWORK und CURL bleiben unveraendert; keine Agenten.
Scope/Gates in der Queue, Grenzen in BROWSER_SCRIPTING_CONTRACT.md.
R3.6b bleibt zurueckgestellt.

Der Nutzer hat inzwischen alle erforderlichen Umsetzungsschritte fuer die
intracom.at-Sollansicht freigegeben. R3.28 CSS-Werte/Boxlayout ist als naechstes
Paket eingefroren und queued; noch keine R3.28-Quellimplementierung.
Schrift-/Rasterpfad und dynamische DOM-/Event-/fetch-Lebensdauer folgen danach.
Details und Bestandsabgleich in BROWSER_ENGINE_PORT_PLAN.md.

## R3.26 abgenommen: JavaScript-Attribute und CSS-Klassen

Fortsetzung auf2d6aba16 im sichtbaren Hauptworktree. Eingefrorene Erweiterung
des bestehenden Dokument-/Mutationsvertrags: HTML-Attribute, id/className,
live classList, versioniertes Journal und echte CSS-Darstellung. Keine neue
Worker-/Netzautoritaet, keine Kernel-/Engineaenderung und keine Agenten.
Scope/Gates in der Queue, Grenzen in BROWSER_SCRIPTING_CONTRACT.md.
Vertragscommit: `b6bb3d6b`. Alle eingefrorenen Gates bestehen; die Queue
schaltet nur formal auf R3.6b weiter. Dessen offene VMware-Abnahme wird nicht
vorgezogen; der Browser behaelt den dokumentierten Benutzervorrang.

Neu: getAttribute/getAttributeNames/hasAttribute/hasAttributes,
setAttribute/removeAttribute/toggleAttribute, schreibbare id/className und
live classList mit add/remove/toggle/replace/value/item/length/Iteration.
Die private Nachricht bleibt48Bytes, ihr explizites Profil2 transportiert
versionierte Attributjournale; Profil1 behaelt Text/Titel-Grammatik und
read-only id. Parser prueft das gesamte Journal und die kumulierten Reserven
vor der ersten Aenderung. Das Replay bindet Profil, Quelltext und Reihenfolge.
Entfernte Knoten und Klassenwrapper bleiben identitaetsstabil; keine neue
Skript-, Netz-, Kernel- oder Geraeteautoritaet.

Der echte Gast zeigt auf `/htdocs/javascript.htm` jetzt gruenen Text durch
eine per JavaScript gesetzte CSS-Klasse. ID und data-Attribut wechseln zwischen
zwei Parser-Callbacks; eine gesetzte style-Eigenschaft wird wieder entfernt.
Fuenf verschiedene JSWORK-Identitaeten,2/2/3/5 Auswertungen bei Erstaufruf,
Reflow, Navigation und Recovery; echte Fault-Adresse4 und nichtkooperativer
Hang, alte Seite/Titel bei Fehlern und Browser-/Desktop-Neustart bestehen.
Der Screenshot `guest-dom.png` wurde auch visuell geprueft. Kein neuer
Durchsatz-/WCET-Nachweis; geschuetzte Binaerdateien bleiben bytegleich.

### Abnahmekarte R3.26

Logs unter `build/codex-agent/r326-dom-attributes/`; Kommandos exakt wie in
der Queue, Hosttests jeweils `python test/<Name>.py -v`:

| Gate | Ergebnis / Zeit | Beleg |
| --- | --- | --- |
| test_browser_scripting |3 PASS /52.722s; betroffener finaler Bindingfall O0/O2 PASS /40.329s | scripting-gate-final.log, binding-webidl-final.log |
| test_html_engine |1 PASS /5.305s | html-gate-fixed.log |
| test_css_engine |1 PASS /58.612s | css-gate.log |
| test_js_service |3 PASS /3.802s | test_js_service.log |
| test_browser_navigation_source |4 PASS /1.497s | test_browser_navigation_source.log |
| test_browser_runtime_source |29 PASS /9.192s | test_browser_runtime_source.log |
| test_gui_browser_source |8 PASS /37.732s | test_gui_browser_source.log |
| test_benchmark_source |9 PASS /0.011s | test_benchmark_source.log |
| test-reist-package vmware/vga |PASS /18s | vmware-gate.log, ../20260908-074854-package-vmware-vga.log |
| test-reist-package qemu/vga |PASS /71s | qemu-gate.log, ../20260908-075003-package-qemu-vga.log |
| run_qemu_browser_scripting --verify-artifacts |PASS /0.998s | artifacts.json |
| run_qemu_browser_scripting Gast |PASS /92.664s | guest-gate.log, guest.log, guest-*.ppm |
| test-reist-runtime runtime-desktop-browser-input |PASS /76.276s (Wrapper) | input-gate.log, input.browser.log |

58 verschiedene Hostfaelle, keine Mehrfachzaehlung korrigierter Wiederholungen.
Erstfehler bleiben erhalten: `html-gate.log` verwendete im neuen Test ein
nicht deklariertes strstr; auf vorhandenes find umgestellt. `scripting-gate.log`
und `binding-flat-*.log` zeigen zu tiefe Helfer-/native Iterator-Aufrufketten.
Die Adapter wurden abgeflacht;16KiB Engine-Stack und alle bestehenden
Assertions bleiben erhalten. `binding-diagnostic.log` ist ein gescheiterter
beschleunigter Diagnoselink mit doppelt ausgewaehlten Cache-Objekten, korrigiert
durch Auswahl nur der fertigen Objektdateien. Kein Hostdialog/Agent/Push und
keine Lockerung eines Gates. Web-IDL-Regressionen fuer optionales undefined,
fehlende Argumente und BigInt-Index sind zusaetzlich abgedeckt.

Akzeptierte Images samt BROWSER/HTMLWORK-Kopien liegen in `reference-qemu/`
und `reference-vmware/`. SHA256 wurde nach dem Kopieren verglichen:

- QEMU: `e88224a7b6ca9cac30e3506cd9090abf76c7efc78f10941299e3e5a6e37b2878`.
- VMware flat: `d0eecc31314ab717362dc01c05c8197df29f7a903f8322963989f7577f255f08`.
- BROWSER2838556Bytes: `bcf3c72462b9965605fe252a5f0c78eac42c806f310d71a554b07489d999b3db`.
- HTMLWORK854060Bytes: `ceefae4746a906e48aae4c3ababd7d39f47a22525b385f6014a87ea851956658`.

Der Imageguard verwendet die weiter gueltigen geschuetzten SHA256-Werte aus
4b2b3302, die auch im unmittelbaren Elternstand2d6aba16 identisch sind:
beide Kernel, GTEST/BENCHMARK/MATHTEST/TEXTTEST/JSTEST/JSWORK unveraendert.
Die vorherigen R3.25-Images und Fehlerbelege bleiben erhalten.
Grenzen: HTML-only Attributadapter, ASCII-XML-Namen bis255Bytes, benannte
Error-Instanzen statt voller DOMException, item()/Iteration statt numerischer
classList-Exoten. Externe/module Skripte, Events/Timer, Selektoren und CSSOM
bleiben offen; keine Zusage allgemeiner moderner Website-Kompatibilitaet.

## R3.25 abgenommen: JavaScript im tatsaechlichen Browser

Auf4b2b3302, Vertragscommit f44400b9: klassische Inline-Skripte laufen jetzt
an Hubbubs echter Parsergrenze im direkt vom Browser besessenen JSWORK.
document.title/getElementById/body/textContent und persistente Globals zwischen
Skripten sind aktiv. Die beiden Worker bleiben getrennte Ring3-Prozesse;
der neue C++-Owner verarbeitet ausschliesslich begrenzte Nachrichten.
Navigation verwirft alte Skriptarbeit, Reflow und CSS-Ressourcenpaesse spielen
das quelltextgenaue Journal ab, ohne Skripte erneut auszufuehren. Fehlgeschlagene
Kandidaten lassen die bisherige Seite samt Journal erhalten.

Testseite: `/htdocs/javascript.htm`, Folgeseite `/htdocs/jsnext.htm`, in beiden
Images enthalten. Der echte Gast zeigt geaenderten Text und Seitentitel in
der Browserleiste; native Fensterdekorationsaktualisierung bleibt getrennt.
Reihenfolge, fehlende zukuenftige Elemente, Objektidentitaet, Fortsetzung nach
Autorenexception, echtes Resize ohne erneute Ausfuehrung, frisches Dokument,
nichtkooperativer Hang, Page Fault, Fencing/Reap, Recovery und Neustart bestehen.
Fuenf unterschiedliche JS-Workeridentitaeten; 2/2/3/5 Autoren-Ausfuehrungen
nach Erstladung/Reflow/Folgedokument/Recovery. Der vorhandene Browsergast
besteht unveraendert mit Tastatureditierung, Navigation, Crash und Neustart.

58 verschiedene Hostfaelle bestanden. Finale Belege relativ zu
`build/codex-agent/r325-browser-js/`; exakte Befehle im eingefrorenen Queueeintrag:

| Gate | Ergebnis / Dauer | Beleg |
|---|---|---|
| QuickJS-Bindung und echter C++-Owner O0/O2 | 2 PASS / 43.597s | `scripting-gate-final.log` |
| Fehlende/stale Gastbelege werden abgelehnt | 1 PASS / 0.054s | `title-guest-validator-host.log` |
| HTML-Parser / CSS | 1/1 PASS / 4.554/41.394s | `html-gate-fixed.log`, `css-gate.log` |
| Unveraenderte JS-Service-Regressionen | 3 PASS / 3.210s | `service-gate.log` |
| Navigation / GUI / Benchmark | 4/8/9 PASS / 1.781/39.372/0.022s | `navigation-gate.log`, `gui-gate.log`, `benchmark-gate.log` |
| Browser-Lifecycle/Rendering | 29 PASS / 7.982s, ergaenzter UTF-8-Titeltest 1.508s | `runtime-host-title.log`, `title-render-host.log` |
| VMware / QEMU Referenz | PASS / 70/68s | `vmware-gate-title.log`, `qemu-gate-title.log` |
| Tatsachliche Imagebytes | PASS / 1.032s | `artifacts-final.json` |
| JavaScript im echten Browser, inklusive Titelpixel | PASS / 91.188s | `guest-final.log`, `guest-final-*.ppm`, `guest-final-dom.png` |
| Bestehender Browser-Tastatur-/Crash-/Neustartgast | PASS; Wrapper ohne eigenen Dauerwert | `browser-input-gate.log`, `browser-input.browser.log`, `browser-input.ppm` |

Beide Kernel sowie GTEST/BENCHMARK/MATHTEST/TEXTTEST/JSTEST/JSWORK sind in
den tatsaechlichen Images bytegleich zu4b2b3302. BROWSER2826268Bytes,
SHA25615ae9613ce1c16fc9b982457a38cf49a606ec7c84759bca6b1c890b0c5328458;
HTMLWORK854060Bytes, SHA2568a8246200f08a77175bc038b5e5841f18f8eab926d933efa264eed2e44ff842f.
Gepruefte Kopien unter `accepted-qemu/` und `accepted-vmware/`:
QEMU9c3415c70143fc5e656f687389cd74e73d172d4ac48eee4a940f944efcb2348c,
VMware65ec6836af6a3da2537525eee696196e497be574a513f9752ffa8d4ec8826097.

Alle Fehlversuchslogs bleiben erhalten: tiefe DOM-Hilfsaufrufe wurden iterativ,
C/C++-Linkage korrigiert, Hostfixtures um echte Links/OS-Mocks und den neuen
Turnbudget-Reset ergaenzt (alte Assertions erhalten), fehlendes Host-strstr
im Zielprobe durch begrenzten Vergleich ersetzt. Der Artefaktleser brauchte
den bestehenden FAT-Kurznamen javasc~1.htm. Der erste Gast bestand die
JS-Funktionen, scheiterte aber nach Resize an der falschen Ausgangskoordinate
des Desktop-Exit-Helfers; feste Frist180s blieb erhalten, Pointer-Voraussetzung
jetzt per Scanout nachgewiesen. Nur betroffene Gates wiederholt; finale Images
kopiert, keine Agenten, sichtbaren VMs oder Pushes.

Grenzen: [BROWSER_SCRIPTING_CONTRACT.md](../architecture/BROWSER_SCRIPTING_CONTRACT.md).
Keine externen/module Skripte, Events/Timer, fetch/XHR, Cookies/Storage oder Date;
kein Anspruch auf breite moderne Website-Kompatibilitaet. Enforcing CSP sperrt
Inline-Skripte konservativ. Keine Kernel-/SDK-/Engine-/Quota-Aenderung und kein
neuer Durchsatz-/WCET-Claim. Nur formaler Queuewechsel zum weiterhin fachlich
zurueckgestellten R3.6b; dieses Paket wurde nicht angefasst.

## R3.24 abgeschlossen: persistenter JavaScript-Dienst

Auf sauberem a09d8841, Paketvertrag90a51376: separate JSWORK-Prozessgrenze
mit persistenter Engine, geprueftem Bulk-IPC, HELLO/Selbsttest, Eval, Health/GC,
Shutdown und nicht kopierbarem C++-Owner. 25 Hostfaelle, beide Referenzbauten,
Imageguard, echter Dienstgast und unveraenderter Browsergast bestehen.
[Dienstvertrag](../architecture/JAVASCRIPT_SERVICE_CONTRACT.md).

Zwei richtungsgebundene Endpoints, maximal ein Auftrag, hoechstens acht
Timeout-null-IPC-Operationen pro Poll. Quelltext bis1MiB, Ergebnis bis65535
Bytes plus NUL, absolute5000ms fuer Transfer/Ausfuehrung/Antwort. Keine
Teilantwortpublikation; bereits publizierte Callerbuffer werden beim
spaeteren Fencing nicht mehr beschrieben. Enginebudget32MiB im expliziten
64MiB-Prozessheap; Core und16KiB-Stackprofil bleiben unveraendert.
Endpunkte vor Kill schliessen, exakte Kindidentitaet, beobachteter Zombie
vor Wait, Reapfrist1000ms und hoechstens ein Kill; maximal zwei explizite
frische Recoveries pro Dokument. Keine stille Skriptwiederholung oder
blockierende Aufraeumarbeit im Destruktor.

JSIPCTST laeuft aus beiden normalen Ring3-Shell-Layouts. Zweimal persistente
Globals/Closures/Promisejobs, Syntaxfehler mit erhaltenem Realm,1MiB-Quelle,
60000-Byte-Antwort, Health/GC und normal0/Fault142/Hang143/Stale143/Cancel143/
frisch0: zwoelf unterschiedliche Workeridentitaeten. Die Fehlerproben
halten echte Engine und8MiB-Buffer live bis zur Prozessfreigabe. Zweimal
abrupter Besitzertod mit Worker-Ende74, fehlender Liveidentitaet, normaler
Slot-Wiederverwendung, verschwundenem alten PID und aufgeraeumten eigenen
Zeugenprozessen. Die Ring3-Shell beantwortet danach jeweils HELP.

Finale Belege relativ zu `build/codex-agent/r324-js-service/`, exakte Befehle
im eingefrorenen R3.24-Queueeintrag:

| Gate | Ergebnis / Dauer | Beleg |
|---|---|---|
| Service i386 O0/O2, Ziel-Link/Layout und Gastvalidator | 3 PASS / 10.535s | `host-orphan-accepted.log` |
| Unveraenderter JavaScript-Kern | 6 PASS / 67.323s | `host-js-final.log` |
| Buildabhaengigkeiten / Benchmark | 7/9 PASS / 0.020/0.002s | `host-{build_dependencies,benchmark_source}-final.log` |
| VMware/vga Referenz | PASS / 74s | `package-vmware-orphan.log`, `../20260907-215218-package-vmware-vga.log` |
| QEMU/vga Referenz | PASS / 65s | `package-qemu-orphan.log`, `../20260907-215358-package-qemu-vga.log` |
| Beide echten Imagekernel und verpackte Programme | PASS / 0.942s | `artifacts.json`, `artifacts-command.log` |
| JS-Dienst inkl. Fault/Hang/Besitzertod/frischem Realm | PASS / 39.786s | `guest.log`, `guest-command.log` |
| Browser-Eingabe/Navigation/Crash/Restart/Konsole | PASS / 72.880s | `browser-input-command.log`, `browser-input.log`, `browser-input.ppm` |

Beide Kernel sowie BROWSER/HTMLWORK/GTEST/BENCHMARK/MATHTEST/TEXTTEST/JSTEST
sind auch in den Image-Dateisystemen bytegleich zu a09d8841. JSWORK1007616
Bytes, SHA256`4bc4bd9d4f2913e00adbaa04e39b61f5f0888f93f0c5272889a8b6064b3516d0`;
JSIPCTST32768 Bytes,
SHA256`1e5f06e3f6e30f4766487570db4cb5425cfbc743c02e61a1a2fd9273cdd38aa2`.
QEMU-Image: `e4f29fd47f1f7d291f1ac78981e4934457ea5606efe6f2a6fb9db8afd07c2dcb`.
VMware-Flatdisk: `4a7657e3bcfb4eeb09368f7c30928987b4d1ced0ff5bb637ee319f2da21e7d40`.
Verifizierte Kopien in `accepted-qemu/` und `accepted-vmware/`.

Erhaltene Vorher-Belege: fehlender C-Linkname fuer den neuen C++-main,
publizierter Ergebnisbuffer beim spaeteren Cancel faelschlich beschrieben
(`published-output-before.log`, danach `host-service-accepted.log`3 PASS),
und falsche Gastannahme zur sofortigen Entfernung verwaister Exitmetadaten
(`guest-before-orphan.log`, FAIL30.605s, Zeile124). Der existierende Kernel
haelt feste verwaiste Exit-Eintraege bis Slot-Wiederverwendung sichtbar;
der korrigierte Gast beweist jetzt diese Wiederverwendung und weiterhin
das Verschwinden des alten PID. Kein Akzeptieren eines verbleibenden
Eintrags als Erfolg, kein fremdes Wait/Kill und keine Fristlockerung.
Vorherige Images in `orphan-before-{qemu,vmware}/`, ihr Guard in
`artifacts-before-orphan.json`; alle Entwicklungs-/Host-/Buildlogs und
vorherige Browserbelege in `before-browser-gate/` bleiben erhalten.
Nur betroffene Service-/Referenz-/Image-/Gastgates nach Reparatur wiederholt;
unveraenderte Core-, Build- und Benchmarktests nicht nochmals ausgefuehrt.

Keine Kernel-, Engine-, ABI-, Quota- oder Browseraktivierungsaenderung und
kein neuer Performance-/WCET-Claim. Website-JavaScript braucht weiterhin
DOM-Objektidentitaet, Mutations-/Eventtransaktionen und Navigationsgrenzen.
Queue geht nur formal auf R3.6b, dessen offene Abnahme hinter Browservorrang
bleibt. Kein spaeteres Paket, keine Agents, sichtbare VM oder Push.

## R3.23 abgeschlossen: isolierter JavaScript-Kern

Auf sauberem 6bc51cbf, eingefrorener Paketvertrag ee754e42: QuickJS2026-06-04
als opt-in Ring3-SDK-Archiv und opake Runtime-/Context-Fassade umgesetzt.
Echte i386-O0/O2-Sprachtests, 40 Hostfaelle, beide Referenzen und alle
Gast-/Artefaktgates bestehen. [Grenzen](../architecture/RING3_JAVASCRIPT_CORE_CONTRACT.md).

Enthalten: Parser/Interpreter, Unicode/RegExp, BigInt, JSON, Proxy,
Collections, TypedArrays, Promises, WeakRef und Zyklus-GC. Bestehender
privater Prozessheap statt statischem Riesenpuffer; Budget vor Allocation,
transaktionales Realloc und vollstaendige Enginefreigabe. Skripte bis 1 MiB,
begrenzte Ergebnisse/Jobs/monotone Fristen. OOM/Kapazitaet/Frist sperren
weitere Arbeit; normale Skriptfehler bleiben im lebenden Kontext auffangbar.
Eigene dtoa/atod/rqsort bleiben upstream, inttypes-Makros verwenden echte
Clang-Targettypen und lrint den unveraenderten i386-musl-fistpl-Pfad.

JSTEST wird in beiden normalen Ring3-Shell-Layouts verpackt. Zweimal echte
Sprach-/GC-Arbeit und normal37/Pagefault142/nichtkooperativer Kill143/frisch37,
acht verschiedene PID-/Generationspaare, Reap und frischer Selbsttest.
Bei Fault/Hang bleiben Engine und 8 MiB ArrayBuffer bis zur OS-Freigabe live;
Parent-FPU/errno und Shell ueberleben. Interrupts sind kein universeller
Native-/GC-Watchdog: der externe Prozesseigentuemer bleibt erforderlich.

Finale Belege relativ zu `build/codex-agent/r323-js/`, exakte Befehle in Queue:

| Gate | Ergebnis / Dauer | Beleg |
|---|---|---|
| JavaScript i386 O0/O2, Allocator, Pins, C/C++, SDK/Link/Dispatch | 6 PASS / 66.516s | `host-js-final.log` |
| Math / Text / libc / Build / Benchmark | 8/6/4/7/9 PASS / 30.702/5.948/1.007/0.022/0.002s | `host-{math,text,libc_source,build_dependencies,benchmark_source}-accepted.log` |
| Gezielte Dispatchpruefung nach Linkregel-Gruppierung | 1 PASS / 0.016s | `js-link-regroup-final.log` |
| VMware/vga Referenz | PASS / 146s | `package-vmware-final.log`, `../20260907-205723-package-vmware-vga.log` |
| QEMU/vga Referenz | PASS / 64s | `package-qemu-final.log`, `../20260907-210018-package-qemu-vga.log` |
| Beide echten Imagekernel und verpackte Programme | PASS / 0.866s | `artifacts.json`, `artifacts-command.log` |
| JSTEST mit echtem Fault/Hang/Neustart | PASS / 33.552s | `guest.log`, `guest-command.log` |
| Unveraenderter MATHTEST im finalen Gast | PASS / 13.078s | `math.log`, `math-command.log` |
| Browser-Eingabe/Navigation/Crash/Restart/Konsole | PASS / 73.255s | `browser-input-command.log`, `browser-input.log`, `browser-input.ppm` |

Beide Kernel und BROWSER/HTMLWORK/GTEST/BENCHMARK/MATHTEST/TEXTTEST sind
bytegleich zu6bc51cbf, auch in den tatsaechlichen Image-Dateisystemen.
JSTEST1015808 Bytes, SHA256
`723765e1e695274750366d36858b1c5812239033c6e9847616fca2357904d440`.
QEMU-Image: `f7f57f531abec20136d0b7b5ad2d691ceb561a13504f69b6150c16b5f05f0cb6`.
VMware-Flatdisk: `5bd8677236e7c3412b20adfab31e981dc03aa87bc0deda72f3032672f3ce47dc`.
Verifizierte Kopien in `accepted-qemu/`, `accepted-vmware/`; vorherige
Browserbelege in `before-browser-gate/`. Alle alten Images/Stashes bleiben.

Erhaltene Entwicklungsfehler: fehlendes echtes alloca-Mapping, unselektierte
stdio-/Date-Diagnoseverweise, implizite Windows-Debug-UBSan-Frames (29824
Bytes Interpreterstack), generischer out-of-range-lrint-C-Cast, fehlendes
errno-Include im neuen Gast und nicht schreibbarer Standard-Compiler-Cache.
Jeweils gezielt repariert, keine Stack-/Heap-/Frist-/Gategrenze gelockert.
Der erste finale Math-Quelltextgate erfasste nach MATHTEST auch benachbarte
TEXTTEST-/JSTEST-Regeln. Gruppierung der unabhaengigen Buildregeln im
freigegebenen Builder korrigiert, MATHTEST-Linkliste unveraendert; originaler
Math-Gate unveraendert wiederholt, JS-Dispatch gezielt nachgeprueft. Belege
in `*-development.log`, `host-math-final.log` und den finalen Logs bleiben
erhalten. Keine Wiederholung unveraenderter JS-Sprachtests nach diesem
reinen Builder-Fix; reale Programme und Imagehashes wurden danach geprueft.

Kein Kernel-/ABI-/Quota-/Benchmarkumbau oder neuer Performance-/WCET-Claim.
Date, Threads, Netz-/Dateiautoritaet und DOM/Event-/Navigation-IPC bleiben
offen; das aktiviert noch kein Website-JavaScript. Queue geht nur formal
auf R3.6b, dessen offene Abnahme bleibt hinter dem Browservorrang; kein
spaeteres Paket in diesem Lauf. Keine Agents, sichtbare VM oder Push.

## R3.22 abgeschlossen: formatierte Ring-3-Zeichenketten

Generatorreparatur umgesetzt: zehn Hostfaelle (acht im Hauptlauf, zwei neue
gezielte Regressionen) sowie echte HTML5-/CSS-Verhaltenstests bestanden.
Alle 2137 Entity-Eintraege liefern bei O0/O2 das Originalergebnis, maximal
sechs laterale Suchschritte pro Zeichen statt der erlaubten sieben.
Zwei finale unabhaengige Cold-Builds bestehen in 106.023s, drei Parserarchive
und HTMLWORK sind jeweils bytegleich. Alle verbleibenden Gates bestehen.
Die unveraenderten 26 Hostfaelle und TEXTTEST-Gast15.106s bleiben erhalten;
der finale Image-Guard bestaetigt dazu die identischen Programmdigests.

Das SDK liefert opt-in snprintf/vsnprintf, einschliesslich long double,
Standard-Laengenmodifikatoren, Trunkierung und Fehlercodes, ohne Heap,
Dateizugriff oder neue Autoritaet. INT_MAX verworfene Paddingzeichen werden
nicht einzeln verarbeitet. TEXTTEST beweist zweimal normal37/echten
Pagefault142/kill143/frisch37, exakte Freigabe und unveraenderten Parent.
MATHTEST bestaetigt weiterhin Numerik, vier Rundungen, echten MF16/status144,
kill143, frische Generation und Shell. Der Browsergast prueft echte Tasten,
URL-Korrektur, Navigation und Konsole nach Prozessfehler und Neustart.

Finale Belege relativ zu `build/codex-agent/r322-text/`; Befehle in der Queue:

| Gate | Ergebnis / Dauer | Beleg |
|---|---|---|
| Text / libc / Build / Benchmark, unveraendert | 6/4/7/9 PASS; 5.432/1.092/0.017/0.002s | `host-text-final.log`, `host-libc_source-final.log`, `host-build_dependencies-final.log`, `host-benchmark_source-final.log` |
| HTML-Build, unveraenderte acht Faelle | 8 PASS / 2.327s | `host-html-build-final.log` |
| Neue Cold-C++- und Imagealias-Regression | je 1 PASS / 0.018s und 0.016s | `cold-cpp-final.log`, `image-alias-final.log` |
| Echte HTML5-/CSS-Verhaltenstests | je 1 PASS / 4.341s und 40.628s | `host-html_engine-final.log`, `host-css_engine-final.log` |
| Zwei unabhaengige Cold-Neubauten | PASS / 106.023s | `html-rebuild.json`, `html-rebuild-command.log` |
| VMware/vga Referenz | PASS / 121s | `package-vmware-generator-final.log`, `../20260907-195316-package-vmware-vga.log` |
| QEMU/vga Referenz | PASS / 67s | `package-qemu-generator-final.log`, `../20260907-195539-package-qemu-vga.log` |
| Beide echten Images / verpackte Programme | PASS / 1.119s | `artifacts-final.json`, `artifacts-final-command.log` |
| TEXTTEST, unveraenderter Test und Payload | PASS / 15.106s | `guest.log`, `guest-command.log` |
| MATHTEST im finalen Image | PASS / 14.153s | `math.log`, `math-command.log` |
| Browser-Eingabe/Navigation/Crash/Restart/Konsole | PASS / 75.489s | `browser-input-command.log`, `browser-input.log`, `browser-input.ppm` |

Zusammen 38 Hostfaelle. Kernel beider Profile, BROWSER/GTEST/BENCHMARK und
TEXTTEST/MATHTEST bleiben bytegleich; alle SHA256 in `artifacts-final.json`.
HTMLWORK845868 Bytes ist neu abgenommen, SHA256
`c40c114e593a1251ce803ac46e0a8639b87ff061dacfe52958a5faa1f3996da8`.
QEMU-Image: `87df18f7b18bee171790044de8a3ada6d029a70184623968a2e8cb38b594bde1`.
VMware-Flatdisk: `22c962ed168a596d43449adfa575e3f69b26dca52768b808dc7df11d0c8f22d7`.
Kopien in `accepted-qemu/` und `accepted-vmware/`, Digests nach Kopieren
bestaetigt. Beide unabhaengigen Cold-Worker und alle Vorher-/Fehlerbelege
bleiben erhalten, ebenso alte FPU-/Benchmarkimages und Stashes.
Keine laufenden Test-VMs/Compiler beim Abschluss, kein Push oder Agent.
Keine Kernel-/ABI-/Budgetaenderung oder neue Performance-/WCET-Zusage;
bytegleiche gemessene Artefakte behalten ihre bisherigen Leistungsbelege.

Die Engine-/Zeit-/DOM-Integration bleibt offen: dieses Paket aktiviert kein
Website-JavaScript. R3.6b geht nur formal auf active; dessen offene Abnahme
bleibt unveraendert und wird nicht vor den Browser gezogen. Naechster
Browser-Voraussetzungsschnitt: isolierte Engine-Portierung auf dem vorhandenen
Speicher-, C/C++-, FPU-, Math- und Textunterbau, ohne doppelte Enginehilfen.

Gezielte Korrekturen am neuen Pruefer: Der Cold-Worker braucht den vorhandenen
C++-Include-Pfad; der Image-Leser muss den existierenden FAT-Kurznamen
`benchm~1.prg` verwenden. Beide Regressionen zuerst fehlgeschlagen, nach
Reparatur bestanden. Alte Cold-Builds/Fehler bleiben erhalten; der finale
Cold-Nachweis bindet alle 168 Eingaben einschliesslich des Pruefers neu.

### Historischer Ausgang und ausdrueckliche Umfangserweiterung

Freigabe zur Fortsetzung: Der Benutzer bestaetigt die Aufnahme des
HTML-Tabellengenerators samt Tests und neuer HTMLWORK-Abnahme. Die bisherige
Blockermeldung unten bleibt als Historie erhalten. Nur dessen zufaellige
Tabellenreihenfolge wird korrigiert; alle anderen Byte-/Resilienzgates bleiben.

Implementiert, noch nicht commit-/abnahmefaehig: 26 Hostfaelle bestehen
(text6/5.432s, libc4/1.092s, build7/0.017s, benchmark9/0.002s), ebenso
VMware66s/QEMU62s und TEXTTEST-Gast15.106s. Der Gast prueft zweimal die
Formate sowie normal37/Pagefault142/kill143/frisch37, exakte Generationen,
Parent-errno/Rundung und frische Shell. TEXTTEST204800 Bytes,
SHA25663776333af8e28e97e5a91196826194c471893d2fe3180f2d27f50ce202cf279.

Urspruenglicher Blocker: Der Bytegleichheits-Guard lehnt HTMLWORK ab. Die neue libc-Header-
Ergaenzung loest den vorhandenen HTML-Bibliotheksbuild aus; dessen gepinnter
Hubbub-Generator verwendet `keys %entities` ohne feste Reihenfolge.
Zwei identische Extraktionen belegen unterschiedliche entities.inc-Digests:
c887645f6e20b71011ce21d25da33d32d54eb8b6fa958b9f10cf87ce0e8dcb9e und
ea0bcf16b4d91846a357aa915537acaa55ea3f4ce936662f0c91e8f09953e52a.
HTMLWORK ist gleich gross (845868 Bytes), aber 53002 Bytes unterscheiden sich;
erste Abweichung0x87924 in der Tabellendarstellung. Keine behauptete semantische
oder Performancegleichheit aufgrund der blossen Groesse.
`scripts/build_html_engine.py` liegt ausserhalb des freigegebenen Pakets.
Die Reparatur und eine neue HTMLWORK-Abnahme brauchen ausdrueckliche Freigabe;
ein altes Binary zurueckzukopieren waere keine Buildreparatur.

Belege unter `build/codex-agent/r322-text/`: host-*-final.log,
package-*-final.log, guest.log/guest-command.log, artifacts.json,
htmlwork-difference.log, generator-nondeterminism.log samt beiden erzeugten
Quelldatensaetzen. Beide aktuellen Images und HTMLWORK/TEXTTEST liegen in
before-html-generator/. R3.21-Sicherungen bleiben unangetastet. Kernel beider
Profile, BROWSER, GTEST und BENCHMARK behalten ihre abgenommenen Digests.
Math-Gast noch nicht ausgefuehrt; kein Implementierungscommit/Queuefortschritt.
Paketvertrag-Commit: a484de23. Alle vorherigen Entwicklungsfehler bleiben
erhalten, insbesondere der korrigierte INT_MAX-Precision-Integerueberlauf.

Der naechste eingefrorene Browser-Voraussetzungsschnitt implementiert die
tatsaechlich fehlenden snprintf/vsnprintf-Aufrufe von QuickJS, ohne dessen
vorhandene dtoa/atod- und rqsort-Implementierungen zu duplizieren. Eigener
opt-in SDK-Vertrag: [String formatting](../architecture/RING3_STRING_FORMAT_CONTRACT.md).
Noch keine vollstaendige Paketabnahme oder JavaScript-Aktivierung.
R3.21 und alle gesicherten Leistungsartefakte bleiben erhalten; R3.6b wird
mit unveraenderten offenen Gates entsprechend dem Browservorrang zurueckgestellt.

## R3.21 abgeschlossen: mathematische Ring-3-Grundlage fuer JavaScript

Alle eingefrorenen Gates bestehen nach den unten belegten gezielten
Korrekturen. Das opt-in SDK bietet 44 binary64-Funktionen aus unveraenderter
gepinnten musl1.2.6-Numerik sowie vier prozesseigene fenv-Operationen.
Keine Heapallokation oder externe Laufzeitabhaengigkeit in libm; keine
Linux-/Allocator-/Threadteile. MATHTEST verwendet fuer seine IPC-Nachrichten
die vorhandene Byte-Laufzeit ohne Heapstart und ist in beiden normalen
Image-Layouts ueber die Userspace-Shell erreichbar. Vertrag:
[Mathematikprofil 1](../architecture/RING3_MATH_RUNTIME_CONTRACT.md).

Finale Belege relativ zu `build/codex-agent/r321-math/`; vollstaendige
eingefrorene Befehle in `automation/reist-s03b.toml`:

| Gate / Kommando | Ergebnis / Dauer | Beleg |
|---|---|---|
| `python test/test_math.py -v` | 8 PASS / 30.013s | `host-math-link-dependency-final.log` |
| `python test/test_libc_source.py -v` | 4 PASS / 1.851s, unveraendert | `host-libc_source-final.log` |
| `python test/test_build_dependencies.py -v` | 7 PASS / 0.013s | `host-build_dependencies-object-final.log` |
| `python test/test_benchmark_source.py -v` | 9 PASS / 0.003s | `host-benchmark_source-object-final.log` |
| Referenz VMware/vga | PASS / 67s | `package-vmware-link-final.log`, `../20260907-183930-package-vmware-vga.log` |
| Referenz QEMU/vga | PASS / 63s | `package-qemu-link-final.log`, `../20260907-184037-package-qemu-vga.log` |
| `run_qemu_math.py --verify-artifacts` | PASS / 0.869s | `artifacts.json`, `artifacts-link-final-time.log` |
| `run_qemu_math.py`, eine CPU | PASS / 13.228s | `guest.log`, `guest-link-final-command.log` |
| `run_qemu_math.py --smp 4` | PASS / 13.511s | `smp.log`, `smp-command.log` |
| Runtime `libc-client` QEMU/vga | PASS / 45s | `runtime-libc-client.log`, `../20260907-184412-runtime-guest-smoke-libc-client.log` |
| Runtime `runtime-desktop-browser-input` QEMU/vga | PASS, 180s-Gastgrenze eingehalten | `runtime-runtime-desktop-browser-input.log`, `browser-input.log` |

Zusammen 28 Hostfaelle. Beide echten Math-Gaeste fuehren zweimal `mathtest`
aus: Numerik/Fenv, frische Kindgeneration, echter #MF16 mit Status144,
schlafendes Kind mit anderer Rundung und Kill143, normales Ende37,
generationstreue Freigabe, unveraenderte Elternkontrollen und frische
HELP-Antwort der Shell. SMP4 bestaetigt alle drei AP-FPU-Kontexte.
Bestehende libc- und Browser-Eingabe/Navigation/Crash/Restart/Konsolentests
sind unveraendert bestanden. Keine neue Performance-/WCET-Zusage aus diesen
Funktionstests und keine Wiederholung des unveraenderten VMware-Benchmarks.

Beide Kernelprofile sowie BROWSER/HTMLWORK/GTEST/BENCHMARK bleiben bytegleich
zur abgenommenen Baseline `0301d708`; alle SHA256 in `artifacts.json`.
Finales QEMU-Image `a5a1397973751486345ffd47ef0698fb99877360a6ce143e8e257614eae28309`,
VMware-Flatdisk `dac34c84613373f5b8f80dd058b047a58de677ae4a5bcd21bfc01d3b967277e1`.
MATHTEST65536 Bytes, SHA256
`0bee6c4057aac105bb7eb87f63869902ccde11078fc40c69f258430b77c467c6`.
Finale Kopien in `accepted-qemu/` und `accepted-vmware/`; alte schnelle
FPU-/Benchmarkimages, Messungen, Stashes und alle Fehler bleiben erhalten.
Keine laufenden Test-VMs/Compiler beim Abschluss. Kein Push oder Agent.

Keine JavaScript-Engine/DOM-, Kernel-, Scheduler-, Benchmark- oder
Budgetaenderung. Der numerische Unterbau ersetzt nicht die noch fehlenden
Interpreter-/Laufzeitadapter und Browserintegration. Nur die Queue geht
auf R3.6b weiter; dessen offene Abnahme bleibt offen und wird in diesem
Paket nicht implementiert oder vor den dokumentierten Browservorrang gezogen.

### Umsetzung und Fehlerkorrekturen (historischer Verlauf)

Fortsetzung nach ausdruecklicher Benutzerfreigabe: genau
`test/test_build_dependencies.py` wurde in den Paketumfang aufgenommen.
Die alte Aufrufassertion kontrolliert jetzt das vorhandene Argumentarray
und weiterhin beide Throw-Zweige bei Fehlercodes. Die neuen Headerchecks
kompilieren echte C/C++-Objekte: Zig meldete mit -fsyntax-only auch bei
vorhandener Eingabedatei FileNotFound. Der Shellcheck prueft den wirklichen
POSIX-Suchpfad. Alle alten Fehlbelege bleiben erhalten.

Erster echter Math-Gast: Numerik und Fenv bestanden, danach zwei enthaltene
Stack-Pagefaults an EIP40006afe/40006b0f, Shell weiterhin vorhanden. Der
bytegleiche Diagnoseneubau (SHA054df84c...) und die Disassemblierung beweisen
direkte Rekursion in der schwachen memset-Ersatzfunktion des unnoetig
gelinkten Compilerarchivs. MATHTEST verwendet jetzt explizit die vorhandene
libreistc-Bytefunktion, ohne Heapinitialisierung; das numerische Archiv hat
ueberhaupt keine offenen externen Symbole. Neues Vorher-Regressionsbeispiel
`before-byte-link.log`, finaler Math-Hostgate 8 PASS / 28.436s in
`host-math-byte-final.log`. Bestehende unveraenderte libc-/Build-/Benchmark-
Gates bestehen mit 4/7/9 Faellen. Beide Referenzen und betroffene Gast-/
Artefaktgates werden auf dem korrigierten Verbraucher wiederholt.
Gasttreiber verwirft unerwartete Pagefaults jetzt sofort; dieselben
Erfolgsmarker, Fehlervektoren und 180s-Deadline bleiben vorgeschrieben.

Der fehlgeschlagene Gast liegt unveraendert in
`guest-before-byte-runtime.log` (180.018s), der erste bestandene Hashvergleich
in `artifacts-before-byte-runtime.json`. Diagnostik:
`mathtest-diagnostic.elf`, `MATHTEST-diagnostic.PRG`,
`recursive-memset-disassembly.log`; nichts geloescht. Ein fehlgeschlagener
CLI-Diagnoseversuch wird ebenfalls behalten; Bibliotheksnamen mit Punkt
sind im bestehenden CLI absichtlich ausgeschlossen. Der dokumentierte
Verbraucher benoetigt nur die normal aufloesbaren Namen m und reistc.

Zusaetzlich nachgewiesener Kandidatenfehler: Der anschliessende
VMware-Build meldete MATHTEST Reused; Datei225280 Bytes und SHA054df84c...
blieben trotz korrigierter Linkliste gleich. Der neue Verbraucher fehlte
noch als Abhaengiger seines eigenen Build-Rezepts. MATHTEST nimmt jetzt
scripts/build_system_programs.py in seine inkrementellen Inputs auf,
analog zu den bestehenden Browserverbrauchern. Regression davor:
`before-link-dependency.log`. Zwischenreferenzen `package-*-byte-final.log`
sind erhalten, aber wegen des alten Verbrauchers keine finale Abnahme.

### Historischer Stopp vor Benutzerfreigabe

`test/test_build_dependencies.py:45`
erwartet den alten Inline-Array-Aufruf von Invoke-PythonProcess. Bereits
`git show HEAD:scripts/build-windows.ps1` belegt stattdessen den bestehenden
Aufruf mit `$systemProgramArguments` und unveraendertem Throw bei Fehlercode.
Die Korrektur gehoert in den Regressionstest, nicht als Scheinreparatur in
den funktionierenden Buildaufruf. Diese Testdatei fehlt in allowed_files;
genau diese Ergaenzung benoetigt Freigabe. Kein Implementierungscommit,
kein Image-Neubau, keine Gate-/Queue-/Schwellenlockerung.

Belege unter `build/codex-agent/r321-math/`:

- `host-math-final.log`: 7 Tests / 30.647s, 5 PASS und 2 Fixturefehler.
  Echte i386-Numerik/Fenv O0/O2 einschliesslich 768 unabhaengiger Samples,
  44 Funktionen, vier Rundungsrichtungen, selektive Flags, Pin-/Metadaten-
  Ablehnung, Gastlog-Negativtests und ELF-Archivabschluss bestehen.
  Offen: Zig stdin-Cache meldet FileNotFound im Headercheck; dafuer einen
  eigenen erzeugten Fixturepfad statt stdin verwenden. Shellquelltest
  erwartet faelschlich DOS-Escapes; bestehende Suchpfade sind /usr/bin usw.
  Beide Reparaturen bleiben in test/test_math.py innerhalb des Pakets.
- `host-libc_source-final.log`: 4 PASS / 1.851s.
- `host-build_dependencies-final.log`: 6 PASS, 1 veraltete Assertion / 0.022s.
- Benchmarkgate, beide Referenzen und alle Gast-/Artefaktgates noch nicht
  ausgefuehrt; keine Laufzeit- oder Leistungsschutzabnahme fuer R3.21 behauptet.
- Vorher- und Entwicklungsfehler samt erfolgreichen numerischen Zwischen-
  pruefungen bleiben erhalten. Keine laufenden Compiler/QEMU-Prozesse beim Stopp.

Kandidat: unveraenderte gepinnte musl-Algorithmen, opt-in libm/fenv und
SDK-Metadaten, MATHTEST in beiden Image-Layouts. Der interne unveraenderte
i386-fsqrt-Helfer deckt acosh mit erweiterter x87-Auswertung ab; kein sqrtl-
API oder drem-Kompatibilitaetsalias. MATHTEST und sein begrenzter Gasttreiber
sind angelegt, aber noch nicht im echten Gast abgenommen.

## R1.3 abgeschlossen: FPU-Isolation und VMware-Leistungsschutz

Alle eingefrorenen Gates bestehen nach den unten belegten gezielten
Testaufbau-Korrekturen. x87/MMX/SSE-Zustand ist nun kernelprivat pro Task-
generation und CPU-Idle-Kontext gesichert, inklusive Null-old-Exit. Frische
Generationen erhalten Standardkontrollen und genullte Register. Keine neue
oeffentliche ABI, keine Quoten-/Schedulerpolicy-/Benchmarkaenderung.
JavaScript, Math/libc und DOM bleiben separate, noch nicht umgesetzte Pakete.
Die Queue schaltet regelgemaess auf R3.6b; dessen offene VMware-Abnahme wird
weder vorweggenommen noch hier implementiert. Browser-Funktionsarbeit bleibt
der dokumentierte Benutzer-Vorrang. Kein Agent, zusaetzlicher Worktree oder Push.

Finale Belege relativ zu `build/codex-agent/r13-fpu/`; Kommandos vollstaendig
im eingefrorenen Paket in `automation/reist-s03b.toml`:

| Gate / Kommando | Ergebnis / Dauer | Finaler Beleg |
|---|---|---|
| `python test/test_fpu_context.py -v` | 15 PASS / 4.654 s | `host-fpu-prompt-final.log` |
| `python test/test_smp.py -v` | 31 PASS / 1.700 s | `host-smp-ap-final.log` |
| `python test/test_scheduler_slack.py -v` | 3 PASS / 1.014 s | `host-scheduler_slack.log` |
| `python test/test_scheduler_time.py -v` | 18 PASS / 0.362 s | `host-scheduler_time.log` |
| `python test/test_scheduler_resource_stats.py -v` | 4 PASS / 0.005 s | `host-scheduler_resource_stats.log` |
| Referenz `test-reist-package.ps1 -Target vmware -Video vga` | PASS / 14 s | `package-preemption-vmware.log`, `../20260907-172414-package-vmware-vga.log` |
| Referenz `test-reist-package.ps1 -Target qemu -Video vga` | PASS / 61 s | `package-preemption-qemu.log`, `../20260907-173333-package-qemu-vga.log` |
| `run_qemu_fpu.py`, APIC | PASS / 18.995 s | `apic-alignment.log` |
| `run_qemu_fpu.py --no-apic`, PIT | PASS / 18.861 s | `pit.log` |
| `run_qemu_fpu.py --smp 4` | PASS / 20.113 s | `smp.log` |
| `run_qemu_fpu.py --unsupported` | PASS / 4.416 s | `unsupported.log` |
| `run_qemu_fpu.py --vmware-fpu-package build/vmware/reist-os` | PASS / 29 s Gast, 31.271 s Wrapper | `workstation-final-fpu.json`, `fpu-workstation-005ec0087e80488897a14bb505d2c53e/serial.log` |
| Runtime `-Mode normal -Target qemu -Video vga` | PASS / 42 s | `runtime-normal.log`, `../20260907-171817-runtime-guest-smoke.log` |
| Runtime `-Mode runtime-desktop-browser-input -Target qemu -Video vga` | PASS, 180-s-Gastdeadline eingehalten | `runtime-browser-input.log`, `accepted-qemu/runtime-desktop.browser.log` |
| `run_qemu_fpu.py --vmware-benchmark-before ... --vmware-benchmark-after ...` | PASS, sechs Gaeste je 31/32 s | `vmware-final-paired.json`, `workstation-63a0dad8718f4823991429e54c8d5a19/` |

Zusammen 71 Hostfaelle. Der Workstation-Gast prueft zweimal echte #MF16,
#XM19 und invalid-MXCSR-#GP13 mit Status144/147/141, Eltern-/Kindzustand,
Preemption/Sleep/Yield, Dirty-Exit, Kill/Reuse, alle APs, frische Shell und
zehn Sekunden Stabilitaet. TCG prueft zweimal das ausdrueckliche Teilprofil
mit echtem Alignment-#GP, keine simulierten Ausnahmen oder Erfolg bei Status94.

Leistungsschutz: drei frische abwechselnde Workstation-Paare, vier CPUs,
1024 MiB, keine parallelen Compiler/VMs. Single-Median 3976.82 ->4098.25
MOp/s (103.0534 Prozent), Multi-Median 4036.62 ->4082.66 (101.1406 Prozent),
beide oberhalb der eingefrorenen 95-Prozent-Grenze. Alle sieben CPU/RAM/HDD-
Rohzeilen je Lauf sowie Ausgangsdisk-/VMX-/Harness-Digests im JSON. Keine
statistisch gesicherte Mehrleistung, exakte 10x-Ursache oder WCET behauptet;
kurze RAM/HDD-Messungen bleiben tickquantisierte Diagnostik. Ausgangsimages
und VMX nach den Messungen unveraendert rueckgeprueft.

Finales VMware-Paket in `build/vmware/reist-os` und separat `accepted-vmware/`:
Raw-/Flatdisk SHA256 `d946ad6301d948104b0d40e18b6e2341129c2663268497cec9e596c694ed5c25`,
Kernel `49a2a5defc545c9687add43418f47b5cd1db03e4f93ac6a9fc5ead4086681a2c`.
Finales gemeinsames `build/reist-os.img` ist QEMU-Profil, separat in
`accepted-qemu-final/`: SHA256
`2979767396f0b9febd7da1584dc8ecfe7ff9a7fa0f7817de504a846f27405ee8`,
Kernel `360739585ff3c46ac6ca097fab8fa86911b7e6f7037fa4c4030e083ade950cdd`.
BENCHMARK.PRG bleibt 28672 Bytes, SHA256
`b001fb18597e4122dc1dad928649c8c281c71bea0cee7b19887074e13facbfb3`;
finales GTEST `48de1c2e41255309083ba67d3649e218c2a15a3ce12622237be2c4f52026d6c0`.
Alle beobachteten Vorherimages, Stashes, Zwischen-PASS- und Fehlbelege bleiben
erhalten. Die folgende Chronologie beschreibt fruehere Zustaende, nicht die
aktuelle Abnahme; insbesondere sind ihre damaligen Stopps jetzt aufgeloest.

### Historie: R1.3 freigegeben, FPU-Isolation vor JavaScript

Benutzerfreigabe nach konkreter Vorpruefung: i386-Compiler erzeugt `fldl`/
`faddl`, der bisherige Scheduler bewahrt diesen Zustand nicht. Sauberer
Ausgangsstand `28973dcb`. Ausschliesslich R1.3 aktiv, R3.6b mit unveraenderten
offenen Gates queued. Vertrag und eingefrorene Abnahme:
FPU_CONTEXT_ISOLATION_CONTRACT.md und automation/reist-s03b.toml.
Noch keine Implementierungs-/Gastabnahme und kein JavaScript-Featureclaim.
Alle bisherigen Browserbelege, Stashes und Fehler bleiben erhalten.

Zusaetzlicher Benutzerauftrag: den deutlich schnelleren VMware-Benchmarkstand
bewahren. Noch vor dem FPU-Build wurden Rawimage, Kernel und komplettes
VMware-Paket nach `build/codex-agent/r13-fpu/observed-before` kopiert.
SHA256 Rawimage `59f19889e2014a8b68228e1b4ee448bf12690c5dfaaefd69bc50106304189ad3`,
Kernel `0b9470d2b1fd1f0551702af6d72f113ab8794f505d42687d7a3ce9055fe182ee`,
VMware-Flatdisk `f4f9c947ebc407071a44146bbc0b432d0015de330c6ccdd559b999dfd54d3dc1`.
Keine Behauptung einer bestimmten Commitprovenienz dieser benutzten Images.
Zusatzgate: drei echte Workstation-Paare mit unveraendertem Benchmark, CPU-
Mediane >=95 Prozent; RAM/HDD-Rohwerte diagnostisch, nicht tickgenau genug
fuer dieselbe Prozentgrenze. Noch keine eigene Leistungsabnahme.

### R1.3 Kandidat: Teilnachweise und erhaltene Historie

Setup-Commits `1ab441ac` und `3cd44a44`; Implementierung im sichtbaren
Worktree, nicht committet. Task-/Idle-Kontexte sichern jetzt eager alle
x87/XMM-Register und Controls; BSP/AP-Profilpruefung und Generationreset
sind implementiert. Keine Aenderung an Benchmark, Schedulerpolicy, Quoten
oder oeffentlicher ABI. JavaScript bleibt unimplementiert.

Hostregression gegen das alte echte i386-`switch.asm`: O0/O2 jeweils
384 Zustandsfehler bei 128 Wiederaufnahmen; Vorherbeleg
`build/codex-agent/js-inventory/fpu-before.log`. Die fuenf eingefrorenen
Hostgates bestehen danach mit insgesamt 62 Tests. Logs relativ zu
`build/codex-agent/r13-fpu/`:

| Kommando | Ergebnis / Dauer | Log |
|---|---|---|
| `python test/test_fpu_context.py -v` | 6 PASS / 2.058 s | `host-final.log` |
| `python test/test_smp.py -v` | 31 PASS / 2.362 s | `host-smp.log` |
| `python test/test_scheduler_slack.py -v` | 3 PASS / 1.014 s | `host-scheduler_slack.log` |
| `python test/test_scheduler_time.py -v` | 18 PASS / 0.362 s | `host-scheduler_time.log` |
| `python test/test_scheduler_resource_stats.py -v` | 4 PASS / 0.005 s | `host-scheduler_resource_stats.log` |
| `.\scripts\test-reist-package.ps1 -Target vmware -Video vga` | PASS / 38 s | `package-vmware.log` |
| `.\scripts\test-reist-package.ps1 -Target qemu -Video vga` | PASS / 61 s | `package-qemu.log` |

Der erste eingefrorene APIC-Aufruf von `scripts/run_qemu_fpu.py` scheitert
nach 180.039 s an fehlendem `FPU_OK` (`apic.log`). Ein gezielter Diagnosebuild
ergaenzt ausschliesslich begrenzte Fehlerausgaben in GTEST; der Kernel bleibt
bytegleich (SHA256
`ec1b5e794331593e0430deed5fb327f337bb6fc1dee466180799dda3ea60c1e8`).
Diagnoselauf mit demselben Aufruf und eigenem
`--log build/codex-agent/r13-fpu/diagnostic-fault-status.log`: FAIL / 13.223 s.
Eltern- und Kind-Preemption bestehen, echter Ring-3-#MF endet mit Status144.
Der nachfolgende `fpu-xm`-Prozess liefert dagegen den Rueckfallstatus94 statt
des erwarteten #XM-Exitstatus147; `fault_index=2`, `stage=97`, `TEST_FAIL FPU`.
Die disassemblierten Instruktionen entmaskieren korrekt MXCSR.Invalid und
fuehren `divps xmm0,xmm0` nach `xorps` aus. Kein Kernelpanic beobachtet.

Installiertes QEMU meldet `11.1.0 (v11.1.0-12130-ge470268ff4)` und nur
TCG als Accelerator. Die QEMU-Quellen dokumentieren fehlende SSE-Traps;
Quellen und Abgrenzung im FPU-Vertrag. Der genaue installierte Commit war
online nicht abrufbar; keine Behauptung eines vollstaendigen Sourceabgleichs.
Der beobachtete fehlende Trap verhindert den eingefrorenen Nachweis.
Keine Umdeutung von Status94 zu PASS, kein simulierter Ersatzinterrupt.

PIT, SMP4, Unsupported-CPU, Normal-, Browser- und gepaarte Workstation-Gates
sind **noch nicht ausgefuehrt**. Die Referenzbuilds/Hostgates liegen vor den
abschliessenden GTEST-Diagnoseausgaben; `build/reist-os.img` enthaelt jetzt
den Diagnosebuild (`diagnostic-build.log`), das VMware-Paket noch den ersten
Referenzkandidaten. Diese Artefakte sind keine abgeschlossene Freigabe.
Saemtliche Vorher-/Fehlbelege bleiben erhalten, Queue bleibt R1.3 aktiv.
Eine Verlagerung der erforderlichen FP-Fehlerabnahme auf echte Workstation
benoetigt eine ausdrueckliche Aenderung des eingefrorenen Hardware-Gates;
noch keine solche Aenderung oder Erweiterung des Dateiscopes vorgenommen.

Anschliessende ausdrueckliche Benutzerfreigabe: verpflichtenden #XM-Nachweis
auf echte VMware Workstation verlagern und deren Testskript erweitern.
Die Queue friert nur diese Plattformkorrektur ein: QEMU behält #MF/#GP und
alle Zustands-/Reuse-Pruefungen, Workstation verlangt zweimal das volle
Profil plus frische Shell und Stabilitaet. Kein Benchmark-/Grenzwertumbau.
Umsetzung und neue Abnahme noch offen; vorheriger Befund bleibt historisch.

### R1.3 Fortsetzung: VMware-FPU und Leistung bestanden

Vertragscommit `433d321c` friert die ausdruecklich genehmigte Verlagerung
des #XM-Nachweises auf echte Workstation ein. Der Kandidat bleibt bis zum
Bestehen aller anderen eingefrorenen Gates uncommittet und R1.3 aktiv.

Gezielte, durch Fehlbelege abgegrenzte Korrekturen:

- PowerShell lieferte bei nicht vorhandenen Compilerprozessen trotz leerer
  Prozessliste Exit1. Expliziter Erfolg erst nach der unveraenderten
  Busy-Pruefung; Regression prueft sowohl Abwesenheit als auch einen echten
  laufenden Prozess. `preflight-before.log`, `host-platform-preflight-fixed.log`,
  `workstation-fpu-preflight-failure.json` bleiben erhalten.
- Sandboxstart scheiterte vor dem Gast am VMware-Autorisierungsdienst
  (`W32AuthConnectionLaunch: WriteFile failed`, Zugriff verweigert).
  `workstation-fpu-sandbox-failure.json` und `workstation-sandbox-ui.log` bleiben;
  folgende eigene versteckte Testkopien liefen mit freigegebenen Hostrechten.
- Echter Ryzen-Workstation-Boot wies die gueltige AMD-MXCSR-Maske `0002ffff`
  ab. AMD-APM-Bit17 wird vom gleichen Legacy-FXSAVE gesichert und bleibt im
  frischen Kontext null. Die Korrektur akzeptiert dieses definierte Bit,
  nicht beliebige hohe Bits oder gemischte CPUs. O0/O2-Vorher-/Nachher-
  Regression: `amd-mask-before.log`, `amd-mask-after.log`; Bootfehler:
  `workstation-fpu-amd-mask-failure.json`, `diagnostic-workstation-boot-stage.json`.
- Danach bestanden beide vollstaendigen Ring-3-Fehlerlaeufe; drei parallele
  AP-Ausgaben waren aber ineinandergeschrieben. Der BSP publiziert sie jetzt
  erst nach geprueftem AP-Ruecklauf und Reap; keine Masken-/Fristlockerung.
  `workstation-fpu-ap-evidence-failure.json` und `ap-evidence-before.log` bleiben.
- Im zweiten Vorher-Benchmarkboot ueberlagerte bereits das alte Image den
  ersten Shellprompt mit AP-Meldungen; Benchmark wurde nicht gestartet.
  Nur bei fehlendem Prompt nach allen anderen Bereitschaftsmarkern fordert
  das Benchmark-Preflight einmalig durch eine Leerzeichenzeile einen neuen
  Prompt an. Kein Benchmarkretry, keine geaenderte Messung/Frist/Schwelle.
  Echte PowerShell-Funktion hostgeprueft. Ganze drei Paare neu gemessen;
  `vmware-paired-prompt-failure.json` und `workstation-c18cc4df6c404f3a866accb7748b55a0/`
  erhalten die erste Messreihe einschliesslich beider vorhandenen Rohmessungen.

Aktuelle Hostbelege: FPU 11 PASS / 5.397 s (`host-prompt-final.log`), SMP
31 PASS / 1.700 s (`host-smp-ap-final.log`), unbeeinflusste Schedulergruppen
3/18/4 PASS weiter gueltig: zusammen 67 Faelle. Alle Zwischenlaeufe bleiben.
Letzte VMware-Referenz PASS / 15 s (`package-ap-vmware.log`), Detail
`../20260907-164809-package-vmware-vga.log`.

Vollstaendiger Workstation-FPU-Gast PASS / 29 s, Wrapper 31.466 s:
`workstation-fpu.json`, Rohbeleg
`fpu-workstation-50804ac667f546eb9655ac6ceb1168f2/serial.log`.
Zweimal echte #MF16/#XM19/#GP13 mit Status144/147/141, alle Register und
Controls bei Eltern-/Kind-Preemption, Sleep/Yield, Dirty-Exit, Kill/Reuse,
alle drei AP-Kontexte, frische Shellantworten und zehn Sekunden Stabilitaet.
Der spaetere Prompt-Fix ist ausschliesslich im Benchmark-Modus aktiv.

Gepaarter unveraenderter Benchmark PASS: `vmware-paired.json` und
`workstation-9e03478b70914af8930181e5c055ae2a/`; sechs frische Laeufe
je 31/32 s, vier CPUs und 1024 MiB. CPU-Single-Median 3947.58 ->4098.25
MOp/s (103.8168 Prozent), Multi-Median 4013.98 ->4074.92 MOp/s
(101.5182 Prozent), jeweils >=95 Prozent. RAM/HDD-Rohwerte vollstaendig
im JSON, weiterhin kurze tickquantisierte Diagnostik. Kein statistisch
gesicherter Mehrleistungs- oder WCET-Claim und keine gesicherte 10x-Ursache.
Beide Ausgangsdisks/VMX wurden unveraendert rueckgeprueft. Nachher-Flatdisk
SHA256 `e852018af1f7d275130c318a4d49d1dee181e4b5d9a65b5369c746c1fd037d61`,
Kernel `49a2a5defc545c9687add43418f47b5cd1db03e4f93ac6a9fc5ead4086681a2c`,
BENCHMARK.PRG unveraendert `b001fb18597e4122dc1dad928649c8c281c71bea0cee7b19887074e13facbfb3`.
QEMU-Referenz PASS / 61 s (`package-ap-qemu.log`, Detail
`../20260907-165940-package-qemu-vga.log`); QEMU-Laufzeitabnahme noch offen.

### Historischer Stopp: QEMU liefert auch den invalid-MXCSR-#GP nicht

Der eingefrorene APIC-Teilprofilaufruf
`python scripts/run_qemu_fpu.py --qemu 'C:/Program Files/qemu/qemu-system-i386.exe' --image build/reist-os.img --log build/codex-agent/r13-fpu/apic-tcg.log`
endet nach 13.499 s mit FAIL. Beide Preemption-Marker und echte #MF-Beendigung
sind vorhanden; `fpu-gp` liefert aber Rueckfallstatus94 statt141,
`fault_index=3`, `stage=97`, `TEST_FAIL FPU`. Kein #GP-Interrupt wird geliefert.
Der gleiche unveraenderte invalid-MXCSR-Code lieferte auf Workstation zweimal
den echten #GP13 und Status141. QEMU wurde vom Harness beendet.

Die bereits gelesenen offiziellen QEMU-Quellen zeigen fuer `gen_LDMXCSR`
und `helper_ldmxcsr` nur den Aufruf von `cpu_set_mxcsr`, das die Bits ohne
Reserved-Bit-Validierung uebernimmt. Der genaue installierte Development-
Commit bleibt nicht abgleichbar; der konkrete Gastbefund ist unabhaengig
davon belegt. Kein Kernelworkaround oder vermeintlicher PASS bei Status94.

Die letzte Freigabe verlaegerte ausschliesslich #XM und liess invalid-MXCSR
unter QEMU verbindlich. Diese neue Plattformgrenze wird nicht still geaendert.
Vorgeschlagene weitere Zuordnung: invalid-MXCSR ebenfalls verpflichtend auf
Workstation (Nachweis vorhanden), QEMU behaelt einen echten #GP ueber
fehl-ausgerichtetes FXRSTOR samt Prozess-/Reuse-Pruefung. Noch nicht umgesetzt
oder freigegeben. PIT/SMP/Unsupported/Normal/Browser-Gastgates sind noch nicht
ausgefuehrt. Queue bleibt aktiv; kein Implementierungscommit, keine
JavaScript-Freigabe, keine abgeschlossene Gesamt-Abnahme.

Anschliessende ausdrueckliche Benutzerfreigabe: invalid-MXCSR verbindlich
unter Workstation pruefen und QEMU-#GP ueber fehl-ausgerichtetes FXRSTOR
erhalten. Queue und FPU-Vertrag frieren diese Zuordnung vor Umsetzung ein.
Alle Fehler-/Reuse-Anforderungen und Leistungsgrenzen bleiben unveraendert;
neue finale Lognamen erhalten die vorigen Fehl- und PASS-Belege.

### R1.3 Abschlusslauf: gezielte Testaufbau-Korrekturen

Vertragscommit `7815e89a` friert den echten Alignment-#GP fuer TCG ein.
APIC/PIT/SMP4 bestehen danach in 19.024/19.053/20.313 s. Der erste negative
CPU-Gast scheitert nach 60.040 s vor der FPU-Pruefung: das Pentium-Modell
kennt das vom bestehenden Kernel verwendete `cmovb` nicht. Interrupttrace
`diagnostic-pentium-interrupts.log` zeigt #UD bei EIP `001a5376`, dann Triple
Fault; Disassembly bestaetigt exakt diese Instruktion. `unsupported-pentium-failure.log`
bleibt erhalten. Die negative Fixture verwendet nun dieselbe Integer-Basis
`qemu32` ohne SSE/SSE2, unveraenderte 60-Sekunden-Grenze und ausdrueckliche
FPU-Profilablehnung vor READY/BOOT_OK/Shell: PASS / 4.416 s (`unsupported.log`).
Keine Aenderung am Kernel und keine Pentium-/i586-Kompatibilitaetszusage.

Normalgast PASS / 42 s (`runtime-normal.log`, Detail
`../20260907-171817-runtime-guest-smoke.log`); Browser-Input PASS
(`runtime-browser-input.log`) einschliesslich Tastatur/Edit/Navigation,
Crash/Restart und neuer Konsolenantwort. Geprueftes QEMU-Rawimage, Kernel,
GTEST, Benchmark und Browser-Rohlog/Screenshot liegen in `accepted-qemu/`;
dieser Zwischenstand bleibt auch nach der letzten reinen Testausgabekorrektur
erhalten. Rawimage SHA256
`1b882eae26ccc8c30396fe711b9a7d5d3eee60a4150e8cdb605ca11e4e802d8b`,
Kernel `360739585ff3c46ac6ca097fab8fa86911b7e6f7037fa4c4030e083ade950cdd`.

Der Workstation-Abschlusslauf liefert alle Fehlervektoren und Reuse-Erfolge,
aber Eltern-/Kindausgabe ueberlagert sich zu `FPU_PREEFPU_PREEMPT_OK parent`.
`workstation-final-fpu-preemption-failure.json` und Rohlog bleiben erhalten.
Nun meldet ausschliesslich der Elternprozess nach erfolgreichem Kindstatus38,
Reap und Zustandsvergleich beide Erfolge. Die beiden 500-ms-Workloads, ihre
gleichzeitige Ausfuehrung, Sleep/Yield, Faults und Statuspruefungen bleiben
unveraendert. Vorherregression `preemption-evidence-before.log`.

Im naechsten Workstation-Start zerstoeren Hintergrundmeldungen den ersten
Shellprompt; kein GTEST wurde gestartet. `workstation-final-fpu-prompt-failure.json`
bewahrt diesen vollstaendig bis zur 180-Sekunden-Deadline fehlgeschlagenen Lauf.
Die bereits verhaltensgepruefte einmalige Leerzeilenanforderung des Benchmark-
Preflight gilt jetzt auch fuer FPU, erst nach den anderen Bereitschaftsmarkern.
Keine Fristlockerung, keine Wiederholung mehrdeutiger Test-/Benchmarkbefehle.
Vorherregression `fpu-prompt-before.log`; final 15 FPU-Hosttests PASS / 4.654 s
(`host-fpu-prompt-final.log`). Kernel und BENCHMARK.PRG bleiben bytegleich.

Betroffen erneut zu pruefen: FPU-Host, beide Referenzen fuer das endgueltige
GTEST, APIC/PIT/SMP4 mit neuen Erfolgsausgaben und volle Workstation-FPU-Abnahme.
Normal-/Browser-/Unsupported- und die vier anderen Hostgates bleiben gueltig:
deren Kernel und ausgefuehrte Programmpfade wurden nicht geaendert. Der finale
Leistungsvergleich bleibt verpflichtend; keine Gesamtfreigabe aus Teilbelegen.

## R3.20 abgeschlossen: Browsermodell hinter der C-Grenze

Alle zehn Hostgruppen / 93 Faelle, beide Referenzbuilds und sieben Gastgates
bestehen. C++-Modellpilot beibehalten: dieselben C-Aufrufe und beobachtbaren
Werte hinter geprueften privaten Typen, ohne neue Ressourcenownership.
Die Vergleichsmessung bleibt innerhalb aller eingefrorenen Grenzen;
kein JavaScript-/Webfeature-, Hardware-WCET- oder allgemeiner Webseitenclaim.

Ausgangscommit `a7d27aa6`, Worktree vor Kandidatenarbeit sauber. Die
abgenommene C-UI-Baseline unter `build/codex-agent/r320a/accepted-c` wurde
vor Modellkonvertierung samt Image-, Oracle- und Harness-Digests geprueft.
Unveraenderter Oracle `864f869a7862af219eedd7e42dee1abba14ebfdf`, Blob
`6b0de40d251a7c1ba70e2989cf361f3bb0a7b737`. Keine erneute Baselineerfindung,
kein Eingriff in main.c oder die eingefrorenen Mess-/Surface-/Compositorpfade.

Eine echte C++-Implementierung ersetzt `browser_model.c`; alle sechs C-
Signaturen/Layouts bleiben. Kleine private Adress-/Textbereichs-/Scrollwerte
kapseln Zulassungsinvarianten, besitzen aber weder Heap noch Navigation oder
Worker. Die fuenf weiter aktiven Modellaufrufe bleiben im Browser; der bereits
durch CSS-Szenenprojektion ersetzte Legacy-Layout-Einstieg bleibt erhalten und
differentiell geprueft. Kein erfundener neuer Layoutcaller, keine neue Web-
oder JavaScript-Funktion. Vertrag: USERSPACE_SDK_AND_PORTABILITY.md.

Finale Nachweise: Originale C-Layout-/Bildfixture unveraendert; zusaetzlich
54138 differentielle Checks je O0/O2 fuer Adressen, Pointer/Scroll, Anker,
UTF-8, schmale Layouts und Bereiche. Hostmediane bei je 200000 Zyklen in
fuenf frischen C/C++-Paaren: Adresse 5.878/6.298 ns, Scrollbar 134.262/134.695 ns.
Keine UI-Latenzen; Grenzen weiterhin C++ <=120 Prozent des gepaarten C und
<=50000 ns/Zyklus. Typisierte Ergebnisse <=64 Bytes, keine neue Allokation
oder Kopie von Layout-/Bildpayloads. i386-Stackspitzen C/C++ je Einstieg:
Layout 100/112, Rechteck 4/4, Anker 156/156, Adresse 16/16, Scrollconfigure
164/168, Scrollpointer 156/156 Bytes; alle Deltas <=256. Unveraenderte externe
C-Callees werden separat als gleiche Blattkante abgeglichen.

Finaler eingefrorener Modellaufruf `python test/test_browser_model_cpp.py -v`:
10 PASS / 5.650 s, `build/codex-agent/r320/gate-model-final.log`.
Verhalten, alle Rohzeiten und Stack-/Korruptionsnachweise liegen in
`model-e4c85074cc074756ab7f6af8e212e4a3/`. Der komplette betroffene Gateaufruf
wurde nach der gezielten Stackreparatur erneut abgenommen, nicht nur ein
ausgewaehlter Testfall; die neun anderen Hostgates blieben einmalig.
Vorheriger 9-PASS/1-Stackfehler in `gate-model-cpp.log` und `model-zkzpck09/`
bleibt erhalten; isolierter Reparaturnachweis `gate-model-stack-final.log`
(0.774 s), `model-60ad7025b099481e8eaab22e6781cce0/` ebenfalls.

Alle zehn Hostgruppen / 93 Faelle bestehen inzwischen, einschliesslich des
separat korrigierten Stackfalls. Weitere Logs relativ zu `r320/`:

| Eingefrorener Hostaufruf (je `python test/... -v`) | PASS / Suite-Dauer | Log |
|---|---|---|
| `test_cpp_types.py` | 5 / 4.764 s | `gate-cpp_types.log` |
| `test_user_cpp.py` | 6 / 5.693 s | `gate-user_cpp.log` |
| `test_user_program_toolchain.py` | 23 / 147.527 s | `gate-user_program_toolchain.log` |
| `test_cpp_baseline.py` | 5 / 0.075 s | `gate-cpp_baseline.log` |
| `test_gui_browser_source.py` | 8 / 37.459 s | `gate-gui_browser_source.log` |
| `test_browser_navigation_source.py` | 4 / 2.950 s | `gate-browser_navigation_source.log` |
| `test_browser_runtime_source.py` | 29 / 8.219 s | `gate-browser_runtime_source.log` |
| `test_browser_public_navigation.py` | 2 / 3.564 s | `gate-browser_public_navigation.log` |
| `test_browser_forms.py` | 1 / 0.364 s | `gate-browser_forms.log` |

Beide eingefrorenen Referenzen bestehen: VMware 65 s (`package-vmware.log`,
Detail `../20260907-151523-package-vmware-vga.log`), QEMU 61 s
(`package-qemu.log`, Detail `../20260907-151701-package-qemu-vga.log`).
Browser unveraendert 2805788 Datei-/6182953 Loaderbytes, neuer SHA256
`e52aaa1c502993ff729c54d31eed5cd7330ecb3208e45fedac6596473fb271e0`.
HTMLWORK bytegleich zur abgenommenen Basis: 845868/2752100 Bytes, SHA256
`20c4d026c264878aa70bacb9ec5f2865d9a4814968994dde80811a76cf42643d`.
Quellzeilen (keine semantische Komplexitaetsmetrik): vorher 314 C, nachher
326 C++ plus 74 private Headerzeilen. Sechs C-Funktionen, keine Owned-
Ressource, kein neues init/destroy-Paar oder Cleanup-Pfad. Der Gewinn ist die
explizit gepruefte, nicht frei konstruierbare Zustandsdarstellung; keine
behauptete LOC-/Cleanup-Reduktion oder neue Browsing-Kompatibilitaet.

Alle folgenden eingefrorenen Gastgates bestehen beim ersten Lauf. Aufruf
jeweils `.\scripts\test-reist-runtime.ps1 -Mode MODE -Target qemu -Video vga`;
Logs relativ zu `build/codex-agent/r320/model-acceptance/`:

| MODE | PASS / Wrapper-Dauer | Log / zusaetzliche Belege |
|---|---|---|
| `cpp-client` | 11.325 s | `cpp-client.log`, `../../20260907-151900-cpp-client.log` |
| `runtime-desktop-browser` | 51.936 s | `runtime-desktop-browser.log`, gleichnamige `.browser.log`/`.ppm` |
| `runtime-desktop-browser-resources` | 42.861 s | `runtime-desktop-browser-resources.log`, `.browser.log`/`.ppm` |
| `runtime-desktop-browser-input` | 73.699 s | `runtime-desktop-browser-input.log`, `.browser.log`/`.ppm` |
| `runtime-desktop-browser-forms` | 44.483 s | `runtime-desktop-browser-forms.log`, `.browser.log`/`.ppm` |
| `runtime-desktop-browser-public` | 55.398 s | `runtime-desktop-browser-public.log`, `.browser.log`/`.ppm` |
| `runtime-desktop-browser-model` | 85.479 s | `runtime-desktop-browser-model.log`, C/C++-Paar unten |

CPPTEST bestaetigt Lebensdauer, Backing-Freigabe, OOM/Fault/Kill/Reap, frisches
Kind und Shell. Browser-/Ressourcengates pruefen CSS-Pixel, Resize, Wheel,
Ressourcenfehler, Abbruch, frisches Nachladen, Recovery und Cleanup. Input
bestaetigt echte Eingaben/Navigation, Crash, Neustart und frische Konsole;
Formulare beweisen exaktes GET, Reflow, Ablehnung ohne Request, Reset und
Recovery. Public prueft grosse kodierte HTTP-Dokumente, Weiterleitung,
Worker-Raster und Close, keine beliebige reale Website oder Skriptausfuehrung.

Gepaartes Modellgate verwendet unveraendert `--model-ui-pair` mit der bereits
abgenommenen `build/codex-agent/r320a/accepted-c/`-Basis. `paired.json` ist
`passed=true`; `paired-c/` und `paired-cpp/` bewahren je alle 64 Rohmessungen,
Pixelreadbacks, Commitbestaetigungen, Gastlogs und `boot.json`. C/C++:
Tipp-p95 60.0902/60.4288 ms, Scroll-p95 114.6334/125.0032 ms, Maxima
119.1616/127.7303 ms; Gastzeiten 40.811/42.624 s. Absolute 250/500-ms-
Grenzen und relative 120 Prozent +1 ms bleiben gleich; beide Browser
schliessen sauber und geben das Terminal zurueck. Keine Wiederholung oder
konkurrierende VM-/Compilerlast. Alle QEMU-Prozesse beendet.
Kandidatenimage-SHA256
`b9609822de2ccbc67aa6112d6d7559d6d03765bdfe89ddf81c124df8e479c3ec`;
C-Image, Modell-Oracle und saemtliche Harness-/Fixture-Digests unveraendert.
Originale fehlgeschlagene Baselines und alle Stashes bleiben erhalten.

R3.20 ist done, das einzige verbleibende queued-Paket R3.6b wird active;
keine Umsetzung oder Abnahmebehauptung dieses Nachfolgers in diesem Lauf.
Neue Browser-Funktionen haben weiterhin Vorrang vor breiter optionaler
C++-Migration, brauchen aber einen eigenen eingefrorenen Paketvertrag.

Erhaltene Regressionen/Reparaturen, keine stillen Wiederholungen:
`model-regression-before.log` zeigt den noch fehlenden C++-Adapter vor der
Konvertierung. `gate-model-cpp-before-fixture-repair.log` und
`model-before-fixture-repair/` enthalten den ersten Gatefehler: URL-Zeichen
falsch gezaehlt, Host-Struct-Padding als Wert verglichen und veralteten
Layoutcaller verlangt. Alle benannten Modellfelder und die echte URL bleiben
nun explizit geprueft. `gate-model-cpp-before-inline-repair.log` zeigt die
messbare Adressregression (6.269/10.985 ns); Disassembly belegt einen
unnoetigen View-/Methodenaufruf, der gezielt inline gestellt wurde.
Die leere HTML-Fixture erwartet nun korrekt Parserfehler, waehrend das leere
Layout weiterhin gegen C geprueft wird. Der Stackpruefer unterscheidet
relokationsgepruefte lokale Switches von weiterhin verbotenen indirekten
Aufrufen und erkennt flag-erhaltende MOVs zwischen CMP/JA. Eindeutige
Artefaktpfade verhindern verlorene .su-Nebenausgaben bei Zig-Cachehits.
Nur die von diesem Lauf erzeugte Python-Temp-ACL wurde auf geerbte Workspace-
Leserechte zurueckgestellt; Inhalte/Fehlerbelege und alte Stashes bleiben.

## R1.1a abgeschlossen: CPU-Abrechnung und sichere Hintergrundzeit

Alle eingefrorenen Gates bestehen. Der Scheduler rechnet die wirklich
dispatchte Klasse ab und vergibt zusaetzliche Ambient-/Service-Zeit nur hinter
jedem bereiten Kandidaten mit Normalbudget. Safety-Depletion und Clockfehler
sperren diesen Zusatzpfad. Keine Budget-/Timer-/ABI-/Kapazitaetserhoehung;
CPU-Besitz, Affinitaet und generationsgebundene Vererbung bleiben erhalten.

Abnahmebelege unter `build/codex-agent/r11a/`; vollstaendige, unveraenderte
Kommandos stehen in `automation/reist-s03b.toml`, Paket R1.1a.

| Gate | Ergebnis / Dauer | Log |
|---|---|---|
| `test_scheduler_slack.py -v` | 3 PASS, echte O0/O2-Funktionen, 1.204 s | `gate-scheduler_slack-static-metric.log` |
| `test_reist_scheduling_policy.py -v` | 6 PASS, 0.420 s | `gate-reist_scheduling_policy.log` |
| `test_scheduler_time.py -v` | 18 PASS, 0.466 s | `gate-scheduler_time.log` |
| `test_scheduler_resource_stats.py -v` | 4 PASS, 0.004 s | `gate-scheduler_resource_stats-final.log` |
| `test_runtime_degradation.py -v` | 3 PASS, 0.023 s | `gate-runtime_degradation-final.log` |
| `test-reist-package.ps1 -Target vmware -Video vga` | PASS, 15 s | `package-vmware-static-metric.log` |
| `test-reist-package.ps1 -Target qemu -Video vga` | PASS, 66 s | `package-qemu.log` |
| `run_qemu_scheduler_slack.py`, APIC | PASS, 41.523 s | `runtime-slack-apic.log`, `slack-apic.log` |
| gleicher Gast mit `--no-apic` | PASS, 20.743 s | `runtime-slack-pit.log`, `slack-pit.log` |
| gleicher Gast mit `--smp 2` | PASS, 92.012 s | `runtime-slack-smp.log`, `slack-smp.log` |
| `test-reist-runtime.ps1 -Mode normal -Target qemu -Video vga` | PASS, 51 s | `runtime-normal.log`, `normal.guest.log` |
| gleicher Runtime-Aufruf mit `-Mode runtime-desktop-browser-input` | PASS, ca. 81 s (Logzeitfenster) | `runtime-browser-input.log`, `browser-input.guest.log`, `browser-input.ppm` |

Die drei neuen Gaeste pruefen je zwei echte 1000-ms-Lastphasen, danach
Sleep/Yield/Kill/Reuse, Ring-3-Invalid-Opcode mit erwartetem Exitstatus und
anschliessende frische Shell-Kommandos. Beobachtete angrenzende 1-ms-Ticks:
APIC 964/952, PIT 861/870, SMP 980/966 (Grenze unveraendert mindestens 400).
Das ist keine CPU-Auslastungs- oder WCET-Messung. APIC-Kalibrierung und
PIT-Fallback sind in den jeweiligen Bootlogs bestaetigt. Der SMP-Gast meldet
online=2, failed=0, Scheduler-Probe-Maske 2 sowie einen erfolgreich reapten
AP-Worker. Normale Systempruefung: alle Gaststufen einschliesslich
Kapazitaetserschoepfung/Freigabe und Exceptions bestehen. Browserpruefung:
Tastatureingabe, Editieren, Navigation, Crash, Neustart und frische Konsole
bestaetigt. Alle QEMU-Prozesse sind beendet.

Abgenommenes Image gesichert als `accepted-scheduler.img`, SHA256
`ab1f5cdb55e1e12ee8834e66ee2b49e7bbd16fedb18e3c687a4222bbf27a25d3`.
Referenzdetails: `../20260907-142215-package-vmware-vga.log`,
`../20260907-142258-package-qemu-vga.log`; Normalgast:
`../20260907-142743-runtime-guest-smoke.log`.

Gezielte Reparaturen, ohne abgesenkte Gates: Die ausdruecklich freigegebene
Ressourcen-Testdatei prueft nun den bestehenden Fehlerzweig samt Rueckgabe.
Der erste VMware-Build (`package-vmware.log`, 80 s;
`../20260907-141952-package-vmware-vga.log`) scheiterte am vom Compiler
erzeugten `memcpy` fuer den neuen GTEST-Diagnosepuffer. Statischer
prozessprivater Speicher beseitigt diese unnoetige Linkabhaengigkeit;
alle vier Ziffern werden pro Aufruf neu gesetzt. Betroffene Slack-Hostpruefung
und VMware-Build einmal gezielt wiederholt und bestanden. QEMU und alle
fuenf Gastgates bestehen beim ersten Lauf. Alle vorherigen Fehlerlogs bleiben.

Queue geht nur an R3.20a zurueck. Browserquellen/-stash wurden nicht angefasst;
keine C++-Migration oder JavaScript-Implementierung in diesem Paket. Die
pixelgepruefte Browser-Latenzabnahme bleibt unveraendert erforderlich.

### Historischer Zwischenstand und erhaltene Fehlerbelege

Fortsetzung: Der Nutzer hat die Aufnahme von
`test/test_scheduler_resource_stats.py` ausdruecklich freigegeben. Nur die
veraltete positive Sourceform wird auf den tatsaechlichen Fehlerzweig
abgeglichen; Reclaim-/Peak-Anforderungen und alle eingefrorenen Gates bleiben.
Die drei bereits bestandenen, unveraenderten Hostgates werden nicht wiederholt.

Setupcommit `5cfc0416`, noch kein Implementierungscommit. Die drei ersten
finalen Hostgates bestehen: Scheduler-Slack 3 Tests / 1.236 s (vier echte
Scheduler-Verhaltensgruppen jeweils O0/O2), bestehende Policy 6 / 0.420 s,
Scheduler-Time 18 / 0.466 s. Logs: `build/codex-agent/r11a/gate-*.log`.
Vorher-Nachher-Belege zeigen den Ausschluss eines allein bereiten Ambient-
Tasks nach 15 ms sowie rueckwirkende Fehlabrechnung nach IPC-Vererbung.
Kandidat: budgetierter erster Auswahlpass, begrenzter Hintergrundpass,
CPU-lokale Dispatchklasse und Yield ohne vorzeitige READY-Publikation.
Eine neue Ring-3-GTEST-Diagnose und der versteckte Gastpruefer sind angelegt,
aber noch nicht gebaut oder im Gast ausgefuehrt.

Abnahmeblocker: `test_scheduler_resource_stats.py` scheitert an einem bereits
in HEAD veralteten Source-Vergleich. Der Test sucht
`reclaimed.active_tasks < exhausted.active_tasks` und Peak `>=`; der
unveraenderte Gast prueft stattdessen im Fehlerzweig Active `>=` und Peak `<`.
Die Bedeutung ist gleich, die gesuchte Schreibweise falsch. Die drei anderen
Ressourcenchecks bestehen. Diese Testdatei liegt ausserhalb `allowed_files`;
keine Aenderung oder Gate-Auslassung ohne ausdrueckliche Umfangsfreigabe.
Runtime-Degradation, Referenzbuilds und alle Gastgates deshalb nicht gestartet.
Kein Abnahme-/Performanceclaim, R1.1a bleibt aktiv. Quellenkandidat bleibt
sichtbar und uncommittet, alle Stashes bleiben erhalten.

Weitere erhaltene Diagnosebelege: `regression-before.log` und
`regressions-before-all.log` (vier Gruppen scheitern O0/O2). In
`regressions-after.log` war nur eine falsche Testfixture-Erwartung uebrig:
nach Service-Auswahl folgt im vorhandenen Zyklus Ambient; Idle muss den
vorher laufenden Testtask tatsaechlich schlafen legen. Beides korrigiert,
ohne Produktionszyklus oder Grenzwerte zu aendern. Der erste finale
Slack-Gate fand ausserdem den irrtuemlich im neuen Sourcecheck benannten
SDK-Wrapper; auf den realen `x86os_spawnv` korrigiert. Fehlerlog bleibt als
`gate-scheduler_slack-before-dispatch-spelling.log` erhalten.

Separates Scheduler-Paket vor R3.20a, ausdruecklich vom Nutzer freigegeben.
Der noch nicht abgenommene Browser-/Surface-Kandidat ist dateigenau in
`d9370608c5849bbae36663d515c7accd24930005` gesichert; beide aelteren Stashes
und alle R3.20a-Fehlerbelege bleiben erhalten. Keine Browserquelle in diesem
Paket. Abnahme und Grenzen stehen im
[Schedulervertrag](../architecture/SCHEDULER_BACKGROUND_SLACK_CONTRACT.md).
Setup wird vor Implementierung committet. Hintergrundzeit bleibt hinter
jedem bereiten Kandidaten mit Restbudget; keine Budget-/Timer-/ABI-Erhoehung.
Nach erfolgreicher Abnahme wird nur R3.20a reaktiviert, nicht mitimplementiert.

## R3.20a abgeschlossen: Browser-Bilduebergabe vor Sprachumbau

Alle eingefrorenen Gates bestehen auf dem separat abgenommenen Scheduler
`5ee7c11d`. Vom sauberen Worktree wurden genau die zwoelf zugeordneten
Kandidatendateien aus `d9370608c5849bbae36663d515c7accd24930005` wiederhergestellt;
keine weitere Produktionsreparatur in diesem Lauf. Viewport-only-Damage,
einmalige Broker-Uebergabe und Escape-Lookahead behalten alle bestehenden
Generations-, Freigabe-, Eingabe-, Frist- und Kapazitaetsgrenzen bei.

Abnahme: 32 echte Adress-Edits und 32 echte alternierende Mausradschritte,
jeweils geordnete Commit-Bestaetigung und exakte Gastpixel. Tippen-p95
62.8166 ms, Scroll-p95 117.1034 ms, Maximum 118.5195 ms. Die unveraenderten
Grenzen sind p95 <=250 ms je Strom und jeder Schritt <=500 ms. Gastlauf
41.635 s bei weiterhin 180-s-Bootfrist und bestehender 30-s-Browserprobe.
Beide Escape-Zustaende, `BROWSER_CLOSE_OK` und `TERMINAL_INPUT_IDLE` bestaetigt.
Kein ausgelassener Messwert, keine Wiederholung oder gelockerte Grenze.

Alle folgenden Gates bestehen beim ersten Lauf der wiederaufgenommenen
Abnahme. Vollstaendige eingefrorene Kommandos: `automation/reist-s03b.toml`,
Paket R3.20a. Logs relativ zu `build/codex-agent/r320a/resume-scheduler/`:

| Gate | Ergebnis / Dauer | Log |
|---|---|---|
| `python test/test_browser_surface_latency.py -v` | 3 PASS, echte C-Funktionen O0/O2, 3.308 s | `host-browser_surface_latency.log` |
| `python test/test_browser_model_cpp.py -v` | 5 PASS, Telemetrie, 0.003 s | `host-browser_model_cpp.log` |
| `python test/test_gui_surface_source.py -v` | 10 PASS, 1.925 s | `host-gui_surface_source.log` |
| `python test/test_desktop_surface_runtime_source.py` | 2 Brokerchecks PASS, Exit 0 | `host-desktop_surface_runtime_source.log` |
| `python test/test_gui_browser_source.py -v` | 8 PASS, 33.904 s | `host-gui_browser_source.log` |
| `python test/test_browser_runtime_source.py -v` | 29 PASS, 9.945 s | `host-browser_runtime_source.log` |
| `python test/test_desktop_source.py -v` | 62 PASS, 1.491 s | `host-desktop_source.log` |
| `test-reist-package.ps1 -Target vmware -Video vga` | PASS, 73 s | `package-vmware.log` |
| `test-reist-package.ps1 -Target qemu -Video vga` | PASS, 60 s | `package-qemu.log` |
| `python scripts/measure_cpp_baseline.py --model-ui-baseline build/codex-agent/r320a/accepted-c` | PASS, Gast 41.635 s | `runtime-model.log`, `../accepted-c/initial-c/boot.json` |
| `test-reist-runtime.ps1 -Mode runtime-desktop-surface -Target qemu -Video vga` | PASS, ca. 29 s | `runtime-surface.log` |
| gleicher Runtime-Aufruf mit `-Mode runtime-desktop-browser` | PASS, ca. 51 s | `runtime-browser.log`, `browser.guest.log`, `browser.ppm` |
| gleicher Runtime-Aufruf mit `-Mode runtime-desktop-browser-input` | PASS, ca. 73 s | `runtime-browser-input.log`, `browser-input.guest.log`, `browser-input.ppm` |
| gleicher Runtime-Aufruf mit `-Mode runtime-desktop-browser-forms` | PASS, ca. 44 s | `runtime-browser-forms.log`, `browser-forms.guest.log`, `browser-forms.ppm` |

Die letzten vier Dauern sind Logzeitfenster, keine Latenzmessungen.
Hostpruefungen laufen ueber den bestehenden `--host-test`-Wrapper ohne
Windows-Fehlerdialoge, mit akzeptiertem Zig-Clang und Workspace-Caches;
alle 119 Faelle eingeschlossen. Referenzdetails unter `build/codex-agent/`:
`20260907-143851-package-vmware-vga.log` und
`20260907-144028-package-qemu-vga.log`. Die vier bestehenden Gaeste bestaetigen
Surface, HTML5-/CSS-Pixel, Worker-Recovery, Resize, Wheel auf/ab, echte Eingabe,
Navigation, Crash/Neustart/frische Shell sowie exaktes Formular-GET, Reflow,
Ablehnung, Reset, Fehlercontainment und Recovery. Alle QEMU-Prozesse beendet.

Angenommene C-Baseline unter `build/codex-agent/r320a/accepted-c/`:
`baseline.json` und `initial-c/paint.model.json` enthalten `passed=true`;
alle 64 Rohmessungen, Commitzeilen, Pixelreadbacks und Abschlussbelege bleiben
in `initial-c/`. `baseline.img`, `model.c`, `source.patch` und die Manifest-
Digests sind fuer die folgende eigene C/C++-Paarabnahme eingefroren.
Image-SHA256 `7d03752905700cf115807a7b7dbc4714b87a5bde2ddafe7c55ce24104eadcc42`.
Browser weiterhin 2805788 Datei-/6182953 Loaderbytes, SHA256
`774c97c00682e42c17dd43e44410184d8d922d55cb722ffa1dfeba4146e5c9f4`;
bytegleich zum zuvor gescheiterten Browserkandidaten. HTMLWORK weiterhin
845868/2752100 Bytes, SHA256
`20c4d026c264878aa70bacb9ec5f2865d9a4814968994dde80811a76cf42643d`.
Model-C-Oracle-Blob unveraendert `6b0de40d251a7c1ba70e2989cf361f3bb0a7b737`.

Das alte fehlgeschlagene `accepted-c` wurde vor der neuen Messung ohne
Ueberschreiben nach `accepted-c-before-scheduler` verschoben. Seine beiden
`passed=false`-Ergebnisse, Images und alle anderen Fehlbelege sowie saemtliche
Stashes bleiben erhalten. Keine Kernel-/SDK-/ABI-/Budgetaenderung in R3.20a;
keine C++-Modellkonvertierung oder neue JavaScript-/Webfunktion. Die Messung
gilt fuer den festen Ein-vCPU-QEMU-TCG-Prueffall, nicht als allgemeine
Website-/Zielhardware- oder WCET-Zusage. Nur R3.20 wird als Nachfolger aktiv;
seine eigenen gepaarten C/C++-Gates bleiben unveraendert erforderlich.

### Historischer Zwischenstand vor der Scheduler-Reparatur

Alle `accepted-c`-Verweise in diesem historischen Unterabschnitt beziehen
sich nun auf das erhaltene `accepted-c-before-scheduler`, nicht auf die neue
bestandene Baseline. Die damaligen Blocker sind durch obige Abnahme abgeloest.

Damals aktueller Blocker: Die finale Gastmessung scheitert nach 82.999 s trotz
vollstaendiger 32+32 Eingabe-/Commit-/Pixelbelege. Tippen-p95 171.2849 ms,
Scroll-p95 490.8986 ms, Maximum 504.9288 ms; die Grenzen 250/500 ms bleiben
unveraendert. Beide Escape-Zustaende sowie `BROWSER_CLOSE_OK` und
`TERMINAL_INPUT_IDLE` sind jetzt bestaetigt. Kein Panic. Der Verzeichnisname
`accepted-c` bedeutet keine Abnahme: `baseline.json` und `paint.model.json`
enthalten ausdruecklich `passed=false`. Belege: `runtime-model.log`,
`accepted-c/initial-c/host.log`, `paint.browser.log`, `paint.model.json`,
`boot.json` und alle PPMs. Image-SHA256
`59588c7ba1aecfc25242dee5c0059ed92ec5f4f56a3a031fa7451f21b2b24899`.
Browser 2805788 Datei-/6182953 Loaderbytes, HTMLWORK unveraendert
845868/2752100 Bytes. C-Modelblob unveraendert.

Readonly-Eingrenzung nach dem Gate: `kernel/sched/scheduling_policy.h`
definiert pro CPU und 100-ms-Fenster feste Klassenlimits von 15 ms AMBIENT,
25 ms SERVICE und 60 ms SAFETY. `find_next_runnable` filtert gedrosselte
Klassen vor der Auswahl; `scheduler_yield` gibt bei fehlendem erlaubten
Nachfolger an den Kernel zurueck. Ungenutztes Budget anderer Klassen wird
hier nicht als Restzeit vergeben. Das kann etwa 85-ms-Pausen verursachen
und passt zu den beobachteten Spruengen auch bei kleinen Arbeitsschritten;
eine genaue Zuordnung der Gastlatenz zu diesen Zaehlern ist noch nicht
bewiesen. Scheduler-Abrechnung und sichere Restzeitnutzung liegen ausserhalb
dieses Pakets. Kein Kernel-/Klassenbudget-/Testgrenzen-Eingriff ohne eigene
Freigabe. Restliche vier Gastgates nicht gestartet, QEMU beendet; R3.20a
bleibt active/blockiert, R3.20 queued. Nur Setupcommit `474b6ce9`, kein
Implementierungscommit/Push, beide Stashes und alle Fehlerbelege erhalten.

Kandidat: gleiche Browsergeometrie invalidiert nur noch den Viewport;
Surface-Backpressure gibt einem bereiten Broker einmal die CPU vor dem
bisherigen Sleep-Fallback; Desktop-Escape behaelt Nicht-CSI-Lookahead.
Keine Wire-ABI, Kernelquelle oder Kapazitaet geaendert. Drei echte C-
Regressionen scheitern vorher und bestehen nachher mit O0/O2.

Gezielte Fehlereingrenzung bleibt unter `build/codex-agent/r320a/` erhalten:
`damage-before/after.log`, `escape-before/after.log`, `handoff-before/after.log`.
`diagnostic-damage` scheitert beim Pixelreadback von Schritt 34 (71.921 s);
kleinere Damage allein reicht nicht. `diagnostic-stages` zeigt meist 1-5 ms
Komposition, aber 65-82 ms Chrome-Transaktionen, und scheitert an einer durch
zwei parallel druckende Prozesse zerteilten Commitzeile. Diese temporaeren
Desktop-/Browser-Stagenachrichten sind entfernt, der Fehlerbeleg bleibt.
Die Testschluss-Ursache Escape/Escape ist separat mit der echten Decoder-
Funktion reproduziert; keine neue Test-Escape-Verzoegerung hinzugefuegt.

Finale Hostgates bestehen (119 Faelle einschliesslich zwei Brokerchecks):

| Gate | Ergebnis / Dauer | Log unter r320a |
|---|---|---|
| `test_browser_surface_latency.py -v` | 3 PASS, O0/O2, 4.273 s | `gate-clang-browser_surface_latency.log` |
| `test_browser_model_cpp.py -v` | 5 PASS, nur Telemetrie, 0.003 s | `gate-browser_model_cpp.log` |
| `test_gui_surface_source.py -v` | 10 PASS, 2.083 s | `gate-gui_surface_source.log` |
| `test_desktop_surface_runtime_source.py` | 2 Brokerchecks PASS | `gate-broker-final.log` |
| `test_gui_browser_source.py -v` | 8 PASS, 4.946 s | `gate-gui_browser_source.log` |
| `test_browser_runtime_source.py -v` | 29 PASS, 7.802 s | `gate-clang-browser_runtime_source.log` |
| `test_desktop_source.py -v` | 62 PASS, 38.107 s | `gate-desktop-final.log` |

Die zuerst versehentlich gewaehlte GCC-Umgebung trifft alte Warnungen in
unveraenderten Renderer/Formularquellen; der akzeptierte Zig-Clang-Pfad
besteht ohne abgeschaltete Warnung. Der erste Desktoplauf scheitert am
gesperrten globalen Zig-Cache und einer Source-Assertion, die ausgerechnet
das Verschlucken des Escape-Lookahead verlangt. Workspace-Cache und die
durch O0/O2-Verhalten gedeckte neue Assertion korrigieren genau diese
Ursachen; alle anderen Assertions bleiben. Fehlerlogs bleiben erhalten.
Der reine Tippfehler `test_desktop.py` im Setup ist vor Ausfuehrung auf die
tatsaechliche komplette Suite `test_desktop_source.py` korrigiert.

Beide Referenzen PASS: VMware 79 s (`package-vmware.log`, Detail
`../20260907-134348-package-vmware-vga.log`), QEMU 67 s (`package-qemu.log`,
Detail `../20260907-134531-package-qemu-vga.log`). Die oben dokumentierte
Gastabnahme besteht nicht; kein Implementierungscommit oder Performanceclaim.

Der Nutzer erlaubt ausdruecklich das separate Reparaturpaket fuer Browser,
Shared Surface und Ring-3-Compositor. Queue: R3.20a active, R3.20 queued;
kein JavaScript-/Modellumbau in diesem Lauf. Umfang und sieben Hostgates,
beide Referenzbuilds sowie fuenf Gastgates sind vor Implementierung eingefroren.
Latenzgrenzen (p95 250 ms, jeder Schritt 500 ms), 64 echte Eingaben samt
Pixelnachweis, 180-s-Bootfrist und bestehende 30-s-Browserprobe bleiben gleich.
Keine Kernel-/SDK-/Treiber-/Wire-ABI- oder Quotenlockerung.

Zuordenbarer Messentwurf gesichert in neuem Stash
`b314cd2b7132d7a7593be09e1acf257cbefa422a`; beide alten Fehlerbelege und der
aeltere Stash bleiben erhalten. Nur Queue/Dokumentation werden als Setup
committet. Erst vom sauberen Setup aus werden die fuenf Messdateien als
Kandidatenarbeit wieder eingebunden. Keine unabgenommenen Quellen im
Setupcommit, kein Push. Neue Belege unter `build/codex-agent/r320a/`.

## R3.20 wieder aufgenommen: C-Baseline bleibt vor Modellumbau gesperrt

Ausgangscommit `475dc3c1`, Worktree vor Wiederaufnahme sauber. Die vier
zuordenbaren Probe-/Messdateien und `test/test_browser_model_cpp.py` wurden
aus Stash `5af103bac06f8a4e6b335f247ed4d89f42c4a59a` wieder eingebunden;
neuere Desktop-Testtasten bleiben erhalten, der Stash selbst unveraendert.
`browser_model.c` ist weiterhin der unveraenderte C-Oracle-Blob
`6b0de40d251a7c1ba70e2989cf361f3bb0a7b737`. Keine Modellkonvertierung,
JavaScript-Engine oder neue Webfunktion in diesem Lauf.

Belege unter `build/codex-agent/r320/resumed-20260907/`:

- Instrumentierter C-QEMU-Build PASS, `baseline-build-captured.log`;
  Kernel-SHA256 `499019d217d39cc713070a0118f8e7d677d1a3a5be79493276b5dcc91e6413be`,
  Image-SHA256 `a3b5701243908b0d9205175522aa6910266eb4f735dd50abcef1b5560e10f4f8`.
- Erster Lauf `ui-baseline/` FAIL: READY bestaetigt die Surface-Transaktion,
  nicht die bereits komponierten Fokus-Pixel. Regression und begrenztes
  Warten auf tatsaechliche Setup-Pixel hinzugefuegt (32 Readbacks, bestehende
  Gesamtfrist, keine erneute Eingabe). Die 64 gemessenen Interaktionen und
  alle absoluten/relativen Latenzgrenzen bleiben unveraendert.
- `python test/test_browser_model_cpp.py -v`: 5 Telemetrietests PASS,
  0.003 s, `telemetry-ready.log`. Noch keine C++-Modellabnahme.
- `python scripts/measure_cpp_baseline.py --model-ui-baseline
  build/codex-agent/r320/resumed-20260907/ui-baseline-ready`:
  FAIL, Gastlauf 95.099 s. Alle 32 Texteingaben und 32 Wheel-Schritte
  bestaetigt, einschliesslich passender Gastpixel. Unveraenderte Rohdaten
  ergeben Tippen-p95 139.2513 ms, Scroll-p95 455.7574 ms, Maximum
  473.6693 ms; Scroll-p95 verletzt 250 ms. Danach meldet der Browser
  `BROWSER_PROBE_FAIL interaction` statt sauberem Close. Ob dabei seine
  bestehende 30-s-Probe-Frist ablaeuft, ist noch nicht bewiesen. Kein Panic.
  `initial-c/paint.model.json`, `boot.json`, `paint.browser.log`, PPMs und
  `baseline.json` bleiben als fehlgeschlagene Belege erhalten.

Eingrenzung: Gast-Rasterisierung maximal 3 ms, Buffer-Erzeugung maximal
4 ms, Pixel-IPC maximal 172 ms, kompletter Body-Pfad maximal 257 ms.
Einige Wheel-Schritte erreichen bereits den Commit erst nach 165-281 ms;
sichtbare Komposition folgt bei diesen Schritten nochmals etwa 160-205 ms
spaeter. Einzelne QMP-Pixelreadbacks dauern etwa 13-18 ms. Der Modellumbau
allein adressiert diese gemessene Bilduebergabe-/Ausgabeverzoegerung nicht.
`publish_pixels` uebergibt pro Scroll einen neuen Vollbildbuffer ueber
mehrere synchrone Surface-Transaktionen, anschliessend folgt der separate
Base-Paint-Commit. Shared Surface-Client und Compositor wurden nur gelesen.
Die genaue Optimierung und der Close-Fehler bleiben offen.

R3.20 bleibt aktiv, aber vor Konvertierung blockiert: Renderer-Aenderungen
in `main.c` sowie Shared-Surface-/Compositor-Reparaturen liegen ausserhalb
seines eingefrorenen Umfangs. Ein eigenes freigegebenes Reparaturpaket ist
vorzuziehen; keine stille Scope-/Gate-/Fristlockerung. Keine finale
Gate-Serie, kein Commit, kein Push; Testprozesse beendet.

## R3.21a abgeschlossen: echte VMware-Aufloesungen

Die Kapazitaetskorrektur besteht alle fuenf finalen Hostgates (74 Tests),
beide Referenzbuilds und drei Gastgates, einschliesslich echtem Workstation.
Ausgangsbefund: 128 MiB VRAM gegen 3.75 MiB aktuellen FB_SIZE; QEMU meldet
beide Werte identisch und konnte diesen Fehler nicht zeigen. VRAM_SIZE ist
jetzt gegen boot-versiegelte BAR1-Grenzen validiert, FIFO gegen BAR2.
Initial werden hoechstens die bestehenden 16 MiB WC/UC abgebildet; nach
Aktivierung werden Bildumfang, Pitch und Offset erneut geprueft. Keine
ABI-/Autoritaets-/Speicherbudgetlockerung oder Aenderung an der Benutzer-VM.

Umfang/Gates vor Implementierung lokal eingefroren in `db4346ac`, saubere
Codebaseline `638b5806`. Queue jetzt R3.21a done, R3.20 active, Browserstash
`5af103bac06f8a4e6b335f247ed4d89f42c4a59a` unveraendert. Kein Push.
Benutzung: neues VMware-Image starten, Systemsteuerung -> Anzeige;
gespeicherte Aufloesung gilt weiterhin erst beim naechsten Desktopstart.

Alle neuen Belege unter `build/codex-agent/r321a/`:

| Unveraendertes Gate-Kommando (Queue) | Ergebnis / Dauer | Beleg |
|---|---|---|
| `python test/test_display_settings.py -v` | 6 PASS, 2.617 s, reale C-Pfade O0/O2 | `gate-display_settings.log` |
| `python test/test_display_abi_minimal.py -v` | 20 PASS, 0.363 s | `gate-display_abi_minimal.log` |
| `python test/test_vmware_svga2d.py -v` | 22 PASS, 0.768 s | `gate-vmware_svga2d.log` |
| `python test/test_bios_vbe_source.py -v` | 11 PASS, 0.002 s | `gate-bios_vbe_source.log` |
| `python test/test_vmware_mouse.py -v` | final 15 PASS, 0.005 s | `gate-vmware_mouse-sequence.log` |
| `test-reist-package.ps1 -Target vmware -Video vga` | PASS, 14 s | `package-vmware.log`; Detail `../20260907-115230-package-vmware-vga.log` |
| `test-reist-package.ps1 -Target qemu -Video vga` | PASS, 59 s | `package-qemu.log`; Detail `../20260907-115329-package-qemu-vga.log` |
| `run_qemu_display_settings.py --backend std` (volle Argumente Queue) | PASS, 55.356 s | `runtime-std.log`, `std/status.json`, `std/serial.log`, PPMs |
| `run_qemu_display_settings.py --backend vmware` (volle Argumente Queue) | PASS, 52.213 s | `runtime-svga2d.log`, `svga2d/status.json`, `svga2d/serial.log`, PPMs |
| `run_vmware_display_settings.ps1 -Evidence build/codex-agent/r321a/workstation -TimeoutSeconds 120` | PASS, beide Modi/Shell bis 21.283 s, Exit 0 | `workstation/gate.log`, `workstation/vmware-serial.log` |

Hostkommandos liefen ueber den bestehenden WER-unterdrueckenden Wrapper
`python scripts/measure_cpp_baseline.py --host-test <Testdatei> -v`.
Referenzen/Workstation liefen versteckt ueber PowerShell 7. Die aeusseren
Workstation-Umleitungsdateien blieben leer; der massgebliche erfolgreiche
Beleg ist das erst nach allen Pruefungen angehaengte vollstaendige Gastlog
in `workstation/gate.log` samt zwei modusbezogenen PASS-Zeilen und Exit 0.

Workstation liest fuer 1280x720 Pitch 5120 und FB_SIZE 3686400, fuer
1920x1080 Pitch 7680 und FB_SIZE 8294400 zurueck. VRAM/BAR bleiben beide
134217728, initiales Mapping 16777216. Pro Modus acht beschleunigte
Verschiebungen, acht Resize-Frames, null Probe-/Clockfehler und keine
Beschleunigungs-Fallbacks; danach deaktiviertes SVGA und frische Shellhilfe.
QEMU beweist weiterhin Applet-Speichern, unveraenderten aktiven Modus bis
Neustart, Applet-Exception/Replacement, ungueltigen Modus und Shellrueckkehr.
Keine Qualifikation physischer Monitore oder saemtlicher angebotener Modi.

Fehlerbelege bleiben erhalten: `regression-before.log` reproduziert die
falsche Kapazitaet vor Produktionsreparatur, `regression-after.log` besteht.
Der erste Workstation-Lauf (`workstation-before-sequence/`, Exit 1) erreichte
1280x720 und fehlerfreies Rendering, wartete aber bis zur 120-s-Grenze auf
`DESKTOP_ACCELERATION_READY`, das nur der automatische Startpfad ausgibt.
`workstation-sequence-before.log` reproduziert die falsche Testannahme.
Eine fokussierte Harness-Reparatur verlangt stattdessen `DESKTOP_OK`, weiterhin
die echte SVGA-Geometrie, RECT_COPY, acht Drag/Resize-Frames, null Fehler,
Deaktivierung und frische Shellantwort. Unveraenderte Produktions-/QEMU-
Nachweise wurden nicht wiederholt; finale Fehlerdiagnose-Ergaenzung am Wrapper
aendert keine Akzeptanzbedingung. Alte R3.21-Belege bleiben unveraendert.

Finales Hauptimage SHA256:
`b6edaf1033f43c8c88982b559b48732e5e62de173d96df073dc2b2b60c383ea3`.
Kernel: `499019d217d39cc713070a0118f8e7d677d1a3a5be79493276b5dcc91e6413be`.
Unveraendertes portables VMware-Quellimage:
`92b3bcae33d6990222c5f2a2852f5cf85126fed5dea6c93f2f3bca947f1397d7`.

## R3.21 abgeschlossen: Anzeige-Applet und gespeicherte Desktopaufloesung

Alle 14 eingefrorenen gezielten Gates, beide Referenzbuilds und alle vier
Gastgates bestehen. Commit-Betreff:
`feat: configure validated desktop modes through a Display applet`.
Queue: R3.21 done, R3.20 active; keine Browsermigration in diesem Lauf,
Stash `5af103bac06f8a4e6b335f247ed4d89f42c4a59a` unveraendert. Kein Push.

Benutzung: **Systemsteuerung -> Anzeige**, Modus auswaehlen und speichern.
Die Aenderung gilt erst beim naechsten Desktopstart. Alternativ
`config set desktop resolution 1280x720` oder `auto`; Datei
`/etc/reist/desktop.conf`. `display --list` ist aus der Ring-3-Shell erreichbar.
Farbtiefe bleibt 32 Bit Ablage / 24 RGB; Live-Umschaltung und Farbschemata sind
nicht Bestandteil dieser Freigabe. Gebaute VMware-Abbilder sind vorhanden;
die neuen Modi sind bisher in QEMU-VGA und emuliertem VMware-SVGA geprueft,
nicht fuer physische Monitore oder VMware Workstation hardwarequalifiziert.

Finale Belege unter `build/codex-agent/r321/`; unveraenderte Hostbelege werden
wiederverwendet, keine Wiederholung ganzer Suiten fuer jede Grafikvariante:

| Gate | Ergebnis / Dauer | Log |
|---|---|---|
| Display Host | 6 Tests PASS, 2.448 s, reale C-Pfade O0/O2 | `gate-display_settings-fault-accounting.log` |
| Desktop Host | 62 Tests PASS, 2.114 s | `gate-desktop_source-selection.log` |
| Toolchain | 23 Tests PASS, 140.379 s | `gate-user_program_toolchain-selection.log` |
| VMware-Referenz | PASS, 63 s | `package-vmware-selection.log` |
| QEMU-Referenz | PASS, 60 s | `package-qemu-selection.log` |
| Display Standard-VGA | PASS, 54.653 s | `runtime-std-final.log`, `std/status.json` |
| Display VMware-SVGA | PASS, 52.386 s | `runtime-svga2d-final.log`, `svga2d/status.json` |
| Boot 128 MiB | PASS, ca. 9.5 s Toollauf | `runtime-boot-128-final.log`, `boot-128.log` |
| Browser-Eingabe | PASS, ca. 132 s Logzeit | `runtime-browser-input-final.log`, `browser-input-final/` |

Die exakten unveraenderten Kommandos stehen im R3.21-Queueeintrag. Weitere elf
Hostgates: `gate-display_abi_minimal.log`, `gate-vmware_svga2d.log`,
`gate-reist_config_source.log`, `gate-gui_control_panel_source-gui.log`,
`gate-desktop_surface_runtime_source-gui.log`, `gate-gui_surface_source-gui.log`,
`gate-desktop_startup_source-gui.log`, `gate-shell_source-gui.log`,
`gate-memory_resilience.log`, `gate-kernel_memory_layout-layout.log`,
`gate-memory_r12-layout-final.log`. Die Surface-Runtime-Quellpruefung ist bei
Erfolg still (Exit 0); die anderen Logs enthalten das jeweilige PASS/OK.
Referenzdetails: `20260907-111504-package-vmware-vga.log` und
`20260907-111635-package-qemu-vga.log` eine Verzeichnisebene darueber.

Beide Display-Gaeste beweisen echte Tastatur-/Mausbedienung, native Listenpixel,
unveraenderten Scanout vor Neustart, gespeicherte 800x600 und 1280x720 nach
Neustart sowie einen sicheren Rueckfall von 4096x4096 auf 1024x768.
Absichtlicher Applet-Fault und neues Applet: PID 14 -> 16 (Standard),
15 -> 17 (SVGA). Jeweils frische Shellantwort nach dem Desktopende.
Die etablierte absichtliche Boot-Probe wird vollstaendig und getrennt von
genau einem spaeter bewaffneten Applet-Fault validiert; Zusatzfehler bleiben
verboten. Der 128-MiB-Gast meldet 74 MiB frei nach Boot und bestandene
Probe-Reintegration. Der Browserlauf bestaetigt Keyboard/Edit/Navigation,
Crash/Restart und `HOST_TERMINAL_FRESH_RESTART_CONSOLE_OK`.

Finaler SHA256 Kernel:
`8664294004add1c939e83623c675c03cfa4ab11d3f13608e28a37beb3e1997e3`;
Image: `1945b9269fb67675974213e4961654ad9e5952438a8a9bbe9a0bf7f0a77e1e48`.
Alle fehlgeschlagenen Laeufe und Diagnosebilder bleiben erhalten. Die folgenden
Abschnitte dokumentieren ausschliesslich die historische Entwicklung.

### Historisch: Freigegebene Auswahl-/Speicher-Handshake-Reparatur

Die erneute Freigabe ist als `selection_repair_extension` eingefroren.
Der reale Applet-Hosttest verarbeitet vom gespeicherten 800x600 aus alle drei
Down-Eingaben, rendert jede Auswahl und speichert exakt 1280x720.
Ein einzelner begrenzter Diagnosegast (35.924 s, absichtlich kein Gate-PASS)
korreliert tatsaechliche Ereignisse, Fokus und Speichersnapshot: Serien 6/7/8
sind Down, Indizes 1 -> 2 -> 3 -> 4, danach erst Save-Down/Up 9/10,
Snapshot 4 und bestaetigte 1280x720. Belege `selection-diagnosis.log` und
`selection-diagnosis/` unter `build/codex-agent/r321`. Kein nachgewiesener
Treiberverlust; die alte Abweichung ist damit nicht deterministisch reproduziert.

Ein deterministischer Test des echten Runners mit verzoegerter Anwendung der
letzten Taste reproduziert jedoch dessen fehlende Synchronisation vor Save
(`selection-runner-before.log`: FAIL). Der Runner wartet jetzt auf den exakten
ausgewaehlten Wert nach erfolgreich bestaetigtem Paint, bevor er den Fokus zum
Speicherknopf verschiebt. Weiterhin genau drei Tasten, ein Klick, eine Deadline;
bei ausbleibender Auswahl keine Wiederholung und kein Speichern. Das Applet
meldet nur im Diagnosemodus geaenderte, vollstaendig akzeptierte Auswahl-Paints;
Normalbetrieb bleibt ohne Zusatzlog. Die umfangreichere Diagnoseaufzeichnung
wurde nach Auswertung wieder entfernt, ihre Belege bleiben erhalten.

Das erweiterte Display-Gate besteht mit fuenf Tests in 2.753 s, einschliesslich
echtem Applet O0/O2, fehlgeschlagenem Paint ohne falsche Meldung, normalem
Silent-Betrieb und verzoegertem/fehlendem Auswahl-ACK. Betroffene weitere Gates
und finale Referenzen/Gastgates sind in Arbeit; noch keine Abnahme/kein Commit.
Der letzte fehlgeschlagene std-Lauf liegt unveraendert unter
`std-before-selection-repair/`. Alle alten Befunde folgen historisch.

Erster Gastlauf dieses erneuten Auswahlkandidaten: alle GUI-Schritte bestanden,
aber 64.710 s Gesamt-FAIL wegen falscher abschliessender Exceptionzaehlung.
Die zwei echten Invalid-Opcode-Berichte stammen aus der vorhandenen absichtlichen
Boot-Probe und dem spaeter ausdruecklich ausgeloesten Display-Applet-Fault.
Die Probe ist vor `BOOT_OK` vollstaendig reintegriert. Ein fokussierter
In-Scope-Gatereparaturschritt (`selection_gate_repair`) prueft jetzt beide
Phasen strikt getrennt und geordnet, mit genau einem Fault je Phase und allen
Boot-Recovery-Markern. Zusatzausnahmen, Pagefaults, falsche Exceptionarten,
fehlende/ungeordnete Bootmarker und fehlende Applet-Arming-Meldung bleiben FAIL.
Sechs Display-Tests PASS in 2.448 s, vorherige Entwicklung
`fault-accounting-before.log` FAIL. Unveraenderte Fehlerbelege unter
`std-before-fault-accounting/`, kein nachtraeglich umgeschriebenes Gate-PASS.
Es wurde ausschliesslich Runner/Test/Dokumentation repariert, kein Gastcode;
die finalen Referenzbuilds (VMware 63 s, QEMU 60 s) bleiben identisch.
Der eine reparierte Standard-Gastlauf wird neu ausgefuehrt, danach die drei
offenen Gastgates. Bei weiterem Fehler gilt wieder die Stoppbedingung.

### Historisch: GUI-Uebergang repariert; Auswahlabweichung blockiert Abnahme

Erneute ausdrueckliche Freigabe des Nutzers fuer einen begrenzten Lauf ist in
`gui_repair_extension` eingefroren. Die Reihenfolge im Compositor ist repariert:
eine gelesene Taste wird vor dem folgenden Mausbatch zugestellt. Der vorher
fehlschlagende Ordnungsregressionstest und echte Menue-Verhaltenstests O0/O2
bestaetigen Escape/Start-Semantik; 62 Desktop-Tests bestehen in 1.257 s.
Der Runner wartet nun auch auf das tatsaechliche PID-bezogene Retirement der
Systemsteuerung, bevor er Start klickt. Keine feste Zusatzpause, kein neues
Timeout, keine ABI-/Speicher-/Recovery-Aenderung.

Betroffene Startup-/Surface-/Control-/Shellgates bestanden, ebenso das ganze
Toolchaingate mit 23 Tests in 142.482 s (`gate-*-gui.log`). Die finalen
Referenzbuilds bestehen: VMware 18 s (`20260907-105117-package-vmware-vga.log`),
QEMU 60 s (`20260907-105202-package-qemu-vga.log`). Der Standardgast beweist
jetzt beide Fenster-Retirements, Startmenue, `DESKTOP_EXIT_OK`, frische
Shellantwort und den Desktop-Neustart mit den gespeicherten 800x600.

Der Gesamtgatelauf scheitert nach 180.034 s am naechsten Speicherschritt:
`DISPLAY_SETTINGS_SAVED 1152x864` statt angefordertem `1280x720`. Der Runner
sendet drei Down-Ereignisse vom bestaetigten 800x600-Eintrag, beobachtet wird
jedoch der nur zwei Eintraege spaetere Wert. Keine Ursache im Eingabetreiber,
Desktop-Dispatch oder Runner allein ist damit bewiesen. Eine QMP-Bestaetigung
belegt Einspeisung, nicht Verarbeitung durch das Applet. Naechste begrenzte
Diagnose muss Eingabe, ausgewaehlten Modellindex und Speichersnapshot korrelieren;
kein laengerer Sleep und keine Wahl eines falschen Erwartungswertes.

Belege unter `build/codex-agent/r321/`: `runtime-std-gui.log`, `std/status.json`,
`std/serial.log`, `std/active-800.ppm`. Vorherige std-Belege unveraendert nach
`std-before-gui-repair` verschoben. Browser-Gastgate wurde nicht ausgefuehrt;
die vier potentiell ueberschriebenen alten Bild-/Logdateien sind vorsorglich
unter `browser-input-before-gui` kopiert. QEMU ist beendet. Gemaess erneuter
begrenzter Freigabe ist die Stoppbedingung erreicht: keine weitere
Kandidatenreparatur/Gate-Wiederholung ohne Richtung, kein Commit/Push.
R3.21 bleibt aktiv, R3.20 queued; Browserstash unveraendert.
SVGA2D-, 128-MiB-Boot- und Browser-Input-Gate bleiben offen, ebenso der noch
nicht erreichte absichtliche Applet-Fault-/Reintegrationsnachweis.

Aktueller SHA256 Kernel:
`8664294004add1c939e83623c675c03cfa4ab11d3f13608e28a37beb3e1997e3`;
Image: `7b698602f2219c913681258e83d277383ac804fb3cc4a2073cc3efee713f2389`.
Konsistente Referenzartefakte, aber keine vollstaendige GUI-Abnahme.

### Aufbewahrter Fehlerstand vor erneuter Freigabe

Vorheriger Stopp: Auch der eine reparierte Standard-VGA-Gatelauf scheitert
nach 180.022 s, diesmal am exakten Zustand
`DISPLAY_PROBE_MENU_READY x=(\d+) y=(\d+)`. Belege:
`build/codex-agent/r321/runtime-std-exit-repair.log` sowie
`std-before-gui-repair/status.json` und `std-before-gui-repair/serial.log`.
Bestaetigt sind Boot, DISPLAY-Shellauflösung, Start des
Applets aus der Systemsteuerung, native Listenpixel, Speichern 800x600 bei
unveraendertem 1024x768-Scanout, Applet-Close und Surface-Retirement von PID 10.
Keine Kernelpanic/Exception gemeldet; kein nachgewiesener Modusneustart,
1280x720, Fault-/Reintegrationstest oder endgueltiger Shell-Rueckweg.

Die finalen Referenzbuilds nach Diagnostikreparatur bestehen: VMware 67 s
(`package-vmware-exit.log`, Detail `20260907-103206-package-vmware-vga.log`),
QEMU 58 s (`package-qemu-exit.log`, Detail `20260907-103351-package-qemu-vga.log`).
Das aktuelle Image ist kein Mischimage mehr, aber **nicht GUI-abgenommen**.
SHA256 Kernel: `8664294004add1c939e83623c675c03cfa4ab11d3f13608e28a37beb3e1997e3`;
Image: `f6056115447d4a6bf6dfd3bd140b1246b8428e82ad0e213ad78782b801efc30c`.

Gemaess eingefrorener Stoppbedingung keine zweite Kandidatenreparatur oder
Gate-Wiederholung ohne erneute Freigabe. QEMU-Prozess ist beendet. SVGA2D-Gate,
128-MiB-Boot und bestehendes Browser-Input-Gate bleiben offen. Alle gezielten
Layout-/PMM-/Toolchain- und betroffenen GUI-Hostgates bestehen wie unten belegt.
Queue unveraendert: R3.21 aktiv, R3.20 queued. Kein Commit, kein Push.

Naechste begrenzte Diagnose: Der Runner sendet nach Applet-Retirement noch
ESC an die Systemsteuerung, wartet aber nicht auf deren Retirement, bevor er
Start klickt. Im Desktop wird ein zuvor gelesenes Tastaturereignis erst nach
dem Mausbatch an die UI zugestellt. Dass diese Reihenfolge ESC auf ein gerade
geoeffnetes Menue statt auf das alte Fenster wirken laesst, ist eine konkrete
Hypothese, noch kein Gastnachweis. Native Menue-/Capture-/Fokuszustand und
Control-Panel-Retirement gezielt erfassen; keine weitere blinde Pause und kein
Wiederholen bis zu einem zufaelligen PASS. Die separate Diagnose mit Bildern
hatte einen Exit gezeigt, ersetzt aber diese fehlende Abnahme nicht.

### Umgesetzte und gepruefte Speicherlayout-Erweiterung

Der Nutzer hat die Linker-/Boot-Speicherlayout-Erweiterung samt Schutztests
ausdruecklich freigegeben. Der gesamte vorhandene Kandidat bleibt eindeutig
zuordenbar; keine fremden Aenderungen, keine Stash-Wiederherstellung.
Inventar: Beide nativen ELF-Loader akzeptieren bereits [1 MiB, 64 MiB),
der PMM reserviert bis hinter den gelinkten Stack und seine Bitmaps/Initialheap.
Paging nutzt supervisor-only Abbildungen unter unveraenderten 1 GiB USER_BASE.
Es ist keine Erweiterung von Bootadmission, Paging oder Allocator notwendig.

`config/klink.ld` bildet jetzt genau dieses bestehende 63-MiB-Fenster ab.
Die unbenutzte eingebettete `.user_*`-Region bei 33 MiB wird nicht verschoben,
sondern einschliesslich Untersektionen explizit abgewiesen. `_kernel_end` bleibt
das tatsaechliche `_stack_end`; 4-KiB-Guard und 8-KiB-Stack werden per Assertion
geschuetzt. Kein pauschales Reservieren des ganzen Linkerfensters, keine
dynamischen Grafikpuffer, kein kleineres Grafikbudget, keine ABI-Aenderung.

Vor Reparatur reproduzierten reale ELF-Links den Ueberlauf. Das neue Gate
`python test/test_kernel_memory_layout.py -v` besteht danach mit vier Tests
in 4.095 s, einschliesslich echter PMM-Verhaltenstests O0/O2 fuer 128/1024 MiB:
geschuetzte Frames nicht freigebbar, Firmwareausschluss, exakte Rueckgewinnung,
unveraenderte 1/16-Recoveryreserve und Ablehnung vor Bitmap-Schreibzugriff bei
reserviertem Metadatenbereich. Linktest prueft beide realen 16-MiB-Puffer,
Nichtueberlappung, NOBITS-Dateigroesse, Guard-/Stacksymbole, Linkerueberlauf und
abgewiesene eingebettete Usersektionen. Beleg `gate-kernel_memory_layout-layout.log`
unter `build/codex-agent/r321/`; negative Entwicklung `layout-before*.log`.

Das zusaetzliche eingefrorene R1.2-Speichergate enthielt eine alte Annahme
`argc * SYSCALL_ARGUMENT_CAPACITY`. Im Rahmen der freigegebenen Schutztests
erwartet es jetzt den schon bestehenden festen `PROCESS_ARGUMENT_TOTAL_BYTES`-
Heap mit abgezogenem argv-/Alignmentbudget und Einzelargumentgrenze. Alle
vorhandenen Cleanup-/Stack-/ABI-Assertions bleiben bestehen; kein Spawncode
geaendert. Ein fokussierter Reparaturlauf: 28 Tests PASS, 0.015 s,
`gate-memory_r12-layout-final.log`; initialer Fehler bleibt erhalten.

Der erweiterte Toolchain-Lauf besteht mit 23 Tests in 137.371 s
(`gate-user_program_toolchain-layout-final.log`). Erste echte Referenzbuilds
nach Layoutreparatur bestanden: VMware 20 s, QEMU 65 s
(`package-vmware-layout.log`, `package-qemu-layout.log`), jeweils frisch
gelinkter Kernel, Manifest-/Signatur- und SBOM-Validierung.

Der erste Standard-VGA-Gast beweist Boot, Shell-DISPLAY-Aufloesung,
Control-Panel-Start, native Listenpixel, Speichern 800x600 ohne Live-Wechsel
und Applet-Schliessen. Er endet nach 180.027 s ohne bestaetigten Desktop-Exit;
Belege unveraendert verschoben nach `std-before-exit-repair/`, keine Abnahme.
Ein separater 60-s-Diagnoseboot (22.389 s, absichtlich kein PASS) zeigt mit
einzelnen Zustandsaufnahmen die funktionierende Exitaktion und fehlenden
Folgeprompt: GUI-Programme starten in der Shell im Hintergrund. Die starre
Klickfolge/Promptannahme des neuen Runners war nicht verlaesslich.

Ein fokussierter in-scope Reparaturschritt ersetzt diese Annahmen: Nur der
vorhandene `--control-probe` meldet jetzt tatsaechliche Applet-Surface-Retirement,
native Start-/Exit-Rechteckzentren und freigegebene Menue-Capture. Der Runner
wartet auf diese Zustaende (auch nach dem absichtlichen Applet-Fault), klickt
erst danach und verlangt nach `DESKTOP_EXIT_OK` eine frische `help`-Antwort.
Keine pauschale Fristerhoehung, keine weiteren Klickversuche, kein alternativer
Exit-/Kill-Pfad. Betroffene Desktop-/Startup-/Shell-Gates erneut PASS mit
59/2/32 Tests in 1.366/0.640/0.828 s (`gate-*-exit.log`). Beide Referenzbuilds
werden fuer diesen endgueltigen Diagnostikkandidaten erneut ausgefuehrt,
danach der eine reparierte Standardlauf und die drei offenen Gastgates.
Keine Abnahme oder Commitbehauptung vor ihrem tatsaechlichen Bestehen.

### Historischer Referenzbuild-Stopp (durch obige Freigabe fortgesetzt)

Die ausdruecklich freigegebene Toolchain-Fortsetzung ist umgesetzt. Ein
begrenzter Diagnosebuild mass 74.285 s, davon 50.773 s im bislang seriell
vorgeschalteten HTML/CSS-Teil. Der SDK-Builder fuehrt nun die unabhaengigen
HTML- und Runtime-Gruppen parallel aus (bestehende vier Objektarbeiter je
Gruppe, maximal acht insgesamt). Beide Gruppen werden auch bei Fehlern
vollstaendig gejoint; keine Hintergrundschreiber nach Rueckkehr. Der
erfolglose globale Cache-Versuch ist entfernt. DISPLAY.PRG ist auch in der
inkrementellen erwarteten GUI-Abhaengigkeitsmenge enthalten. Keine Frist,
Laufzeitquote oder alte Assertion wurde abgeschwaecht.

`python test/test_user_program_toolchain.py -v`: 22 Tests PASS, 156.711 s,
`build/codex-agent/r321/gate-user_program_toolchain-resumed.log`.
`python test/test_memory_resilience.py -v`: vier Tests PASS, 2.020 s,
`build/codex-agent/r321/gate-memory_resilience.log`. Die zuvor bestandenen
zehn gezielten Gates bleiben als unveraenderte Belege erhalten.

Der anschliessende VMware-Referenzbuild ist **nicht bestanden**, trotz
irrefuehrendem Wrappertext `PACKAGE PASS elapsed=68s`. Vollstaendiger Beleg:
`build/codex-agent/20260907-095447-package-vmware-vga.log`. Der Linker meldet:

```
section '.bss' will not fit in region 'kernel_ram': overflowed by 18211020 bytes
section '.stack' will not fit in region 'kernel_ram': overflowed by 18227200 bytes
```

Die beiden 16-MiB-BSS-Grafikpuffer passen nicht in den durch
`config/klink.ld` festgelegten 32-MiB-Kernelbereich. Diese Quelldatei liegt
ausserhalb des freigegebenen Pakets. Kein stilles Hochsetzen der Grenze,
Verschieben von Boot-/Userregionen, Wechsel auf unbewiesene dynamische
Grafikallokation oder Reduzieren des Modusbudgets allein fuer einen gruenen
Link. Vor Fortsetzung ist eine explizit eingefrorene Speicherlayout-Erweiterung
mit Reservierungs-/Isolations- und 128-MiB-Bootnachweis erforderlich.

Zusaetzlich reproduziert: Der lokale `$LASTEXITCODE=0` des aufrufenden
Paketwrappers verdeckt im Buildskript den globalen nativen Fehlercode. Der
Kernel-Build prueft jetzt unmittelbar `$?` und benutzt den globalen Code nur
fuer die Fehlermeldung; bei Fehlern wird kein alter Kernel weiterverpackt.
Der neue Verhaltenstest fuehrt den echten Pruefblock mit nativen Exitcodes
2/0 und geerbten Codes 0/7 aus. Vorher beide Faelle FAIL, danach PASS in
0.823 s; `native-exit-before.log` / `native-exit-after.log` unter
`build/codex-agent/r321/`. Das ist eine gezielte Entwicklungsregression,
kein wiederholtes Paketgate oder vollstaendiger neuer Toolchain-Gatelauf.

**Das erzeugte Hauptimage ist ein nicht abgenommenes Mischimage aus altem
Kernel und neuen PRGs; nicht als funktionierende Anzeigeversion starten.**
Es und die Belege bleiben erhalten, keine unaufgeforderte Wiederherstellung.
SHA256 Kernel: `5fc6982b09df641473aadce0cff3f4963a7795cf36a4baafe81bb71bad7a2219`;
Image: `9a446109cdef9a96d7540931beb82777cfa241bfe5fa6c206f7ccf9ac5ec931a`.
Kein QEMU-Referenzbuild und keines der vier Gastgates ausgefuehrt. Nach
Speicherreparatur muessen beide Referenzbuilds und alle Laufzeitgates den
endgueltigen Kandidaten pruefen; die erweiterte Toolchain-Pruefung ebenfalls.
R3.21 bleibt aktiv/uncommittet, R3.20 queued, Stashes unveraendert. Kein Push.

### Historischer Toolchain-Stopp (durch obige Freigabe fortgesetzt)

Implementierung liegt uncommittet im sichtbaren Hauptworktree auf
`38c1556c` (Paketdefinition auf akzeptiertem `732b2930`). DISPLAY.PRG,
Konfigurationsparser, Startmodusaufnahme, vorhandene Display-/SVGA-Mediation
und Control-Panel-Aufruf sind implementiert; noch keine Gast-/Hardwareabnahme.
Keine Browser-Stash-Wiederherstellung, keine Agenten oder sichtbaren Hostfenster.

Zehn der zwoelf gezielten Gates bestanden. Der neue reale Verhaltenstest
`python test/test_display_settings.py -v` besteht mit vier Tests jeweils bei
O0/O2: native Applet-Eingaben/asynchroner CONFIG-Besitz, Modusprogrammierung
mit fehlerhaftem Readback/Mapping/Disable, Ring-3-Treiber/Broker/Syscall-Grenzen
und Setting-/Kapazitaetspruefung. Alte Desktop-Quellannahmen (Startwrapper und
sieben statt acht Surface-Programme) wurden einmal gezielt repariert; das
Desktop-Gate besteht danach mit 59 Tests. Alle Belege unter
`build/codex-agent/r321/gate-*.log`; Entwicklungsregressionen sind separat
`build/codex-agent/r321-*-development*.log` und zaehlen nicht als Abnahme.

Stop: `python test/test_user_program_toolchain.py -v` scheitert nach einem
fokussierten Reparaturversuch erneut. Erster Lauf: 149.313 s,
`gate-user_program_toolchain.log`; nach Reparatur: 160.908 s,
`gate-user_program_toolchain-repair.log`. DISPLAY.PRG wird nach Einbindung der
vorhandenen libc erfolgreich mitgebaut. Offen bleiben:

- Der frische installierte GUI-SDK-Build ueberschreitet unveraendert seine
  60-s-Grenze. Der versuchte persistente globale Compiler-Cache im SDK-Builder
  hat den Fehler nicht geschlossen; keine erfolgreiche Performancebehauptung.
- Der inkrementelle GUI-Neubau liefert korrekt auch DISPLAY.PRG; die zweite
  erwartete Programmenge in `test/test_user_program_toolchain.py` (um Zeile 666)
  wurde noch nicht aktualisiert. Diesen nachfolgenden Fehler nicht kaschieren.

Zum damaligen Stopp gemaess eingefrorener Stoppbedingung keine Kandidatenreparatur oder
Gate-Wiederholung ohne neue Freigabe. Memory-Resilience-Gate, beide
Referenzpakete und alle vier Laufzeitgates sind noch offen. Der neue
`run_qemu_display_settings.py` ist geschrieben, aber nicht ausgefuehrt;
insbesondere Aufloesung, Bildpixel, Persistenz und echte Applet-Exception sind
noch nicht im Gast nachgewiesen. Das vorhandene Hauptabbild wurde durch diese
Arbeit nicht neu gebaut. Keine Queue-Transition, kein Implementierungscommit,
kein Push. R3.21 bleibt aktiv, R3.20 queued; der gesicherte Browserentwurf bleibt
unveraendert. Naechster notwendiger Auftrag: begrenzte Toolchain-Reparatur und
Abnahmefortsetzung mit unveraenderten Fristen/Assertions ausser der expliziten
DISPLAY-Erweiterung der GUI-Abhaengigkeitsmenge freigeben.

### Freigegebener Umfang

Nach Nutzerbestaetigung sauberer Start auf `732b2930`. Das neue Paket friert
Modusaufnahme, vorhandene Display-/SVGA-Mediation, Konfiguration und eigenes
Surface-Applet als zusammenhaengenden Startmodus-Verbraucher ein. Kein
Live-Wechsel, Farbschema, weiterer Pixelformatpfad oder Browserumbau. R3.20
wieder queued, sein Entwurf und saemtliche Belege unveraendert gesichert.
Vertrag: `docs/architecture/DISPLAY_SETTINGS_CONTRACT.md`; genaue Dateien und
Gates in der Queue vor Implementierung festgelegt. Noch keine Abnahme.

## R1.2e abgenommen: Boot-/Probe-Starttransaktion

Die folgende Startreparatur ist mit allen fuenf gezielten Hostgates (zusammen
52 Testfaelle, einschliesslich gezielt reparierter Altannahmen), beiden
Referenzbuilds und allen drei Gastgates abgenommen. Genau dieses Paket ist
abgeschlossen; keine Paging-/ABI-/Frist-/Quotenlockerung und kein Browserumbau.
Die unten beschriebenen Zwischenfehler und Belege bleiben sichtbar.

Die vorgeschriebene Queue-Transition aktiviert formal den bisherigen
Nachfolger R3.20. Dessen Implementierung und Stash-Wiederherstellung starten
hier nicht: Vor weiterer Funktionsarbeit ist gemaess neuer Nutzerprioritaet
zuerst das Anzeige-Paket nach `DESKTOP_DISPLAY_SETTINGS_PLAN.md` einzufrieren
und vorzuziehen. Das Anzeige-Applet ist nicht implementiert.

Der Nutzer hat das vorgeschaltete Reparaturpaket ausdruecklich freigegeben.
Der vollstaendig zuordenbare, nicht abgenommene R3.20-Probeentwurf samt
ungetracktem Hosttest ist in lokalem Stash
`5af103bac06f8a4e6b335f247ed4d89f42c4a59a` gesichert. Ignorierte Evidenz unter
`build/codex-agent/r320/` bleibt unangetastet. Danach Worktree sauber auf
`4132d88f`; kein Browserumbau und keine Browser-Abnahme in diesem Paket.

Der C-Baselinelauf bestaetigte 32 Texteingaben und 32 Scrollereignisse, blieb
mit Scroll-p95 450,5252 ms ueber 250 ms (Tippen 134,8426 ms). Ein spaeterer
Boot desselben gesicherten Images scheiterte vor dem Browser mit
`Unable to start REIST Ring-3 probe`; letzter Kontext `validate image`
beweist keine Dateikorruption. Beide Imagehashes wurden gleich verifiziert:
`651346437c0011c11a7be68d8a8d7c3a1e8dd03bacaef2a37e1aedbc889d12ef`.

Inventar: Der Probe-Spawn macht das Kind bereits runnable und veroeffentlicht
erst anschliessend PID/Generation. Dessen sofortiger Selbsttest wird bei
fehlender Identitaet abgewiesen; sein Exit kann schon die nachfolgende
Identitaetsabfrage scheitern lassen. Treiber, Audio und Compositor benutzen
bereits den vorhandenen PREPARED-Start. R1.2e uebernimmt genau diese
Publikationsreihenfolge fuer Boot-, automatische und manuelle Probe-Recovery,
mit deterministischem Regressionstest vor der Reparatur. Keine neue
Schedulerfunktion, Wartezeit, Wiederholung oder Lockerung von Schutzbudgets.
Paket und Abnahmebefehle sind vor Implementierung in der Queue eingefroren.

Paketdefinition lokal committet als `b3b6f52b`; danach sauberer
Implementierungsstart. Der neue reale Hosttest extrahiert die vollstaendige
Spawnfunktion, die Ring-3-Startupfunktion sowie unveraenderte zusammenhaengende
Startup-Reportfaelle. Er erzwingt Kind-Ausfuehrung vor Rueckkehr des Starters.
Nach Korrektur einer uint64_t-Teststub-Signatur scheitert der alte Code
deterministisch an `assert(probe_spawn_next())`: der fruehe Selbsttest wird
abgewiesen und das Kind beendet sich vor der Identitaetsaufnahme. Beide
Fehllogs bleiben unter `build/codex-agent/r12e/regression-before*.log`.

Der Kandidat benutzt PREPARED, veroeffentlicht dann PID/Generation und den
gefenceten Kontrollzustand und gibt erst danach die Ausfuehrung frei. Kein
Stale-Write oder Liveness-Recheck nach erfolgreichem Start. Fehlgeschlagener
Start restauriert vor Beenden den vorherigen nicht laufenden Kontrollzustand,
einschliesslich Launch-Historie. Gescheiterte Ruecknahme/Bereinigung fencet
Ausgaben; keine Wiederholung. Laufende/ungefencete Altgeneration und
Zaehlerueberlauf werden vor dem Spawn abgewiesen. Kein Scheduler-, Prozess-,
ABI-, Frist-, Quoten- oder Browserwechsel.

Targeted: Probe O0/O2 PASS (1 Test, 1,015 s), sofortige/verzoegerte Reports,
Sofort-Exit, vier Modi/Generationen, alte Identitaeten, sieben Ablehnungsstellen,
Rollback-/Bereinigungsfehler und Aufnahmegrenzen. Supervisor 15 Tests PASS
(2,263 s), Argumentpfad 1 Test PASS (0,549 s). Boot-Readiness: 3/4 PASS;
alter Test erwartete Version 2, der unveraenderte Header hat seit `9a4bd883`
Version 3 mit geschuetzter Post-ready-Affinitaet. Exakt diese Testannahme
korrigiert und additiv das bestehende Feld geprueft; betroffener Test PASS
(0,002 s). Keine Produktionsversion geaendert.

Das SMP-Gate erreicht 30/31 PASS (2,351 s). Sein alter Pattern-Test erwartet
`page_table_lock_acquire_irq()` unmittelbar in `free_page_directory`.
Seit `8eb525d0` delegiert dieser begrenzte Wrapper jedoch an
`free_page_directory_step`; dort umschliesst dieselbe Sperre weiterhin alle
Mutationen. Kein durch diesen Kandidaten veraenderter Pagingpfad. Erforderlich
ist eine eng begrenzte Testkorrektur, die Delegation UND Erwerb/Freigabe im
Schritt prueft, statt eine globale Sperre um den ganzen Wrapper einzubauen.
`test/test_smp.py` ist nicht im eingefrorenen erlaubten Dateiumfang;
deshalb nicht veraendert, keine Gate-Abschwaechung und keine blinde Wiederholung.
Freigabe fuer diese einzelne Testdatei ist angefragt. Referenz-/Gastgates
stehen noch aus; Kandidat nicht committet, R1.2e weiterhin aktiv. Alle Logs
liegen unter `build/codex-agent/r12e/gate-*.log`.

Fortsetzung am 7. September: Nutzerfreigabe fuer genau `test/test_smp.py`
liegt jetzt vor. Paket-/Planungscommit `3b905422` friert diese Erweiterung ein
und haelt das Anzeige-Applet als naechste Funktionsprioritaet fest; vorhandene
Kandidatenquellen blieben dabei uncommittet und unveraendert. Der SMP-Test
prueft nun die Wrapper-Delegation und die bestehende Sperre im 64er-Schritt,
einschliesslich Reihenfolge vor/nach allen Freigabe-/PTE-Mutationen. Die
uebrigen SMP-Pruefungen bleiben erhalten; Pagingcode unveraendert.
Repariertes SMP-Gate: 31 Tests PASS (1,852 s),
`build/codex-agent/r12e/gate-smp-repair.log`. Unveraenderte bereits bestandene
Hostgates wurden nicht wiederholt.

Finale Referenz-/Gastabnahme, jeweils ein Lauf ohne Wiederholung:

- `test-reist-package.ps1 -Target vmware -Video vga`: Exit 0,
  vollstaendige Pflichtartefakte; Buildlog
  `build/codex-agent/20260907-075738-package-vmware-vga.log` (ca. 18 s).
  Der versteckte Kindprozess lieferte keinen stdout-Text an die aeussere
  Umleitung; das innere Buildlog und die Artefakte sind vorhanden.
- `test-reist-package.ps1 -Target qemu -Video vga`: PASS, 61 s;
  `build/codex-agent/r12e/gate-package-qemu.log` und
  `build/codex-agent/20260907-075850-package-qemu-vga.log`.
- Beide eingefrorenen `run_qemu_smoke.py --boot-only --expect-reist-probe`
  Aufrufe mit einer/vier CPUs: PASS (ca. 10 s / 9,984 s),
  `build/codex-agent/r12e/boot-1.log`, `boot-4.log` und `gate-boot-*.log`.
  Beide zeigen Crash-/Hang-/Invalid-Reply-Recovery und Reintegration;
  SMP zusaetzlich `SCHEDULER_READY cpus=4 probe_mask=0000000E`.
- `test-reist-runtime.ps1 -Mode runtime-desktop-browser-input -Target qemu
  -Video vga`: PASS, 139,316 s unter der unveraenderten 180-s-Gastfrist;
  `build/codex-agent/r12e/gate-browser-input.log`. Zwei echte Eingabe-/
  Navigationssitzungen, absichtlicher Ring-3-Terminalbesitzer-Absturz,
  funktionierende Konsole danach sowie frische Sitzung und erneuter
  Konsolenrueckweg. Keine Behauptung einer R3.20-Latenzabnahme.

Die finalen Screenshot-/Gastlogs liegen ebenfalls in
`build/codex-agent/r12e/`; zuvor belegte gleichnamige Standard-Ausgaben sind
unter `prior-desktop-evidence/` kopiert, nicht geloescht. Alle Testprozesse
beendet, keine sichtbaren Windows-/VMware-Testfenster gestartet.
QEMU-Image SHA256:
`01e82ac54230f95763ad9c399bca74dc1d3c8305344739af9e3f5122c20f7e34`;
Kernel SHA256:
`2f1d5d134ec00b43473b3fdb286d6f685a07c1048de6412764d8f18c4d2c2d85`.
Direkte Diff-/Scopepruefung bestaetigt ausschliesslich freigegebene Dateien;
urspruengliche Gates, Stopbedingungen und Invarianten unveraendert.

## R3.19 abgenommen: Ressourcen-C++, SDK- und Ladewartekorrektur

Sauberer Start auf `864f869a`, direkt im sichtbaren Hauptworktree. Genau
R3.19 ist umgesetzt und abgenommen; kein Kernel-/ABI-/Frist-/Quotenwechsel.
R3.20-model ist als Nachfolger definiert und aktiv, aber nicht implementiert.
Seine eingefrorenen Eingabe-/Scrollmessungen muessen vor Modellumbau am
noch unveraenderten C-Code erhoben werden; VMware bleibt zurueckgestellt.
Der einzelne zuvor nicht reproduzierte Reflow-
Timingabbruch bleibt als Risiko im R3.18-Verlauf sichtbar.

`browser_resources.cpp` ist die einzige Ressourcenimplementierung fuer
Browser und HTMLWORK. `ValidatedResources::open` erstellt nach der bisherigen
vollstaendigen Validierung eine kleine geliehene Ansicht oder einen Fehler.
Pack verwendet nur deren geprueften Snapshot; Unpack wird vor erfolgreicher
Rueckgabe ueber dieselbe C-Admission validiert. Die elf C-Symbole, Layouts,
Rueckgabecodes, aktuelle Version 3 und Legacy-v2-Decoder bleiben unveraendert.
Kein Bundle im Result oder auf dem Stack, keine neue Heapallokation oder
Nutzdatenkopie. Der Besitzer muss den gesamten geliehenen Speicher lebend und
unveraendert halten; Reset/Mutation/Navigation/Freigabe invalidieren Ansichten.
Eine Generation prueft keine Zeigerlebensdauer. Null neue externe Besitzer,
Destruktoren oder Cleanup-Pfade; kein erfundenes Ressourcen-RAII.

Buildadapter aktivieren das bestehende Profil nun auch fuer HTMLWORK und
verfolgen die private hpp-Abhaengigkeit in beiden Programmen. C-Aufrufer und
alle Fremdbibliotheken bleiben C. Der CSS-Hostadapter uebersetzt nur diese
eine TU separat als C++, bei gleicher Symbolumleitung und insgesamt weiter
120 Sekunden fuer seinen bisherigen Compile-/Linkschritt. Keine Gate-Assertion
oder Zeitgrenze entfernt.

Regression zuerst angelegt: fehlender neuer Header abgewiesen.
Gezielter Vorlauf: je 4278 C/C++-Vergleiche in O0/O2 bestehen; im i386-
Stackfixture fehlte zunaechst der vorhandene libc-Includepfad fuer string.h.
Nur dieser Test-Includepfad wurde ergaenzt. Logs `r319/regression-before.log`
und `r319/focused.log` unter `build/codex-agent/` bleiben erhalten.

Neues eingefrorenes Ressourcen-Gate PASS: 5 Tests/6,547 s, darunter
je 4278 echte Vergleichsfaelle, private Konstruktion, temporaere Borrows,
Generationen, falsche Offsets/Quoten, Teil-Ausgaben bei Fehler, aktuelle und
alte Wireversion, voller Pool und 4096 deterministische URL-Mutationen.
Gepaarte Validierung: C 387,239 ns, C++ 385,902 ns (99,655 Prozent), unter
120 Prozent und 50000 ns. Elf i386-Einstiegspunkte: maximal +12 Byte eigener
Stack, auch mit konservativer Differenz an gemeinsamen externen Callees
innerhalb +256 Byte. Kein unbekannter/indirekter/rekursiver Aufruf und keine
neue Runtime-/Allocatorabhaengigkeit. Messung, Source-/Fixturehashes,
Compilerprofile und Disassemblies: `r319/resources/`.

Im ersten Lauf bestanden zwoelf der dreizehn Hostgruppen (68 Tests), darunter
die echten HTML-/CSS-Worker-Fixtures sowie Formular-, Navigations- und
Laufzeit-Hosttests. Die Toolchaingruppe scheiterte mit einem Fehler und einer
fehlgeschlagenen Assertion (21 Tests, 161,090 s). Ihr externer SDK-Neubau
ueberschreitet 60 Sekunden; die Ursache dieses separaten Zeitfehlers ist noch
nicht bewiesen. Beim Systemprogrammbau weist die nun auch fuer HTMLWORK
aktive C++-Admission `.eh_frame` ab. Fehlerlog bleibt erhalten:
`r319/gate-test_user_program_toolchain.log`. Keine Zeitgrenze oder Pruefung
geweitet und kein blinder Wiederholungslauf.

Read-only-Pruefung der installierten sechs HTMLWORK-Abhaengigkeiten grenzt
die verbotene Sektion auf `libclang_rt.builtins-i386.a`, Mitglied `builtins.o`,
ein; libhubbub, libcss, libwapcaplet, libparserutils und libreistc bestehen.
Der gemeinsame SDK-Builder erzeugt die Zig-Compilerhilfen bisher ohne
`-fno-unwind-tables`. Ein separater diagnostischer Neubau mit genau diesem
zusaetzlichen Toolchain-Schalter besteht die unveraenderte vollstaendige
Objekt-Admission (1,792 s; `r319/builtins-no-unwind-diagnostic.log`). Keine
Sektionen nachtraeglich entfernt, installierte Bibliothek und SDK-Builder
unveraendert geblieben. Die anschliessend angefragte Erweiterung um
`scripts/build_user_sdk.py` ist durch die direkte Nutzerantwort
"weiter machen" freigegeben und in der Queue dokumentiert. Der SDK-Builder
verwendet jetzt denselben nachgewiesenen Schalter; der externe SDK-Test
validiert additiv jedes Mitglied aller sechs HTMLWORK-Archive.

Reparaturlauf der unveraendert befristeten Toolchaingruppe ohne parallele
Compilerlast: 21 Tests PASS, 153,700 s; externer SDK-Neubau und kompletter
Systemprogrammbau bestehen. Log `r319/gate-test_user_program_toolchain-repair.log`.
Der separate SDK-Timeout ist in diesem kontrolliert isolierten Lauf nicht
reproduziert; keine bewiesene allgemeine Timingursache oder Fristaenderung.
Damit zunaechst alle 89 Hosttests gruen; unveraenderte Ressourcen-/Hosteingaben
behalten ihre bereits bestandenen Nachweise.

Erste Referenzen PASS: VMware 18,398 s, QEMU 58,806 s. Browser und HTMLWORK
bleiben bei 2801692/845868 Dateibytes und 6178829/2752100 Ladebytes, alle
R3.19-Grenzen eingehalten. Erster Gastlauf: cpp-client 11,605 s, browser
88,779 s, resources 75,550 s und forms 76,104 s PASS. Die oeffentliche
Navigation scheitert nach 93,838 s: Beide geforderten Rastermarker liegen
vor, aber der unveraenderte globale 30-Sekunden-Browser-Selbsttest endet
zu spaet. Kein Crash; alle Dokumente/Redirects waren verarbeitet.
Fehler inklusive Abschlusszustand bleibt in
`r319/runtime-desktop-browser-public.guest.log` und zugehoerigem JSON erhalten.

Ein unabhaengig reproduzierbarer Wartefehler im schon freigegebenen main.c:
`finish_load_turn` schlief auch ohne lebendes Kind vor bereits ausfuehrbaren
lokalen Ressourcen-/Redirect-/Reflow-/Bildschritten. Neuer echter Hostfall
scheitert zuerst exakt an diesem Timerwait (`r319/ready-turns-before.log`),
ohne Windows-Fehlerdialog. Solche begrenzten lokalen Folgeschritte erhalten
jetzt genau eine Scheduler-Uebergabe statt eines unnoetigen Timerwaits.
Lebende/abzuerntende Kinder, blockierte Queues, unerfuellte Reflowbedingungen
und ausgeschoepfte Bildslots behalten den Idlewait. Kein Retry-Spin,
zusaetzlicher Worker, geaenderter Cache, erweitertes IPC-Turnbudget oder
verschobene/erhoehte Frist. Runtime-Hostgruppe inklusive unveraenderter
Fehler-/Reapfaelle und neuer Ready/Blocked-Regression: 29 Tests PASS,
6,435 s (`r319/gate-test_browser_runtime_source-ready.log`). Betroffene
Hostgruppen danach erneut PASS: Toolchain 21/152,543 s, GUI-Browser
8/33,895 s und Public-Navigation 2/1,619 s. Keine geaenderten Assertions
oder Fristen; zusammen mit unveraenderten Nachweisen weiter 89 Tests.

Finale Referenzen mit dieser Reparatur: VMware 61,879 s und QEMU 58,076 s
PASS. Alle sechs protokollierten SDK-/Programm-Artefakte sind zwischen
beiden Referenzen identisch. BROWSER.PRG SHA-256
`f5c6e4f50011aad1a1d610fd4cecd963fae0a2b661f851fbf9de82641e2a6002`,
HTMLWORK.PRG `f346bff0934adc119e4b5c5f83f06c828b1c739a1b6d2f7ba7b148ce088f1f37`.
Datei-/Ladegroessen bleiben exakt auf R3.18-Niveau, unter allen R3.19-Grenzen.
Desktop, CPPTEST und libreistcpp sind gegenueber R3.18 unveraendert.
Finale oeffentliche Navigation PASS, 93,013 s Hostlaufzeit: lange CSS-/Bild-
URLs, exakte sieben HTTP-Anfragen inklusive Redirects/Import, Windows-1252,
beide Rasterpruefungen und CLOSE_OK innerhalb der unveraenderten Gastfrist.
Autoritative finale JSON-/Gastlogs liegen unter `r319/ready/`; alter Fehler
bleibt daneben erhalten. Alle fuenf finalen Gastgates bestehen:

| `test-reist-runtime.ps1 -Mode ... -Target qemu -Video vga` | Ergebnis | Hostsekunden |
|---|---|---:|
| `cpp-client` | PASS | 11,799 |
| `runtime-desktop-browser` | PASS | 89,244 |
| `runtime-desktop-browser-resources` | PASS | 74,344 |
| `runtime-desktop-browser-forms` | PASS | 75,556 |
| `runtime-desktop-browser-public` | PASS | 93,013 |

Die Nachweise umfassen normale C++-Lebensdauer/OOM/Fault/Kill/Reap und
Rueckkehr zur Shell, Browserlinks/Bilder/native Scrollbar/Mausrad,
Worker-Fault/Timeout/Recovery, Ressourcen-Cancel/Fencing/Reload/Cleanup sowie
Formularwerte nach Eingabe, Wheel, Reflow, Reset, Ablehnung und Wiederherstellung.
Keine Abschwaechung der Gates oder Wiederholung unveraenderter Messquellen.
Diff-/Scope-/Hash-/Gateaudit: `r319/candidate-audit.json`; keine Umsetzung
des Nachfolgepakets und kein Push. Nutzen des C++-Piloten ist gepruefte
Snapshot-Publikation ohne erfundene Ownership; keine behauptete Reduktion
von Cleanup-Pfaden oder neue Webkompatibilitaet. Der beobachtete SDK-Timeout
und das fruehere Reflow-Timingrisiko bleiben ohne bewiesene allgemeine Ursache
dokumentiert. Kein allgemeiner Timing-/Live-Internet-/WCET-Claim.

## R3.18 abgenommen: Response-C++ mit erhaltener C-Grenze

Der echte Browser verwendet jetzt ausschliesslich die C++-Response-Admission.
5793 C/C++-Vergleichsfaelle je O0/O2, negative Konstruktionstests und der
Objekt-/Stacknachweis sichern vollstaendig validierte typisierte Erfolge ab.
C-Fehlerdiagnosen, Rueckgabecodes, Wire-Layouts und Aufrufer bleiben erhalten.
Vorher/nachher: null besessene externe Ressourcen, null Init/Destroy-Paare,
keine Heapallokation und keine neue Bodykopie. Zwei begrenzte Metadatenkopien
pro C-Admission. Der Gewinn ist die nicht ungeprueft konstruierbare Erfolgs-
invariante; keine behauptete LOC-/Cleanup-Reduktion oder neue Webkompatibilitaet.

Die Stackregression und die serielle Empfangsrace sind repariert. Die vom
Nutzer gewuenschte Fortsetzung nach Fehlern ist in der Queue dokumentiert.
Final bestehen 88 Hosttests in elf unveraenderten Gate-Befehlen, ohne Skips.
Die zuletzt betroffene Runtime-Gruppe besteht mit 29 Tests/6,550 s;
unveraenderte Gruppen behalten die unten aufgefuehrten Nachweise. Keine
unnuetze Wiederholung der gepaarten Messung oder unveraenderter Compiler-Gates.

Finale Referenzen (`test-reist-package.ps1 -Target ... -Video vga`):

| Target | Ergebnis | Sekunden | Log unter build/codex-agent |
|---|---|---:|---|
| qemu | PASS | 64,127 | 20260906-212839-package-qemu-vga.log |
| vmware | PASS | 60,379 | 20260906-213739-package-vmware-vga.log |

Bootmanifest und SBOM bestehen. Die bekannte optionale FAT12-Warnung bleibt
sichtbar und ist keine Diskettenabnahme. Finales `BROWSER.PRG`: 2801692 Byte,
Loader-Payload 6178829 Byte (+16588 private BSS-Bytes), SHA-256
`1a2ce8d42e89b2d7db901f941b688fea9b26d04ad246c20d4340de92bd2b85ab`.
HTMLWORK, Desktop und CPPTEST bleiben binaer unveraendert. Kein Kernel-,
Stackguard-, Quoten-, Prozessprioritaets- oder Timervertragswechsel.

Finale Gastgates (`test-reist-runtime.ps1 -Mode ... -Target qemu -Video vga`):

| Mode | Ergebnis | Sekunden |
|---|---|---:|
| cpp-client | PASS | 19,348 |
| runtime-desktop-browser | PASS | 89,555 |
| runtime-desktop-browser-resources | PASS | 77,770 |
| runtime-desktop-browser-forms | PASS | 77,366 |
| runtime-desktop-browser-public | PASS | 94,560 |

Gastlogs: `r318/framing-repair/cpp-client.guest.log`, alle vier Browserlogs
unter `r318/worker-diagnostic/`. Formularnachweis umfasst exakten GET,
Ablehnung ohne Anfrage, Reflow/Reset, absichtlich fehlerhaften Worker,
Recovery und Close. General prueft Links/Bilder/Scrollbar/Wheel, Resources
Cancellation/Cleanup; Public lange CSS-/Bild-URLs, Encoding und Redirects.
Public verwendet lokale deterministische HTTP-Fixtures, keinen Live-Google-
oder JavaScript-Kompatibilitaetsnachweis.

Unveraenderte quantitative Response-Messung: C 1994,843 ns, C++ 2180,436 ns
(109,304 Prozent, Grenze 120 Prozent und 5000 ns). Boundary-Stackpeak
8308 statt 8480 Byte; Factory 28 statt 16616 Byte des verworfenen Kandidaten.
Compilerartefakte, Quellen-/Fixturehashes und alle fuenf Paare unter
`r318/stack-repair/response/`; finale Artefaktgrenzen unter
`r318/worker-diagnostic/artifact-bounds.log`.

Verbleibendes Risiko: Der einzelne langsame Reflow-Abbruch im vorherigen
Lauf wurde nach Instrumentierung nicht reproduziert. Die neue Diagnose
weist im erfolgreichen Formularlauf nur den absichtlich ausgeloesten
Worker-Fault als `css-ipc/-84` aus. Sie beweist keine Ursache oder Reparatur
der vorherigen Laufzeitschwankung (Spawn dort 4586 ms, hier maximal 1520 ms).
Fehlgeschlagene Logs bleiben erhalten; das 5000-ms-Budget und alle Assertions
sind unveraendert. Keine generelle Performance-/WCET-Garantie behauptet.

R3.18 ist done; R3.19 fuer gepruefte geliehene Ressourcen-Snapshots ist als
naechstes Paket aktiv und vor Implementierung quantitativ eingefroren.
Ressourcen-/Modelquellen wurden in diesem Lauf NICHT migriert. Die bereits
abgeschobene VMware-Pointer-Abnahme bleibt mit ihren alten Gates queued.
Abschliessender Read-only-Abgleich PASS/0,563 s: 20 erlaubte Pfade, erhaltene
Parserlogik, unveraenderte Gate-Befehle, Quellen-/Artefakthashes, Grenzen und
alle Gastmarker (`worker-diagnostic/final-verification.log`). Der vorher
unbenutzte Pruefentwurf hatte Maxlength-/Wheel-Marker vertauscht; nur diese
falsche Reihenfolge wurde an echten Code und Log angeglichen, kein Gastgate
wiederholt oder abgeschwaecht. Sein Fehlprotokoll bleibt ebenfalls erhalten.

### Verlauf: Empfangsrace und Worker-Diagnose

Der Nutzer gibt die Runner-Dateierweiterung frei und weist nach dem folgenden
Gastfehler ausdruecklich an, Fehler weiter zu korrigieren statt anzuhalten.
Die Queue dokumentiert diese Abweichung von der Ein-Reparatur-Stoppregel nur
fuer R3.18. Keine blinden Wiederholungen, Frist-/Assertion-/Schutzlockerungen
oder stillen Dateierweiterungen. Kein Commit/Paketwechsel; R3.19 unimplementiert.

Der Formular-Testtreiber wartet jetzt auf die vollstaendige Tastaturzeile,
bevor er den unveraenderten exakten Tastencode prueft. Regression-first:
LF/CRLF, alle Fragmentgrenzen, einzelne Zeichen, falsche Codes/Ordinals,
fehlender Zeilenabschluss, Gastfehler und unveraenderte Gesamtfrist.
Begrenzte Host-Exceptiontexte werden vor Cleanup im Gasttranskript gespeichert;
Status und Reaping bleiben unveraendert. Vorherige Regression FAIL,
danach alle 29 Runtime-Hosttests PASS/8,589 s, Logs
`build/codex-agent/r318/framing-repair/`. Insgesamt 88 Hosttests in elf Gruppen;
unveraenderte Nachweise der vorherigen Response-/Buildreparatur bleiben erhalten.

Formulargast mit unveraendertem finalen Image: FAIL/72,824 s.
Alle elf ersten Tasten, Edit-only, Maxlength und Wheel bestaetigt. Beim Reflow
folgt `BROWSER_HTML5_REJECT exit=143 result=-5 cancelled=1`, dann Phase 3
`BROWSER_PROBE_FAIL interaction`. Der neue gespeicherte Fehlertext ist
`HOST_RUNTIME_FAILURE RuntimeError: Forms guest failure`. Kein Page-Fault-Marker;
Kindprozess bereits gereapt, `loaded=1`, alte Seite bleibt. Das ist keine
erfolgreiche Reflow-Abnahme. Der Spawn benoetigt hier 4586 ms, innerhalb eines
unveraenderten 5000-ms-Gesamtbudgets; zuvor 747/1605 ms. Auch Desktop-Font-I/O
liefert in diesem Lauf -110. Das Transkript beweist noch nicht, ob Deadline
oder IPC den Worker-Abbruch ausgeloest hat; keine spekulative Fristerhoehung.
`framing-repair/runtime-desktop-browser-forms.guest.log` bleibt erhalten.

Unveraendertes cpp-client-Gastgate PASS/19,348 s, mit allen Runtime-/Reap-
Markern und anschliessender Shell; `framing-repair/cpp-client.guest.log`.
Nun begrenzte Probe-Diagnose an der echten Browser-Abbruchstelle: Grund,
Rueckgabecode, Restbudget und IPC-Fortschritt, erst nach Fencing/Terminate und
hoechstens einmal je Abbruch. Normale Browserausfuehrung gewinnt keine
zusaetzliche Ausgabe. Reale Hostregression fuer Deadline und EPIPE zeigt
weiterhin unveraendertes Budget, erhaltene Seite und genau einen Kill/Wait;
vorher FAIL, danach Runtime-Hostgruppe 29 PASS/6,550 s
(`r318/worker-diagnostic/`). Instrumentierter QEMU-Build/Gastlauf laufen noch;
kein erneuter Formular-PASS und keine Vollabnahme behauptet.

### Vorheriger Stopppunkt: Stackreparatur und Formular-Testtreiber

Finaler Stand dieses Laufs: kein Commit/Paketwechsel. Alle elf Hostgruppen
bestehen (83 Tests ohne Skips). Nach der unten beschriebenen Stackreparatur
erneut geprueft: Response 10,268 s, Toolchain 157,854 s, Navigation 2,121 s,
Browser-Runtime 4,728 s, Public-Admission 1,554 s; Logs
`build/codex-agent/r318/stack-repair/gate-test_*.log`. Unveraenderte Gruppen:
`r318/resumed/gate-test_{cpp_types,user_cpp,cpp_baseline}.log` und
`r318/resumed/clang-test_{gui_browser_source,browser_resources,browser_forms}.log`.

Finale Referenzen PASS: vmware 65,886 s
(`20260906-205620-package-vmware-vga.log`), qemu 61,779 s
(`20260906-205726-package-qemu-vga.log`), jeweils Bootmanifest/SBOM bestanden.
BROWSER.PRG bleibt 2801692 Byte; Payload 6178829 Byte (+16588 private BSS-Bytes),
beide festen Grenzen eingehalten. SHA-256
`032d63496ef6b95bf639e853ac4e07b015f49c59d58f61c2831749904f5fcca5`,
`r318/stack-repair/artifact-bounds.log`. Keine Kernel-/Stackguard-/Quotenaenderung.

Das zuvor fehlgeschlagene Formular-Gate wurde auf diesem finalen Image zuerst
erneut ausgefuehrt, mit unveraendertem Befehl und unveraenderten Fristen.
FAIL/69,644 s; `r318/stack-repair/runtime-desktop-browser-forms.guest.log`.
Jetzt kein Page-Fault-/Probe-Fail-Marker: Reflow, alle drei Ablehnungen und
Reset bestehen; das Transkript endet bei Tastaturevent 20 (`code=108`), vor
`BROWSER_FORMS_SEND_READY`. Der urspruengliche Stackfault wurde in diesem Lauf
somit nicht erneut erreicht; seine Formular-Gastregression bleibt offen.
Wegen des Stopppunkts nach dem Reparaturlauf wurden auf dem finalen Image die
anderen vier Gastgates nicht ausgefuehrt. Fruehere Gast-PASS gelten nicht als
Abnahme der reparierten Binaerdatei. Leeres PowerShell-stdout bleibt kein
Nachweis fuer Fehlerfreiheit und lieferte hier keinen genauen Exceptiontext.

Read-only Diagnose mit den unveraenderten AST-Funktionskoerpern `wait`/`key`
aus `run_browser_forms_probe`: Vollstaendiges Event 20 wird akzeptiert;
nur `BROWSER_FORMS_KEY ordinal=20 code=` fuehrt sofort zu
`Forms keyboard event mismatch`, bevor restliche Bytes eintreffen koennen.
Der echte Reader liefert einzelne Zeichen; `wait` kehrt schon beim Praefix
zurueck, `key` verlangt danach aber eine vollstaendige Zeile. Dieser sicher
reproduzierte Testtreiberfehler passt zum beobachteten Abbruch; mangels
erhaltenem stderr ist die exakte Exception des Gastlaufs nicht bewiesen.

Erforderliche, noch NICHT freigegebene Dateierweiterung:
`scripts/run_qemu_runtime_desktop.py` fuer vollstaendige, weiterhin begrenzt
empfangene Formular-Tastaturrecords und verlaessliche Fehlerdiagnose.
Regressionen passen in das bereits erlaubte `test/test_browser_runtime_source.py`.
Keine Fristverlaengerung, keine geaenderte Eingabe, abgeschwaechte Assertion
oder Kernel-/Browserfunktion dafuer erforderlich. Die Runnerdatei wurde nicht
geaendert; R3.18 bleibt allein active, R3.19 wurde nicht definiert/implementiert.
Der ignorierte `r318/resumed/verify_candidate.py` ist ein unbenutzter Entwurf
fuer spaetere Vollabnahme, kein ausgefuehrter oder bestandener Nachweis.

### Wiederaufnahme und gezielte Stackreparatur

Die 18 sichtbaren Aenderungen stammen aus dem vorherigen R3.18-Lauf; kein
fremder Source-Writer. Der Nachweis verwendet jetzt direkte i386-Aufrufketten
mit Relokationen und konservativen Tailcalls statt alternativer Wrapper-Summen.
Unbekannte/indirekte/rekursive Kanten werden abgewiesen; eigene Regressionen
sichern dies. Die feste 32768-Byte-Grenze bleibt unveraendert.

Die erste Wiederaufnahme besteht mit 83 Hosttests in elf Gruppen. Ein durch
meinen ergaenzten PATH ungewollt mit GCC ausgefuehrtes Navigationstest-Fixture
scheiterte an dessen Warnungen. Der urspruengliche PATH/Zig-Clang besteht ohne
Source-/Warnungs-/Fristaenderung. Logs `r318/resumed/gate-*.log`, letzte sechs
Gruppen `clang-*.log`. Referenzen PASS: vmware 68,544 s
(`20260906-203626-package-vmware-vga.log`), qemu 62,034 s
(`20260906-203734-package-qemu-vga.log`), jeweils Manifest/SBOM. Die bekannte
optionale FAT12-Warnung ist keine Diskettenabnahme. Gast: cpp-client 12,536 s,
Browser 95,477 s, Resources 78,763 s PASS; `r318/resumed/*.guest.log`.

Formulare FAIL/180,949 s nach `BROWSER_FORMS_SEND_READY`: EIP `0x4001B668`,
CR2 `0xBFFF6420`. Das gesicherte `r318/resumed/failed-forms-BROWSER.PRG`
(SHA-256 `6b04f18500113b4027224f6283f324f860ca7a2d389de82fb912801c9da797a0`)
zeigt dort `push $0x2014` nach dem 16-KiB-Factoryframe. Der gesamte bewachte
Userspace-Stack reicht nur von `0xBFFF7000` bis `0xBFFFF000` (32 KiB).
Ein Modul-Delta allein schuetzt die bestehenden Aufrufer also nicht.
Kein Public-Gate auf diesem fehlerhaften Image und kein Commit/Paketwechsel.

Eine gezielte In-Scope-Reparatur verlegt feste Parser-Header/Metadaten in
konstant initialisierte private BSS-Scratchfelder; sie zaehlen zum geprueften
Loader-Payloadbudget. Serialisierung war bereits fuer den URL-Resolver Pflicht.
Jeder Aufruf setzt/prueft seinen Zustand neu; Result kopiert unabhaengige
Snapshots. Keine Scratch-/Bodyzeigerretention, Heapallokation, Destruktor-
Registrierung, Kernel-/Quoten-/Guard-Aenderung. Weiterhin zwei Metadatenkopien
je C-Admission, keine neue Bodykopie. Die Aufrufer bleiben unveraendert.

Neue Guard-Regression: Response-Peak hoechstens akzeptierter C-Peak plus
256 Byte ABI-Spielraum. Vor Reparatur FAIL (24912 > 8736),
`r318/resumed/guard-regression-before.log`; danach PASS, konservative
Zusatzstack-Obergrenze 8308 Byte. Factoryframe von 16616 auf 28 Byte reduziert.
O0/O2 jetzt je 5793 Vergleichsfaelle inklusive unabhaengiger alter Ergebnisse
nach spaeteren Admissions; gezielter Lauf 3,682 s (`stack-repair/focused.log`).
Finales Response-Gate PASS/10,268 s: C 1994,843 ns, C++ 2180,436 ns
(109,304 Prozent), feste Grenzen unveraendert. Betroffene Hostgruppen, beide
Referenzen und alle fuenf Gastgates werden nach Reparatur erneut geprueft;
unveraenderte Hostgruppen behalten ihre bestehenden Nachweise. Noch kein Commit.

### Vorheriger Stopppunkt: Response-C++ und zu grober Stacknachweis

Fortsetzung auf sauberem `2e17d5fb8eb414d4676d3a5fbd8592df8e5dd195`, direkt
im sichtbaren Worktree. Noch kein Commit und kein Paketwechsel. Einzige aktive
Queue bleibt R3.18; keine Umsetzung des Resources-/Model-Nachfolgers.

Der bisherige Parser ist jetzt die einzige Produktionsimplementierung in
`browser_response.cpp`. `ValidatedResponse::open` liefert ein allokationsfreies
`Result<ValidatedResponse,ResponseError>`; der private Konstruktionsschluessel
verhindert ungepruefte Erfolgskonstruktion. Eingaben werden nur waehrend des
Aufrufs geliehen, keine Bodykopie/Pointerretention oder erfundener Destruktor.
Die drei C-Einstiegspunkte behalten Struktur, Rueckgabecodes und auch partielle
Fehlerdiagnosen (insbesondere HTTP-Status). Der reale `finish_fetch` verwendet
diese Grenze weiterhin; nur Status wird vor der Fehlerpruefung gelesen.
Gemischter Systembuild und betroffene Hostadapter uebersetzen nach TU-Sprache;
der Browser aktiviert dieselbe bestehende C++-Objekt-/Archivzulassung.

Regression-first-Fixture wurde vor dem noch fehlenden C++-Header angelegt.
Gezielter O0/O2-Vorlauf besteht (30,143 s): je 5665 direkte Vergleiche mit dem
committeten C-Original, inklusive Erfolg, Redirect, Encoding, allen Statuscodes
200..599, Trunkierung, malformed/oversized und 4096 deterministischen Mutationen.
Keine duplizierte Produktionsimplementierung. Alte C-Tests bleiben erhalten.

Erstes eingefrorenes Gate:
`python test/test_browser_response_cpp.py -v` FAIL/12,189 s,
`build/codex-agent/r318/gate-test_browser_response_cpp.log`.
Zig legt `-fstack-usage` im Cache statt neben dem Zielobjekt ab; das Gate fand
seinen Nachweis nicht. Einzige gezielte Reparatur: explizite, lokal per
Clang-cc1-Hilfe bestaetigte `-stack-usage-file`-Ausgabe. Kein Grenzwert geaendert.

Erneutes Gate FAIL/9,822 s,
`r318/final-test_browser_response_cpp.log`: vier Tests bestehen, Stacktest
scheitert. Belege in `r318/response/stack-profile.json`, `target-c.su`,
`target-cpp.su` und den zugehoerigen i386-Objekten. Der Test summiert konservativ
alle C++-Frames einschliesslich drei alternativer C-Wrapper und subtrahiert
nur den groessten C-Frame: 41404 - 8412 = 32992 Byte statt hoechstens 32768.
Read-only Disassembly bestaetigt, dass jeder der drei 8232-Byte-Wrapper direkt
die Factory ruft, nicht die anderen Wrapper. Das ist ein Fehler des zu groben
Stacknachweises, kein belegter realer Stackueberlauf; der vertraglich geforderte
Nachweis ist trotzdem nicht erbracht. Nach einem fehlgeschlagenen Reparaturlauf
greift der eingefrorene Stopppunkt. Keine zweite Reparatur/weiteren Gates.

Bereits bestandene Teilnachweise: O0/O2-Differentialfaelle, Compile-time-
Ablehnung gefaelschter Erfolge/Temporaer-Borrows, echte C-Aufrufer sowie
i386-Profil-/Undefined-Symbolpruefung ohne Allocator/C++-Runtime. Gepaarte
Messung: C 2021,721 ns, C++ 2164,542 ns (107,064 Prozent); beide festen Grenzen
(120 Prozent, 5000 ns) eingehalten. Fuenf frische alternierende Prozesspaare,
200000 validierte Aufrufe, unveraendertes Fixture/C-Abhaengigkeiten, Zig 0.16,
O2/UNDEBUG/fno-builtin/kein LTO, QPC und 15-s-Frist. Alle Samples/Hashes stehen
in `r318/response/paired/paired-response.json`; erste Messung separat erhalten
in `r318/first-paired-response.json`. Keine UI-/WCET-Aussage.

Offen fuer einen neuen Lauf: Stacknachweis anhand tatsaechlicher Aufrufketten
mit demselben 32768-Byte-Limit korrigieren und testen, danach restliche zehn
Hostgruppen, beide Referenzen, finale PRG-/Payload-/Kopiergrenzen und alle fuenf
Gastgates. Bisherige Images bleiben alte akzeptierte Artefakte, kein Nachweis
dieses Kandidaten. VMware-Pointer und vollstaendige Browserkompatibilitaet
bleiben offen. Keine globalen Windows-Einstellungen, Testdialoge, Agenten oder Push.

## R3.17 abgenommen: minimale allokationsfreie C++-Hilfstypen

Alle zehn eingefrorenen Gates bestehen. Finale Hostabnahme: 80 Tests ohne
Skips (5+6+21+4+31+13). Typen 4,195 s, Toolchain 145,786 s; die unveraenderten
Runtime-/libc-/Shell-/Layoutgruppen bestehen mit 4,905/0,822/0,755/0,012 s.
Logs unter `build/codex-agent/r317/`: `final-test_cpp_types.log`,
`final-test_user_program_toolchain.log` und die vier `gate-test_*.log`.

Finale Referenzen und Gastnachweise nach der unten dokumentierten Probenreparatur:

- vmware/vga PASS/55,090 s: `20260906-195321-package-vmware-vga.log`.
- qemu/vga PASS/51,307 s: `20260906-195416-package-qemu-vga.log`.
  Beide mit gueltigem Bootmanifest und SBOM. Die bekannte optionale FAT12-
  Rescue-Warnung bleibt unveraendert, keine neue Diskettenabnahme.
- `cpp-client` PASS/12,106 s: `20260906-195507-cpp-client.log`, geordnet
  Typen-/Handlemarker plus alle alten OOM/Fault/Kill/Reap-/Shellmarker.
- `runtime-desktop-browser-forms` PASS/76,468 s: gesichertes Gasttranskript
  `r317/final-browser-forms.browser.log`, SHA-256
  `76a0e3cf70e87aecc4fa47cb913922a48a10b1bc8bf50046b2834ebd8e5ea6ee`.
  Reale Eingabe, exakter GET, Reflow, Ablehnung ohne Request, Reset,
  Worker-Failure/Recovery und Close. Die aeusseren stdout-Dateien blieben wie
  bei frueheren versteckten PowerShell-Kindern leer; sie sind kein Ersatz fuer
  diese datierten Build-/Gastlogs und die beobachteten Exitcodes.

CPPTEST bleibt 28676 Byte (+4096 gegen R3.16), SHA-256
`9be0832515a90521b6db7a2a5240e24dd72e3c6853413691b9b37df25dd84f5d`.
Runtime-Archiv unveraendert: 12196 Byte,
`8ef0d80b3132049643ee0b6cd82122b9d5ca302b37042603fb4607b5fbb37538`.
BROWSER.PRG, HTMLWORK.PRG und DESKTOP.PRG bleiben exakt baseline-bytegleich.
Keine neue Browserfunktion, kein Heap-/Kernelumbau und keine pauschale
Performancebehauptung. Header-Vertraege unterscheiden normale Lebensdauer,
geliehene Ansichten und OS-Crash-Reaping ausdruecklich.

R3.17 ist done. R3.18 ist als einziger Nachfolger active und definiert den
response-Piloten mit echten C-Aufrufern, gleicher C-ABI und vorab festen
Messgrenzen (gepaarter Median <=120 Prozent und <=5000 ns, PRG/Payload jeweils
hoechstens +64 KiB, keine Response-Heapallokation, Stackdelta <=32 KiB).
Nur der Vertrag wurde angelegt, keine R3.18-Produktionsdatei implementiert.
Resources/Model folgen spaeter; VMware-Pointer bleibt unveraendert deferred.

Abschliessender read-only Scope-/Queue-/Evidenzabgleich besteht (0,457 s),
`r317/final-scope-evidence.log`: 17 erlaubte Dateien, nur der vorgeschriebene
R3.17-Statuswechsel plus neuer R3.18-Vertrag, alle alten Gates/Vertraege
unveraendert, Header im installierten SDK bytegleich. Direkter Diff-Review
prueft Lebensdauer, Bounds, Cleanup und unveraenderte C-Grenzen;
`git diff --check` besteht. Keine Agenten, globalen Hostaenderungen oder Push.

### Umsetzung und erhaltene Erstfehler

Die zurechenbare Regression-first-Arbeit wird nach Plancommit `0a8326f5`
fortgesetzt; keine fremden Aenderungen im Worktree. Implementiert sind sieben
Header fuer die sechs vereinbarten Typen plus interne Utility, ohne Aenderung
an Runtime, Allocator, Buildskripten, Browser, GUI oder Kernel. Der vorhandene
SDK-Kopierer installiert sie; der externe SDK-Test nutzt alle sechs Typen und
unveraenderte C-Aufrufe. Details/Abweichungen stehen im SDK-Vertrag.

CPPTEST ergaenzt Werte-/Kapazitaetspruefungen und echte IPC-Ownership:
Factory-Erfolg, Move/Self-Move, Release/Adoption, genau einmal Close, altes
Handle abgewiesen und frischer funktionsfaehiger Endpunkt. Die vorhandenen
OOM/Fault/Kill/Reap-/Shellmarker und Fristen bleiben unveraendert. Abnahme
inzwischen wie oben bestanden; keine neue Browserfunktion oder Performancebehauptung.

Gezielte Vorlaeufe: fehlende Header erwartungsgemaess reproduziert. Die erste
Host-Kompilation beanstandete den nur fuer freestanding erforderlichen
`extern "C" main` im hosted Test; die Probe trennt beide Startkonventionen
jetzt ohne Warnungsunterdrueckung. O0/O2-Lebensdauer und i386-Link ohne
Allocator/C++-Runtime bestanden (2 Tests/41,092 s beim kalten Linkcache).
Erstlogs bleiben unter `build/codex-agent/r317/`. Die finalen sechs Hostgruppen,
beide Referenzbuilds und beide Gastgates wurden gemaess Queue ausgefuehrt;
Paketwechsel und Implementierungscommit erfolgen erst nach deren Erfolg.

Erste Abnahme: 79 Hosttests bestehen (4+6+21+4+31+13; Toolchain 144,722 s).
Referenzbuilds bestehen: vmware 9,756 s, qemu 53,897 s, Logs
`20260906-194109-package-vmware-vga.log` und
`20260906-194118-package-qemu-vga.log`; Manifest und SBOM gueltig.
CPPTEST 28676 Byte; Browser/HTMLWORK/Desktop weiterhin baseline-bytegleich.
Der erste `cpp-client` scheitert nach 12,205 s bei `phase=types`, Log
`20260906-194344-cpp-client.log`. Ursache ist die neue Probe, nicht der IPC-
Vertrag: `receivable_offset` liefert keine Nachrichten derselben Sender-
Prozessgeneration. Der echte neue Probenkoerper reproduziert dies im Hostmodell
mit Exit 1 (`before-ipc-sender.log`), nachdem dessen fehlende Host-CRT-
Deklaration korrigiert wurde. Eine gezielte Reparatur prueft den bestehenden
Senderausschluss mit EAGAIN/Nullfrist und Queue-Cleanup bei Close. Der echte
Kindprozess-Austausch verwendet jetzt dieselben UniqueHandle-Owner, weiterhin
mit allen alten Canaries, Fehlerstatus und Reap-Fristen. Keine Kernel-/ABI-
Aenderung oder abgeschwaechte Abnahme. Finale betroffene Typen-/Toolchaingates,
beide Referenzbuilds und Gastabnahmen bestanden nach dieser Reparatur;
unveraenderte Runtime-/libc-/Shell-/Layout-Hostnachweise bleiben erhalten.

## C++-Planrevision 1.1: freigegebene Praezisierung, keine Runtime-Abnahme

Der Nutzer bestaetigt die sechs Review-Anpassungen. Der
[Migrationsplan](../REIST_CPP_MIGRATION_PLAN.md) benennt jetzt die tatsaechlichen
Ressourcenbesitzer, fallible Factories, begrenzten Destruktor-Cleanup gegenueber
OS-Recovery, explizit budgetierte grosse private Payloads, Browserprioritaet
und vorab numerisch festzulegende Grenzen fuer zukuenftige Migrationspakete.
Die Zeichnung trennt Ring-3-Dienste/Treiber vom geschuetzten Microkernel.
SDK-, Browservertrag, Roadmap und aktiver Queue-Verweis sind abgeglichen;
veraltete SDK-/VMware-Prioritaetsangaben sind korrigiert.

Nur Dokumentation und ein erklaerendes `plan_alignment`-Feld in R3.17 werden
geaendert. Paketstatus/-reihenfolge, erlaubte Implementierungsdateien, Profile,
eingefrorene Gates und bisherige Evidenz bleiben unveraendert. Es gibt keine
neue Browserfunktion oder Performancezusage. Baseline `8ff162a3` und Runtime
`478289b7` bleiben die abgenommenen Grundlagen.

Der zuvor von dieser Sitzung angelegte Regressionstest `test/test_cpp_types.py`
bleibt unveraendert und ausserhalb des Dokumentations-Commits. Sein gezielter
Vorherlauf scheitert erwartungsgemaess an den noch fehlenden Headers
(`build/codex-agent/r317/before.log`); kein finales R3.17-Gate wurde ausgefuehrt
und R3.17 ist nicht abgenommen. Keine Produktionsaenderung in dieser Planrevision.

Dokumentationspruefung: `python build/codex-agent/r317/verify_plan_revision.py`
PASS/0,673 s. TOML-Vergleich gegen HEAD bestaetigt ausschliesslich das neue
Erklaerungsfeld, keine Scope-/Gate-/Status-/Evidenzaenderung; Taskfolge,
Markdown-Fences und acht neue lokale Links bestehen. Log:
`build/codex-agent/r317/plan-revision-check.log`. Direkter Diff-Review und
`git diff --check` vor Commit; keine Wiederholung bereits abgenommener VM-Gates.

## R3.16 abgenommen: opt-in C++20-SDK und explizite Objektlebensdauer

Alle zehn eingefrorenen Gates bestehen. Finale Referenzen: vmware/vga
53 s (`20260906-184558-package-vmware-vga.log`), qemu/vga 50 s
(`20260906-184746-package-qemu-vga.log`), jeweils Bootmanifest und SBOM gueltig.
75 Hosttests ohne Skips; Details und erhaltene Erstfehler unten.

Gastnachweise:

- `cpp-client`: PASS/11,354 s, `20260906-184852-cpp-client.log`.
  Normale Objekt-/Array-/aligned-Lebensdauer, exakte Frame-Rueckgabe,
  nothrow-OOM ohne Zustandsverlust, gewoehnliches new mit Prozessstatus 71,
  Fault/134 und Kill/143, Parent-Canary, exaktes Reap, frisches Kind und Shell.
- `memory-resilience`: PASS/209,829 s einschliesslich separatem Build;
  Gastphase 72 s, `20260906-185204-runtime-guest-smoke-memory-resilience.log`,
  endet nach den bestehenden Ausnahmepruefungen mit TEST_OK und Shell.
- `runtime-desktop-browser-forms`: PASS/76,927 s,
  `r316/gate-browser-forms.log`; echte Eingabe, exakter GET, Reflow,
  Ablehnung, Reset, Recovery und Close bleiben funktional.

`CPPTEST.PRG` hat 24580 Byte, SHA-256
`5c28ec456e6d7b268553708a36db7fe97204e181beb349ab24aab375646f61ba`;
`libreistcpp.a` 12196 Byte, SHA-256
`8ef0d80b3132049643ee0b6cd82122b9d5ca302b37042603fb4607b5fbb37538`.
Die neu gebauten BROWSER.PRG, HTMLWORK.PRG und DESKTOP.PRG stimmen bytegenau
mit der committeten Baseline ueberein. Das GUI-Archiv bleibt gleich gross,
hat nach der Header-Neuuebersetzung aber einen anderen Hash; keine Behauptung
vollstaendiger Artefaktidentitaet. Keine neue Performance-/VMware-Pointerzusage.

Bekannte, unveraenderte Nebenwarnung: Die optionale QEMU-FAT12-Rettungsdiskette
passt nicht in ihre Tabelle; bereits im Vorlauf
`20260906-140440-package-qemu-vga.log` dokumentiert. Die eingefrorenen
HDD-/SDK-Gates bestehen; keine neue FAT12-Abnahme oder Lockerung des Limits.

R3.16 ist done, R3.17 fuer die minimalen allokationsfreien Hilfstypen aus
TASK-2001 active. Dieses Folgepaket ist nur definiert, noch nicht implementiert.
Danach bleibt die Reihenfolge response -> resources -> model; die VMware-
Pointerabnahme bleibt unveraendert zurueckgestellt. Kein Push oder Agentenlauf.

### Umsetzung und Diagnosechronik

Baseline-Commit ist `8ff162a3`. Implementiert sind das additive Runtime-Archiv,
gemischter C/C++-PRG-Build, C-Linkage der bestehenden SDK-Funktionen und
Zulassungspruefung aller ELF-/Archivinputs vor GC/Strip. Kein neuer Kernel-
oder Allocatorpfad, kein C++-Browserumbau. Der C-Einstieg verlangt ausdruecklich
`extern "C" int main(int,char**)`; keine implizite Heapinitialisierung.
Der SDK-Vertrag dokumentiert Profil 1 und seine Grenzen.

Die sechs C++-Host-/Objekt-/Linktests bestehen (4,770 s). Echte Runtime und
C-Heap pruefen normale/Array-/aligned-Lebensdauer, Placement/Move, Zero/Null,
Overflow, nichtveraenderndes nothrow-OOM, Backingrueckgabe sowie lokale
Exitcodes 71/72. Windows-Tests laufen ausschliesslich durch den vorhandenen
nichtinteraktiven Launcher; keine globalen Systemeinstellungen oder Agenten.

Erster Toolchainlauf: 21 Tests/127,257 s, eine fehlende Soll-Listen-Ergaenzung
fuer das neue CPPTEST.PRG. Compiler, externer C++-Sysroot-Link und MYPR-Build
bestanden bereits. Gezielte Reparatur ergaenzt die exakte Programmliste und
erhaelt fuer erneut validierte, bytegleiche C++-PRGs die inkrementellen
Zeitstempel; keine Abschwaechung der Rebuild-Assertions oder Fristen.
Logs unter `build/codex-agent/r316/`, einschliesslich Erstfehlern.
Alle fuenf Hostgruppen bestehen: 6+21+4+31+13=75 Tests ohne Skips;
Toolchain-Reparaturlauf 136,152 s, libc 25,251 s, Shell 0,798 s, Layout 0,010 s.
Erste Referenzbuilds vmware/vga (46 s) und qemu/vga (49 s) bestehen.
Erster cpp-client-Gastlauf bestaetigt Lebensdauer und exakte Backingrueckgabe,
scheitert aber in der neuen Probe bei --oom: process_identity_of gibt fuer
einen Zombie korrekt ESRCH zurueck. Die Probe hatte irrtuemlich weiterhin
eine lebende Identitaet verlangt. Gezielte Reparatur erlaubt ESRCH ausschliesslich
zum anschliessenden Zombie-/Parent-Wait; fremde lebende Generationen bleiben
gesperrt. Kein OS-Vertrag wird geaendert. Beide Referenzen werden fuer diese
geaenderte Gastprobe erneut gebaut, danach genau ein reparierter cpp-client-Lauf.
Erstnachweis: `20260906-184423-cpp-client.log`. Kein Abnahmecommit vor den
finalen Referenz-/Gastgates; diese sind inzwischen wie oben aufgefuehrt bestanden.

## R3.16a abgenommen: Baseline vor der C++-Migration

Alle fuenf eingefrorenen Host-/Messgates und die QEMU-Renderprobe bestehen.
169 Hosttests ohne Skips; 72 Quelldateien, Lifecycle-/Ownership-Audit,
Artefaktgroessen/-Hashes und Host-/Gastmesswerte sind in
[CPP_MIGRATION_BASELINE.md](CPP_MIGRATION_BASELINE.md) und dem verlinkten
JSON festgehalten. Produktions-C/ASM, Font-/Lizenzpayloads und die
Nutzervorgabe bleiben unveraendert. Testharness- und Fontkatalogkorrekturen
wurden ausdruecklich freigegeben, Originalherkunft nachgewiesen. Pillow
12.1.0 liegt nur unter ignoriertem build/codex-agent; keine globalen Aenderungen
oder neuen Fehlerdialoge. Alle fehlgeschlagenen Erstlogs bleiben erhalten.

Finale Fontgruppe: 8 PASS/16,912 s; Messung: PASS/38,781 s.
Die neue QEMU-Renderprobe endet mit Status 0 nach 31,485 s:
1 Fullframe/15 ms, 8 Dragframes/max. 79 ms, 8 Resizeframes/max. 91 ms,
keine Fallback-/Clock-/Probefehler, Rueckkehr zur Shell. Nachweis ist
`20260906-180136-runtime-desktop-metrics.log`; die zusaetzliche versteckte
stdout-Umleitung blieb leer und wird nicht als Beweis verwendet.
Das vorhandene Hauptimage stammt laut Buildkonfiguration von vmware/vga,
wurde fuer diese Messung in QEMU TCG gebootet. Die drei gemessenen PRGs
stimmen exakt mit Image und SBOM ueberein; keine Behauptung einer kompletten
aktuellen SBOM-Imageidentitaet oder neuen VMware-Pointerabnahme.

Zum Baseline-Abschluss wurde R3.16a done und R3.16 active fuer TASK-1001/1002.
Dessen C++-Implementierung begann erst nach dem lokalen Baseline-Commit.
Keine Agenten und kein Push. Die folgende Diagnosechronik beschreibt die
frueheren, inzwischen aufgeloesten Blocker.

### Diagnosechronik vor der Abnahme

Nutzerauftrag nach `cc3d0dcb`: neue Migrationsanleitung uebernehmen und die
noetigen Schritte ausfuehren. Einzige Vorabaenderung ist die eindeutig
zugeordnete `docs/REIST_CPP_MIGRATION_PLAN.md`; ihr Inhalt bleibt unveraendert.
Vertrags-/Dokumentcommit vor Kandidatenarbeit stellt einen sauberen Worktree her.
TASK-0001 ist ein ausdruecklich vorgeschalteter committeter Nachweis. R3.16a
misst nur, ohne Produktionscode zu migrieren. R3.16 ist mit erhaltenen bisherigen
Gates queued: C++20, keine globale dynamische Initialisierung, keine automatische
Exit-Registrierung/Heapinitialisierung. Danach minimale libreist++-Typen und der
Browserpilot in der neuen Abhaengigkeitsreihenfolge. Keine Agenten oder Push.
Baseline-/Testnachweise stehen nach Abschluss in CPP_MIGRATION_BASELINE.md.

Aktueller Befund nach ausdruecklicher Freigabe der beiden Testdateien:
Explorer erwartet jetzt exakt MOVE/COPY/LINK/LAYOUT; der Startup-Harness
deklariert sein umbenanntes main vorab statisch. Keine Produktionsaenderung,
Warnungsabschaltung oder entfernte Assertion. Finale Desktop-Gruppe:
81 PASS/3,669 s (`cpp-baseline-desktop-final.log`), GUI-Gruppe:
75 PASS/9,378 s (`cpp-baseline-gui.log`), jeweils ohne Skips und durch den
nichtinteraktiven Launcher mit demselben MinGW GCC. Kein neuer Windows-Dialog.
Das naechste eingefrorene Gate `python test/test_gui_font.py -v` scheitert:
7 Tests/0,777 s, 1 Fehler und 1 fehlgeschlagene Assertion
(`cpp-baseline-font.log`). Pillow 12.1.0 ist im verwendeten Python nicht
installiert. Ausserdem stimmen zwei Lizenz-Hashes des vorhandenen Fontkatalogs
nicht mit den unveraenderten eingecheckten Lizenzdateien ueberein:

- Unifont `source/OFL-1.1.txt`: Katalog `4c69dde8...`, Git/Worktree
  `ddd1809b...`; auch reine CRLF-Umsetzung ergibt nicht den Kataloghash.
- Source Code Pro `source/source-code-pro/OFL.txt`: Katalog `7c940e28...`,
  Git/Worktree `0b2c3168...`; hier entspricht der Kataloghash exakt CRLF,
  waehrend `.gitattributes` LF verlangt.

Alle fuenf Font-Quelldateien und die drei anderen Lizenzdateien entsprechen
ihren Pins. Beide abweichenden Lizenzen sind seit mindestens `13bfa0dd`
bytegleich im Git. Keine Benutzerdatei oder Fontdatei wurde ueberschrieben.
Vor Korrektur des Katalogs muss dessen Originalherkunft geprueft werden;
blosses Uebernehmen aktueller Hashes waere kein Integritaetsnachweis.
Der Fontkatalog und sein Test/Generator liegen ausserhalb des freigegebenen
Mess-/Desktop-Testscopes. Daher Stopp vor Erweiterung; keine weiteren Gates,
kein Baseline-Abschlusscommit und keine Queue-Transition. Bereits gruene
unveraenderte Gruppen behalten ihren Nachweis. Der nachfolgende Text erhaelt
die bisherige Diagnosechronik einschliesslich der urspruenglichen Fehler.

Vertragscommit `61932c14` besteht, Worktree war danach sauber. Die 13
beabsichtigten Markdown-Hardbreaks in der Nutzerdatei wurden unveraendert
erhalten (Gitblob `1851b3571c2b26ec664bb792a841af23b9739f4c`).
Die neue Inventarmetrik besteht zunaechst 4 Hosttests/0,001 s.
Desktop-Baseline mit vorhandenem MinGW GCC im prozesslokalen PATH:
`python -m unittest discover -s test -p test_desktop_*source.py -v`
FAIL, 81 Tests/36,395 s, zwei Fehler (`cpp-baseline-desktop.log`).
Explorer-Hosttest endet mit Status 3 und zeigte einen Windows-Fataldialog;
Startup-Testharness scheitert an GCC -Werror=old-style-declaration beim
umbenannten main. Keine Produktionsquelle wurde geaendert; beide Fehler sind
offen. Keine GUI-/Benchmark-/Gastabnahme und kein Baseline-Abschlusscommit.
Nutzer untersagt solche Dialoge; betroffener Prozess ist bereits beendet.
Weitere Hoststarts nur durch `scripts/measure_cpp_baseline.py --host-test`
mit prozesslokaler Dialogsperre und verstecktem Python-Kind. Die eigentlichen
Gateargumente, Assertions und Rueckgabecodes bleiben unveraendert. Der
Messbenchmark setzt denselben Schutz vor jedem Start. Keine Registry- oder
globale WER-Aenderung. Vererbung wird nur mit einem harmlosen Python-Kind
geprueft, nicht durch einen weiteren absichtlich ausgeloesten Absturz.
Referenz: [Microsoft SetErrorMode](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode).
Die Desktop-Fehler bleiben bis zur sicheren gezielten Diagnose offen; kein
erneuter Gesamtlauf und kein Abschwaechen des Baselinegates.
Dialogsperre samt harmloser Kindvererbung nachgewiesen: 5 PASS/0,069 s
(`cpp-baseline-noninteractive-verified.log`). Keine nativen Crashfaelle erneut
gestartet. Der erste versteckte Python-Aufruf lieferte keine nutzbare
Standardausgabe; der Launcher erfasst jetzt beide Streams explizit und gibt
den unveraenderten Kindstatus weiter. Das leere Erstlog bleibt erhalten,
wird aber nicht als Nachweis verwendet. Baseline und Migration bleiben offen;
wegen der zwei Desktop-Gatefehler kein Kandidatencommit und keine Transition.

Gezielte sichere Diagnose vom 6. September: Explorer kompiliert unveraendert
mit demselben GCC, endet nach 0,531 s mit Status 3 und erfasstem Assertiontext
ohne Dialog (`cpp-baseline-explorer-diagnosis.log`). Ursache ist die veraltete
Operationsmaske in `test/test_desktop_explorer_host.c:544`: MOVE/COPY/LINK
statt der seit `d53c421e` vorhandenen zusaetzlichen LAYOUT-Operation.
Dieser Commit erweiterte den Drag-Vertrag und dessen eigenen Hosttest,
nicht jedoch diese Explorer-Erwartung. LAYOUT ist nur lokale Iconpositionierung,
keine zusaetzliche VFS-Move-Autoritaet. Der Startup-Harness verwendet
`#define main static desktop_application_main`; aus `int main(...)` wird
das von GCC mit -Werror abgewiesene `int static ...`. Eine vorangestellte
statische Funktionsdeklaration plus reine Namensumbenennung erhaelt die
interne Bindung ohne Warnungsabschaltung.
Die gezielte Reparatur benoetigt ausschliesslich diese beiden bestehenden
Testdateien; sie liegen ausserhalb von R3.16a.allowed_files. Daher noch keine
Test- oder Produktionsaenderung, keine Scopeerweiterung, kein erneuter
Gesamtgate-Lauf und kein Commit. Naechster erforderlicher Schritt ist die
ausdrueckliche Freigabe dieser Testharness-Erweiterung. Die exakte
Maskenpruefung und alle bisherigen Assertions/Fristen bleiben dabei Pflicht.

## R3.15 abgenommen: maxlength und native Formulargeometrie

Aktuell: **alle acht Hostgruppen, beide Referenzen und alle fuenf Gastgates
bestanden** am finalen Kandidaten auf `34670757`. R3.15 ist done;
`R3.16-ring3-cpp-runtime` ist als naechstes Paket active, noch nicht
implementiert. Lokaler Implementierungscommit nach diesen Gates; kein Push.
Der native Formularschnitt schliesst maxlength, passende intrinsische
Controlmasse und getrennte Indikatoren. Vollstaendige Formular-/Google-
Ergebnisseiten- oder JavaScript-Kompatibilitaet bleibt ausdruecklich offen.
Toolchain: `python test/test_user_program_toolchain.py -v`, 21 PASS/132,033 s
(`r315-toolchain.log`). Referenzen `test-reist-package.ps1 -Target vmware
-Video vga` PASS/47 s (`20260906-170338-package-vmware-vga.log`) und
`-Target qemu -Video vga` PASS/49 s
(`20260906-170510-package-qemu-vga.log`). Alle Logs unter `build/codex-agent/`.
Reale Forms-Gastprobe PASS (ca. 75 s laut Logzeitstempeln): zusaetzliche
Maxlength-Refusal-/State-Marker, Mausrad, Reflow, Reset, drei Ablehnungen ohne
HTTP-Abgabe, exakter GET, Workerfehler und Wiederherstellung bis Close.
`r315-runtime-forms.log` und `.browser.log` bleiben separat erhalten.
Die unveraenderten QEMU-Pixel wurden verlustfrei als
`r315-native-controls.png` gespeichert und visuell geprueft: kompakte Buttons,
Textarea, Checkboxhaken, Radiokreise und Selectpfeil. Kein echtes Dropdown.
Public-Gastprobe PASS/91,206 s (`r315-runtime-public.log` und `.browser.log`).
Input-Gastprobe PASS/130,368 s (`r315-runtime-input.log` und `.browser.log`),
reale Tastatureingabe, Navigation, Crash/Restart und Konsole.
Browser-Gastprobe PASS/87,924 s (`r315-runtime-browser.log` und `.browser.log`).
Ressourcen-Gastprobe PASS/74,833 s (`r315-runtime-resources.log` und `.browser.log`).
Alle Gastbefehle nutzen `test-reist-runtime.ps1 -Target qemu -Video vga -Mode`
mit `runtime-desktop-browser-forms`, `runtime-desktop-browser-public`,
`runtime-desktop-browser-input`, `runtime-desktop-browser` bzw.
`runtime-desktop-browser-resources`, jeweils genau ein finaler Lauf nacheinander
ohne parallele Builds. Die urspruenglichen Worker-/Ketten-/Probe-Fristen bleiben
unveraendert. Alle bisherigen Fehlerlogs sind erhalten.
R3.16 fuer den danach freigegebenen C++-SDK-/Runtime-Schnitt ist mit Scope und
Host-/Referenz-/Gastgates definiert, nicht implementiert. R3.6b bleibt mit allen
bisherigen Anforderungen queued, keine behauptete VMware-Pointerabnahme.
Direkte Diffpruefung: 20 zuordenbare Dateien innerhalb des freigegebenen Scopes,
kein Public-ABI-Drift, keine geloeschte Cleanup-/Ablehnungspruefung, keine
Quoten-/TLS-/Kernel- oder Fristlockerung. Die Formwire-Erweiterung ist privat
versioniert; alte Formdatensaetze bleiben ueber den getesteten v1-Adapter lesbar.

Das nachfolgende Nutzer-Ja autorisiert den vorhandenen Buildadapter samt
Parser-/Pin-/Patchtests. Nur die obsolete numerische CR->LF-Abbildung wird
nach unveraenderter Archivpruefung mit exaktem Einzelkontext entfernt.
Zusaetzliche Parsergates sind vor Implementierung eingefroren; alle bisherigen
Gates, Fristen und Schutzgrenzen bleiben erhalten. Die 14 zuordenbaren
Kandidatendateien auf `34670757` werden fortgesetzt; keine fremden Aenderungen.

Gezielte Parserregression reproduziert den Fehler vor der Korrektur:
`python test/test_html_engine.py -v`, FAIL/3,767 s
(`r315-parser-before.log`). Danach derselbe vollstaendige Test inklusive
numeric-cr, sonstiger C1-/NUL-Abbildung und bisheriger Fehlerpfade:
PASS/3,407 s (`r315-parser-final.log`).
`python test/test_html_engine_build.py -v`: 5 PASS/0,338 s
(`r315-parser-patch.log`), unveraendertes Archiv, Einzelpatch, fehlender/
doppelter/geaenderter Kontext sowie falscher Archiv-/Sidecarpin.
`python test/test_css_engine.py -v`: PASS/32,638 s
(`r315-native-parser-repair.log`), alle Modi einschliesslich neuer Geometrie-,
Raster-, Textarea-Limit- und Legacy/v2-Wireassertionen. Alle urspruenglichen
Gates bleiben erhalten; unveraenderte erfolgreiche Forms-/GUI-/Laufzeitquell-/
Public-Hostgruppen werden nicht grundlos wiederholt. Der Parseradapter wird
von diesen vier Gruppen nicht eingebunden.

Die vier unveraenderten Hostgruppen sind konkret:
`python test/test_browser_forms.py -v` (PASS/0,794 s,
`r315-maxlength-host-repair.log`), `python test/test_gui_browser_source.py -v`
(8 PASS/32,325 s, `r315-final-test_gui_browser_source.log`),
`python test/test_browser_runtime_source.py -v` (24 PASS/3,135 s,
`r315-final-test_browser_runtime_source.log`) und
`python test/test_browser_public_navigation.py -v` (2 PASS/1,099 s,
`r315-final-test_browser_public_navigation.log`). Ihre geprueften Produktions-
und Testquellen wurden seit dem jeweiligen erfolgreichen Lauf nicht geaendert.

### Erhaltene Vorgeschichte und negative Regressionen

Vorheriger Zwischenstand: **an Parser-Paketgrenze blockiert, kein Commit**.
Der Nutzer bestaetigt die Reihenfolge Formularreparatur -> abgenommenes C++-
Userspace-SDK -> schrittweise Browsermigration. Die neun zuordenbaren Dateien
auf `34670757` wurden fortgesetzt; keine fremden Aenderungen.
Native Geometrie verwendet nun intrinsische Beschriftungs-/size-/rows-/cols-
Masse ohne doppelten Vollbreitenhintergrund. Radio/Checkbox besitzen getrennte
Indikatoren, Select einen Pfeil; Fokus ersetzt diese nicht mehr durch ein
Textrechteck. Das ist weiterhin kein Dropdown-/neuer-Inputtyp-Nachweis.
Die reale Formularprobe wurde fuer maxlength=5, verweigertes sechstes Zeichen,
unveraenderten Wert und Reset erweitert; noch nicht ausgefuehrt.

`python test/test_css_engine.py -v`: FAIL nach 33,915 s
(`r315-native-host.log`). Alle neuen Geometrieassertionen bestehen, aber
`a&#13;&#10;b` ergibt im Textarea-Modell `a\n\nb` statt `a\nb`.
Die begrenzte getrennte Real-Code-Diagnose reproduziert bereits im rohen
Hubbub-Baum `61 0a 0a 62`, vor jeder Formularprojektion
(`r315-textarea-raw-diagnostic.log`, 3,680 s). Das gepinnte Archiv enthaelt in
`libhubbub-0.3.8/src/tokeniser/tokeniser.c:3039` eine explizite Umwandlung von
numerischem CR (0x0D) in LF (0x0A). Der WHATWG-Standard verlangt fuer diese
Referenz den CR-Codepunkt; die Textarea-API normalisiert erst anschliessend.
Beide legitimen LF und das urspruengliche CR/LF sind nach diesem Parserverlust
nicht mehr unterscheidbar. Keine nachtraegliche Zusammenfassung von LF-Paaren,
keine Testabschwaechung und kein zweiter HTML-Parser als Umgehung.

Noetig waere eine eng begrenzte, gepruefte Upstream-Korrektur im vorhandenen
Extraktions-/Buildadapter `scripts/build_html_engine.py`, samt Pin-/Patch-/
Verhaltenstests. Dieser Produktionspfad liegt ausserhalb R3.15.allowed_files;
vor Aenderung gestoppt und explizite Umfangsfreigabe erforderlich.
Unabhaengige Gruppen bestehen: Laufzeitquelle 24 Tests/3,135 s, Public 2/1,099 s,
GUI 8/32,325 s (`r315-final-test_*.log`). Der vorherige unveraenderte Forms-
Hostnachweis bleibt 0,794 s PASS. Toolchain, Referenzen und Gastgates wurden
nach dem CSS-Fehler nicht gestartet. 14 zuordenbare Paketdateien bleiben
uncommitted; Diagnoseskripte/-logs unter ignored `build/codex-agent/` erhalten.

Neuer Nutzerfehler nach sauberem Commit `5214c019`: Google startet, aber die
Suchabgabe meldet "Formular nicht unterstuetzt". Die gespeicherte Antwort vom
6. September 10:37 UTC zeigt `maxlength=2048` am Feld `q`; der vorhandene
Projektor blockiert damit das ganze Formular. R3.15 implementiert dieses
normale Eingabelimit mit versioniertem privaten Modell, Unicode-Unitzaehlung,
Reset/Reflow und realem GET-Nachweis. Kein Google-Sonderfall und keine
JavaScript-/Kernel-Erweiterung. Gates/Scope stehen vor Implementierung fest.
R3.6b bleibt mit unveraenderter Abnahme zurueckgestellt. Noch kein neuer
Implementierungs- oder Live-Suchergebnisnachweis.

Der nachfolgende Nutzerauftrag verlangt zunaechst den vollstaendigen
HTML-Formularinventar-Abgleich; er steht im
`BROWSER_FORM_INTERACTION_CONTRACT.md` unter Formularinventar. Basis ist
`5214c019`: acht von 22 input-Typen mit Teilfunktion, vierzehn fehlen, weitere
Elemente besitzen nur Datenmodell oder Kindtext statt vertrauter Widgets.
Der UI-Zusatzauftrag wurde sichtbar auf die vorhandene CSS-Layoutdatei
erweitert; keine neuen Inputtypen oder Kernelautoritaeten stillschweigend
freigegeben. Aktueller Kandidat auf Vertragscommit `34670757` bleibt erhalten:
private Formversion 2, maxlength-Unitzaehlung, begrenzte Eingabe/Dirty/Reset und
Legacy-Wire-Adapter. Der Ersttest reproduziert die Google-Ablehnung
(`r315-maxlength-before.log`). Nach Korrektur eines const-Fehlers im Testdouble
besteht der gezielte Formularhosttest in 0,794 s
(`r315-maxlength-host.log`, `r315-maxlength-host-repair.log`).
Der neue UI-Regressions-Erstlauf scheitert erwartbar am ueberbreiten Blockhintergrund
(`r315-native-before.log`, 33,235 s); alle anderen CSS-Modi bestehen in diesem
Lauf. Zum damaligen Erstlauf war die CSS-Produktionsdatei noch unveraendert.
Der fortgesetzte Stand und die neue Abnahme stehen oben.

## R3.14 abgenommen: Dokumentaufnahme und Browser-Ladepfad

Alle eingefrorenen Gates bestehen am finalen Kandidaten auf `a9229d14`.
R3.14 ist `done`; naechstes aktives Paket ist
`R3.6b-vmware-pointer-pinned-mutex`. Dessen Implementierung/Abnahme wurde in
diesem Lauf nicht begonnen. Kein Push. Alle frueheren Fehlerlogs bleiben erhalten.

Ergebnis: private 1-MiB-Dokumente, weitere Zeichenkodierungen, Schriften bis
64 CSS-Pixel und echte 8192-Byte-CSS-/Bildadressen mit begrenzter Spawnaufnahme.
Browserdownloads vermeiden den temporaeren Dateiumweg durch private Bulk-IPC.
Die unabhaengige 128-Sektor-PIO-Lesequote behaelt Schreib-/Journal-/AHCI-Grenzen.
Demand-backed Legacy-Worker und Reflow nur belegter Ressourcen entfernen
unnoetige Mapping-/Null-/Kopierarbeit: HTMLWORK-Mappingpayload von 6946468 auf
2752100 Byte bei unveraenderten Dateibytes und Heapbudgets. Alle temporaeren
Timingausgaben sind entfernt. Keine Frist-/Reserve-/TLS-Lockerung.

### Finale Abnahme vom 6. September

Die Befehle stehen unveraendert in `automation/reist-s03b.toml`; die sechs
Loader-Hostbefehle und der Benchmark wurden vor der Loaderreparatur ergaenzt.
Geaenderte Quellen wurden nach Reparatur neu geprueft; erfolgreiche Gates
unveraenderter Quellen bleiben separat nachgewiesen, nicht redundant wiederholt.
Alle Logs liegen unter `build/codex-agent/`.

| Hostbefehl (`python test/<Name>.py -v`) | Ergebnis | Sekunden | Log |
| --- | --- | ---: | --- |
| test_browser_navigation_source | PASS 4 | 1,750 | r314-ipc-navigation.log |
| test_html_engine | PASS 1 Gruppe inkl. Legacy-OOM | 3,817 | r314-lazy-legacy-host.log |
| test_css_engine | PASS 1 Gruppe inkl. Raster/Workerfehler | 48,440 | r314-sparse-css-host.log |
| test_browser_runtime_source | PASS 24 | 3,004 | r314-sparse-runtime-host.log |
| test_gui_browser_source | PASS 8 | 40,795 | r314-sparse-gui-host.log |
| test_browser_forms | PASS 1 Gruppe | 0,733 | r314-resumed-test_browser_forms.log |
| test_user_program_toolchain | PASS 21 | 141,351 | r314-ipc-toolchain.log |
| test_browser_public_navigation | PASS 2 | 1,214 | r314-sparse-public-host.log |
| test_process_arguments | PASS 1 Gruppe | 0,457 | r314-loader-arguments-host.log |
| test_ata_multiple | PASS 1 Gruppe | 0,611 | r314-loader-final-ata-host-repair.log |
| test_ata_source | PASS 8 | 0,001 | r314-loader-test_ata_source.log |
| test_ata_lba48_source | PASS 5 | 0,003 | r314-loader-test_ata_lba48_source.log |
| test_ahci_source | PASS 7 | 0,003 | r314-loader-test_ahci_source.log |
| test_reist_undo_journal | PASS 11 | 0,012 | r314-loader-test_reist_undo_journal.log |
| test_fs_host | PASS 14, kein Skip | 6,651 | r314-loader-final-fs-host.log |

Referenzbefehle: `.\scripts\test-reist-package.ps1 -Target <Target> -Video vga`.
VMware PASS 57 s (`r314-sparse-package-vmware.log`,
`20260906-160249-package-vmware-vga.log`); QEMU PASS 48 s
(`r314-sparse-package-qemu.log`, `20260906-160405-package-qemu-vga.log`).
Beide enthalten alle finalen Produktionsaenderungen.

`python scripts/run_qemu_benchmark.py --image build/reist-os.img --min-read-kib-per-sec 400 --log build/codex-agent/r314-loader-benchmark.log`:
PASS 34,490 s Hostzeit; 755,16 KiB/s Lesen, 17,40 KiB/s Schreiben, volle
256-KiB-Bytepruefung, fsync, Cleanup und Shell. Unveraenderter Loaderstand.

Gastbefehle: `.\scripts\test-reist-runtime.ps1 -Mode <Mode> -Target qemu -Video vga`.
Die fuenf finalen Laeufe liefen einzeln ohne parallele Builds und alle bestehen.
Jeweils separate Browserdetails unter dem gleichen Namen mit `.browser.log`.

| Mode | Ergebnis | Hostsekunden | Log |
| --- | --- | ---: | --- |
| runtime-desktop-browser-public | PASS Encoding/lange Ressourcen/Redirect/Raster/Close | 94,449 | r314-sparse-runtime-public.log |
| runtime-desktop-browser-input | PASS Eingabe/Navigation/Crash/Restart/Konsole | 129,445 | r314-sparse-runtime-input.log |
| runtime-desktop-browser | PASS | 88,627 | r314-sparse-runtime-browser.log |
| runtime-desktop-browser-resources | PASS | 77,282 | r314-sparse-runtime-browser-resources.log |
| runtime-desktop-browser-forms | PASS echte Eingabe/GET/Reflow/Ablehnung/Reset/Recovery/Close | 75,106 | r314-sparse-runtime-browser-forms.log |

Public beweist beide Rastermarker und exakt sieben HTTP-Requests inklusive
langer CSS-Weiterleitung, Import und Bild, anschliessender Dokumentweiterleitung
und sauberem Close innerhalb der unveraenderten 30-Sekunden-Gastprobe.
Workerfristen bleiben fuenf Sekunden. Sieben Workerstarts brauchen 6077 ms;
keine Quoten- oder Autoritaetserweiterung zur Umgehung der Abnahme.
Das ist kontrollierte HTTP-Evidenz, kein Live-Website-Nachweis fuer Intracom
oder Google. Erfasste Seiten bestehen getrennte Host-Rastertests. JavaScript,
POST, Cookies und Kompression bleiben offen; Top-Level-URL-/Link-/Formfelder
behalten ihre 256-Byte-Grenzen. Keine Vollbrowser-Kompatibilitaet behauptet.

### Erhaltener Diagnose- und Reparaturverlauf (historischer Status)

Aktuelle ausdrueckliche Wiederaufnahme: Nutzer fordert Fehlerkorrektur fuer
eine sauber committbare Version. Die 49 zuordenbaren Dateien auf `a9229d14`
bleiben erhalten; keine fremden Aenderungen. Die begrenzte instrumentierte
Pipeline-Reproduktion misst 0,3-0,5 s Renderarbeit, bis 0,5 s IPC-Empfang und
0,8-1,5 s Workerstart. Sie ist Diagnose, kein Abnahmeerfolg
(`r314-pipeline-diagnostic.browser.log`, 93,306 s Hostzeit, unveraenderte Fristen).
Ein isolierter -O2/-Os/-Oz-Buildvergleich spart nur 20480 Dateibytes und wird
nicht als Produktionsaenderung uebernommen. Reparatur im vorhandenen Worker-
Lebenszyklus: Die bisher in jedem neuen Worker unbenutzte 4-MiB-Legacy-BSS-Arena
entfaellt. Auch Legacy nutzt den bestehenden demand-backed Prozessprovider
mit weiterhin 4 MiB Budget; Input-/Knoten-/Attributgrenzen bleiben gleich.
Ressourcen resetten nur den Header und nullen jeden neu aufgenommenen Record
vollstaendig. Reflow kopiert nur belegte Records, CSS-Bytes und Bildpixel nach
Bereichsvorpruefung. Alte Records/Pixeltails bleiben unzugaenglich und werden
nicht ueber IPC publiziert. Alle temporaeren Pipeline-Ausgaben sind entfernt.
Regressions-Erstlaeufe scheitern am unnoetigen Vollpool-Reset bzw. der alten
statischen Arena. Nach Reparatur bestehen HTML (3,817 s, inklusive Legacy-OOM),
Public/Legacy-Bundle (1,214 s), Browserlaufzeit (24 Tests/3,004 s), CSS inklusive
Worker-Fault-/Quota-Faellen (48,440 s) und GUI (acht Tests/40,795 s).
Logs: `r314-lazy-legacy-*.log`, `r314-sparse-*.log`. Die anschliessende finale
Referenz-/Gastabnahme und Queue-Transition stehen oben.

Vorheriger Abschluss der Loader-Wiederaufnahme: **blockiert, kein Commit**.
Auch nach der einmaligen gezielten Idle-Reparatur scheitert
`.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-browser-public -Target qemu -Video vga`
nach 94,823 s Hostzeit am unveraenderten 30-Sekunden-Gastbudget. Die grosse
Legacy-kodierte Seite mit langer CSS-Weiterleitung, Import, Bild und Raster
besteht; auch die letzte Dokumentweiterleitung wird ausgefuehrt. Der letzte
HTML/CSS-Worker ist am Gesamtlimit jedoch noch aktiv (`phase=3`, `child=22`).
Sieben Worker-Spawns brauchen 6353 ms. Eine ausreichende Beschleunigung der
Gesamtkette durch den entfallenen Idle-Sleep ist damit nicht nachgewiesen.
Logs: `r314-idle-runtime-public.log` und `.browser.log` unter
`build/codex-agent/`. Die Paket-Stoppregel greift: keine weitere Reparatur oder
Gluecks-Wiederholung, keine Queue-Transition, keine erfolgreiche Live-Website-
oder vollstaendige Browserabnahme behauptet. Die vier nachfolgenden Browser-
Gastgates wurden nach dem Fehler nicht gestartet. Test-VM ist beendet.
Alle 15 gezielten Hostgruppen bestehen (betroffene neu: 24 Laufzeittests in
2,721 s, acht GUI-Tests in 34,266 s). Beide finalen Referenzen bestehen:
VMware 70 s, QEMU 48 s (`r314-idle-package-*.log`). Der separate unveraenderte
Loaderbenchmark bleibt mit 755,16 KiB/s, Bytepruefung, fsync und Cleanup PASS.
49 zuordenbare geaenderte/neue Dateien liegen im freigegebenen Paketumfang;
Diff-Whitespace-Pruefung besteht. Alle frueheren Fehlernachweise bleiben erhalten.

Verlauf dieser Wiederaufnahme vor der abschliessenden Stopentscheidung:

Die finalen Loader-Referenzen bestehen: VMware 51 s, QEMU 48 s
(`r314-loader-package-*.log`). Der hinzugefuegte bytegepruefte QEMU-Benchmark
besteht mit 755,16 KiB/s Lesen, fsync, Cleanup und Shell-Rueckkehr
(`r314-loader-benchmark.log`, 34,490 s Hostzeit). Der anschliessende Browser-
Public-Gast scheitert nach 94,104 s Hostzeit weiterhin am unveraenderten
30-Sekunden-Probenbudget: grosse Seite, CSS/Import/Bild und abschliessende
Weiterleitung gelingen, der letzte Parser ist aber noch aktiv. Sieben Spawns
brauchen 6562 ms (`r314-loader-runtime-public.browser.log`). Einmalige gezielte
Nachbesserung: Die Hauptschleife darf nach erfolgreich uebergebenen privaten
IPC-Paketen nicht zusaetzlich auf einen Idle-Timer warten. Nur tatsaechlicher
Paketfortschritt unterdrueckt diesen Sleep; bestehender Yield, Paketbudget,
EAGAIN-Schlaf und absolute Fristen bleiben. Der reale Hosttest reproduziert
den unnoetigen Sleep und besteht nach Reparatur mit allen 24 Laufzeittests
(2,721 s; `r314-idle-regression-before.log`, `r314-idle-runtime-host.log`).
Die anschliessende Reparatur-Gastabnahme ist oben dokumentiert.

Erneute ausdrueckliche Freigabe des Nutzers ("ok") erweitert die Reparatur um
Prozessstart-/Ladekosten. Die separate Shellmessung trennt HTMLWORK-Dateilesen
1048/1026 ms von Speicheraufbau 9/6 ms; CURL 525/504 ms gegen 1/3 ms
(`r314-loader-diagnostic.log`). Der vorhandene zusammenhaengende FAT32-Lesepfad
uebernimmt bislang die 20-Sektor-Schreib-/Journalquote. Neue reine PIO-Lesequote:
128 Sektoren; Backendhinweis, erneute Pruefung unter ATA-Mutex, AHCI und aktive
Journaltransaktionen weiterhin 20. Keine groesseren Cache-/DMA-Puffer, keine
geaenderten Schreibbefehle, Quoten der Undo-Records, Fristen oder Loader-
Validierung. R3.14-Pfade und Gates wurden vor Implementierung explizit ergaenzt.
Real-Code-Hosttests bestaetigen 128-Sektor-Bytes, unaligned Ziel/Guards,
Restblock, LBA28/48, frische Identifikation, Fehler ohne Cachepublikation,
Partitionen, AHCI, Journalprioritaet und veralteten Kapazitaetshinweis.
Der neue Ersttest scheitert wie erwartet an der alten 20-Sektor-Grenze.
Nach Korrektur fehlender Basis-/Masterwerte im neuen Adapter-Testdouble
besteht das ATA-Gate (0,611 s). Die 14 Dateisystemtests bestehen mit echtem GCC
ohne Skip (6,651 s), einschliesslich zusammenhaengender/gemappt fragmentierter
Bytefolge sowie kleinerem Backendbudget. ATA/48-bit/AHCI/Journal-Quellgruppen:
8/5/7/11 Tests PASS. Logs: `r314-loader-*.log`.
Die erste instrumentierte Vergleichsmessung scheitert beim Storage-Start;
die zusaetzliche Diagnoseausgabe hinter Prozessfreigabe wird vor diese verlegt.
Der korrigierte Diagnosepfad erreicht die Shell und beide Wiederholungen:
HTMLWORK liest 650/623 ms, CURL 289/269 ms
(`r314-loader-batch-prepublish.log`). Alle temporaeren Kernel-Timingausgaben
sind danach entfernt. Ergebnisse der folgenden Referenzbuilds und Gastabnahme
stehen oben; dies ist noch kein Commit-/Live-Website-Nachweis.

Vorheriger Abschluss vor der Loader-Freigabe: **blockiert, kein Commit**.
Auch die einmalige gezielte IPC-Handoff-Reparatur verfehlt das eingefrorene
30-Sekunden-Gastbudget. Abnahmebefehl:
`.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-browser-public -Target qemu -Video vga`
Ergebnis FAIL nach 98,052 s Hostzeit; Log
`build/codex-agent/r314-ipc-handoff-runtime-public.browser.log`.
Die grosse HTTP-Seite samt Legacy-Encoding, langer CSS-Weiterleitung, Import,
langem Bild und echtem Raster erreicht `BROWSER_PUBLIC_LARGE_ENCODING_RASTER_OK`.
Danach endet die Probe in Phase 3, bevor die abschliessende Dokumentweiterleitung
abgenommen ist. Sechs gemessene Worker-Spawns brauchen insgesamt 8336 ms;
eine ausreichende Verbesserung der gesamten Kette durch Yield ist nicht belegt.
Die weitere Prozessstart-/Ladekostenanalyse bleibt offen. Nicht durch laengere
Fristen, geaenderte Fixtures oder einen zusaetzlichen Glueckslauf kaschieren.
Betroffene Hostgates nach Reparatur: 24 Laufzeittests/2,733 s und 8 GUI-Tests/
29,618 s PASS. Referenzen: VMware 57 s
(`20260906-150009-package-vmware-vga.log`), QEMU 49 s
(`20260906-150118-package-qemu-vga.log`) PASS. Die zuvor bestehenden
unveraenderten Hostgruppen bleiben als getrennte Nachweise erhalten.
Die vier nachfolgenden Gastgates wurden nach dem Fehler nicht gestartet.
Paket-Stoppregel greift; keine Queue-Transition, keine Implementierungsabnahme
und kein Live-Website-Erfolg behauptet. 42 geaenderte/neue Dateien liegen alle
innerhalb der freigegebenen Pfade; Diff-Whitespace-Pruefung besteht.

Aktuelle Wiederaufnahme: Nutzer fordert die Fehlerkorrektur fuer einen sauber
geprueften Commit. Der zuordenbare Kandidat auf `a9229d14` bleibt erhalten.
Eine getrennte, begrenzte CURL-/Paketmitschnitt-Diagnose misst bei 90000 Byte
5,28 s bis zum letzten Antwortbyte und weitere 8,55 s bis zum Verbindungsende
(`r314-transport-diagnostic-summary.log`). Der Browser ersetzt deshalb seinen
Datei-Schreib-/Lese-/Loeschumweg durch private CURL-Bulk-IPC. Vollstaendiges
HTTP-Framing, Nachrichtengrenzen, exakte Kindidentitaet, erfolgreicher Exit und
Reap bleiben Voraussetzung der Dokumentfreigabe. Abbruch sperrt zuerst den
Endpunkt; jeder neue Hop bekommt einen neuen. Keine TCP-/Dateisystemerweiterung
im Kernel, keine gelockerten Zeitlimits oder Quoten. CLI-Dateien behalten ihren
atomaren Abschluss, stdout bleibt streamend.
Betroffene Hostgruppen bestehen: Navigation 4 Tests/1,750 s, Browserlaufzeit
24 Tests/2,671 s, GUI 8 Tests/28,342 s, oeffentliche Aufnahme 2 Tests/1,576 s
(`r314-ipc-*.log`). Neue Verhaltensfaelle beweisen ungueltige Frames ohne
Teilpublikation, endliche Delegationsrennen, Abbruch waehrend der Uebergabe,
erfolgreiche Daten vor noch nicht erfolgtem Reap und spaeteren Kindfehler.
Referenzbuild VMware besteht (95 s, `20260906-145006-package-vmware-vga.log`);
weitere Abnahme laeuft. Noch kein Implementierungscommit/keine Queue-Transition.
Toolchain: 21 Tests/141,351 s; QEMU-Referenz 49 s
(`r314-ipc-toolchain.log`, `20260906-145202-package-qemu-vga.log`).
Der erste IPC-Gastlauf beweist `BROWSER_PUBLIC_LARGE_ENCODING_RASTER_OK`,
einschliesslich langer CSS-/Import-/Bildkette, scheitert aber am Gesamtbudget
direkt beim Beginn der abschliessenden Weiterleitung, Phase 3. Sechs Worker-
Spawns brauchen 7718 ms (`r314-ipc-runtime-public.browser.log`).
Gezielte Reparatur: Nach einem erfolgreichen privaten IPC-Paket uebergibt der
Browser die CPU an bereite Tasks, statt fuer jeden Ein-Slot-Paketwechsel einen
Idle-Timertick zu bezahlen. Kein Yield bei EAGAIN, weiter hoechstens acht Pakete
pro UI-Turn; normale Leerlaeufe schlafen. Der reale Hostregressionstest zeigt
den vorherigen Ein-Paket-Stillstand (`r314-ipc-handoff-before.log`) und besteht
danach zusammen mit allen 24 Laufzeithosttests (2,733 s,
`r314-ipc-handoff-runtime-host.log`). Die gezielte Gast-Reparaturabnahme ist offen.
Die folgenden Abschnitte erhalten die bisherigen Fehler- und Reparaturnachweise.

Nutzerauftrag: die gemeldeten Browserfehler zusammen beheben. Sauberer
Ausgangspunkt `3e94cf00`; R3.6b wird mit unveraenderten Gates zurueckgestellt.
Gemessen: Google 83259 Byte und ISO-8859-1 gegen bisher 65536 Byte/UTF-8;
Intracom 16707 Byte plus Stylesheet mit 36-Pixel-Ueberschrift gegen maximal
32 Pixel. VMware meldet ausserdem TCP-Timeout und generische Ladeablehnung.
Der neue private Navigationsvertrag und die eingefrorenen Gates stehen in
`automation/reist-s03b.toml` und `BROWSER_PUBLIC_NAVIGATION_CONTRACT.md`.
Kandidat auf Vertragscommit `c285e903` liegt uncommitted im sichtbaren Worktree:
private 1-MiB-Dokumentaufnahme, Charset-Adapter, 36/64-Pixel-Raster und begrenzte
Transportdiagnostik/-wiederholung. Sieben eingefrorene Hostgruppen bestehen;
CSS-Gate scheitert noch am neuen UTF-16-BOM-Fall. Keine Referenzbuild- oder
Gastabnahme des Kandidaten, keine Queue-Transition, kein Implementierungscommit.
Logs: `build/codex-agent/r314-*.log`.

Neuer ausdruecklicher Nutzerauftrag: lange Ressourcen wirklich unterstuetzen,
nicht bloss ueberspringen. Der bisherige Kandidaten-Fallback fuer lange URLs
ist damit keine akzeptierte Loesung. Googles erfasste externe CSS-Adresse hat
261 Zeichen. Neben privaten 256-Byte-Feldern begrenzen
`kernel/syscall/syscall_table.c:3031` und `kernel/proc/process.c:724` jedes
Startargument auf 256 Byte inklusive NUL. Eine durchgaengige normale CURL-
Argumentuebergabe braucht deshalb einen begrenzten, getesteten Spawn-/Initial-
Stack-Vertrag, nicht nur groessere Browserarrays. Beide Kerneldateien liegen
ausserhalb R3.14.allowed_files. Umfangsfreigabe/Vertragsanpassung erforderlich;
an dieser Paketgrenze gestoppt. Kein stilles Erweitern des Kernels und keine
Abnahme der gespeicherten Seiten als erfolgreicher Live-Netzwerktest.

Fortsetzung ausdruecklich freigegeben: Nutzer bestaetigt "ja mach alle noetigen
anpassungen". Der R3.14-Vertrag wird um begrenzte Spawnargumente und echte lange
Ressourcen-URLs erweitert. Der zuordenbare Kandidat bleibt erhalten, die alten
Gates und ihre Ergebnisse bleiben nachvollziehbar. Keine neue Abnahme behauptet.

Vertragserweiterung lokal committed als `a9229d14`; Implementierung weiter
uncommitted. Kernel-Snapshot/Initial-Stack bestehen reale Hosttests inklusive
8192-Zeichen-Argument, E2BIG, EFAULT, ENOMEM, Kopierfehler, exakt erhaltenen
Argumentbytes und 16-KiB-Stackreserve (`r314-arguments-gate.log`). Der breite
URL-Adapter besteht den 8192-Byte-Grenztest, alte 256-Byte-Resolversemantik bleibt
erhalten (`r314-long-public-host-repair.log`). Ressourcen-Metadaten v3 lesen
alte v2-Records, private Szene v4 transportiert lange Bildquellen getrennt vom
unveraenderten semantischen Dokument. CURL-Request/Redirect-Puffer und der
kontrollierte Lang-URL-Gasttest sind angepasst, aber noch nicht abgenommen.

Erneuter Paketstopp: Das CSS-Gate nach gezielter Kompilierkorrektur scheitert
am neuen `public-opaque`-Test, Zeile 128 (`r314-css-long-compile-repair.log`).
Er erwartet die Farbe `#123456` der geladenen p-Regel fuer den enthaltenen Link;
die vorhandene UA-Regel `a:link` setzt dessen eigene Farbe. UTF-16 und die
anderen CSS-Teilfaelle melden in diesem Lauf keinen Fehler. Erwartung/Fixture
bei ausdruecklicher Wiederaufnahme gezielt korrigieren, keine Gateabschwächung:
die geladene lange CSS-Quelle muss an einem passend selektierten Element
nachgewiesen werden. Keine Referenzbuilds, Gastabnahme, Queue-Transition oder
Implementierungscommit. Aktueller Kandidat ist kein fertiger Browser-Release.

Fortsetzung nach ausdruecklichem Nutzerauftrag: `public-opaque` selektiert nun
das Linkelement selbst mit der geladenen Autorenregel. Kein abgeschwaechter
Farbvergleich. Alle neun eingefrorenen Hostgruppen bestehen, einschliesslich
UTF-16, 8192-Byte-URLs, alter Ressourcenformate und Kernel-Kopierfehlern.
Logs: `r314-css-resumed.log`, `r314-public-final.log`,
`r314-arguments-final.log`, `r314-resumed-test_*.log`.
Gespeicherte Intracom-/Google-Antworten bestehen HTML/CSS und Rasterdiagnostik
an drei Scrollpositionen (`r314-captures-final.log`); dies ist kein Live-
Netzwerknachweis. Referenzbuilds bestehen: VMware 9 s
(`20260906-140409-package-vmware-vga.log`), QEMU 48 s
(`20260906-140440-package-qemu-vga.log`). Die fuenf Gastgates stehen noch aus.
Kernel-SHA des alten Pakets ist wegen der freigegebenen Argumentreparatur
keine Unveraendertheitsbehauptung fuer diesen Kandidaten.

Erster kontrollierter Gastlauf scheitert bereits bei der Workspace-Aufnahme
(`r314-runtime-public.log`, `r314-runtime-public-first.browser.log`). Das reale
i386-Compilerlayout zeigt Decoderarena-Offset 17537516, also nur vierfach
statt achtfach ausgerichtet. Kein Nachweis von Speichermangel. Gezielte
Reparatur: explizites C11 `_Alignas(8)` samt Compile-Time-Layout-/36-MiB-
Quotenpruefung und Hostassertion. Betroffene 24 Hosttests bestehen in 2,709 s
(`r314-workspace-runtime-host.log`). Erneute Referenzen bestehen: VMware
56 s (`20260906-141019-package-vmware-vga.log`), QEMU 48 s
(`20260906-141115-package-qemu-vga.log`). Gast-Reparaturabnahme laeuft;
Fristen, Decoder-Admission und Prozessreserven bleiben unveraendert.

Erneuter eingefrorener Gastfehler nach dieser gezielten Reparatur (96 s
Hostlauf, `r314-runtime-public-alignment-repair.log` und zugehoeriges
`.browser.log`): Workspace, Font, Fenster, initiales HTML, Bild und Reflow
bestehen; `BROWSER_PUBLIC_READY` wird erreicht. Die grosse HTTP-Antwort
erreicht den Worker bei Gast-ms 57210, eine Ressourcen-Weiterleitung wird
bestaetigt. Nach erneutem Workerstart bei ms 66616 endet das 30-Sekunden-
Gesamtbudget der Browserprobe in Phase 2 mit noch lebendem Kind 15. Das ist
kein nachgewiesener Worker-Crash und kein Argument-Admissionfehler; das
eingefrorene Gesamt-Gate besteht trotzdem nicht. Die initialen Workerstarts
liegen bei ms 36934/41035; vier gemessene Worker-Spawns brauchen zusammen
6489 ms. Weitere serielle Lade-/Parsekosten sind gezielt zu untersuchen,
ohne Fristen oder Assertions zu lockern. Die exakte lange HTTP-Kette samt
neuem Bild ist noch nicht abgenommen; die vier nachfolgenden Gastgates
wurden nach dem ersten fehlschlagenden Gate nicht gestartet.

Paketregel `a frozen gate fails after one focused in-scope repair` greift:
Implementierung hier gestoppt, kein Commit und keine Queue-Transition.
Alle Aenderungen und Fehlerlogs bleiben sichtbar erhalten. Fortsetzung
erfordert eine ausdrueckliche Wiederaufnahme; Live-Zugriff aus dem Browser
auf Intracom/Google bleibt ebenfalls unbestaetigt.

Ausdrueckliche Wiederaufnahme durch den Nutzer: gezielte Beschleunigung ohne
gelockerte Fristen. Realer CURL-Hostregressionstest zeigt 62 Dateischreibaufrufe
fuer 90000 Byte aus 1460-Byte-Netzfragmenten (`r314-buffer-before.log`).
Ein fester privater 128-KiB-Puffer reduziert denselben Fall auf einen Aufruf;
270000 Byte benoetigen drei. Unveraendert: vollstaendige Short Writes,
HTTP-Framing-/Bytelimits, Close/Rename erst nach Erfolg und Unlink bei Fehler.
Neue Regressionen pruefen partielle Writes, Fehler beim letzten Flush,
Verwerfen unvollstaendiger Antworten, leeren Folgetransferzustand und weiterhin
streamendes stdout. Nach einer gezielten Deklarationskorrektur besteht die
betroffene Navigations-Hostgruppe (`r314-buffer-navigation-repair.log`,
4 Tests/1,250 s). Beide Referenzbuilds bestehen: VMware 52 s
(`20260906-142146-package-vmware-vga.log`), QEMU 61 s
(`20260906-142238-package-qemu-vga.log`). Der erneut betroffene Toolchain-
Nachweis besteht mit 21 Tests/132,315 s (`r314-buffer-toolchain.log`).
Die neue Gastmessung startet erst nach Abschluss paralleler Builds.

Gast-Reparaturgate bleibt rot (`r314-runtime-public-buffer.log`,
`r314-runtime-public-buffer.browser.log`, Hostlauf 98,313 s). Nach lokaler
Seite/Bild/Reflow erreicht die grosse HTTP-Antwort den Worker bei ms 56917;
der nachfolgende Ressourcenworker startet bei ms 64032. Beim unveraenderten
30-Sekunden-Probenende laeuft in Phase 2 Kind 16 mit Status `Lade Stylesheets`.
Vier Worker-Spawns summieren sich auf 5452 ms. Der Schreibaufruf-Rueckgang
ist hostseitig bewiesen, eine ausreichende End-to-End-Beschleunigung nicht.
Keine weitere Gastabnahme, kein Commit und keine Queue-Transition nach dem
erneuten Fehler der gezielten Reparatur.

Read-only Folgeinventur: `drivers/net/tcp_socket.h` und der SDK-Vertrag
begrenzen Sendeaufrufe auf 512 Byte und Empfang auf 2048 Byte. Der vorhandene
`tcp_socket_send` wartet fuer jeden Aufruf auf ACK, bevor er die Bytezahl
zurueckgibt. `CURL send_all` muss eine knapp 8-KiB-Adresse daher in mindestens
16 nacheinander bestaetigte Segmente zerlegen. Das ist eine weitere konkrete
strukturelle Grenze, noch kein gemessener Beweis ihres Anteils am Timeout.
TCP-Produktion/SDK liegen ausserhalb R3.14.allowed_files und der freigegebenen
Ring-0-Argumentreparatur. Keine stille Konstantenerhoehung und kein Ausbau
des bestehenden monolithischen TCP-Pfades als Architekturvorbild. Ein separater
OS-Schnitt braucht zuerst einen freigegebenen standardnahen Sende-/Empfangs-,
Budget-, Generation-/Recoveryvertrag und passende Host-/Gastnachweise unter
Erhalt der Ring-3-Zielgrenze. Implementierung hier nach Paketregel gestoppt;
Quellstand und saemtliche Fehlevidenz bleiben erhalten.

## R1.2d abgenommen: VMware-/QEMU-Standard auf 1 GiB RAM

Neuer ausdruecklicher Nutzerauftrag nach Browser-Abnahme `9d4635fa`:
VMX `memsize=1024` und QEMU `-m 1024M` fuer die normalen Start-/Testpfade.
Die virtuelle Festplatte bleibt 512 MiB. Keine Kernel-/Allocatoraenderung,
keine gelockerte Prozessquote oder Reserve. Explizite kleine RAM-Profile
bleiben erhalten. Der bereits nachgewiesene 1-GiB-Direct-Map-Vertrag gilt
unveraendert; dies fuehrt keine Unterstuetzung oberhalb von 1 GiB ein.
Alle Paketgates bestehen. R1.2d geht auf done; das naechste Queuepaket
R3.6b wird active, seine reale VMware-Pointerabnahme bleibt offen.

Implementiert im sichtbaren Worktree auf sauberem Vertragscommit `e1f7fd9c`.
Die Regression weist vor der Aenderung `512M != 1024M` nach
(`build/codex-agent/r12d-memory-default-before.log`). Nach der freigegebenen
Testprofil-Reparatur bestehen alle fuenf eingefrorenen Hostgruppen, 109 Tests
ohne Skip: RAM-Vorgaben 5/0,106 s, Imagegenerator 18/5,083 s,
QEMU-Smoke 47/18,931 s, Desktop-Smoke 15/3,411 s, Browser-Runtime 24/2,718 s.
Logs: `build/codex-agent/r12d-test_*.log` sowie fuer die erneut betroffenen
RAM-/QEMU-Gruppen `r12d-display-test_*.log`. Unbetroffene Evidenz bleibt gueltig.

Beide Referenzbuilds bestehen: VMware 51 s
(`20260906-121913-package-vmware-vga.log`), QEMU 46 s
(`20260906-122004-package-qemu-vga.log`), jeweils unter `build/codex-agent`.
Beide erzeugten VMX-Dateien (`build/reist-os.vmx` und
`build/vmware/reist-os/reist-os.vmx`) enthalten genau `memsize = "1024"`.
RAW und VMware-Flat-Disk bleiben jeweils 536870912 Bytes gross.
Kernel-SHA256 bleibt unveraendert
`AB7639D9043E5D4EA3AECB35F1F2949974D69D57E14F4D36C6747FF0AF606E10`;
finales QEMU-Image:
`DF327168E161C52A806B5206D43C6565009B116EE49AB68FFD537516C2B8FF59`.

Der erste Standardgast meldete `Memory: 1023 MiB detected, 1023 MiB managed,
996 MiB free` und `BOOT_OK`. Der erste Lauf von `-Mode normal` scheiterte nach 16 s
jedoch an `TEST_FAIL UNICODE_RASTER`: `-nodefaults` stellt ohne explizite
Grafikkarte kein PCI-Display bereit. Der unveraenderte verpflichtende
GTEST-Grafiktest bekommt deshalb `driver=-19 fallback=-19`. Dies ist kein
Speichermangel. Evidenz: `r12d-runtime-normal.log` und
`r12d-runtime-normal.serial.log` unter `build/codex-agent`.

Das unabhaengige Gate `-Mode runtime-desktop-browser-forms` mit vorhandenem
Grafikprofil besteht bei 1 GiB (81,875 s): reale Eingabe, Scrollen/Resize,
exakter GET, Ablehnung ohne Request, Reset, Worker-Recovery und Close.
Log/Screenshot: `build/codex-agent/r12d-runtime-browser-forms*`.

Der Nutzer hat die Korrektur des allgemeinen QEMU-Testprofils ausdruecklich
freigegeben. Ein echter CLI-Regressionstest weist vor der Reparatur das
fehlende `-vga vmware` nach (`r12d-display-before.log`). Vollstaendige
GTEST-Laeufe waehlen jetzt das bereits bestehende SVGA-Profil; reine Boottests
und direkte Desktop-Runner behalten ihre eigene Geraetewahl. Ein explizites
`--vmware-vga` bleibt auch bei `--boot-only` wirksam und erzeugt nur ein Display.
Keine Kernel-/Treiber-/ABI-Aenderung, keine gelockerte Frist oder Assertion.

Das zuvor fehlgeschlagene Gate `-Mode normal -Target qemu -Video vga`
besteht nach dieser einen gezielten Reparatur (72,479 s), mit 1023 MiB
verwaltetem RAM und 995 MiB frei beim Boot, einschliesslich
Unicode-Raster, kompletter GTEST-Sequenz und Shell-Rueckkehr. Nachweise:
`build/codex-agent/r12d-display-runtime-normal.log` und `.serial.log` sowie
`20260906-122842-runtime-guest-smoke.log`. Alte Fehlerevidenz bleibt erhalten.
Die Image-/Kernel-Hashes wurden erneut verglichen und sind unveraendert;
Builds und Browser-Formulargate wurden deshalb nicht unnoetig wiederholt.
Alle 17 Kandidatenpfade liegen im freigegebenen Umfang. Keine spaetere
Paketimplementierung und kein Push.

## R3.13 abgenommen: native statische GET-Formulare

Alle eingefrorenen Paketgates bestehen. HTML5-Projektion, privates
CSS3-Formularmodell, Unicode-Wertespeicher,
native Eingabeebene und explizite GET-Absendung sind verbunden. Kapazitaeten:
16 Formulare, 256 Controls, 512 Optionen, je 128 KiB Strings/Werte. Reflow
erhaelt Werte nur bei identischem Modell und Navigationsgeneration; ein neuer
Dokumentstand setzt sie zurueck. Keine Kernel- oder Surface-ABI-Aenderung.

Der neue QEMU-Formulartest bedient Felder, Mausrad, Resize, Ablehnung, Reset und
Absendung ueber echte Geraeteeingaben. Ein lokaler HTTP-Server prueft die genaue
GET-Query und ausbleibende Requests bei Ablehnung; danach Workerfehler und
neue Generation. Der reale Gastnachweis besteht auf dem finalen Image.
Vertrag: `../architecture/BROWSER_FORM_INTERACTION_CONTRACT.md`.
Ausgangspunkt ist R3.12 (`13f01adb`). R3.13 geht auf done; das naechste
Queuepaket R3.6b wird active, seine VMware-Pointerabnahme bleibt offen.
POST, JavaScript und vollstaendige moderne Webkompatibilitaet bleiben offen.

Wiederaufnahme vom Nutzer freigegeben, einschliesslich `browser_images.c`:
Das reale Transportfixture verwendet jetzt die aktuelle private Szenenversion.
Der Browser linkt die bestehende C-Laufzeit statt eigener doppelter Bytehelfer;
Decoder-Pin, Algorithmen und feste Arena sind unveraendert. Keine neuen Stubs.

Alle neun Hostgruppen bestehen: 149 Tests, 147 PASS, zwei bestehende SKIP.
Der erste parallele Toolchain-Lauf traf beim SDK-Aufbau die unveraenderte
60-Sekunden-Frist; exklusiv besteht die ganze Gruppe (21 Tests, 118,366 s).
Die finalen VMware-/QEMU-Referenzbuilds bestehen in 50/46 s
(`20260906-115849-package-vmware-vga.log`,
`20260906-115939-package-qemu-vga.log`); Kernel-SHA256 bleibt
`AB7639D9043E5D4EA3AECB35F1F2949974D69D57E14F4D36C6747FF0AF606E10`.

Der erste Formular-Gastlauf (96,757 s) belegt echte Feldeingabe ohne neue
Seitenbuffer und Werterhalt bei Mausrad-Scrollen. Resize blieb aus: Der
Testmarker erbte sieben Pixel `div`-Abstand und leitete einen falschen
Fenstergriff ab. Die gezielte Korrektur setzt `margin:0`; ein neuer echter
LibCSS-Test prueft den Messpunkt der verpackten HTML-Datei (CSS-Gruppe:
31,217 s, PASS). Die erneute Gastabnahme besteht; keine Frist oder Assertion
wurde abgeschwaecht. Fehlgeschlagene Evidenz bleibt erhalten.

Alle vier finalen Laufzeitgates bestehen in der eingefrorenen Reihenfolge.
Befehl jeweils `scripts/test-reist-runtime.ps1 -Target qemu -Video vga` mit:

- `-Mode runtime-desktop-browser-forms`: PASS (81,271 s), echte Feldeingabe,
  Mausrad-/Resize-Werterhalt und Fokus, drei Ablehnungen ohne Request, Reset,
  exakte GET-Query, neue Navigation, Workerfehler, Recovery und sauberes Close.
- `-Mode runtime-desktop-browser-input`: PASS (138,629 s), bestaetigte
  Einzeltasten, URL-Korrektur, lokale Navigation, Compositor-Absturz,
  antwortende Shell und frischer Desktop; kein Burst-Latenznachweis.
- `-Mode runtime-desktop-browser`: PASS (94,755 s), Bilder, Links, Scrollbar,
  CSS, echtes Mausrad/Resize, Worker-Fault-/Timeout-Recovery und Close.
- `-Mode runtime-desktop-browser-resources`: PASS (81,285 s), bestehende
  externe/importierte Stylesheets und Ressourcen-Recovery unveraendert.

Logs und Screenshots: `build/codex-agent/r313-final-runtime-desktop-browser*`.
Finales QEMU-Image SHA256:
`296064B028CDEDBF5C33F8EB4A15BB6D0C339D148E538D1FCDFCAF2BF9215113`.
Alle 29 geaenderten Pfade liegen im freigegebenen Paketumfang. Keine neue
Kernelautoritaet, keine abgeschwaechte Speicherresilienz und kein Push.

## R3.12 abgenommen: generationgebundene Eingabe und GTEST-Kinduebergabe

Alle eingefrorenen Paketgates bestehen. Der begrenzte Terminal-Mediator
prueft die Vordergrundgeneration vor jedem Entnehmen von Zeichen; Shell,
Desktop und GTEST verwenden explizite Uebergaben. Prozessende fenced diese
Autoritaet und verwirft alte Queuebytes vor der langsameren Bereinigung.
GUI-Fokus bleibt in Ring 3; Syscall 127 ist append-only. R3.12 geht auf done,
der vorbereitete R3.13-Vertrag fuer native GET-Formulare auf active. In diesem
Lauf keine Formularimplementierung; VMware-Pointerabnahme bleibt offen.

Der Nutzer hat `guest_test.c` samt Regression explizit in den Paketumfang
aufgenommen. Die unveraenderten 32 Kandidatenpfade sind weiterhin zugeordnet;
kein fremder Source-Writer. GTEST identifiziert sein unreaptes Desktopkind,
delegiert an dessen exakte PID/Generation und prueft nach erfolgreichem
Unicode-Test die Rueckkehr seiner Vordergrundautoritaet. Fehlgeschlagene
Identitaetsabfrage oder Uebergabe fuehren ueber Kill und den bestehenden
Wait-/Reappfad zum Testfehler, ohne Wiederholung oder Schonung der Probe.
Kernel, Desktop, ABI, Unicode-Assertions und Gastfristen bleiben unveraendert.

Die reale GTEST-Funktion laeuft im Hostfixture zusammen mit dem echten
Terminal-Mediator. Acht Faelle pruefen Erfolg, Spawnfehler, Identitaetsfehler,
veraltete Generation, abgewiesene Uebergabe, fehlgeschlagene Probe, Waitfehler
und einen mit Kill konkurrierenden Exit. Kindtasten duerfen weder GTEST noch
Shell entnehmen; bei Ende werden alte Bytes verworfen und die vorherige
Generation bekommt die Eingabe zurueck. Vor Reparatur scheitert die echte
Funktion nachweislich an fehlender Uebergabe (0,924 s,
`r312-gtest-handoff-before.log`). Danach `python test/test_terminal_input.py -v`:
2 PASS (0,798 s); `python test/test_shell_source.py -v`: 28 PASS/2 bestehende
SKIP (0,233 s). `python test/test_user_program_toolchain.py -v`: 21 PASS
(114,082 s). Referenzbuilds ueber `test-reist-package.ps1 -Target ... -Video vga`:
VMware PASS (8,825 s), QEMU PASS (45,676 s).

Finale Hostbilanz: sieben Gruppen, 151 Tests, 149 PASS und zwei bestehende
Shell-Skips. Die unveraenderten erfolgreichen ABI-, Desktop-, Browserlaufzeit-
und Prozessspeicher-Gruppen behalten ihre unten dokumentierte Evidenz.
Alle fuenf finalen Laufzeitgates wurden auf den neu gebauten Images einmal
in der eingefrorenen Reihenfolge ausgefuehrt. Befehl jeweils
`scripts/test-reist-runtime.ps1 -Target qemu -Video vga` mit:

- `-Mode runtime-desktop-browser-input`: PASS (154,348 s), echte URL-Korrektur
  und lokale Navigation in zwei Generationen, Compositor-UD2, antwortende
  Shell, frischer Desktop und saubere Konsolenrueckkehr.
- `-Mode runtime-desktop-browser`: PASS (92,312 s), Bilder, Links, Scrollbar,
  CSS-Pixel, Resize, Mausrad ab/auf, Worker-Absturz/Timeout, Recovery und Close.
- `-Mode runtime-desktop-browser-resources`: PASS (82,157 s), externe/importierte
  Cascade, Zyklen/Dubletten, Fehlerbehandlung, Abbruch, frischer Reload und Cleanup.
- `-Mode runtime-desktop`: PASS (90,113 s), `exit-vfs-relaunch-exit-shell`,
  Start 24454 Gast-ms, Neustart 23907 Gast-ms.
- `-Mode memory-resilience`: PASS (75,553 s einschliesslich separatem Build,
  Gastabschnitt 71 s), resiliente Seiten rekonstruiert, `DESKTOP_UNICODE_OK`,
  `TEST_STAGE UNICODE_RASTER_OK`, vollstaendiges `TEST_OK` und Shellprompt.

Neue Belege stehen unter `build/codex-agent/r312-gtest-*`; Speicher-Detail
`20260906-105420-runtime-guest-smoke-memory-resilience.log`. Fruehere Fehler
bleiben unveraendert erhalten. Der Tastaturgast beweist generationstreue
Zustaendigkeit, keinen Burst-Durchsatz und keinen oeffentlichen HTTPS-Abruf.
Der absichtliche Compositorfehler benutzt den Diagnoseprozess, nicht einen
automatischen Supervisor-Crash-/Restart-Nachweis. JavaScript bleibt offen.

QEMU-Image SHA256 vor/nach den finalen Desktopgates unveraendert:
`C0BC4A07949BEE5E78321101FE0721DA862734F50D5EA96F6D4ADB553C27A210`.
Kernel `AB7639D9043E5D4EA3AECB35F1F2949974D69D57E14F4D36C6747FF0AF606E10`,
Konfiguration `0618FA93CD8CF57B055498C7D05531200AC9E9252E39DDB08628733C75AB80A7`.
Separates Speicher-Proofimage:
`999F16E984BCA8ADC31BFDEC0A9AC65E7FBD9BE506170DEAECB6F31DCEF1E5DB`,
Kernel `6D8DEB7876B3278A274E1A03869CDFA598347461CF258E010361A1F23DF3B905`,
Konfiguration `15C2F95547CE9BB0D2F954543795163FEB0BCBC767A40B2155FA8E2C3721349B`.

### Vorheriger Stopp: GTEST-Kinduebergabe ausserhalb des Pakets

Der Nutzer hat die Fortsetzung des dokumentierten Kandidaten freigegeben.
Bei Wiederaufnahme waren Baseline `e2b61b9a`, Gastcode und die unten genannten
Image-/Kernel-/Konfigurationspruefsummen unveraendert. Keine fremden Source-
Aenderungen; eine spaetere externe Buildartefakt-Aenderung ist unten abgegrenzt.
Der Runner behaelt jetzt die urspruengliche absolute 180-s-Gesamtfrist fuer
beide Eingabesitzungen; der erste Desktopstart bleibt zusaetzlich auf seine
bisherige Frist begrenzt. Keine Frist wird beim Neustart erneuert. Ein
Verhaltenstest fuehrt die echten Fristzuweisungen und den echten Dispatch
auch bei spaetem Start und abgelaufener Gesamtfrist aus.

Finale Hostbilanz: sieben Gruppen, 151 Tests, 149 PASS, zwei bestehende
Shell-Skips. Nur die betroffene Browserlaufzeit-Gruppe wurde erneut
ausgefuehrt: `python test/test_browser_runtime_source.py -v`, 24 PASS
(2,799 s), `build/codex-agent/r312-resume-browser-host.log`.
Die unveraenderten erfolgreichen Hostgruppen und beide Referenzbuilds
bleiben gueltig; keine unnoetige Wiederholung derselben Builds.

Laufzeitgates, jeweils `scripts/test-reist-runtime.ps1 -Target qemu -Video vga`:

- `-Mode runtime-desktop-browser-input`: PASS (141,236 s). Zwei verschiedene
  Desktopidentitaeten (PID 7/Generation 1, PID 12/Generation 2), jeweils
  echte URL-Korrektur und lokale Navigation; nach absichtlichem Ring-3-UD2
  antwortet die konkurrierende Shell auf HELP. Der frische Desktop beendet
  sich sauber, danach antwortet die Shell erneut.
- `-Mode runtime-desktop-browser`: PASS (92,629 s). Bilder, Links, native
  Scrollbar, CSS-Pixel, Resize, beide Mausradrichtungen, isolierter Worker-
  Absturz/Timeout, anschliessende Recovery und Close.
- `-Mode runtime-desktop-browser-resources`: PASS (80,057 s). Externe/importierte
  Stylesheets, Dubletten/Zyklen, alte Seite bei fehlenden Ressourcen, Abbruch,
  frischer Reload, temporaere Dateien entfernt und Close.
- `-Mode runtime-desktop`: PASS (86,148 s), `exit-vfs-relaunch-exit-shell`,
  normaler ueberwachter Start 23170 Gast-ms, Neustart 22706 Gast-ms.
- `-Mode memory-resilience`: FAIL (107,908 s einschliesslich separatem Build;
  Gastabschnitt 18 s), `desktop: terminal ownership unavailable`, danach
  `TEST_FAIL UNICODE_RASTER`. Der vorherige eigentliche Seiten-Rebuild-
  Bootnachweis meldet `REIST_RESILIENT_PAGE BOOT_PROOF_OK objects=2`; das
  Gesamtgate bleibt trotzdem fehlgeschlagen.

Read-only-Diagnose: Die laufende Shell delegiert an GTEST. Dessen
`test_unicode_raster()` in `userspace/programs/guest_test.c` startet einen
direkten Desktop-Kindprozess und wartet ohne explizite Eingabeuebergabe.
Dieser ist weder aktueller Besitzer noch ueberwachter Compositor und wird
deshalb korrekt abgewiesen. Es handelt sich nicht um fehlenden Shellstart
oder einen Fehler des resilienten Seiten-Rebuilds. Das neue Terminal-API
muss im aufrufenden GTEST integriert werden, einschliesslich exakter
Kindgeneration, Fehlercleanup und Rueckgabe; keine Probe-Ausnahme im Kernel
oder Desktop. `guest_test.c` liegt ausserhalb der erlaubten Dateien. Vor
dieser weiteren Reparatur ist eine explizite Scope-Erweiterung samt realem
Regressionstest erforderlich. Kein Retry, kein Commit und kein done/
active_id-Wechsel. R3.13 ist nur als nachfolgender Formularvertrag queued,
nicht implementiert oder aktiv; VMware bleibt zurueckgestellt.

Neue Belege: `r312-resume-*` unter `build/codex-agent/`; fruehere Fehlerbelege
bleiben erhalten. Letztes Gate-Detail:
`20260906-103444-runtime-guest-smoke-memory-resilience.log`.
Der Eingabegast laedt keine oeffentliche HTTPS-Seite und bestaetigt jede
Taste einzeln. Er beweist Eingabezustaendigkeit und Recovery, keinen
Burst-Tastaturdurchsatz oder automatischen Supervisor-Neustart nach Crash.

Artefaktabgrenzung: Das QEMU-Hauptimage blieb ueber alle vier Desktopgates
hashidentisch (`8BBB...1876`, voller Digest unten). Ausserhalb dieser Laeufe
wurden um 10:26:11 Uhr Hauptkernel und Hauptkonfiguration fuer VMware neu
erzeugt; ihr derzeitiger Hash ist nicht der unten dokumentierte QEMU-Build.
Ein lesender Prozesscheck fand vor dem Speicherlauf keinen fremden Build
mehr. Keine fremden Quellen wurden veraendert oder Artefakte zurueckgesetzt.
Die Speicherpruefung erstellte regulaer ihr eigenes Image unter
`build/memory-resilience/`: SHA256
`C45F6D76FBC75886F33EA3DA92BBEDB29E24B8F12B9C1AF858017789FB22A320`,
Kernel `6D8DEB7876B3278A274E1A03869CDFA598347461CF258E010361A1F23DF3B905`,
Konfiguration `15C2F95547CE9BB0D2F954543795163FEB0BCBC767A40B2155FA8E2C3721349B`.

### Vorheriger Stopp: falsche Testfrist nach Tastatur-/Crash-Teilnachweis

Kandidat: feste Vordergrundkette, Zulassung unter derselben Sperre wie das
Entnehmen von Tasten, explizite Shell-Uebergabe, ueberwachter Compositor-
Erwerb und idempotentes Fencing bei Prozessende. Syscall 127 ist append-only;
alte Leseadapter, Surface-Fokus und Maus bleiben erhalten. Der neue echte
Tastatur-Gasttest verlangt URL-Korrektur, Navigation, Ring-3-UD2, frische
Desktopgeneration und Konsolenrueckkehr. Noch kein R3.12-Abnahmeclaim/Commit.

Damals alle sieben Hostgruppen: 150 Tests, 148 PASS, zwei bestehende Shell-Skips.
Terminal-Verhalten 2 PASS (29,240 s einschliesslich kaltem Compiler-Cache),
Shell 28 PASS/2 SKIP (0,247 s), ABI 5 PASS (0,246 s), Desktop 59 PASS
(0,572 s), Speicher 10 PASS (3,283 s). Nach dem gezielten Reparaturversuch:
Browserlaufzeit 23 PASS (3,486 s), Toolchain 21 PASS (111,870 s),
VMware-Referenzbuild PASS (53,282 s), QEMU-Referenzbuild PASS (45,548 s).
Die unveraenderten Hostgruppen wurden nicht erneut ausgefuehrt.

Erster neuer Tastatur-Gastlauf: FAIL (98,566 s), Phase 1. Sein Ready-Marker
kam vor Bild-/CSS-Reflow. Der einzige gezielte Reparaturversuch wartet auf
das abgeschlossene initiale Layout und bestaetigt jede echte Taste einzeln,
ohne Replay oder Verlaengerung der 30-s-Browserfrist. Kein Burst-Durchsatz-
Nachweis wird daraus abgeleitet.

Wiederholung: FAIL (104,404 s), aber alle 46 PS/2-/Surface-Tastencodes,
`BROWSER_INPUT_ADDRESS_OK`, `BROWSER_INPUT_NAVIGATION_OK`, Browser-Close,
echter Compositor-UD2 und `HOST_TERMINAL_EXCEPTION_CONSOLE_OK` sind vorhanden.
`https://intracom.at` wurde bearbeitet, nicht aus dem Netz geladen; die
Navigation ging deterministisch auf `/htdocs/index.html`. Der Kernel bleibt
aktiv und die Shell beantwortet HELP nach dem Fehler.

Konkreter verbleibender Blocker: `run_qemu_runtime_desktop.py` ueberschreibt
die angeforderte absolute 180-s-Gesamtfrist mit `deadline = desktop_deadline`
(90 s ab erstem Start). Die Wiederholung endet deshalb waehrend des
Font-Ladens der zweiten Desktopinstanz, nicht an einer zweiten Gast-Exception.
Nach einem fehlgeschlagenen Gate trotz eines Reparaturversuchs verlangt das
aktive Paket einen Stopp. Es gab keinen weiteren Implementierungsversuch,
keinen Commit und keine Queue-Weiterschaltung. Die vier nachfolgenden
Laufzeitgates wurden in R3.12 noch nicht ausgefuehrt.

Belege: `build/codex-agent/r312-*`, final
`r312-repair-input-runtime.log`, `r312-repair-input-guest.log` und das frische
`r312-repair-input.ppm`. Achtung: `r312-input.ppm` ist ein versehentlich
kopiertes altes R3.11-Bild (09:24 Uhr), kein Bildnachweis des ersten R3.12-Laufs.
QEMU-Imagepruefsumme des unveraenderten Kandidaten:
`8BBB2E351FB0591FFEB5C4ED46D9DE19C3AD3261A4F972414B8C561D22DD1876`.
Kernel `AB7639D9043E5D4EA3AECB35F1F2949974D69D57E14F4D36C6747FF0AF606E10`,
Buildkonfiguration `0618FA93CD8CF57B055498C7D05531200AC9E9252E39DDB08628733C75AB80A7`.

## R3.11 abgenommen: externe Stylesheets und isolierte Recovery

Die gezielt reparierte R3.11-Implementierung besteht alle eingefrorenen
Gates. Externe Stylesheets und verschachtelte Imports werden durch echte
LibCSS-Kaskade verarbeitet; nur der Browserkoordinator beschafft Bytes.
Native Bedienung, Bilder, Mausrad, alte Seite bei Fehlern und Workerisolation
bleiben nachgewiesen. Kernel und globale Speichergrenzen sind unveraendert.

Hostnachweis: neun Gruppen, 90 Tests, 88 PASS und zwei bestehende Shell-Skips.
Die echte CSS-Gruppe enthaelt zusaetzlich 25 Engine-/Worker-Unterfaelle.
Final geaenderte Gruppen: Browserlaufzeit 21 PASS (2,291 s), GUI-Browser
8 PASS (32,061 s), Toolchain 21 PASS (112,037 s). Unveraenderte bestandene
Gruppen bleiben gueltig: Ressourcen (0,688 s), CSS (31,793 s), HTML5
(4,832 s), libc (1,315 s), Navigation (1,032 s), Shell (0,067 s).

Referenzbuilds ueber `test-reist-package.ps1 -Target ... -Video vga`:
VMware PASS (12 s), QEMU PASS (56 s). QEMU-Gastgates ueber
`test-reist-runtime.ps1 -Target qemu -Video vga`:

- `-Mode runtime-desktop-browser`: PASS (95,193 s), alle bisherigen
  Render-/Bild-/Eingabe-/Link-/Scroll-/Resize-/Mausrad-Assertions sowie echter
  Worker-UD2, Timeout, Recovery und Close.
- `-Mode runtime-desktop-browser-resources`: PASS (80,776 s), echte USB-
  Testauswahl, externe/importierte Cascade-Pixel, Dubletten/Zyklen, fehlende
  CSS-Datei bei erhaltener alter Seite, Abbruch, frischer Reload mit neuen
  Pixeln/Generation, beide temporaeren Dateien entfernt und Close.

Finale Belege unter `build/codex-agent/r311-reap-*`, Laufzeit-Hostlog
`r311-exit-race-host-final.log`; fruehere Fehlerbelege bleiben erhalten.
Identisches QEMU-Image vor/nach beiden Gastgates:
`C5CDFC593EA48CF67EAE41957B81F70E68843A89288024AE1274D0284C9C5E35`.
Kernel `4F221764847917123DB817745008B7A4C0EAE184CDA7B2D4D8C6AF29A644348F`,
Buildkonfiguration `0618FA93CD8CF57B055498C7D05531200AC9E9252E39DDB08628733C75AB80A7`.

Nachweisgrenzen: Die Ressourcen-Gastprobe ist lokal/NIC-los; HTTP/CSS-MIME,
Redirects, Downgradeabwehr und Quoten sind hostgeprueft, keine zusaetzliche
oeffentliche HTTPS-Gastabnahme. Kein vollstaendiger moderner Browser, keine
Formulare, Cookies, POST oder JavaScript. Worker-/Dateiladezeiten bleiben
Performance-Schuld: acht Spawns im Ressourcenlauf benoetigen zusammen
13293 ms, trotz nur 621 ms fuer dessen 13 Dateilesevorgaenge.

R3.11 geht auf done. Als naechstes separates Paket ist R3.12 fuer
generationgebundene Terminal-Eingabezustaendigkeit definiert und aktiv:
Die Recovery-Shell darf dem fokussierten Browser keine Zeichen stehlen.
Hier wurde nur der Paketvertrag angelegt, keine solche OS-Implementierung.
VMware-Pointerabnahme bleibt mit allen offenen Gates bewusst zurueckgestellt.

### Wiederaufnahme: Worker-Exit und Reaping

Der Nutzer hat die gezielte Diagnose/Reparatur mit `mach weiter` freigegeben.
Alle 35 vorhandenen Pfade sind dem eigenen R3.11-Kandidaten zugeordnet;
Scope, Kernel, Schutzpruefungen und eingefrorene Gates bleiben unveraendert.
Ein neuer First-Failure-Datensatz nennt einmalig Exitpfad, Fehlercode,
Probephase, Kindidentitaet und fehlgeschlagene Beobachtung. Fehler ausser
`-5` werden nicht mehr faelschlich als Prozess-Erfolg zurueckgegeben.

Read-only-Inventur von `task_exit_status`, `process_begin_exit` und
`process_terminate` sowie der reale Browser-Hosttest zeigen ein konkretes
Rennen: IPC wird waehrend Exitbereinigung vor dem Zombie-Commit entzogen.
Ein erneuter Kill wird dann abgewiesen, obwohl Prozess/PID/Generation noch
korrekt gepinnt sind. Der bisherige Browser behandelt diesen Zwischenstand
als Identitaetsverlust und beendet sich. `r311-exit-race-regression-before.log`
reproduziert exakt dieses Browserende. Die Reparatur laesst nach erneuter
Eigentuemer-/Generationspruefung nur den bestehenden Kindprozess bis zum
festen Ein-Sekunden-Reapbudget auslaufen. Kanal bleibt gesperrt, keine
weiteren Killversuche, keine neuen Kinder und keine spaeten Szenenbytes.
Das Budget wird beim ersten Fence gesetzt und beim Browsercleanup nicht
erneuert. Ein festhaengendes Kind bleibt ein expliziter Timeoutfehler.

Hosttests decken normalen Abschluss, dauerhaft abgelehnten Kill,
Uptime-Ueberlauf, erste Fehlerdiagnose und unveraenderte Fremdgenerations-
Ablehnung ab. Der erste neue Wrap-Fixture fehlte die reale Spawn-Pollzeit;
der Test initialisiert diese jetzt wie die Produktion. Final 21 PASS
(2,291 s), `r311-exit-race-host-final.log`. Die anschliessende vollstaendige
Abnahme ist oben dokumentiert.

### Vorheriger Stopp: Browserende nach Worker-Fault

Finaler Selektorstand: 21 Browserlaufzeit-Hosttests PASS (2,297 s),
VMware-Referenzbuild PASS (51 s), QEMU PASS (44 s). Die neue USB-Testauswahl
ist damit hostgeprueft, aber noch nicht im Ressourcengast nachgewiesen.
Der zuerst auszufuehrende normale Browsergate scheitert nach 106,994 s:
Rendern, Bilder, Adresse, Links, Scrollbar und Transport-Exit bestehen;
nach dem beabsichtigten Ring-3-UD2 des dritten HTMLWORK folgt jedoch direkt
`BROWSER_CLOSE_OK`, ohne `BROWSER_HTML5_FAULT_CONTAINED_OK`. Es fehlen die
anschliessenden Timeout-/Recovery-/CSS-/Resize-/Mausradnachweise. Der
Close-Marker allein ist ausdruecklich kein PASS. Kein Kernelpanic im Log.

Belege: `r311-selector-browser-runtime.log`,
`r311-selector-browser-guest.log`, `r311-selector-browser.ppm`.
Image-SHA256 vor/nach Lauf unveraendert:
`6FFCF48FFEC74161228B38E1B8B4A230A96C970B1737DDF024794ED07F9D2DFA`.
Kernel und `.windows-build-config.json` behalten die vorherigen Digests.
Es lief kein paralleler Build; QEMU ist beendet. Das Read-only-Inventar
grenzt den noch unbewiesenen Ursprung auf Browser-Exitpfade bei Kindidentitaet,
Wait bzw. Surface-Empfang/Rendern ein. Das vorhandene Log nennt deren
konkreten Status nicht; keine unbelegte IPC- oder Kernelursache behaupten.

Die eingefrorene Stop-Regel greift nach der fokussierten Reparatur und dem
erneuten Gatefehler: keine weitere Source-Reparatur, kein Zufalls-Retry,
kein anschliessender Ressourcenlauf, keine Queue-Transition und kein Commit.
Alle 35 Kandidatenpfade bleiben sichtbar und dem R3.11-Scope zugeordnet.
Zur Wiederaufnahme ist zunaechst eine begrenzte Diagnose genau dieser
Exitentscheidung mit realem Hostregressionstest noetig; Schutzpruefungen und
Fristen duerfen nicht ignoriert werden. Fremde Subsystemdateien bleiben
ohne explizite Erweiterung ausgeschlossen.

### Wiederaufnahme: private Worker-Transferpuffer

Der Nutzer hat mit `mach weiter` die gezielte Speicherreparatur nach dem
zweiten Toolchainfehler freigegeben. Die 34 vorhandenen Pfade wurden als
eigener Kandidat wiedererkannt; kein neuer/fremder Source-Writer. Der neue
Queue-Vermerk erweitert weder allowed_files noch Gates oder Schutzgrenzen.
HTMLWORK haelt Eingabe und dekodiertes Bundle jetzt in einer festen,
separat zugelassenen privaten Ring-3-Allokation von 2230748 Byte statt BSS.
Der Beginn der Fuenf-Sekunden-Frist liegt vor der Allokation. Alle normalen
Returns laufen durch dieselbe Freigabe; Fault/Kill bleibt Aufgabe des
bestehenden generationstreuen Kernel-Reapers. Die C-Parserarena ist davon
getrennt, damit deren Initialisierung keine Transferbytes entwertet.
Tests fuer Allokationsfehler, Fristablauf, Guardbytes und paarige Freigabe
sind ergaenzt. CSS-Gate PASS (38,375 s), Toolchain 21 PASS (128,886 s),
VMware-Referenzbuild PASS (14 s), QEMU PASS (48 s). HTMLWORK belegt 6440292
Byte Programmregion bei unveraenderten 8388608 Byte Loaderlimit.

Der erste neue Browser-Gastgate erreicht CSS-Pixel und Resize, laeuft aber
in Phase 10 kurz vor dem Mausradnachweis in die unveraenderte 30-Sekunden-
Frist. Belege: `r311-memory-browser-runtime.log`,
`r311-memory-browser-failed-guest.log` und `r311-memory-browser-failed.ppm`.
1842/1890 ms Dokumentauftraege, 2902/3205 ms Reflows; sechs Spawns zusammen
8403 ms. Kein vollstaendiger Gast-PASS und kein Kandidatencommit.

Eine fokussierte in-scope Reparatur entfernt ungenutzte Metadaten vom
privaten Wireformat: Bundle v2 uebertraegt nur header + count Records +
length CSS-Bytes. Leere Bundles kosten 16 statt 33808 Byte und vermeiden
17 unnoetige Bulkpakete je CSS-Auftrag. Kapazitaeten, Identitaetspruefung,
private Reserven, Parser und Fristen bleiben unveraendert. Der reale
Pack/Unpack-Test prueft exakte Groessen, abgeschnittene/zusaetzliche Bytes
und Count-Ueberlauf. Ressourcen PASS (0,688 s), Browserlaufzeit 20 PASS
(3,423 s), echte CSS/Worker-Faelle PASS (31,793 s). Referenzbuilds und beide
Gastgates bestaetigen den kompakten Stand teilweise: VMware PASS (55 s),
QEMU PASS (48 s), vollstaendige normale Browserprobe PASS (98,382 s),
einschliesslich Resize, beider USB-Mausradrichtungen und Close.

Der erste Ressourcen-Gastlauf scheitert nach 94,740 s schon an der Auswahl:
`s` erscheint als Konsolenecho, kein `BROWSER_RESOURCES_STARTED`; stattdessen
laeuft die normale Probe bis zur vom Ressourcencontroller nicht bedienten
Resize-Phase. Codeinventur bestaetigt die konkurrierende Terminalqueue:
shell.c wartet beim abgekoppelten Desktop bewusst nicht, beide lesen
getchar. Kein CSS-Gastnachweis aus diesem Lauf. Belege bleiben als
`r311-compact-resources-*` erhalten. Eine fokussierte Reparatur ersetzt nur
den Testselektor durch die bestehende USB-Mausradstrecke nach Bereitschaft
beider Teilnehmer, exklusiv in der initialen Probephase. Normale Nutzung und
spaete Mausradassertions bleiben unveraendert; kein Fristreset, Retry oder
Kernel-/Compositor-Edit. Hosttests pruefen Bereitschaftsreihenfolge, einmalige
Injektion, zwingenden Gast-ACK und Selektorgrenzen. Die echte Terminal-
Foregroundautoritaet muss ein eigenes OS-Paket korrigieren, nicht ein
zufaellig wiederholter Tastendruck. Finale Builds/Gastabnahme noch offen.

Unten bleibt der vorangegangene Stopp unveraendert als Historie erhalten.

### Vorheriger Stopp: Worker-Programmregion beim Toolchain-Gate

Baseline ist die abgenommene R3.10-Implementierung `a86d4948`; die vom Nutzer
explizit freigegebene Antwortadapter-Erweiterung wurde vor dem sauberen
Kandidatenstart als `018f7d17` separat committed. Keine fremden Aenderungen,
Agenten, Worktrees oder Push. Aktiver Scope und eingefrorene Gates bleiben
`R3.11-browser-stylesheet-resources`.

Der Kandidat verwendet echte LibCSS-Imports mit privaten CSS2-Bundles,
generationstreuer Discovery/Acquisition/Reap-Folge, CSS-MIME-Pruefung,
Reload-Frische und Abbruch. Externe Bytes kommen nur ueber den bestehenden
Browserkoordinator/VFS/CURL, nie aus Parsercallbacks. 64 Ressourcen, acht
Importkanten, 256 KiB je Datei, 1 MiB CSS insgesamt, 32 MiB opt-in Workerheap;
fuenf Sekunden je Worker und absolute 30 Sekunden Akquisition bleiben fest.
Prozessspeicher-Resilienz und Kernel bleiben unveraendert.

Die Abnahme ist am wiederholten Toolchain-Gate gestoppt. Kein Implementierungscommit
und keine vollstaendige Browser- oder VMware-Abnahme behauptet. Erste echte
CSS-, Ressourcen- und Koordinatortests bestanden. Der erweiterte CSS-Test
hatte einen undeclared-`strcpy`-Compilerfehler; die fokussierte Korrektur
benutzt bestehendes memcpy und der CSS-Gate besteht danach (31,367 s).
Der erste Toolchain-Gate meldete einen gekapselten Compilerfehler ohne
Compilerstderr; der Test bewahrt dieses jetzt bei unveraendertem Exit-/
240-Sekunden-Vertrag. Fuer Chrome werden ausschliesslich seine vorhandenen
bounded Text-/Byte-Operationen verwendet, keine neue libc nachgebildet.
Alle Logs bleiben unter `build/codex-agent/r311-*` erhalten.

Finale acht Hostgruppen bestehen: Ressourcen 1 (0,692 s), echte CSS/Worker-
Engine 1 mit 23 Unterfaellen (31,367 s), HTML5 1 (4,832 s), libc 4 (1,315 s),
GUI-Browser 8 (32,953 s), Browserlaufzeit 20 (3,502 s), Navigation 4 (1,032 s),
Shell 29 (0,067 s, zwei unveraenderte Skips). Das sind 68 Tests, 66 PASS und
zwei Skips; keine Gastbehauptung aus dem gemockten Runner-PASS ableiten.

`python test/test_user_program_toolchain.py -v` scheitert nach der fokussierten
Korrektur erneut (115,335 s, 20 PASS/1 FAIL):
`ld.lld: error: user program exceeds the 8 MiB loader region`.
Vollstaendiger Beleg `r311-toolchain-repaired-host.log`, erster Fehler weiterhin
in `r311-user_program_toolchain-host.log`. Betroffen ist HTMLWORK mit den neuen
statischen Eingabe-/Bundle-Puffern plus der erhaltenen 4-MiB-Legacyarena.
`config/user_program.ld` zaehlt BSS zur 8-MiB-Programmregion. Die bereits
implementierten privaten Laufzeitheaps sind davon unabhaengig.

Gemaess Paket-Stop-Regel keine weitere Implementierung, keine Referenzbuilds,
keine QEMU-Laeufe, keine Queue-Transition und kein Kandidatencommit. Alle
34 geaenderten/neuen Pfade wurden gegen allowed_files geprueft. Der naechste
gezielte Reparaturansatz ist, die grossen Worker-Transferpuffer ueber die
vorhandene private Ring-3-Speicheradmission statt als statisches BSS zu halten,
mit Fehler-/OOM-/Reap-Pruefung. Loader-, Kernel-, Heapbudget- und Zeitgrenzen
muessen dafuer nicht angehoben werden. Erfordert die Freigabe zur Wiederaufnahme
nach dem zweiten Gatefehler, nicht das Verschieben oder Abschwaechen des Gates.

Die vorhandenen build-Artefakte werden nicht als Kandidatennachweis verwendet:
aktuelles Image-SHA256 `7BE9EB89FDFA96B20EBD82B6B18DE1185F490B68486E957F92CF2CB77DFEFE16`,
Kernel `DAEA0E46621168BD5044972CB920351589919D88BA63F7C6DD32E0A9722551BA`.
Sie unterscheiden sich vom historischen R3.10-Abnahmeimage; kein stilles
Wiederverwenden dieser alten Gastnachweise fuer den neuen Kandidaten.

## R3.10 abgeschlossen: echte CSS-Kaskade, Boxlayout und Mausrad

Die vollstaendige eingefrorene Browser-Gastabnahme besteht am 6. September
2026 in 94,94 Hostsekunden: Eingabe, Bilder, Links, native Scrollbar/Clipping,
Reload, Transportfehler, isolierter Worker-Fault/Timeout, Recovery, echte CSS-
Scanout-Pixel, USB-Maus-Resize, Mausrad ab/auf mit Paint-Nachweis und
`BROWSER_CLOSE_OK`, ohne nachfolgenden Fehlermarker. Die fuenf Sekunden pro
Layoutauftrag und 30 Sekunden fuer den Gastprobe bleiben unveraendert.

Alle neun gezielten Hostgruppen sind bestanden: 97 Tests, 95 PASS und zwei
unveraenderte Shell-Skips; zusaetzlich 58 Desktoptests PASS. Geaenderter Runner:
19 Browsertests PASS (2,335 s), `r310-pointer-host.log`. Unveraenderte
Referenzbuilds werden nach Digestpruefung wiederverwendet: VMware PASS
44,76 s (`20260906-011739-package-vmware-vga.log`), QEMU PASS 42,66 s
(`20260906-011901-package-qemu-vga.log`). Alle Nachweise liegen unter
`build/codex-agent/`; die vollstaendige Befehlsliste bleibt in der Queue.
Finale Gastbelege: `r310-pointer-runtime.log`,
`r310-pointer-accepted.browser.log`, `r310-pointer-config.json`,
`r310-pointer-digests-{before,after}.json`,
`r310-pointer-{css-scanout,image-fixture}.ppm` sowie die frischen Home-/Grip-
Zeiger-Scanouts. Alle frueheren Fehlversuche einschliesslich des vom Nutzer
erklaerten Standby-Laufs bleiben als negative Evidenz erhalten.

Image/Kernel/Config sind vor/nach der Abnahme identisch; qemu/vga ohne
Injection. Image-SHA256:
`97AEBD0CC759AB1DB1E6030951F6F37859D556479A7DA2B11D6E9C0121519845`.
Dokumentauftraege 1653/1673 ms, Bild-/Resize-Reflow 2404/2601 ms. Hoststeuerung
Resize 1169 ms, Rad ab 42 ms, Rad auf 1 ms. Beide Zeigerbarrieren bestehen mit
dem ersten frischen Scanout. Die kumulierten sechs Worker-Starts kosten noch
8320 ms, zwoelf Dateiladevorgaenge 5449 ms: der OS-Ladepfad bleibt messbare
Performance-Schuld; der schnellere Testcontroller behebt ihn nicht.

R3.10 wird auf `done` gesetzt. Als naechster Browser-Schnitt ist
`R3.11-browser-stylesheet-resources` definiert und aktiv, in diesem Lauf
nicht implementiert. Er ergaenzt externe Stylesheets/Imports ueber ein
begrenztes Ressourcenbuendel ohne Worker-Datei-/Netzwerkautoritaet.
Formulare, Cookies, POST, CSS-Bild-/Font-Ressourcen und JavaScript bleiben
offen; keine Behauptung eines vollstaendigen modernen Browsers oder einer
VMware-Laufzeitabnahme. R3.6b bleibt mit unveraenderten Anforderungen vertagt.

### Abschliessende Wiederaufnahme mit beobachteter Mausposition

Nutzerfreigabe zur Fortsetzung der verbleibenden Teststeuerungsreparatur.
Die 50 vorhandenen geaenderten Pfade sind dem Kandidaten zugeordnet, keine
fremden Aenderungen. Nur Runner, seine Browser-Regression und Dokumentation/
Queue werden geaendert; OS-Produktionscode und Referenzartefakte bleiben gleich.
Vor der Richtungsumkehr nach Homing und vor dem Resize-Button-down prueft der
Runner den vollstaendigen sichtbaren Schwarz-/Weiss-/Schattenpfeil an der
erwarteten Position. Native QMP-Screendumps haben quittierte Dateierzeugung
und pro Versuch einen frischen Namen; kein alter Scanout kann bestehen.
Jede Barriere hat maximal eine Sekunde und 16 Versuche innerhalb der bisherigen
absoluten Hostfrist. Keine neue Gastfrist, kein direktes Pointer-Setzen und
kein Entfernen der eigentlichen Resize-/Wheel-/Paint-/Close-Pruefungen.

Die reale Runner-Regression scheitert vor der Reparatur an den fehlenden
Beobachtungsbarrieren (`r310-pointer-behavior-red.log`) und besteht danach.
Der Pixeltest erzeugt seine Referenz unabhaengig aus der produktiven
Framebuffer-Zeigerform, nicht aus der Matcher-Konstante. Falsche Position,
beschaedigte Pixel, kaputte Groessen, fehlende Capture-ACKs und endlose
unpassende Bilder werden abgewiesen. Browser-Host: 19 PASS (2,335 s),
`r310-pointer-host.log`. Die unveraenderten VMware-/QEMU-Builds und Hostgruppen
werden wiederverwendet; Image/Kernel/Config wurden vor dem Gastlauf exakt
gegen die letzte Referenz geprueft. Die anschliessende vollstaendige Gastabnahme
ist bestanden; siehe Abschlussnachweis oben.

## R3.10: Abschluss blockiert an der quittierten Maus-Teststeuerung

**Vorheriger Abschluss: kein Commit; R3.10 blieb aktiv.** Nach der fokussierten QMP-Reparatur
scheitert die finale Gastabnahme (96,45 Hostsekunden) in Phase 9. CSS-Pixel,
Bilder, Eingabe, Links, Scrollbar, Reload und Worker-Fault-/Timeout-Recovery
bestehen. QMP quittiert die Mauskommandos, der Gast meldet `DESKTOP_MOUSE_OK`,
aber kein Resize/Reflow; Rad und Close werden in diesem Lauf nicht erreicht.
Die Steuerung braucht nur noch 1137 statt 4126 ms; daraus folgt keine
bestandene Interaktionspruefung und kein behobener Worker-Ladeengpass.

Finale Referenzbuilds: VMware PASS 44,76 s
(`20260906-011739-package-vmware-vga.log`), QEMU PASS 42,66 s
(`20260906-011901-package-qemu-vga.log`). Browser-Host 17 PASS (2,106 s),
ergaenzend Desktop 58 PASS (0,365 s); die acht unveraenderten gezielten
Hostgruppen bleiben wiederverwendbare Evidenz, insgesamt 95 Tests mit
93 PASS und zwei alten Shell-Skips. Finales Profil qemu/vga ohne Injection;
Image-/Kernel-/Config-SHA256 sind vor/nach dem Gastlauf identisch. Image:
`97AEBD0CC759AB1DB1E6030951F6F37859D556479A7DA2B11D6E9C0121519845`.
Logs, Digests, Konfiguration und beide frischen Scanouts liegen unter
`build/codex-agent/r310-qmp-final-*`; Wrapper-Buildlogs unter
`r310-qmp-package-{vmware,qemu}.log`. Alle frueheren Negativbelege und der
nutzergestaetigte Standby-Befund bleiben erhalten.

Read-only-Nachpruefung des primaeren QEMU-HID-Quellcodes:
[`hid_pointer_sync`](https://github.com/qemu/qemu/blob/master/hw/input/hid.c)
fasst noch ungelesene Motion-Ereignisse bei gleichem Buttonzustand zusammen.
QMP-ACK beweist daher keinen konsumierten Homing-Pfad. Die neue schnellere
Folge kann Homing und Gegenbewegung zusammenziehen, bevor der Gast am
Bildschirmrand klemmt; die behauptete Ausgangsposition `(0,0)` ist dann nicht
bewiesen. Das ist eine plausible konkrete Fehlerhypothese, noch kein
Trace-Nachweis fuer die installierte QEMU-Binary. Die Hosttests pruefen bisher
Wire-ACK und Geometriearithmetik, nicht diese Geraete-/Gast-Konsumbarriere.
Naechste notwendige Arbeit ist ein begrenzter beobachtbarer Zeiger-/Homing-
Nachweis vor dem Drag, ohne willkuerliche Fristverlaengerung oder Umgehen des
echten USB-Eingabepfads. Nach fehlgeschlagener fokussierter Reparatur gilt
die Paket-Stopregel: keine weitere Implementierung/Gastwiederholung oder
Queue-Weiterschaltung in dieser Wiederaufnahme.

### Messung und Reparaturverlauf

Neue ausdrueckliche Freigabe: Diagnose, Reparatur, vollstaendige Abnahme und
lokaler Commit bei Erfolg ohne Routine-Rueckfragen. Keine fremden Aenderungen,
keine Erweiterung der Produktionsautoritaet oder der eingefrorenen Grenzen.
Der instrumentierte Gastlauf scheitert nach 94,81 Hostsekunden erneut in
Phase 12; beide Rad-Richtungen bestehen, Close fehlt. Image/Kernel/Config sind
vor/nach dem Lauf identisch. Negative Evidenz bleibt erhalten:
`r310-timing-runtime.log`, `r310-timing-failure.browser.log`,
`r310-timing-digests-{before,after}.json` und `r310-timing-config.json`.

Feste, nur im Probe-Modus aktive Zaehler messen kumuliert: sechs Worker-Starts
9302 ms, zwoelf Datei-Lesevorgaenge 4713 ms, drei Bilddekodierungen 273 ms,
22 Rasterungen 124 ms, Puffererzeugung 51 ms, Pixel-IPC 1247 ms. Body-Zeit
3627 ms schliesst Raster/Puffer/IPC ein und darf nicht zu diesen addiert werden;
Chrome 1312 ms, Status 559 ms. Teststeuerung: Resize 4126 ms, Rad ab 341 ms,
Rad auf 150 ms. Somit ist Glyphvorbereitung nicht der verbleibende Engpass.
Die 150-ms-Mux-Pause je HMP-Kommando ist unnoetige Testlatenz; langsame Worker-
Starts bleiben ein separater OS-Ladepfad-Befund, kein behobener Performancefehler.

Gezielte Reparatur: nur der Browser-Test nutzt quittiertes natives QMP
`input-send-event` ueber eine kurzlebige Loopback-Verbindung. Kein HMP-
Kompatibilitaetsaufruf, keine gefaelschten Gastmarker und kein direktes
Handler-Aufrufen. Relative Bewegung, USB-HID-Geraet, Pausen zur HID-Verarbeitung,
Scanout-Geometrie, Configure/Reflow, beide Rad-/Paint-ACKs, Fault/Timeout-
Recovery, Close und alle Fristen bleiben unveraendert. Negativtests pruefen
Fragmentierung, Ereignisquota, Antwort-ID, EOF/kaputte Antworten, Fristablauf
und Schliessen/Reapen bei Admissionfehler. Rot-/Gruennachweis in
`r310-qmp-{red,green}.log`; abschliessende Hostpruefung 17 Tests PASS (2,106 s)
in `r310-qmp-final-host.log`. Abschliessende Build-/Gastbefunde stehen oben;
keine Abnahme durch Hosttests allein.

## R3.10: CSS, Resize und Mausrad im Gast bestaetigt; Gesamtabnahme blockiert

**Vorheriger Abschluss am 6. September: kein Commit, R3.10 blieb aktiv.**
Die Glyphvorbereitung ist reduziert, Motion/Wheel-Reihenfolge inklusive
Legacy-Client-Motion korrigiert. Alle neun gezielten Hostgates bestehen
(91 Tests: 89 bestanden, zwei unveraenderte Shell-Skips); zusaetzlich bestehen
58 vorhandene Desktop-Regressionspruefungen. Unveraenderte Hostevidenz wurde
wiederverwendet, geaenderte Pfade erneut geprueft. Finale Referenzbuilds:
VMware PASS 52,77 s (`20260906-004815-package-vmware-vga.log`), QEMU PASS
42,77 s (`20260906-004908-package-qemu-vga.log`). Kernel und oeffentliche
Protokollgroessen, Queues, Fristen und Quoten wurden nicht geaendert.

Die einzige finale Gastwiederholung scheitert nach 96,36 Hostsekunden:
`test-reist-runtime.ps1 -Mode runtime-desktop-browser -Target qemu -Video vga`.
Dieser regulaere Lauf bestaetigt Eingabe, Bilder, Links, Scrollbar/Clipping,
Reload, Transportfehler, Worker-Fault-/Timeout-Recovery, CSS-Pixel, Resize
und beide echten Mausrad-Richtungen. Danach erneut Phase 12 und
`BROWSER_PROBE_FAIL interaction`; kein `BROWSER_CLOSE_OK`. Das unveraenderte
30-s-Gesamtbudget ist beim Abschluss erschoepft. Dokumentauftraege 1576/1717 ms,
Bild-/Resize-Reflow 2368/2701 ms; ein belastbarer Gesamt-Performancegewinn
ist durch die reduzierte Glypharbeit allein somit nicht nachgewiesen.
Profil qemu/vga, Fault-Injection-Schalter false und Image-/Kernel-/Config-
Hashes sind vor/nach diesem Lauf identisch. Der vorherige Standby-Lauf ist
ein separater Befund, nicht die Erklaerung dieses regulaeren Fehlschlags.

Finale Evidenz in `build/codex-agent/`:
`r310-completion-final-runtime.log`,
`r310-completion-final-failure.browser.log`,
`r310-completion-final-config.json`,
`r310-completion-final-digests-{before,after}.json`,
`r310-completion-final-css-scanout.ppm` und
`r310-completion-final-image-fixture.ppm` (beide frisch).
Nach erschoepfter fokussierter Reparatur gilt Stop: kein weiterer Umbau,
kein weiterer Gastversuch, keine Queue-Weiterschaltung. Naechster Bedarf
ist eine genaue End-to-End-Messung des Bildpuffer-/Surface-/Eingabepfads und
gezielte Reparatur des nachgewiesenen Engpasses; kein blind groesserer Cache
und keine Fristverlaengerung. SDK/Framebuffer/Monitor wurden dazu nur gelesen:
Pixelpublikation erzeugt weiterhin einen neuen unveraenderlichen Vollpuffer
und mehrere Surface-Transaktionen, der HMP-Adapter wartet zweimal 75 ms je
Kommando. Die jeweiligen Anteile am Restengpass sind noch nicht vermessen.

### Reparaturverlauf dieser Wiederaufnahme

Neue ausdrueckliche Freigabe fuer die verbleibende Browser-/Eingabelatenz:
der zugeordnete Kandidat wird auf 8eb525d0 fortgesetzt; keine fremden
Aenderungen gefunden. Keine Fristen-/Quotenerhoehung und kein Ueberspringen
der abschliessenden Fristpruefung. Die echte Glyphraster-Regression zeigt
256 redundante Vorbereitungen und besteht nach frame-lokalem Cache mit einer;
Pixel bleiben identisch, Font-/Hoehenwechsel und kaputte Grenzen bleiben
geprueft. Der echte extrahierte Compositor-Zweig reproduziert ausserdem
Scroll-Hit-Testing an der alten Zeigerposition bei ausstehender Bewegung;
nach der Korrektur werden Motion und Wheel in dieser Reihenfolge verarbeitet.
Rot-/Gruennachweise: `r310-completion-glyph-{red,green}.log` und
`r310-completion-motion-{red,green}.log` unter `build/codex-agent/`.
Die vollstaendige Abnahme dieses reparierten Kandidaten steht noch aus.

Erste Wiederaufnahme: Browser-Host 13 Tests (2,55 s), Surface 10 (1,36 s),
CSS (30,40 s), HTML (3,70 s) und zusaetzlich Desktop 58 Tests (0,74 s) PASS.
Unveraenderte Hostgates bleiben gueltig. Referenzbuilds: VMware 52,75 s
(`20260905-231524-package-vmware-vga.log`), QEMU 43,09 s
(`20260905-231616-package-qemu-vga.log`). Der Gastlauf ist dennoch FAIL:
5290,36 Hostsekunden einschliesslich einer unerwarteten Werkzeugunterbrechung
von rund 5208 Sekunden. Der Host-Runner bricht mit fehlender Recovery-/CSS-/
Resize-/Wheel-Evidenz ab, waehrend das letzte Gastlog den Recovery-Workerstart
bei 59743 ms zeigt. Der Nutzer bestaetigt am 6. September, dass er den PC
waehrenddessen selbst in Standby versetzt hatte. Die lange Unterbrechung
ist damit erklaert und kein nachgewiesener REIST-OS-Fehler; der unvollstaendige
Lauf bleibt dennoch ohne erfolgreichen Gastnachweis oder belastbaren
Gesamt-Latenzvergleich.
Image-/Kernel-/Konfigurationshashes sind vor/nach dem Lauf unveraendert.
Logs: `r310-completion-runtime.log`, `r310-completion-interrupted.browser.log`,
`r310-completion-digests-{before,after}.json`. Das alte CSS-PPM ist nicht aus
diesem Lauf und wird nicht als frische Evidenz verwendet.

Eine bei direkter Diffpruefung gefundene und real reproduzierte Luecke wird
als einzige fokussierte Nachreparatur geschlossen: Nach vorgezogenem Motion-
Flush muss das normale Client-Motion-Ereignis erhalten bleiben, insbesondere
fuer Clients ohne Scroll-Opt-in. Der echte Branch-Test scheitert zuvor daran
(`r310-completion-motion-legacy-red.log`); der finale Surface-Host besteht
mit 10 Tests in 1,42 s, Desktop mit 58 Tests in 0,46 s
(`r310-completion-final-gate-{surface,desktop}.log`). Keine neue Frist,
keine Aenderung an Glyphcache, Browserprobe oder Kernel. Nach beiden finalen
Referenzbuilds folgt genau eine weitere vollstaendige Gastabnahme; bei
erneutem Fehlschlag Stop ohne Commit und ohne weitere Reparatur.

### Letzte abgeschlossene Abnahme vor der aktuellen Reparatur

**Aktueller Abschluss: kein Commit, keine Queue-Weiterschaltung.** Die neue
ausdrueckliche Freigabe fuer QEMU-Referenzbuild und eine vollstaendige
Gastabnahme ist ausgefuehrt, ohne Produktions- oder Runner-Aenderung.
`test-reist-package.ps1 -Target qemu -Video vga`: PASS, 42,69 s
(`20260905-225752-package-qemu-vga.log`). Das zuvor vorhandene generierte
VMware-Image wurde wie freigegeben durch die QEMU-Referenz ersetzt;
Quellen, alter CSS-Stash und negative Evidenz sind erhalten.

`test-reist-runtime.ps1 -Mode runtime-desktop-browser -Target qemu -Video vga`:
FAIL, 95,86 Hostsekunden. In diesem Lauf bestehen Bilder, Eingabe, Links,
Scrollbar/Clipping, Reload, Transportfehler, Worker-Fault-/Timeout-Recovery,
CSS-Pixel und echter Resize/Reflow. Erstmals sind auch beide realen
Mausrad-Richtungen einschliesslich gezeichneter Scrollposition nachgewiesen:
`BROWSER_WHEEL_DOWN_OK`, `BROWSER_WHEEL_UP_OK`. Unmittelbar danach folgt
`BROWSER_PROBE_STATE phase=12 loaded=1 child=0 pending=0` und
`BROWSER_PROBE_FAIL interaction`; `BROWSER_CLOSE_OK` fehlt.
Die lesende Codepruefung lokalisiert den Abbruch im weiterhin unveraenderten
30-s-Gesamtbudget: Der erfolgreiche Up-Schritt setzt exit_requested und
erhoeht die Phase auf 12; noch in derselben Iteration prueft main die Frist.
Das ist keine gescheiterte Scrollbewegung und kein nachgewiesener Browser-
Absturz, aber weiterhin keine bestandene Gesamtabnahme. Die Frist darf nicht
durch Ueberspringen der Abschlusspruefung umgangen werden.

Dokumentauftraege: 1510/1722 ms; Bild-/Resize-Reflow: 2392/2800 ms.
Profil qemu/vga und alle Fault-Injection-Schalter false; SHA256 von Image,
Kernel und Konfiguration vor/nach dem Gastlauf exakt identisch. Damit ist
ein erneuter Artefaktwechsel fuer diesen Lauf ausgeschlossen.
Evidenz unter `build/codex-agent/`: `r310-reference-restored-package.log`,
`r310-reference-restored-runtime.log`,
`r310-reference-restored-failure.browser.log`,
`r310-reference-restored-config.json`,
`r310-reference-restored-digests-before.json`,
`r310-reference-restored-digests-after.json`,
`r310-reference-restored-css-scanout.ppm` und
`r310-reference-restored-image-fixture.ppm` (beide frisch aus diesem Lauf).
Unveraenderte Host-/VMware-Gates behalten ihre dokumentierte Evidenz;
Scope und diff --check bestehen fuer alle 49 zugeordneten Kandidatenpfade.
Gemaess Freigabe kein weiterer Reparatur- oder Gastversuch. Naechster Bedarf:
gezielte Laufzeitreparatur im bestehenden Browser-/Eingabepfad mit Regression
und erneuter vollstaendiger Abnahme, nicht eine laengere Frist. Der offene
Motion-/Wheel-Batching-Nebenbefund ist durch diesen Lauf nicht erledigt.

### Vorheriger Lauf mit zwischenzeitlich ausgetauschtem Referenzimage

**Vorheriger Abschluss: HMP-Richtung korrigiert, Gastabnahme blockiert.**
Der echte Runner-Zweig reproduzierte die falsche Down-/Up-Reihenfolge und
sendet nun -1 fuer Down, +1 fuer Up. Die Regression prueft Wiederholungen
und dass Up erst nach Down-ACK gesendet wird. Browser-/Runner-Hostgate:
12 Tests bestanden, 1,97 s (`r310-wheel-direction-host.log`); Rotnachweis:
`r310-wheel-direction-red.log`. Mit unveraenderten anderen Hostgates sind
es 89 Tests, 87 bestanden und zwei alte Shell-Skips. Keine OS-Quellaenderung.

Der freigegebene Gastlauf scheitert nach 67,92 Hostsekunden schon in Phase 0:
erstes Testbild nicht verfuegbar, davor Desktop-Font-Read und Close mit -110
sowie Storage-Service-Neustart. Erster CSS-Auftrag 3370 ms, davon Spawn
3018 ms. Mausrad, CSS-Recovery und Resize werden in diesem Lauf nicht erreicht;
ihre fruehere Teil-Evidenz ist keine Abnahme dieses Laufs.

Die nachtraegliche lesende Artefaktpruefung zeigt einen unerwarteten Wechsel:
`build/.windows-build-config.json` hat jetzt `target=vmware`, alle
Fault-Injection-Schalter sind false. Konfiguration/Kernel wurden um 22:45:15,
das Image um 22:45:17 neu erzeugt, nach dem letzten dokumentierten
QEMU-Referenzbuild um 22:31. Der Testrunner verwendet vorhandene Images und
startet QEMU mit Snapshot; dieser Lauf hat den Profilwechsel nicht erzeugt.
Die vorab angenommene unveraenderte QEMU-Referenz war somit falsch und wurde
nicht rechtzeitig geprueft. Das belegt den Artefaktwechsel, nicht bereits
dessen Kausalitaet fuer den I/O-Timeout. Kein weiterer Gastversuch.

Evidenz unter `build/codex-agent/`: `r310-wheel-direction-runtime.log`,
`r310-wheel-direction-failure.browser.log`,
`r310-wheel-direction-observed-build-config.json`,
`r310-wheel-direction-observed-sbom.json`. Kein frischer Screenshot in diesem
Lauf; alte PPMs werden nicht als aktuelle Evidenz ausgegeben. Kein Commit,
keine Queue-Weiterschaltung. Vor einer weiteren Abnahme muss der freigegebene
QEMU-Referenzbuild wiederhergestellt und ein paralleler Profilwechsel
ausgeschlossen werden. Vorhandenes VMware-Image wurde nicht ueberschrieben.

Neue ausdrueckliche Freigabe: ausschliesslich HMP-Richtung im Runner,
echte Runner-Regression und erneute vollstaendige Gastabnahme. OS-Quellen,
Images, Quoten, Fristen und vorhandene Nachweise bleiben unveraendert.
Der zugeordnete Kandidat wird fortgesetzt; keine fremden Aenderungen gefunden.

**Letztes Ergebnis: blockiert, kein Commit.** Die Performance-Korrektur spart
im Gast zwei identische Workerstarts nach Fragmentnavigation. CSS, Bilder,
Links, Scrollbar, Worker-Fault-/Timeout-Recovery und echter Resize/Reflow
sind nachgewiesen. Alle neun Hostgates bestehen mit aktuell 88 Tests
(86 bestanden, zwei alte Shell-Skips); der zuletzt geaenderte Browser-/Runner-
Hostgate besteht mit 11 Tests in 1,98 s. Beide unten genannten finalen
Referenzbuilds bleiben gueltig: Die letzte Korrektur aendert nur den Runner.

Die abschliessende erlaubte Gastwiederholung scheitert nach 95,03 Hostsekunden
weiter in Phase 10. Im Gegensatz zum vorherigen Lauf sind nun sowohl der
kurze Mausweg als auch `mouse_move 0 0 1` vor dem Abbruch im Transkript sichtbar.
Dokumentauftraege 1698/1759 ms, Reflows 2570/2602 ms; Resize-Reflow ist fertig
bei 64921 ms. Negative Evidenz: `r310-wheel-path-runtime.log`,
`r310-wheel-path-failure.browser.log`, `r310-wheel-path-css-scanout.ppm`.

Die anschliessende **nur lesende Diagnose** belegt einen Vorzeichenfehler im
Gasttest: QEMUs `hmp_mouse_move` ordnet positives dz `WHEEL_UP` zu, der Runner
sendet aber +1 fuer seinen Down-Check. Am Seitenanfang ist dies korrekt ohne
Scrollbewegung; die Down-Bestaetigung kann so nicht kommen. -1 ist Down, +1 Up.
Referenz: [QEMU HMP-Implementierung](https://github.com/qemu/qemu/blob/master/ui/ui-hmp-cmds.c),
auch im Release v10.0.0; installiert ist 11.1.0-12130-ge470268ff4. Der Fehler
liegt nachweislich im Testrichtungssignal; das ist noch kein Gastnachweis
fuer die vollstaendige Mausrad-Zustellung. USB-HID/xHCI und Kernel wurden
nur gelesen, nicht geaendert. Nebenbefund: gemeinsames Batching von Motion
und Wheel muss die Reihenfolge am Routingpunkt bewahren; dafuer ist noch
kein neuer Verhaltensnachweis gefuehrt.

Die Stop-Regel nach einer fokussierten Reparatur ist erreicht. Keine weitere
Quellaenderung oder Gastwiederholung in diesem Lauf, keine Queue-Weiterschaltung,
kein Commit. Erforderlich ist eine neue Freigabe fuer Korrektur und Regression
der HMP-Richtung sowie die erneute unveraenderte vollstaendige Gastabnahme.
Alle negativen Logs und der gesicherte CSS-Stash bleiben erhalten.

Erneute ausdrueckliche Freigabe am 5. September: Worker-Starts und Bild-Reflows
weiter optimieren, ohne Sicherheits- oder Zeitgrenzen zu lockern. Nur der
zugeordnete bestehende Kandidat wird fortgesetzt; keine fremden Aenderungen.
Erster reproduzierbarer Ansatz ist der durch Fragmentnavigation unnoetig
verworfene Szenencache. Einweg-Worker, Frischepruefung und alle Gastchecks
bleiben unveraendert; untenstehende Fehlschlaege bleiben negative Evidenz.

Der echte Browser-Hostfall reproduzierte den Fragmentfehler und besteht mit
frischem Datei-Reload, unveraendertem Szeneninhalt sowie Ablehnung geaenderter
Query/Herkunft (`r310-fragment-red.log`, `r310-fragment-gate-browser.log`,
10 Tests, 1,89 s). Beide Referenzbuilds bestanden (VMware 11,31 s,
`20260905-223009-package-vmware-vga.log`; QEMU 42,25 s,
`20260905-223103-package-qemu-vga.log`). Unveraenderte Hostgates behalten ihre
bestandene Evidenz; SDK, Parser, gemeinsame Surface-Dateien und ihre Tests
wurden seit deren letzter Abnahme nicht geaendert.

Erster Gast dieser erneuten Freigabe: FAIL nach 96,47 Hostsekunden.
Der Fragmentfix spart nachweislich zwei Workerstarts: zweiter
`BROWSER_CSS_SCENE_REUSED` statt Dokument- und Bild-Reflow. Fault/Timeout-
Recovery, CSS-Pixel und nun auch echter Resize/Reflow bestehen. Der letzte
Abbruch erfolgt in Phase 10, bevor der Monitor die erste Radbewegung sendet.
Resize-Reflow startet bei 62521 ms und dauert 2591 ms; danach verbraucht
der unnoetige Sechs-Paket-Mausweg zum Seitenanfang das restliche Gesamtbudget.
`r310-fragment-failure.browser.log`, `r310-fragment-runtime.log` und
`r310-fragment-css-scanout.ppm` bleiben negative Evidenz, kein Gesamterfolg.

Die einzige fokussierte Reparatur nach diesem Fehlversuch aendert deshalb
lediglich den Test-Zielpunkt: naechster Dokumentpunkt acht Pixel innerhalb
von Scrollbar-/Statusgrenze statt Rueckweg quer ueber die Seite. Der echte
Runner-Hostfall reproduziert sechs statt eines Bewegungspakets und prueft
Resize-Press/Release sowie den Zielpunkt im Dokument (`r310-wheel-path-red.log`).
Keine simulierten Browserhandler, entfallenden Wheel-/Pixel-/Recovery-Checks,
Produktionsaenderung oder Fristverlaengerung. Nach finalem Hostgate folgt genau
eine erneute Gastpruefung auf den unveraenderten finalen Referenzimages.

Abschluss der freigegebenen Scroll-/Performance-Erweiterung am 5. September:
**Kein Commit, keine Queue-Weiterschaltung.** Alle neun Hostgates bestehen
(87 Tests, 85 bestanden, zwei unveraenderte Shell-Skips). Die finalen
Referenzbuilds nach der Bulk-Read-Anpassung bestehen: VMware 50,13 s
(`20260905-221141-package-vmware-vga.log`), QEMU 43,21 s
(`20260905-221231-package-qemu-vga.log`).

Die einzige fokussierte Gastwiederholung scheitert nach 97,93 Hostsekunden:
`test-reist-runtime.ps1 -Mode runtime-desktop-browser -Target qemu -Video vga`.
Der Lauf erreicht nun Szene-Wiederverwendung, Bilddarstellung, Eingabe,
Links, Scrollbar/Clipping, Reload, Transportfehler sowie echte Worker-Fault-,
Timeout- und Recovery-Erfolgsmarker. Auch `BROWSER_CSS_PIXELS_OK` und der
CSS-Scanout mit Hintergrund, Text und Rahmen sind erreicht. Dokumentauftraege
dauern 1507/2145/1643 ms, Bild-Reflows 2496/2495 ms; allein die gemessenen
Worker-Starts benoetigen weiterhin 1134 bis 1904 ms.

Der konkrete Restblocker ist Phase 9 am unveraenderten 30-s-Gesamtbudget:
`BROWSER_PROBE_STATE phase=9 loaded=1 child=0 pending=0`. Das Monitortranskript
zeigt den Abbruch waehrend der Mauspositionierung, noch vor dem ersten
Resize-Button-down; danach gibt es keinen weiteren Worker-Start. Daher kein
Nachweis fuer abgeschlossenen Resize, echte Wheel-down/up-Ereignisse oder
sauberes Close in diesem Lauf. Das ist keine erfolgreiche Mausrad-Gastabnahme.
Die Mausrad-Implementierung und ihre Hostregressionen bleiben als Kandidat
erhalten; keine Frist wurde verlaengert und kein Test entfernt.

Negative Evidenz unter `build/codex-agent/`:
`r310-perf-final-runtime.log`, `r310-perf-final-failure.browser.log`,
`r310-perf-final-css-scanout.ppm`, `r310-perf-final-image-fixture.ppm`.
Die CSS-Pixelpruefung stammt aus Gast und Runner; der lokale Bildbetrachter
konnte das PPM nicht oeffnen. Nach der Stop-Regel keine weitere
Implementierung oder Gatewiederholung ohne neue Reparaturfreigabe.
Naechster Bedarf: weitere gezielte Lade-/Worker-Start-/Reflow-Optimierung
innerhalb der unveraenderten Fehlergrenzen, nicht eine laengere Testfrist.
Aktiv bleibt R3.10 auf `8eb525d0`; der alte CSS-Stash bleibt unangetastet.

### Verlauf und erhaltene negative Evidenz

Neue ausdrueckliche Freigabe: gemeinsame Surface-/Compositor-Dateien fuer
Mausrad-Scrollen sowie Lade-/Reflow-Optimierung, ohne Lockerung von Kernel-,
Sicherheits- oder Zeitgrenzen. Der zugeordnete Kandidat wird im selben
Arbeitsbaum fortgesetzt. Die Queue listet die additiven Dateien und den
zusaetzlichen Surface-Hostgate; alte Gates und negative Evidenz bleiben erhalten.
Implementiert sind opt-in Scroll-v1 im unveraenderten v6-Umschlag, geordnete
gepruefte Radereignisse und Wiederverwendung des Browser-Scrollpfads.
Ueberfluessige Loeschungen von vier nie erzeugten CSS-Tempdateien je Worker
sind entfernt; nur gestartete CURL-Kinder loesen noch ihre bisherige
Body-/Teil-Dateibereinigung nach Reap aus. Neue Regressionen reproduzierten
beide alten Verhaltensfehler. Die ersten Hostlaeufe brauchten eine lokale
Zig-Cachekonfiguration im bisher compilerbedingt uebersprungenen Surface-Test
und eine signierte Koordinatenpruefung im Mausradhandler; keine Warnung oder
Pruefung wurde abgeschaltet. Finale Gateergebnisse stehen oben.

Der anschliessende Toolchain-Gate zeigte einen i386-Linkfehler. Die begrenzte
separate Assemblierdiagnose (`r310-wheel-browser.s`) belegte `__divdi3` fuer
die neue 64-Bit-Scrollrechnung. Quotient-/Restzerlegung verwendet nun nur
sichere 32-Bit-Division und breite Addition, ohne neue Laufzeitabhaengigkeit.
Browser-Runtime-Hostgate bestand in 1,83 s, Toolchain mit 21 Tests in 101,05 s;
Surface (9 ohne Skip), GUI-Browser, CSS und HTML bestanden ebenfalls.
Referenzbuilds: VMware 57,31 s (`20260905-215546-package-vmware-vga.log`),
QEMU 43,09 s (`20260905-215644-package-qemu-vga.log`).

Erster Gast dieser Erweiterung: FAIL nach 96,39 Hostsekunden, erneut in Phase 6
am originalen 30-s-Limit. Dokumente 1593/2311/2287 ms, Reflows 2583/2598/2677 ms;
die entfernten spekulativen Unlinks waren nicht der dominante Zeitanteil.
Die Start-/Scroll-/Bildchecks erreichten ihre bisherigen Marker, nicht aber
CSS-Pixel, Resize oder Mausrad. Negative Evidenz bleibt als
`r310-wheel-gate-runtime.log` und `r310-wheel-failure.browser.log` erhalten.

Die fokussierte Performance-Reparatur nach diesem Gastfehler verwendet nur
die letzte validierte Szene bei frisch gelesenem, byteidentischem HTML unter
identischer URL, Geometrie und intrinsischen Massen. Bilder werden weiter
frisch geladen; identische Masse benoetigen kein weiteres CSS-Reflow.
Geaenderte Bytes/URL/Viewport und explizite Fehlermodi bleiben Workerauftraege.
Echte Hostregression Rot/Gruen: `r310-wheel-scene-cache-{red,green}.log`.
Zusaetzlich bauen die echten Parserbibliotheken mit Standard-LLVM-`-Os` und
Linker-Garbage-Collection fuer Funktionen/Daten, ohne Quoten oder Regeln zu
lockern. Keine weitere Gastwiederholung nach einem erneuten Misserfolg.
Vor dieser abschliessenden Wiederholung wurde der zweite messbare Faktor
abgesichert: `demo-colors.gif` hat 214860 Byte; die bisherigen 4096-Byte-Reads
verursachten 53 IPC-Aufrufe. `BROWSER_READ_CHUNK` verwendet jetzt die vorhandene
128-KiB-Bulkgrenze, ein echter Hostfall beweist genau zwei Reads und bytegleiche
Ausgabe (`r310-perf-bulk-red.log`, finaler Browserhostgate in 1,89 s).
Der groessenoptimierte Worker hat 821292 statt 874540 Byte. Der Toolchain-Gate
bestand nach der Compilerprofil-Aenderung in 101,45 s; die danach ausschliesslich
geaenderte Read-Batchgroesse wird durch finalen Browserhostgate und beide
erneuten Referenzbuilds geprueft. Vorlaeufige Builds vor der Batchanpassung
sind keine finale Gastevidenz.

Erneute Freigabe am 5. September 2026: Der Nutzer beauftragt ausdruecklich
die unten diagnostizierte Worker-Startreparatur. Der zugeordnete Kandidat wird
im bestehenden Arbeitsbaum fortgesetzt; keine fremden Aenderungen gefunden.
Nur vor dem ersten gueltigen Paket darf EBADF/EACCES begrenzt abgewartet
werden. Kernel, Autoritaet und absolute Fristen bleiben unveraendert.
Betroffene Hostgates, beide Referenzbuilds und genau eine weitere Gastabnahme
werden erneut ausgefuehrt; unbeeinflusste bestandene Hostevidenz bleibt gueltig.
Die vorherige Sperre und saemtliche negativen Ergebnisse bleiben unten erhalten.

Ergebnis dieser freigegebenen Reparatur: Der Worker wartet nun nur vor dem
ersten akzeptierten Paket bei EBADF/EACCES auf Delegation, jeweils mit 1 ms
Schlaf und unveraenderter absoluter Deadline. Spaeterer Rechteverlust oder
fehlgeschlagenes Schlafen beendet den Auftrag sofort. Der echte CSS-Hostlauf
prueft jetzt 13 Faelle einschliesslich fehlender Delegation und Revocation.
Eine erste Testfassung benutzte versehentlich das nicht im SDK vorhandene
strstr; nur der Test wurde auf bestehende Stringfunktionen korrigiert.
Danach reproduzierte der alte Worker sechs Verhaltensfehler. Beide negativen
Entwicklungslogs bleiben als `r310-startup-regression-red*.log` erhalten.

Finale betroffene Hostgates bestanden: `python test/test_css_engine.py -v`
(27,74 s), `python test/test_html_engine.py -v` (3,40 s),
`python test/test_browser_runtime_source.py -v` (1,80 s). Die uebrigen fuenf
unveraenderten Hostgates behalten ihre unten dokumentierte bestandene Evidenz:
insgesamt weiterhin 76 bestandene Tests und zwei alte Shell-Skips. Logs der
neuen Gates: `build/codex-agent/r310-startup-gate-test_*.log`.
Beide Referenzbuilds bestanden mit finaler Startreparatur:
`test-reist-package.ps1 -Target vmware -Video vga` (46,55 s,
`20260905-213230-package-vmware-vga.log`) und anschliessend
`test-reist-package.ps1 -Target qemu -Video vga` (41,23 s,
`20260905-213316-package-qemu-vga.log`).

**Weiterhin blockiert, kein Commit:** Der einzige freigegebene Gastlauf
`test-reist-runtime.ps1 -Mode runtime-desktop-browser -Target qemu -Video vga`
scheiterte nach 95,34 Hostsekunden am unveraenderten 30-s-Gesamtbudget der
Browserprobe. Der Startfehler trat nicht mehr auf: Sechs CSS-Workerauftraege
publizierten erfolgreich, Bilder, Adressleisteneingabe, Links, Anker,
Scrollbar-Capture, Scroll-Clipping und Reload lieferten ihre Erfolgsmarker.
Dokumentauftraege dauerten 1700/2357/2379 ms, Bild-Reflows 2596/2679/2582 ms.
Alle lagen unter der unveraenderten 5-s-Workergrenze, zusammen mit Bild-/
Dateiladen war aber das Gesamtbudget bereits in Phase 6 beim gestarteten
Fehlerinjektionsauftrag erschoepft (`loaded=1 child=16`). Timeout-Recovery,
CSS-Testpixel, echter Resize und Close wurden nicht mehr nachgewiesen.
Kein weiterer Gastversuch, kein verlaengertes Limit, keine Queue-Weiterschaltung.
Vollstaendige negative Evidenz: `r310-startup-gate-runtime.log` und
`r310-startup-failure.browser.log`; der frische Screenshot der normalen
Bildfixture ist als `r310-startup-image-fixture.ppm` gesichert, kein Nachweis
der noch nicht erreichten CSS-Pixel-/Resize-Probe. Naechste erforderliche
Reparatur ist die tatsaechliche Lade-/Reflow-Leistung, nicht die Testfrist.

Historischer Stand vor der oben dokumentierten Erweiterungsfreigabe:
Zusaetzlicher Nutzerauftrag waehrend dieses Laufs war Scrollen per Mausrad.
Nur lesende Bestandsaufnahme: Das bestehende Maus-Syscall liefert `wheel`,
aber `desktop.c` leitet es nur an den internen Explorer weiter. Surface kennt
lediglich Motion, Button und Keyboard; `desktop_surface.c` und
`surface_client.c` weisen andere Typen ab. Der Browser ignoriert nicht passende
Pointertypen. Ein echter Mausradpfad braucht daher eine versionierte,
append-only Surface-Scrollereignis-Erweiterung, begrenzte gemeinsame
Queue-/Validierungslogik, Compositor-Routing und Browser-Anwendung mit
Richtung, Randbegrenzung und Gastnachweis. Diese gemeinsamen Produktionsdateien
liegen ausserhalb von R3.10 und dessen bisherigem ABI-invarianten Umfang.
Noch keine Mausradimplementierung; keine stille Scope-Erweiterung oder
Umdeutung von Motion/Keyboard-Ereignissen. Architekturfreigabe steht aus.

Aktiv ist `R3.10-browser-css-layout` auf der abgenommenen Speicherbasis
`8eb525d0`. Der gesicherte CSS-Kandidat wurde durch kontextgepruefte Patches
aus `121cb536d7c2ef63df59b7d3aa08a4f4b3da0086` wiederhergestellt; die neuen
MEMTEST-/SDK-/Speicheraenderungen bleiben erhalten. Das unveraenderte
LibCSS-0.9.2-Archiv ist gegen seinen eingefrorenen SHA-256 geprueft.
Alte Diagnoseergebnisse sind keine Abnahme dieses zusammengefuehrten Stands.

Die Schrift ist read-only ins Browserprogramm eingebettet. Der erste alte
Gastlauf scheiterte vor der Dokumentverarbeitung mit nicht verfuegbarer
Schrift; `r310-development.browser.log` bleibt negative Evidenz. Der nun
verwendete Browser-Runner uebernimmt die in R1.2c nachgewiesene prozesslokale
Windows-QEMU-Timerpraemisse aus dem gemeinsamen Helper. Ein neuer echter
Runner-Verhaltenstest beweist dessen Aufruf und vollstaendiges Child-Reaping
vor Readerstart bei verweigerter Konfiguration (Rot-/Gruen-Logs:
`r310-resume-timer-{red,green}.log`). Keine Gastfrist oder Power-Prioritaet
wurde geaendert. Sieben Hostgates bestanden. Der erste Toolchain-Gate-Lauf
scheiterte nach 144,60 s ausschliesslich am 60-s-Limit des SDK-Unterprozesses.
Die fokussierte Reparatur parallelisiert unabhaengige SDK-Objekte mit genau
vier festen Arbeitern und stabiler Rueckgabereihenfolge; der neue echte
Helper-Verhaltenstest beweist Parallelitaetsgrenze, Vollstaendigkeit und
Fehlerweitergabe. Sein vorheriger serieller Pfad scheiterte an der Testbarriere.
Keine Timeout-Erhoehung, kein Weglassen von Bibliotheken oder Tests. Der
Toolchain-Wiederholungslauf bestand mit 21 Tests in 101,37 s. Alle acht
Hostbefehle zusammen: 78 Tests, 76 bestanden, zwei alte Shell-Compilerfaelle
uebersprungen. Erste Referenzbuilds: VMware 11,30 s, QEMU 41,74 s.

Der erste formale Browsergast erreichte `BROWSER_FONT_READY` und einen
erfolgreich beendeten CSS-Worker, verwarf jedoch dessen Antwort. Der Parent
las nach dem letzten vollstaendigen Paket erneut; das Kernel-IPC liefert
nach Peer-Exit dann korrekt EPIPE. `service_css_ipc` normalisierte dies zu
einem Protokollfehler und brach den Auftrag ab. Die fokussierte Reparatur
stoppt den Empfang ausschliesslich bei exakt vollstaendiger Rahmung; Reap,
Generation und gesamte Szene werden danach weiterhin validiert.
Der echte Hosttransport stellt nun den Exit unmittelbar nach dem letzten
Paket und EPIPE beim Folgeaufruf nach. Vollstaendige Antwort besteht;
unvollstaendige bleibt abgewiesen. Der erste neue Trunkierungs-Test erwartete
versehentlich den rohen -32-Code statt des normalisierten -84; die Erwartung
wurde korrigiert, nicht die Fehlerbehandlung gelockert. Die anschliessende
Rot-/Gruen-Probe reproduzierte den eigentlichen zusaetzlichen Receive.
Alle negativen Logs bleiben erhalten, insbesondere
`r310-resume-first-failure.browser.log`, `r310-resume-ipc-eof-*.log` und
`r310-resume-gate-test_browser_runtime_source-repair.log`.
Die betroffenen Hostgates bestanden gegen die finale Reparatur:
`test_browser_runtime_source.py -v` (10 Tests, 1,81 s) und
`test_gui_browser_source.py -v` (8 Tests, 25,51 s). Die finalen Referenzbuilds
bestanden: VMware/VGA 48,26 s, QEMU/VGA 41,70 s; Logs
`20260905-211704-package-vmware-vga.log` und
`20260905-211752-package-qemu-vga.log` unter `build/codex-agent/`.

**BLOCKIERT / Wiederholungsgrenze erreicht:** Der zweite formale
`test-reist-runtime.ps1 -Mode runtime-desktop-browser -Target qemu -Video vga`
scheiterte ebenfalls (etwa 94 Hostsekunden). Kein Commit, keine Queue-
Weiterschaltung und keine weitere Gastwiederholung. HEAD bleibt `8eb525d0`,
R3.10 bleibt aktiv; nur sein zugeordneter Kandidat liegt sichtbar im Arbeitsbaum.
Der zweite Lauf kam nicht zur CSS-Publikation: `BROWSER_FONT_READY`,
`BROWSER_HTML5_STARTED`, `BROWSER_HTML5_START_ERROR result=-84`, danach
`BROWSER_HTML5_REJECT exit=74 result=-5 cancelled=1`. Die 5,36-GHz-
Bootkalibrierung ist unauffaellig. Vollstaendige negative Evidenz:
`r310-resume-repair-failure.browser.log` und
`r310-resume-gate-runtime-repair.log`.

Die anschliessende reine Quellpruefung identifiziert einen weiteren
Start-Race: `kernel/ipc/ipc.c:resolve_capability` liefert bei noch nicht
delegiertem Handle korrekt EBADF (-9). `html_worker.c:css_worker` wartet in
dieser Phase bisher nur bei EACCES (-13); EBADF fuehrt sofort zu Exit 74,
sodass die nachfolgende Parent-Delegation scheitern kann. Der vorhandene
CSS-Hostmock stellte vor Delegation nur -13 nach und verfehlte diesen Fall.
Das passt zur beobachteten Exit-/Startfehlerfolge; ein korrigierter Gastnachweis
fehlt weiterhin. Erforderlich ist eine erneut freigegebene fokussierte
Worker-Startreparatur mit realem EBADF-Regressionstest: nur vor dem ersten
empfangenen Paket begrenzt auf Delegation warten, ohne neue Autoritaet,
Kernelveraenderung oder Fristverlaengerung. Vollstaendiger CSS-Gasterfolg,
Pixel-/Resize-Nachweis und Abnahme sind ausdruecklich noch offen.

Logs insgesamt: `build/codex-agent/r310-resume-*`. Der originale CSS-Stash
bleibt unangetastet; keine spaetere Browser- oder VMware-Pointer-Implementierung
wurde in diesem Lauf begonnen.

## R1.2c: abgenommene Speichergrundlage

Aktiver Auftrag: Browser-Engine-Umbau einschließlich fehlender OS-Grundlagen.
`R7.1n-ata-pio-read-throughput` und `R3.9-browser-html5-worker` sind abgenommen.
R1.2c ist mit beiden Referenzbuilds und allen vier Gastgates abgenommen;
169 Hosttests bestanden, drei alte Compiler-abhaengige Faelle wurden
uebersprungen. Die Queue aktiviert als naechstes R3.10. Der noch nicht
abgenommene CSS-Kandidat bleibt einschliesslich neuer Dateien im lokalen Stash
`121cb536d7c2ef63df59b7d3aa08a4f4b3da0086` auf `c8eda742`; ignorierte Logs
bleiben erhalten. R3.10 wird in diesem Speicherpaket nicht implementiert.

R1.2c liefert bedarfsgerechtes privates Backing bis zur Haelfte des verwalteten
RAM, maximal 512 MiB je Prozess, wiederverwendbare VA-Luecken und eine globale
1/16-Frame-Reserve. Die libc gibt vollstaendig leere Regionen automatisch
zurueck; Fault/Kill/Exit werden generationstreu und fortsetzbar aufgeraeumt.
64-Seiten-Batches erhalten den Fortschritt anderer Prozesse. Das ist kein
Tracing-GC fuer lebende C-Objekte. Vertrag und Grenzen:
`docs/architecture/PRIVATE_PROCESS_MEMORY_CONTRACT.md`. Die bestehende
1-GiB-Physikgrenze bleibt; High-Memory/Paging und Anwendungs-/Dateicaches sind
separate Folgearbeiten. Reservierte und redundante Speicherobjekte bleiben
unveraendert. Keine allgemeine DIMM-Fehlertoleranz oder VMware-Laufzeitzusage.

Elf eingefrorene Hostbefehle: 172 Tests, davon 169 bestanden und drei alte
Compiler-abhaengige Faelle uebersprungen (ein Memory-Resilience-Hostfall und
zwei Shell-Hostfaelle). Alle neuen realen Allocator-, Provider-, Reaper-,
Desktop-Deadline- und Win32-Runner-Verhaltenstests liefen ohne Skip. Nach der
autorisierten Reparatur wurden nur betroffene Hostgates erneut ausgefuehrt;
unveraenderte erfolgreiche Gates bleiben gueltig. Logs unter
`build/codex-agent/private-memory-gate-test_*.log` und
`private-memory-repair-gate-test_*.log`. Der Toolchain-Test bestand nach der
fokussierten Ergaenzung von MEMTEST in seiner Artefaktmenge in 99,29 s.

Finale Referenzbuilds (`scripts/test-reist-package.ps1`, jeweils `-Video vga`):
VMware PASS in 8 s, QEMU PASS in 43 s. Logs unter `build/codex-agent/`:
`20260905-203644-package-vmware-vga.log` und
`20260905-203701-package-qemu-vga.log`. Formale Gastpruefungen:

| Eingefrorener Befehl / Modus | Ergebnis | Hostsekunden |
| --- | --- | ---: |
| `run_qemu_smoke.py --memory 1024M --expect-process-memory --timeout 180` | PASS, zweimal 256-MiB-calloc, Peer maximal 85 ms | 93,67 |
| `run_qemu_smoke.py --memory 128M --expect-process-memory --timeout 180` | PASS, zweimal 16-MiB-calloc, Peer maximal 88 ms | 78,90 |
| `test-reist-runtime.ps1 -Mode libc-client -Target qemu -Video vga` | PASS | 74,98 |
| `test-reist-runtime.ps1 -Mode memory-resilience -Target qemu -Video vga` | PASS, separates Resilienzimage | 176,13 inkl. Build; 73 Gast-Runner |

Beide MEMTEST-Gates pruefen OOM-Datenerhalt, SDK-realloc, Nullung, exakte
Frame-Rueckgabe und Fault/Kill/Reap nach dem unveraenderten GTEST. Vollstaendige
Befehle stehen in `automation/reist-s03b.toml`; finale Logs:
`private-memory-1024.log`, `private-memory-128.log` und
`private-memory-repair-gate-*.log` im selben Logverzeichnis.
Die bestehenden Gastvertraege belegen ausserdem
`20260905-204438-runtime-guest-smoke-libc-client.log` und
`20260905-204736-runtime-guest-smoke-memory-resilience.log`.

Historischer Blocker und Reparatur: Die erste 1024-MiB-Abnahme endete nach
180,41 s vor GTEST; ihr negatives Log bleibt als
`private-memory-gate-runtime-1024.log` und `private-memory-1024-first-failure.log`
erhalten. Nach expliziter Umfangsfreigabe (`e9f1bed0`) zeigten begrenzte
Font-I/O-Marker vollstaendigen VFS-Fortschritt statt Deadlock. Ein kontrollierter
Same-Image-A/B-Lauf belegte die Windows-11-Timerpolitik des unsichtbaren
QEMU-Prozesses als Testvoraussetzung: ca. 59-GHz-Fehlkalibrierung ohne Opt-out,
5,33 GHz und vollstaendiger Gastnachweis mit prozesslokalem Opt-out. Der
freigegebene Vertrag (`d687cbeb`) verlangt Readback und Reaping bei Fehlern;
andere Power-Policies, globale Windows-Einstellungen, Gastkalibrierung und
Zeitlimits bleiben unveraendert. Details und Microsoft-Referenz stehen im
Videovertrag. Der Desktop-Dateilader hat jetzt zusaetzlich eine aggregierte
30-s-Grenze und genau einen lokalen Session-Abschluss auch nach Fehlern.
Ein PowerShell-Parameterbindungsfehler in der Abschlusskette startete die
letzten zwei Gates zunaechst nicht; erst ihre korrigierten direkten Aufrufe
zaehlen als Ausfuehrung. Keine erfolgreichen Gastgates wurden wiederholt.

## Historie: abgenommener R3.9-Stand

Der unfertige R3.9-Browser ist vollständig einschließlich unversionierter
Dateien im lokalen Stash `a58233043f81ee80f08b7db3591d7ab2de76803c` gesichert.
Auf sauberer Grundlage `d725efb0` wurde die Wiederaufnahme in `495ed85f`
festgehalten. Die erlaubten Browser-/SDK-/Build-/Testdateien sind daraus jetzt
wiederhergestellt; Dokumentation wird mit der ATA-Abnahme zusammengeführt.
Die erfolglose ATA-Sleep-only-Probe und Kernel-/Treiber-Zeitdiagnosen wurden
nicht übernommen. Der akzeptierte ATA-/Prozesscode bleibt unverändert.
Die Logs bleiben unter `build/codex-agent/`; die bisherigen negativen
Gastnachweise werden nicht als Abnahme umgedeutet. R3.9 wird mit seinen
unveränderten Gates und Zeitlimits fortgesetzt. Der diagnostische QEMU-Build
ist bestanden. Der erste Gastlauf erreichte alle Interaktions-, HTML5-Fehler-
und Recovery-Marker, überschritt aber danach durch die alte einsekündige
Screenshot-Pause die 30-s-Probegrenze. Sein Runner-PASS war falsch: In der
abschließenden Close-Warteschleife fehlte die Prüfung später Fehlermarker.
`r39-resume-diagnostic.browser.log` bleibt negative Evidenz, keine Abnahme.
Der neue Host-Verhaltenstest führt den echten Runner-Zweig mit gestaffelten
Transkripten aus; er reproduzierte drei falsche Erfolge und besteht nach der
Korrektur. Screenshot-Aufnahme erfolgt jetzt nach dem Bild-Reload während
der nachfolgenden Fehlertests, ohne künstliche Gastpause; Fehler werden bis
zum Close geprüft. Kein Testschritt oder Zeitlimit wurde entfernt/verlängert.
Die eingefrorene R3.9-Abnahme ist vollständig bestanden. Alle sieben
Hostbefehle liefen einmal gegen ihren finalen Quellstand: 74 Tests insgesamt,
72 bestanden, zwei optionale Shell-Hosttests mangels erkanntem GCC/Clang
übersprungen. Die Browser-/Parser-C-Verhaltenstests liefen mit Zig und aktiven
Assertions. Befehle (jeweils `python test/<Datei> -v`), Ergebnis und Laufzeit:

| Datei | Ergebnis | Sekunden |
| --- | --- | ---: |
| `test_html_engine.py` | PASS, 1 Test mit 8 zusätzlichen Fallprozessen | 3,37 |
| `test_libc_source.py` | PASS, 4 Tests | 1,03 |
| `test_gui_browser_source.py` | PASS, 8 Tests | 27,28 |
| `test_browser_runtime_source.py` | PASS, 9 Tests | 1,92 |
| `test_browser_navigation_source.py` | PASS, 4 Tests | 1,21 |
| `test_shell_source.py` | PASS, 26/28, 2 optionale Skips | 0,24 |
| `test_user_program_toolchain.py` | PASS, 20 Tests | 112,55 |

`test-reist-package.ps1 -Target vmware -Video vga`: PASS in 52 s;
danach `test-reist-package.ps1 -Target qemu -Video vga`: PASS in 45 s.
`test-reist-runtime.ps1 -Mode runtime-desktop-browser -Target qemu -Video vga`:
PASS in etwa 86 s Hostzeit. Vollständiges Gasttranskript ohne späten
`BROWSER_PROBE_FAIL`: Bilder, URL-Chrome, Link-Release, Anker, Scrollbar-Capture,
Clipping, Reload, CURL-Fehler/Reap, echter HTML5-Worker, injiziertes UD2,
erzwungener Timeout, erfolgreiche anschließende Navigation und Close.
Der Worker-Spawn brauchte im finalen Gastlauf 604–904 ms einschließlich
synchronem Laden/Mapping; die erste komplette Worker-Transaktion bis
Output-Close 1592 ms. Die erste Fixture benötigte 14 ms Parserzeit.
Der kompakte Host-Fixture-Reply hat 4254 statt 133380 Byte. Worker-5-s-,
Browser-Probe-30-s- und Gastzeitlimits bleiben unverändert. Keine allgemeine
Latenzzusage für beliebige Webseiten oder VMware-Laufzeitabnahme.
Alle Logs unter `build/codex-agent/`: `r39-resume-gate-*.log`,
`20260905-160145-package-vmware-vga.log`,
`20260905-160253-package-qemu-vga.log`, `r39-accepted.browser.log` und
`r39-accepted.ppm` (unveränderte Aufnahme; PNG-Kopie ebenfalls abgelegt).

R3.9 liefert echten HTML5-Baumaufbau mit semantischer Projektion, keine
vollständige Browser-Engine. R3.10 übernimmt als nächster eigener Schnitt
CSS-Kaskade und Boxlayout im isolierten Worker. Externe CSS-Ressourcen,
Formulare, Cookies/POST und JavaScript sind weiterhin nicht implementiert.

ATA-Abnahme: alle 43 Tests der sechs eingefrorenen Hostbefehle bestanden.
`test-reist-package.ps1 -Target vmware -Video vga` bestand in 50 s;
derselbe QEMU-Befehl in 45 s. Der eingefrorene QEMU-Benchmark mit
`--min-read-kib-per-sec 400` bestand mit 635,23 KiB/s Lesen gegenüber
101,91 KiB/s zuvor: Faktor 6,23, vollständige 256-KiB-Byteprüfung, fsync,
Cleanup und Shell-Rückkehr. Benchmarkdauer 30,720 s. Schreiben maß
14,14 KiB/s und war ausdrücklich nicht Teil dieser Lesepfadoptimierung.
Kein Timeout, Journalformat, Schreibbefehl, AHCI-/DMA-Pfad oder öffentliches
ABI wurde gelockert. Logs: `ata-gate-*.log`, `ata-multiple-runtime.log` und
`20260905-153147-package-vmware-vga.log` /
`20260905-153311-package-qemu-vga.log` unter `build/codex-agent/`.
Die separate HTML5-Browser-Abnahme auf diesem ATA-Stand ist oben dokumentiert.
Der Nutzer hat R3.6b ausdrücklich mit unveränderten Anforderungen zurückgestellt
und automatische Browser-Fortsetzung ohne erneute Routinefreigaben beauftragt.
Keine parallelen manuellen Builds während der Agentenabnahme: Der erste
Hover-Neulauf verlor sein SDK durch einen gleichzeitig gestarteten Nutzerbuild.
Der anschließende unveränderte Lauf erreichte Desktop und Mauseingang, aber
keine Startmenü-Bestätigung. Logs: `r36b-hover-20260905-125318-serial.log` und
`r36b-hover-20260905-125318-vmware.log` unter `build/codex-agent/`.
Die Test-VM und ihre Oberfläche sind beendet; R3.6b ist nicht abgenommen.

Die folgenden Abschnitte halten die bisherigen Abnahmen und ihren historischen
Queue-Stand fest.
`R3.7-browser-http-navigation` ist nach allen eingefrorenen Gates abgenommen.
Auch `R3.6c-browser-interaction-and-images` ist jetzt abgenommen. Wie in R3.7
steht ein zusätzlicher QEMU-Referenzbuild nach dem unveränderten VMware-Build
und vor dem unveränderten Browser-Gastgate. Alle vier bisherigen Targeted-Gates
sind erhalten und bestanden. Grundlage: `805132f0`, Testvoraussetzung:
`de3ca2f5`. Dieser Lauf schließt die offene Abnahme der vorhandenen Implementierung;
er fügt noch keine neue Browser-Engine hinzu.
Nach R3.8 rückt gemäß Queue R3.6b vor; seine bisher offenen Nachweise sind
dadurch nicht bestanden. Kein Push.

`R3.8-ring3-browser-c-runtime` ist abgenommen; aktives Queue-Paket ist R3.6b.
Inventar vor R3.8: `x86os_malloc/free/realloc` sind vorhanden, aber nur Syscall-Wrapper;
TLS besitzt einen privaten Arena-Allocator und Bytefunktionen, der Browser
eigene Decoder-Bytefunktionen. Eine installierbare allgemeine C-Schnittstelle
für Upstream-Bibliotheken fehlte. R3.8 bündelt deshalb begrenzte Speicherverwaltung,
die benötigten C-Byte-/Stringfunktionen, SDK-Integration und die echte
NetSurf-Abhängigkeit LibWapcaplet einschließlich Host-/Gast-Fehlernachweis.
Der Nutzer beauftragt die automatische Fortsetzung bis zum Abschluss.
R3.8 ist jetzt implementiert und abgenommen: opt-in `libreistc.a`, gepinnte
LibWapcaplet 0.4.3, installierte Standardheader und `CRTEST.PRG`. Kernel, TLS und
vorhandene SDK-Verbraucher bleiben unverändert. Die Entwicklungsprüfung mit
echtem Allocator/Upstream-OOM ist bestanden, ebenso der Link aus dem installierten
SDK. Nach ausdrücklicher Reparaturfreigabe sind auch alle restlichen Gates
bestanden. Vollständiges libc, DOM/CSS und JavaScript werden damit nicht
behauptet. In diesem Paketdurchlauf wurde ausschließlich R3.8 umgesetzt.

### R3.8: abgeschlossene Abnahme

Alle drei Targeted-Gates bestehen: 51 Tests, davon 49 erfolgreich und zwei
bestehende optionale Shell-Hostcompiler-Tests übersprungen. Shell und vollständige
SDK-/Toolchainprüfung wurden nach dem alleinigen C++-Testtreiberfix nicht wiederholt.
Der reparierte libc-Gate wurde auf ausdrückliche Nutzerfreigabe einmal ausgeführt;
beide Referenzbuilds und der unveränderte Gastgate anschließend jeweils einmal.

| Befehl | Ergebnis | Dauer |
| --- | --- | --- |
| `python test/test_libc_source.py -v` | PASS, 4 Tests inkl. C++-Objekt | 0,8 s |
| `python test/test_user_program_toolchain.py -v` | PASS, 20 Tests | 100,8 s |
| `python test/test_shell_source.py -v` | PASS, 27 Tests, davon 2 übersprungen | 0,1 s |
| `.\scripts\test-reist-package.ps1 -Target vmware -Video vga` | PASS | 51 s |
| `.\scripts\test-reist-package.ps1 -Target qemu -Video vga` | PASS | 44 s |
| `.\scripts\test-reist-runtime.ps1 -Mode libc-client -Target qemu -Video vga` | PASS | 108 s |

Logs unter `build/codex-agent/`: `r38-gate-test_libc_source-object.log`,
`r38-gate-test_user_program_toolchain.py.log`, `r38-gate-test_shell_source.py.log`,
`20260905-121145-package-vmware-vga.log`, `20260905-121332-package-qemu-vga.log`,
`20260905-121455-runtime-guest-smoke-libc-client.log` und
`r38-libc-accepted-guest-transcript.log`.
Zwei vollständige Ring-3-Shellaufrufe von `crtest` bestätigen zwölf verschiedene
PID-/Generationspaare, zehn exakt abgeholte Kinder, sechs erfolgreiche Heap-/
Upstream-Prüfungen, zweimal kontrollierten Heapfehler/Abort und zweimal CPL3-UD2.
Nach jedem Aufruf kehrt die Shell zurück; kein Kernelpanic oder Neustart.
Das rohe Referenzimage enthält das QEMU-Profil, das separate VMware-Paket ist
unter `build/vmware/reist-os/` gebaut. Keine VMware-Laufzeit- oder CSS-/Browser-
Funktionsabnahme wird daraus abgeleitet. Die früheren Fehlversuche bleiben unten
als historische Evidenz erhalten.

### R3.8: historischer Testtreiber-Stopp

- `python test/test_shell_source.py -v`: PASS, 27 Tests, davon zwei bestehende
  optionale Hostcompiler-Tests übersprungen, 0,1 s.
- `python test/test_user_program_toolchain.py -v`: PASS, 20 Tests, 100,8 s.
  Der gemeinsame Zig-Resolver aktiviert die zuvor wegen eines veralteten
  Fallbackpfads übersprungenen Toolchain-Tests. Enthalten sind ein vollständiger
  Systemprogrammbuild, ein wirklich externes C-Programm gegen das installierte
  SDK, MYPR-Validierung und die unveränderte GUI-Inkrementalgrenze. Der veraltete
  Image-Verbrauchervergleich berücksichtigt jetzt den bereits vorhandenen Browser.
- `python test/test_libc_source.py -v`: FAIL, 29,1 s, nur der zusätzliche
  C++-Headercheck. Zig 0.16.0 meldet bei `-fsyntax-only` `FileNotFound` für seine
  stdin-Quelldatei. Der eine fokussierte Reparaturversuch schreibt die vier
  Header-Includes in eine echte temporäre `.cpp`-Datei; derselbe Gate-Befehl
  scheitert erneut nach 0,8 s an `headers.cpp:1:1: error: FileNotFound`.
  Die übrigen drei Tests einschließlich echtem Heap und Upstream-OOM bestehen.
- Die Paket-Stoppregel „a frozen gate fails after one focused in-scope repair“
  ist erreicht. Keine zweite Implementierungsreparatur und keine Paket-/Gastgates
  gestartet. Ein begrenzter Diagnosecompile desselben SDK-`stdlib.h` mit `-c`
  statt `-fsyntax-only` besteht in 0,3 s; das ersetzt den offenen Header-Gate nicht.

Logs unter `build/codex-agent/`: `r38-gate-test_shell_source.py.log`,
`r38-gate-test_user_program_toolchain.py.log`, `r38-gate-test_libc_source.py.log`,
`r38-gate-test_libc_source-repair.log`, `r38-header-diagnostic.log`.
Der Nutzer hat danach mit „ja und mach weiter“ ausdrücklich einen weiteren auf
den C++-Testtreiber begrenzten Reparaturversuch freigegeben: reguläre
Objektübersetzung statt Syntax-only, dieselben Header und dieselbe Frist,
zusätzlich ein zwingender Objektdateinachweis. Der attributierte Kandidat wird
im Hauptworktree fortgesetzt; Shell-/SDK-Ergebnisse bleiben erhalten, danach
folgen der reparierte libc-Gate und die unveränderten Paket-/Gastgates.
Quelländerungen und Evidenz bleiben im sichtbaren Hauptworktree erhalten.

## Abnahme R3.6c

Alle Gates liefen für den wiederaufgenommenen Paketstand jeweils einmal.
26 Targeted-Tests: 24 erfolgreich, zwei bestehende optionale Compiler-Tests
übersprungen. Der echte Surface-Rückstautest und die Browser-/Bildmodelle
wurden mit Zig ausgeführt; die Browser-Range-Fälle sind dort ebenfalls enthalten.

| Befehl | Ergebnis | Dauer |
| --- | --- | --- |
| `python test/test_gui_browser_source.py -v` | PASS, 8 Tests | 27,3 s |
| `python test/test_browser_runtime_source.py -v` | PASS, 7 Tests | 1,5 s |
| `python test/test_gui_value_controls_source.py -v` | PASS, 2 Tests, davon 1 übersprungen | 0,03 s |
| `python test/test_gui_surface_source.py -v` | PASS, 9 Tests, davon 1 übersprungen | 0,4 s |
| `.\scripts\test-reist-package.ps1 -Target vmware -Video vga` | PASS | 47 s |
| `.\scripts\test-reist-package.ps1 -Target qemu -Video vga` | PASS | 43 s |
| `.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-browser -Target qemu -Video vga` | PASS | 94 s |

Logs unter `build/codex-agent/`: `r36c-resume-test_*.log`,
`20260905-112202-package-vmware-vga.log`,
`20260905-112304-package-qemu-vga.log`,
`r36c-resume-browser-runtime.log` und `r36c-resume-browser-guest-transcript.log`.
Der Gast bestätigt URL-Eingabe ohne Dokument-Repaint, Bilder, Link-Release,
Anker, Scrollbar-Capture, Scroll-Clipping, Reload, Transportfehler-Abholung und
sauberes Schließen. Keine neue VMware-Laufzeit- oder öffentliche Webseitenabnahme;
R3.6b bleibt offen. Das rohe Image enthält das QEMU-Profil, das separat gebaute
VMware-Paket liegt weiterhin unter `build/vmware/reist-os/`.

Der abgenommene Stand `BROWSER_BUILD navigation-20260905-r5` ergänzt HTTP/1.1-GET,
`curl --include/-i`, begrenztes chunked-Decoding und echte HTTP-Weiterleitungen
für Dokumente und Bilder. Kopf und dekodierter Body werden gemeinsam publiziert;
Close-/Rename-Fehler melden Misserfolg. Abgebrochene eigene Kindprozesse werden
vor dem Entfernen ihrer Teil-/Ergebnisdateien abgeholt. Fehlerantworten,
ungeeignete Darstellungen, HTTPS-Downgrades und Schleifen erhalten die alte Seite.

Hosttests führen den echten CURL-Stream-/Publikationscode und den
Browser-Ladezustand aus: fragmentierte Antworten, Weiterleitungsketten, effektive
Bild-/Linkbasis, Esc, neue Navigation, Zeit-/Bytebudgets und Schreibfehler.
Die neue Desktop-Hostregression führt den echten Start-Dateileser mit
fragmentierten Reads, Größen-/Antwortfehlern, Timeout, Lifecycle-Fortschritt
und Handle-Cleanup aus. Sie fixiert außerdem die Build-/Runtime-Zielreihenfolge.
Der Desktop meldet die optionale Beschleuniger-Verbindung separat als
`accel-info`; funktionale VFS-, Treiber-, Kernel- oder ABI-Änderungen waren
für die aufgeklärte Startblockade nicht erforderlich.

## Abnahme R3.7

Alle folgenden Gates wurden für den finalen Kandidaten jeweils einmal
ausgeführt und bestanden. Targeted insgesamt: 102 Tests, davon 98 erfolgreich
und vier bestehende optionale Tests übersprungen. Die zwingende Zig-Hostprüfung
führt den CURL-URL-/Headerparser auch ohne GCC aus; der TLS-Hostnachweis besteht.

| Befehl | Ergebnis | Dauer |
| --- | --- | --- |
| `python test/test_desktop_startup_source.py -v` | PASS, 2 Tests | 0,6 s |
| `python test/test_desktop_source.py -v` | PASS, 58 Tests | 0,4 s |
| `python test/test_browser_navigation_source.py -v` | PASS, 4 Tests | 0,9 s |
| `python test/test_gui_browser_source.py -v` | PASS, 8 Tests | 29,4 s |
| `python test/test_browser_runtime_source.py -v` | PASS, 7 Tests | 1,4 s |
| `python test/test_gui_surface_source.py -v` | PASS, 9 Tests, davon 1 übersprungen | 0,3 s |
| `python test/test_network_tools.py -v` | PASS, 14 Tests, davon 3 übersprungen | 89,9 s |
| `.\scripts\test-reist-package.ps1 -Target vmware -Video vga` | PASS | 14 s |
| `.\scripts\test-reist-package.ps1 -Target qemu -Video vga` | PASS | 45 s |
| `.\scripts\test-reist-runtime.ps1 -Mode curl-client -Target qemu -Video vga` | PASS | 117 s |
| `.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-browser -Target qemu -Video vga` | PASS | ca. 86 s |

Logs unter `build/codex-agent/`:

- `r37-startfix-gate-*.log`
- `20260905-110240-package-vmware-vga.log`
- `20260905-110344-package-qemu-vga.log`
- `20260905-110443-runtime-guest-smoke-curl-client.log`
- `r37-startfix-curl-guest-transcript.log`
- `r37-startfix-browser-runtime.log`
- `r37-startfix-browser-guest-transcript.log`

Der CURL-Gast erreicht `TEST_OK`, die unveränderte Unicode-Prüfung und
`REIST_CURL_RUNTIME_OK` mit `X-Reist-Transport: chunked-include`.
Gemessene Startphasen dort: `accel-info` 4 ms, `font-io` 20507 ms,
`font-parse` 25 ms. Der Browser-Gast bestätigt URL-Eingabe ohne Dokument-Repaint,
Bilder, Links/Anker, Scrollbar-Capture, Scroll-Clipping, Reload, Transportfehler
und sauberes Schließen. Das sind begrenzte QEMU-Nachweise, keine Abnahme beliebiger
öffentlicher Webseiten oder der VMware-Laufzeit/Zeigerlatenz.

`build/reist-os.img` enthält nach der Abnahme das QEMU-Profil; das separat
gebaute VMware-Paket liegt weiterhin unter `build/vmware/reist-os/`.
Die Tests verwenden eigene Snapshot-Gäste; keine Benutzer-VM wurde bedient.

NetSurf bleibt der bevorzugte Portierungskandidat, noch nicht integriert.
Fehlende allgemeine ISO-C-/Datei-/Zeit-/Speichergrundlagen werden als echte
Ring-3-SDK-Verträge nachimplementiert, nicht als funktionslose Browser-Stubs.
CSS, DOM/Formulare und später isoliertes JavaScript sind noch nicht umgesetzt.
Details: `docs/architecture/BROWSER_ENGINE_PORT_PLAN.md`.

## Historischer R3.7-Zwischenstand und Startdiagnose

Transportvertrag: `e5ae16b8`, Ausgangsbasis `cd7025a2`. Die ersten fünf
Targeted-Gates bestanden (38 erfolgreich, vier optionale Tests übersprungen),
Logs `build/codex-agent/r37-gate-*.log`. Der erste VMware-Build bestand in 22 s,
Log `build/codex-agent/20260905-103403-package-vmware-vga.log`.

`test-reist-runtime.ps1 -Mode curl-client -Target qemu -Video vga` scheiterte
nach 180 s **vor** dem CURL-Aufruf: GTEST erreicht `VFAT_UTF8_OK`, startet
`desktop --unicode-probe` und erreicht `TEST_OK` nicht. Der Desktop protokolliert
`splash`, aber noch kein `font`/`font-io`. Das grenzt den fehlenden Fortschritt
auf den unveränderten Desktop-Startpfad zwischen SVGA-Verbindung und Font-Laden
ein; die genaue Blockierstelle ist noch nicht bewiesen. Log:
`build/codex-agent/20260905-103457-runtime-guest-smoke-curl-client.log`.
Der anschließende eingefrorene Browser-Gasttest wurde nach diesem ersten
Fehlschlag nicht gestartet. Der CURL-/Browser-Gastnachweis blieb damals offen.

Paketstopp: `userspace/gui/compositor/desktop.c` und der allgemeine
`userspace/programs/guest_test.c` liegen außerhalb des Transportpakets. Keine
Abschwächung oder Umgehung des GTEST-Gates und keine stille Umfangserweiterung.
Der Nutzer hat danach ausdrücklich zugestimmt, R3.7 als **nicht abgenommenen
Zwischenstand** zu committen und den Paketumfang anschließend um die
Desktop-Startblockade zu erweitern. R3.7 blieb bis zur oben dokumentierten
Abnahme aktiv; der Checkpoint allein bewirkte keinen Queue-Fortschritt.

Zwischenstand: `abe907e1`, begrenzte Reparaturfreigabe: `780dbe65`.
Der freigegebene Reparaturumfang ergänzte ausschließlich
den bestehenden Ring-3-Desktop-Startpfad, dessen VFS-Dateiclient und reale
Hostregressionen/Diagnosen. Kernel, öffentliche ABI, GTEST-Anforderungen und
Gastzeitgrenzen bleiben unverändert. Accelerator-Verbindung und Font-I/O werden
vor einer funktionalen Änderung getrennt beobachtet; eine fehlende optionale
Ressource darf keine endlose Startabhängigkeit erzeugen.

Die begrenzte Diagnose zeigt: Das separat mit `Target=qemu` gebaute Image
erreicht nach Font-I/O (11 s ohne NIC, rund 20 s im GTEST mit RTL8139) die
Unicode-Marker. Die Beschleuniger-INFO dauert 3 ms. Der vorangegangene
Abnahmeablauf hatte dagegen nach dem VMware-Paket dessen Image unter QEMU
gestartet: `test-reist-runtime -Target qemu` wählt nur den Emulator und baut
das Image nicht neu. `VMWARE_BUILD` verwendet 10-ms-ATA-Polls, `QEMU_BUILD`
1-ms-Polls. Ein zusätzlicher QEMU-Referenzbuild vor den unveränderten Gastgates
stellt nun die korrekte Testvoraussetzung her; der VMware-Paketbuild bleibt
erhalten. Diese Testvoraussetzung wurde vor dem finalen Kandidaten in
`e829a48f` festgelegt. Kein Treibertiming und keine Gastfrist wurden gelockert.
Die anschließenden regulären Abnahmen sind oben getrennt von den Diagnoseläufen
belegt. Ein Kernel-Deadlock wurde nicht nachgewiesen oder als behoben behauptet.

## Historischer Checkpoint R4 (`cd7025a2`)

Damals aktiver Auftrag: Browser-Bedienung und Bilder verbessern. Aktives Paket war
`R3.6c-browser-interaction-and-images`; `R3.6b-vmware-pointer-pinned-mutex` ist
mit offenen Nachweisen zurückgestellt (`queued`), nicht abgeschlossen.
Basis: `7b365f3b`; Paketvertrag: `337318d9`. Der Browser-Kandidat wird auf
ausdrücklichen Nutzerwunsch als lokaler Zwischenstand gesichert und ist
**nicht abgenommen**. Das Paket bleibt aktiv, alle eingefrorenen Gates und
offenen Nachweise bleiben bestehen; kein Push.

Der Nutzer bestätigt jetzt sichtbar verbesserte Stabilität, fordert aber einen
umfangreicheren Browser und vor dem nächsten großen Umbau diesen Commit.
Sein Screenshot von `https://google.com` zeigt eine gerenderte `301 Moved`-
Antwort mit Link sowie den Status „Laden abgelehnt – bisherige Seite bleibt“.
Dies dokumentiert insbesondere die fehlende automatische HTTP-Weiterleitung,
nicht eine erfolgreiche vollständige Darstellung von Google. Der nächste
Funktions-/Engine-Schnitt ist noch nicht definiert und wird mit diesem
Sicherungsauftrag nicht implementiert. JavaScript bleibt für einen späteren
isolierten Ring-3-Dienst vorgesehen.

Damals aktueller Build: `BROWSER_BUILD interaction-20260905-r4`. Der neu erfasste
Scrollfehler ist `buffer-unregister code=-110`, bei Scrollposition 719 nach
zwölf Dokumentframes; anschließend ebenfalls Surface-Cleanup-Timeout.
Der Nutzer hat die Reparatur der gemeinsamen Surface-IPC-Bibliothek und ihrer
Tests ausdrücklich freigegeben und verlangt, dass gewöhnlicher Rückstau keine
Anwendung beendet. Der Paketumfang wurde genau dafür erweitert; Kernel und
öffentliche Schnittstellen bleiben unverändert.

Ursache: Der Kernelendpunkt hat vier gemeinsame Nachrichtenplätze für beide
Richtungen. Füllen Desktop-Eingaben diese Plätze, wartet der bisherige
blockierende Client-Send auf den Desktop, der seine eigenen Eingaben nicht
abholen darf. Die echte Clientbibliothek reproduziert im Hosttest den Timeout
ohne einen einzigen Receive. Jetzt versucht sie nichtblockierend zu senden,
holt bei Rückstau validierte Ereignisse in die vorhandene geordnete gemeinsame
Event-Owner-Queue und versucht erneut. Keine reentrante Verarbeitung und keine
verlorenen Close-/Button-Kanten; ohne abholbare Eingabe begrenztes Schlafen.
500-ms-Sendebudget, Arbeitsobergrenze, Kapazitäts- und Protokollprüfungen bleiben
erhalten. Der neue Hosttest deckt mehrere Surfaces, Reihenfolge, Rückstau beider
Richtungen, fortgesetzten Eingang, volle lokale Queue, Deadline, stehende und
rückwärtslaufende Uhr, Schlaf-/Protokollfehler und Close ab. Vorher rot, danach
grün: `surface-backpressure-before.log`, `surface-backpressure-after.log` und
`surface-backpressure-final-targeted.log` unter `build/codex-agent/`.

Zuvor ergänzt: UTF-8-sichere Alternativtexte, geclippte Bild-Link-Treffer und
Unterstreichungen sowie vollständige Glyphen im Viewport. Der Renderer-Hosttest
benutzt den echten Surface-Manager und Fontvalidator für jede Scrollposition,
dekodierte/fehlgeschlagene Bilder, kleine Fenster und hochgeladene Pixel.

Isolierter VMware-Selbsttest des R4-Builds bestanden: tatsächliche Bildanzeige
per Framebuffer-Aufnahme, Scroll-Clipping, Adress-Chrome, Links, Reload,
abgeholtes fehlerhaftes CURL-Kind und sauberes Schließen. Belege:
`build/codex-agent/r4-isolated-selftest.log` und
`build/codex-agent/browser-isolated-r3/vmware/reist-os/selftest.png`.
Der Ordnername bleibt historisch R3, der Gast meldet ausdrücklich R4.
Ein längerer echter Maus-Scrolllauf mit beiden `intracom.at`-Adressvarianten
ist damit noch nicht nachgewiesen. Die automatische Public-Prüfung kam wegen
nicht ankommender RFB-Mausbewegungen nicht bis zum Seitenaufruf. Der sichtbare
R3-Lauf lieferte den oben genannten Timeout, nicht eine erfolgreiche Abnahme.

Bei der ergänzenden Screenshotprüfung kollidierte Port 5909 der Testkopie mit
der inzwischen gestarteten Nutzer-VM. Teile eines Testkommandos erreichten
offenbar diese VM. Prüfung gestoppt, nur Testkopie beendet, Nutzer informiert.
Die Fortsetzung benutzt ausschließlich Test-Port 5997, separate VM-Dateien und
prüft Listener-PID, exakten VMX-Pfad, aktuelles VMware-Log und RFB-Servernamen
vor Eingaben. Keine Eingabe geht an 5909. Der R4-Referenzbuild erfolgte erst
nach bestätigtem Stillstand aller VMs; seine Disk wurde in die ausgeschaltete
Test-VM kopiert. Die Test-VM wurde nach dem Selbsttest beendet.

Implementierter, noch nicht im Gast abgenommener Gesamtstand: separate Adress-/Statusebenen,
32er-Eingabebatches, URL-Cursor, Link-Press/Release, Fragmentziele, vorhandener
Range-Controller als Scrollbar, Bild-Unterlage über generationgebundene Surface-
Buffer und serielle CURL-Jobs mit Zeit-/Bytegrenzen. PNG/JPEG verwenden den
gepinnten stb_image-v2.30-Adapter mit 12-MiB-Arena; BMP/GIF die vorhandene
Bibliothek. Einmalige feste Workspace-Reservierung von etwa 22 MiB beim Start;
keine Änderung der 8-MiB-Programmgrenze oder Kernel-/Surface-ABIs.

Aktuelle Fortsetzung: Nutzer meldet sofortiges Schließen bei fast jeder URL,
darunter `intracom.at` und `https://intracom.at`. Das echte VMware-Protokoll
zeigt nach `BROWSER_RENDER_OK` wiederholt `BROWSER_PROBE_FAIL cleanup`, einmal
zusätzlich einen CURL-DNS-Fehler. Ursache im Browser: `PROCESS_IDENTITY` wird
auch nach regulärem Kind-Exit verlangt, obwohl diese Kernelabfrage nur lebende
Identitäten liefert. Dadurch endet der Browser und kann das Zombie-Kind auch
beim Cleanup nicht abholen. Keine Kernel-/ABI-Änderung erforderlich.

Korrigiert: zuerst Parent/PID/Status prüfen, für lebende Kinder zusätzlich die
Generation; der parentgebundene, nicht wiederverwendbare Zombie bleibt bis zum
eigenen `wait` abholbar. Exit vor dem ersten Identitätssnapshot, zwischen
Status/Identität und zwischen Status/Kill werden begrenzt behandelt. Unbekannte
Generationen oder fremde Eltern bleiben fail-closed. Normale Transportfehler
erhalten Fenster und bisherige Seite. Regression vor Reparatur reproduziert,
danach bestanden: Host-Harness inkludiert die echte `main.c` und mockt nur
OS-/IPC-Grenzen; erfolgreiche Veröffentlichung, DNS-/Bildfehler, Exit-Races,
Abbruch, Zeitlimit, idempotentes Abholen und fremde/stale Identitäten geprüft.

Titel werden UTF-8-sicher gekürzt: Parser höchstens 127, Surface höchstens
39 Bytes. Die erste Annahme einer 63-Byte-Parsergrenze war falsch und wurde
korrigiert; der Titel von `intracom.at` überschreitet nur die Surfacegrenze.
Zu lange optionale Parser-Titel verhindern ebenfalls nicht mehr das Laden;
ungültiges UTF-8 nach einer Kürzung wird weiterhin abgelehnt. Die öffentliche
HTML-Antwort (16707 Bytes) besteht den echten Parser-/Layout-/Publikationspfad
auf dem Host: 156 Elemente, 176 Layout-Runs. Das beweist keine erfolgreiche
DNS-/HTTPS-Verbindung aus der VM; der beobachtete DNS-Fehler ist nicht behoben.

Targeted R4: `python test/test_gui_browser_source.py -v` besteht mit acht
tatsächlich ausgeführten Fällen (29,3 s), darunter Bilddekodierung samt
beschädigten Eingaben, Titelgrenzen und Scrollbar-/URL-Hostverhalten.
Der neue Titeltest hatte zunächst selbst eine falsche Kapazitätsannahme;
korrigierter Grenztest und explizit aktive Assertions bestehen.
`test_browser_runtime_source.py -v`: sieben bestanden (1,5 s);
`test_gui_value_controls_source.py -v`: einer bestanden, ein optionaler
Compiler-Skip; `test_gui_surface_source.py -v`: acht bestanden, ein
optionaler Compiler-Skip. Zusammen 24 bestanden und zwei Skips.
Die neuen Browser-Hostfälle verwenden Zig; sie wurden nicht übersprungen.
Das gilt auch für den neuen echten Surface-Client-Hosttest. Logs:
`build/codex-agent/r4-test_*.log`.

`.\scripts\test-reist-package.ps1 -Target vmware -Video vga`: PASS,
R4-Kandidatenbuild 25 s, Log
`build/codex-agent/20260905-094020-package-vmware-vga.log`.
Der normale Build einschließlich VM-Abbildern enthält R4; alle GUI-Programme
wurden mit der korrigierten gemeinsamen Clientbibliothek neu gelinkt.
Die Kernel-/Surface-Schnittstellen sind unverändert. Die Erweiterung des
Paket-Dateiumfangs um die gemeinsame Clientbibliothek und Tests ist explizit
freigegeben und im Paketvertrag dokumentiert.
`git diff --check` und Scope-Prüfung bestehen.

QEMU-Gastabnahme weiterhin offen: Der einmal ausgeführte eingefrorene R4-Lauf
`.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-browser -Target qemu -Video vga`
scheitert nach rund 120 s erneut vor der Browserprüfung. Ring-3-Shell und
Display-Aktivierung vorhanden, letzter Startup-Marker `font`, kein
`DESKTOP_OK` und keine Browser-Marker. Log:
`build/codex-agent/r4-runtime-qemu.log`. Diagnoseursache des QEMU-Desktopstarts
weiterhin nicht gesichert; keine Abschwächung von Markern, Schwellen oder
Hardwareprofil. VMware-Erfolg ersetzt dieses eingefrorene Gate nicht.
Frühere QEMU- und Nutzerdiagnosen bleiben unverändert erhalten.
Quelländerungen am Start-/Compositorpfad außerhalb des aktuellen Dateiumfangs
benötigen einen explizit freigegebenen Reparaturschnitt. Nach dieser Runde
greift die Stop-Regel erneut.
Weitere offene Browser-Nachprüfung: langer Public-URL-/Mauslauf und extreme
Bildseitenverhältnisse gegen die gekappte Scrollbar-Obergrenze. Das bestätigte
256-KiB-/1024x768-Limit bleibt bestehen: zu große Seitenbilder ergeben weiterhin
Alternativtext. Lokale Format-/Kapazitäts-Hosttests ersetzen diese
Laufzeitnachweise nicht.
Keine Schwelle gelockert und kein Paket als fertig markiert. Der ausdrücklich
gewünschte Zwischenstand-Commit ersetzt keine Abnahme. Vollständige allgemeine HTML5-/CSS-/JavaScript-
Kompatibilität wird nicht behauptet; JavaScript bleibt inert.

Die folgenden Abschnitte dokumentieren den vorangegangenen VMware-Auftrag.

Der Nutzer bestätigt die deutlich verbesserte Geschwindigkeit und hat
ausdrücklich angewiesen, den gesamten Änderungsstand lokal zu committen.
Dieser Sicherungscommit ist keine vollständige Paketabnahme: Die unten
genannten offenen Laufzeitnachweise bleiben offen, das Paket bleibt aktiv.

Aktueller Reparaturstand (vollständige Abnahme noch offen): Die frühe
VMware-Erkennung legte den Framebuffer UC an; die spätere WC-Anforderung
scheiterte korrekt am Cache-Konflikt. Der erste Scanout-Mappingversuch verwendet
jetzt WC, FIFO/Register bleiben UC. PAT wird zusätzlich auf jeder CPU vor
Online vorbereitet; die PAT-Korrektur allein beseitigte die Kopierlatenz nicht.
Runtime-CPUID im CPU-lokalen Pfad ist durch validierte private GDTR-Bindungen
ersetzt. Die gemessene Pixelkopie war die entscheidende verbleibende Bremse.

Der normale VMware-Paketbuild ist bestanden (14 s). Fünf Targeted-Suites
melden 144 Fälle, davon 141 bestanden und drei optionale Hostcompiler-Skips;
die neuen CPU-Identitäts- und Greifpunkt-Hosttests laufen wirklich über Zig.
Der Vier-vCPU-QEMU-Boot einschließlich SMP-Prüfungen ist bestanden. Der echte
Vier-vCPU-VMware-Render-/Lifecycle-Lauf mit anschließender Shell und zehn
Sekunden Stabilitätsprüfung ist bestanden (26 s): Vollbild 15 ms,
Verschieben maximal 11 ms, Resize maximal 2 ms, acht beschleunigte Moves,
keine Fallbacks, keine Clock-/Probe-Fehler. Greifpunkt und Fensterposition
bleiben im nativen WM-Test und im Gast-Renderprobe exakt gekoppelt.
Die temporären Laufzeitmessausgaben sind aus dem Produktionscode entfernt.

Offene Abnahme: Der RFB-Hoverlauf bestätigt Start weder versteckt noch nach
ausdrücklich erlaubtem sichtbarem Start. Die temporäre Koordinatensonde sah
beide Tastenflanken bei unverändert `(512,384)` trotz eingespeister Bewegung;
das ist kein bestandener realer Bewegungs-/Latenznachweis. Die Sichtbarkeit des
Testfensters ist deshalb ausdrücklich über `-Visible` opt-in. Der ergänzende
Vier-vCPU-QEMU-Desktop-Renderlauf erreichte nach der Befehlseingabe kein
`DESKTOP_OK`; Boot/SMP dagegen sind nachgewiesen. Keine Schwelle wurde gelockert,
kein fehlgeschlagener Lauf als Erfolg verbucht. Das Paket bleibt aktiv.
Logs liegen unter `build/codex-agent/`, insbesondere
`20260904-225234-runtime-vmware-mouse.log`, `final-qemu-smp.log` und
`final-qemu-render.log`. Die nachfolgende Beschreibung ist der frühere
Diagnoseverlauf; die vorstehenden Werte sind der aktuelle Kandidatenstand.

Frühere Nachprüfung: Die Maus funktioniert laut Nutzer, der Desktop bleibt jedoch
unbrauchbar langsam. Die manuelle VMware-Spur enthält 414 ms für ein Vollbild
und bis zu 399 ms für Resize. Die bisherige Reparatur ist nicht abgenommen.
Eine weitere gemeinsame Kostenquelle ist die doppelte CPUID-Abfrage bei jedem
CPU-lokalen Zugriff (auch IRQ, Locks und Scheduler). Der aktive Reparaturschnitt
prüft daher eine validierte Zuordnung über die bereits private GDT jeder CPU;
CPUID bleibt für Bootstrap und Identitätsprüfung beim Binden erhalten.

Branch/Startpunkt: `working_branch` / `50bf1044`

Aktives Reparaturthema: `R3.6b-vmware-pointer-pinned-mutex`. Das aktuelle
VMware-Abbild erreicht zwar `DESKTOP_MOUSE_OK`, bestätigt den Start-Klick der
realen RFB-zu-xHCI-Trajektorie danach aber nicht innerhalb von 75 Sekunden.
Seit der generationgenauen Mutex-Recovery registriert jede Cursor-Publikation
den Display-Mutex unter dem globalen Task-Table-Lock, obwohl der kurze
Cursorpfad bereits präemptionsgepinnt ist. Der Reparaturschnitt ergänzt eine
lockfreie Validierung der bereits gepinnten lokalen Taskidentität sowie eine
explizite nichtblockierende, bis zum finalen Unlock gepinnte Mutexakquisition
für genau diesen Pfad. Außerdem wird der veraltete Hover-Nachweis auf die
aktuellen sieben Startmenüeinträge aktualisiert. Normale Mutexbesitzer bleiben
vollständig generationgenau verfolgt. Der erste Reparaturlauf öffnete Start und
erreichte alle sieben Zeilen; Menüframes blieben bei 3 ms, der bisherige
Softwarecursor benötigte jedoch bis zu 66 ms pro Publikation. Ein daraufhin
capability-geprüft implementierter `SVGA_FIFO_CAP_CURSOR_BYPASS_3`-Prototyp
beseitigte zwar die Pixelpublikation, unterbrach im getesteten Workstation-
Profil aber sowohl reale als auch RFB-gesteuerte relative xHCI-Mausbewegung.
Auch der anschließend erprobte physische VMware-Hostcursor blieb unsichtbar und
erhielt die Klickkopplung nicht. Beide Hardwarepfade sind deshalb verworfen.
Der aktive Schnitt behält den sichtbaren Softwarecursor und entfernt stattdessen
in Shadow-Modus dessen redundante zweite Hintergrundkopie: alte Zeigerfläche
restaurieren, neuen Zeiger direkt in den Scanout zeichnen, alten und neuen
Schaden einmal publizieren. Eine Messsonde bestätigte zuvor, dass der bedingte
SVGA-Doorbell selbst 0 Gastmillisekunden beansprucht.

`R3.6a-browser-input-and-mutex-owner-recovery` ist abgeschlossen. Ein Klick in
die Browser-Adressleiste ersetzt beim ersten Editieren jetzt den bisherigen
Inhalt; nackte Hostnamen erhalten begrenzt `https://`, explizites `http://` und
`https://` bleibt möglich. Taskgenerationen verfolgen außerdem bis zu acht
verschiedene gehaltene Kernel-Mutexe. Bei erzwungener Beendigung wird nur der
Besitz der exakt gepinnten, nicht mehr laufenden Generation vor dem VFS-Cleanup
aufgehoben. Damit entfällt die reproduzierte Kaskade aus drei VFS-Timeouts zu je
zehn Sekunden samt degradiertem Compositor. Die fünf Browser-, 29 SMP-/Mutex-
und fünf VFS-Prüfungen, das 43-Sekunden-QEMU-Paketgate sowie beide realen
Desktop-Läufe sind bestanden. Der normale Start und Neustart wurden ohne
`-11`-Ladefehler mit 23 811 ms beziehungsweise 23 507 ms gemessen. Diese
verbleibende Startzeit stammt aus dem bereits vorhandenen synchronen Laden des
großen Unicode-Fontkatalogs und ist ausdrücklich ein separates Folgethema. Es
ist kein weiteres Paket eingereiht und `active_id` ist leer.

Abgeschlossenes Thema: `R3.6-surface-web-browser`. Der neue Ring-3-Surface-Client
verwendet den vorhandenen, abgenommenen `CURL.PRG`-Transport mit festem
64-KiB-Limit und rendert einen dokumentierten HTML-Teilsatz aus einem
heapfreien, festkapazitiven Dokumentmodell. URL-Leiste, semantische
Überschriften/Absätze/Listen/Links, Umbruch, Scrollen und Linknavigation werden
begrenzt; CSS, Formulare und Medien bleiben ausdrücklich unsupported und
Skripte in diesem Schnitt inert. Eine spätere JavaScript-Engine wird als
eigener quota- und generationgebundener Ring-3-Dienst über versionierte IPC
angebunden, nicht in den Browser oder Kernel eingebettet. Browser und Curl
erhalten getrennte Fehlerdomänen, der Browser selbst keine rohe Netzwerk- oder
TLS-Autorität. Alle vier Targeted-Gates, das QEMU-Framebuffer-Paketgate und der
reale Surface-Lauf für Rendern, Scrollen, Link, Reload und sauberes Schließen
sind bestanden; es ist kein weiteres Paket eingereiht und `active_id` ist leer.

`N3f-ext2-cross-sector-rename` ist abgeschlossen. Der begrenzte Folgeschnitt
erweitert gleichverzeichnisiges No-replace-Rename ausschließlich auf einen
vorhandenen Zielrecord in einem zweiten 512-Byte-Sektor desselben EXT2-
Directory-Blocks. Beide vollständigen Sektoren werden vor Planung validiert
und als eine alte-oder-neue Undo-Journaltransaktion publiziert. ABI, Inode,
Dateidaten, Allokation, Cross-Directory, Replace und Verzeichnisse bleiben
unverändert beziehungsweise ausgeschlossen. Alle 38 Targeted-Checks, der
46-Sekunden-QEMU-Framebuffer-Paketbuild und der finale 95-Sekunden-EXT2-Lauf
sind bestanden. Der Lauf erzwang beide Zielrecords in Sektor 2, startete den
Storage-Dienst neu und bestätigte anschließend direkt im persistenten Image
Offsets 512 und 540, unveränderte reguläre Inode-/Dateidaten sowie zwei saubere
Journalheader.

`N3e-ext2-same-sector-rename-growth` ist abgeschlossen.
Das vorhandene gleichverzeichnisige No-replace-Rename kann einen längeren
Zielnamen nun in einen freien EXT2-Record oder `rec_len`-Slack umplatzieren,
wenn Quellentfernung, Donor-Split sowie vollständiger Zielheader und -name in
genau denselben 512-Byte-Publikationssektor passen. Inode, Dateidaten,
Allokationen, ABI und Ring 0 bleiben unverändert; Cross-Sector,
Cross-Directory, Replace und Verzeichnisse scheitern vor Medienwirkung. Alle
38 Targeted-Checks, der 80-Sekunden-QEMU-Framebuffer-Paketbuild und der finale
110-Sekunden-EXT2-Lauf mit beiden wachsenden Renames, Dienstneustarts,
anschließendem Unlink und sauberen Journalheadern sind bestanden.

`N3d-ext2-regular-unlink` ist abgeschlossen. Operation
34 entfernt nun ohne ABI-Änderung reguläre EXT2-Dateien mit Linkzähler eins,
höchstens 64 eindeutigen direkten/einfach-indirekten Allokationen und einem
vollständig in das vorhandene 24-Sektor-Undo-Journal passenden Commit.
Sparse-, EA-, Double-/Triple-Indirect-, Journal-, Directory-, Struktur-,
Duplikat- und unallokierte Blockfälle werden vor jedem Medienwrite abgewiesen.
Die Direct-/Single-Indirect-Hostmatrix deckt jeden Write-/Flush-Abbruch ab.
Alle 38 finalen Targeted-Checks, der 68-Sekunden-QEMU-Framebuffer-Paketbuild
und der reale 104-Sekunden-EXT2-Lauf mit persistentem DEL, erneutem
Storage-Neustart, freigegebenen Bitmapbits und sauberen Journalheadern sind
bestanden. `STORAGE.PRG` bleibt 188416 Byte groß.

`N3c-ext2-regular-rename` ist abgeschlossen. Der
begrenzte N3-Schnitt erweitert die vorhandene gleichverzeichnisige
EXT2-Rename-Transaktion auf reguläre Dateien. Operation 34, der feste Frame,
der Ring-3-Client und der FAT-Fallback bleiben unverändert; ein akzeptierter
Rename verändert genau einen vorhandenen Directory-Sektor und bewahrt Inode,
Datenblöcke, Dateiinhalt, Bitmaps und freie Zähler bytegleich. Die Hostmatrix
deckt jeden Write-/Flush-Abbruch ab. Alle 38 Targeted-Checks, der
77-Sekunden-QEMU-Framebuffer-Paketbuild und der reale 94-Sekunden-EXT2-Lauf
mit zweitem Storage-Neustart und sauberen redundanten Journalheadern sind
bestanden. Verzeichnisse, Replace, Cross-Directory, Record-Wachstum und Unlink
regulärer Dateien bleiben getrennte Pakete.

`N3b-ext2-symlink-namespace` ist abgeschlossen. Der
begrenzte N3-Schnitt ergänzt den in N3a eingeführten nativen EXT2-Symlinks um
servicegebundenes `unlink` und gleichverzeichnisiges `rename`, ohne den alten
Ring-0-EXT2-Parser zu erweitern. Die erste Rename-Teilmenge bleibt auf einen
vorhandenen 512-Byte-Publikationssektor und ein nicht vorhandenes Ziel
beschränkt; reguläre Dateien, Verzeichnisse, Cross-Directory-Moves und
allgemeines Dateischreiben bleiben getrennte spätere N3-Pakete.

Die Implementierung ergänzt die append-only Storage-Operation 34 mit einem
festen 512-Byte-Namespace-Frame und den Operationen `unlink`/`rename`. Fast-
und Block-Symlinks nutzen das vorhandene 26-Sektor-Undo-Journal, genau einen
Directory-Publikationssektor und verifizierenden Readback. `DEL.PRG`,
`RM.PRG` und `RENAME.PRG` verwenden den generationgebundenen Client zuerst und
fallen nur bei `EOPNOTSUPP` auf FAT-Legacyoperationen zurück. Host-Preflights
für Client, Request-Pool und die vollständige EXT2-Unterbrechungsmatrix sowie
alle 47 eingefrorenen Targeted-Checks sind erfolgreich. Der erste Runtime-Lauf
deckte vor `BOOT_OK` eine Überschreitung der unveränderten
224-KiB-Rescue-Einzelgrenze durch `STORAGE.PRG` auf. Die O2-Codeerzeugung ohne
Funktions-Inlining und ihre explizite inkrementelle Buildabhängigkeit reduzieren
das Programm von 233472 auf 188416 Byte, ohne die Grenze anzuheben. Der finale
QEMU-Framebuffer-Paketbuild bestand in 16 Sekunden; der reale 77-Sekunden-
EXT2-Lauf bestätigte Unlink, Rename, Dienstneustart, Persistenz, unverändertes
Linkziel und zwei saubere Journalheader.

`R3.5a-desktop-icon-drag-layout` ist abgeschlossen. Ein reines, heapfreies
Layoutmodul parst und serialisiert bis zu 131 Einträge, berechnet eine
kollisionsfreie temporäre View und begrenzt die Rasterzellensuche auf 4096
Kandidaten. Der Desktop verwendet diese View gemeinsam für Rendering, Hit-Test
und Drop, unterscheidet `LAYOUT` von Datei-`MOVE` und publiziert eine neue
Anordnung erst nach Tempdatei, `fsync`, Close und atomarem Rename. Alle 80
eingefrorenen Targeted-Checks und der QEMU-Framebuffer-Paketlauf in 23 Sekunden
sind bestanden. Der reale USB-Mauslauf verschob und lud ein eingebautes Icon
und eine echte Verknüpfung dauerhaft neu, prüfte die temporäre Platzierung in
einem kleineren Arbeitsbereich und öffnete die verschobene Verknüpfung danach
ohne Compositor-Neustart wieder im Editor.

`N2g1-notepad-document-navigation-wrap` ist abgeschlossen. Der vertikale
Scrollbereich bildet das vollständige Piece-Table-Dokument ab und erreicht
auch bei logischen Zeilen über 256 Byte Anfang, beliebige Position, exaktes
Ende und lückenlose Rückwärtsfenster. `Format -> Zeilenumbruch` schaltet eine
gemeinsame visuelle-Zeilen-Abbildung für Rendering, Cursor, Pointer und
Viewport ein, deaktiviert horizontales Scrollen und verändert keine
Dokumentbytes. 32 gezielte Prüfungen, der QEMU-Framebuffer-Paketbuild in 14
Sekunden und der reale große Surface-Notepad-Lauf sind bestanden.

`N3a-vfs-symbolic-links` ist abgeschlossen. Die öffentliche Storage-Service-/
Client-ABI ergänzt append-only `symlink`, `readlink`, `lstat` und
`O_NOFOLLOW`; Auflösung, native EXT2-Fast-/Block-Symlinks sowie das feste
Undo-Journal liegen im restartbaren Ring-3-Storage-Dienst. Relative und
absolute Ziele, Dangling Links, Ketten und Zyklen sind durch feste Pfad-,
Walk-, Tiefen-, Sektor-, Retry-, Transaktions- und Zeitbudgets gebunden.
FAT12/32 lehnen die Erzeugung vor jeder Wirkung mit `EOPNOTSUPP` ab. Alle 44
Targeted-Checks, der QEMU-Framebuffer-Paketbuild in 87 Sekunden und der reale
EXT2-Lauf in 69 Sekunden einschließlich unterbrochener Mutation,
Dienstneustart, persistiertem Testimage und sauberem Journal sind bestanden.
Der alte Ring-0-VFS-/EXT2-Parser blieb unverändert.

`R3.3c-desktop-explorer-details-view` ist abgeschlossen. Der feste
Ansichtsschalter wechselt ohne VFS-Zugriff oder Snapshotwechsel zwischen der
Symbolmatrix und einer vertikal scrollbaren Detail-Liste. Deren feststehende
24-Pixel-Kopfzeile und 24-Pixel-Zeilen zeigen Name, Typ, dezimale Größe und
UTC-Änderungszeit. Auswahl, Doppelklick, Navigation, Verlauf und Scroll-Capture
bleiben gemeinsam; neue Fenster starten mit Symbolen. 106 Targeted-Checks, der
QEMU-Framebuffer-Paketbuild in 41 Sekunden und der reale Gastlauf mit beiden
Ansichten, Scrollen, Resize, Navigation sowie antwortender Ring-3-Shell sind
bestanden.

`R3.3b-desktop-explorer-scrollbar` ist abgeschlossen. Unterordner ersetzen
atomar den Inhalt desselben Fensters; Zurück, Vor, Aufwärts und Aktualisieren,
schreibgeschützte Adress- und Statuszeile sowie feste 16-Pfad-Verläufe bilden
die klassische Chrome. Der 128 Einträge große Snapshot besitzt eine gemeinsame
monotone Zehn-Sekunden-Grenze und einsekündige Requests. Pfeile, Seitenklick,
Thumb-Drag, Mausrad, Tastatur-Nachführung und Resize verwenden dieselbe
Zeilenrange und berechnen die Scrollbar stets aus der aktuellen Clientfläche.
104 Targeted-Checks, der QEMU-Framebuffer-Paketbuild in 17 Sekunden und der
reale Navigations-/Scrolllauf mit abschließender Ring-3-Shell-Antwort sind
bestanden. Suche, Baumansicht, editierbare Adresse und Dateimutationen bleiben
getrennte spätere Pakete.

`R3.5-desktop-shortcuts` ist mit der korrigierten Dateisystemsemantik
abgeschlossen. `/desktop` ist ein echtes Verzeichnis und sein atomarer,
128 Einträge großer Snapshot liefert alle dynamischen Desktop-Icons.
„Verknüpfung erstellen“ legt eine begrenzte `reist.shortcut/1`-`.LNK` neben
der Quelle an und verändert den Desktop nicht automatisch. Reguläre Dateien
einschließlich `.LNK` werden über einen synchronisierten, zurückgelesenen
Ring-3-MOVE erst am Ziel veröffentlicht und erst danach an der erneut
validierten Quelle gelöscht. 81 Targeted-Checks, der QEMU-Framebuffer-
Paketbuild in 46 Sekunden und der reale USB-Mauslauf mit Storage-Neustart,
Aktivierung im Editor sowie beidseitigem MOVE von Verknüpfung und normaler
Datei sind bestanden; `/desktop` war danach wieder leer.

`R3.2b-pixel-hinted-editor-fonts` ist abgeschlossen. Die vier Outline-Familien
werden jetzt in allen acht auswählbaren Höhen direkt als gehintete PSF2-Raster
erzeugt und aus 32 festen Compositor-Slots ohne Laufzeit-Downsampling gerendert.
Damit sind die zuvor zerfallenden oder blockartig verdickten Glyphen beseitigt.
118 Targeted-Checks, der QEMU-Framebuffer-Paketbuild in 18 Sekunden und der
reale große Notepad-Fontlauf ohne Fallback oder Compositor-Neustart sind
bestanden.

`R3.2a-notepad-font-selection` ist abgeschlossen. GNU Unifont, JetBrains Mono,
Source Code Pro, Iosevka und Fira Code sind aus gepinnten freien Quellen als
deterministische PSF2-Assets samt Lizenznachweisen installiert. Notepad wählt
Familie und 10 bis 28 Pixel große Schrift sitzungsbezogen nur für Dokument,
Cursor und Viewport; Systemschrift, Dokumentbytes, Piece Table, Modified-State
und Saveprotokoll bleiben unverändert. Der begrenzte Downscaler erhält dünne
Schriftstriche beim Verkleinern. 142 Unit-Checks plus der Surface-Quellvertrag,
der QEMU-Framebuffer-Paketbuild in 9 Sekunden und der reale große Notepad-
Fontlauf über alle fünf Familien und beide Randgrößen sind bestanden.

`N2g-desktop-trash-metadata-authority` ist abgeschlossen. Restore und
Empty-Validierung lesen `.trashinfo` genau einmal ueber ein
generationgebundenes Ring-3-VFS-Objekt mit READ-/STAT-Rechten, fester
640-Byte-Kapazitaet und absoluter Fuenf-Sekunden-Frist. Typ, exakte Groesse,
Inhalt, EOF und Close sind vor Parser und Mutation gebunden. 107 Targeted-
Checks, der QEMU-Framebuffer-Paketbuild in 52 Sekunden und der fokussierte
reale Move-/Restore-Lauf in 27 Sekunden bestanden. Papierkorb-Mutationen,
Formatversion 2, oeffentliche ABI, Kernelparser und Storage-Service blieben
unveraendert.

`N2f-editor-load-authority` ist abgeschlossen. Der read-only Ladepfad von
`EDIT.PRG` verwendet ein generationgebundenes Ring-3-VFS-Objekt mit READ-/
STAT-Rechten, fester 51200-Byte-Gesamtgrenze und absoluter 60-Sekunden-Frist.
Inhalt wird erst nach exaktem EOF und erfolgreichem Close in den Editor
übernommen. 68 Targeted-Checks, der QEMU-Framebuffer-Paketbuild und der
fokussierte reale QEMU-Editorlauf mit Shell-Erstellung, `EDIT_VFS_LOAD_OK`,
`Ctrl-X`, CAT-Readback und Cleanup bestanden. Der atomare Tempfile-/Fsync-/
Rename-Savepfad sowie die Editorinteraktion blieben unverändert.

`N2g-notepad-piece-table` ist abgeschlossen. Der grafische Notepad hält große
Dokumente in einer festen Piece Table, materialisiert nur ein begrenztes
Editorfenster und streamt beim atomischen Speichern höchstens 256 Pieces.
Die nachgezogene Korrektur N2g1 behebt nun die zuvor nur fensterlokale
Scrollbar-Range und ergänzt byte-neutralen virtuellen Zeilenumbruch.

`N2e-audio-wave-read-authority` ist abgeschlossen. Der gemeinsame WAV-
Preview-Loader von `WAVPLAY.PRG` und `SOUNDPLAYER.PRG` verwendet genau ein
generationgebundenes Ring-3-VFS-Objekt mit READ-/STAT-Rechten und absoluter
60-Sekunden-Ladefrist. `libreistaudio.a` enthält den festen Objektclient und
die kanonische Pfadauflösung als vollständige SDK-Linkabhängigkeit. 52
Targeted-Checks, der QEMU-Framebuffer-Paketbuild und der reale Sound-Player-
Surface-Lauf mit gültiger Stereo-S16-Aufzeichnung sind bestanden. Audio-
Service, HDA, DMA, Surface, Wiedergabe, Assets, öffentliche ABI und
Dateisystemmutationen blieben unverändert.

`N2d-chkdsk-readonly-authority` ist abgeschlossen. Der generische read-only
Pfadmodus von `CHKDSK.PRG` verwendet Ring-3-Stat-, Readdir-at- und
Objektclients mit einer gemeinsamen absoluten 60-Sekunden-Scanfrist. Das
Maintenance-Profil besitzt keine direkten Legacy-Open-/Read-/Close-/Stat-/
Readdir- oder Storage-Bulk-Syscalls mehr und darf nur den bestehenden
read-only VFS-Shadow-Umschlag sowie die unveränderten FAT12-Operationen 11 bis
30 senden. 137 Targeted-Checks, der QEMU-Framebuffer-Paketbuild, der reale
`/htdocs`-Scan und die reale FAT12-BPB-/Spiegelprüfung sind bestanden. Der
unabhängige alte FDD-Remount-Lauf reproduziert weiterhin `ADMIN MOUNT_FAILED`
und wurde weder verändert noch als N2d-Nachweis gewertet.

`N2c-document-loader-authority` ist abgeschlossen.
BASIC LOAD verwendet vollständig die generationgebundene Ring-3-
Objektautorität; die redundante Notepad-Dateidialog-Lesevorprüfung ist
entfernt. 73 Targeted-Checks, Framebuffer-Paketbuild, interaktiver BASIC-
Lauf samt Cleanup sowie der Notepad-Desktoplauf sind bestanden.

`N2b-copy-source-authority` ist abgeschlossen. Die
Quellseite von `COPY.PRG` verwendet ein generationgebundenes Ring-3-Storage-
Objekt mit READ-/STAT-Rechten; Zielerzeugung und -mutation bleiben
unverändert. 58 Targeted-Checks, VGA-Paketbuild und der reale
Copy/CAT/Delete/Stat-Shelllauf sind bestanden. N2 bleibt für weitere
Legacy-Lesekonsumenten offen.

`N2-control-panel-config-authority` ist abgeschlossen.
Der gemeinsame read-only Konfigurationsladepfad aller vier Control-Panel-
Applets verwendet generationgebundene Ring-3-Storage-Objekte mit READ-/STAT-
Rechten. Die `CONFIG.PRG`-Mutations-, Autorisierungs- und Persistenzgrenze
bleibt unverändert. 58 Targeted-Checks, Paketbuild, Control-Panel-Runtime und
isolierter Storage-Service-Restart sind bestanden.

`N1-notepad-readonly-vfs` ist abgeschlossen. Genau der
read-only Dokument-Ladepfad des Notepads verwendet nun statt Legacy-VFS-
Syscalls ein generationgebundenes Ring-3-Storage-Objekt mit ausschließlich
READ-/STAT-Rechten. Speichern, `fsync`, Rename, Journal und andere Mutationen
bleiben unverändert. 80 Targeted-Checks, der QEMU-Framebuffer-Paketbuild, der
reale Notepad-Desktop-Lauf und der isolierte Storage-Service-Restartlauf sind
bestanden. Als nächstes ist N2 abzugrenzen; `active_id` bleibt bis dahin leer.

R3.4h behält im Menü-Controller die exakten alten/neuen Itemzeilen, bildet
sie auf dem aktiven VMware-RECT_COPY-Pfad ohne RECT_FILL jedoch als sichtbaren
vier Pixel breiten linken Auswahlstreifen ab. Damit entfällt der langsame
vollständige Framebuffer-BAR-Write pro Hoverwechsel. xHCI fasst Requeues je
Endpoint zu einem Doorbell pro begrenztem Event-Batch zusammen; der
Compositor verarbeitet weiterhin höchstens vier Reports pro Turn und hält den
1-ms-Handoff ein. Alle neun eingefrorenen Quellprüfungen bestanden; sechs
optionale Host-C-Harnesses wurden mangels Compiler übersprungen. VMware/VGA
und QEMU/Framebuffer bauten erfolgreich. Der finale Vier-vCPU-VMware-Hoverlauf
meldete sechs verschiedene Hot-Zustände ohne Vollbild-Probe-Frame, maximal
3 ms je Hot-Frame, 18 ms Pointer-Abstand, 6 ms Eingabe-zu-Pointer-Latenz und
6 ms Cursor-Aufrufzeit. QEMU bestand denselben Nachweis mit 0/12/2/1 ms
(Hot-Frame/Pointer-Abstand/Latenz/Aufruf). Der reale VMware-Maus- und
SVGA2D-Lifecycle-Lauf bestanden jeweils mit zehn Sekunden stabilem Nachlauf.
Das ist eine reproduzierbare Referenzmessung, keine universelle Windows-95-
oder p99-Behauptung.

R3.4g ersetzt die zwischen weit entfernten Cursorpositionen potenziell
bildschirmgroße Softwarecursor-Bounding-Box durch höchstens zwei exakte alte
und neue Rechtecke in einem Present-Batch. Der Maus-Syscall leert vorhandene
HID-Ereignisse vor je höchstens einem xHCI-/OHCI-Poll. Der Compositor verwirft
nur vollständig validierte alte SVGA2D-Antworten innerhalb der festen Queue
und verwendet absolute 100-ms- beziehungsweise 500-ms-Fristen. Der periodische
SMP-Scheduler wartet im IRQ nicht mehr auf den globalen Task-Lock: Nach zwei
endlichen Try-Locks kehrt er zurück und wiederholt die Entscheidung im nächsten
festen Quantum. Ohne gültige Owner-Kante entfällt außerdem die quadratische
Priority-Inheritance-Passage.

Die 141 fokussierten Prüfungen liefen ohne Fehler: 135 bestanden, sechs
optionale Host-C-Harnesses wurden ohne verfügbaren GCC übersprungen, während
die Zielpakete den Produktionscode kompilierten. VMware/VGA und
QEMU/Framebuffer bestanden in 46 und 43 Sekunden. Die finalen QEMU-Benchmarks
bestanden mit 1 vCPU in 33,8 und
mit 4 vCPUs in 38,3 Sekunden. Auf dem betroffenen AMD-VMware-Host stieg das
Vier-Worker-/Single-Verhältnis von 0,75x auf 0,92x (645,27 zu 593,88 MOp/s
gesamt); die Worker bleiben vertragsgemäß BSP-gebunden, daher ist dies ein
Kontentions- und kein AP-Skalierungsnachweis. Der beschleunigte echte
VMware-Mauslauf bestand in 28 Sekunden. Der 29-sekündige SVGA2D-Render-Probe
bestand Aktivierung, RECT_COPY, Deaktivierung, Metriken, Exit, erneute
Ring-3-Shell-Antwort und zehn Sekunden Nachlauf mit acht beschleunigten Frames
und ohne Fallback, Reconnect oder Transaktionsfehler. Ein p99-Latenznachweis,
physische AMD-GPU-Beschleunigung und eine Windows-95-Paritätsbehauptung bleiben
ausdrücklich offen.

`R3.4f-desktop-smp-input-cadence` ist abgeschlossen. Der Compositor behaelt
den begrenzten Post-Input-Yield nur auf einem vCPU; auf SMP kann der Surface-
Client parallel laufen und der globale Scheduler-Lock wird nicht mehr nach
jeder zugestellten Eingabe freiwillig beansprucht. Der 4-vCPU-Referenzlauf
bestand mit 14 ms Vollbild, maximal 88 ms Drag und 87 ms Resize sowie einem
echten GUIDEMO-Control-Klick; Fallback-Frames und Probe-Fehler blieben null.

`R3.4e-desktop-interaction-cadence` ist abgeschlossen. Es reduziert den
inaktiven Desktop-Poll von fuenf auf eine Millisekunde.
Nach tatsaechlich zugestelltem Surface-Input erhaelt der Client einen
begrenzten Scheduler-Turn und der Compositor verarbeitet einen festen
Broker-Drain vor derselben Frame-Entscheidung. Effizient angrenzende
Dirty-Rechtecke werden kanonisiert; Eckkontakte, feste Kapazitaeten,
Edge-Reihenfolge und Vollbild-Fallback bleiben unveraendert. Der QEMU-Lauf
mass 14 ms fuer den Vollbildaufbau, hoechstens 79 ms fuer Drag- und 88 ms fuer
Resize-Frames bei maximal einer Schadensregion und ohne Fallback-Frame. Das
belegt die Referenzgrenzen, aber keine Windows-95- oder Zielhardware-Paritaet.

`R4.1c-curl-large-transfer` ist abgeschlossen. Große Antworten behalten bei
laufendem Fortschritt die begrenzte HTTPS-Verbindung; TCP kündigt nach dem
Lesen das wieder geöffnete feste Empfangsfenster an. Leerlauf bleibt auf 30
Sekunden, Gesamtzeit auf fünf Minuten und Antwortgröße auf 16 MiB begrenzt.

`R4.1a-curl-https` ist abgeschlossen. `libreisttls.a` kapselt das
SHA-256-gepinnte Mbed TLS 4.1.1 als begrenzte Ring-3-Clientbibliothek mit
TLS 1.2/1.3, X.509-Ketten-, SAN-, RTC- und Recordpruefung. `curl.prg` nutzt
sie fuer authentisiertes HTTPS; der reale QEMU-Lauf bestand den lokalen
CA-/IP-SAN-Handshake und beobachtete `REIST_CURL_HTTPS_RUNTIME_OK`. Eine
einzige 4-MiB-Backing-Arena haelt alle internen TLS-Allokationen innerhalb
der 16 Prozess-Heapobjekte und der unveraenderten 8-MiB-MYPR-Grenze.

`R4.1b-curl-public-https` ist abgeschlossen. Der lokale Nachweis war fuer den
Produktionsbetrieb nicht hinreichend: Der
normale QEMU-Start exponierte kein RDRAND, der Runtime-Probe-Build verwendete
das gemeinsame Buildverzeichnis, und ein oeffentlicher DNS-Ausfall war wegen
der zusammengefassten curl-Fehlermeldung nicht lokalisierbar. R4.1b setzt fuer
alle dokumentierten QEMU-Starts explizit `qemu32,+rdrand`, behaelt den
fail-closed CPUID-/RDRAND-Vertrag ohne schwachen Fallback, isoliert Test-CA und
Testabbild unter `build/curl-https-runtime`, ergaenzt einen deadline- und
groessenbegrenzten DNS-over-TCP-Fallback und trennt DNS-, TCP-, TLS-, Ausgabe-
und Antwortfehler. Der Produktionsnachweis startet exakt
`curl https://google.com` aus `/bin/shell.prg`; ein begrenzter Testpeer liefert
die hostaufgeloeste oeffentliche A-Adresse an den Gast-DNS-Parser, danach laufen
Gast-TCP und TLS ueber QEMUs User-Netzwerk zum Google-Endpunkt, ohne
Host-TLS-Terminierung oder Test-Vertrauensanker.
Große oeffentliche TCP-Segmente werden im validierenden Ring-3-Dienst
sequenztreu in die unveraenderte 512-Byte-Ingress-ABI zerlegt. Die feste
NIST-ECP-Reduktion und begrenzte Viererfenster-/Fixed-Point-Konfiguration
bringen moderne Google-ECDSA-Handshakes innerhalb der Peerfrist zum Abschluss.
Der fokussierte Test, das QEMU-VGA-Paket und der echte oeffentliche Runtime-
Nachweis sind erfolgreich.

R7.1k hat den ersten Teilbefund geschlossen: Der feste, unter einer
verschachtelten Ebene der rekursiven FAT32-Operationsmutex gehaltene
Verzeichnisscan reduzierte seinen GCC-Frame von 3168 auf 112 Byte und bestand
36 fokussierte Tests. Der anschliessende vollstaendige Analysecompile zeigte
jedoch einen tieferen, zuvor verdeckten Rename-Pfad mit 8384 Byte gegen das
unveraenderte 7168-Byte-Syscallbudget. Weil Rename, LFN-Publikation,
FAT-Sektorverifikation und Journal-Recovery ausserhalb des eingefrorenen
R7.1k-Scopes lagen, wurde das Paket ohne Produktionscommit gestoppt.

R7.1l nahm diese FAT32-Kette auf und schloss Rename-, LFN-, FAT-, ATA-Journal-,
Unicode-, Panik- und Validatorbesitz. Der erste vollstaendige Analysebuild
zeigte danach jedoch einen unabhaengigen FAT12-Create-Fehlerpfad: Vor dem
ersten `kassert_fail` waren bereits 8116 Byte, mit dessen Frame 8164 Byte gegen
das unveraenderte 7168-Byte-Syscallbudget belegt. Die gleichzeitig lebenden
FAT12-Verzeichnis-, Allokations-, Ziel-, Journalheader- und Readbacksektoren
lagen ausserhalb des R7.1l-Scopes; das Paket wurde deshalb ohne Kandidatenbuild
oder Laufzeitclaim beendet.

R7.1m uebernimmt die geprueften R7.1l-Arbeiten und bindet den vollstaendigen
FAT12-Metadatenpfad ein. Core-I/O und alle VFS-Helfer besitzen getrennte feste
Sektor-/Pfadslots unter einer deadline-begrenzten rekursiven FAT12-Mutex;
gleichnamige Rekursion wird vor Payloadzugriff abgewiesen und jeder Ausgang
loescht seinen Slot. Das FAT12-Journal besitzt genau vier nur zur Laufzeit
existierende Sektoren im einzelnen Journalobjekt und einen atomaren
Einmalbesitzer; Load und Recovery verwenden interne, nicht erneut sperrende
Varianten. Eine Medienquarantaene publiziert Schutzobjekt, Read-only-Latch und
Fences weiterhin synchron, stellt aber nur ihr Diagnosebit atomar bereit. Erst
der Supervisor-Poll formatiert hoechstens einen Marker, nachdem die
FDD-Transaktionsmutex frei ist. Der aktualisierte Entwicklungsgraph umfasst
104 Objekte und liegt bei 6820/7168 Byte; der zuvor ueberlaufende FAT12-Create-
Teilpfad sank auf 3040 Byte. Stackgroesse, Guardpage, Journal-v2-Medienbytes,
VFAT-/Unicode-Semantik, Barrieren und ABI bleiben unveraendert. Die finale
Abnahme bestand 136 fokussierte Pruefungen und den sauberen 104-Objekt-
Stackbuild mit 1967 Stackdatensaetzen, 3721 Graphknoten und 12134 Kanten. Der
VMware-VGA-Paketbuild bestand in 49 Sekunden. Der reale Vier-vCPU-Renamelauf
bestand Inhaltspruefung, Cleanup, Shell-Rueckkehr und zehn Sekunden stabilen
Nachlauf in 74 Sekunden. Der bytegepruefte Benchmark bestand dieselben
Nachbedingungen in 37 Sekunden und erreichte 314,88 KiB/s Schreiben sowie
450,70 KiB/s Lesen. Danach lief keine VMware-VM mehr.

Nach dem erfolgreichen R7.1i-Benchmark trat erst nach der Shell-Rueckkehr ein
Kernel-Panic auf; der beobachtete Neustart ist daher kein Absturz des Desktop-
Prozesses. Build-ID und die schon im vorherigen Rekursionsbefund enthaltene
Lockadresse ordnen den Fehler `task_table_lock` im Prozess-Endpfad zu. Die
globale Lock-Reihenfolge bleibt konsistent. Der reale Fehler ist die Kopplung
zweier CPU-geschwindigkeitsabhaengiger Retrygrenzen: Die Scheduler-Policy las
die PIT-Sequenz unter dem Task-Lock, waehrend VMware eine beteiligte vCPU kurz
vom Host anhalten kann; andere vCPUs erschoepften waehrenddessen eine Million
CAS-Schleifen und loesten einen falschen Lock-Timeout aus.

R7.1j verschiebt die monotone Zeitaufnahme vor den Task-Lock, ersetzt den
normalen Erwerb durch test-before-CAS mit einer kalibrierten festen 250-ms-
Frist und behaelt fuer Boot und defekte Zeitbasis endliche harte Grenzen. Ein
Timeout nennt nun Lockadresse, Besitzer-CPU und wartenden Aufrufer. Der VMware-
Runner beendet Benchmark oder Desktop nicht mehr sofort nach dem
Erfolgsmarker, sondern beobachtet weitere zehn Sekunden auf Panic,
Degradation, Storage-Fence und einen zweiten `BOOT_OK`-Marker.

Der erste so verlaengerte Desktop-Lauf deckte zusaetzlich einen unabhaengigen
Double Fault direkt nach `DESKTOP_SPLASH_READY` auf. Eine identische
Vier-vCPU-QEMU-Konfiguration reproduzierte ihn und lieferte den exakten
Interruptzustand: Syscall 25 (`SYS_READDIR_BATCH`) lief mit EIP in der FAT32-
Unicode-Normalisierung; CR2 lag vier Byte unter ESP in der unteren
Kernelstack-Guardpage. Die Compiler-Stackdaten zeigen fuer den Syscall allein
2532 Byte und fuer den tiefsten FAT32-Pfad mehr als den gesamten 8-KiB-Slot.
R7.1j verschiebt Pfad, vier VFS-Eintraege und vier ABI-Eintraege in einen
festen Arbeitsbereich je Task-Slot. Die nie-null Task-Generation und ein
Belegtbit verhindern Aliasing bei blockierendem I/O, Rekursion und
Slot-Wiederverwendung; ein vergroesserter Stack oder eine abgeschaltete
Guardpage ist nicht Teil des Fixes.

Die finale Abnahme bestand 87 eingefrorene Quell-/Host-Pruefungen bei einem
optionalen Compiler-Skip und den VMware-VGA-Paketbuild in 43 Sekunden. Der
reale Benchmark erreichte 284,12 KiB/s Schreiben und 439,10 KiB/s Lesen mit
vollstaendigem Datenvergleich, Cleanup, Shell-Rueckkehr und zehn Sekunden
Nachlauf. Der reale Desktop passierte den frueheren Fehlerpunkt ueber
`DESKTOP_EXPLORER_OK`, `COMPOSITOR_READY`, `DESKTOP_OK` und den echten
virtuellen xHCI-Marker `DESKTOP_MOUSE_OK`; auch sein zehnsekündiger Nachlauf
blieb ohne Panic, Fatal-Marker, wiederholten BIOS-Lader oder `BOOT_OK`,
Degradation oder Storage-Fence.

`R7.1i-fat32-ordered-append-io` ist abgeschlossen.

Die saubere Gegenanalyse ordnet die Restlatenz nicht dem FAT32-Format zu,
sondern der Kommandovielzahl: Der 256-KiB-Leselauf liest fuer fast jeden der
511 FAT-Nachfolger denselben FAT-Sektor erneut. Der Schreiblauf zerlegt die
Nutzdaten in 64 einzelne 4096-Byte-VFS-Mutationen und fuehrt fuer jeden
Nutzdatensektor Undo-, Ziel- und Readback-I/O aus. Das aktive Paket fuehrt
deshalb einen festen, transaktionsbewussten FAT-Sektorcache und einen
geordneten Append-Pfad ein. Vollstaendige Sektoren neuer, noch nicht
erreichbarer Cluster werden zuerst per AHCI geschrieben, geflusht und ueber
den gesamten Lauf rueckgelesen; erst danach duerfen FAT-Verkettung,
Verzeichniseintrag und FSInfo ueber das bestehende Journal sichtbar werden.
Alle nicht beweisbaren Faelle bleiben beim bisherigen 4096-Byte-Rueckfall.
Journal-v2, zwanzig Slots, exakt vier Barrieren und der akzeptierte gepollte
AHCI-Abschluss bleiben unveraendert. Der FAT-Kettenleser verwendet jetzt
denselben Cache statt einer zweiten direkten ATA-Leseroutine; der Hostnachweis
begrenzt einen sequentiellen 24-KiB-Lauf dadurch auf hoechstens zwei physische
FAT-Sektorreads. Der eingefrorene VMware-Grenzwert von 95 KiB/s Schreiben und
415 KiB/s Lesen wurde im realen Vier-vCPU-Lauf mit 314,49 KiB/s und
640,00 KiB/s deutlich ueberschritten. Vollstaendige 256-KiB-Bytepruefung,
Cleanup und Ring-3-Shell-Rueckkehr bestanden in 17 Sekunden. Der VMware-VGA-
Paketbau bestand in 36 Sekunden; das signierte Bootartefakt hat SHA-256
`16f53675d45203df7a0e4976ddd8711b0a799920df8e9aa8bb24bb0132e6924f`.
R7.1i ist damit angenommen und abgeschlossen.

`R7.1h-ahci-interrupt-completion` ist zuvor an seiner eingefrorenen
Laufzeitgrenze blockiert und als nicht angenommen beendet worden.

Der Kandidat bestand alle 32 gezielten Prüfungen und den VMware-Paketbau. Zwei
reale Vier-vCPU-Läufe bewiesen AHCI-Interruptzustellung, vollständige
256-KiB-Byteprüfung, Cleanup und Ring-3-Shell-Rückkehr ohne Panic oder
Degradation. Der erste Lauf erreichte dennoch nur 11,31 KiB/s Schreiben und
52,29 KiB/s Lesen. Die einzige zulässige fokussierte Korrektur beschränkte
Wakeups auf terminale D2H-Completion, verbesserte das Ergebnis aber nur auf
11,57/53,51 KiB/s. Die geforderten 95/415 KiB/s sind damit klar verfehlt.
Gemäß Stop-Bedingung gibt es keinen Implementierungscommit; der Kandidat
bleibt nur als Diagnoseevidenz dokumentiert. R7.1i ersetzt ihn nicht durch
einen weiteren Wait-Versuch, sondern beseitigt die nachgewiesene
Kommandoverstärkung.

Der VMware-HDD-Gegenlauf zeigte trotz flacher persistenter SATA-VMDK nur
18,29 KiB/s Schreiben und 77,48 KiB/s Lesen. CPU und RAM waren gleichzeitig
unauffällig; der Gastpfad war damit isoliert. Der FAT32-VFS-Adapter löste vor
jedem 4096-Byte-Read und -Write den bereits geöffneten Directory-Eintrag erneut
auf, obwohl Handle und Mount bereits dieselbe monotone Datengeneration
trugen. R7.1g verwendet diesen vorhandenen Vertrag nun vollständig: Nur ein
intern konsistentes Dateihandle mit exakter aktueller Generation darf Name,
Cluster und Größe wiederverwenden. Fremde VFS- oder Legacy-Mutationen erhöhen
die gemeinsame Generation, verwerfen alle sequentiellen Hinweise und erzwingen
vor dem nächsten I/O genau eine begrenzte Namensauflösung. Bei
Generationserschöpfung bleibt der Cache dauerhaft aus. Journal-v2, vier
Persistenzbarrieren, AHCI-Verifikation, 4096-Byte-Staging und öffentliche ABIs
bleiben unverändert. Der Hostnachweis beobachtet bei sechs aufeinanderfolgenden
4096-Byte-Reads null statt sechs Root-Verzeichnisreads und nach einer
Legacy-Mutation genau einen Revalidierungsread. Alle 28 eingefrorenen Tests
bestanden. Der finale VMware-Paketbuild bestand in 5 Sekunden; der echte
Vier-vCPU-Lauf erreichte nach bytegleichem 256-KiB-Readback, Cleanup und
Shell-Rückkehr in 35 Sekunden 18,94 KiB/s Schreiben und 82,36 KiB/s Lesen.
Gegenüber 18,29/77,48 ist der verbliebene VFS-Overhead messbar reduziert, die
Schreibrate aber weiterhin niedrig. Sie bleibt als Restkosten der vier
Durabilitätsphasen und des gepollten Controllerpfads sichtbar; R7.1g lockert
weder Barrieren noch Warte- oder Host-Sicherheitskonfiguration. Das Paket ist
abgeschlossen; der nachfolgende R7.1h-Versuch konnte die Controllerlatenz
nicht ausreichend senken.

Der reale R7.1e-Gegenlauf ist korrekt, aber mit 7,99 KiB/s Schreiben und
503,93 KiB/s Lesen weiterhin unbrauchbar langsam. Ursache ist der nur dem
Namen nach verzögerte AHCI-Journaltransport: Jeder 512-Byte-Sektor fuehrt
weiterhin `WRITE DMA EXT -> FLUSH CACHE EXT -> READ DMA EXT` aus, bevor das
Journal seine vier eigentlichen Phasenbarrieren setzt. R7.1f implementiert den
vollstaendigen sechsstufigen Pfad: Undo-Batch, Flush/Readback, ACTIVE,
Flush/Readback, zusammenhaengende Ziel-Batches, Flush/Readback und zuletzt
CLEAN mit Flush/Readback. Ein fester kernel-eigener 20-Sektor-Puffer nimmt die
erwarteten Daten auf; kurze DMA-Transfers, nicht passende Readbacks oder ein
unklarer Abschluss vergiften die Phase bis zur kontrollierten Recovery. Genau
vier geordnete Flushes, das Journal-v2-Format und das fail-closed Write-Fencing
bleiben erhalten. Derselbe feste Mehrsektor-DMA-Mechanismus buendelt validierte
sequentielle FAT32-Leseabschnitte auch ueber physisch aufeinanderfolgende
Einsektorcluster hinweg. Bei Teilsektoren, offenen Journaldaten oder einer
fragmentierten Kette bleibt der validierte Rueckfallpfad erhalten. Alle 39
eingefrorenen Quell- und Hosttests bestanden. Eine zusaetzliche Assertion im
AHCI-Port- und Batch-Lock hatte den fruehen Auto-Mount auf echter Hardware und
VMware mit `scheduler_can_sleep()` gestoppt. Sie war mit dem vorhandenen
Kernel-Mutex-Vertrag unvereinbar: Vor Schedulerbereitschaft ist eine
unkontendierte Uebernahme erlaubt, Kontention endet begrenzt mit
`WOULD_BLOCK`. Nach der fokussierten Korrektur bestanden alle sechs
AHCI-Regressionstests erneut. Der finale `real_hw/vga`-Paketbuild bestand in
40 Sekunden ohne VM-Start und erzeugte `build/reist-os-real-hw.img` mit
SHA-256
`CFBEBAA94A85489CFC82E4FF1C75D7490FABEFD7C90BC0CC0254C2155F3814AF`.
Ein begrenzter QEMU-Lauf mit einer vCPU und ICH9-AHCI mountete `hdd0p2`,
erreichte `BOOT_OK` und die Ring-3-Shell. Der anschliessende 256-KiB-Benchmark
erreichte nach 10,485 Sekunden `phase=complete`, pruefte jedes Byte, entfernte
die Testdatei und meldete 42,75 KiB/s Schreiben sowie 1347,36 KiB/s Lesen. Der
abschliessende manuelle ASUS-/Samsung-SSD-Gegenlauf auf echter Hardware
erreichte ungefaehr 30 KiB/s Schreiben und 670 KiB/s Lesen. Gegenueber dem
R7.1e-Ausgangswert entspricht das etwa dem 3,75-Fachen beim Schreiben und dem
1,33-Fachen beim Lesen. Da die Benchmark-Ergebniszeilen erst nach
bytegeprueftem Readback und Cleanup erscheinen, ist die physische Abnahme
bestanden. R7.1f ist abgeschlossen; kein weiteres Paket ist eingereiht.

Der manuelle Gegenlauf auf dem ASUS-Board mit Samsung-SSD und dem AHCI-Volume
`hdd0p2` ist erfolgreich abgeschlossen. Der Benchmark schrieb 256 KiB,
beendete `fsync`, las alle 256 KiB bytegleich zurueck, entfernte die Testdatei
und erreichte `phase=complete`. Die Tabelle meldete 7,99 KiB/s sequenzielles
Schreiben und 503,93 KiB/s Lesen, jeweils `OK`. Die Bootdiagnose belegte
weiterhin `ATA: detected 0 PIO drive(s)`; der reale Fehler lag daher in der
gemeinsamen Uhr-/90-Sekunden-Auswertung und den 512 einzelnen
Journaltransaktionen, nicht in einem AHCI-Flushfehler.

Die abgeschlossene Umsetzung kopiert pro Dateischreibschritt hoechstens eine
4096-Byte-Seite in einen statischen, supervisor-only Puffer unter einem
10-Sekunden-Mutex und uebergibt diesen als genau eine VFS-Mutation. Der
256-KiB-Benchmark benoetigt dadurch 64 statt 512 Journaltransaktionen. Der
Hostnachweis schreibt eine ganze Seite innerhalb der festen 20 Undo-Slots,
behaelt exakt vier Persistenzbarrieren und liest jedes Byte identisch zurueck.
Ein separater Grenztest weist den 21. eindeutigen Sektor vor der
Zielpublikation ab und belegt den unveraenderten Abbruchzustand. AHCI behaelt
fuer jeden Sektor WRITE, FLUSH und vollstaendigen Readback-Vergleich. Die
Benchmark-Auswertung hat getrennt eine 300-Sekunden-Grenze und meldet
Uhrfehler, Grenzueberschreitung sowie Nulldauer getrennt; kein I/O-Wait wurde
verlaengert.

Der PIO-Rueckfallpfad ist ebenfalls korrekt abgeschlossen: IDENTIFY-Wort 83
waehlt `0xEA` nur bei Bit 13 und `0xE7` nur bei Bit 12; vier
Alternate-Status-Reads sowie feste Zeit-/Pollgrenzen liefern bei Fehlern
`ATA_FLUSH_FAILED`, ohne einen unklaren Befehl zu wiederholen. Alle 54
eingefrorenen gezielten Pruefungen bestanden. Der abschliessende
`real_hw/vga`-Paketbuild bestand in 17 Sekunden ohne VM-Start und erzeugte das
64-MiB-Image mit SHA-256
`C0074B3FD710C0C4AD346313DCAF3A961271691EE0443704F1D82B93B6145487`.
Es ist kein weiteres Paket eingereiht.

`R7.1d-storage-readdir-continuation` ist abgeschlossen.

Die indexierte Operation 7 bleibt bytegleich. Die unabhaengigen FAT- und
EXT2-Parser besitzen nun einen festen Fortsetzungszustand, und der
Storage-Dienst haelt hoechstens acht ersetzbare, exakt an
Client-/Servicegeneration und Pfad gebundene Hinweise. Vor jeder
Wiederaufnahme werden Medium, Verzeichnisidentitaet und FAT-Kette
beziehungsweise EXT2-Inode-/Blockgrenzen erneut validiert; jede Abweichung
verwendet den vorhandenen begrenzten Kaltlauf. `LS.PRG` bleibt ohne
Kernel-VFS-Fallback und besitzt eine absolute Fuenf-Sekunden-Frist sowie eine
Grenze von 128 Eintraegen.

Alle 46 gezielten Pruefungen bestanden. Der FAT-Hostlauf listet 128 Eintraege
plus EOF mit hoechstens 129 Verzeichnis-Datensektorreads; EXT2 listet 62
Eintraege plus EOF mit hoechstens 126 Verzeichnissektorreads insgesamt.
Beschaedigte Cursor, eine geaenderte FAT-Kette und ein geaenderter
EXT2-Inode-Blockplan erzwingen den Kaltlauf. `STORAGE.PRG` bleibt mit 196.608
Bytes unter 224 KiB. Der QEMU-VGA-Paketbuild bestand in 70 Sekunden, ohne eine
VM zu starten.

R7.1c ist abgeschlossen. `BENCHMARK.PRG` meldet jede Phase und feste
64-KiB-Fortschritte, ohne Konsolenausgabe in den Durchsatz einzurechnen. Die
FAT32-Allokation verwendet den validierten FSInfo-Next-Free-Hinweis und offene
VFS-Handles lesen sequentiell ueber einen generationsgeprueften Cluster-Cursor.
Das unveraenderte 20-Sektor-Undo-Journal sammelt alte und neue Sektoren in
fester Kapazitaet und persistiert sie in der Reihenfolge Undo, ACTIVE, Ziele,
CLEAN mit genau vier Barrieren. Syscall-Bounce, Transaktionsgrenzen,
On-Disk-Format und oeffentliche ABI bleiben unveraendert.

Alle 49 gezielten Tests sowie der QEMU-VGA-Paketbuild bestanden. Der finale
Ein-vCPU-QEMU-Lauf schrieb 256 KiB mit 3,17 KiB/s, las sie mit 77,24 KiB/s
bytegleich zurueck, entfernte die private Testdatei und erreichte nach
96,265 Sekunden erneut die Ring-3-Shell. Derselbe Benchmark wurde vom Benutzer
auch unter VMware erfolgreich ausgefuehrt. Die 120-Sekunden-Gesamtfrist bleibt
hart; der Runner beendet ausschliesslich seine eigene VM und arbeitet auf einer
danach geloeschten Rohkopie.

R7.1b behebt den beim Datentraegerteil von `BENCHMARK.PRG` sichtbaren
quadratischen FAT32-Schreibpfad. Ein offenes VFS-Handle behaelt jetzt einen
validierten sequentiellen Cluster-Cursor und den bereits geprueften physischen
Kettentail. Jede Sektormutation erhoeht eine volumenbezogene 64-Bit-Generation;
Legacy-Writer, ein zweites Handle, Truncate, Random-Offset, I/O-Mehrdeutigkeit
oder Generationsueberlauf entwerten beide Hinweise und verwenden wieder den
vollstaendigen begrenzten Kettenlauf. Der Syscall kopiert weiterhin hoechstens
512 Bytes in einen stabilen Kernelpuffer, und jede VFS-Operation bleibt genau
eine Transaktion des unveraenderten 20-Sektor-Undo-Journals. Insbesondere gibt
es keinen Zwischencommit beim Freigeben einer Clusterkette.

Der Hostnachweis verwendet ein FAT32-Volume mit gueltigem FSInfo und fuehrt 48
getrennte 512-Byte-Appends aus. Alle Bytes stimmen; 1.794 FAT-Sektorreads
bleiben unter der festen linearen Grenze von 1.920. Derselbe Lauf prueft die
Cacheentwertung nach einem Legacy-Append sowie nach Truncate/Rewrite ueber ein
zweites VFS-Handle. 12 Dateisystem-, 20 Syscall-Grenz-, 9 Undo-Journal-, 8
Power-Cut- und 5 Benchmarktests bestanden. Der abschliessende QEMU-VGA-
Paketbuild bestand in 38 Sekunden ohne VM-Start.

R8.2e ist als isolierter Architekturbaustein abgeschlossen. Das Paket hebt
die feste physische Verwaltungs- und 4-KiB-Direct-Map-Grenze von 64 auf genau
128 MiB an und beweist in einem Ein-vCPU-/128-MiB-QEMU-Lauf einen
Multiboot-autorisierten Frame oberhalb 64 MiB. Die beiden Bitmaps, alle Scans
und der Nachweis bleiben fest begrenzt; der vollstaendige Bootstrap muss
weiterhin in seine bestehende 2-MiB-Identity-Map passen. Der bestehende
128-Byte-C-Handoff behaelt Layout und Rechte und aktualisiert nur den Wert
seines vorhandenen Speicherlimits. Dynamische Seitentabellen, RAM oberhalb
128 MiB, SMP, VFS, Geraete und alle produktiven i386-Artefakte bleiben
ausserhalb dieses Pakets. Alle 45 Quellvertragstests bestanden. Der Build
erzeugte ein 120.664-Byte-Bootstrap; dessen Assembly-BSS endet bei
`0x00183000`, die C-Bruecke beginnt disjunkt bei `0x00184000`, und das
Gesamtende `0x001880e0` bleibt unter 2 MiB. Der Gast meldete geordnet
`PHYSICAL_MEMORY_OK`, `PHYSICAL_MEMORY_128M_OK` und alle unveraenderten
Loader-, Scheduler-, Shell- und C-Control-Marker bis `RING3_SHELL_OK`.
R8.2f ist abgeschlossen. Der bislang auf 64 MiB begrenzte ordinary-allocation-Pfad von
ELF64-Loader, Userstack und Prozess-Seitentabellen wird gemeinsam auf die in
R8.2e abgenommene 128-MiB-Direct-Map angehoben. Ein Boot-Selbsttest erzwingt
hohe Frames, raeumt sein Auswahlfenster vor CPL3 ab und verlangt danach den
vollstaendigen bestehenden Ressourcen-Cleanup. Alle 46 Quellvertragstests,
der isolierte 121.056-Byte-Build und der native Ein-vCPU-/128-MiB-QEMU-Dialog
bis `HIGH_FRAME_CONSUMERS_OK` und `RING3_SHELL_OK` bestanden; die Queue ist leer.

R8.2g ist abgeschlossen. Der x86_64-Scheduler allokiert fuer jede live Generation
PML4, PDPT, PD und PT aus dem gemeinsamen 128-MiB-Framebestand. Der Aufbau
publiziert CR3 und READY erst nach vollstaendiger Validierung; Reap und
Force-Cleanup muessen alle Ebenen unter Kernel-CR3 nullen und exakt einmal
freigeben. 47 Quellvertragstests, der warnungsfreie 121.208-Byte-Build und der
native Ein-vCPU-/128-MiB-QEMU-Lauf bis `DYNAMIC_PROCESS_TABLES_OK` und
`RING3_SHELL_OK` bestanden. Die vier Slots und alle bestehenden ABIs bleiben
fest; die Queue ist leer.

R8.2h ist abgeschlossen. Der verbleibende fruehe x86_64-Einzelprozesspfad ersetzt seine
statische PML4-/PDPT-/PD-/PT-Arena durch vier allokierte Frames. `user_cr3`
wird erst nach dem vollstaendigen W^X-/NX-Aufbau sichtbar; jeder Erfolg oder
Fehler stellt Kernel-CR3 wieder her und gibt Tabellen, Stack und ELF-Frames
exakt einmal zurueck. 48 Quellvertragstests, der warnungsfreie
125.944-Byte-Build und der native QEMU-Lauf bis `EARLY_EXECUTION_TABLES_OK`
und `RING3_SHELL_OK` bestanden; die Queue ist leer.

R8.2i ist abgeschlossen. Die geplante x86_64-Ring-3-Shell integriert GETPID,
SPAWN und WAIT ueber `RUN`. Ein einziges Kind in Slot 1/Generation 41 teilt nur
RX-Code, besitzt private Tabellen und Stack, beendet sich mit 77 und wird vor
dem Wakeup der Shell vollstaendig reaptiert. 49 Quellvertragstests, der
warnungsfreie 126.700-Byte-Build und der native QEMU-Dialog mit `INFO`, `RUN`
und `EXIT` bestanden bis `RING3_SHELL_OK`; die Queue ist leer. Kein VFS- oder
argv-Vertrag wird vorweggenommen.

R8.2j ist abgeschlossen. Der vollstaendig reaptierte Shell-Kindslot wird fuer
genau einen zweiten `RUN`-Zyklus als Generation 42 wiederverwendet. 50
Quellvertragstests, der warnungsfreie 126.868-Byte-Build und der native
`INFO`-/`RUN`-/`RUN`-/`EXIT`-QEMU-Dialog mit exakt zwei `RUN_OK`-Markern
bestanden bis `RING3_SHELL_OK`. Beide WAIT-Aufrufe blockieren
generationengenau, jede Wiederverwendung setzt leere Queue-, Beziehungs-,
Wait- und Framebesitzdaten voraus, und die Queue ist leer. Parallele Kinder,
VFS und argv bleiben ausserhalb dieses Pakets.

R8.2k ist abgeschlossen. `/shell/child` waehlt ein separat assembliertes und
gelinktes 360-Byte-System-V-AMD64-ELF64-Abbild statt eines Shell-Entry-Modus.
Drei feste Loaderkontexte halten Probe, Shell und Kind getrennt; jeder
Kind-Reap gibt auch den exakten Abbildkontext frei, bevor die Shell wieder
laeuft. 51 Quellvertragstests, der warnungsfreie 127.488-Byte-Build und der
native QEMU-Dialog mit zwei frischen Kindladungen bestanden bis
`RING3_SHELL_OK`; die Queue ist leer. VFS, argv und dynamische Registries
bleiben ausgeschlossen.

R8.2l ist abgeschlossen. Die reale x86_64-Shell verwendet fuer `RUN` den
bestehenden REIST-v1-Index `SPAWNV` 30 mit exakt `argv[0] = /shell/child` und
`argv[1] = token77`. Der Kernel validiert den gesamten privaten Shell-Stack vor
jeder Wirkung und baut einen festen System-V-AMD64-Startstack im privaten
NX-Kindstack. 52 Quellvertragstests, der warnungsfreie 128.328-Byte-Build und
der native QEMU-Lauf mit zwei erfolgreichen Kind-Eigenpruefungen bestanden bis
`RING3_SHELL_OK`; die Queue ist leer. Variable Argumentlisten, Umgebung, VFS
und Capability-Vererbung bleiben ausgeschlossen.

R8.2m ist abgeschlossen. Vier feste generationengebundene Syscallprofile
ergaenzen den voll belegten Taskrecord ohne ABI- oder Kapazitaetswachstum. Die
Shell erhaelt nur ihre acht benoetigten Indizes, das Kind ausschliesslich
`EXIT` 9. Beide Kindgenerationen beobachteten einen wirkungslosen
`GETPID`-Versuch als `EACCES`, liefen ueber denselben IRETQ-/Runqueue-Rueckweg
weiter und beendeten nach der bestehenden Stackpruefung mit 77. 53
Quellvertragstests, der warnungsfreie 129.680-Byte-Build und der native
QEMU-Dialog bis `RING3_SHELL_OK` bestanden. Reap loeschte jedes exakte Profil;
alle vier Records und alle bisherige Autoritaet waren vor Erfolg null. Die
x86_64-Queue ist leer.

R8.2n ist abgeschlossen. Der reale x86_64-`RUN`-Pfad besitzt genau einen
festen REIST-v1-IPC-Endpoint, eine 140-Byte-Nachricht und vier feste
Capabilityrecords. Generation 40 behaelt `RECEIVE` und `CONTROL` und delegiert
nur `SEND` an die exakte Kindgeneration. Das Handle wird ueber den privaten
System-V-Auxv-Typ `0x52534901` uebergeben. Beide Kinder beobachteten
`RECEIVE -> EACCES`, sendeten `token77`, gaben ihre Capability frei und
endeten mit 77. 54 Quellvertragstests, der warnungsfreie 136.364-Byte-Build und
der native QEMU-Dialog mit exakt zwei `RUN_OK`-Markern bestanden bis
`RING3_SHELL_OK`. Close, Reap, Fehlercleanup und Abschlusspruefung hinterliessen
Endpoint, Nachricht und alle vier Capabilityrecords null. Die x86_64-Queue ist
leer; allgemeine Endpointregistries, Queue-Tiefe groesser eins und produktive
x86_64-Integration bleiben ausserhalb dieses Nachweises.

R8.2o ist abgeschlossen. Genau ein generationengebundener
`IPC_RECEIVE_TIMEOUT`-Wait nutzt die vorhandene feste PIT-Deadlinequeue. Jeder
reale `RUN` bewies zuerst einen unveraenderten leeren Output nach echter
10-ms-Deadline und `ETIMEDOUT`; danach blockierte der Parent vor der
Kindausfuehrung und nur der validierte SEND der exakten Kindgeneration lieferte
`token77` und weckte Generation 40. 54 Quellvertragstests, der warnungsfreie
138.000-Byte-Build und der native QEMU-Dialog mit zwei `RUN_OK`-Markern
bestanden bis `RING3_SHELL_OK`. Timer, Deadline, Waiter, Endpoint, Nachricht,
Capabilities, Tasks, Loader und Frames waren vor Erfolg null; die Queue ist
leer.

R8.2p ist abgeschlossen. Der reale `RUN`-Nachweis behaelt genau einen
Endpoint und einen 140-Byte-Queue-Slot: Das Kind publiziert `token76` ohne
wartenden Empfaenger. Die normale Delegate-Rueckkehr und drei begrenzte
Parent-`YIELD`s decken die zwei vorherigen Denial-Proofs, die Publikation und
den wiedereingereihten Full-Queue-Versuch ab; dessen `token77`-SEND erhaelt
wirkungslos `EAGAIN` und gibt per `YIELD` an den Parent ab.
Der Parent entnimmt und prueft `token76`, installiert danach den bestehenden
10-ms-Receive-Wait und laesst den Kind-Retry `token77` ueber den bereits
validierten Deadline-/Wakeup-Pfad liefern. Blocking-Sender, tiefere Queues und
mehrere Waiter bleiben ausserhalb des Pakets. 54 Quellvertragstests, der
warnungsfreie 142.800-Byte-Build und der native QEMU-Dialog mit zwei
`RUN_OK`-Markern bestanden bis `RING3_SHELL_OK`; alle IPC- und Lifecycle-
Records waren vor Erfolg null. Die x86_64-Queue ist leer.

R8.2q ist abgeschlossen. `IPC_SEND_TIMEOUT` 53 besitzt genau einen festen,
kernel-eigenen Nachrichtensnapshot mit generationengebundenem Handle und
monotoner Deadline. Der Parent-Selbsttest idlet auf dem belegten Ein-Slot-
Endpoint wirklich bis `ETIMEDOUT`, ohne Queue oder Callernachricht zu
veraendern. Danach blockiert der Kind-Sender auf derselben vollen Queue; das
validierte Parent-Dequeue entfernt seine exakte Deadline, publiziert den
Snapshot in den frei gewordenen Slot und weckt nur diese Kindgeneration.
54 Quellvertragstests, der warnungsfreie 145.572-Byte-Build und zwei reale
QEMU-`RUN`-Zyklen bestanden bis `RING3_SHELL_OK`. Weitere Senderwaiter und
tiefere Queues bleiben ausgeschlossen; die x86_64-Queue ist leer.

R8.2r ist abgeschlossen. Beide bestehenden `RUN`-Zyklen behalten zuerst den gesamten
R8.2q-Ablauf. Danach gibt die exakte Kindgeneration ihre delegierte `SEND`-
Capability frei, waehrend der Parent auf dem leeren Endpoint blockiert. Nur
nach vollstaendiger Validierung werden Receive-Deadline und Timer entfernt;
der Parent erhaelt `EPIPE` bei bytegleichem Ausgabepuffer. Dieselbe noch
lebende Kindgeneration wird erneut delegiert, `token78` belegt den einzelnen
Queue-Slot und `token79` blockiert als Send-Snapshot. Owner-`IPC_CLOSE`
widerruft Queue, Snapshot, beide Capabilities, Deadline, Timer und Endpoint
und weckt nur dieses Kind mit `EBADF`. Ein begrenzter Kind-`YIELD` nach der
erneuten Delegation laesst den Parent vor `token79` exakt `token78` publizieren.
55 Quellvertragstests, der 155.244-Byte-Build und der reale QEMU-Lauf bis
`RING3_SHELL_OK` bestanden. ABI und feste Kapazitaeten bleiben unveraendert.

R8.1a hat den Dual-Architekturpfad begonnen. Der vorhandene i386-Kernel samt
Userspace, BIOS-Images, VMware-/Hardwarepaketen und Installern bleibt der
unveränderte Standard und Fallback. Das getrennte 14.360-Byte-Bootstrap-
Artefakt prüft CPUID-Long-Mode, baut drei statische Seitentabellen auf, bildet
genau die ersten 2 MiB identisch ab und prüft nach dem Far-Transfer im
64-Bit-Code `CR0.PG`, `CR4.PAE` und `EFER.LMA`. Sechs Quellvertragstests, der
isolierte Windows-Build und der Ein-vCPU-/32-MiB-QEMU-Lauf bestanden; der
Laufzeitmarker erschien nach 1,8 Sekunden. Ein vollständiger x86_64-Kernel,
ELF64-Prozesse und 64-Bit-Userspace sind ausdrücklich spätere Pakete.

R8.1b ist als nächster isolierter Architekturbaustein abgeschlossen. Es ergänzt die
32 reservierten Exceptionvektoren, normalisierte 64-Bit-Frames, eine statische
TSS mit Double-Fault-IST und einen exakt validierten `UD2`-Resume-Probe. Das
Paket aktiviert keine Hardware-IRQs und erweitert weder Paging noch Prozess-,
Syscall- oder Userspace-ABI. Neun Quellvertragstests, der warnungsfreie
16.844-Byte-Build und der Ein-vCPU-/32-MiB-QEMU-Lauf bestanden. Der Gast
meldete geordnet Long Mode, IDT-Bereitschaft, den behandelten Vektor 6 und die
erfolgreiche Rückkehr.

R8.1c ist abgeschlossen. Es ersetzt die dauerhafte 2-MiB-
Identity-Map im isolierten Bootstrap durch feste 4-KiB-Abbildungen eines
kanonischen Higher-Half-Alias. Text, schreibgeschützte Daten und veränderliche
Daten erhalten getrennte W^X-/NX-Rechte; `CR0.WP` und `EFER.NXE` werden vor
dem Nachweis aktiviert. Nach dem Wechsel auf den Higher-Half-Stack wird die
niedrige Übergangsabbildung widerrufen und ein exakt validierter NX-Page-Fault
beweist den Schutz. Physische Speicherkarte und Allocator folgen erst in
R8.1d; der produktive i386-Pfad wird nicht verändert.
Elf Quellvertragstests und der warnungsfreie 26.180-Byte-Build bestanden. Der
Ein-vCPU-/32-MiB-QEMU-Lauf meldete geordnet Higher-Half-Paging, IDT, UD2 und
den NX-Page-Fault, bevor er den Abschlussmarker erreichte. Die erste
Laufzeit-Selbstprüfung wurde einmal gezielt korrigiert, damit ausschließlich
die CPU-eigenen PTE-Accessed-/Dirty-Bits vom Vergleich ausgenommen sind. Die
Queue ist wieder leer.

R8.1d ist abgeschlossen. Es validiert die Multiboot-v1-
Speicherkarte mit festen Grenzen, reserviert Bootstrap- und verwendete
Handoffbereiche und verwaltet ausschließlich vollständige 4-KiB-Frames unter
64 MiB. Zwei feste Bitmaps trennen verwaltbaren RAM von aktuellen
Allokationen. Eine statische 4-KiB-Direct-Map ist RW/NX und bildet nur diese
Frames ab. Der isolierte Laufzeitnachweis umfasst Allokation, Schreibzugriff,
Free/Reuse sowie ungültiges und doppeltes Free. Dynamische Seitentabellen und
Speicher oberhalb 64 MiB bleiben späteren Paketen vorbehalten.
Vierzehn Quellvertragstests und der warnungsfreie 29.788-Byte-Build bestanden.
Der Ein-vCPU-/32-MiB-QEMU-Lauf allozierte drei eindeutige Frames, schrieb ueber
deren RW/NX-Direct-Map, pruefte Free/Reuse sowie negative Free-Faelle und
stellte den urspruenglichen Freizaehler wieder her. Der Gast meldete
`PHYSICAL_MEMORY_OK` vor dem bestehenden Abschlussmarker. Die Queue ist wieder
leer.

R8.1e ist abgeschlossen. Ein separat assembliertes und als
ELF64-`ET_EXEC` gelinktes Probeabbild wird in das isolierte Bootstrap-Artefakt
eingebettet. Der Gast validiert ELF-Identitaet, x86_64-Maschine, Header- und
Programmtabellengrenzen sowie hoechstens zwei W^X-konforme `PT_LOAD`-Segmente
in einem festen Acht-Seiten-Userfenster. Segmentdaten und BSS werden nur in
Frames des R8.1d-Allokators gestaged, byteweise nachgeprueft und danach
vollstaendig freigegeben. Ring-3-Wechsel, User-Seitentabellen und Syscalls
bleiben R8.1f vorbehalten. HPASA ist ein eigenstaendiges Projekt ausserhalb
dieses REIST-Repositories und ausserhalb der R8-Roadmap.
Siebzehn Quellvertragstests bestanden. Nach einer gezielten Korrektur eines
mehrdeutigen NASM-Labels erzeugte der warnungsfreie Build ein 45.156-Byte-
Bootstrap mit einem unabhaengig gelinkten 9.008-Byte-ELF64-Probeabbild. Der
Ein-vCPU-/32-MiB-QEMU-Lauf meldete `ELF64_LOAD_OK` geordnet zwischen
`PHYSICAL_MEMORY_OK` und dem Abschlussmarker und stellte zuvor alle Frames
sowie den urspruenglichen Freizaehler wieder her. Die Queue ist wieder leer.

R8.1f ist abgeschlossen. Es behaelt ein vollstaendig validiertes
R8.1e-Abbild kontrolliert, bildet dessen hoechstens acht Seiten und genau eine
getrennte NX-Stackseite in einer privaten statischen Vier-Ebenen-Hierarchie ab
und betritt mit festen Selektoren und Flags einmal CPL3. Der einzige zulaessige
Syscall ist die bestehende REIST-v1-Nummer 9 (`EXIT`) nach AMD64-Konvention.
Ein zweiter Eintritt provoziert einen exakt gebundenen User-`UD2`; nur dieser
CPL3-Fehler wird ueber TSS-RSP0 enthalten. Vor Erfolg muessen CR3 und
Syscall-MSRs zurueckgesetzt, alle Userabbildungen geloescht und alle Frames
freigegeben sein. Scheduler, allgemeine Syscalls und produktiver 64-Bit-
Userspace bleiben spaeteren Paketen vorbehalten. HPASA bleibt ein eigenes,
unabhaengiges Projekt.
Alle 21 Quellvertragstests bestanden. Der warnungsfreie Build erzeugte ein
50.980-Byte-Bootstrap mit einem unabhaengigen 9.048-Byte-ELF64-Probeabbild. Der
Ein-vCPU-/32-MiB-QEMU-Lauf beruehrte den Userstack, akzeptierte exakt Exit 9
mit Status 100, enthielt danach den CPL3-`UD2` und meldete
`USER_EXECUTION_OK` geordnet vor dem Abschlussmarker. Die Nachpruefung maskiert
nur CPU-eigene Accessed-/Dirty-Bits und verlangt beim Fault das architektonische
Resume-Flag; alle Rechte und Adressen bleiben exakt. CR3, Syscall-MSRs,
Usertabellen und Frames wurden vor Erfolg vollstaendig zurueckgesetzt. Die
Queue ist wieder leer.

R8.1g ist abgeschlossen. Der isolierte Bootstrap besitzt genau zwei feste,
generation-gebundene Prozessslots mit privaten Seitentabellen, privaten
writable ELF-Seiten und je einer privaten NX-Stackseite. Nur validierter
unveraenderlicher RX-Code darf geteilt werden. Der kooperative Pfad akzeptiert
ausschliesslich die vorhandenen REIST-v1-Syscalls `YIELD` 40 und `EXIT` 9.
Task B muss nach einer festen Interleaving-Sequenz mit einem exakt validierten
CPL3-`UD2` isoliert werden; Task A muss danach weiterlaufen, seine private
Datenseite erneut bestaetigen und erwartungsgemaess beenden. Timerpreemption,
SMP, allgemeine Syscalls und produktive Integration bleiben ausserhalb dieses
Pakets. HPASA bleibt ein separates Projekt.
Alle 26 Quellvertragstests bestanden. Der warnungsfreie Build erzeugte ein
62.612-Byte-Bootstrap mit einem 9.264-Byte-ELF64-Probeabbild. Der Ein-vCPU-/
32-MiB-QEMU-Lauf fuehrte die drei erwarteten Handoffs aus, isolierte und reapte
nur Task B nach dessen exakt adressiertem CPL3-`UD2`, setzte Task A fort und
nahm dessen Exit 9 mit Status 101 an. Der Lauf meldete
`PROCESS_SCHEDULER_OK` geordnet vor dem Abschlussmarker und stellte CR3, TSS,
Syscall-MSRs, alle Tasktabellen und den urspruenglichen Freizaehler wieder her.
Die Queue ist wieder leer.

R8.1h ist abgeschlossen. Der isolierte Bootstrap erweitert die IDT genau um Vektor 32,
remappt die beiden Legacy-PICs mit gesicherten Masken, laesst ausschliesslich
PIT-IRQ0 zu und nimmt exakt drei validierte Kernel-Timerereignisse innerhalb
einer festen TSC-Grenze an. Das Paket bildet gemeinsam die notwendige
maskierbare Interrupt- und Clockgrundlage fuer die folgende CPL3-
Timerpreemption. LAPIC, IOAPIC, SMP und produktive Clockintegration bleiben
noch ausserhalb der Scheibe.
Alle 27 Quellvertragstests bestanden. Der warnungsfreie Build erzeugte ein
68.888-Byte-Bootstrap; das ELF64-Probeabbild blieb 9.264 Byte gross. Der kurze
Ein-vCPU-/32-MiB-QEMU-Lauf nahm exakt drei validierte IRQ0-Frames an und
meldete `TIMER_IRQ_OK` geordnet vor dem Abschlussmarker. IF, IRQ0, beide
PIC-Masken und die temporaere Generation waren davor restauriert. Die Queue
ist wieder leer.

R8.1k ist abgeschlossen. Vier feste private Prozessgenerationen werden ueber eine
generationengebundene Vier-Slot-FIFO in der exakten Reihenfolge 0-1-2-3-0-2
ausgefuehrt. Tasks 0 und 2 geben je einmal ab, Task 1 beendet direkt und nur
Task 3 darf nach einem exakten CPL3-`INT3` isoliert werden. Doppelte und stale
Queueeintraege muessen vor jeder Mutation scheitern. Allgemeine Spawnpolicy,
Blocking, Prioritaeten, SMP und produktive Integration bleiben ausserhalb. 30
Quelltextpruefungen und der 81.524-Byte-Build mit 9.936-Byte-Probe sind gruen.
Der kurze Ein-vCPU-/32-MiB-Lauf meldete `RUNQUEUE_LIFECYCLE_OK`; Vector 3,
Queue, Taskrecords, CR3, TSS, Syscall-MSRs, Frames und Freizaehler waren davor
vollstaendig restauriert.

R8.1m ist abgeschlossen. Die englische und deutsche Projektwebsite ist auf dem
den abgenommenen Stand bis R8.1k und trennt produktives i386 klar vom isolierten
x86_64-Bootstrap. Alle acht vorhandenen Laufzeitaufnahmen wurden gegen ihre
Bildtexte geprueft und bleiben als zutreffende i386-Evidenz erhalten; es wurde
kein synthetisches Bild und kein schwerer QEMU-Capture erzeugt. Drei lokale
Website-Vertragstests pruefen Sprachen, Claims, Links, PNG-Masse und Metadaten.
Zwei neue echte 1024x768-QEMU-Aufnahmen zeigen die aktuellen Desktop-Icons
sowie Editor, Scrollbars und About-Dialog. Der Standard-VGA-Capture deckte
einen begrenzt degradierten Beschleunigungspfad auf; die abgenommenen Bilder
stammen vom funktionierenden QEMU-VMware-VGA-Pfad. Danach folgte R8.1l.

R8.1l ist abgeschlossen. Eine feste Vier-Eintrag-Deadline-Liste verbindet die
Vier-Slot-FIFO mit `SLEEP_MS` 41 und `MONOTONIC_MS` 42. Drei private Tasks
blockieren fuer ein, zwei oder drei relative 100-Hz-Ticks; ein vierter liest
die monotone Millisekundenzeit und beendet direkt. Der absolute Horizont ist
auf acht Ticks begrenzt und nimmt damit einen beim ersten CPL3-Eintritt bereits
anstehenden PIT-Tick auf, ohne eine unbeschraenkte Wartezeit einzufuehren.
31 Quellvertragstests, der 89.188-Byte-Build mit 10.088-Byte-Probe und der
kurze Ein-vCPU-/32-MiB-QEMU-Lauf bestanden. Der Gast weckte exakt 1-2-0,
akzeptierte Status 120 bis 123, leerte beide Queues und meldete
`DEADLINE_SLEEP_OK` vor dem unveraenderten Abschlussmarker. Die Queue ist leer.

R8.1n ist abgeschlossen. Parent-Slot 0 erzeugt Kind-Slot 1 erst nach einer
begrenzten privaten Pfadpruefung. `WAIT` blockiert den Parent, konsumiert
Kindstatus 77 genau einmal und gibt den Slot erst nach Reap fuer Generation 32
frei. Nullpfad, doppelter Spawn, fremde PID, Null-Statusausgang und stales Wait
werden vor Seiteneffekten abgelehnt. 32 Quellvertragstests, der 92.372-Byte-
Build mit 10.264-Byte-Probe und der kurze Ein-vCPU-/32-MiB-QEMU-Lauf bestanden;
`SPAWN_WAIT_OK` erschien vor dem unveraenderten Abschlussmarker.

R8.2a ist abgeschlossen. Der isolierte ELF32-Multiboot-Container bettet einen
separat vollstaendig gelinkten ELF64-freestanding-C-Kern ein, ohne den i386-
Produktionsgraphen zu beruehren. Ein gepackter 128-Byte-Handoff Version 1
uebergibt ausschliesslich validierte Architektur-, Speicher-, ELF-Probe- und
Lifecyclegrenzen. Der C-Eintritt laeuft nach allen R8.1-Markern auf einem
eigenen ausgerichteten 16-KiB-Stack, beweist Data/BSS, feste Arithmetik,
begrenzte Kopie und C-zu-Assembly-Callback und loescht anschliessend Handoff
und C-Zustand. Assembly prueft die Loeschung vor `C_CORE_HANDOFF_OK`. Geraete,
VFS, DMA, SMP und produktive x86_64-Integration bleiben ausserhalb.
37 Quelltests, der 106.808-Byte-Bootstrap, das 13.328-Byte-gelinkte C-Payload
und der kurze Ein-vCPU-QEMU-Nachweis bestanden; die Queue ist leer.

R8.2b ist abgeschlossen und buendelt den naechsten vollstaendigen Pfad: ein separates
freestanding-ELF64-Shellabbild, private Ring-3-Ausfuehrung, begrenzte serielle
READ-/WRITE-Vermittlung mit den bestehenden REIST-v1-Indizes und einen echten
automatisierten `INFO`-/`EXIT`-Dialog. VFS, allgemeine Terminal- und
Geraetetreiber sowie produktive x86_64-Integration bleiben getrennt. 41
Quellvertragstests, der 117.260-Byte-Bootstrap mit 1.256-Byte-RX-Shell und der
begrenzte Ein-vCPU-/32-MiB-QEMU-Dialog bis `RING3_SHELL_OK` bestanden; die
Queue ist leer.

R8.2c ist abgeschlossen. Der unveraenderte kompakte Shell-ELF startet nicht
mehr ueber den Boot-Sonderaufruf, sondern als genau eine READY-Generation in
der vorhandenen festen Runqueue. READ, WRITE und YIELD speichern den Kontext
und dispatchen den Slot erneut ueber die generationengepruefte Queue; EXIT
wechselt ueber EXITED zu FREE. 42 Quellvertragstests, der isolierte Build und
der begrenzte Ein-vCPU-/32-MiB-QEMU-Dialog bis `SCHEDULED_SHELL_OK` und
`RING3_SHELL_OK` bestanden. Queue, Taskrecord, Tabellen, Loaderauswahl, TSS,
Syscall-MSRs, Frames und Freizaehler waren vor Erfolg bereinigt. VFS,
allgemeine Terminals, Treiber, Prioritaeten, SMP und i386 blieben unveraendert;
die Queue ist leer.

R8.2d ist abgeschlossen. Nach dem unveraenderten C-Core-Handoff publiziert
Assembly einen getrennten gepackten 64-Byte-Control-Vertrag. Der freestanding-
C-Kern validiert genau Shell-Service 1/Generation 1 und ruft ueber den festen,
lease-geschuetzten und SysV-konformen Adapter den abgenommenen Schedulerpfad
auf. Erst nach dessen Reap und Cleanup loescht C den Vertrag und meldet
`C_KERNEL_CONTROL_OK`; Assembly prueft Vertrag, Lease, CR3 und IF erneut vor
dem bestehenden finalen Shellmarker. 44 Quellvertragstests, der isolierte Build
und der begrenzte Ein-vCPU-/32-MiB-QEMU-Dialog bestanden. VFS, allgemeine
Prozessdienste, Treiber, SMP und i386 blieben ausserhalb; die Queue ist leer.

R8.1i ist abgeschlossen. Task A gibt einmal kooperativ an eine CPU-gebundene
Task B ab. Ein generation- und framevalidierter PIT-IRQ preemptiert und reapt
ausschliesslich B; A behaelt ihre private Datenseite und beendet sich mit
`EXIT` 9/Status 102. Ein festes TSC-Limit begrenzt Bs Userloop. Alle 28
Quellvertragstests bestanden. Der warnungsfreie Build erzeugte ein
70.964-Byte-Bootstrap mit einem 9.400-Byte-ELF64-Probeabbild. Der kurze
Ein-vCPU-/32-MiB-QEMU-Lauf meldete `TIMER_PREEMPTION_OK` geordnet zwischen
`TIMER_IRQ_OK` und dem Abschlussmarker. Vor Erfolg waren PIC-Masken, IF, CR3,
TSS, Syscall-MSRs, Tabellen, Taskrecords, Frames und der urspruengliche
Freizaehler restauriert. Wiederkehrende Quanten, Fairness und produktive
Schedulerintegration bleiben spaeteren Paketen vorbehalten. R8.1j folgt als
naechste begrenzte Scheibe.

R8.1j ist abgeschlossen. Zwei CPU-gebundene private CPL3-Generationen laufen
ueber genau vier PIT-Quanten in der festen Folge A-B-A-B-A. Jeder IRQ speichert
den vollstaendigen unterbrochenen AMD64- und IRET-Kontext erst nach exakter
Framevalidierung. Beide Tasks beweisen unabhaengigen privaten Fortschritt; nach
Tick vier wird nur B reaptiert und A beendet den Nachweis mit `EXIT` 9/Status
103. Alle 29 Quellvertragstests bestanden. Der warnungsfreie Build erzeugte
ein 73.820-Byte-Bootstrap mit einem 9.688-Byte-ELF64-Probeabbild. Der kurze
Ein-vCPU-/32-MiB-QEMU-Lauf meldete `QUANTUM_SWITCH_OK` geordnet vor dem
Abschlussmarker. Vier IRQs erzeugten vier Master-EOIs; Timer, PIC-Masken, IF,
CR3, TSS, Syscall-MSRs, Tabellen, Taskrecords, Frames und Freizaehler waren vor
Erfolg restauriert. Dynamische Tasks, Prioritaeten, allgemeine Fairness, SMP
und produktive Schedulerintegration bleiben ausserhalb des Pakets. R8.1k ist
als naechste begrenzte Scheibe ebenfalls abgeschlossen.

R6.2o ist abgeschlossen. Nur der ausdrückliche Ring-3-Befehl `DESKTOP` startet
den generationsgebundenen Compositor-Supervisor; kein Image startet den
Desktop automatisch. Der Fehlerinjektionslauf ließ ausschließlich die erste
AP-affine Generation ihren Heartbeat verlieren. Sie wurde begrenzt auf den BSP
zurückgeführt, ihre generationseigenen Frames und Surface-Puffer wurden
widerrufen und Generation 2 erst nach Self-Test und `COMPOSITOR_READY` erneut
AP-affin. Der unabhängige SVGA2D-Dienst und sein aktiver Scanout bleiben beim
Compositor-Fence erhalten. Der Vier-vCPU-VMware-Lauf erreichte anschließend
`DESKTOP_MOUSE_OK` in 17 Sekunden ohne Degradation oder Panic. Die Queue ist
leer.

R7.1a ist abgeschlossen. Das neue
`BENCHMARK.PRG` misst CPU, RAM, sequentielle VFS-/Datentraegerzugriffe und den
vollstaendigen VGA-Framebufferpfad mit festen Arbeitsgrenzen. Es verwendet nur
oeffentliche Ring-3-ABIs, schreibt ausschliesslich eine eigene temporaere Datei
und gibt die Ergebnisse nach Wiederherstellung der Textkonsole als feste
ASCII-Tabelle aus. Der Quell-/Layoutvertrag bestand 5 Tests, das freestanding
i386-Programm linkte als 20-KiB-MYPR und der QEMU-VGA-Paketbuild bestand in 36
Sekunden; der finale inkrementelle Paketnachweis nach exklusivem CREATE bestand
in 10 Sekunden ohne VM-Start.

Diese Datei ist der kompakte Wiedereinstiegspunkt. Maßgeblich bleiben Code,
Tests und der lokale Diff.

## Auftrag und Grenzen

- Änderungen direkt im sichtbaren lokalen Haupt-Worktree umsetzen.
- Keine Subagenten, isolierten Klone oder zusätzlichen Worktrees verwenden.
- Automatisierte Laufzeittests nur mit QEMU und VMware; echte Hardware prüft
  der Benutzer.
- Reguläre Kernel- und Ring-3-Dienste bleiben CPU-0-affin, bis die
  verbleibenden gemeinsamen Treiberzustände für R6.2 auditiert sind.
- Der begrenzte SMP-Bootstrap ist als `ad8884e8` committed. Die aktuelle
  R6.2-Scheibe baut direkt darauf auf.

## Abgeschlossener Memory-Resilience-PoC

R2.2ai ist mit erhaltener Audio-/Surface-Implementierung beendet, aber nicht
als vollständig bestanden markiert: Alle gezielten Gates und beide
Paketbuilds bestanden, der manuelle VMware-Test zeigt keine Audiostörung, doch
der kombinierte QEMU-Gate endet beim ersten retained Control-Gallery-Frame mit
`DESKTOP_AUDIO_FAIL launch status=-110`. Die Queue dokumentiert das Paket
deshalb als `cancelled` statt `done`.

R1.2a ist abgeschlossen. Vier explizite kernel-eigene 4096-Byte-Objekte
besitzen je zwei Copy-on-write-Bänke in zwei aktiven simulierten
Fehlerdomänen, generationsgebundene Critical-Object-Metadaten und CRC32 für
die Nutzdaten. Der Host-Fault-Test besteht Commit-Unterbrechungen,
Einzeldomänen- und Doppelverlust, stale Handles, CRC-/Konfliktfälle und den
festen Rebuild. Der normale QEMU-VGA-Paketbuild linkt das Modul erfolgreich.

R1.2b ist ebenfalls abgeschlossen. Ein spezieller Testbuild führt die feste
Kampagne einmal nach den Speichertests und vor allgemeiner Prozessaufnahme aus.
Der QEMU-Gast validiert geordnet Commit, degradierte committed Daten, ein
unabhängiges Objekt, Rebuild und HEALTHY, erreicht danach Shell, Userspace und
`TEST_OK` und bleibt innerhalb einer festen 180-Sekunden-Grenze. Der
Desktop-Autostart ist inzwischen für alle Targets entfernt: Jeder normale und
jeder Proof-Boot erreicht zuerst die Ring-3-Shell; `DESKTOP` bleibt ein
ausdrücklicher Benutzerbefehl. Auch diese Stufe erteilt keine User-, DMA- oder
Paging-Autorität und behauptet keine physische DIMM-/Rank-/Channel-Isolation.

R5.2x ist abgeschlossen. Die append-only USB-Diagnose
Version 7 persistiert das exakte Setup-Paket und den terminalen Eventzustand.
Completion Code 13 wird nur bei einem host-to-device Zero-Data-Request ohne
Data-TRB, mit Eventzeiger auf die Status-TRB und Restlänge null angenommen.
Alle anderen Shorts sowie fehlgeschlagenes `SET_CONFIGURATION` oder
`SET_PROTOCOL` bleiben fail-closed. Die USB-Tastaturtests bestehen 9/9, die
Maustests 16/16, das nach der Kernel-Log-/ABI-Erweiterung vollständig neu
erzeugte VMware-VGA-Paket bestand in 153 Sekunden; der abschließende
inkrementelle Paketnachweis nach der Pointer-Überlaufhärtung bestand in 16
Sekunden und der echte
VMware-RFB-Mauspfad in 14 Sekunden. Der Runtime-Nachweis wartet zuerst auf die
Ring-3-Shell, gibt erst dann `DESKTOP` ein und verwirft jeden Lauf, in dem
Desktopmarker bereits vor diesem ausdrücklichen Befehl erscheinen.

Ein abschließender automatisierter VMware-Nachlauf konnte die Paket-VM auf dem
Host nicht starten und erreichte den Gast daher nicht. Der Benutzer startete
das erzeugte VMware-Paket anschließend manuell und bestätigte dessen
fehlerfreien Betrieb. Diese manuelle Abnahme ergänzt den zuvor bestandenen
automatisierten RFB-Lauf; sie wird nicht als automatischer Gate-Erfolg
ausgegeben.

Der erfolgreich physisch getestete Kandidat liegt unter
`build/r5.2x-physical-test/reist-os-r5.2x.img` (64 MiB, SHA-256
`DBA5E5794405180CC11CBCB3FDB2BF81FDA001B2206BFF17700ED29135EDA3C3`). Der
`real_hw/vga`-Build erzeugte das Image vollständig; RSA-PSS-Signatur und
Bootmanifest bestanden. Als begrenzter Hardware-Diagnosekandidat wurde der
Release-SBOM-Schritt für den verschachtelten Testordner ausdrücklich
übersprungen; das Image ist daher kein Releaseartefakt. Der ASUS-Nachtest
bestätigt nun die USB-Tastatur am USB-3/xHCI-Port und den paged `DMESG`-Zugriff.
Die physische Paketabnahme ist damit für diese konkrete Hardwarekombination
erfüllt; eine breite xHCI-Freigabe wird nicht behauptet.

Der ASUS-Nachtest bestätigt die Tastatur am separaten USB-2.0-Controller. Am
USB-3.0-Port wurde sie zunächst nicht nutzbar, funktionierte jedoch parallel,
sobald eine PS/2-Tastatur angeschlossen war. Das grenzt den Restfehler auf eine
Bootzeitabhängigkeit des xHCI-Pfads ein. Der feste 500-ms-Zeitraum zum Sammeln
sichtbar werdender Root-Ports gilt deshalb jetzt für jeden xHCI-Controller und
nicht mehr nur nach Intel-Companion-Routing. Tastatur, Boot-HID-Parser und
allgemeiner Eingabepfad bleiben unverändert. Der nie
freigegebene Desktop-Autostart ist ausnahmslos aus dem Kernelboot aller Images
entfernt, damit die physische Diagnose sichtbar bleibt. Ein nachfolgend
erfolgreicher Mauskandidat darf den vorherigen Tastatur-Control-Fehler nicht
mehr löschen. Die Shell-Startanzeige gibt beim Ausfall nun ohne Eingabe auch
Setup-Paket, Completion, Restlänge, Eventstufe und Flags aus. Nur bei einer
nicht bereiten USB-Tastatur ergänzt sie weiterhin eine kompakte Startdiagnose.
Unabhängig vom 80x25-Scrollback hält ein neuer fester 32-KiB-Kernel-Logring alle
Ring-0-Konsolenausgaben im Speicher. Der append-only Syscall 125 und das
Ring-3-Programm `DMESG` lesen einen begrenzten Snapshot; `DMESG` pausiert
standardmäßig nach 22 Zeilen mit Leertaste, Enter oder Q. Der Host-Ringtest
besteht Wrap, Stale-Cursor und begrenzte Reads. Der erste ASUS-Kandidat wies
den Syscall wegen der veralteten exklusiven Prozessprofilgrenze 125 mit `-13`
ab; sie ist nun append-only auf 126 angehoben, und `DMESG` zeigt künftige
Fehlercodes an. Der inkrementelle `real_hw/vga`-Build umfasst 104 Kernelobjekte,
baute `DMESG.PRG` neu und bestand Signatur- sowie Bootmanifestprüfung.

Jeder `real_hw`-Build veröffentlicht künftig unabhängig vom internen
Ausgabeordner zusätzlich `build/reist-os-real-hw.img`. Die Samsung- und
Fujitsu-Installer verwenden ausschließlich diesen nach einer zweiten
Manifestprüfung atomar veröffentlichten Pfad. QEMU- und VMware-Builds können
das physische Installerartefakt dadurch nicht überschreiben.
Das aktuell validierte 64-MiB-Artefakt hat SHA-256
`BF774039CF11370093B49E4E0D20094FB1315E15E440E84E6778B23F0E4DBFE9`.

Ein weiterer ASUS-Kaltstart mit demselben Image zeigte eine verbleibende
Zeitabhängigkeit: Ohne gleichzeitig angeschlossene PS/2-Tastatur endete bereits
der erste acht Byte große `GET_DESCRIPTOR(Device)`-Request ohne übernommenes
Transfer-Event (`cc=0`, `residual=8`, `stage=0`, Timeout/Failed). Mit PS/2 war
derselbe USB-Pfad nutzbar. R5.2y ergänzt deshalb nach erfolgreichem xHCI
`Address Device` die feste USB-2.0-SetAddress-Recovery vor dem ersten
EP0-Doorbell; fehlende Events und Descriptor-Shorts bleiben fail-closed.
R6.2o blieb bis zu diesem Hardware-Nachtest geordnet in der Queue und ist nach
dem erfolgreichen Nachweis inzwischen abgeschlossen.

Der R5.2y-Kandidat besteht 10 Tastatur- und 16 Maustests sowie den
VMware-VGA-Paketbuild in 15 Sekunden. Der vollständige `real_hw/vga`-Build
veröffentlichte nach zwei erfolgreichen Bootmanifestprüfungen das 64-MiB-Image
`build/reist-os-real-hw.img` mit SHA-256
`93E416F48327CA62E7056B6CD9D791D983E62782A0B3318B2A83B6488691B8A5`.
Die ASUS-Abnahme startete dieses Image ohne angeschlossene PS/2-Tastatur kalt;
die USB-Tastatur funktioniert am betroffenen xHCI-Port. R5.2y ist damit
abgeschlossen. Die Bestätigung gilt für diese Controller-/Gerätekombination
und ist keine breite USB-Hardwarefreigabe.

## Erreichter stabiler Meilenstein

- ACPI/MADT-Erkennung, AP-Trampolin und begrenzter INIT/SIPI-Start für bis zu
  15 APs.
- Private AP-GDTs, Runtime-/Double-Fault-TSS, guard-page-geschützte Stacks und
  CPU-lokaler IRQ-, Präemptions-, Adressraum- und Schedulerzustand.
- CPU-besitzende Spinlocks, sleepfähige deadlinebegrenzte Kernelmutexe,
  atomarer Runqueue-Handoff und generationsgebundener TLB-Shootdown.
- Serialisierte Waitqueue-, IPC-, Prozess-, Socket-, VFS-, FAT32-, ATA-,
  AHCI-, FDD- und Storage-Pool-Pfade.
- Drei AP-affine Scheduler-, Mutex-, Integritäts- und Storage-Probetasks werden
  nach Abschluss generationsgeprüft reap't.
- 48 feste Kernelstack-Slots erhalten die 32 öffentlichen Taskslots auch bei
  maximal 15 AP-Idle-Stacks. `CAPWAIT.PRG` belegt im Gast alle 32 Taskslots
  gleichzeitig und liefert `TASK_CAPACITY_OK`.
- Der Unicode-Desktopprobe führt nach Capability-Aktivierung einen realen,
  begrenzten VMware-SVGA2D-`RECT_COPY` aus.
- IRQ-Kontextdiagnosen speichern den Vektor CPU-lokal. Eine rekursive
  Spinlock-Panic enthält Lockadresse, CPU und symbolisierbare Aufruferadresse.

## Verifizierte Evidenz

- `python test/test_smp.py -v` – 24/24
- `python test/test_sync_diagnostics_r13.py -v` – 28/28
- `python test/test_block_transactions_r13.py -v` – 10/10
- `python test/test_vmware_svga2d.py -v` – 16/16
- `python test/test_qemu_smoke.py -v` – 41/41
- Systemprogramm-Toolchaintest – 1/1
- `scripts/build-windows.ps1 -Target qemu -Video vga -SkipReleaseSbom` – PASS
- Vier aufeinanderfolgende vollständige QEMU-SMP4-Läufe nach der
  Lockdiagnose – PASS; der letzte zusätzlich mit VMware-SVGA2D.
- `python test/test_usb_keyboard.py -v` – 5/5
- `python test/test_usb_mouse.py -v` – 14/14
- `python test/test_reist_supervisor.py -v` – 9/9
- `python test/test_vmware_mouse.py -v` – 6/6
- `python test/test_desktop_source.py -v` – 41/41
- VMware-VGA-Paket – PASS in 14 Sekunden; `vmware-mouse`-Runtime – PASS in
  13 Sekunden mit geordneten Markern `USB: xHCI HID ready`,
  `REIST_SMP SCHEDULER_READY cpus=4`, `COMPOSITOR_READY`, `DESKTOP_OK`,
  `DESKTOP_EXPLORER_OK`, `COMPOSITOR_AP_EXEC` und `DESKTOP_MOUSE_OK`.

Referenzlauf:

```powershell
python scripts/run_qemu_smoke.py --image build/reist-os.img --smp 4 --expect-smp --vmware-vga --expect-svga2d --timeout 120 --log build/codex-agent/smp4-final-low-rect.log
```

Er enthält `online=4`, alle SMP-READY-Marker, `REAP_READY`,
`TASK_CAPACITY_OK`, `SVGA2D_RECT_COPY_OK` und `TEST_OK`, ohne Panic,
Assertion oder unbekannten FIFO-Befehl.

## VMware-Maus und Explorer wiederhergestellt

Der virtuelle VMware-xHCI-Boot-Mausendpunkt kodiert Max ESIT Payload nun in
den standardisierten Endpoint-Context-Feldern statt den Average-TRB-Wert in
das High-Payload-Feld zu duplizieren. Der überwachte Compositor darf den exakt
identifizierten Displaydienst verbinden. Seine Pointer-Präsentation hält die
Präemption nicht mehr über dem sleepfähigen Displaymutex; die bestehende
Frame-Reservierung bleibt die Veröffentlichungsgrenze.

Das Default-Deny-Compositorprofil besitzt zusätzlich nur den begrenzten
Submit-/Collect-/Cancel-/Bulk-Transport für serviceeigene VFS-Shadow-Objekte.
Der Syscall weist für diese Domäne Raw-Block-, Format- und Maintenance-
Operationen vor jeder Veröffentlichung ab. Damit öffnet der initiale Explorer
`/` wieder. Ein deadlinebegrenzter VMware-Runner startet ausschließlich bei
leerem Workstation-Laufzustand, speist eine virtuelle Bewegung ein, verlangt
die geordneten Explorer-/Mausmarker und beendet genau seinen einzigen VMX-
Prozess. Der Benutzer bestätigte die Maus zusätzlich sichtbar; der frische
Gast bestätigte den Root-Explorer über den Erfolgsmarker.

## R6.2n abgeschlossen

Der veröffentlichte xHCI-Eventring, die Diagnosesnapshots und die
generationsgebundenen HID-Tastatur-/Mauszustände werden als kurze, feste
IRQ-sichere SMP-Transaktionen serialisiert. Die deadlinegebundene
Controllerkonstruktion bleibt davon getrennt und vollständig BSP-only. Danach
darf genau die gesunde aktuelle Compositorgeneration ihre im geschützten
Kontrollrecord abgelegte Einmal-AP-Maske nach `SERVICE_READY` übernehmen.

Die Abnahme verwendet die generierte VMware-Referenz mit vier vCPUs. Legacy-
PIC-xHCI-IRQ bleibt auf CPU 0; der Compositor muss vor der virtuellen
Mauszustellung AP-Ausführung melden. Die virtuelle Bewegung kommt
deterministisch über einen ausschließlich an `127.0.0.1:5909` gebundenen
RFB-3.8-Pointerevent; physisches HID-Passthrough bleibt verboten. Das Paket-
Gate bestand in 14 Sekunden, der Vier-vCPU-Runtime-Nachweis in 13 Sekunden.
Restart-Affinität wurde nicht implizit übernommen und benötigt einen
getrennten Fehlerlauf.

## Restrisiko und nächster zusammenhängender Schritt

Der physische Fehler `VFS_ERR_IO` beim Laden von
`/usr/gui/bin/desktop.prg` trat nur auf dem AMD-Mainboard auf. Dasselbe Image
startet den Desktop auf dem unterstützten ASUS-Board ohne diesen Fehler. Das
AMD-Board wird derzeit nicht weiter benutzt; deshalb bleiben R2.2af, R2.2ag
und R2.2ah ohne Produktionskandidat abgebrochen. Insbesondere wird aus dem
AMD-Befund keine allgemeine Storage-/Startup-Admission-Änderung abgeleitet.

Die danach unter VMware und Zielhardware beobachteten Desktopausfälle beim
Abspielen einer WAV-Datei und beim Start der alten Control Gallery haben
dieselbe Quellursache: Beide Programme waren synchron gestartete
Vollbildclients. Der Compositor blockierte deshalb in `x86os_wait()` und
verpasste seinen 500-ms-Heartbeat; nach zwei Sekunden wurden nacheinander die
drei Restartbudgets verbraucht. R2.2ai migriert beide Programme auf den
delegierten Surface-Vertrag und beweist gleichzeitig die einmalige echte
Wiedergabe des paketierten Startklangs, beide aktiven Clients und fortlaufende
Compositor-Heartbeats.

Zum selben Audio-Lifecycle gehören sechs eigenständig erzeugte, unter CC0
freigegebene Systemklänge. `reist.sounds/1` ordnet Start, Ende, Fehler
und Benachrichtigung sowie erfolgreiches Papierkorb-Drop und endgültiges
Leeren festen WAV-Pfaden oder `none` zu; der bestehende
Konfigurationsdienst ist bereits die spätere Mutationsgrenze für die
Systemsteuerung. Der Desktop hält keine Audio-Capability, sondern startet
höchstens zwei generationengeprüfte `wavplay`-Kinder und wartet nie auf ein
lebendes Kind. Ein sauberer Clientwechsel benötigt das antwortlose
Audio-`RELEASE`, den tatsächlichen Peer-Entzug und einen zusätzlich in Ring 0
geprüften leeren IPC-Endpunkt; ein Crash behält die vollständige
Servicegenerationrotation.

Der VMware-Befund beim Doppelklick auf WAV-Dateien war keine HDA-Störung:
Ordneröffnungen und erfolgreiche Programmstarts lösten fälschlich
`event.notification` aus. Das kurzlebige `wavplay`-Kind belegte dadurch den
einzelnen Audio-Clientendpunkt, während der Sound Player denselben Endpunkt
öffnen wollte. Navigation und erfolgreiche Datei-/Programmaktivierung bleiben
nun still; Benachrichtigungsklang ist echten Informationsdialogen vorbehalten.
Der danach weiterhin beobachtete Fehler war im Seriellog eindeutig
`SOUNDPLAYER_AUDIO_FAIL stage=start status=-110`: Der Sound Player hatte die
öffentliche 500-ms-Audiofrist lokal auf 100 ms verkürzt, obwohl der vermittelte
Service-zu-HDA-Start selbst bis zu 500 ms beanspruchen darf. Er verwendet nun
die öffentliche Standardfrist; schnelle Starts erhalten dadurch keine
zusätzliche Wartezeit, nur der Fail-Closed-Abbruch bleibt ausreichend groß.

Die beobachtete WAV-Startverzögerung hatte eine eigene, reproduzierbare
Ursache: 96 PCM-Bytes im v1-Payload erforderten für 15360 Frames bis zu 640
synchrone Roundtrips, bevor `START` gesendet wurde. Der append-only IPC-v2-Pfad
überträgt nun 2016 PCM-Bytes beziehungsweise 504 Stereo-Frames pro Block in
einem getrennten CRC-geschützten Rendezvous-Slot. V1 bleibt bytegleich; eine
maximale Vorschau benötigt höchstens 31 bestätigte Schreibvorgänge. Der
Preview-Loader öffnet und liest die WAV-Datei nur einmal, und der Sound Player
startet die Wiedergabe vor seiner Surface-Konstruktion.

Die im REIST Editor beobachtete unbrauchbare Verzögerung von Scrollbar-Drag und
Menü-Hover lag nicht in den Controls. Der erste Reparaturversuch behielt die
lokale Paint-Differenz, Damage-Akkumulation, Inputweiterleitung unter Paintlast
und den gecachten Notepad-Viewport bei, begrenzte den Broker aber auf vier
Queue-Scheiben. Auf dem SMP-Desktop verschlechterte das vollständige Frames:
Notepad läuft auf CPU 0, der Compositor auf einem AP, und jeder nach vier
Nachrichten blockierte Produzent wartete mangels CPU-übergreifendem Wakeup bis
zum nächsten 10-ms-Schedulertick. Der Broker verarbeitet deshalb wieder bis zu
16 faire Queue-Scheiben pro Desktopzyklus; große Retained-Frames laufen über
spätere Zyklen weiter, damit Hover- und Mauseingaben nicht warten. Neu
fordert jeder Foreground-Wakeup erst nach Freigabe des Scheduler-Locks einen
spin-begrenzten Reschedule-IPI für die entfernte Affinitäts-CPU an. Ein
IPI-Fehler verändert den READY-Zustand nicht; der periodische Scheduler bleibt
Fallback. Die vorhandenen Paint- und Viewport-Optimierungen bleiben erhalten.

Der erste VMware-Nachtest mit Reschedule-IPI war wesentlich schneller, blieb
aber messbar hinter derselben VM mit nur einer aktiven CPU zurück. Ursache ist
der verbleibende Transportaufwand: Ein maximaler Paintframe benötigt bis zu 48
Queue-Refills zwischen dem BSP-Notepad und dem AP-Compositor und damit ebenso
viele lokale-APIC-/Schedulerübergaben. Das Produktionsprofil hält den
Compositor deshalb wieder auf CPU 0 bei seinen gewöhnlichen Surface-Clients.
Storage, Netzwerk, HDA, Audio-Service und geprüfte Gerätetreiber behalten ihre
AP-Nutzung. Die geschützte post-ready Compositor-AP-Maske bleibt im Supervisor
implementiert, ist aber standardmäßig leer, bis Paint-Batching oder eine
gemeinsame GUI-Affinitätsdomäne separat nachgewiesen ist.

Der im anschließenden Rescue-Shell-Bild sichtbare USB-Fehler ist eine getrennte
xHCI-Enumerationsgrenze: `config=59` bezeichnet die Länge des gelesenen
Konfigurationsdeskriptors; `cc=13` ist ein unerwarteter Short-Packet-Abschluss
bei einem nachfolgenden HID-Control-Request. R5.2x persistiert nun Typ, Request,
Wert, Index, Länge, Completion, Restlänge, Eventstufe und Flags vor dem
Doorbell-Zugriff. Die begrenzte Korrektur akzeptiert ausschließlich einen
terminalen, restlosen Status-Short für einen Zero-Data-Request und führt keinen
Retry auf dem alten EP0-Zustand aus. R6.2o blieb bis zum physischen Nachweis
queued und ist inzwischen abgeschlossen.

Die frühere sporadische Meldung einer rekursiven Übernahme von
`task_table_lock` wurde im VMware-Benchmark erneut beobachtet und über Build-ID,
Lockadresse und Aufrufer auf die Mutex-Identitätsabfrage eingegrenzt. Ursache war
die getrennte Veröffentlichung von binärem Lockwort und `owner_cpu`: Der Erwerb
prüfte das Diagnosefeld vor der atomaren Lockoperation und konnte einen
veralteten Besitzer als Rekursion deuten. Das Lockwort enthält nun selbst den
eindeutigen CPU-Token; nur der von Compare-and-swap atomar beobachtete eigene
Token löst die Rekursions-Panic aus. Ein vierkerniger QEMU-Benchmark schloss
anschließend Schreiben, bytegeprüftes Lesen, Cleanup und Shell-Rückkehr ab.

Die erste R6.2-Scheibe inventarisiert gemeinsam genutzte Treiberzustände und
gibt ausschließlich die autoritätslose, überwachte Driver-Fault-Fixture für
Online-APs frei. Null bleibt im append-only Supervisorprofil BSP-only; reale
Treiber und allgemeine Ring-3-Dienste behalten diese Voreinstellung. Der
Driver-Domain-Gastlauf muss Initialstart und Restart auf einem AP sowie Crash,
Hang, stale Generation, Reset-Fence und Budgeterschöpfung gemeinsam bestehen.
Weitere Produktionsdomänen folgen erst nach ihrem eigenen Zustandsaudit.

## Erste R6.2-Scheibe abgeschlossen

Die Driver-Fault-Domäne lief im vierkernigen QEMU-Nachweis bei Initialstart
und drei Restartgenerationen auf CPU 1 beziehungsweise CPU 3. Crash, Hang,
stale Generation, Reset-Fence und Budgeterschöpfung blieben begrenzt; die
Ring-3-Shell lief parallel auf dem BSP weiter. Das Runtime-Gate bestand in
15 Sekunden. Als Nächstes ist genau eine reale Treiberdomäne samt Controller-,
DMA-, IRQ- und Fencezustand zu auditieren; bis dahin bleibt sie BSP-affin.

## Zweite R6.2-Scheibe abgeschlossen

Der VMware-SVGA2D-Normalpfad startet weiterhin innerhalb seines bisherigen
BSP-Startup-Watchdogs. Erst nach `SCHEDULER_READY` wird die aktuelle gesunde
Generation generationsgeprüft AP-only umgebunden; Restarts fallen bewusst auf
den BSP-Default zurück. Der gemeinsame Displayzustand ist durch einen
deadlinebegrenzten Kernelmutex vor dem FIFO-Spinlock serialisiert. Der
vierkernige QEMU-Lauf führte den Treiber auf CPU 2 aus und bestätigte einen
realen `RECT_COPY`, geordnete Deaktivierung, `TASK_CAPACITY_OK` und `TEST_OK`.
Der anschließende R6.2c-Nachweis schützt die gewünschte AP-Maske im
ECC-gesicherten Driver-Control-Record. Jede Generation konstruiert und testet
sich auf dem BSP und wird erst nach gesunder Veröffentlichung AP-only. Eine
nur im Testbuild vorhandene Fault Injection unterdrückte den ersten
AP-Heartbeat: Epoch 1 wurde nach zwei Sekunden isoliert, vor dem Fence auf den
BSP zurückgeführt und innerhalb des einsekündigen Recovery-Fensters beendet.
Epoch 2 startete erneut auf dem BSP, wurde gesund, lief auf CPU 2 und schloss
den realen `SVGA2D_RECT_COPY_OK`-Pfad sowie `TEST_OK` ab. Die Treiberdomäne
erhielt dabei weiterhin keine DMA-, IRQ-, BAR- oder Raw-Port-Autorität.

## Dritte produktive R6.2-Scheibe abgeschlossen

Der HDA-Treiber konstruiert jede Generation einschließlich vermitteltem DMA,
IRQ-Bindung, Codec-Erkennung und Self-Test auf dem BSP. Nach
`SCHEDULER_READY` wird nur die gesunde Treibergeneration AP-only; Audio-Service,
Supervisor und Legacy-PIC-Hard-IRQ bleiben BSP-affin. Der SMP4-QEMU-Lauf führte
autorisierte HDA-Operationen auf CPU 3 aus, bestand fünf Playback-Zyklen und
erzeugte 278332 Stereo-S16-Frames bei 440,4 Hz mit `max-gap=1`. Für begrenzte
SMP-/PIC-Schedulinglatenz gilt ein festes Fünf-Sekunden-Heartbeatfenster;
einsekündiger Fence und Restartbudget drei bleiben erhalten.

## HDA-AP-Restart nachgewiesen

Eine compile-time-begrenzte Fault Injection unterdrückte den Heartbeat der
ersten HDA-AP-Epoch. Nach fünf Sekunden kehrte die Generation auf den BSP
zurück; der Fence entzog IRQ und Bus-Mastering, nullte den DMA-Pool und führte
das profildefinierte, auf 100 Polls und die einsekündige Recovery-Deadline
begrenzte GCTL-Resetrezept aus. Die neue Treibergeneration wurde auf dem BSP
gesund, der stale Service-Endpoint löste die normale Service-Rotation aus und
die Ersatzgeneration lief wieder auf einem AP. Fünf Playback-Zyklen ergaben
271216 Stereo-S16-Frames bei 440,4 Hz und `max-gap=1`.

## Audio-Servicegenerationen auf APs

Der geräteautoritätslose Audio-Service verbindet, testet und publiziert jede
Generation weiterhin auf CPU 0. Erst nach `SERVICE_READY` wird die geschützte
Online-AP-Maske angewandt. Vor jeder normalen Sessionrotation kehrt die alte
Generation sleepfähig auf den BSP zurück, bevor der präemptionsgeschützte
Fence-/Reap-Commit beginnt. Der SMP4-Lauf bestätigte AP-Ausführung über fünf
Rotationen und fünf PCM-Zyklen mit 274297 Frames bei 440,4 Hz und `max-gap=1`.

## Audio-Service-AP-Restart nachgewiesen

Eine nur im Fault-Build vorhandene Injection unterdrückte Fortschrittsberichte
der ersten AP-affinen Audio-Service-Epoch. Der bestehende Zwei-Sekunden-
Heartbeat isolierte die Generation; der begrenzte BSP-Handoff lief vor
Endpoint-Entzug und Reap. Die Ersatzgeneration verband sich auf CPU 0 erneut
mit der aktuellen HDA-Generation, bestand Self-Test und Ready-Publikation und
kehrte anschließend auf einen AP zurück. Fünf PCM-Zyklen erzeugten 269153
Stereo-S16-Frames bei 440,4 Hz und `max-gap=1`, ohne neue Geräteautorität.

## Storage-Service auf AP nachgewiesen

Der Ring-3-Storage-Service inventarisiert, bindet, testet und publiziert Ready
weiterhin auf CPU 0. Nach der SMP-Scheduler-Freigabe wird nur die aktuelle
gesunde Generation über die ECC-geschützte Zielmaske auf einen AP verschoben.
Der SMP4-Gast bestätigte eine autorisierte Storage-Operation auf CPU 2, den
realen Storage-Self-Test, VFS-/FAT-Zugriffe, `TASK_CAPACITY_OK` und `TEST_OK`.
ATA, AHCI, FDD, Administration und Legacy-PIC-IRQ blieben BSP-affin.

## Storage-Service-AP-Restart nachgewiesen

Die bestehende Test-Injection beendete Generation 1 erst nach einem realen
vermittelten Block-Read. Der Supervisor entzog die generationsgebundene
Request-Autorität und startete Generation 2 innerhalb des vorhandenen Budgets.
Sie band und publizierte Ready auf CPU 0, erhielt erst danach erneut die
geschützte AP-Maske und führte auf CPU 1 aus. Der SMP4-Gast bestand
`STORAGE_RESTART_OK`, `STORAGE_SERVICE_OK`, `TASK_CAPACITY_OK` und `TEST_OK`.

## Gesunder Netzwerkdienst auf AP nachgewiesen

Die drei begrenzten Crash-, Hang- und Invalid-Reply-Bootstrapgenerationen
blieben auf CPU 0 und erreichten `RECOVERY_SEQUENCE_OK`. Die gesunde vierte
Generation publizierte `SERVICE_READY` vor der SMP-Scheduler-Freigabe und
führte danach einen autorisierten Heartbeat auf CPU 2 aus. Der Gast bestand
`NETWORK_PARSER_OK`, `TASK_CAPACITY_OK` und `TEST_OK`; NIC-Treiber,
`netdev_poll`, Supervisor und Legacy-PIC-IRQ blieben BSP-affin. Vor einem
späteren Fence kehrt eine noch lebende Generation begrenzt auf CPU 0 zurück.

## Netzwerkdienst-AP-Restart nachgewiesen

Im RTL8139-Socket-Hub-Lauf arbeitete Generation 4 auf CPU 3. Der bestehende
Queue-Druck-/Crashpfad führte sie vor dem Entzug von ARP-, Socket-, Lease-,
Frame- und Endpoint-Autorität auf CPU 0 zurück. Ersatzgeneration 5 self-testete
und publizierte Ready auf dem BSP, lief anschließend wieder auf CPU 3 und
bestand `NETWORK_RECOVERY_OK`, `NETWORK_PARSER_OK`, `TASK_CAPACITY_OK` und
`TEST_OK`.

## Session-Compositor unter begrenzter Aufsicht

Der Desktop startet nicht mehr als unbeschraenkter Kompatibilitaetsprozess,
sondern in einer eigenen Default-Deny-Domaene. Jede Generation aktiviert und
initialisiert Display und Surface-Broker auf CPU 0, meldet danach geordnet
Self-Test, Fortschritt und Ready und erneuert ihren Heartbeat alle 500 ms.
Crash, ungueltiger Report oder Zwei-Sekunden-Timeout deaktivieren die
Displaypublikation vor der Prozessbeendigung; der Supervisor startet hoechstens
drei Ersatzgenerationen mit einem einsekundigen Recovery-Fenster. Erst normales
Sitzungsende oder Budgeterschoepfung gibt den Userspace-Shell-Fallback frei.
Diese Scheibe erteilt noch keine AP-Affinitaet.

## VMware-Compositorstart innerhalb des Heartbeats

Der AMD-Hostlauf erreichte den Desktop erst nach 8555 ms; allein die festen
Icons benötigten 3784 ms. Weil der Compositor `SERVICE_READY` bereits vor
dieser Arbeit meldete, wertete der Supervisor die aggregierte Iconphase als
verlorenen Zwei-Sekunden-Heartbeat, startete drei Generationen neu und ging
anschließend in `COMPOSITOR_DEGRADED`; danach schlug auch die nächste Surface-
Erzeugung fehl. R6.2p hält `SERVICE_READY` jetzt bis nach dem ersten
vollständigen Frame zurück und meldet streng steigenden Fortschritt nach dem
Splash-Fallback, jedem der acht festen Splash-Streifen, jedem festen Icon und
den übrigen Startphasen. Display- und Surface-Initialisierung bleiben vor dem
Self-Test. Weil der reale VMware-Lauf bereits vor diesem Self-Test die frühere
Fünf-Sekunden-Erstaufnahme überschritt, besitzt ausschließlich der Compositor
nun eine feste aggregierte 30-Sekunden-Erstaufnahme. Zwei-Sekunden-Heartbeat,
einsekündiger Fence, Restartbudget drei und alle Autoritätsgrenzen bleiben
unverändert.

Der finale Vier-vCPU-VMware-Lauf bestand in 24 Sekunden ohne Restart,
Degraded-Zustand oder Kernel-Panic. Der Desktop benötigte 2698 ms, davon
806 ms für den Splash, 652 ms für Icons, 54 ms für Dateitypen und 43 ms für
Klänge; Explorer, `COMPOSITOR_READY`, `DESKTOP_OK` und die echte virtuelle
xHCI-Maus erschienen in der geforderten Reihenfolge.
# R7.1k CPU-Skalierungsbenchmark

`BENCHMARK.PRG` misst den Integer-Mix nun getrennt als seriellen
Single-CPU-Durchsatz und als aggregierten Multi-CPU-Durchsatz. Beide Werte
verwenden dieselbe kalibrierte, feste Arbeitsmenge pro Worker. Der Kernel gibt
Ring 3 dazu ueber den append-only Syscall 126 ausschliesslich die versionierte
Anzahl online geschalteter CPUs bekannt. Der Benchmark startet genau einen
Worker je online CPU vor einer gemeinsamen monotonen Startzeit; Image-Laden
und Prozesserzeugung liegen damit ausserhalb des Messfensters. Ein fehlender,
verspaeteter oder fehlerhafter Worker verwirft den Multi-CPU-Wert. Die Tabelle
weist zusaetzlich Multi/Single-Skalierung aus und kennzeichnet die Werte als
schedulerbeeinflusste Vergleichsmessung, nicht als Hardwarezertifizierung.
# R3.4b Notepad-Interaktionslatenz

Notepad trennt statische Basis, dynamischen Editorinhalt, Menue/Dialog-Overlay
und Hovermarkierung in vier atomare retained Surface-Layer. Scrollbar-Drag
ersetzt nur sichtbaren Text, Cursor, Scrollleisten und Status. Ein Wechsel des
Menue-Hovers uebertraegt höchstens 16 Kommandos fuer aktiven Titel,
markierten Eintrag oder sofortige Scrollbar-Rückmeldung. Damit sinken IPC- und
Repaint-Arbeit unabhaengig von SMP;
eine einzelne CPU ist der Referenzpfad.

# R3.4c native GUI-Clientflaechen

Control Gallery und Notepad verwenden unter dem Desktop ausschliesslich ihre
lokale Clientflaeche. Rahmen, Titel, Fokus, Verschieben, Groessenaenderung und
Schliessen rendert der Window Manager genau einmal. GUIDEMO zeichnet keinen
inneren Fensterrahmen mehr; sein zuvor fehlender erster Frame entstand durch
eine zentrierte Textbreite ausserhalb der 800-Pixel-Surface und wird nun vor
dem Paint auf die verbleibende Clientbreite begrenzt. Der Runtime-Nachweis
wechselt den zweiten Tab ueber echte Motion-, Press- und Release-Ereignisse und
beobachtet danach den fortlaufenden Compositor-Heartbeat. Notepad erfuellt
denselben Vertrag mit dem OS-Titel `REIST Editor` und ohne eigene Titelleiste.

# R3.4d physisches GUI-Routing und sichtbare Regionen

Der Desktop hält ein Client-Capture vom akzeptierten Button-Down bis zum
zugehörigen Button-Up und leitet Dekorations-Captures nicht an eine Surface
weiter. Ein echter emulierter xHCI-USB-Mauspfad aktiviert in GUIDEMO sowohl
den zweiten Tab als auch den Info-Menüpunkt. Der dabei gefundene Clientabbruch
war keine SMT-Ursache: Ein Tab-Hover benötigte sechs Paint-Kommandos, während
der feste Hover-Layer nur vier aufnehmen konnte. Die weiterhin statische und
fail-closed Grenze beträgt jetzt 16 Kommandos.

Notepad priorisiert während eines Scrollbar-Drags Track und Thumb in diesem
kleinen Hover-Layer. Der größere Dynamic-Layer des Editors bleibt
koaleszierbar und folgt im nächsten freien Eventloop-Umlauf beziehungsweise
spätestens nach Button-Up. Der Compositor zerlegt Dirty-Regionen außerdem in
feste sichtbare Teilrechtecke und rastert vollständig verdeckte Flächen nicht.
Erschöpft die feste Regionliste, zeichnet er den ursprünglichen Clip komplett;
Kapazitätsdruck darf daher Laufzeit, aber keine Pixelkorrektheit kosten.
