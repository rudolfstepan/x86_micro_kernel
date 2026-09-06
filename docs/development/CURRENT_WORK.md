# REIST OS – aktueller Arbeitsstand

Stand: 6. September 2026

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
