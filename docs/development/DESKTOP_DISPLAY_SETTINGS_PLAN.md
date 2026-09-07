# Anzeige-Einstellungen und eigenes PRG-Applet

Stand: 7. September 2026. R1.2e ist mit `732b2930` abgenommen; R3.21 ist als
zusammenhaengendes Anzeige-Paket mit allen eingefrorenen Gates abgenommen. Die
nachfolgende Bestandsaufnahme beschreibt den Ausgangspunkt vor R3.21.
Verbindlicher aktueller Vertrag: `docs/architecture/DISPLAY_SETTINGS_CONTRACT.md`.
Farbtiefe bleibt bewusst unveraendert; Farbschemata sind nicht Teil dieses Pakets.
Die Speicherlayout-Erweiterung ist inzwischen ausdruecklich freigegeben:
Linkerfenster an die bestehende Loadergrenze 64 MiB angepasst, veraltete
eingebettete Userregion abgewiesen, keine Paging-/Reservenlockerung.
Reale ELF-/PMM-Schutztests, alle weiteren Hostgates und beide Referenzbuilds
bestanden. Standard-VGA und emuliertes VMware-SVGA beweisen gespeicherte
800x600/1280x720 nach Neustart, echte Eingaben/Listendarstellung, Applet-Fault
und Wiedereroeffnung, sicheren Fallback und Shellrueckkehr. 128-MiB-Boot und
bestehendes Browser-Eingabegate ebenfalls PASS. Die Auswahl wird vor dem
Speicherklick jetzt anhand des akzeptierten Paints bestaetigt; die erwartete
Boot-Probe wird getrennt vom bewaffneten Applet-Fault validiert.
Ergebnisse und unveraendert erhaltene Fehlerbelege in CURRENT_WORK.

## Gewuenschtes Ergebnis

- Eigenes `DISPLAY.PRG` unter `/usr/gui/bin/display.prg`, als **Anzeige** aus
  der Systemsteuerung erreichbar, nicht nur eine weitere Zeile im Hauptfenster.
- Aufloesung aus den auf dem jeweiligen Backend geprueften Modi auswaehlen;
  aktuellen Modus und gespeicherten Wunsch getrennt anzeigen.
- Menschenlesbare Einstellung in `/etc/reist/desktop.conf`, auch ueber
  `config set desktop ...` aenderbar. `auto` bleibt die sichere Vorgabe.
- Vorhandene native Listen, Buttons und Fokus-/Tastaturbedienung verwenden;
  kein eigens nachgezeichnetes Ersatz-Control und keine zweite Titelleiste.
- Farben sind optional angefragt. Farbschema und Farbtiefe sind verschiedene
  Funktionen; die Rueckfrage ist offen. Das blockiert nicht die Spezifikation
  der Aufloesung. Keine unbewiesenen 16-/24-Bit- oder Palettenoptionen anbieten.

## Bestehende Mechanismen und Luecken

| Bereich | Im aktuellen Quellstand | Konsequenz |
|---|---|---|
| Systemsteuerung | `control_panel/main.c`: vier feste Einstellungen; frisches `CONFIG.PRG` zum Schreiben | Eigenes Anzeige-Fenster ueber den bestehenden Surface-Startpfad oeffnen |
| Konfiguration | `reist.desktop/1`, begrenzter gemeinsamer Parser, `TEMP -> fsync -> close -> rename` im Config-Dienst | Bestehenden Mutationspfad erweitern, keine zweite Schreibimplementierung |
| Desktop-Thema | `classic`/`contrast` werden gespeichert; der Compositor verwendet eine feste Palette und liest diese Einstellung bislang nicht | Speichern allein ist kein Nachweis eines funktionierenden Farbschemas |
| QEMU-Anzeige | `display_control.c`: 1024x768 oder 800x600 fest gewaehlt, 32 Bit | Gepruefte Modusbeschreibung und parametrisierte Aktivierung fehlen |
| VMware SVGA-II | Ueberwachter Ring-3-Treiber; Aktivierung/INFO und begrenzter Kernelmediator; ebenfalls feste Geometrie | Bestehenden Treibervertrag erweitern, Applet erhaelt keine Geraeterechte |
| BIOS/VBE-Fallback | Ein versiegelter Bootmodus; Handoff akzeptiert 1024x768 oder 800x600 bei 32 Bit | Nur diesen bestaetigten Modus anbieten, keine erfundene allgemeine VBE-Modusliste |
| Shell/Abbilder | `/usr/gui/bin` ist Suchpfad; Windows und Make paketieren PRGs explizit | Applet in beiden Layouts eintragen und den tatsaechlichen Start testen |

Quellanker: `userspace/gui/apps/control_panel/main.c`,
`userspace/services/config/config_service.c`,
`userspace/config/include/reist/config.h`, `config/etc/reist/desktop.conf`,
`userspace/gui/compositor/desktop.c`, `drivers/video/display_control.c`,
`userspace/drivers/video/vmware_svga2d.c`,
`userspace/video/include/reist/svga2d.h` und `userspace/bin/shell.c`.

## Architektur und Reihenfolge

