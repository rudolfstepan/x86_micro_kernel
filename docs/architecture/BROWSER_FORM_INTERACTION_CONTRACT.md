# Static browser form interaction (R3.13 / R3.15)

Status: accepted on 2026-09-06; all frozen host, reference-build and real-input
QEMU gates passed. Evidence is recorded in `../development/CURRENT_WORK.md`.
This is a bounded Ring-3 adapter, not DOM or full HTML compatibility.

## References and admission

Use [WHATWG HTML forms](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html)
for ownership, successful controls, reset and explicit submission, and the
[WHATWG URL serializer](https://url.spec.whatwg.org/#application/x-www-form-urlencoded)
for UTF-8 GET queries. Preserve tree order, repeated names, checked controls,
selected enabled options and only the activated submitter. Normalize line
breaks to CRLF at serialization, spaces to `+`; replace the action query,
preserving its fragment. Hidden `_charset_` submits `UTF-8`.

Hubbub supplies the retained tree and parser form association. Explicit `form`
uses the first matching element ID, which must identify a form. A disabled
fieldset exempts its first legend subtree. Values are Unicode scalar strings;
text/search removes line breaks, textarea retains LF. Reset restores defaults.

This bounded subset supports text/search, hidden, checkbox/radio, textarea,
select/options, submit/reset and labels. Unsupported input types, methods,
encoding/target overrides, validation constraints, base elements affecting form
resolution and credential/file-bearing submissions fail closed with a visible
message before transport. No POST-to-GET fallback. Only HTTP(S) actions without
credentials are admitted. Existing URL capacity is 256 bytes including NUL;
overflow rejects the entire candidate, never a truncated request.

## Ownership, memory and interaction

At most 16 forms, 256 controls (including labels), 512 options, 128 KiB immutable
strings and 128 KiB mutable values, allocated in private Ring 3. The private
worker envelope carries indices and offsets, no pointers. Exact worker identity
and generation, envelope sizes, counts, flags, UTF-8, relationships and geometry
must validate before publication. A failed candidate retains the old document.

Form state belongs to one accepted navigation generation. Reflow can reuse it
only with an identical immutable model; replacement, including identical-content
reload, resets it. Capture never survives reflow or navigation. Editing uses no
worker, parser, network or allocation. Capacity failures preserve previous data.

The existing native value controller accepts only ASCII and 64 bytes; reusing
that storage would violate this package's Unicode and memory contract. A private
browser adapter therefore implements scalar cursor movement and bounded value
storage, using the browser's existing native Surface painting, bevel and font
primitives. No shared UI or public Surface ABI changes. Clipboard, grapheme/IME
editing, DOM events, script, cookies and uploads are not implied.

The browser links the existing opt-in ISO C byte/string runtime and headers;
there is no second browser-private memcpy/memset/memcmp implementation. This
does not select a libc heap: browser allocations still use the existing
private process allocator and STB still uses its fixed decoder arena.

## Acceptance

Real-code host tests exercise projection, malformed envelopes, ownership,
encoding, defaults, exhaustion and stale/reflow state. QEMU must receive actual
pointer/keyboard input and a controlled HTTP fixture must observe the exact
accepted GET and no requests for rejected submissions. Existing input, browser
recovery and resource probes remain frozen. No runtime claim from source tests.

## R3.15 accepted: native maxlength and geometry

Accepted on 2026-09-06: eight host groups, both reference builds and all five
real-input browser guest gates passed. Evidence and preserved failed regressions
are in CURRENT_WORK. Native label/size/rows/cols geometry, centered buttons,
distinct checkbox/radio indicators and a select arrow are included. Select
interaction is still cycling, not a dropdown. No additional input type is claimed.

The 2026-09-06 Google screenshot and retained response identify the normal
`maxlength=2048` search attribute as the local rejection cause. Implement it
for text/search and textarea rather than treating the form as unsupported.
Reference: [WHATWG maxlength](https://html.spec.whatwg.org/multipage/form-control-infrastructure.html#limiting-user-input-length)
and [non-negative integer parsing](https://html.spec.whatwg.org/multipage/common-microsyntaxes.html#rules-for-parsing-non-negative-integers).
Measure UTF-16 code units, not UTF-8 bytes; textarea counts normalized LF.
Missing/invalid limits are absent. Zero is valid. Numerical limits at or above
the existing 128-KiB value capacity saturate there without overflow; that private
byte bound remains stronger. Author defaults are not truncated. User insertion
cannot exceed the limit; deletion remains possible. A dirty, still-overlong
editable value blocks submission; reset restores the default and clears dirty
state, and same-generation reflow preserves it. Readonly/disabled controls
retain their constraint-validation exemptions.

Private form version 2 appends one `max_length_plus_one` word per live control
after the existing strings in the compact wire: zero means absent, otherwise
subtract one for the unit limit. Version 1 remains explicitly decodable with
no limit extension. Old control fields and scene envelope versions do not change.
Validate version, exact length, applicable control kind and limit bounds before
publication. Private editing state caches unit counts and dirty flags without
network, allocation or parser work per key. No Google-specific field rewriting,
JavaScript, POST, cookie or credential authority is added. Other unsupported
constraints remain rejected. Original transport/worker deadlines stay frozen.

Acceptance requires real projection and legacy/new wire tests, Unicode and
boundary edit/reset/reflow cases, then all five browser guest gates. The native
forms gate additionally proves a maximum-length field, refused extra typing
without value loss and the exact successful GET. This is not a claim that
Google’s live results pages or every form work.

## Formularinventar auf Nutzerauftrag, 6. September 2026

R3.15-Parsererweiterung ausdruecklich freigegeben: Der neue Real-Code-Grenzfall
`<textarea>a&#13;&#10;b</textarea>` zeigt eine vorbestehende numerische CR->LF-
Umwandlung in Hubbub 0.3.8, bevor die native API CR/LF normalisieren kann.
Referenz: [numeric character reference end state](https://html.spec.whatwg.org/multipage/parsing.html#numeric-character-reference-end-state).
Der vorhandene Buildadapter entfernt nur diesen Zweig nach unveraenderter
Archivpin-Pruefung und mit exaktem Einzelkontext, der bei Drift abbricht.
Neue Real-Code- und Patch-/Pintests pruefen numerisches CR, echte CR/LF-
Vorverarbeitung, legitime LF-Paare und unveraenderte sonstige Abbildungen.
Keine Zusammenfassung legitimer LF-Paare und kein abgeschwaechter Test als
Ersatz; Diagnose und Stand stehen in CURRENT_WORK.

Historische Vergleichsbasis ist der abgenommene Produktionsstand `5214c019`.
Das oben separat dokumentierte R3.15 ist inzwischen gastabgenommen: maxlength,
size/rows/cols, intrinsische Buttons und Checkbox-/Radio-/Selectdarstellung sind
korrigiert. Die Anzahl unterstuetzter Elementtypen hat sich nicht erhoeht.
"Basis" bedeutet statische native Teilfunktion, nicht vollstaendige HTML-
Konformitaet. Reine Ausgabe von Kindtext zaehlt nicht als Widgetunterstuetzung.
Referenzen: [HTML forms](https://html.spec.whatwg.org/multipage/forms.html),
[Form elements](https://html.spec.whatwg.org/multipage/form-elements.html) und
[alle input states](https://html.spec.whatwg.org/multipage/input.html#states-of-the-type-attribute).

| HTML-Element | Implementierter Stand | Wesentliche Luecke |
| --- | --- | --- |
| form | Basis: Besitzer, action, GET, Reset, geordnete erfolgreiche Controls | Kein POST, multipart, DOM-Submit oder allgemeiner target-/encoding-Support |
| input | Acht Typen mit Basisfunktion; siehe unten | Vierzehn Typen fehlen |
| button | Submit, Reset und inerte Schaltflaeche | Groessen/Beschriftung fehlerhaft; keine Skript-, command- oder Popover-Aktionen |
| label | Explizite und implizite Zuordnung, Fokus/Weiterleitung | Nur einfache Text-/Treffergeometrie |
| textarea | Mehrzeilige Unicode-Eingabe, Reset und GET | rows/cols, wrap, Platzhalter, Selektion/Clipboard und vertraute Scrollbedienung unvollstaendig |
| select | Modell, Auswahlwechsel, GET; multiple im Datenmodell | Kein echtes Dropdown/Optionsfenster; Mehrfachauswahl nicht als brauchbare Liste dargestellt |
| option | Werte, Standardauswahl, disabled und GET | Nur erste ausgewaehlte Beschriftung sichtbar, keine vollstaendige Optionsliste |
| optgroup | disabled-Vererbung | Gruppenbeschriftung/visuelle Gruppierung fehlt |
| fieldset | disabled-Vererbung mit erster-Legend-Ausnahme | Native Gruppenrahmen-/Titelgeometrie fehlt |
| legend | Erste-Legend-Ausnahme; gewoehnlicher Text | Keine echte Rahmenbeschriftung |
| datalist | Keine Vorschlagsfunktion | list-Verknuepfung und Auswahlvorschlaege fehlen |
| output | Hoechstens statischer Kindtext | Kein eigener Ergebnis-/Formzustand, Reset oder DOM-API |
| progress | Hoechstens Alternativtext | Kein Fortschrittswidget |
| meter | Hoechstens Alternativtext | Kein Messwert-/Bereichswidget |
| selectedcontent | Keine spezifische Implementierung | Anpassbarer Select-Inhalt fehlt |

| input type | Implementierter Stand |
| --- | --- |
| hidden | Basis: unsichtbarer erfolgreicher Wert; _charset_ wird UTF-8 |
| text | Basis: Unicode-Eingabe, Cursor, Loeschen, readonly, GET |
| search | Basis: wie text; keine eigene Suchfeld-Ausstattung |
| tel | Fehlt, als unsupported blockiert |
| url | Fehlt, als unsupported blockiert |
| email | Fehlt, als unsupported blockiert |
| password | Fehlt, keine Maskierung/Absendung; absichtlich blockiert |
| date | Fehlt |
| month | Fehlt |
| week | Fehlt |
| time | Fehlt |
| datetime-local | Fehlt |
| number | Fehlt |
| range | Fehlt |
| color | Fehlt |
| checkbox | Basis: checked, Umschalten, GET; nur generisches Kaestchen mit x |
| radio | Basis: exklusive Gruppe und GET; derzeit ebenfalls Kaestchen mit x |
| file | Fehlt, keine Dateiauswahl-/Uploadautoritaet |
| submit | Basis: expliziter Submitter und GET |
| image | Fehlt, kein Bild-Submitter/Koordinatenpaar |
| reset | Basis: Besitzerbezogene Standardwerte wiederherstellen |
| button | Basis: sichtbar/fokussierbar, keine Skriptaktion |

Damit besitzen acht der 22 Typen eine Basisimplementierung; vierzehn fehlen.
Unbekannte input-Typen werden ebenfalls blockiert, statt den standardmaessigen
Text-Fallback zu erhalten. Fehlendes type waehlt bereits text.
Zusaetzlich formassoziierte object-/Custom-Element-Funktionen fehlen; object
wird blockiert. Das obsolete keygen ist kein modernes Formularelement und wird
ebenfalls blockiert. Keine ElementInternals-/FormData-/ConstraintValidation-API.

| Querschnittsfunktion | Stand |
| --- | --- |
| name/value, form-Zuordnung, checked/selected, disabled/readonly | Basis vorhanden |
| GET-Kodierung | UTF-8, Prozentkodierung, CRLF, Reihenfolge, doppelte Namen und aktivierter Submitter vorhanden |
| maxlength | In 5214c019 gesamte Formularabgabe blockiert; mit R3.15 Unitzaehlung/Dirty/Reset/Legacy-Wire abgenommen |
| required, pattern, minlength, min/max/step | Nicht implementiert; Formularabgabe blockiert |
| formaction/formmethod/formenctype/formtarget/dirname | Nicht implementiert; Formularabgabe blockiert |
| placeholder, autofocus, autocomplete/list | Keine entsprechende native Funktion |
| Tab/Enter | Einfache Vorwaerts-Fokusfolge und Submit-Button-Suche; keine vollstaendige tabindex-/Implicit-Submit-Semantik |
| Selektion, Clipboard, IME, DOM-Ereignisse | Nicht implementiert |
| size/rows/cols und intrinsische Knopfgroesse | In 5214c019 weitgehend feste Masse; R3.15-Reparatur abgenommen |
| POST, multipart, Upload, Cookies, JavaScript | Nicht implementiert |
| Lange Formular-/Navigations-URLs | Weiterhin 256 Byte inklusive NUL; nicht mit den 8192-Byte-Ressourcen-URLs verwechseln |

Codebasis: `userspace/gui/apps/browser/browser_forms.c` (Projektor, Zustand,
Submit), `css_engine.c` (Control-Geometrie), `browser_scene.c` (Raster) und
`main.c` (Fokus, Mausklick, Tastatur). Im historischen Inventarstand zeichnet
der Renderer die meisten Typen als Rechteck/Text; Checkbox und Radio verwenden
dasselbe x, Select zeigt nur eine Beschriftung. R3.15 korrigiert diese optische
Luecke fuer die vorhandenen Typen; die uebrigen Inventarluecken bleiben offen.
