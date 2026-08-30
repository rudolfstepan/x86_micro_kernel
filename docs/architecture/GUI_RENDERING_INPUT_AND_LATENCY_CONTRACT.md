# GUI-Rendering-, Eingabe- und Latenzvertrag

Stand: 30. August 2026.

## Zweck und Geltungsbereich

Dieses Dokument definiert den verbindlichen Architekturvertrag für die
interaktive REIST-Desktop-GUI. Er gilt für Window Manager, Session-Compositor,
`libreistgui`, Surface-Protokoll und alle grafischen Anwendungen. Ziel ist ein
deterministischer, portierbarer und auf Ein- wie Mehrprozessorsystemen
reaktionsschneller Desktop.

Der Vertrag ist eine technische Grundlage für spätere Assurance- und
Zertifizierungsarbeit. Er ist weder eine Zertifizierung noch die Behauptung,
dass REIST bereits eine bestimmte Schutzklasse, einen Evaluation Assurance
Level oder eine funktionale Sicherheitsstufe erfüllt. Zielnorm, Target of
Evaluation, Bedrohungsmodell, Safety Case und unabhängige Bewertung müssen
separat festgelegt werden.

Die Begriffe **MUSS**, **DARF NICHT**, **SOLL** und **KANN** bezeichnen in
diesem Dokument normative Anforderungen.

## Referenzprinzipien

REIST übernimmt folgende etablierte Desktopprinzipien, ohne Win32-, Wayland-,
Qt- oder GTK-Kompatibilität zu behaupten:

- Globale Fenstergeometrie, Z-Order, Fokus, Non-Client-Area und
  Pointer-Capture gehören dem Betriebssystem beziehungsweise Compositor.
- Eine Anwendung besitzt ausschließlich ihre lokale Clientfläche und erhält
  lokale Eingabekoordinaten.
- Button-Down etabliert ein implizites Capture; Motion und Button-Up bleiben
  bis Release, Abbruch oder Zerstörung beim selben Ziel.
- Eine Zustandsänderung invalidiert nur die betroffene Region. Zeichnen erfolgt
  gegen eine geclippte Update-Region und wird atomar abgeschlossen.
- Vollständig verdeckte Bereiche werden nicht gerastert. Teilweise verdeckte
  Bereiche werden in sichtbare, disjunkte Rechtecke zerlegt.
- Ein interaktiver Feedbackpfad darf einen umfangreichen Inhalts-Redraw nicht
  abwarten müssen.

Diese Aufteilung entspricht unter anderem dem Win32-Modell für Mouse Capture,
Update-Regionen und OS-seitig gezeichnete Non-Client-Areas. Das REIST-
Surface-Protokoll verwendet ähnliche Zustandsgrenzen wie `wl_surface` und
`xdg_toplevel`, bleibt aber ein eigenes, fest begrenztes und versioniertes
Protokoll.

## Autoritäts- und Prozessgrenzen

```text
HID-Treiber
  -> lokales Pointer-Ereignis
     -> Session-Compositor: Hit-Test, Fokus, Capture, Z-Order
        -> generation-geprüfte Surface-Queue
           -> libreistgui-Control-Zustandsautomat
              -> lokale Damage-Region und Paint-Layer
                 -> atomarer Surface-Commit
                    -> Occlusion-Culling und Frame-Transaktion
                       -> validierte Display-ABI
```

Der Kernel vermittelt nur die begrenzten Mechanismen für Scheduling, IPC,
Prozessidentität und Displayressourcen. Window Manager, Control-Logik,
Compositor und Anwendungen bleiben getrennte Ring-3-Komponenten. Kein
GUI-Client erhält globale Fensterkoordinaten, rohe Framebufferadressen,
Geräteautorität oder Zugriff auf eine fremde Surface.

Jede Surface-Operation MUSS Besitzer-PID, Prozessgeneration, Handle-Generation,
Protokollversion, Strukturgröße und lokale Grenzen vor einer sichtbaren
Zustandsänderung prüfen. Zerstörung und Revocation sind idempotent und
generation-gebunden.

