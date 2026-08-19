# GUI-Komponenten, Controls und Dialoge

Stand: 19. August 2026.

Dieses Dokument ist der technische Katalog für sichtbare und interaktive
REIST-GUI-Komponenten. Es trennt bereits nutzbare API von compositorinternen
Bausteinen und geplanter Arbeit. Ein Häkchen bedeutet, dass Quell-API,
Verhaltenstest, SDK-Installation und mindestens eine reale Verwendung
vorhanden sind; eine gezeichnete Attrappe allein gilt nicht als Control.

## Ziel und Schichten

REIST übernimmt etablierte Interaktionsmodelle, aber behauptet keine
Binärkompatibilität zu Win32, Qt, GTK oder Wayland. Wiederverwendbare Controls
sind rendererunabhängige, versionierte C11-Zustandsautomaten in
`libreistgui.a`. Der Compositor besitzt globale Platzierung, Z-Order,
Fensterdekoration und Eingaberouting. Ein späterer GUI-Client besitzt nur
seine lokale Surface und die Controls darin.

```text
GUI-Anwendung
  -> Layout und Controls aus libreistgui
     -> lokale Paint-Liste und lokale Events
        -> versioniertes Surface-/Event-Protokoll
           -> Session-Compositor und Window-Manager
              -> validierte Display-ABI
```

Ein öffentliches Control darf weder Framebufferadressen noch globale
Fensterkoordinaten, Gerätezugriff oder Prozessrechte besitzen. Geometrie ist
lokal und halb offen: `[x, x + width)` und `[y, y + height)`.

## Verbindliche Zustände und Ereignisse

Alle interaktiven Controls verwenden, soweit für ihren Typ sinnvoll, dieselben
Grundbegriffe:

- `visible`, `enabled` und `read_only` bestimmen Teilnahme an Layout, Hit-Test
  und Änderung.
- `hovered` ist Pointer-Feedback ohne Eingabebesitz.
- `pressed` gilt nur während eines passenden Button-Down/Up-Capture.
- `focused` bezeichnet genau ein Keyboardziel innerhalb einer Fokusgruppe.
- `default` und `cancel` benennen die Enter- beziehungsweise Escape-Aktion.
- `checked`, `selected`, `expanded`, `indeterminate` und `invalid` sind
  typspezifische semantische Zustände, keine reinen Farben.

Pointer-Capture beginnt nur nach einem akzeptierten Button-Down und endet
begrenzt mit Button-Up, Abbruch oder Zerstörung. Keyboardnavigation verwendet
eine deterministische Fokusreihenfolge. Zustandsänderungen liefern eine feste
Anzahl lokaler Damage-Rechtecke; Überlauf fordert einen vollständigen Redraw
der betroffenen Surface an.

## Unterstützungsstatus

| Komponente | Öffentliche API | Rendering | Pointer | Tastatur | Status |
|---|---:|---:|---:|---:|---|
| lokale Rechteckgeometrie | ja | n/a | n/a | n/a | [x] gemeinsam versioniert |
| Menüleiste und Popup-Menü | ja | Desktop-Theme | Capture | Pfeile, Enter, Esc | [x] nutzbar |
| Top-Level-Fensterrahmen | nein, serverseitig | ja | Fokus, Move, Resize, Close | Fokus/Open | [x] compositorintern |
| Alert-/Message-Dialog | ja | Desktop-Theme | Buttons, Close, Move | Fokus, Enter, Esc | [x] Basis-API |
| allgemeiner Dialogcontainer | Basis vorhanden | Basis vorhanden | modal/modeless | Ergebnisdispatch | [ ] Clientinhalt fehlt |
| Label | nein | Textprimitive vorhanden | n/a | n/a | [ ] API definieren |
| Push Button | nur Dialogbutton | ja | Capture/Aktivierung | Fokus/Enter | [ ] allgemeines Control extrahieren |
| Checkbox, Radio Button | nein | nein | nein | nein | [ ] geplant |
| einzeiliges Textfeld | nein | nein | nein | nein | [ ] geplant |
| mehrzeiliger Texteditor | nein | Legacy-Vollbildeditor | nein | Legacy | [ ] als GUI-Control neu bauen |
| Liste und Listenauswahl | nein | nein | nein | nein | [ ] geplant |
| Scrollbar und ScrollView | nein | nein | nein | nein | [ ] geplant |
| Baumansicht | nein | nein | nein | nein | [ ] für Dateimanager geplant |
| ComboBox | nein | Popup-Menü wiederverwendbar | nein | nein | [ ] geplant |
| Tabs | nein | nein | nein | nein | [ ] geplant |
| Slider und SpinBox | nein | nein | nein | nein | [ ] geplant |
| Fortschrittsanzeige | nein | nein | n/a | n/a | [ ] geplant |
| Toolbar und Statusbar | nein | Desktopleisten vorhanden | nein | nein | [ ] API definieren |
| Tooltip | nein | nein | nein | n/a | [ ] erst mit monotonem Timer |
| Kontextmenü | Menücontroller verwendbar | ja | Capture | Pfeile/Enter/Esc | [ ] öffentlicher Öffnungsanker fehlt |
| Dateiauswahl-, Farb- und Fontdialog | nein | nein | nein | nein | [ ] spätere Systemdienste |
| interaktive Control Gallery | Menü- und Dialog-API | ja | ja | ja | [x] `/usr/gui/bin/guidemo.prg` |

