# Work Paper: gemeinsame JavaScript-Laufzeit, getrennte Host-Autorität

Stand: 8. September 2026. Ausgangspunkt: `270754bd`.
Status: Architektur und Reihenfolge festgelegt; R3.34 ist umgesetzt und mit
allen11 Prüfgruppen abgenommen. Die allgemeine Shell-Scripting-Umgebung ist noch nicht
implementiert. Dieses Papier ersetzt keine vorhandene Browser-Abnahme.

## Ziel und Sicherheitsgrenze

Eine gemeinsame QuickJS-Sprachimplementierung dient Browser-, Benutzer- und
autorisiertem System-Scripting. Gemeinsam ist der Code, nicht ein privilegierter
Prozess oder ein zwischen Vertrauensdomänen geteilter Heap. Jeder Worker besitzt
einen eigenen Ring-3-Prozess, Runtime, GC, private Seiten und Lebenszyklus.
Der Host bzw. ein gesonderter Broker delegiert ausschließlich explizite Rechte.
Auch ein administratives Skript erhält keine unbeschränkte native OS-API.

```text
gemeinsame QuickJS-Bibliothek: Sprache, Werte, Interpreter, GC
    | jeweils eigener eingeschränkter Ring-3-Worker
    +-- Browser-Host: DOM / Ereignisse / kontrollierte Ressourcen
    +-- Benutzer-Host: ausdrücklich delegierte Benutzeroperationen
    +-- System-Host: ausdrücklich autorisierte Broker-Operationen

Worker -- versioniertes, begrenztes IPC --> zuständiger Host/Broker
       -- Kernel prüft Prozessprofil und delegierte Endpoint-Rechte
```

Das Kernelprofil ist ein Mechanismus zum Rechteentzug. Dateipfade, URL-Policy,
DOM, Skriptparser, Konfiguration und administrative Entscheidungen bleiben
in Ring 3. Fehlende Sprach-Bindings allein sind keine OS-Sandbox. Ein nativer
Enginefehler darf nicht über die verbleibenden Syscalls ausbrechen.
Dieses Vorhaben beseitigt nicht automatisch die übrige monolithische Altlast
oder sämtliche Ambient-Rechte anderer REIST-Anwendungen.