## Verbindlicher Eingabevertrag

### Hit-Test und Fokus

Der Compositor führt den globalen Hit-Test genau einmal gegen die oberste
sichtbare Surface aus. Fensterdekorationen haben Vorrang vor der Clientfläche:
Close, Resize, Titel/Move und danach Client. Nur ein Client-Hit DARF ein
Client-Capture erzeugen.

Ein Pointer-Ereignis für eine Client-Surface enthält ausschließlich lokale,
auf die aktuelle Clientgröße begrenzte Koordinaten. Keyboardereignisse gehen
an genau eine fokussierte Surface. Ein verdecktes oder nicht fokussiertes
Fenster DARF kein neues Capture erhalten.

### Capture-Zustandsautomat

```text
NONE --Button-Down im Client--> CLIENT_CAPTURE
CLIENT_CAPTURE --Motion--> CLIENT_CAPTURE
CLIENT_CAPTURE --Button-Up--> NONE
CLIENT_CAPTURE --Cancel/Destroy/Revoke--> NONE + CANCEL
```

Button-Up gehört immer dem Besitzer des zugehörigen Button-Down. Es darf nicht
an das aktuelle Hover-Ziel umgeleitet werden. Ein Capture auf Titel, Resize
oder Close DARF niemals als Client-Button-Up in die Surface-Queue gelangen.
Ein Capture-Verlust MUSS den betroffenen Control-Zustand abbrechen und das
notwendige visuelle Feedback invalidieren.

### Queue- und Koaleszierungsregeln

Button-, Keyboard-, Cancel- und Configure-Ereignisse sind nicht ersetzbar.
Aufeinanderfolgende reine Motion-Ereignisse KÖNNEN auf die jüngste Position
koalesziert werden. Ist eine Queue voll, DARF ein altes reines Motion-Ereignis
verworfen werden, um eine Button-Kante zu erhalten. Kann eine nicht ersetzbare
Kante nicht gespeichert werden, MUSS der Broker den Fehler diagnostizieren und
das Capture kontrolliert abbrechen und die betroffene Surface-Generation
fencen; ein stiller Zustandsverlust ist verboten.

Alle Queuegrößen, Drain-Runden und Wartezeiten sind feste Konstanten. Keine
GUI-Komponente darf auf Input, Paint oder Present unbeschränkt warten.

## Verbindlicher Renderingvertrag

### Invalidation statt unmittelbarer Vollbilder

Control-Zustandsautomaten liefern lokale Damage-Rechtecke. Eine Anwendung
zeichnet nicht als Seiteneffekt des Hit-Tests, sondern markiert den
betroffenen Layer als ungültig. Mehrere Schäden dürfen bis zur festen
Damage-Kapazität vereinigt werden. Ein Kapazitätsüberlauf fordert einen
vollständigen Redraw der betroffenen Surface, niemals ein unvollständiges
Bild.

REIST verwendet vier atomar ersetzbare retained Layer:

| Reihenfolge | Layer | Inhalt | Typische Änderung |
|---:|---|---|---|
| 1 | Base | statische Fläche und Control-Geometrie | Layout/Resize |
| 2 | Dynamic | Dokument, Listeninhalt, Werte | Editieren/Scrollen |
| 3 | Overlay | Menüs, App-Dialoge, Popups | Open/Close |
| 4 | Hover | Hot-/Pressed-Feedback und Drag-Thumb | Pointer-Motion |

Ein Commit ersetzt genau einen vollständigen Layer. Ein teilweise empfangener
Paint darf niemals sichtbar werden. Die kompatible primitiveweise Paint-API
bleibt erhalten; der bevorzugte Zielpfad sammelt einen Frame lokal und
veröffentlicht ihn mit genau einem versionierten, generation-gebundenen
Command- oder Pixelbuffer-Commit. Eine neue Batch-ABI MUSS append-only sein und
vor Veröffentlichung Anzahl, Bytegröße, Textlängen, Rechtecke und Layer
vollständig validieren.

