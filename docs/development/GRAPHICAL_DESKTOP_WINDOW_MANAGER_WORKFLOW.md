# Grafischer Desktop und Window-Manager: VMware-Workflow

Stand: 19. August 2026.

Dieses Dokument ist die schrittweise Arbeitsliste für den sichtbaren Ausbau
des REIST-Desktops unter VMware Workstation. Eine Checkbox wird erst nach
Codeprüfung und dem jeweils genannten Nachweis abgehakt. Der aktuelle
Haupt-Worktree bleibt die einzige Arbeitskopie, damit jede Änderung sofort in
der IDE sichtbar ist.

## Zielbild

`desktop.prg` wird zunächst zum festen Ring-3-Session-Compositor und
Window-Manager. Er besitzt als einzige Desktopkomponente globale
Fensterpositionen, Z-Order, Fokus, Pointer-Capture und die Zusammenstellung des
sichtbaren Bildes. Spätere GUI-Programme liefern ausschließlich Inhalt für
ihre lokale Clientfläche und erhalten weder den linearen Framebuffer noch
globale Fensterkoordinaten oder fremde Eingaben.

Die Gestaltung orientiert sich an klassischen, klar lesbaren Oberflächen wie
Amiga Workbench und Windows 3.1: Menüleiste, Desktopicons, überlappende Fenster,
deutlich markierte aktive Titelleiste, einfache Rahmen und vorhersehbare
Tastaturbedienung. Das Erscheinungsbild ändert nicht die Sicherheitsgrenzen.

Der sichtbare Produktname ist **REIST Workspace**. `desktop.prg` bleibt der
kompatible technische Programmname und `desktop` der Shell-Startbefehl. Der
Begriff „Windows“ wird nur für die Microsoft-Referenz oder auf Englisch für
einzelne Fenster verwendet, nicht als Name der REIST-Oberfläche.

## Quell- und Installationsstruktur

GUI-Code wird nicht mit Console- und Shellprogrammen vermischt. Die aktuelle
und künftige Struktur lautet:

| Quellpfad | Inhalt und Vertrauensgrenze |
|---|---|
| `userspace/gui/compositor/` | vertrauenswürdiger Session-Compositor und Window-Manager |
| `userspace/gui/apps/<name>/` | künftig je ein eigener GUI-Clientprozess pro Anwendung |
| `userspace/gui/include/reist/gui/` | künftig die einzige versionierte, öffentliche Client-ABI |
| `userspace/gui/lib/` | künftig begrenzte Clientbibliothek und gemeinsame Controls |
| `userspace/gui/share/` | künftig versionierte, nur lesbare Icons, Fonts und Themen |
| `userspace/programs/` | Console- und Systemprogramme ohne GUI-Clientrolle |
| `userspace/bin/` | interaktive Consoleprogramme wie Shell und Editor |

Im Zielabbild liegen direkt startbare GUI-Programme unter `/usr/gui/bin`,
Bibliotheken später unter `/usr/gui/lib` und architekturunabhängige Ressourcen
unter `/usr/gui/share`. Die Ring-3-Shell führt `/usr/gui/bin` in ihrem festen,
begrenzten Suchpfad. `/DESKTOP.PRG` und `/usr/bin/desktop.prg` bleiben feste
Kompatibilitätsaliase; neuer Code verwendet ausschließlich
`/usr/gui/bin/desktop.prg`. Verzeichnisse ohne reale Inhalte werden nicht als
Platzhalter angelegt. Die vollständige Besitzregel steht zusätzlich in
`userspace/gui/README.md`.

## Architektur-Kompatibilitätsprofil

REIST übernimmt das etablierte Wayland/xdg-shell-Modell auf Ebene der
Architektur und Semantik, aber derzeit weder dessen Wire-Format noch eine
Binärkompatibilität. Damit bleibt eine spätere Protokollbrücke möglich, ohne
jetzt Linux-, Socket-, `mmap`- oder dynamische Objektabhängigkeiten in den
Kernel zu ziehen. Neue GUI-ABI-Entwürfe müssen die folgende Zuordnung
beibehalten oder eine begründete Abweichung dokumentieren.

