# Browser-Engine-Umbau

Stand: 5. September 2026. Ausgangspunkt: Checkpoint `cd7025a2`.

## Entscheidung und Grenzen

NetSurf ist der bevorzugte **Portierungskandidat**, noch keine integrierte
Engine. Es besitzt eine eigene in C geschriebene Layout-Engine, mehrere
Frontend-Adapter und getrennt nutzbare HTML-/DOM-/CSS-Bibliotheken. Der
Framebuffer-Port ist ein Beispiel für die Trennung von Engine und Plattform,
aber kein Grund, REIST-Anwendungen direkten Framebufferzugriff zu geben.
Quelle: [NetSurf-Projektbeschreibung](https://www.netsurf-browser.org/about/).

Litehtml bietet HTML/CSS-Layout über Zeichen-Callbacks, benötigt C++/STL und
empfiehlt sich ausdrücklich nicht als vollwertige HTML-Engine. Es würde die
gewünschte Browserfunktionalität nicht allein liefern.
Quellen: [README](https://github.com/litehtml/litehtml/blob/master/README.md),
[C++17-Build](https://github.com/litehtml/litehtml/blob/master/CMakeLists.txt).

Der aktuelle REIST-SDK baut `x86-freestanding`, hat `x86os.h` und spezifische
Subsystembibliotheken, aber keine vollständige ISO-C-/POSIX-Laufzeit. NetSurf
verlangt unter anderem LibCSS, LibDOM, LibNSUtils und zlib; weitere Teile sind
konfigurierbar. Ein direktes Linken gegen das vorhandene SDK reicht nicht.
Quellen: `scripts/build_user_sdk.py`, `scripts/build_user_program.py`,
[NetSurf-Build](https://github.com/netsurf-browser/netsurf/blob/master/Makefile).
Upstream-Lizenzhinweise und genaue Quellpins müssen vor Aufnahme der jeweiligen
Abhängigkeit erhalten und im Third-Party-Verzeichnis dokumentiert werden.

## Abgenommener vertikaler Schnitt: HTTP-Antworten und Navigation

`R3.7-browser-http-navigation` behandelt genau die Transport-/Publikationsgrenze,
die jede spätere Engine benötigt. Der bisherige CURL-Prozess lieferte nur den
Body; Status, `Location` und Inhaltstyp gingen verloren. Daher erschien eine
301-Antwort als Webseite. Der Browser erhält jetzt mit `--include` den
originalen HTTP-Kopf und den dekodierten Body in einer atomar publizierten
Datei. Weiterleitungen verwenden den echten Location-Wert und die effektive
URL wird zur Basis für weitere Ressourcen. Chunked Transfer-Encoding wird im
Transport dekodiert, nicht im HTML-Renderer.

Referenzen: [HTTP-Semantik, RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html),
[HTTP/1.1-Framing, RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html),
[URI-Auflösung, RFC 3986](https://www.rfc-editor.org/rfc/rfc3986.html),
[curl-Optionen](https://curl.se/docs/manpage.html#-i).

Der Adapter bleibt begrenzt: 8 KiB Antwortkopf, unveränderte Body-Quoten,
höchstens fünf Weiterleitungen, 10 Sekunden je Kind und 30 Sekunden pro Kette.
HTTPS darf nicht auf HTTP oder lokale Dateien umgeleitet werden. Ungeeignete
Inhaltstypen, kaputte Antworten, Schleifen und erschöpfte Quoten erhalten die
vorherige Seite. Es gibt weder neue Netzwerkautorität im Browser noch eine
Kernel-/Surface-ABI-Erweiterung.

Bewusste Einschränkungen: GET-only; keine Cookies, Authentifizierungsdialoge,
POST, Kompressionsformate oder stillschweigende Zeichensatzkonvertierung.
`--include` erhält die Originalheader auch bei dekodiertem chunked Body, wie
curl; die gespeicherte Kombination ist deshalb kein unverändert wiederholbarer
HTTP-Datenstrom. Der Browseradapter kennt diesen ausdrücklich benannten Vertrag.
Der RFC-9112-Decoder verarbeitet höchstens vier Informationsantworten, 16384
Chunks und 64 KiB Framing. Eine Chunk-/Trailerzeile hat höchstens 1024 Byte,
der Trailerbereich höchstens 8 KiB und 128 Zeilen. Unbekannte korrekt gerahmte
Extensions/Trailer werden ignoriert; Trailer dürfen Framing, Redirect-Ziel und
Darstellungsmetadaten nicht nachträglich umdefinieren. Mehrdeutiges Framing
(etwa Content-Length zusammen mit Transfer-Encoding), doppelte Location-Felder
und ungültige Steuerzeichen scheitern geschlossen. Explizite Ports sind auf
fünf Dezimalziffern und 1..65535 begrenzt. Dies ist ein begrenzter HTTP/1.x-
Clientvertrag, keine vollständige curl-/HTTP-Kompatibilitätsbehauptung.

R3.7 ist am 5. September 2026 mit allen sieben Targeted-Gates, VMware- und
QEMU-Referenzbuild sowie den QEMU-Gates `curl-client` und
`runtime-desktop-browser` abgenommen. Der QEMU-Build muss unmittelbar vor den
QEMU-Gastgates stehen: Die Laufzeitoption allein baut das vorherige
VMware-Image nicht neu. Befehle, Laufzeiten und Logs stehen in
`docs/development/CURRENT_WORK.md`. Dies ist kein Nachweis vollständiger
öffentlicher Webseiten oder einer VMware-Laufzeitabnahme.

## HTML5-Worker und nachfolgende Arbeit

Der erste abgenommene HTML5-Schnitt R3.9 nutzt Hubbub 0.3.8 und LibParserUtils 0.2.5;
diese liefern den echten HTML5-Tokenizer und Baumaufbau in `HTMLWORK.PRG`. Der
Browser erhält nur eine vollständig validierte semantische Projektion; seine
bisherige Zeichen-/Bild-/Linkschicht bleibt zunächst bestehen. Ein Parserfehler
darf daher nicht die Browser-Chrome beenden. Die Ausgabe ist kein vollständiges
öffentliches DOM. R3.10 ergaenzt inzwischen abgenommen echte CSS-Kaskade und
begrenztes Boxlayout; Details folgen unten. Die offene VMware-Mausabnahme bleibt auf ausdrückliche
Nutzerfreigabe zurückgestellt, nicht bestanden.

Implementiertes R3.9, Abnahme separat in `CURRENT_WORK.md`: lokale
und HTTP-Dokumente gehen erst nach Transport-Reap an `/usr/bin/htmlwork.prg`.
Ein Kind verarbeitet genau einen Auftrag: 64 KiB Eingabe, 2048 kumulative
Baumknoten, 4096 Attribute, 256 KiB Strings, 128 Baumebenen, 262144
Callback-Arbeitsschritte und 4 MiB Upstream-Heap. Die semantische Projektion
behält zusätzlich die bisherigen 512 Elemente und 16 Bildmetadaten bei.
Die Limits sind Ablehnungsgrenzen, keine Vollseiten-Kompatibilitätszusage.

Der private Little-Endian-i386-Dateiadapter hat Version 2 und einen
48-Byte-Header ohne Zeiger. V1 übertrug 133380 Byte auch bei leeren Arrays;
die nicht abgenommene V1-Drahtdarstellung wird explizit abgewiesen. V2 überträgt
Titel, fünf geprüfte Zähler und nur belegte Array-Präfixe: Text, Elemente, Links,
Bilder, Anker. Die maximale Antwort bleibt 133380 Byte. Vor Rekonstruktion
werden Dateilänge, Headergröße, Zähler und die exakt berechnete Nutzlastgröße
geprüft. Nur der private Kandidat wird vollständig mit Nullen initialisiert;
der Decoder normalisiert dessen Headergröße auf die In-Memory-Strukturgröße.
Es gibt keine Kompressionsbibliothek, dynamische Allokation oder neue
Dateiautorität. Die semantische Prüfung bleibt vollständig erhalten.
Auftrag, Parent-Generation und
beobachtete Kind-Generation werden geprüft; ein früh beendetes Kind bleibt
bis zum Wait an den bestätigten Parent gebunden. Erst nach dessen Reap wird
die gesamte Antwort auf Größe, Zähler, UTF-8, Indizes, Styles und Reserven
geprüft. Die Darstellung wird erst nach erfolgreichem Layout umgeschaltet.
Eingabe und Ausgabe gehören dem Parent; während ein Kind lebt, werden sie
nicht entfernt. Auch partielle Ausgabe nach Fehler ist nicht publizierbar.
Fünf Sekunden Parent-Deadline umfassen Auftragsdatei, Spawn und Parser;
Abbruch beendet/reapt das eigene Kind, behält die Seite und erlaubt den nächsten
Auftrag ohne automatischen Wiederholungszyklus. Die bisherigen begrenzten
VFS-/Syscall-Dateiadapter bleiben Migrationsbestand, kein Capability-Sandbox-
Nachweis für ein bösartiges Programm. Der Worker ruft keine Netzwerk-/GUI-API auf.
Explizite lokale Testmodi injizieren UD2 und verzögerten Exit; Webseiten können
diese Modi nicht aktivieren. JavaScript und Stylesheets werden nicht ausgeführt.

Die R3.9-Gastabnahme ist bestanden. Vor der getrennten ATA-Reparatur
wurden 4009 ms Laden und 88 ms Speicheraufbau des Workers gemessen. Ein
Sleep-only-Versuch verschlechterte das Laden auf 6370 ms und wurde
zurückgenommen. R7.1n ist inzwischen in `d725efb0` separat abgenommen:
fähigkeitsgeprüfte READ-MULTIPLE-Transaktionen erreichen im eingefrorenen
QEMU-Benchmark 635,23 statt 101,91 KiB/s. Fähigkeiten, Reset und Teilfehler
sind im `ATA_PIO_TRANSFER_CONTRACT.md` samt Prüfungen beschrieben. Auf dieser
Grundlage bestand der Browser seinen separaten QEMU-Gastnachweis einschließlich
Parserfehler, Timeout, anschließender Navigation und Close. Ein zuvor falsch
positiver Diagnose-Runner wurde durch einen Verhaltenstest abgesichert und
repariert; Screenshot-Capture erfordert keine künstliche Gastpause mehr.
Der vollständige finale Lauf hat keinen späten Fehlermarker. Das ist sichtbare
Legacy-Treiberschuld, kein Nachweis
der geplanten Ring-3-Treiberisolation. Journalbarrieren, öffentliche ABIs und
alle Browser-Zeitlimits bleiben unverändert; auch die negative Evidenz bleibt
in CURRENT_WORK dokumentiert.

R3.10 liefert abgenommen echte LibCSS-0.9.2-Kaskade und begrenztes
CSS-Boxlayout; die vollstaendige Host-/Gastabnahme steht in CURRENT_WORK.
Der unveränderte MIT-Release wird geprüft/gepinnt, seine C-Property-Generatoren
laufen nur auf dem Host. Nur HTMLWORK linkt LibCSS, Hubbub und LibWapcaplet.
Der erhaltene HTML5-Baum trägt Elementnamen, Attribute, Eltern und Geschwister.
Element-/Klassen-/ID-Selektoren, Kind-/Nachfahren-/Geschwisterbeziehungen,
Attributselektoren, Inline- und eingebettete Styles verwenden die tatsächlichen
Upstream-Regeln für Spezifität, Reihenfolge, Vererbung und `!important`.

Referenz ist [CSS 2.1, Boxdimensionen](https://www.w3.org/TR/CSS21/visudet.html):
implementiert sind normaler Block-/Inline-Fluss und
`display:none`, Text-/Hintergrundfarben, Fontgröße 1–32 CSS-Pixel, synthetisches
Fett/Kursiv, links/zentriert/rechts ausgerichtete Zeilen, Blockränder, Padding,
Rahmen und begrenzte Breite/Höhe. CSS-Pixel sind bei diesem 96-dpi-Profil
Gerätepixel. em bezieht sich auf die berechnete Fontgröße; Prozentbreiten und
Padding/Margins auf den umschließenden Inhaltsblock. Prozenthöhen wirken nur
bei bestimmter Elternhöhe, sonst auto. Benachbarte vertikale Blockmargins
kollabieren; Eltern-/Kind-Margin-Collapsing ist noch nicht vollständig.
Die Schriftfamilie ist die feste Unicode-Monospace-Schrift; ihre vorhandenen
PSF2-Daten sind wie das Desktop-Splash-Asset read-only ins Programm eingebettet
und werden einmal vor der Dokumentverarbeitung geprüft. Das spart einen
weiteren langsamen VFS-Transfer, repariert aber nicht dessen allgemeine
Durchsatzgrenze. Das Programm wächst um 2585144 Byte, bleibt innerhalb der
unveränderten 8-MiB-MYPR-Grenze und braucht keinen Font-Dateihandle. LibCSS löst
thin/medium/thick auf 1/2/4 CSS-Pixel auf. Rahmen werden einfarbig gezeichnet.
Inline-Box-Dekoration, CSS-Tabellenlayout, Float/Positionierung, Flex/Grid,
Pseudoelement-Inhalte, CSS-Fonts, vollständiges Bidi/Shaping und Animation sind
nicht implementiert. Andere display-Werte nutzen den dokumentierten einfachen
Block-Fallback; inline-block nutzt Inline-Fluss. Keine vollständige CSS-,
NetSurf- oder moderne Web-Kompatibilitätszusage.

Der private CSS1-Auftrag enthält den bestehenden 48-Byte-HTML-Header,
Viewport, 16 intrinsische Bildgrößen und die Dokument-URL (444 Byte), gefolgt
von maximal 64 KiB HTML. Generation-scoped Bulk-IPC-Pakete enthalten 16 Byte
Framing (Magic, Request, Offset, Gesamtlänge) und höchstens 2032 Datenbytes.
Ein Parent-eigener Endpoint dient genau einem Kind. Pro UI-Runde werden
höchstens acht Pakete ohne Warten verarbeitet; Replies werden schon vor dem
Reap privat abgeholt, damit kein voller Kanal den Worker blockiert.
Vor der ersten akzeptierten Nachricht wartet der Worker bei EBADF/EACCES
auf die erst nach Spawn moegliche Rechteuebergabe: jeweils 1 ms Schlaf,
innerhalb seiner unveraenderten absoluten Deadline. Fehlgeschlagenes Schlafen
oder Rechteverlust nach Empfangsbeginn beendet den Auftrag ohne Antwort.
Fehlende Delegation bleibt damit begrenzt; ein alter Auftrag erhaelt durch
diese Startbehandlung keine erneute Autoritaet.
Bei exakt vollstaendiger Rahmung endet der Receive-Pfad: Das regulaere EPIPE
nach sofortigem Worker-Exit darf eine fertige Antwort nicht verwerfen.
EPIPE vor dem Ende, falsche Laengen und falsche Generationen bleiben Fehler.
Abbruch widerruft den Endpoint, beendet/reapt das Kind und verwirft die Ausgabe.
Die fünfsekündige Parent-Deadline umfasst Spawn, Parsing, Layout und Transfer.
Der alte V2-Datei-CLI-Adapter bleibt separat kompatibel; neue Browseraufträge
schreiben keine Parser-Temporärdateien. Das erweitert keine OS-Capabilities.
Auch deren vorsorgliche Bereinigung entfaellt: Nur ein tatsaechlich gestartetes
CURL-Kind markiert seine Body-/Teil-Datei als aufzuraeumen. Vor Reap erfolgt
kein Unlink, nach Bereinigung ist der Pfad ohne weiteren VFS-Aufruf idempotent.
Die alte grobe Unlink-Fehlerabbildung bleibt sichtbare Legacy-Schuld, kein
neuer Nachweis exakter POSIX-errno- oder garantierter Loeschsemantik.
Die Entfernung dieser Aufrufe allein beseitigte den gemessenen Engpass nicht:
das wiederholte Laden des 874540-Byte-Workers blieb dominant. Die echten
Parserbibliotheken werden deshalb mit dem regulaeren LLVM-`-Os` sowie
Funktions-/Datensektionen gebaut; der bestehende Linker entfernt unerreichbare
Sektionen. Quellen, Parserregeln, Heap-/Arbeitsquoten und Fristen bleiben gleich.

Chrome darf ausschliesslich seine letzte vollstaendig validierte Szene
wiederverwenden: zuvor ist das Dokument erfolgreich frisch gelesen/geladen,
der Inhalt bytegenau gleich (kein Hash-Ersatz), die effektive Ressourcen-URL identisch,
Viewport und verwendete intrinsische Bildmasse passen. Fehler-/Timeoutmodi,
geaenderte Bytes, URL, Geometrie oder noch ausstehendes Reflow verhindern den
Treffer. Vorhandene HTML-/Szenenpuffer tragen diesen Ein-Dokument-Cache; hinzu
kommen nur 64 Byte intrinsische Vergleichsmasse, kein neuer Heap oder Dateicache.
Reload laedt Bilder weiterhin frisch, blendet alte Bitmapdaten bis zur
Neudekodierung aus und zeigt bei Fehlern Alternativtext. Nur tatsaechlich
geaenderte intrinsische Masse erfordern dann ein neues isoliertes Reflow;
fehlgeschlagene Bilder duerfen ihre zuletzt bekannten Boxmasse behalten.
Nur das Fragment wird beim Ressourcenvergleich entfernt, wie bei der
Dereferenzierung nach [RFC 3986, Abschnitt 3.5](https://www.rfc-editor.org/rfc/rfc3986#section-3.5).
Nach `#details` und Reload startet damit kein identischer CSS-/Bild-Reflow
erneut. Query, Pfad, Schema und Authority bleiben bytegleich erforderlich;
Adressleiste und Fragment-Scrollziel werden dennoch aktualisiert. Die aktuelle
CSS-Szene besitzt keine dynamischen Pseudoklassen wie `:target`; deren spaetere
Einfuehrung muss den Szenenschluessel um diesen Zustand erweitern. Ein echter
Datei-Reload vor dem Treffer bleibt vorgeschrieben, ebenso Bildaktualisierung.
Das ist keine HTTP-Cache-Control-, externe Stylesheet- oder Script-Cache-API.
Dateiinhalte werden innerhalb der bereits vorhandenen 128-KiB-VFS-Bulkgrenze
gelesen, nicht mehr in 4-KiB-Transaktionen. Fuer das 214860-Byte-GIF sind damit
zwei statt 53 Aufrufe erforderlich. Maximalgroesse, exact-size/short-read-
Pruefung, generationgebundenes Dateiobjekt und Close-Pfad bleiben erhalten;
neue VFS-/Kernelmechanismen oder groessere Bildpuffer sind nicht erforderlich.

Der Reply enthält den geprüften V2-Dokumentteil und eine Szene mit Version,
Viewport, Gesamthöhe und höchstens 2048 zeigerfreien 40-Byte-Zeichenbefehlen.
Text (höchstens 128 Byte je Run, insgesamt 64 KiB), Bilder, Anker und Farbflächen
haben geprüfte Typen, Indizes, UTF-8-Grenzen, Flags und Koordinaten bis 262144.
Bild-Pixel und Fonts werden nicht vom Worker mitgeliefert. Chrome prüft die
komplette Szene und die exakte Kind-/Auftragsgeneration vor Veröffentlichung;
komplexe CSS-/Layoutarbeit verbleibt im Worker. Zeichenarbeit ist auf vier
Millionen sichtbare Pixeloperationen pro Frame begrenzt. Ein überschrittenes
Darstellungsbudget behält die bisherige Ansicht, statt Chrome zu beenden.
Der private Single-Thread-Renderer bereitet gleiche Glyphen/Pixelhoehen pro
Frame nur einmal vor, solange ihr direkter Cacheplatz nicht verdraengt wird.
128 feste Plaetze benoetigen 263168 Byte Prozessspeicher; Tag-Invalidierung
vor jedem Rasteraufruf verhindert die Wiederverwendung alter Fontinhalte.
Farbe, Deckkraft, Fett/Kursiv und Clipping wirken weiterhin auf jedes Zeichen.
Es gibt keine Allokation im Zeichenpfad, kein neues Dateicache-/Font-Handle
und keine erweiterte Zeichenarbeitsquote. Ein Hostfall prueft 256 gleiche
Zeichen mit einer statt 256 Glyphrasterungen und unveraenderten Pixeln sowie
neue Schriftbytes, kaputte Fontgrenzen und verschiedene Pixelhoehen.
64 Stylesheets, 262144 Adapter-Arbeitsschritte und der bestehende 4-MiB-Heap
begrenzen den Worker. Chrome reserviert vorab die bisherigen 22 MiB plus
2 MiB für Unicode-Mappings; die Bitmapdaten liegen im read-only-Programmsegment.

Resize-Aufträge werden auf den jüngsten Viewport zusammengefasst, Ergebnisse
für überholte Größen nicht dargestellt. Intrinsische Bildgrößen werden nach
der begrenzten Ladefolge gemeinsam neu berechnet; Scrollposition und Cache
bleiben beim Reflow erhalten. Links und die native Scrollbar verwenden die
geprüfte Szenengeometrie, Pixel/Glyphen werden am Viewport abgeschnitten.
Der Browser aktiviert die kompatible Surface-Scroll-Erweiterung v1 explizit.
Das Mausrad scrollt den vorhandenen Dokumentzustand um 48 Pixel je Rasterstufe;
Bruchteile werden ohne Verlust akkumuliert und Anfang/Ende ueberlaufssicher
begrenzt. Adress-/Statuszeile und ein aktives Thumb-Capture bleiben unberuehrt.
Kein Laden, CSS-Auftrag, Fokuswechsel oder synchrones Paint im Mausradhandler;
der feste Event-Batch fasst die noetige Neuzeichnung zusammen. Der Gasttest
verlangt echte QEMU-USB-HID-Radereignisse abwaerts/aufwaerts und gezeichnete
Scrollpositionen, nicht direkt im Test aufgerufene Browserhandler.
Die Browser-Teststeuerung verwendet quittiertes natives
[`input-send-event`](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#command-input-send-event)
nach der [QMP-Spezifikation](https://www.qemu.org/docs/master/interop/qmp-spec.html).
Eine kurzlebige Loopback-Verbindung ersetzt ausschliesslich die zweimalige
75-ms-Serial-Mux-Pause je Mauskommando; andere Desktop-Proben behalten ihren
Adapter. QMP-ACK bedeutet nur eingespeistes Geraeteereignis: HID-Pacing und
Gastmarker fuer Configure, Reflow und gezeichnetes Scrollen bleiben notwendig.
Admission hoechstens fuenf Sekunden, ein Kommando hoechstens eine Sekunde,
jeweils innerhalb der bestehenden absoluten Hostfrist; 64-KiB-Antwortpuffer,
4096-Byte-Kommando und 32 Zwischenereignisse. Fehler beenden die Abnahme und
schliessen Verbindung/Listener sowie den QEMU-Prozess. Der Gast bekommt weder
zusaetzliche Zeit noch einen alternativen Eingabehandler. Probe-only-Zeitzaehler
trennen Datei, Dekoder, Raster, Pixelpuffer/IPC, Body, Chrome, Status und Spawn;
Body enthaelt Raster/Puffer/IPC, die Summen sind deshalb nicht additiv.
Der erste QMP-Lauf quittierte die Eingaben, erreichte aber kein Resize.
QEMU kann noch ungelesene relative Bewegungen vor der Randbegrenzung
zusammenfassen. Deshalb muss der volle sichtbare Softwarepfeil einschliesslich
Schatten nach Homing und vor dem Resize-Button-down an der erwarteten Position
stehen. Quittierte native Screendumps mit frischen Dateinamen liefern diesen
beobachtbaren Gastnachweis; maximal eine Sekunde/16 Versuche pro Barriere,
innerhalb der unveraenderten Gesamtfrist. Wire-ACK oder berechnete Position
genuegen nicht. Abnahme und erhaltene Negativbelege stehen in CURRENT_WORK.
Style-URLs werden rein syntaktisch aufgelöst, niemals geladen. Externe link-
Stylesheets und CSS-Bilder sind inert; ausstehende `@import`-Abhängigkeiten
führen zu einer begrenzten Ablehnung des Auftrags, nicht zu vorgetäuschtem
Import-Erfolg. Das benötigt später einen eigenen Ressourcen-/Herkunftsvertrag.
Unbekannte Deklarationen behandelt LibCSS nach seiner Parser-Fehlererholung.
Formulare, Cookies, POST und JavaScript bleiben aus; es gibt keine implizite
Netzwerk-, Datei- oder Skriptautorität durch einen CSS-Callback.

Die Wiederaufnahme auf der abgenommenen R1.2c-Speicherbasis behaelt die
eingefrorene CSS-Auftragsgrenze von 64 KiB und den 4-MiB-Worker-Heap; sie sind
keine allgemeine OS-Speichergrenze. Der Browser-Testprozess nutzt denselben
verifizierten Windows-11-Timer-Helper wie die Speicherabnahme, mit unveraenderter
Prioritaet und Gastfrist. Konfigurationsfehler beenden/reapen das neue QEMU-Kind
vor dem Readerstart. Der alte VFS-Schriftfehler bleibt als negative Evidenz
erhalten, die aktuelle eingebettete Schrift benoetigt keinen Dateitransfer.

Die Engine-Portierung braucht einen eigenen Ring-3-Laufzeit-/Allocatoradapter,
einen hostgeprüften und im Gast ausgeführten DOM/CSS-/Layoutpfad sowie feste
Speicher- und Ausführungsbudgets. Renderer-Absturz oder Hänger müssen gegenüber
Browser-Chrome, Compositor und unabhängigen Anwendungen isolierbar bleiben.
Eingaben und Ausgabe laufen über validierte Nachrichten und Surface-Puffer;
HTTP(S) bleibt im getrennten Transportprozess.

Expliziter Nutzerauftrag: Alles für diesen Port Notwendige, das REIST fehlt,
wird nachimplementiert. Fehlende allgemeine C-/Datei-/Zeit-/Speicherfunktionen
gehören als wiederverwendbare, standardnahe SDK-Verträge ins Betriebssystem,
nicht als wirkungslose Browser-Stubs. Die Implementierung bleibt in Ring 3;
neue notwendige Kernelmechanismen benötigen ihren eigenen begrenzten Nachweis.
Diese Zusage ist keine Behauptung, dass POSIX oder die Engine schon vollständig
unterstützt wären.

Formulare und ihre Navigation müssen mit DOM und Layout integriert werden;
POST-/Cookie-Persistenz und Herkunftsregeln sind eigene Autoritätsgrenzen.
JavaScript bleibt zunächst aus. Eine spätere Engine läuft in einem separaten,
quota- und generationgebundenen Ring-3-Dienst hinter einem versionierten
IPC-/DOM-Adapter. NetSurfs vorhandene JavaScript-Einbettung wird nicht ungeprüft
in den REIST-Browser übernommen. Vollständige moderne Webkompatibilität wird
nicht behauptet.

R3.6c ist inzwischen vollständig abgenommen; die reale VMware-Mausabnahme
R3.6b bleibt offen. R3.8 implementiert die erste wiederverwendbare C-Speicher-/
Byte-Laufzeit und führt LibWapcaplet 0.4.3 aus dem NetSurf-Projekt auf ihr aus.
Die Standardheader und Archive sind opt-in im Sysroot installiert; TLS und
bestehende Browserprogramme werden nicht umgestellt. `CRTEST.PRG` prüft im Gast
Speichermangel, Kindfehler, Reap und neue Generationen. Die R3.8-Abnahme steht in
`CURRENT_WORK.md`; ein geplanter oder implementierter Test ist allein noch kein
bestandener Gastnachweis. R3.9/R3.10 haben seither HTML5 und begrenztes CSS-Layout
integriert und abgenommen; R3.11 ergaenzt als naechster separater Schnitt externe
Stylesheets/Imports mit einem validierten Ressourcenbuendel. Formulare und
JavaScript bleiben offen.
Der genaue Speicher-/Fehlervertrag steht in `USERSPACE_SDK_AND_PORTABILITY.md`.