Die feste Hover-Kapazität beträgt 16 Paint-Kommandos. Diese Grenze deckt einen
vollständigen Bevel-Tab mit Text, einen Menü-Hot-State oder Track und Thumb
einer aktiven Scrollbar ab. Ein siebzehntes Kommando wird vor dem Commit
abgelehnt; die Kapazität wächst weder zur Laufzeit noch aufgrund der CPU-Anzahl.

### Occlusion-Culling

Für jede Dirty-Region berechnet der Compositor die tatsächlich sichtbaren
Teilrechtecke. Beginnend mit der Dirty-Region werden die opaken Bounds aller
höheren Fenster sowie opake System-UI abgezogen. Desktop-Hintergrund und Icons
werden gegen sämtliche opaken Fenster ausgeschnitten. Danach werden Fenster
in Z-Reihenfolge nur in ihren verbleibenden Teilregionen gerastert.

Die Visible-Region-Liste hat eine feste Kapazität und verwendet keine
Heap-Allokation. Erschöpft die Rechteckzerlegung diese Kapazität, MUSS der
Compositor für die ursprüngliche Dirty-Region auf korrektes vollständiges
Zeichnen zurückfallen. Kapazitätsdruck darf zusätzliche Arbeit verursachen,
aber niemals fehlende oder falsch geordnete Pixel.

Schatten gehören zu den visuellen Bounds eines opaken Fensters. Transparente
Layer dürfen erst dann als Occluder verwendet werden, wenn Alpha- und
Deckungssemantik versioniert und getestet sind.

### Atomare Präsentation

Alle Dirty-Regionen eines Desktopumlaufs werden innerhalb einer begrenzten
Frame-Transaktion gezeichnet. Der Hardwarepointer wird vor dem Frame verborgen
und danach an der aktuellen Position wiederhergestellt. Bei Displayfehlern
wird die Transaktion abgebrochen beziehungsweise auf den validierten
Softwarepfad zurückgeführt; ein halber Frame gilt nicht als Erfolg.

Während eines interaktiven Resize bleibt nur die zuletzt bestätigte
Surface-Größe autoritativ. Nach dem finalen Configure-ACK invalidiert der
Compositor die vollständige neue Clientfläche. Überlappende Resize-Kopien
verwenden den atomaren Shadow-Blit; VMware-SVGA-RECT_COPY bleibt auf
gleich große Fensterverschiebungen begrenzt, weil ein asynchroner Device-Copy
keine Grundlage für die Korrektheit einer gleichzeitig geänderten Geometrie
sein darf.

## Sofortiger Scrollbar-Feedbackpfad

Beim akzeptierten Button-Down auf einen Scrollbar-Thumb beginnt ein lokales
Control-Capture. Jede zugestellte Drag-Position führt in folgender Reihenfolge
zu Arbeit:

1. Scrollwert begrenzen und neuen Thumb berechnen.
2. Nur Track und Thumb im Hover-Layer invalidieren.
3. Das aktuelle Event-Batch beenden und den kleinen Layer sofort committen.
4. Den größeren Editor-Viewport als Dynamic-Damage markieren.
5. Dokumentinhalt im nächsten freien Eventloop-Umlauf aktualisieren; neuere
   Drag-Positionen dürfen ältere, noch nicht präsentierte Inhaltsstände
   ersetzen.
6. Bei Button-Up den endgültigen Dynamic-Zustand committen und den
   Drag-Hover-Layer leeren.

Damit bleibt der Thumb direkt an der Maus. Der Editorinhalt darf während einer
schnellen Bewegung kontrolliert hinterherlaufen, muss aber spätestens nach
Release den endgültigen Scrollwert zeigen. Button-Up und Endzustand dürfen
nicht koalesziert oder verworfen werden.