## Interaktive Referenzanwendung

`userspace/gui/apps/control_gallery/main.c` ist die ausführbare Referenz für
den aktuell freigegebenen API-Umfang. Beide Image-Builds installieren sie als
`/usr/gui/bin/guidemo.prg`; der begrenzte Standard-Suchpfad der Ring-3-Shell
macht sie mit `guidemo` direkt startbar. Die Anwendung bindet ausschließlich
`x86os.h`, `<reist/gui/menu.h>` und `<reist/gui/dialog.h>` ein und besitzt
keinen Zugriff auf private Compositor-Header.

Interaktiv nachweisbar sind Menüleiste, Popup-Menü, modeless und
application-modal Dialoge, semantische Responses, Default-/Cancel-Buttons,
Tastaturfokus, Enter/Escape, Schließfeld und Verschieben per Pointer-Capture.
Noch nicht implementierte Controls stehen getrennt als deaktivierte
Planungsliste in der Anwendung. Eine gezeichnete Attrappe wird dadurch nicht
mit einer vorhandenen API verwechselt. Bis Stufe 3 des Desktop-Workflows eine
Surface-IPC bereitstellt, läuft die Galerie bewusst als temporärer
Vollbildclient.

## Dialogvertrag

Die REIST-Dialog-API folgt den gemeinsamen, weiterhin aktuellen Mustern von
[GTK 4 `AlertDialog`](https://docs.gtk.org/gtk4/class.AlertDialog.html),
[Qt 6 `QDialog`](https://doc.qt.io/qt-6/qdialog.html) und
[Qt 6 `QMessageBox`](https://doc.qt.io/qt-6/qmessagebox.html): Ein Dialog hat
einen Owner, eine Modalität, explizite Buttons, eine Default- und eine
Cancel-Aktion sowie einen auslesbaren Ergebniswert. Standardbuttons und ihre
semantischen Rollen entsprechen dem Prinzip von
[Win32 Task Dialogs](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-taskdialogindirect),
nicht deren ABI oder numerischen Konstanten.

REIST verwendet ausschließlich ein asynchrones `open`/`dispatch`/`complete`-
Modell. Es wird keine zweite, verschachtelte Eventloop wie bei einem
blockierenden `exec()` gestartet; auch Qt empfiehlt `open()`, um solche
Nebenwirkungen zu vermeiden. Abschluss wird genau einmal als typisierte
Response publiziert.

- **modeless:** Eingaben außerhalb des Dialograhmens werden nicht konsumiert
  und können an Menü, Fenster oder Anwendung weiterlaufen.
- **window-modal:** Nur der generationsgebundene Owner und dessen abhängige
  Controls sind gesperrt; andere Top-Level-Fenster bleiben grundsätzlich
  bedienbar, sobald die Surface-ABI dies unterscheiden kann.
- **application-modal:** Alle normalen Ziele der Session sind inert, bis eine
  Response oder ein kontrollierter Abbruch den Dialog schließt.
- **Default/Cancel:** Enter aktiviert den fokussierten oder den expliziten
  Defaultbutton; Escape und das Schließfeld liefern die konfigurierte
  Cancel-Response. Fehlt eine zulässige Cancel-Response, darf Escape den
  Dialog nicht stillschweigend schließen.
- **Verschieben:** Ein Button-Down in der Titelleiste erwirbt ein implizites
  Move-Capture. Die Geometrie bleibt vollständig im Arbeitsbereich; Button-Up
  beendet das Capture auch außerhalb des Rahmens.
- **Buttons:** Jeder sichtbare Button besitzt lokalisierbaren Text, stabile
  Response-ID und Rolle wie Accept, Reject, Destructive, Apply, Reset oder
  Help. Reihenfolge ist Theme-/Plattformpolitik und keine Geschäftslogik.

Für modale Dialoge gelten zusätzlich die Fokusregeln des
[W3C Modal Dialog Pattern](https://www.w3.org/WAI/ARIA/apg/patterns/dialog-modal/):
Fokus beginnt innerhalb des Dialogs, Tab bleibt im Dialog, Escape nutzt die
Cancel-Aktion und darunterliegende Inhalte sind währenddessen inert. REIST
übernimmt diese Interaktionssemantik; eine Accessibility-Bridge wird erst als
vorhanden markiert, wenn Rollen, Namen, Zustände und Fokus über eine
versionierte Systemgrenze abfragbar sind.

## Standardbuttons und Responses

Die öffentliche API reserviert stabile Responses für `OK`, `Cancel`, `Yes`,
`No`, `Retry`, `Close`, `Apply`, `Reset`, `Help`, `Save` und `Discard`.
Anwendungen dürfen positive, anwendungseigene Responses ergänzen. Eine
Response ist nicht mit einem lokalisierten Label gleichzusetzen. Buttonrollen
steuern Standardverhalten und spätere Theme-Reihenfolge; sie ersetzen keine
explizite Default-/Cancel-Auswahl.

## Layout- und Renderingregeln

- Metriken kommen aus einem zentralen Theme; Anwendungen codieren keine
  systemweiten Rahmenbreiten oder Farben.
- Textmessung und Layout erfolgen vor Rendering. Geometrieabfragen verändern
  keinen Zustand.
- Controls zeichnen nur innerhalb ihrer lokalen Clipregion. Solange die
  Display-Textprimitive keinen echten Glyph-Clip besitzt, darf der Compositor
  für Textänderungen sicher auf einen atomaren größeren Redraw zurückfallen.
- Dialogtitel und Fenstertitel gehören zur serverseitigen Dekoration. Ein
  Client darf keinen zweiten konkurrierenden Close-/Move-Rahmen zeichnen.
- Mindestgrößen sind aus Theme, Inhalt und Buttonleiste ableitbar. Zu kleine
  Flächen führen zu einem validierten Fehler, nicht zu Integer-Unterlauf.
- Jede Paint-, Event- und Layoutoperation ist kapazitäts- oder
  schleifenzählerbegrenzt und heap-frei, bis ein eigener Allocatorvertrag
  eingeführt wird.

## Accessibility- und Internationalisierungsvertrag

Jedes neue Control muss bereits im Modell ein semantisches Role-, Name-,
Value- und State-Konzept besitzen, auch wenn die systemweite Accessibility-API
noch fehlt. Farbe allein darf Fokus, Fehler oder Auswahl nicht ausdrücken.
Labels sind caller-owned, UTF-8-fähige API-Daten; der aktuelle VGA-Font deckt
nur einen begrenzten Zeichensatz ab und ist deshalb noch kein vollständiger
Internationalisierungsnachweis. Textlauf, Mnemonics und Rechts-nach-links-
Layout bleiben explizit offen.

## Umsetzungsreihenfolge

- [x] GUI-Quellen, öffentliche Header, Bibliothek und Beispiele trennen.
- [x] Gemeinsame lokale Rechteckgeometrie veröffentlichen.
- [x] Menücontroller mit Capture, Tastatur und Damage bereitstellen.
- [x] Dialogcontroller mit Modalität, Responses, Buttonrollen und Move-Capture bereitstellen.
- [x] Desktop-Hilfe und About auf den öffentlichen Dialogcontroller migrieren.
- [x] Alle freigegebenen Controls in `guidemo.prg` interaktiv demonstrieren.
- [ ] Allgemeines Button- und Label-Control aus dem Dialogrenderer lösen.
- [ ] Fokusgruppe mit Tab/Shift-Tab und semantischer Focus-Reason definieren.
- [ ] Box-/Grid-/Stack-Layout mit festen Kapazitäten bereitstellen.
- [ ] Textfeld samt Cursor, Auswahl, Clipboard- und IME-Grenze spezifizieren.
- [ ] Liste, Scrollbar und ScrollView gemeinsam implementieren.
- [ ] Surface-/Event-IPC generationsgebunden veröffentlichen.
- [ ] Accessibility-Baum und assistive Eventausgabe versionieren.
- [ ] Theme-, Font-, Icon- und Lokalisierungsressourcen versionieren.
- [ ] Dateimanager, Editor, Terminal und Systeminfo als getrennte GUI-Clients portieren.

## Definition of Done für ein Control

- [ ] Öffentliche Struktur beginnt mit Version und Größe; Reserved-Felder
  werden vor Zustandsänderung geprüft.
- [ ] Ownership, Lebensdauer, Koordinaten, Einheiten und Kapazitäten sind im
  Header dokumentiert.
- [ ] Pointer-, Keyboard-, Fokus-, Disable- und Abbruchpfade sind definiert.
- [ ] Renderer und Zustandsautomat sind getrennt; keine Geräte- oder
  Framebufferauthorität gelangt in die Bibliothek.
- [ ] Damage ist begrenzt und Überlaufverhalten getestet.
- [ ] Semantische Rolle, Name, Wert und Zustände sind spezifiziert.
- [ ] Host-Verhaltenstest, Quelltest und installiertes SDK-Beispiel bestehen.
- [ ] Reale VMware-Bedienung ist sichtbar nachgewiesen.
