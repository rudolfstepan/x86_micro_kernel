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

Abgenommener Schnitt ist R3.9: Hubbub 0.3.8 und LibParserUtils 0.2.5
liefern den echten HTML5-Tokenizer und Baumaufbau in `HTMLWORK.PRG`. Der
Browser erhält nur eine vollständig validierte semantische Projektion; seine
bisherige Zeichen-/Bild-/Linkschicht bleibt zunächst bestehen. Ein Parserfehler
darf daher nicht die Browser-Chrome beenden. Die Ausgabe ist kein vollständiges
öffentliches DOM und keine CSS-Layout-Engine. CSS/DOM-Layout folgt als nächster
Browser-Schnitt. Die offene VMware-Mausabnahme bleibt auf ausdrückliche
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

Als nächstes ist R3.10 für echte LibCSS-Kaskade und begrenztes CSS-Boxlayout
definiert, noch nicht implementiert. Style-Attribute und eingebettete Styles
teilen die isolierte Dokumentgrenze. Externe Stylesheets, Imports und CSS-URLs
brauchen dagegen einen separaten Ressourcen-/Herkunftsvertrag; dieser Auftrag
erteilt dem Parser keine implizite Netzwerk- oder Dateiautorität.

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
bestandener Gastnachweis. DOM/CSS und eigentliche Engine-Integration bleiben offen.
Der genaue Speicher-/Fehlervertrag steht in `USERSPACE_SDK_AND_PORTABILITY.md`.