## Latenz- und Lastziele

Die folgenden Werte sind Abnahmekriterien für die Referenz-VM und ausdrücklich
keine bereits bewiesenen Worst-Case-Garantien:

| Messgröße | Ziel |
|---|---:|
| Pointer-Kante bis sichtbarer Hover/Pressed-Zustand | p99 <= 16,7 ms |
| Scrollbar-Motion bis sichtbarer Thumb | p99 <= 16,7 ms |
| Button-Up bis endgültiger Control-Zustand | p99 <= 33,4 ms |
| ruhender Desktop ohne Ereignis | kein Busy-Wait, begrenzter Sleep/Block |
| Motion-Verarbeitung pro Desktopumlauf | fest begrenzt |
| Paint-/Input-Queues und Regionlisten | feste Kapazität |

Die Messung MUSS getrennt für einen virtuellen Prozessor, mehrere virtuelle
Prozessoren und reale Zielhardware erfolgen. Erfasst werden mindestens Median,
p95, p99, Maximum, verlorene nicht ersetzbare Ereignisse, Queue-Hochwasser,
Paint-Retries, Region-Fallbacks und verpasste Present-Deadlines. Durchschnitts-
FPS allein ist kein ausreichender Interaktivitätsnachweis.

## Überlastung und Fehlerverhalten

- Reine Motion darf begrenzt koalesziert werden; Button-Kanten nicht.
- Ein temporär voller Paintpfad behält Damage und wiederholt mit einer festen
  Anzahl kurzer Versuche. Die App beendet sich nicht nach dem ersten
  transienten Timeout.
- Nach erschöpftem Retry-Budget wird der Fehler mit Surface, Generation,
  Layer, Kommandoindex und Status diagnostiziert. Unbestätigte Daten werden
  nicht sichtbar gemacht.
- Ein defekter GUI-Client wird isoliert und seine Ressourcen werden widerrufen;
  andere Fenster und der Desktop bleiben bedienbar.
- Ein hängender Compositor unterliegt seinem Supervisor- und
  Recovery-Vertrag. Ein App-Restart darf Capture, alte Events oder
  Bufferautorität der vorherigen Generation nicht übernehmen.

## Portierbarkeit von Anwendungen

Anwendungen verwenden öffentliche Control- und Surface-APIs. Sie implementieren
keine eigenen Top-Level-Rahmen, Titelleisten, globalen Hit-Tests oder
Fensterverschiebung. Theme, Standardschrift, Fokusdarstellung, Scrollbar-
Geometrie, Menüs und Standarddialoge werden durch OS-Bibliotheken und den
Compositor bereitgestellt.

Ein Portierungsadapter darf fremde Eventloops und Widgetmodelle auf diesen
Vertrag abbilden. Direkte Win32-, Wayland-, Qt- oder GTK-Kompatibilität darf
erst behauptet werden, wenn Quell- beziehungsweise Laufzeitverhalten durch
eigene Tests nachgewiesen ist.

## Verifikations- und Assurance-Matrix

| Invariante | Hosttest | Quelltest | Gastnachweis |
|---|---:|---:|---:|
| Button-Up bleibt beim Capture-Besitzer | erforderlich | erforderlich | echte USB-Maus |
| Dekorations-Capture leckt nicht zum Client | erforderlich | erforderlich | echte USB-Maus |
| GUIDEMO Tabs, Controls und Menüs reagieren | Control-Modelle | App-Bindung | Klicksequenz |
| Scrollbar-Thumb wird vor Editorinhalt präsentiert | Zustandsfolge | Layerpriorität | Drag-Sequenz |
| vollständig verdecktes Fenster wird übersprungen | Regionzerlegung | feste Bounds | überlappende Fenster |
| Regionüberlauf bleibt pixelkorrekt | Kapazitätsfault | Fallback vorhanden | Referenzbild |
| Paint-Timeout ist transient begrenzt | Fehlerfolge | Retrylimit | Fault Injection |
| stale Generation erhält keine Events/Buffer | erforderlich | erforderlich | Restart-Probe |
| Ein-Kern-Latenzziele | n/a | Bounds | QEMU/VMware 1 vCPU |
| Mehr-Kern-Latenzziele | n/a | Affinitätsvertrag | QEMU/VMware SMP |