1. **Begrenzter Modusvertrag.** Vorhandene Anzeige-Autoritaet wiederverwenden.
   Ring 3 waehlt die Policy; der Kernel prueft lediglich Version, Generation,
   Rechte, Geometrie, Pixelbelegung, Pitch, Speicherbereich und Ressourcenbudget.
   Bestehende Operationen und Strukturlayouts nicht umdeuten; neue Operationen
   append-only und versioniert. VMware-SVGA-II- und VBE-Begriffe aus dem
   Videovertrag beibehalten. Keine allgemeine BIOS-Ausfuehrungs-, Port-, MMIO-
   oder Framebufferautoritaet fuer das Applet. Eine Modusliste ist begrenzt,
   backendgebunden und bei Generationstausch ungueltig.
2. **Konfiguration und Applet als zusammenhaengender Verbraucher.** Einen
   Aufloesungswert statt unabhaengiger Breiten-/Hoehenschreibvorgaenge verwenden,
   beispielsweise `resolution=auto` bzw. `resolution=1024x768`. Alte Dateien
   ohne diesen optionalen Schluessel behalten das bisherige Verhalten;
   unbekannte Schluessel bleiben erhalten. Ganzes Dokument validieren und
   Schreibfehler sichtbar melden. Keine blockierende Kind-Warteoperation im
   UI-Ereignispfad: Statusabfrage und Ende des Config-Kindes sind begrenzt,
   generationsgebunden und halten das Fenster bedienbar.
3. **Anwendung und Rueckfall beweisen.** Zuerst beim Desktopstart den Wunsch
   vollstaendig pruefen, dann aktivieren und tatsaechliche Geometrie ruecklesen.
   Bei ungueltiger/fehlender Konfiguration, fehlendem Modus oder fehlgeschlagener
   Aktivierung auf den geprueften Automatikmodus zurueckfallen, ohne endlose
   Wiederholungen. Scheitert auch dieser, bleibt der bestehende begrenzte
   Degradierungs-/Textkonsolenpfad erhalten. Solange nur Startanwendung besteht,
   bezeichnet das Applet dies ehrlich als beim naechsten Desktopstart wirksam.
4. **Live-Umschaltung nur mit vollstaendigem Ruecknahmevertrag.** Vor einer
   solchen Freigabe brauchen Frames, Eingabegrenzen, Cursor, maximierte Fenster,
   Fensterplatzierung und Surface-Generationen einen zusammenhaengenden Wechsel.
   Testmodus erst nach expliziter Bestaetigung dauerhaft speichern. Eine endliche
   monotone Bestaetigungsfrist und der Rueckfall gehoeren dem Sitzungsdienst,
   nicht dem Applet: Schliessen, Absturz, Hang und veraltete Antworten muessen
   ebenso zurueckgenommen werden. Eine nicht abgesicherte Live-Umschaltung ist
   kein zulaessiger Zwischenschritt.

Groessere Aufloesungen duerfen nicht allein wegen der bisherigen fest
eingetragenen Werte fehlen. Ihre Aufnahme richtet sich nach nachgewiesenen
Backendgrenzen, Pitch, Scanout-/Shadowbedarf und bestehenden Speicherbudgets.
Keine pauschale Garantie fuer 1080p/4K, Monitorfrequenzen oder andere Farbtiefen.
32-Bit-Pixelablage ist insbesondere keine Zusage von 32 sichtbaren Farbbits.

## Urspruengliche Abnahmeanforderungen (in R3.21 erfuellt, ausser expliziten Nichtzielen)

Das Implementierungspaket wurde nach Abschluss von R1.2e mit exakten
`allowed_files` und ausfuehrbaren Gates eingefroren. Modus-/Hardwaregrenze
stand vor Implementierung fest; Live-Recovery bleibt ausserhalb. Zusammengehoerige
Feldfaelle nicht in Kleinstpakete aufteilen; unabhaengige Hardware-Abnahmen
und ein neuer Live-Recovery-Vertrag bleiben eigene Grenzen.

Erforderliche Nachweise:

- Reale Host-Verhaltenstests fuer Modusaufnahme, ueberlaufende Groessen,
  unpassende Pitch-/VRAM-Werte, Quoten, alte Generationen und fehlerhafte
  Hardwareantworten: Ablehnung vor Seiteneffekten, keine verlorenen Ressourcen.
- Config- und Applet-Verhalten: fehlender Schluessel, ungueltige Datei,
  unbekannte Schluessel, Schreib-/Dienstfehler, nur lesbarer Betrieb, Abbrechen,
  Mausklick und Tastatur; kein Speichern halbfertiger Werte.
- Beide Referenzabbilder samt Userspace-Shell-Aufloesung und echtem
  Surface-Start des Applets aus der Systemsteuerung.
- Headless-QEMU-Gastbeweis fuer mindestens zwei unterstuetzte Geometrien,
  Persistenz nach Desktopneustart, sicheren Fallback und weiterhin funktionierende
  Maus-/Fenster-/Scrollbedienung. Pixel-/Geometrienachweis statt nur Logmeldung.
- VMware-Funktionsbehauptungen erst nach eigener Gastabnahme; ein gebautes
  VMware-Abbild oder ein QEMU-SVGA-Test allein qualifiziert VMware nicht.
- Vor Live-Freigabe zusaetzlich Timeout, Applet-/Dienstabsturz, Ruecknahmefehler
  und veraltete Bestaetigung pruefen. Keine sichtbaren Windows-Testdialoge oder
  VMware-Fenster ungefragt oeffnen.

Bestehende Belege und der gesicherte Browserentwurf bleiben erhalten.
Die vorgeschaltete Boot-Reparatur ist separat abgenommen. Nach R3.21 folgt
wieder R3.20 mit seinen eigenen unveraenderten Baseline-/Migrationsgates.