| Etabliertes Konzept | Verbindliche REIST-Entsprechung |
|---|---|
| `wl_compositor` / Session-Compositor | genau ein Ring-3-Prozess setzt die sichtbare Szene zusammen und vermittelt Eingaben |
| `wl_surface` | owner- und generationsgebundene, rechteckige Clientoberfläche ohne globale Position |
| `attach` + `damage_buffer` + `commit` | Buffer, begrenzten Schaden und atomare Veröffentlichung als getrennte Zustände führen |
| xdg-Toplevel-Rolle | genau eine dauerhafte Rolle pro Surface; Platzierung und Dekoration gehören dem Compositor |
| `configure` + `ack_configure` | neue Größe/Zustand erhält eine Seriennummer und wird erst nach passender Bestätigung sichtbar |
| `wl_seat`, Pointer- und Keyboard-Fokus | Geräte gruppieren, Fokusarten trennen und Events nur an den berechtigten Besitzer liefern |
| implizites Pointer-Grab | Button-Down bindet die Sequenz bis zum passenden Button-Up an genau ein Ziel |
| Buffer-Release | ein Client darf einen noch vom Compositor gelesenen Buffer nicht überschreiben |

Bewusst nicht übernommen werden globale Clientkoordinaten, direktes
Framebuffer-Sharing, unbeschränkte Objekt-/Eventmengen, wiederverwendete IDs
ohne Generation, synchrone Rundreisen im Renderpfad und implizites Vertrauen
in Clientgeometrie. Diese Abweichungen reduzieren bekannte Race-,
Ressourcenerschöpfungs- und Stale-Handle-Risiken, ohne das sichtbare
Fenstermodell zu verändern. Maßgeblich sind die offizielle
[Wayland-Architektur](https://wayland.freedesktop.org/docs/book/Architecture.html),
das [Protokoll- und Surface-Modell](https://wayland.freedesktop.org/docs/book/Protocol.html)
und die [stabile xdg-shell-Spezifikation](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/stable/xdg-shell/xdg-shell.xml).
Eine spätere
Kompatibilitätsbehauptung benötigt einen eigenen Interoperabilitätstest; die
Ähnlichkeit der Architektur allein genügt dafür nicht.

Für jede spätere öffentliche GUI-Operation wird vor dem Code ein kurzer
Standardsabgleich festgehalten: Referenzoperation, übernommene Semantik,
bewusste Abweichung, Sicherheitsgrund und Regressionstest. Eine eigene
Semantik ohne diesen Nachweis blockiert die betreffende Checkbox.

## Verbindliche Architekturregeln

- Der Compositor ist für Ausgabe und Eingabe-Routing zuständig. Dieses Modell
  folgt der Trennung aus der offiziellen
  [Wayland-Architektur](https://wayland.freedesktop.org/docs/book/Architecture.html),
  ohne das Wayland-Protokoll oder dessen Linux-Abhängigkeiten zu kopieren.
- Nur der Window-Manager kennt globale Position, Z-Order und Fokus. Ein Klick
  aktiviert genau ein Fenster und hebt nur dieses in der Reihenfolge an. Das
  entspricht den grundlegenden
  [Window-Management-Regeln](https://learn.microsoft.com/en-us/windows/win32/uxguide/win-window-mgt).
- Nach einem Button-Down hält der Window-Manager ein implizites, einzelnes
  Pointer-Capture bis zum passenden Button-Up. Dadurch bleibt ein Ziehvorgang
  auch außerhalb der Titelleiste wohldefiniert; siehe das offizielle
  [Mouse-Capture-Modell](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-mouse-input).
- Zustandsänderung und Zeichnen bleiben getrennt. Nach dem MVP werden nur
  begrenzte ungültige Bereiche neu aufgebaut, entsprechend dem etablierten
  [Update-Region-Modell](https://learn.microsoft.com/en-us/windows/win32/gdi/redrawing-in-the-update-region).
- Alle Register, Fenster, Schäden, Events und Clientressourcen besitzen feste
  Kapazitäten. Keine unbeschränkte Queue, keine Rekursion über fremde
  Fensterbäume und kein Busy-Wait.
- Ring 3 erhält kein LFB-Mapping und keine I/O-Rechte. Der Kernel validiert
  weiterhin jede Displayoperation und der Compositor ist der einzige
  Desktopprozess, der die Pixel-ABI verwenden darf.
- Eine Clientoberfläche wird später erst durch einen validierten `commit`
  sichtbar. Unvollständige oder veraltete Generationen dürfen nie teilweise
  publiziert werden.
- Bestehende Console-Programme werden nicht als Fensterprogramme ausgegeben.
  Bis zur GUI-Client-ABI starten sie bewusst im Vollbild und kehren danach zum
  Desktop zurück.
- Physische Host-Tastatur und -Maus werden niemals an VMware durchgereicht.
  Die VM verwendet ausschließlich virtuelle PS/2-Tastatur und virtuelle
  USB-HID-Maus.

## Multitasking- und Prozessgrenze

Präemptives Multitasking benötigt nicht zwingend mehrere CPU-Kerne. Der
aktuelle REIST-Kernel plant mehrere Tasks mit Zeitquantum auf einem Kern und
besitzt getrennte Prozessidentitäten und Adressräume; SMP ist eine davon
unabhängige spätere Ausbaustufe. Für die GUI genügt diese Aussage allein aber
nicht als Nachweis vollständiger Diensttrennung.

Der jetzige `desktop.prg` ist noch ein einzelner Session-Compositor-Prozess.
Seine Rasteroperationen bleiben präemptibel; nur kurze Zustandsübergänge sind
gesperrt. Offene Frames gehören immer PID plus Prozessgeneration, besitzen
eine feste Lease und werden sowohl bei normalem Task-Ende als auch bei einer
erzwungenen Terminierung bereinigt. Dadurch blockiert ein pausierter oder
beendeter Zeichner weder Scheduler noch Display dauerhaft.

Vor Controls, Dialogen und echten GUI-Anwendungen muss Stufe 3 zusätzlich
nachweisen:

- Compositor, GUI-Clients und Systemdienste laufen als getrennte Prozesse in
  getrennten Adressräumen und kommunizieren ausschließlich über validierte,
  begrenzte IPC.
- Jeder Client besitzt eigene generationsgebundene Surface-Handles und eine
  feste Eventqueue; kein Client kann Fokus, Z-Order oder fremde Buffer ändern.
- Ein blockierter, abgestürzter oder CPU-intensiver Client verhindert weder
  Eingabe noch Darstellung anderer Clients; Fairness und Cleanup werden im
  Gast getestet.
- Keine GUI-Sperre wird über einen blockierenden IPC-Aufruf, einen Wait oder
  eine lange Rasteroperation gehalten.

Bis diese Nachweise bestehen, wird weder vollständige GUI-Prozessisolation
noch SMP-Unterstützung behauptet.

## Stufe 0: belastbare Ausgangslage

- [x] VGA-Boot kann `desktop.prg` starten und VMware SVGA-II aktivieren.
- [x] Escape deaktiviert den Laufzeitgrafikmodus und stellt die VGA-Shell her.
- [x] Ring-3-Rechtecke und Pixelschrift sind geclippt und validiert.
- [x] Der Kernel zeichnet einen sichtbaren Softwarepointer über der Szene.
- [x] VMware-HID-Passthrough ist in der generierten VMX verboten.
- [x] Vier vorhandene Programme sind über den Desktop startbar.
- [x] Der bisherige Stand ist als Vollbild-Launcher, nicht als Window-Manager,
  inventarisiert.

## Stufe 1: sichtbarer Window-Manager-MVP

Sichtbares Ergebnis: Unter VMware liegen mindestens zwei klassische Fenster
übereinander. Sie können fokussiert, nach vorne geholt, an der Titelleiste
verschoben und über das Schließfeld geschlossen werden.

- [x] Feste Kapazität für vier Top-Level-Fenster definieren; kein Heap.
- [x] Fensterzustand von der Renderfunktion trennen.
- [x] Explizite Z-Order und genau ein fokussiertes Fenster verwalten.
- [x] Hit-Test von vorne nach hinten implementieren.
- [x] Implizites Pointer-Capture für Verschieben und Schließfeld umsetzen.
- [x] Fenster beim Ziehen vollständig im nutzbaren Arbeitsbereich halten.
- [x] Aktive und inaktive Titelleisten sichtbar unterscheiden.
- [x] Desktopicons für Shell, Dateien, Editor und System zeichnen.
- [x] Mausbedienung: Icon öffnen, Fenster fokussieren, ziehen und schließen.
- [x] Tastaturbedienung: Auswahl mit Tab/Pfeilen, Start mit Enter, Ende mit Esc.
- [x] Legacy-Programme weiterhin mit geprüftem Wait und Kill/Reap-Fehlerpfad im Vollbild
  starten und danach die Fensterszene vollständig wiederherstellen.
- [x] Quell- und Hosttests für Kapazität, Z-Order, Capture und Bounds ergänzen.
- [x] VMware-Paket erfolgreich bauen.
- [x] Manueller VMware-Sichtnachweis durch den Benutzer.

Abnahme vom 19. August 2026: Szene, Fenster, Dekorationen und Pointer wurden
unter VMware sichtbar bestätigt. Der Benutzer hat außerdem Mausbewegung,
Fokus/Z-Reihenfolge, Fensterziehen sowie Schließen und erneutes Öffnen
erfolgreich geprüft. Reale Hardware wurde für diesen Window-Manager-Stand noch
nicht geprüft.

Für diese Stufe ist ein vollständiger Szenenaufbau nach einer geometrischen
Änderung zulässig. Er ist ausdrücklich nur die sichere Referenz, bis die
Schadens- und Frame-Publikation aus Stufe 2 verfügbar ist.

## Stufe 2: Compositor-Kern und flüssige Frame-Publikation

Sichtbares Ergebnis: Ein gezogenes Fenster bewegt sich ohne vollständiges
Neuzeichnen des Bildschirms und ohne schrittweise sichtbaren Bildaufbau.

- [x] Typisierten, zentralen Event-Dispatch statt direkter Eingabe-Manipulation
  des Fensterzustands einführen.
- [x] Pointer- und Keyboard-Fokus als getrennte Zustände führen.
- [x] Implizites Pointer-Grab für jede Button-Down-Sequenz bis Button-Up
  beibehalten, auch außerhalb des Ursprungsfensters.
- [x] Feste Liste von höchstens acht Damage-Rechtecken definieren.
- [x] Überlappende Schäden begrenzt vereinigen; bei Überlauf kontrolliert auf
  einen Vollbildschaden zurückfallen.
- [x] Alle Desktop- und Fensterprimitive an ein explizites Clip-Rechteck binden.
- [x] Kernel-ABI für owner- und generationsgebundenes
  Frame-Begin/Commit/Cancel append-only in den vorhandenen Display-Control-
  Syscall integrieren.
- [x] Frame-Lease, Seriennummer, Draw-Reservation und Cleanup in beiden
  Scheduler-Exit-Pfaden umsetzen; keine nach Prozessende hängenbleibende
  Batch-Sperre.
- [x] Shadow-Framebuffer während eines Frames beschreiben und den validierten
  Schaden erst beim Commit an VMware SVGA-II präsentieren.
- [x] Höchstens acht VMware-Updates in einem FIFO-Batch mit genau einer
  abschließenden SVGA-Synchronisation veröffentlichen.
- [x] Softwarepointer vor dem Szenenaufbau restaurieren und anschließend als
  oberste Ebene erneut setzen.
- [x] Alte und neue Fensterposition sowie freigelegte und überdeckte Fenster
  durch vollständige Back-to-front-Komposition innerhalb der Dirty Regions
  korrekt invalidieren.
- [x] Hosttests für Dirty-Überlauf, Fokusarten, Event-Dispatch, implizites Grab,
  Stale-Serial, Timeout und Prozesscleanup ergänzen.
- [x] VMware-Paket mit Kernel und aktualisiertem `DESKTOP.PRG` bauen.
- [x] Compositor-Quellen in den eigenen GUI-Baum verschieben und die
  Zielabbilder unter `/usr/gui/bin` vereinheitlichen.
- [x] Direkten Start über den begrenzten Userspace-Shell-Pfad sowie feste
  Legacy-Pfadaliase durch Quell- und Image-Layout-Tests absichern.
- [ ] Messwerte für Vollbildaufbau und Fensterzug im seriellen Test protokollieren.
- [ ] VMware-Sichttest ohne Flackern oder stehenbleibende Fensterreste.

Die Frame-ABI ist bewusst eine Publikationsgrenze, keine globale
Compositor-Sperre. Längere Rechteck- und Textoperationen bleiben präemptibel;
konkurrierende Zeichner werden begrenzt mit Fehlerstatus abgewiesen. Das
gegenwärtige Dirty-Redraw zeichnet innerhalb jedes Clips Hintergrund, Icons
und sichtbare Fenster erneut in Z-Reihenfolge. Damit werden freigelegte Flächen
korrekt rekonstruiert, ohne den restlichen Bildschirm neu aufzubauen.

## Stufe 3: versionierte GUI-Client- und Surface-ABI

Sichtbares Ergebnis: Ein separater Ring-3-Testclient öffnet ein echtes Fenster,
zeichnet darin und erhält nur die für ihn bestimmten lokalen Eingaben.

- [ ] Vor der Implementierung einen eigenen, begrenzten Arbeitspaketvertrag
  mit den benötigten IPC-, Speicher- und Prozessdateien festlegen.
- [ ] Semantik-Matrix zu Wayland Core und stabilem xdg-shell vollständig
  ausfüllen; jede bewusste Abweichung mit Sicherheitsgrund und Test versehen.
- [ ] Feste Surface-Handles mit Generation, Besitzer-PID und Rechten definieren.
- [ ] Höchstzahlen für Clients, Fenster, Surfacebytes und ausstehende Requests
  festlegen und vor Seiteneffekten prüfen.
- [ ] Requests mindestens für `create`, `destroy`, `attach`, `damage_buffer`,
  `commit`, `configure`, `ack_configure` und `buffer_release` versionieren.
- [ ] Entscheidung zwischen mediiertem Zeichenstrom und gemeinsamem
  Surface-Speicher erst nach Prüfung der vorhandenen Memory-Capabilities treffen.
- [ ] Client kennt nur lokale Koordinaten; Platzierung und Dekoration bleiben
  vollständig beim Compositor.
- [ ] Keyboardfokus und Pointerfokus getrennt und generationsgebunden routen.
- [ ] Pointer-Button-Sequenzen erhalten ein implizites Grab bis Button-Up.
- [ ] Veraltete Handles, falsche Besitzer, ungültige Größen und unbestätigte
  Commits ohne sichtbare Teilwirkung ablehnen.
- [ ] Prozessende räumt Fenster, Surfaces, Fokus und Capture idempotent auf.
- [ ] Ein absichtlich abgestürzter Client beschädigt weder Desktop noch andere
  Fenster.

## Stufe 4: kleine GUI-Bibliothek

Sichtbares Ergebnis: GUI-Programme verwenden gemeinsame Controls statt eigener,
inkompatibler Fensterrahmen.

- [ ] Clientbibliothek für Surface-Lebenszyklus und Eventdispatch bereitstellen.
- [ ] Geclippte Zeichenprimitive, Schrift und Layoutmetriken kapseln.
- [ ] Controls für Label, Button, Textfeld, Liste, Scrollbar und Menü definieren.
- [ ] Fokusreihenfolge und Aktivierung vollständig per Tastatur ermöglichen.
- [ ] Fensterrahmen, Titel, Schließen, Minimieren und Größenänderung bleiben
  serverseitig; Clients zeichnen keine konkurrierenden Dekorationen.
- [ ] Farb- und Metrikthema zentral versionieren.

## Stufe 5: echte Desktopprogramme

- [ ] Systeminformationen als erster read-only GUI-Client.
- [ ] Dateimanager mit Laufwerksicons, Verzeichnisliste und Fehleranzeige.
- [ ] Editor mit begrenztem Textpuffer, Speichern und klarer Dirty-Anzeige.
- [ ] Terminalemulator als eigener GUI-Client statt globaler Console-Ausgabe.
- [ ] Programmstart aus Dateimanager und Desktopicons über kanonische Pfade.
- [ ] Legacy-Vollbildbrücke erst entfernen, wenn alle vier Kernprogramme eine
  getestete GUI-Variante besitzen.

## Stufe 6: vollständige klassische Desktopfunktionen

- [ ] Größenänderung mit Mindestgröße und Pointer-Capture.
- [ ] Minimieren und Wiederherstellen über eine feste Fensterleiste.
- [ ] Maximieren und exakte Rückkehr zur vorherigen Geometrie.
- [ ] Menüleiste mit Desktop-, Fenster- und Hilfeaktionen.
- [ ] Modale Dialoge bleiben an ihren Besitzer gebunden und darüber gestapelt.
- [ ] Tastaturkürzel für Fensterwechsel, Schließen und Menübedienung.
- [ ] Optionales Double-Click erst mit monotoner Zeit und fester Schwelle.
- [ ] Fensterplatzierung bleibt nach Auflösungswechsel im Arbeitsbereich.

## Stufe 7: Robustheit und Abnahme

- [ ] Fuzztests für ungültige GUI-Requests, Geometrien und Generationen.
- [ ] Kapazitätserschöpfung lässt bestehende Fenster unverändert bedienbar.
- [ ] Langsamer oder hängender Client blockiert Eingabe und Compositor nicht.
- [ ] Compositor-Neustart besitzt eine dokumentierte, begrenzte Recovery-Route.
- [ ] QEMU-Smoke für deterministische Eingabe und VMware-Sichtabnahme trennen.
- [ ] VGA-Rückkehr nach Desktopende bleibt nach jeder Stufe funktionsfähig.
- [ ] Dokumentation, ABI-Versionen, SDK und Beispielclient stimmen überein.

## Arbeitsablauf pro Checkbox-Gruppe

1. Bestehenden Mechanismus und betroffene Sicherheitsgrenze inventarisieren.
2. Einen gezielten Regressionstest ergänzen oder verschärfen.
3. Kleinste vollständige Änderung innerhalb des freigegebenen Scopes umsetzen.
4. Quelltests und den nativen VMware-Build ausführen.
5. Bei sichtbaren Änderungen die VM starten und genau die neue Interaktion
   prüfen; keine Wiederholung bereits belegter Hardwaretests.
6. Diff auf ABI-Drift, unbeschränkte Arbeit, fehlendes Cleanup und veraltete
   Dokumentation prüfen.
7. Nur belegte Checkboxen abhaken und den zusammengehörigen Stand committen.

## VMware-Sichttest

```powershell
.\scripts\build-windows.ps1 -Target vmware -Video vga
.\build\vmware\reist-os\START-VMWARE.cmd
```

Nach dem Shell-Prompt:

```text
C:\> desktop
```

Für Stufe 1 sind genau diese Handlungen abzunehmen:

1. Beide sichtbaren Startfenster abwechselnd anklicken; die aktive Titelleiste
   und Z-Order müssen wechseln.
2. Jedes Fenster an der Titelleiste bis an alle Arbeitsbereichsgrenzen ziehen.
3. Ein Fenster über das linke Schließfeld schließen und über sein Desktopicon
   erneut öffnen.
4. Eine Legacy-App mit Enter öffnen, beenden und die
   unveränderte Fensterszene wiedersehen.
5. Escape drücken und die bedienbare VGA-Shell erhalten.