Ein Source-Pattern-Test ergänzt nur die Prüfung; er ersetzt keinen
Verhaltenstest. Ein Runtime-Test, der Events direkt in eine Surface-Queue
schreibt, beweist nur die Broker-/Client-Seite. Der End-to-End-Nachweis MUSS
eine emulierte oder reale HID-Eingabe über Treiber, Window Manager, Capture,
Surface-Queue, Control-Zustand und Present führen.

## Zertifizierungsvorbereitung

Für eine spätere Common-Criteria-Evaluation müssen mindestens Target of
Evaluation, Security Target, Schutzprofile beziehungsweise
Konformitätsanspruch, Bedrohungen, Annahmen, funktionale Anforderungen,
Assurance-Anforderungen, Konfigurationsmanagement und reproduzierbare
Testevidenz festgelegt werden. ISO/IEC 15408 stellt hierfür das allgemeine
Evaluationsmodell bereit; ISO/IEC 18045 beschreibt die Evaluationsmethodik.

Eine Safety-Zertifizierung benötigt zusätzlich eine konkrete Domäne und Norm.
GUI-Flüssigkeit allein ist keine Safety-Eigenschaft. Für sicherheitsrelevante
Bedienfunktionen wären unter anderem Gefährdungsanalyse, sichere Zustände,
zeitliche Anforderungen, Unabhängigkeit, Diagnoseabdeckung und nachweisbare
Worst-Case-Grenzen erforderlich.

Bis diese Artefakte und unabhängigen Bewertungen vorliegen, verwendet REIST
ausschließlich die Formulierung **zertifizierungsvorbereitende Architektur und
Evidenz**.

## Implementierungsstatus

| Bestandteil | Status am 30. August 2026 |
|---|---|
| serverseitige Fensterdekorationen | vorhanden |
| generation-geprüfte Surface-Events | vorhanden |
| implizites Client-Capture | vorhanden; QEMU-xHCI Tab-/Menüpfad bestanden |
| Base/Dynamic/Overlay/Hover-Layer | vorhanden |
| priorisierter Notepad-Scrollbar-Thumb | umgesetzt; Reihenfolgetest bestanden, Latenzmessung offen |
| kleine GUIDEMO-Hover-Layer | umgesetzt; physischer Tab-/Menütest bestanden |
| begrenzte transiente GUIDEMO-Paint-Retries | umgesetzt; Fault-Injection-Abnahme offen |
| compositorseitige sichtbare Regionen | umgesetzt, Verhaltensabnahme offen |
| ein atomarer Batch-Commit pro Layer | Zielvertrag; Legacy-Paint kompatibel |
| Ein-/Mehrkern-Latenzevidenz | offen |
| formale Zertifizierungsevidenz | offen |

## Referenzen

- Microsoft Learn: `SetCapture` und Mouse Capture,
  <https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture>
- Microsoft Learn: `WM_CAPTURECHANGED`,
  <https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-capturechanged>
- Microsoft Learn: `WM_PAINT` und geclippte Update-Regionen,
  <https://learn.microsoft.com/en-us/windows/win32/gdi/the-wm-paint-message>
- Microsoft Learn: Invalidierung und unmittelbares `UpdateWindow`,
  <https://learn.microsoft.com/en-us/windows/win32/gdi/invalidating-the-client-area>
- ISO: ISO/IEC 15408, Evaluationskriterien für IT-Sicherheit,
  <https://www.iso.org/standard/72891.html>
- Common Criteria Portal: offizielle CC-/CEM-Dokumente,
  <https://www.commoncriteriaportal.org/cc/>