Sprachreferenz ist [ECMA-262](https://tc39.es/ecma262/), eingebettete Laufzeit
die bereits vendorte [QuickJS-C-API](https://bellard.org/quickjs/quickjs.html#QuickJS-C-API).
Keine neue Engine, kein JIT, keine ungeprüften Bytecodeimporte. REIST-Host-APIs
werden als solche versioniert, nicht als Node.js-, Python-, POSIX- oder
vollständig Web-kompatible APIs ausgegeben. Fehlernummern verwenden den
vorhandenen errno-Teilumfang. Bestehende Syscallnummern bleiben unverändert.

## Vorhandenes wiederverwenden

- `userspace/quickjs/`: explizite Sprach-Intrinsics, eigener Allocator,
  Speicher-/Stack-/Quelltext-/Ergebnis-/Jobbudgets und Deadline-Hook.
- `JSWORK.PRG`: persistente isolierte Dokument-Runtime; bisher allerdings
  natives Compatibility-Syscallprofil. Genau diese Lücke schließt R3.34.
- `JsSession` und `js_protocol`: feste Nachrichten, gerichtete delegierte
  Endpoints, PID und Generation, eine laufende Anfrage, absolute Deadlines,
  begrenztes Abbrechen/Reap und Wiederanlauf mit leerer Runtime.
- `JSIPCTST.PRG`: echte persistente Werte, große Transfers, GC, nativer Fault,
  Hang, stale reply, Cancel, Elternverlust und frischer Worker; normal aus
  `/bin/shell.prg` über `/usr/bin` in beiden Image-Layouts erreichbar.
- Private Prozessheaps, IPC-Rechte und bestehende Reap-/Recoverymechanismen.

## Reihenfolge und Definition of Done

| Paket | Zusammenhängender Schritt | Erforderliche Abnahme |
| --- | --- | --- |
| R3.34 | Irreversible native Script-Prozessdomäne; JSWORK vor Skriptannahme einschränken | Native Negativtests, Heapreserve, echte eingeschränkte JS-Lebenszyklen, Browserregression |
| JS2 | Allgemeiner Benutzer-Runner `JS.PRG` und wiederverwendbarer Sprach-/Host-Transport außerhalb des Browserpakets | Shell-Auflösung in beiden Images; Datei/argv lädt nur der Host; begrenztes stdout/stderr über IPC; Source/Eval/Fehler/Timeout/Cancel/Exit und parallele getrennte Realms im Gast |
| JS3 | Explizite, versionierte Capability-Objekte und dateigebundener Benutzer-Broker | Rechteprüfung auch bei gefälschtem IPC, Pfad-/Handle-/Generations-/Quota-Negativfälle; Lesen/Schreiben/Close, Crash und sichere Persistenz |
| JS4 | Autorisierter Prozessstart und benannte Service-/Konfigurationsoperationen | Getrennte Verträge für Startautorität und persistente administrative Transaktionen; argv/env/Kindrechte begrenzt; Abbruch und unklarer Commit-Ausgang sicher |
| JS5 | Optionales Netzwerk und GUI-Automation als getrennte delegierte Hosts | Ziel-/Origin-/Redirectprüfung bzw. explizites GUI-Ziel; keine globale Eingabeautorität als Nebeneffekt; Host-Crash, Revoke und stale replies |
| JS6 | Optionale Installer-/Service-/Startskripte, Dokumentation und Gesamtintegration | Feste Abhängigkeiten/Restartbudgets, Safe-State ohne JS, keine doppelte Ausführung von Seiteneffekten, vollständige Milestone-Abnahme |

Jede Tabellenzeile ist eine fachliche Etappe. Vor ihrer Aktivierung werden
konkrete Dateien und ausführbare Gates eingefroren. Unabhängige persistente
Formate, Autoritäts- oder Hardwaregrenzen bekommen eigene Pakete; kompatible
Feldfälle oder ABI-Varianten werden nicht künstlich zerlegt. Pro Lauf wird
genau ein aktives Paket umgesetzt. R3.6b bleibt mit unveränderter VMware-
Zurückstellung erhalten und wird nicht stillschweigend als erledigt markiert.

### Verbindliche Regeln für spätere Hosts

- Rechte sind opake, generationengebundene Hostobjekte, keine JS-Strings,
  erratbaren IDs oder globalen `isSystemScript`-Flags. IPC-Nachrichten gelten
  immer als feindlich, auch wenn JS-seitige Wrapper vorher validiert haben.
- Importe laden Code, erteilen aber niemals Rechte. Keine automatische
  Autorität durch Dateiendung, Pfad, Umgebungsvariablen oder Modulnamen.
- Dateirechte binden stabile Wurzel-/Dateiobjekte: `..`, Symlinks, Rename,
  Reopen und PID-/Handle-Reuse dürfen die Grenze nicht verschieben.
- Prozessrechte begrenzen ausführbares Objekt, Argumente, Umgebung und
  Kindrechte. Netzwerkrechte prüfen auch Redirect-Ziele erneut. GUI-Rechte
  sind separat und standardmäßig nicht vorhanden.
- Jeder Host definiert feste Nachrichten-/Handle-/Cache-/Job-/Zeitbudgets.
  RAM darf bedarfsgerecht genutzt werden; keine unbeschränkten Caches und
  keine Reduzierung der bestehenden OS-Recoveryreserve. Größere Sprachheaps
  werden später als explizite, gemessene Profile zugelassen.
- GC verwaltet Sprachobjekte, nicht die Zuverlässigkeit von OS-Cleanup.
  `close`, Revoke und Reap sind explizit, idempotent und generationengebunden;
  Finalizer sind höchstens ein zusätzlicher Freigabehinweis.
- Recovery: detect -> isolate -> fence/revoke -> reap -> recreate -> self-test
  -> reintegrate. Kein Replay bereits möglicherweise ausgeführter Änderungen;
  administrative Transaktionen benötigen bestätigten Commit-Status bzw.
  persistente Idempotenz. Bei Budgetende definierter Degraded/Safe-State.
- Kernel, essentielle Startsequenz und letzte Recoveryinstanz funktionieren
  ohne JS. System-Scripting bleibt ein abschaltbarer Ring-3-Client.

## R3.34: eingefrorener Implementierungsvertrag

Eine einzige Fehler-/Autoritätsgrenze: Übergang des eigenen lebenden Prozesses
von Compatibility in ein unveränderlich engeres Scriptprofil, Integration in
JSWORK sowie native und reale Lifecycle-Abnahme. Keine allgemeine Shell-Engine,
Datei-/Netzwerk-/GUI-Bindings oder Brokerimplementierung in diesem Paket.

Append-only Syscall128 `PROCESS_RESTRICT`, 16-Byte Request mit Version1,
Strukturgröße, Profil1 SCRIPT und reserviertem Nullwort. Ungültige Version,
Größe, reservierte Bits oder unbekanntes Profil: EINVAL; ungültiger Userpointer:
EFAULT; unzulässiger Ausgangszustand/Rechtezuwachs: EACCES; bereits über dem
Script-Heaplimit: ENOMEM. Jede Ablehnung lässt Profil und Ressourcen unverändert.
Kein Ziel-PID-Parameter, keine Rückkehr zu Compatibility. Erneute erfolgreiche
Scriptbeschränkung ist idempotent. Andere Serviceprofile erhalten den neuen
Syscall nicht. Interne Profilversion2 hat fünf Bitmapwörter für129 Nummern;
alle bisherigen Nummern und Service-Bits0..127 bleiben erhalten.

Script erhält nur privaten MALLOC/FREE/REALLOC, EXIT, GETPID, PROCESS_INFO
(nur selbst), YIELD, SLEEP_MS, MONOTONIC_MS, gerichtetes SEND/RECEIVE_TIMEOUT,
IPC_RELEASE, PROCESS_IDENTITY (selbst oder tatsächlicher Elternprozess mit
passender Generation) und erneuten PROCESS_RESTRICT. Keine Terminal-/VFS-/
Netzwerk-/Geräte-/GUI-/Spawn-/Kill-/Service-Connect-/Create-/Delegate-Syscalls.
IPC prüft weiterhin seine eigenen Rechte und Generationen. Das Profil erzeugt
keine Endpoints; vorab explizit delegierte Rechte werden nicht erweitert.
Bestehende Dateideskriptoren werden unbenutzbar und normal beim Reap aufgeräumt.

JSWORK aktiviert die Einschränkung vor jeder Engine-/Skriptausführung und
beendet sich bei fehlender Kernelunterstützung. Vertrauenswürdiger nativer
Loader/Bootstrap liegt vor diesem Übergang; kein Anspruch auf Isolation
beliebigen nativen Startcodes. Der private Workerheap bleibt bei höchstens
64MiB wie bisher, Engine32MiB, Stack16KiB, Source1MiB, Result64KiB,
Jobs1024, Request5s und vorhandene Restartbudgets. Das OS-weite adaptive
RAM-Limit und die physische Recoveryreserve dürfen nicht erhöht/umgangen
werden. Kein neuer Laufzeit-Heap oder komplexer Kernelparser.

JSIPCTST erhält einen expliziten nativen Diagnosemodus im vorhandenen Worker:
alle verbotenen Syscallnummern müssen vor Argumenten/Seiteneffekten EACCES
liefern; falsche IPC-Richtung/Handles, fremde Prozessidentität, Profilaufweitung
und Heapüberschreitung scheitern. Erfolgreiche Diagnose wird über vorhandenes
validiertes IPC/HELLO und eine JS-Auswertung bestätigt, nicht über eine neue
Terminalberechtigung. Fault/Hang/Stale-Fixtures dürfen keine direkten Worker-
Terminalmarker mehr liefern; der neue eingeschränkte Validator verlangt deren
Abwesenheit UND weiter echte Faultdaten, Exitstatus, geordnete Reaps und
frische Generationen. Legacy-Validator bleibt nur für alte Belege verfügbar.
Der native Hang-Zweig bestätigt seinen Eintritt als reguläres EVAL-Ergebnis
über den bereits delegierten Kanal und hängt danach. Erst nach validierter
Bestätigung sendet der Elternprozess die nächste Anfrage; diese muss mit
ETIMEDOUT und Reap143 enden. Das ersetzt den alten direkten Terminalmarker,
ohne dem Worker Konsolenrechte oder dem privaten Protokoll neue Opcodes zu geben.

### Scope und eingefrorene Gates

Die verbindliche vollständige Dateiliste und Befehle stehen beim aktiven
`R3.34-script-process-domain` in `automation/reist-s03b.toml`. Vier gezielte
Hostgruppen, zwei Referenzpakete und fünf Artefakt-/Gastgruppen, insgesamt11.
Die JS-Link-Hostgruppe läuft nach dem SDK-/Referenzbuild, damit nicht gegen
ein altes SDK geprüft wird. VMware und QEMU werden nacheinander gebaut;
VMware-Kernel vor dem QEMU-Build unter dem Paket-Belegordner gesichert.

Host: öffentliche ABI/Projektionen, echte Profil-/Restrict-/Dispatch-/Sicht-
Pfade O0/O2 mit fehlerhaften Backends, vorhandene private Speicherresilienz,
echter JS-Transport und Ziel-Link. Gast: zweimaliger eingeschränkter nativer
und JSIPC-Zyklus einschließlich Fault/Hang/Elternverlust/Recovery; bestehende
externe Browser-Skripte,2560x1440 Resize/Fault/Recovery und Memory-Resilience.
Quellmuster ergänzen diese Tests, ersetzen sie nicht.

Artefaktprüfung liest beide Images unabhängig aus: Kernel müssen den jeweils
neu gebauten Bytes entsprechen; JSWORK/JSIPCTST und alle aktuellen Programme
korrekt verpackt. BENCHMARK, MATHTEST, TEXTTEST, CURL und JSTEST bleiben
byteidentisch zu270754bd. Frühere R3.33-Belege bleiben unberührt. Keine neue
VMware-Performancezusage ohne Messung; CPU-local/Scheduler/Framebuffer-
Hotpaths werden nicht umgebaut. Keine synchronen JS-RPCs in GUI-Eingabepfaden.

Logs und fehlgeschlagene Versuche bleiben unter
`build/codex-agent/r334-script-domain/`. Erst nach allen Gates, direkter
Diff-/Scope-/Queueprüfung und sauberem `git diff --check` darf lokal committed
werden. Kein Push, keine Agenten, keine sichtbaren VMs/Windows-Fehlerdialoge.
Bei Bedarf einer nicht freigegebenen Quelldatei oder fremden Änderungen
stoppt dieses Paket mit konkretem Grund; keine stille Scopeerweiterung.

## Abnahmeprotokoll

Vertragscheckpoint `3a0a3148`. R3.34 ist nach allen11 eingefrorenen Gruppen
abgenommen. Hostprüfungen, beide Referenzbuilds, tatsächliche Image-Inhalte,
eingeschränkte native/JS-Gäste, externer Browser-JavaScript-Lauf,2560x1440-
Browser-Recovery und Memory-Resilience sind erfolgreich. Einzelzeiten und
abschließende Belegnamen stehen in [CURRENT_WORK.md](CURRENT_WORK.md).

Der eingeschränkte Gast bestätigt zweimal alle nativen Verbote, Heap und IPC,
persistente Werte/GC, echte PF142, bestätigten Hang/Timeout/Reap143, Stale/Cancel,
frische Generationen und Elternverlust. Beide Images enthalten90 passende
Programmpayloads und die jeweils neu gebauten Kernel. Alle fünf geschützten
Programmhashes bleiben unverändert.100 archivierte Referenzdateien einschließlich
separatem Speicher-Beweisimage; frühere Abnahmen bleiben unangetastet.

Erhaltene Korrekturbelege unter `build/codex-agent/r334-script-domain/`:

- `red-worker-corrected.log`: vor der Änderung fehlende native Einschränkung;
  vorheriger Test-Syntaxfehler bleibt in `red-worker.log` sichtbar.
- `domain.log`: Namenskollision mit Windows-CRT-Konstante SYS_OPEN im Host-
  Fixture; explizit entkoppelt, danach echte Kernelpfade O0/O2 erfolgreich.
- `artifact-gate.log`: zusätzlicher Out-of-line-SDK-Wrapper verschob auch
  unbeteiligte Programme. Inline-Wrapper beseitigt dies; die fünf eingefrorenen
  Hashes wurden nicht geändert und stimmen wieder exakt.
- `artifact-gate-sdk-corrected.log`: Imageprüfer forderte zusätzlich globale
  Makefile-Parität auch für das fremde Windows-GUI-Fixture SURFACEDEMO.
  Auf die vertraglich benötigte Shell-Parität von JSWORK/JSIPCTST eingegrenzt;
  alle tatsächlich verpackten Programme bleiben in beiden Images bytegeprüft.
  Keine Änderung/Abnahme der fremden Surface-Demo-Integration.
- `js-runtime.log`: erster echter Gast scheitert im neuen Diagnosemodus.
  Dessen Erwartung für IPC_RELEASE(0) widersprach dem bestehenden EINVAL-
  Vertrag. Erwartung korrigiert und nichtnull Fake-Handle auf EBADF ergänzt;
  keine Lockerung des Kernel-/IPC-Vertrags.

Der neue Hang-Nachweis nutzt bestätigtes IPC statt unzulässiger direkter
Worker-Konsolenausgabe. Betroffene Builds/Prüfungen wurden nach diesen gezielten
Korrekturen wiederholt; alte Belege wurden weder überschrieben noch als
abschließende Abnahme verwendet. Keine allgemeine System-Scripting- oder neue
VMware-Laufzeit-/Performancezusage aus diesen Teilbelegen.

Nächste Etappe JS2 ist als R3.35 auf ef9fb2de eingefroren:
[OS_JAVASCRIPT_RUNNER_CONTRACT.md](../architecture/OS_JAVASCRIPT_RUNNER_CONTRACT.md).
Gemeinsamer Dienst außerhalb des Browsers, JS.PRG, native speichergebundene
Konsole/Argumente, validiertes IPC und getrennte Realms; keine OS-Brokerrechte.
Implementierung und zehn Gates folgen erst nach dem Vertragscheckpoint.
R3.6b behält seine ausdrückliche VMware-Zurückstellung.
