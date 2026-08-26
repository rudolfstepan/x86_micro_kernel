# Projektstatus

Stand: 26. August 2026. Maßgeblich sind ausführbarer Code, die Tests und die
aktive Paketqueue in `automation/reist-s03b.toml`.

REIST OS ist ein nicht zertifizierter High-Assurance-Forschungsprototyp. Die
vorhandenen Schutzmechanismen dürfen nicht als klinische, industrielle oder
sonstige sicherheitsbezogene Freigabe verstanden werden.

`R2.2-nvidia-gk208-bringup` ist als automatisierter Hardware-Schnitt für native
2D-Beschleunigung auf dem ASUS-Board. Die exakte Karte `10de:1280` erhält eine
überwachte Ring-3-Domäne und einen passiven, festen Kernelmediator für PMC-,
PTIMER-, PFIFO- und PGRAPH-Diagnose. VBE-Scanout und Software-Framebuffer
bleiben maßgeblich; Beschleunigungs-Capabilities bleiben bis zu einem echten,
deadlinebegrenzten GPU-Fence absichtlich null. QEMU/VMware prüfen Build,
Lifecycle und Rückfall, der abschließende Probe-Nachweis bleibt ein manueller
ASUS-Lauf.

`R2.2g-nvidia-gk208-engine-contract` bereitet den nächsten Engine-Schritt
fail-closed vor. Ein fester 64-Dword-Compiler und ein unabhängiger Parser
akzeptieren ausschließlich dokumentierte `FERMI_TWOD_A`-Methoden für
pitch-lineares XRGB8888-Fill und überlappungssicheres Same-Surface-Copy. Der
Ring-3-Treiber prüft diesen Vertrag ohne Hardwarewirkung und verifiziert über
zwei read-only BAR0-Snapshots mit einer begrenzten Millisekundenpause einen
fortschreitenden PTIMER. GPU-VM, GR-/FECS-/GPCCS-Initialisierung, GPFIFO,
Busmaster, DMA, IRQ und Fence sind noch nicht aktiviert; deshalb bleiben die
NVIDIA-Capabilities null und VBE verbindlich.
`R2.2h-nvidia-ring3-register-probe` beseitigt dabei die verbliebene direkte
NVIDIA-Registerprobe aus Ring 0. Der generische Device-Domain-Mediator clippt
den 16-MiB-BAR vor dem Mapping auf die unveränderliche read-only Apertur
zunächst `0x400104`; `R2.2p` erweiterte sie bis zum GPCCS-DMEM-Port und
`R2.2q` read-only bis `0x5fa60c` für den vollständigen, auf 32 GPCs begrenzten
Topologie-Snapshot. Der überwachte Treiber liest PMC, PTIMER, PFIFO und PGRAPH nun
selbst mit generationsgebundenem Handle. PTIMER-Kohärenz ist auf vier
high-low-high-Versuche begrenzt. Schreib-, Mapping-, DMA-, IRQ-, Busmaster-
und Beschleunigungsrechte bleiben ausgeschlossen.
`R2.2i-nvidia-gk208-submission-contract` versiegelt nun auch die spätere
Einreichungsform ohne Hardwarewirkung. Ein fester 72-Dword-Umschlag enthält
genau einen Klassen-Bind, einen bereits unabhängig validierten 2D-Strom, eine
wait-for-idle 4-Byte-Semaphorfreigabe und einen exakten Kepler-GPFIFO-Eintrag.
Der zweite Parser weist Adress-, Längen-, Padding-, Privileg-, Subroutine-,
Conditional-Fetch-, Sync-Wait- und Fence-Abweichungen zurück. GPU-VM,
Kanalerzeugung, USERD-Kick, Firmware, Busmaster und echte GPU-Ausführung sind
weiterhin nicht aktiv; NVIDIA-Capabilities bleiben deshalb null.
`R2.2j-nvidia-gk208-dma-staging` bindet für die exakte GK208-Domäne erstmals
einen kernelverwalteten, beim Generationwechsel genullten 64-KiB-Pool. Oberhalb
der kernelexklusiven 4-KiB-Deskriptorseite werden genau ein 8-Byte-GPFIFO-
Eintrag, der vollständig nullaufgefüllte 72-Dword-Pushbuffer und ein 4-Byte-
Null-Fence geschrieben und bytegenau zurückgelesen. Physische Adressen bleiben
verborgen. Gleichzeitig weist der generische Mediator DMA-Binds von reinen
`MEDIATED_IO`-Profilen jetzt vor einer Allokation ab; VMware bleibt dadurch
unverändert DMA-frei. GPU-VM, RAMFC, Runlist, USERD, IRQ, Aktivierung,
Busmaster und Capabilitybits bleiben offen.
`R2.2k-nvidia-gk208-channel-image` ergänzt den exakten, weiterhin
hardwareinaktiven Einzelkanal-Speichervertrag. Aus GK208s upstream Auswahl von
`gk110_chan`/`gk110_runl` und deren GK104-Erbe entstehen ein vollständig
validiertes 4-KiB-Instanz/RAMFC-Bild, 512 Byte nullinitialisiertes USERD und
genau ein 8-Byte-Runlist-Eintrag für Kanal 1. Der 4-KiB-GPFIFO wird nur als
GPU-VA-Platzierung beschrieben. Die physische USERD-Adresse bleibt in RAMFC
null und wird durch genau eine noch ungelöste kernelverwaltete 64-Bit-
Relokation bezeichnet. Ring 3 überträgt und verifiziert alle drei Bilder in
höchstens 1024-Byte-Abschnitten, erhält aber weiterhin keine physische Adresse.
GPU-VM, GR-Kontext, MMIO-Schreibrechte, Runlist-Commit, USERD-Kick, IRQ,
Aktivierung, Busmaster und Capabilitybits bleiben offen.
`R2.2l-nvidia-gk208-gpu-vm-plan` reserviert danach die vollständigen,
festen Seitentabellenfenster. Nur GK208 wählt den 512-KiB-Pool; alle bisherigen
mediated-DMA-Profile behalten 64 KiB. Für 64-KiB- und 128-KiB-FB-Seiten werden
die beiden upstream GK104/GK208-Geometrien mit 4-KiB-GPU-Seiten, 40-Bit-VA,
passenden 14+14- beziehungsweise 13+15-Bit-Indizes und exakt fünf
kernelverwalteten Relokationen validiert. Pushbuffer und GPFIFO sind read-only,
das Fence ist schreibbar, alle drei verwenden die NCOH-Apertur. Ring 3 prüft
weiterhin nur null gelassene Adressziele und schreibt ausschließlich das feste
VM-Limit `2^40-1`; Relokation, Aktivierung und GPU-Ausführung bleiben offen.
`R2.2m-nvidia-gk208-dma-relocation-seal` löst diese sechs Adressen nun atomar
im Kernel auf. Zwei vor dem ersten Claim installierte Templates decken die
bereits validierten 64-/128-KiB-FB-Seitenvarianten ab; der Treiber wählt den
upstream Standardwert 17. Kommando 19 vergleicht alle Regeln exakt, prüft
sämtliche Zielwörter vorab auf null und schreibt erst danach die vollständige
Menge. Ein erfolgreicher Seal sperrt Ring-3-Lesen, -Schreiben und
Deskriptoränderungen bis zur generationgebundenen vollständigen Nullung.
Physische Adressen werden nicht zurückgegeben. MMIO, Page-Mode-Umschaltung,
GPU-VM-Aktivierung, USERD-Kick, IRQ, Busmaster und Capabilitybits bleiben offen.
`R2.2n-nvidia-gk208-vm-page-mode` setzt anschließend ausschließlich die zur
versiegelten Policy passende FB-Page-Auswahl. Append-only Kommando 20 bindet
Gerät, read-only BAR-Handle und sealed DMA-Pool derselben Generation und ändert
bei `0x100c80` nur Bit 0; der Standard 17 löscht es, Alternative 16 setzt es.
Der Kernel prüft Ziel- und alle erhaltenen Bits. Fehler rollen sofort zurück;
Fence und Cleanup deaktivieren zuerst Busmaster und stellen das ursprüngliche
Policy-Bit mit Readback wieder her. Eine fehlgeschlagene Wiederherstellung
bleibt gesperrt und wiederholbar. Ring 3 erhält kein Schreibrecht. Channel-/
PGD-Bind, Runlist, GR/FECS/GPCCS, USERD-Kick, IRQ, Busmaster, echter Fence und
Capabilitybits bleiben weiterhin offen.
`R2.2o-nvidia-gk208-gr-firmware-contract` bindet nun die exakten
MIT-lizenzierten Nouveau-nofw-Abbilder für GK208 FECS und GPCCS an einen
gepinnten Linux-Commit. Vier unveränderliche Arrays enthalten zusammen 1244
Little-Endian-Dwords; ein fester 64-Byte-Manifestvertrag hält Wortzahlen und
IEEE-CRC32-Werte fest. Der überwachte Treiber validiert alle 4976 Bytes noch
vor dem Öffnen des DMA-Pools. Der öffentliche Zugriff kopiert nur ein
bereichsgeprüftes Wort und gibt keinen veränderlichen Zeiger frei. Upload,
GR-Registerpakete, Firmware-Start, Kontextgenerierung, Channel-Bind, Runlist,
USERD, IRQ, Busmaster, echter Fence und Capabilitybits bleiben offen.
`R2.2p-nvidia-gk208-falcon-upload` schließt den gesamten sicheren
Pre-Start-Slice: Alle vier Images liegen in festen, getrennten Fenstern des
512-KiB-Pools und werden vor dessen Versiegelung in höchstens 1024-Byte-
Abschnitten geschrieben und zurückgelesen. Append-only Kommando 21 prüft die
vier CRCs erneut, resettiert nur das erhaltene GR-Bit, wartet höchstens 100 ms
auf abgeschlossenes FECS-/GPCCS-Scrubbing, schreibt beide DMEM-/IMEM-Paare
mit 256-Byte-Tags und liest jedes Wort zurück. Beide Falcons bleiben
angehalten. Fehler/Fence deaktivieren Busmaster, resetten GR und stellen erst
danach den Page-Mode wieder her; nicht bestätigte Bereinigung bleibt
wiederholbar gesperrt. Offen sind nun die vollständigen topologyabhängigen
MMIO-/Context-Switch-Listen, Falcon-Start, Channel-Bind, Runlist, USERD, IRQ,
echter Fence und Capabilityfreigabe.
`R2.2q-nvidia-gk208-gr-plan` bündelt die gesamte sichere Planvorstufe. Aus dem
bereits gepinnten MIT-lizenzierten Nouveau-Stand werden reproduzierbar alle 30
geordneten GK208-MMIO-Pakete mit 115 Tupeln und die fünf FECS/GPCCS-
Kontextstreams mit 199 Tupeln erzeugt und durch feste CRC32-Werte geschützt.
Ring 3 liest GPC-/ROP-Anzahl sowie je aktivem GPC TPC-Anzahl und PPC-Maske,
validiert Summen und Architekturgrenzen und erzeugt die maximal 32 Register
langen Transfergruppen in festem Speicher. Der spätere HUB-Start- und
Readiness-Ablauf ist als 64-Byte-Manifest eingefroren, wird aber noch nicht
ausgeführt. Offen bleiben die dynamischen topologieabhängigen Registerwerte,
deren rollback-fähige Kernelmediation, Falcon-Start, Channel-/Runlist-Bind,
USERD-Kick, IRQ, echter Fence und Capabilityfreigabe.
`R2.2r-nvidia-gk208-gr-execution-image` kompiliert den vollständigen
Pre-Start-Ablauf nun in ein versiegelbares, weiterhin hardwareinaktives
Abbild. Ein 64-Byte-Header schützt höchstens 2048 feste 16-Byte-Operationen mit
Topologie- und Operations-CRC. Darin liegen die expandierten statischen
Pakete, dynamische Copy/Mask-Abhängigkeiten, die exakt abgeleitete
Tile-/ZCULL- und Exception-Konfiguration, sämtliche 15 nutzbaren
LTC-/PGRAPH-ZBC-Farb- und Tiefenslots sowie alle fünf Kontextgruppen mit
HUB-Start, Readiness und Kontextgrößen-Readback. Die beiden von Nouveau
verlangten MMU-Faultbuffer bleiben getrennte, typisierte und ungelöste
128-KiB-Geräte-VRAM-Offsets. Ring 3 staged nur die verwendeten Bytes ab
`0x72000`, liest sie vor dem vorhandenen DMA-Seal vollständig zurück und führt
keine der Operationen aus. Offen bleiben eine validierte FB-/LTC-Basis, die
begrenzte VRAM-Reservierung und eine atomare Kerneltransaktion mit Deadline
und GR-Reset-Rollback; danach erst dürfen Falcon-Start/Readiness und später
Channel, Runlist, USERD, echter Fence und Capabilityfreigabe folgen.
`R2.2s-nvidia-gk208-gr-prerequisites` schließt jetzt die FB-/LTC-Prüfung und
die hardwareinaktive VRAM-Planung. Append-only Kommando 22 akzeptiert nur die
versiegelte Ausführungsabbildung derselben Generation, validiert sie samt
zweifach stabiler Live-Topologie erneut und ermittelt über feste
GK104/GK208-Register Gesamt-VRAM, aktive LTCs und den Nouveau-konformen
Tag-RAM-Bedarf. Das exakte Profil bestimmt den VRAM-BAR anhand des validierten
VBE-Scanouts statt anhand einer angenommenen BAR-Nummer. Zwei getrennte,
128-KiB-ausgerichtete Faultbuffer und der Tag-Bereich werden hinter dem
sichtbaren Scanout logisch reserviert, bleiben für Ring 3 jedoch vollständig
opak. Es erfolgen noch keine VRAM-/MMIO-Schreibzugriffe; Falcon, Channel,
Runlist, USERD, Busmaster, IRQ und Capabilitybits bleiben inaktiv. Der nächste
Slice ist die atomare kernelmediierte LTC-/Resolve-/GR-Ausführung mit Deadline,
GR-Reset-Rollback sowie FECS-Readiness- und Kontextgrößenprüfung.
`R2.2t-nvidia-gk208-gr-execution` führt diesen Slice nun über append-only
Kommando 23 aus. Vor dem ersten Hardwareeffekt werden das versiegelte Abbild,
die zweimal stabile Live-Topologie und der opake VRAM-/LTC-Plan erneut geprüft.
Der Kernel nullt ausschließlich die beiden reservierten 128-KiB-Faultbuffer,
programmiert die gepinnten GK104-LTC-/CBC-Register und interpretiert danach den
vollständigen typisierten GR-Ablauf mit streng gerahmten Kontextgruppen. Alle
CBC-, Idle- und FECS-Waits geben den Prozessor frei und sind zusätzlich durch
eine gemeinsame monotone 5-s-Deadline begrenzt. Nur Readiness, Operationszahl
und eine nichtleere Kontextgröße werden zurückgegeben. Teilfehler setzen GR vor
Retry/Fence zurück; Channel, Runlist, USERD, Busmaster, IRQ, Submission, Fence
und NVIDIA-Capabilitybits bleiben weiterhin gesperrt.
`R2.2u-nvidia-gk208-context-memory-plan` schließt nun die folgende
Speicherabhängigkeit. Der Ring-3-Compiler und append-only Kommando 24 berechnen
unabhängig 32-KiB-Pagepool, 12-KiB-Bundle, den exakten
`0x20 * 0xb23 * TPC`-Attributpuffer sowie 512 KiB CB-Reserve plus ausgerichtete
FECS-Kontextgröße. Ring 0 prüft Abbild, Live-Topologie und Kommando-23-Zustand
erneut und reserviert nicht überlappende Bereiche nur innerhalb des bereits
geclippten opaken VRAM-Fensters. Keine Adresse wird veröffentlicht und weder
VRAM noch MMIO werden geschrieben. Golden-Save, Channel-Bind, Runlist, USERD,
Submission, Fence und Capabilityfreigabe bleiben offen.
`R2.2v-nvidia-gk208-golden-context-plan` schließt jetzt den letzten
hardwareinaktiven Golden-Context-Vertrag. Der gepinnte Generator übernimmt
zusätzlich 245 ICMD- und 311 klassengebundene MTHD-Tupel mit festen CRC32-
Werten. Ein topologiegebundener Compiler ordnet Pagepool, Bundle, Attribut- und
temporären Kontextpuffer als vier 4-KiB-GPU-VA-Spannen innerhalb derselben
128-MiB-Small-Page-Table an und erzeugt die exakten GK104/GF100/GF117-Patches
in fester Kapazität; beim maximalen 32-GPC-Profil sind es 80 von 96 Einträgen.
Zwölf Phasen decken den vollständigen Ablauf bis FECS-Bind, Golden-Save und
Retain ab. Der NVIDIA-Dienst prüft diesen Plan vor Kommando 24, verändert aber
weder DMA-Pool noch PTE, VRAM, MMIO oder FECS. Offen ist damit die unabhängige,
deadlinebegrenzte Kernel-Ausführung mit GR-Reset-Rollback; Channel und
Capabilityfreigabe bleiben danach weiterhin getrennte Gates.
`R2.2w-nvidia-gk208-golden-context-execution` führt den versiegelten Plan nun
über append-only Kommando 25 tatsächlich aus. Der Kernel validiert eigene
immutable Tabellenkopien (199 Kontext-, 245 ICMD-, 311 MTHD-Tupel), baut eine
rein VRAM-interne temporäre Instance/PGD/PGT-Domäne und speichert den von FECS
erzeugten Golden Context unter einer gemeinsamen Fünf-Sekunden-Deadline. Nach
Erfolg sind Instance-Bindung und temporäre Seitentabelleneinträge gelöscht;
das opake Kontextabbild bleibt mit CRC erhalten. Teilfehler setzen GR zurück.
PCI-Busmastering, IRQ, Channel, Runlist, USERD, Submission, Fence und NVIDIA-
Capabilitybits bleiben weiterhin null; der nächste Hardwareabschnitt ist das
getrennte Channel-/Fence-Gate.
Der im ersten ASUS-Lauf beobachtete Fehler `SVGA2D-Service status=-19` ist im
Folgepaket `R2.2a-nvidia-vbe-fallback` behoben: Ein fehlender oder noch nicht
bereiter Beschleunigungsdienst löst jetzt eine ausdrückliche VBE-Reaktivierung
mit Software-Rendering aus. Veraltete Boot-Framebuffer-Metadaten können diese
Modusumschaltung nicht mehr überspringen.

Das abgeschlossene `R2.2b-desktop-startup-splash` beseitigt den schwarzen
Übergang zwischen Grafikaktivierung und erstem Desktop-Frame. Der Desktop
zeigt sofort einen dunklen REIST-OS-Fallback und lädt danach das feste 512x288-BMP aus dem
vollständigen Systemimage. Decoder-, Pixel- und Fontdaten verwenden in dieser
Startphase überlappungsfreie Bereiche bereits vorhandener, fest begrenzter
Puffer; Heap-Allokation oder ein Kernel-Bildparser kommen nicht hinzu. Fehlt
die Ressource oder ist sie ungültig, startet der Desktop mit dem sichtbaren
Textfallback weiter. Die Rettungsdiskette bleibt unverändert klein.

Der nachgereichte ASUS-Bootnachweis identifizierte den nächsten Fehler exakt:
Die passive GK208-Probe liest `10DE:1280`, 16 MiB BAR0 und den PTIMER korrekt,
aber `nvidia-gk208-ring3` überschreitet einschließlich NUL die bisherige
16-Byte-Supervisor-Namenskapazität. Deshalb entsteht vor dem Treiber-Spawn
`REIST_VIDEO DRIVER_DEGRADED result=-36`. Das abgeschlossene `R2.2c` hebt den
gemeinsamen festen Namenspuffer auf 32 Bytes an, bewahrt die Nicht-Trunkierung
und prüft weiter die 64-Byte-Grenze des geschützten Deskriptors. Die danach
sichtbaren ANSI-Sequenzen im VFS-Status entfernt das abgeschlossene `R2.2d`
durch eine portable Klartextzeile für serielle und Framebuffer-Ausgabe; Mount-
und Fehlerzahlen bleiben erhalten. Die absichtlichen
Crash-, Hang- und Invalid-Reply-Proben mit abschließendem
`RECOVERY_SEQUENCE_OK` sind weiterhin erwartete Selbsttests.

Der zweite ASUS-Lauf erreichte anschließend `NVIDIA_GK208_READY`, zeigte den
Splash und verlor den sichtbaren VBE-Modus erst bei zwei überwachten
NVIDIA-Treiberrestarts. Das abgeschlossene `R2.2e` trennt deshalb
Gerätemodusbesitz von passiver VBE-Nutzung: VMware deaktiviert seinen
SVGA-Scanout beim Fence weiterhin;
GK208 behält den kernelverwalteten VBE-Scanout, durchläuft aber unverändert
Quieszenz, Generation-Fence, Reap und Device-Recovery. `DESKTOP_OK` bleibt
damit nach einem Restart sichtbar statt nur im VGA-Textmodus zu erscheinen.
Die reproduzierte Restart-Serie während des Fontladens wurde zunächst durch
feste 24-KiB-Leseabschnitte mit Scheduling-Punkten behoben. Der nachfolgende
Bulk-Dateipfad ersetzt diese Übergangslösung inzwischen ohne die künstliche
Timerlatenz. Die gezielten Tests, beide
Framebuffer-Paketbuilds und der QEMU-Runtime-Lauf bis `TEST_OK` sind grün; der
abschließende Sichtnachweis auf dem ASUS-System bleibt manuell.

Der anschließende ASUS-Test meldete `/trash` als nicht vorhanden. Ursache war
kein Desktop-Pfadfehler, sondern die Ableitung des nativen FAT32-Verzeichnisbaums
ausschließlich aus Dateien: Ein leer ausgelieferter Papierkorb erzeugte daher
keinen Eintrag und war von einem erfolgreichen Laufzeit-`mkdir` abhängig.
`R2.2f` provisioniert `/trash/files` und `/trash/info` nun als echte leere
Verzeichnisse in jedem nativen HDD-Image. Pfad-, Tiefen-, Slot- und
Kollisionsgrenzen gelten unverändert; das Floppy-Image bleibt unverändert.
Die Layout-, Native-Image- und Papierkorbtests sowie beide vollständigen
Framebuffer-Paketbuilds bestehen. Der Ersatzimage-Test auf ASUS bleibt
manuell.

`R3.3-imageviewer-bulk-io` beseitigt den ungeeigneten Legacy-Lesepfad des
Bildbetrachters. `IMAGEVIEWER.PRG` verwendet nun ausschließlich das
generationgebundene Ring-3-Dateiobjekt und liest seinen festen 1-MiB-Puffer in
höchstens acht CRC-geschützten 128-KiB-Bulk-Requests. Die weiterhin genau zwei
kernel-eigenen Slots belegen zusammen fest 256 KiB. Ein requestlokaler
FAT-Sektorcache verhindert wiederholte physische FAT-Zugriffe; der Hosttest
liest 128 KiB am Ende einer synthetischen 1-MiB-FAT32-Datei mit höchstens 280
Sektorzugriffen. Verzeichnisgrenzen, EXT2-Verhalten und Decodergrenzen bleiben
unverändert. Die gezielten Tests und beide vollständigen Framebuffer-
Paketbuilds sind grün; die Queue ist wieder leer.
Der anschließende reale Desktop-Test bestätigt, dass ein Bild nach dem Klick
nun praktisch sofort geladen wird; das ursprünglich beobachtete mehrsekündige
Ladeproblem ist damit auch außerhalb der Hostmessung behoben.

`R3.3a-desktop-startup-bulk` stellt die Desktopressourcen auf denselben
generationgebundenen Ring-3-Bulkpfad um. Splash, Icons und
Dateitypzuordnungen werden ohne künstliche Millisekundenpause in höchstens
128-KiB-Transfers geladen. Der 2,5-MiB-Voll-Unicode-Font wird beim normalen
Start nicht mehr vorsorglich gelesen: Der eingebaute VGA-Font deckt die
Startoberfläche ab, während der vollständige Font beim ersten Zeichen außerhalb
dieses Satzes einmalig geladen und validiert wird. Ein fehlgeschlagener Versuch
behält den sichtbaren eingebauten Fallback und wird nicht bei jedem Redraw
wiederholt. Der Unicode-Probelauf lädt ihn
weiterhin zwingend. Wiederholte QEMU-Messungen reduzierten
`DESKTOP_STARTUP_MS` von 8280 ms auf 1907 bis 1984 ms, also um rund 76 bis
77 Prozent. FAT-
Dateiinhalte dürfen bis zu 6400 Cluster besuchen; eine konstante
Brent-Zykluswache hält den Userspace-Stack unabhängig von der Dateigröße klein.
Die 128-Cluster-Grenze für Verzeichnisse und das 320-Sektor-Budget pro Operation
bleiben unverändert.

`R3.4-notepad-scrollbars` ergänzt den REIST Editor um horizontale und
vertikale Scrollleisten. Der öffentliche Texteditor liefert dafür begrenzte
Viewport-Metriken und setzt einen geklemmten Ursprung, ohne Cursor, UTF-8-
Dokument oder Dirty-State zu verändern. Der Editor zeichnet klassische
Pfeilfelder, Seitentracks und proportionale Thumbs; Maus-Drag, Laden,
Editieren, Cursor-Navigation und Fenster-Resize halten beide Achsen synchron.
Dokument- und Eingabekapazitäten sowie der heapfreie Ring-3-Vertrag bleiben
unverändert.

`R3.1-unicode-text-raster` ersetzt die byteweise grafische Textausgabe durch
einen vollständig vorvalidierten, auf 256 Bytes begrenzten RFC-3629-Lauf. Die
unveränderte Display-v1-ABI zählt weiterhin Bytes; Rasterposition, Clipping und
Damage zählen Unicode-Skalarzellen. Eine reproduzierbare Tabelle bildet alle
im vorhandenen IBM-PC-8x16-Font darstellbaren Unicode-Zeichen auf CP437 ab.
Jeder andere gültige Skalar bis U+10FFFF erhält genau eine sichtbare
Ersatzglyph; fehlerhafte Folgen scheitern vor Frame-Reservierung und
Pixelwirkung. Der Desktop trennt beim Begrenzen keine UTF-8-Sequenzen mehr.
Breite Fontabdeckung, Shaping, Bidi, Grapheme und IME bleiben ausdrücklich
offen und gehören nicht als komplexe Parser in Ring 0.

`R3.2-ring3-psf2-font-fallback` ergänzt darauf einen öffentlichen,
heapfreien PSF2-Decoder in `libreistgui`. Er validiert die vollständige, auf
4 MiB begrenzte Datei samt Unicode-Tabelle vor Publikation und baut seinen
Index ausschließlich in caller-owned Festkapazität auf. Der reproduzierbare
Referenzfont bewahrt die 256 IBM-PC-Glyphen und ergänzt U+20AC als echte
Erweiterungsglyph. Der Desktop lädt ihn einmalig, behält den schnellen
Kernel-Textlauf bei und überlagert nur Erweiterungen über geclippte
Ring-3-XRGB-Uploads. Fehlende oder ungültige Fontdaten lassen den bisherigen
sichtbaren Ersatzpfad aktiv; breite Schriftpakete und komplexes Textlayout
bleiben offen.

`R3.2-unifont-bmp-coverage` erweitert die Kaskade um alle 57.086 eindeutigen
BMP-Abbildungen der offiziellen GNU-Unifont-16.0.04-HEX-Quelle. Quelle,
SHA-256, OFL-1.1-Lizenz und Generator sind im Repository fixiert. Das
abgeleitete PSF2 ist rund 1,14 MiB groß; native 8x16-Glyphen bleiben
unverändert und 16x16-Glyphen werden deterministisch durch OR-Verknüpfung
benachbarter Spalten verdichtet. Der Desktop reserviert feste 2 MiB Fontdaten
und 65.536 Indexeinträge, zeichnet ASCII/CP437 weiterhin mit einem Kernel-Lauf
und lädt nur dort fehlende BMP-Glyphen geclippt aus Ring 3 nach. Das ist breite
Standalone-BMP-Glyphauswahl, aber noch kein Supplementary-Plane-Font, Shaping,
Bidi-, Graphem- oder IME-Vertrag.
Der große Font wird in höchstens 65.536 Byte großen Read-Syscalls geladen und
nach jedem erfolgreichen Abschnitt explizit an den Scheduler abgegeben. Das
reduziert den Start von rund 280 auf etwa 18 Scheduling-Abschnitte, während der
synchron serialisierte VFS-Pfad weiterhin nie die gesamte Ressource in einem
einzigen Aufruf hält.
`R3.2-unifont-supplementary-coverage` ersetzt dieses BMP-Artefakt durch die
gepinnten 126.086 eindeutigen Abbildungen der offiziellen GNU-Unifont-16.0.04-
All-Quelle. 60.518 Abbildungen liegen in der BMP und 65.568 darüber. Das rund
2,47 MiB große PSF2 bleibt unter einer festen 3-MiB-Dateigrenze; der Desktop
verwendet einen caller-owned 262.144-Slot-Index mit weniger als halber Last und
weiterhin 64-KiB-Leseabschnitte mit Schedulerabgabe. Der Gastprobe weist 🚀 als
echte Supplementary-Glyphe sowie U+10FFFD als weiterhin sichtbaren Fallback
nach. Das vervollständigt nicht Shaping, Bidi, Combining-Positionierung,
Graphemnavigation oder Eingabemethoden.
Eine nackte Escape-Taste beendet die grafische Sitzung nicht mehr. Sie bleibt
lokaler Abbruch für Drag, Menü, Dialog und fokussierte Surface-Anwendungen. Der
kontrollierte Desktop-Exit erfolgt ausschließlich über `Desktop beenden` im
Startmenü und stellt den vorherigen Anzeigemodus weiterhin validiert wieder her.
Der Runtime-Probe bindet die Anzeige erst nach dem vollständigen Fontload und
wartet dabei höchstens zwei Sekunden auf eine neue freigegebene
SVGA2D-Servicegeneration. Dadurch kann ein gleichzeitig überwachter
Treiberneustart keine alte Endpoint-Fähigkeit bis zur Pixelwirkung tragen.
Die Datei `/usr/share/fonts/unicode.txt` liegt im vollständigen HDD-Image und zeigt
die BMP- und Supplementary-Abdeckung mit realen lateinischen, griechischen, kyrillischen,
hebräischen, arabischen, Devanagari-, CJK- und Hangul-Zeichen. Offen gebliebene
Combining-, Bidi- und Shaping-Fälle sind darin ausdrücklich
markiert, damit Ersatzdarstellung nicht mit fertigem Layout verwechselt wird.
Der REIST Editor akzeptiert diese Datei nun als vollständig validiertes
RFC-3629-UTF-8. Sein fester Puffer speichert weiterhin Bytes, während Cursor,
Viewport und Statusspalte Unicode-Skalare zählen. Laden, Bearbeiten,
horizontales Clipping und Speichern trennen keine Mehrbytefolge; ungültige
Kodierungen und Steuerzeichen werden vor Dokumentmutation abgelehnt.

`R2.2-unicode-nfc-casefold` ergänzt vollständige kanonische
Unicode-15-Dateinamenidentität. Ein reproduzierbarer Generator fixiert 2.061
kanonische Zerlegungen, 1.530 Default-Casefold-Abbildungen, 922
Combining-Class-Einträge und die zulässigen Kompositionspaare. Der heapfreie
Laufzeitpfad führt Full Case Folding, rekursive Zerlegung, stabile kanonische
Sortierung und NFC-Komposition einschließlich Hangul für alle gültigen
Skalarwerte bis U+10FFFF aus. Aus der 255-Byte-Komponentengrenze sind 382
Zwischenskalare und 763 Schlüsselbytes fest hergeleitet. Originalnamen bleiben
auf dem Medium und in Readdir unverändert; nur Lookup-Identität wird
normalisiert. Das schließt nicht automatisch Font-Fallback, Glyphenabdeckung,
Bidirektionalität, Eingabemethoden oder Graphemnavigation der GUI ein.

`R2.2-vfat-utf8-roundtrip` ersetzt den bisherigen ASCII-only-LFN-Pfad durch
eine gemeinsame heapfreie RFC-3629-/UTF-16-Konvertierung. FAT32 zählt Slots
nach UTF-16-Codeunits, schreibt und liest BMP-Zeichen sowie gültige
Surrogatpaare und verwirft überlange UTF-8-Sequenzen, Surrogatskalare,
unvollständige Paare, verbotene FAT-Zeichen und Kapazitätsüberschreitungen vor
Namespacewirkung. Der unabhängige Ring-3-FAT-Parser verwendet dieselben festen
Codecgrenzen. Die bestehende 256-Byte-Pfad-ABI bleibt unverändert. Nicht-ASCII
wird über Unicode-15-NFC und vollständiges Default Case Folding verglichen.

`R2.2-fat-timestamp-completion` schließt die verbliebene FAT12-Lücke im bereits
öffentlichen Zeitstempelvertrag. Neue Dateien, Verzeichnisse sowie `.`/`..`
erhalten Create-, Write- und date-only Access-Felder vor ihrer Publikation;
erfolgreiche Inhalts- und Größenänderungen tragen mtime im selben
Directory-Write nach. `touch` bewahrt Create-Felder, Inhalt und Identität und
setzt mtime sowie date-only atime. FAT32s bestehender Pfad ist durch denselben
Hostvertrag abgesichert. FAT-Kalender bleiben lokale, nicht näher
spezifizierte Zeit mit Zwei-Sekunden-mtime und Tages-atime; Reads und
Metadatenabfragen verursachen keine Medienwrites.

`R2.2-fat32-ata-image-fault-campaign` schließt die zweite Hälfte der
vollständigen Sektorwrite-Fehlermatrix. Der unveränderte Journal-v2-Datensatz
liegt jetzt in einem festen transportneutralen Kern, den ATA/AHCI und der
FAT32-Hostharness gemeinsam verwenden. Bis zu 20 Zielsektoren werden nach
persistiertem Undo und `ACTIVE` in fester Pending-Ablage zusammengeführt;
transaktionsinterne Reads sehen den letzten Stand, physisch wird beim Commit
nur die endgültige Fassung unter Storage-Supervision publiziert. Eine echte
0→700-Byte-VFS-Erweiterung wird nach jedem gemessenen Rohwrite gekappt. Der
Abbildprüfer verlangt vor Recovery alte oder finale ganze Nichtjournalsektoren
und nach frischem Mount ein vollständig altes oder neues Image einschließlich
Nullbytes, Kette, FAT-Spiegel und unabhängiger Datei; echte Headerambiguität
bleibt separat fail-closed. Es gibt keinen Produktions-Fault-Hook und keine
Format- oder ABI-Änderung.

`R2.2-fat12-image-fault-campaign` ergänzt die bisherige isolierte
Journal-Fehlermatrix um eine vollständige VFS-Transaktion. Eine gesunde
Cross-Cluster-Erweiterung bestimmt zuerst ihre tatsächliche, fest auf 384
begrenzte Zahl von Sektorwrites. Aus demselben Nullgrößen-Basisabbild wird dann
nach jedem einzelnen abgeschlossenen Write der Hosttransport abgeschaltet.
Der Abbildprüfer lässt außerhalb des Journals nur ganze alte oder finale
Sektoren zu. Erfolgreiche frische Mounts müssen vollständig alten oder neuen
Zustand einschließlich Nullbytes, Kette, FAT-Spiegel und unabhängiger Datei
zeigen; echte Headerambiguität zählt separat als fail-closed Ablehnung. Es gibt
keinen Produktions-Fault-Hook.

`R2.2-open-namespace-locks` schließt die erste offene Handle-Lücke der FAT-
Namespace-Mutation. Bis zu 256 Nicht-Root-Nodes werden statisch registriert.
Vor `unlink`, `rmdir` und beiden Seiten von `rename` vergleicht VFS die aktuelle
FAT12-Directory-Sektor/-Slot- beziehungsweise FAT32-Elterncluster-/Kurznamen-
Identität mit den offenen Nodes. Treffer liefern `BUSY` vor Wirkung, auch über
Groß-/Kleinschreibungs- oder VFAT-Aliase. Nur ein erfolgreiches Backend-`close`
gibt Registrierung und Mountzähler frei; Unmount bleibt bei offenen Nodes
gesperrt. Eine POSIX-artige Unlink-while-open-Lebensdauer wird nicht behauptet.

`R2.1-open-flags-rights` hängt Syscall 120 für POSIX-nahe Open-Modi an. Neue
Deskriptoren erhalten exakt angeforderte READ-/WRITE-Rechte; `CREAT` prüft die
feste Slotquote vor Namespace-Wirkung und `APPEND` bestimmt den Schreiboffset
für jeden Aufruf neu. Die alten Open-/Create-Syscalls bleiben unverändert.
`TRUNC` wurde dabei zunächst als ABI-Flag reserviert und bis zum getrennten
FAT12-/FAT32-Transaktionspaket wirkungslos mit `ENOTSUP` abgewiesen.

`R2.1-fat-truncate-zero` aktiviert dieses Flag jetzt für journalmarkierte
REIST-FAT12-/FAT32-Dateien. FAT12 journalisiert beide FAT-Kopien und den
Directory-Sektor gemeinsam; FAT32 trennt den Nullgrößen-Eintrag vor Freigabe
der alten Clusterkette. `O_TRUNC` verlangt Schreibrechte und läuft vor
Descriptorpublikation. EXT2, fremde/read-only Medien und kritische
FAT12-Replikate bleiben ausdrücklich nicht unterstützt.

`R2.1-fat-ftruncate` ergänzt append-only Syscall 123 und erweitert die
node-basierte Operation auf beliebige 32-Bit-Ziellängen innerhalb von
Mediengeometrie und festem Transaktionsbudget. FAT12 prüft freien Platz und
den vollständigen 64-Sektor-Undo-Umfang vor Wirkung; Schrumpfen, Nullung und
Directorypublikation bleiben eine Transaktion. FAT32 nullt Erweiterungen bei
noch alter sichtbarer Größe und publiziert erst danach; beim Schrumpfen wird
zuerst das sichere logische Präfix publiziert und anschließend der private
Kettensuffix freigegeben. `ftruncate` verlangt einen schreibbaren regulären
Deskriptor und verändert dessen Offset nicht. EXT2 liefert `EROFS`.

`R2.1-descriptor-seek-fstat` ergänzt append-only Syscalls 121/122 für
`lseek` und `fstat`. Alle Berechnungen verwenden signierte 64-Bit-
Zwischenwerte; negative Positionen, 32-Bit-Überläufe und nicht seekbare
Terminal-/Socket-Deskriptoren ändern den gespeicherten Offset nicht. Positionen
hinter EOF sind bis `INT32_MAX` zulässig und nutzen beim folgenden FAT-Write
den vorhandenen Vertrag für genullte Lücken. `fstat` fragt ausschließlich den
geöffneten VFS-Node ab: FAT12 revalidiert Directory-Sektor und Slot, FAT32 die
gehaltene Directory-Identität und EXT2 die Inode-Nummer. Eine erneute
Pfadauflösung oder zusätzliche Mutationsautorität entsteht nicht.

`R2.1-standard-descriptors` erweitert die bestehende feste Prozesstabelle um
echte Deskriptoren 0/1/2 mit richtungsgebundenen Terminalrechten. Der
Tastaturpfad bleibt bis zu einer eigenen TTY-/Deadline-ABI nichtblockierend;
`dup`, Pipes und Spawn-Vererbung waren nicht Teil dieses Schnitts.

`R2.1-shared-syscall-abi` ersetzt die doppelte Kernel-/SDK-Pflege der
Syscallnummern durch einen versionierten gemeinsamen Header und deterministisch
geprüfte Kompatibilitätsprojektionen. Der Schnitt änderte weder Nummern noch
Dispatchersemantik; Make- und Windows-Build brechen bei Drift vor der
Kompilierung ab.

## Arbeitscheckpoint 16. August 2026

Dieser historische Hardware-Checkpoint basiert auf Commit `0a2c08e`. Das reale
SATA-Hotplug-Szenario wurde auf Zielhardware erfolgreich durchgeführt:

- `SATAWR.PRG` schrieb synchronisierte Testdaten, während die System-HDD
  abgezogen und wieder angeschlossen wurde.
- Der Storage-Service erkannte den I/O-Fehler, quarantänisierte die
  AHCI-Elternressource und setzte Systemvolume und Treiber fail-closed auf
  read-only.
- Nach Reconnect liefen begrenzter AHCI-COMRESET, IDENTIFY, frische
  Medienidentitätsprüfung und Undo-Journal-Recovery erfolgreich durch.
- Eine verwaiste Schreiboperation wird erst nach erfolgreicher
  Journal-Recovery ressourcengebunden beendet. Die Supervisor-IDLE-Meldung ist
  idempotent, wodurch Storage- und Filesystem-Fences wieder freigegeben werden.
- Die reale Ausgabe erreichte `RESOURCE_REINTEGRATED_RW 0`; das Volume wurde
  wieder beschreibbar und der Anwender bestätigte den erfolgreichen Lauf.
- `DRIVES.PRG` übernimmt für Partitionen den Zustand der Blockgeräte-
  Elternressource und zeigt `READY`, `READONLY`, `DEGRADED`, `QUARANTINED`,
  `RECOVERING`, `OFFLINE` oder `UNKNOWN`.

Zugehörige Commits sind `fe53ff3`, `ad89fde`, `bf6d95b`, `f55a024` und
`0a2c08e`. Das zuletzt erzeugte reale Hardware-Image ist
`build/reist-os.img` mit Build-ID
`D531CB4F2886278DC31059E36BC0B91B1BCFC74B`; normaler SATA-QEMU-Gasttest und
Hosttests waren erfolgreich. Der nächste Arbeitstag setzt bei der aktiven
Paketqueue in `automation/reist-s03b.toml` fort. Der reale Hotplug-Lauf ist
positive Hardwareevidenz für diesen getesteten Aufbau, aber keine allgemeine
SATA-Hardwarefreigabe.

## Verifizierter Systempfad

- eigener BIOS-/MBR-Bootloader mit Manifest-v3-, ELF32-, SHA-256- und
  RSA-2048-PSS-Prüfung; ein unabhängiges Imagegate validiert das signierte
  Kernelartefakt zusätzlich; native HDD-Images besitzen feste Kandidaten A/B
- 32-Bit-i386-Kernel mit Paging, Ring-3-Prozessen, präemptivem Scheduler,
  endlichen Waits und versionierter Syscall-/MYPR-ABI
- inkrementeller Windows-Build für `qemu`, `vmware` und `real_hw`
- QEMU-Regressionspfad über ATA/IDE sowie expliziter AHCI/SATA-Gastlauf
- generiertes VMware-Paket mit persistenter SATA-VMDK an `sata0:0`
- physischer BIOS-Boot über SATA sowie reale PS/2-Eingabe wurden auf
  Zielhardware beobachtet; die Hardwarematrix bleibt klein
- deterministische Rootauswahl: BIOS-Bootdiskette oder genau eine strukturell
  gültige FAT32-Partition mit Label `X86 SYSTEM`
- VFS mit FAT12, FAT32 und EXT2 sowie DOS-artiger Ring-3-Shell
- E1000, RTL8139, RTL8168/8111G und NE2000 hinter einer gemeinsamen
  Netzgeräteschicht
- VGA-Text als Standard und optionaler VBE-/VMware-SVGA-II-Framebuffer mit
  Ring-3-Desktop; der SVGA-II-Bootselbsttest gibt die Anzeige vor der Shell
  zurück und der Desktop deaktiviert sie beim Sitzungsende über denselben
  generationgebundenen Treiberkanal

Der SATA-Pfad leitet partition-relative Zugriffe anhand des Elterntransports an
AHCI statt an den ATA-PIO-Kompatibilitätspfad weiter. Der vollständige
QEMU-SATA-Gastlauf erreicht `FILE_IO_OK` und `TEST_OK`; zusätzlich wurde die
Abzieh-/Reconnect-Recovery des oben genannten Builds auf einer realen
Zielmaschine erfolgreich beobachtet. Weitere Zielmaschinen bleiben jeweils
eine eigene Hardwareabnahme.

## REIST-Ausbaustand

Die High-Assurance-Arbeit folgt dem Ablauf Detect, Contain, Recover, Validate
und Reintegrate. Bereits vorhanden sind unter anderem:

- begrenzte monotone Deadlines für IPC, Treiber- und Dienstoperationen
- geschützte, redundante kritische Steuerobjekte mit Integritätsprüfung
- überwachte Ring-3-Dienste mit generationgebundener Revocation und Recovery
- Crash-, Hang- und ungültige-Antwort-Proben für die Ring-3-Domäne
- fail-closed Storage-Quarantäne, Requalifizierung und Schreib-Fencing
- persistente Crashrecords und vorbereitete Supervisor-/Handover-Protokolle
- begrenzte Netzwerkparser, ARP-/IPv4-/ICMP-/UDP-/DHCP-Entscheidungen in der
  überwachten Ring-3-Domäne

Das automatisierte S0-Forschungsgate ist für die generische
`REIST-research`-Baseline auf QEMU i386 und VMware i386 abgeschlossen. S0.1
ist für diese explizit abgegrenzte Baseline abgeschlossen: Ein separates maschinenlesbares
Scope-Inventar bindet Systemgrenze, Essential Functions, Anforderungen,
Komponenten und Profilausschlüsse an das Gefahrenregister. Schema v2 prüft
vollständige Komponentenabdeckung und die SHA-256-Traceability reicht von
Anforderung über Design und Code bis Test und Ergebnis. Dieser Abschluss ist
keine Zertifizierung und keine Freigabe der ausgeschlossenen Referenzprofile
oder unqualifizierter Zielhardware.

S0.2 ist für die automatisierte QEMU/VMware-Forschungsbaseline abgeschlossen.
QEMU prüft den emulierten IB700-Watchdog; VMware bootet das frisch erzeugte
disponible Build-Paket unter festen Fristen und verlangt fail-closed das fehlende externe
Backend, überwachte Probe-Recovery, `BOOT_OK` und die Ring-3-Shell. Das
maschinenlesbare physische Profil bleibt `unbound` und kann nur mit eindeutig
gebundener Ziel-/Monitor-/Firmwareidentität, separater Strom- und Zeitbasis,
unabhängigem Reset, latched Safe-State-Ausgang, elektrischem Sense-Readback
und gehashten physischen Fault-Injection-Berichten auf `qualified` wechseln.
Diese Realhardwareprüfung führt der Benutzer manuell durch; es entsteht kein
automatischer Zielhardware- oder Fail-operational-Claim. Das physische und
produktbezogene Gesamtgate bleibt deshalb offen.

`S0.3c-layout1` mit kleingeschriebener,
hierarchischer Systemprogrammablage ist umgesetzt. `S0.3c-admin2` mit statischer
Komponenten-Lifecycle-Steuerung und `S0.3c-admin1` mit capability-
gebundener Storage-Administration, `S0.3c-6f5` mit der FAT12-
Persistenz-Fehlermatrix und `S0.3c-hw11` mit begrenzter SATA-Hotplug-Recovery
sind abgeschlossen. Aussagen über vollständig nachgewiesene
Fail-Operationalität oder unabhängige Hardware-Failover-Domänen sind weiterhin
unzulässig.

Die ausführbare Paketqueue für S0.3c ist nach dem FAT12-Persistenzabschluss
abgearbeitet. Für S0.4 ist nun auch die feste, saturierende Scheduler-/INT-80-
Zeitdiagnostik samt maschinenlesbaren QEMU-/VMware-Regressionsgrenzen
umgesetzt. Sie ist ausdrücklich keine Zielhardware-WCET; diese Abnahme führt
der Benutzer manuell durch. Die automatisierte Abnahme vom 23. August 2026
bestand auf QEMU (maximal rund 0,613 ms Scheduler und 0,102 ms INT 0x80) sowie
VMware (rund 0,051 ms und 0,034 ms), jeweils mit null Zeitquellenanomalien und
deutlich unter der festen 10-ms-Grenze. Auf diesem Workstation-Host verwendet
die Automation den vom generierten Paket vorgesehenen GUI-Start, weil VIX den
Headless-Start mit `Unknown error` ablehnt; Markerprüfung und harter Stopp
bleiben automatisiert und begrenzt. Danach folgen S0.5 und S0.6. Ein externes
Monitorgerät samt Transport, eigener
Versorgung/Zeitbasis, Reset- und Interlockverdrahtung wird erst nach einer
manuellen Auswahl angebunden; ohne diese Identität wird kein Produktionstreiber
erfunden und keine Hardwarequalifikation behauptet.

`S0.4c-2b2c` ergänzt die Laufzeitdiagnostik des kernel-eigenen mediated-DMA-
Pools. Eine append-only 32-Byte-Struktur meldet aktive und maximale Belegung
der vier Slots, die 64-KiB-Standardgröße sowie saturierende echte
Kapazitätsablehnungen, ohne
physische Adressen oder Pooltokens offenzulegen. Hostseitig sind vollständige
Erschöpfung, `ENOSPC`, generationgebundene Freigabe, Wiederverwendung und die
Rückkehr auf null aktive Pools geprüft. Der QEMU-HDA-Treiber bestätigt seine
eigene gebundene Poolbelegung über einen maschinenlesbaren Marker im bereits
autorisierten, generationsgebundenen Supervisor-Diagnosekanal.

`S0.4c-3` begrenzt nun IRQ-Stürme pro aktiver Device-Domain auf 128
Aufnahmen in 100 ms. Die erste Überschreitung sowie eine rückwärts laufende
Device-Zeit fencen vor einer weiteren Ring-3-Benachrichtigung über den
vorhandenen vollständigen Mask-/Bus-Master-/DMA-Cleanup-Pfad. Eine Regression
der Scheduler-Abrechnungszeit verriegelt alle Ring-3-Klassen auch über spätere
Fensterwechsel hinweg; nur explizite Neuinitialisierung löscht den Fehler.
Die Grenzen stehen im Ressourcenregister, die Zähler saturieren, und ein
separater Compilezeit-QEMU-Build weist beide Guards vor `BOOT_OK` sowie
anschließenden normalen Ring-3-Fortschritt bis `TEST_OK` nach. Daraus folgt
keine Zielhardware-WCET- oder Hardwarequalifikationsaussage.

S0.5 umfasst nun die abgeschlossenen Pakete `S0.5a1`, `S0.5a2`, `S0.5a3a`,
`S0.5a3b`, `S0.5b1`, `S0.5b2`, `S0.5b3`, `S0.5b4` und `S0.5b5`. Die letzten fünf Pakete ergänzen
die redundante Bootstufe, deren transaktionalen Pending-Zustand und das
Ring-3-Erfolgs-Acknowledge. Das native
BIOS-Manifest v3 enthält SHA-256 und die
256-Byte-RSA-PSS-Signatur des exakten Kernelartefakts; Windows-
und Makefile-Builds validieren HDD- und Floppy-Images mit einem unabhängigen
Parser und brechen bei Versions-, Layout-, Bounds-, Prüfsummen- oder
Digestfehlern ab. Stage 2 berechnet SHA-256 und CRC32 mit festen Puffern in
einem begrenzten Kernel-Lesedurchlauf und stoppt einen Digestfehler vor dem
ELF-Parsing. Der negative QEMU-Nachweis hält CRC32 und Manifest-Prüfsumme trotz
Kerneländerung gültig und erreicht ausschließlich den SHA-Fehlerpfad.

`S0.5a3a` ergänzt eine hostseitige Kernelsignatur nach RFC 8017 mit
RSA-2048-PSS/SHA-256, MGF1-SHA-256 und 32-Byte-Salt. Windows- und Makefile-
Builds erzeugen eine feste 256-Byte-Signatur und prüfen sie unabhängig gegen
eine versionierte Policy sowie den gepinnten SHA-256-Fingerprint des Public
Keys, bevor das Image veröffentlicht wird. Die private Research-Testfixture
ist öffentlich und wird im Release-Modus abgelehnt. `S0.5a3b` bettet die
Signatur in Manifest v3 ein und prüft sie in Stage 2 mit festem Modulus,
Exponent 65537 und begrenzter RFC-8017-PSS-/MGF1-SHA-256-Logik vor dem
ELF-Parsing. Damit ist der Kernel relativ zur Stage-2-Vertrauensgrenze
authentifiziert. Stage 1 und Stage 2 bleiben auf dem beschreibbaren Medium
ersetzbar; Secure Boot, Anti-Rollback und ein unveränderlicher Plattformanker
werden weiterhin nicht behauptet.

`S0.5b1` legt im nativen HDD-Image Manifest A/B an den
partitionrelativen LBAs 0/96 und Kernel A/B an 128/3136 ab. Die 446-Byte-
MBR-Stufe lädt ohne Manifestparser die feste Stage-2-Reserve. Stage 2 startet
mit A und prüft B nach einem A-Fehler genau einmal vollständig und unabhängig.
Der Hostvalidator verlangt beide Kandidaten. Persistente Slotwahl,
Bootversuchszähler, Erfolgsbestätigung und atomare Updateumschaltung bleiben
offen; die Rescue-Diskette bleibt single-slot.

`S0.5b2` ergänzt zwei Boot-Control-Sektoren an den partitionrelativen LBAs
97/98 und einen Offline-Updater. Der Updater prüft den signierten ELF-Kernel,
schreibt ausschließlich Slot B und veröffentlicht Pending B erst nach
vollständiger Revalidierung. Stage 2 dekrementiert zwei Testboots persistent
vor der B-Ausführung und schreibt bei Erschöpfung oder B-Fehler Rollback auf A
zuerst. Die Kopien sind CRC-/sequenzgeschützt und werden ältere zuerst mit
Read-back aktualisiert.

`S0.5b3` ergänzt den CRC-geschützten Stage-2-Handoff an `0x4E00`. Der Kernel
kopiert ihn vor Allocator-Nutzung, löst genau eine MBR-`0xDA`-Bootpartition auf
und gibt append-only Syscall 117 erst nach `BOOT_OK` an die gebundene
Storage-Service-Generation frei. Der Ring-3-Dienst revalidiert Manifest,
Sequenz und beide Records, bestätigt B mit älterer Kopie, Flush und Read-back
zuerst und heilt benachbarte Kopien idempotent. Bestätigtes B startet direkt;
eine später ungültige B-Signatur führt erst nach persistentem Control-Commit
zurück zu A. QEMU deckt Bestätigung, Neustart und diesen Rollback ab.
Updateverteilung, unveränderliches Recovery und Anti-Rollback bleiben offen.

`S0.5b4` führt Boot-Control v2 append-only ein. v1 bleibt strikt auf
bestätigt A mit Pending B begrenzt; v2 darf nur den jeweils gegenüberliegenden
inaktiven Slot wählen. Der Offline-Updater schreibt und verifiziert Kernel und
Manifest von A oder B vollständig, bevor die ältere Control-Kopie Pending-
Autorität erhält. Stage 2 persistiert Dekrement und dynamischen Rollback vor
der Kandidatenausführung. Der unveränderte 64-Byte-Handoff bleibt read-only;
der generationsgebundene Ring-3-Storage-Dienst bestätigt nur `selected ==
pending != active` und validiert dabei das tatsächlich ausgewählte Manifest.
Hostseitige Power-Loss-Matrizen decken beide Richtungen ab; der persistente
QEMU-Lauf bestätigt B, aktualisiert danach A, bestätigt A und erhält den
bestehenden Rollback eines beschädigten bestätigten B.

`S0.5b5` ergänzt die hostseitige Offline-Verteilung als festes binäres
REIST-Update-Bundle v1. Sein 512-Byte-Header bindet exakte Gesamt-/Kernelgröße,
RSA-PSS/SHA-256-Algorithmus, Kernel-Digest, 256-Byte-Signatur und den lokal
gepinnten SPKI-Fingerprint; Flags und Reserven müssen null sein, CRC32 erkennt
Transportkorruption. Der Producer prüft ELF, Policy und Signatur vor atomarem
Publish. Ein strukturell unabhängiger Consumer begrenzt das gesamte Bundle auf
die feste Slotkapazität, verwirft Truncation und Nachlaufdaten und authentifiziert
erneut, bevor der bestehende A/B-Updater ein Output-Image erzeugen darf. Der
persistente QEMU-Lauf nutzt dasselbe Bundle für A nach B und B nach A.
Online-Verteilung, TUF-/Uptane-Metadaten, unveränderliches Recovery,
Release-Key-Verwahrung und Anti-Rollback bleiben ausdrücklich offen.

`S0.3c-admin1` stellt sichere Storage-Operationen (`device down/up`, `mount`,
`umount`) und einen festen, integritätsgeprüften 272-KiB-RAM-Rescue-Pool mit
112 KiB Einzelgrenze aus
Shell, Anzeige-, Diagnose-, Dienst- und Adminprogrammen bereit. `S0.3c-admin2`
ergänzt eine statische, abhängigkeitsbewusste Lifecycle-Steuerung für
ausdrücklich unterstützte Treiber und überwachte Dienste. Der reale QEMU-Lauf
weist geschützte Kernkomponenten, Abhängigkeitsreihenfolge sowie Down/Up und
Restart der unterstützten Komponenten nach. Ein universelles dynamisches
Entladen von Kernel-Treibern ist nicht vorgesehen.

## Storage und Dateisysteme

### Blockgeräte

- ATA-PIO unterstützt Legacy- und begrenzt erkannte PCI-IDE-Kanäle.
- AHCI erkennt PCI-Klasse `01/06/01`, validiert BAR5 und verwendet feste,
  adressgeprüfte DMA-Strukturen mit endlichen Deadlines.
- MBR-Partitionen werden als eigene Child-Ressourcen veröffentlicht.
- ATA, AHCI, Partitionen und FDD verwenden die gemeinsame Blockgeräteschicht.
- Der Storage-Service vermittelt generationgebundene Requests, Quarantäne,
  Requalifizierung, Flush, Schreib-Fencing sowie FAT12-/FAT32-Formatierung.
- Der erste VFS-Migrationspfad transportiert `stat` als exakt 512 Byte großen,
  voll-duplexen Shadow-Frame zum Storage-Service. Der normale QEMU-Gast
  vergleicht Typ, Größe und Zeitfelder mit dem weiterhin autoritativen Kernel-
  VFS und publiziert `STORAGE_VFS_SHADOW_STAT_OK`. Der Dienst erhält dafür nur
  `SYS_STAT`.
- Der zweite Shadowmodus parst den längsten Mountpräfix, vollständig geprüfte
  FAT12-/FAT32-BPBs, ASCII-8.3-/VFAT-Namen, die feste FAT12-Rootdirectory und
  begrenzte Verzeichniscluster im
  Ring-3-Storage-Service selbst. Maximal 64 vermittelte Sektorreads und feste
  Stackpuffer begrenzen die Arbeit. Nur eine bytegenaue Übereinstimmung mit dem
  Legacy-`SYS_STAT` wird publiziert; der QEMU-Gast markiert dies mit
  `STORAGE_VFS_FAT32_PARSER_OK`. Für bestehende Clients bleibt der Kernel
  autoritativ.
- `STAT.PRG` ist der erste kontrolliert umgestellte Client. Sein separater,
  heapfreier Adapter normalisiert relative, absolute und DOS-Pfade, verwendet
  ausschließlich die append-only autoritative FAT-Parseroperation 4, wartet mit monotoner
  Deadline und
  validiert den vollständigen Antwortframe ohne Legacy-Fallback. Der normale
  QEMU-Gast startet das paketierte Programm auf `/GUEST.TMP` und markiert den
  Erfolg mit `STORAGE_VFS_STAT_CLIENT_OK`. Andere Clients bleiben bis zu einer
  getrennten Umstellung am Kernel-VFS.
- Append-only Syscall 118 und `x86os_storage_cancel` widerrufen genau ein
  owner- und generationsgebundenes Requesthandle. Queued und vollständige
  Requests geben ihren Slot sofort frei. Bereits geclaimte Requests bleiben
  bis zur Quittierung der gebundenen Dienstgeneration `cancel-pending`, belegen
  die Statistik weiterhin und publizieren weder Status noch Daten. Der normale
  Gast markiert den ABI-Nachweis mit `STORAGE_REQUEST_CANCEL_OK`. Dies ist kein
  physischer I/O-Abbruch und kein Rollbackvertrag.
- Operation 2 bleibt ABI-kompatibel FAT32-spezifisch. Operation 3 ergänzt
  FAT12 mit standardisierter Clusterzahl-Typauswahl, 12-Bit-FAT-Einträgen und
  fester Rootdirectory; FAT16 und EXT2 werden abgewiesen. Der QEMU-FDD-Test
  führt nach echter Medienreintegrierung das paketierte `STAT.PRG` auf
  `/mnt/fdd0/HOTPLUG.TXT` aus und prüft Name, Größe und Shell-Rückkehr. Die
  erkannte 80x2x18-Geometrie wird dabei als feste Grenze von 2880 Sektoren an
  Ring 3 publiziert.
- Append-only Operation 4 macht denselben begrenzten FAT12-/FAT32-Parser zum
  autoritativen read-only `stat`-Ergebnisweg. Sie ruft `SYS_STAT` nicht auf und
  besitzt keinen Legacy-Fallback; Operationen 1 bis 3 bleiben unverändert. Der
  normale QEMU-Gast vergleicht Operation 4 außerhalb dieses Produktionspfads
  bytegenau mit Legacy-`stat` und markiert den Erfolg mit
  `STORAGE_VFS_FAT_STAT_AUTHORITY_OK`.
- Append-only Operation 5 ergänzt einen unabhängigen, heapfreien EXT2-Parser
  und ist der autoritative generische FAT12/FAT32/EXT2-`stat`-Pfad. Der
  unterstützte EXT2-Subset umfasst Revision 0/1, 1--4-KiB-Blöcke, lineare
  Directories sowie direkte und einfach-indirekte Directory-Blöcke unter 128
  Sektorreads. HTree, Extents, Symlinks und 64-Bit-Größen bleiben fail-closed.
  Der QEMU-Nachweis hängt eine deterministische zweite IDE-Platte ein und führt
  das paketierte `STAT.PRG` auf `/mnt/hdd1/readme.txt` aus.
- Append-only Operationen 6 und 7 liefern autoritatives, pfadbasiertes
  `read-at` mit höchstens 256 Byte beziehungsweise genau einen indexierten
  Verzeichniseintrag. FAT12/FAT32 und EXT2 nutzen nur vermittelte Sektorreads
  mit festen Parsergrenzen; Fehler veröffentlichen keine Teilbytes. `CAT.PRG`
  und `LS.PRG` besitzen in diesem Pfad keinen Kernel-VFS-Fallback. Der normale
  Gast prüft den FAT-Pfad, der zweite QEMU-IDE-Datenträger `stat`, `cat`, `ls`
  und die jeweilige Rückkehr zur Userspace-Shell.
- Ein fester prozesslokaler Read-only-Sessionlayer verwaltet vier
  generationcodierte Slots mit beim Öffnen kanonisiertem Pfad, 32-Bit-Offset,
  `read`, `SEEK_SET`/`SEEK_CUR`/`SEEK_END`, `fstat` und `close`. Fehler ändern
  den Offset nicht; stale Handles bleiben ungültig und Generationen laufen
  nicht über. Das ist bewusst keine stabile Inode- oder POSIX-Deskriptor-
  Identität und besitzt keine Vererbung.
- `HTTPD.PRG` nutzt für `/htdocs` ausschließlich Operation 5, die Sessions
  über Operation 6 und Listings über Operation 7. Der QEMU-Modus `http-server`
  führt zwölf abwechselnde echte Datei- und Verzeichnisanfragen aus, verlangt
  die Ring-3-Marker und gewinnt nach `Ctrl+C` die Userspace-Shell zurück.
- `FIND.PRG` und `TREE.PRG` verwenden für ihre vollständigen read-only-
  Baumläufe nur noch die autoritativen Operationen 5 und 7. Relative Pfade
  laufen weiterhin durch die gemeinsame Ring-3-Kanonisierung. Neben 256 Byte
  Pfad, 16 Ebenen und 512 Knoten gilt nun eine absolute monotone
  Fünf-Sekunden-Deadline; Einzelrequests erhalten höchstens eine Sekunde der
  Restzeit. Der normale FAT32-Gast startet beide paketierten Programme auf
  `/htdocs` und markiert `VFS_READONLY_WALKERS_OK`.
- Der Desktop-Explorer bezieht Verzeichnis-`stat`, einzelne Einträge und
  Leer/Voll-Ordnerproben ausschließlich aus den autoritativen Ring-3-
  Operationen 5 und 7. Ein atomarer Snapshot publiziert höchstens 32 sichtbare
  Einträge, scannt höchstens 128 und läuft unter einer absoluten monotonen
  Fünf-Sekunden-Deadline mit höchstens einsekündigen Requests. Timeout und
  Protokollfehler bewahren das zuvor veröffentlichte Fenster samt Generation.
  Der normale Gast weist den realen Root-Eintrag `htdocs` mit
  `DESKTOP_EXPLORER_VFS_OK` nach. Font-, Icon- und Konfigurationsstreams bleiben
  bis zu ihrer einzelnen Umstellung auf dem bestehenden Slice-Pfad.
- Der begrenzte Ring-3-Bulk-Lesetransport ist vorhanden. Storage-Operation 32
  und Objektoperation 15 behalten einen 512-Byte-Kontrollframe und verwenden
  genau zwei feste kernel-eigene 128-KiB-Slots. Append-only Syscall 124 bindet
  Publikation und Abholung an die exakten Service-/Clientgenerationen. CRC,
  Userbereich, Deadline und Cancel werden vor Offsetfortschritt geprüft; der
  normale Gast liest 1537 Byte in einem Request und markiert
  `STORAGE_VFS_BULK_READ_OK`. Der Bildbetrachter ist als erster großer
  Desktop-Client vollständig auf diesen API-Aufruf umgestellt: Sein fester
  1-MiB-Puffer benötigt höchstens acht Requests. Ein operationseigener
  FAT-Sektorcache und ein getrenntes 6400-Cluster-Dateibudget halten auch einen
  128-KiB-Lesezugriff am Ende einer 3-MiB-Datei unter 310 physischen
  Sektorzugriffen; die Zykluserkennung benötigt dabei konstanten Speicher und
  Verzeichnisgrenzen bleiben unverändert.
- Die langlebige Userspace-Shell löst Programmdateien über Ring-3-Operation 5
  auf und enumeriert Tab-Vervollständigungen über Operation 7. Alle Kandidaten
  einer Aktion teilen eine absolute monotone Fünf-Sekunden-Deadline,
  einsekündige Requestgrenzen und höchstens 128 akzeptierte Einträge. Fehler
  übernehmen keine Teilvervollständigung. Der geschützte Resident-Fallback
  bleibt verfügbar; der normale Gast markiert `SHELL_VFS_NAMESPACE_OK`.
- Der feste Rescue-Programmpool umfasst nun 352 KiB für weiterhin genau elf
  geschützte Programme; die Einzelgrenze beträgt 192 KiB. Damit passen die
  vollständigen Unicode-15-Tabellen des isolierten Storage-Dienstes in die
  Allowlist, ohne dynamische Cacheallokation einzuführen.
- Der Windows-Build wertet die Exitcodes der System- und Beispielprogramm-
  Builder über explizite Child-Prozesse aus. Ein fehlgeschlagener PRG-Build kann
  daher kein scheinbar erfolgreiches Image aus veralteten Artefakten erzeugen.
- Die append-only Claim-v2-Mediation der kernelgeschützten Client- und
  Servicegeneration ist umgesetzt. Syscall 119 liefert nur dem exakt
  gebundenen Storage-Dienst einen separaten 40-Byte-v2-Deskriptor; der Dienst
  revalidiert Client-Liveness und beide Generationen vor dem Dispatch.
  Syscall 68 und der 28-Byte-Claim-v1-Vertrag bleiben unverändert. Hosttests,
  QEMU-Paketbuild, normaler Gastlauf und Storage-Recovery-Gastlauf bestehen.
- Sechzehn feste, ownergebundene serviceeigene read-only Objekt-Slots sind
  umgesetzt; höchstens vier gehören einer Clientgeneration. Operation 8 löst
  den Pfad einmal auf, Operationen 9 bis 11 verwenden nur Servicehandle und
  Servicegeneration. FAT bindet Directory-Entry, Startcluster und
  Bootrecord-Signatur, EXT2 Inode, Inodegeneration und Superblock-Signatur.
  Tote Owner werden inkrementell reap-t; Service-Restart, Medienwechsel und
  Locator-Reuse können alte Handles nicht neu autorisieren. Normaler FAT-,
  echter EXT2- und Storage-Recovery-QEMU-Lauf bestehen.
- Append-only Operationen 12 bis 14 ergänzen explizite READ-/SEEK-/STAT-/
  DELEGATE-Rechte und eine auf exakte Ziel-PID/-Generation gebundene,
  abschwächende Objektübergabe. Das Ziel übernimmt innerhalb einer monotonen
  Fünf-Sekunden-Deadline aktiv; tote, abgelaufene und service-stale Slots
  werden begrenzt widerrufen. Der echte QEMU-Gast weist Vier-Slot-Quota,
  Rechteabschwächung, Quellhandle-Erhalt und Ablauf nach. Ambiente Spawn-
  Vererbung bleibt ausgeschlossen. Das Storage-Image bleibt unter der festen
  192-KiB-Einzelgrenze im festen 352-KiB-Rescue-Gesamtpool.
- `FDISK.PRG` erzeugt auf leeren, ungeschützten ATA-/AHCI-Medien eine
  ausgerichtete und rückgelesene MBR-Partition und veröffentlicht sie ohne
  Neustart. Root- und bereits partitionierte Medien bleiben geschützt.

### FAT32

Das erzeugte Image besitzt eine Systempartition mit Label `X86 SYSTEM`.
Markierte REIST-Images verwenden ein redundantes Undo-Journal. Datei-I/O,
Verzeichnisse, `fsync`, Same-Directory-Rename und Replace sind angebunden. Der
Editor speichert über Tempdatei, `fsync`, Close und Rename. Fremde FAT32-Medien
bleiben kompatibel lesbar, sind ohne gültigen REIST-Journalmarker jedoch
read-only. Jede Mutation prüft vor dem ersten Sektorwrite die exakte
Journalbindung an Gerät, Partition und Volumegrenze erneut; eine durch ein
anderes Mount verdrängte globale Bindung wird sicher neu aufgebaut.

VFAT Long File Names transportieren validiertes RFC-3629-UTF-8 innerhalb der
255-Byte-/255-UTF-16-Codeunit-Grenzen einschließlich Surrogatpaaren.
LFN-Slotfolge, Reihenfolge und 8.3-Prüfsumme werden vor Veröffentlichung
validiert; ungültige oder nicht unterstützte Unicode-Folgen fallen auf den
checksum-gebundenen 8.3-Alias zurück. Create, Lookup, `readdir`, Datei-I/O,
Delete, lange Verzeichnispfade und Same-Directory-Rename sind abgedeckt. Beim
Replace eines bestehenden regulären LFN-Ziels bleibt dessen validierte LFN-
Folge samt Alias erhalten. Der Alias übernimmt in derselben Undo-Journal-
Transaktion die Quellmetadaten, anschließend wird die vollständige Quellfolge
tombstoned und erst danach die alte Zielkette freigegeben. Verzeichnisse,
offene Objekte und Cross-Directory-Rename bleiben fail-closed ausgeschlossen.

`FORMAT.PRG` unterstützt auf einer veröffentlichten, nicht gemounteten
Partition zwei explizit bestätigte Modi:

```text
format --reist-fat32 --quick <resource-id> --confirm
format --reist-fat32 --full  <resource-id> --confirm
```

Quickformat invalidiert zuerst den alten Bootsektor, leert beide FAT-Kopien in
begrenzten Chunks und veröffentlicht erst danach BPB, FSInfo, Root und das
redundante REIST-Journal. Fullformat prüft danach jeden Datencluster durch
wiederholtes Schreiben und Readback. Reproduzierbar isolierte Defekte werden
in beiden FATs als `0x0FFFFFF7` markiert; Kontroll- oder Transportfehler
quarantänisieren das Medium.

### Userspace-Dateisystemwerkzeuge und Zeitstempel

Die erste Linux-artige Werkzeuggruppe ist als Ring-3-Programm in der festen
Systemhierarchie verfügbar: `/bin/rename.prg`, `/bin/stat.prg`,
`/bin/df.prg`, `/bin/touch.prg`, `/bin/tree.prg`, `/bin/find.prg` und
`/bin/rm.prg`. `ren` und `mv` aliasieren `rename`, `cp` aliasiert `copy`.
`tree`, `find` und `rm --recursive` verwenden feste Grenzen von 16 Ebenen und
512 besuchten Einträgen; Root-Pfade werden von `rm` nicht akzeptiert.

`vfs_dir_entry_t` und die Userspace-Dateiinformation liefern
`create_time`, `modify_time` und `access_time` als Sekunden seit
1970-01-01. FAT12 und FAT32 übersetzen ihre Directory-Felder über den
gemeinsamen Konverter `fs/vfs/vfs_time.h`; `x86os_touch()` verwendet den
append-only Syscall 108, um mtime und atime zu aktualisieren. FAT begrenzt
mtime auf zwei Sekunden und atime auf ein Datum ohne Uhrzeit. EXT2 liefert
seine vorhandenen Inode-Zeiten, bleibt für `touch` jedoch read-only. Ungültige
FAT-Kalenderfelder werden als Zeitwert null veröffentlicht. Die Werte werden
als lokale, nicht näher spezifizierte FAT-Zeit interpretiert; eine
Zeitzonenverschiebung wird nicht geraten. Nur Erzeugung, Write/Truncate und
explizites `touch` ändern Zeitfelder, nie Read/Stat/Readdir.

### FAT12

Für explizit markierte REIST-FAT12-Medien sind umgesetzt:

- zentrale Schreibzulassung nach erfolgreicher Journal-, Remap- und
  Replikatvalidierung; fremde FAT12-Medien bleiben lesbar, aber VFS-, FAT- und
  Sektormutationen werden vor der ersten Zustandsänderung abgewiesen
- verifiziertes redundantes Undo-Journal und Recovery vor Metadatennutzung
- begrenzte Defektbestätigung, `0xFF7`-Markierung und redundante Remaptabelle
- persistente Replikate für die feste Liste kritischer 8.3-Dateien
- geordnete Dateiänderungen: Daten, beide FATs, Verzeichniseintrag,
  Replikatpublikation und abschließendes Journal-`CLEAN`
- transaktionale Neuerzeugung durch `FORMAT.PRG` und den Storage-Service
- capability-gebundene BPB-/Spiegel- und Clusterkettenanalyse sowie bestätigte,
  journalisierte Reparatur einer eindeutig beschädigten FAT-Kopie,
  überlanger regulärer Dateiketten und eindeutig kurzer EOC-Dateien sowie
  bestätigtes Freigeben vollständig unerreichbarer Nicht-Bad-Allokationen und
  reine, ausreichend lange Schleifen regulärer Dateiketten sowie reine
  Rücksprungschleifen in vollständig gescannten Unterverzeichnissen und
  zugleich kurze reguläre Dateischleifen mit atomarer Größenbegrenzung sowie
  Crosslinks, die ausschließlich aus überlangen Dateitails entstehen, und
  unzulässige Größenfelder ansonsten gültiger Unterverzeichnisse sowie
  reservierte Nichtnull-Felder ansonsten gültiger Volume-Label-Einträge und
  eindeutig besessene Restallokationen regulärer Dateien der Größe null sowie
  positive Größen regulärer Dateien ohne Startcluster und unzulässige Größen
  korrekt verknüpfter `.`-/`..`-Einträge sowie falsche niedrige Clusterfelder
  ansonsten gültiger Dot-Beziehungen sowie reine mehrfach benötigte
  reguläre Dateiketten durch vollständig verifiziertes Klonen späterer Dateien
  und Same-Parent-Aliase strikt leerer einclusteriger Unterverzeichnisse
- versionierte Prüfung von Journal v2 und Remap v1 sowie bestätigtes,
  readback-verifiziertes Remapping von höchstens acht FAT-/Root-
  Metadatensektoren; unbekannte Versionen, unklare Daten und erschöpfte Spares
  setzen die Ressource fail-closed read-only

`FORMAT.PRG` akzeptiert ausschließlich eine veröffentlichte FDD-Ressource:

```text
DRIVES
FORMAT --reist-fat12 <resource-id> --confirm
```

`CHKDSK.PRG [pfad]` führt einen begrenzten read-only VFS-Scan aus. Die
FAT12-Modi `--repair`, `--repair-chains`, `--repair-short`,
`--reclaim-orphans`, `--repair-loops`, `--repair-dir-loops` und
`--repair-short-loops`, `--repair-crosslinks`, `--repair-dir-size` und
`--repair-volume-label`, `--repair-zero-files`, `--repair-zero-start` sowie
`--repair-dot-size`, `--repair-dot-cluster` und
`--repair-required-crosslinks`, `--repair-directory-crosslinks`,
`--repair-directory-topology`, `--salvage-orphans` sowie
`--record-bad-sector <sektor>`
benötigen jeweils `--confirm`, laufen ausschließlich im Storage-Dienst unter
Maintenance-Lease und melden Erfolg erst nach Undo-Journal, Readback und
sauberem Vollscan. Der Reclaim-Modus verwirft unerreichbare Inhalte
ausdrücklich. Der getrennte Salvage-Modus veröffentlicht vollständig gültige
Orphan-Ketten unter `FOUND.000` als `FILEnnnn.CHK`; `nnnn` hält den
ursprünglichen Startcluster fest. Reine Pflicht-Crosslinks regulärer
Dateien werden durch vollständige Kopien in höchstens 48 freie Cluster
getrennt. Same-Parent-Aliase strikt leerer einclusteriger Unterverzeichnisse
werden ebenfalls verifiziert kopiert und umgebunden. Der gebündelte
Topologiepfad entfernt darüber hinaus eindeutig attribuierbare nichtleere,
mehrclusterige, Same-Parent- und parentübergreifende Alias-Einträge samt streng
gebundenen VFAT-LFN-Slots, ohne die gemeinsame Kette oder FAT zu verändern;
mehrdeutige Parentbeziehungen, Teilketten und gemischte Datei-/Directory-Fälle
bleiben gesperrt.
`FDISK.PRG --create <resource-id> <mbr-type> --confirm` richtet ein leeres,
ungeschütztes ATA-/AHCI-Medium ein. Nicht eindeutig attribuierbare
Verzeichnisschäden und die reale Hardware-Power-Loss-Matrix bleiben offen.

### EXT2

EXT2 ist über VFS lesend und in den explizit unterstützten Operationen
verwendbar. Es besitzt kein REIST-Persistenzjournal und darf nach unklarer
Schreibunterbrechung nicht automatisch als wieder schreibsicher gelten.

## Shell und Systemprogramme

`/bin/shell.prg` ist der reguläre Ring-3-Command-Interpreter; die Kernel-Shell ist
nur Rettungskonsole. DOS-Laufwerksbuchstaben, kanonische VFS-Pfade,
laufwerksbezogene Arbeitsverzeichnisse, `PATH`, Verlauf und Tab-Vervollständigung
sind implementiert. Der flüchtige Verlauf ist als fester Ring mit 32 Einträgen
ausgeführt; Cursor-Up/Down navigiert darin und stellt hinter dem neuesten
Eintrag den begonnenen Eingabeentwurf wieder her. Die feste Standardsuche ist
`/bin`, `/sbin`, `/usr/bin`, `/usr/gui/bin`; interne Dienste liegen unter
`/libexec/reist`.
FAT12 und FAT32 speichern die Hierarchie begrenzt und zeigen ihre kanonischen
Namen kleingeschrieben an; FAT32 erhält dabei validierte lange Namen. Exakte alte Root-Pfade bleiben über eine feste
Kompatibilitätstabelle nutzbar. `/sbin/drives.prg` zeigt Resource-ID, Laufwerksbuchstaben,
Gerätenamen, Typ und den von der Elternressource geerbten Recovery-Zustand.

Die Buildliste enthält unter anderem `/libexec/reist/reist.prg`,
`/libexec/reist/storage.prg`, `/bin/shell.prg`, `/sbin/drives.prg`,
`/sbin/chkdsk.prg`, `/sbin/fdisk.prg`, `/sbin/format.prg`, `/bin/basic.prg`,
`/bin/edit.prg`, `/bin/rename.prg`, `/bin/stat.prg`, `/bin/df.prg`,
`/bin/touch.prg`, `/bin/tree.prg`, `/bin/find.prg` und `/bin/rm.prg`.
`/bin/basic.prg` ist ein
normales Ring-3-Programm, keine Kernelkomponente.

Grafische Programme sind getrennt unter `/usr/gui/bin`: Der Desktop ist der
Session-Compositor, Notepad und Image Viewer sind eigenständige
Ring-3-Surface-Clients, Sound Player und Control Gallery verwenden derzeit
noch die Vollbild-Kompatibilitätsbrücke. `/etc/reist/filetypes.conf` ordnet
Text-, WAV-, BMP- und GIF-Dateien ihren Anwendungen zu.

## Eingabe, Diagnose und Panic

Der i8042-Treiber arbeitet mit rohem Scan-Set 2, IRQ1 und einem begrenzten
Polling-Fallback. NumLock und die Tastatur-LEDs werden vom Treiber verwaltet.
Die frühere per COM1 injizierte Tastatureingabe ist entfernt; COM1 dient nur
der begrenzten Diagnoseausgabe. Der Panic-Screen zeigt Phase, Komponente,
Operation, Subjekt, Ergebnis, Details, Sequenz, Panic-Aufrufadresse, Build-ID
und – falls vorhanden – den Registerrahmen.

USB/xHCI unterstützt begrenzt HID-Boot-Tastatur und -Maus; PS/2 bleibt der
unabhängige Fallback. Allgemeine USB-Unterstützung und das
AULA/BY-Tech-Composite-Keyboard `258A:010C` bleiben offen. Verifizierte
Evidenz und VMware-Sicherheitsgrenze stehen in
[USB-Design](../hardware/USB_DESIGN.md) und [VMware](../hardware/VMWARE.md).

## Grafik, Audio und Medien

- Laufzeitgrafik, Explorer und Surface-Compositor sind umgesetzt; Notepad und
  Image Viewer laufen als echte externe Fensterclients. Details und offene
  Migrationen stehen ausschließlich im
  [Desktop-Workflow](GRAPHICAL_DESKTOP_WINDOW_MANAGER_WORKFLOW.md), in der
  [Framebuffer-Referenz](../features/FRAMEBUFFER.md) und im
  [Image-Vertrag](../architecture/IMAGE_SUBSYSTEM.md).
- VMware SVGA-II `15ad:0405`/`15ad:0710` besitzt einen überwachten Ring-3-
  2D-Treiber mit festem Kernelmediator. QEMU und VMware Workstation bestätigen
  `RECT_COPY`, Treiber-READY und `BOOT_OK`; der Compositor behält bei jeder
  Ablehnung den CPU-/Shadow-Framebuffer-Pfad. DMA, GMR, 3D und beliebige FIFO-
  oder BAR-Autorität sind nicht Bestandteil des Profils. Details stehen im
  [Videovertrag](../architecture/VIDEO_SUBSYSTEM.md).
- NVIDIA GK208 `10de:1280` besitzt nun den exakten überwachten Bring-up-Pfad
  `nvidia-gk208-ring3`. Er verändert beim Boot keine GPU-Register und meldet
  erst nach der vollständig in Ring 3 vermittelten BAR0-/Timerprüfung
  `NVIDIA_GK208_READY`. Native
  `RECT_FILL`-/`RECT_COPY`-Ausführung bleibt bis zum GPFIFO-/Fence-Paket offen.
- PCI-HDA läuft über getrennte überwachte Ring-3-Domänen; QEMU prüft den
  PCM-Pfad, VMware-Wiedergabe und Pegel wurden manuell bestätigt. Format,
  Lifecycle und Hardwaregrenzen stehen im
  [Audiovertrag](../architecture/AUDIO_SUBSYSTEM.md).

## Netzwerk

Der überwachte Ring-3-Dienst `REIST.PRG` übernimmt validierte
Netzwerkentscheidungen. Vorhanden sind Ethernet, ARP, IPv4, ICMP, DHCP,
prozessgebundene UDP-/TCP-FD-Sockets, DNS-A/CNAME-Auflösung, aktives und passives
TCP mit `listen`/`accept` sowie ein begrenzter HTTP/1.0-Dateiserver mit
Directory-Listing. Der neue RTL8168/8111G-Treiber bindet
die H81M-K-PCI-ID `10EC:8168` über MMIO, feste TX-/RX-DMA-Ringe und denselben
Netdev-Fence-Vertrag ein. Link und DHCP wurden auf dem physischen H81M-K
beobachtet; der ARP-/ICMP-Retest des aktuellen Builds bleibt dort noch offen.
Die QEMU-Referenz emuliert den Chip nicht. Die Socket-, DNS-, TCP- und HTTP-Pfade
sind hostseitig sowie deterministisch in QEMU getestet: DHCP und ARP, aktiver
TCP-Handshake/Daten/Close (`pong`), DNS-A über UDP (`test.reist` auf
`10.0.2.77`) und drei aufeinanderfolgende passive HTTP-Verbindungen mit
`/htdocs`-Directory-Listing laufen im selben Standard-Serverprozess; erst der
anschließend injizierte `Strg+C`-Abbruch führt zurück zur Shell und zu
`guest-smoke: PASS`. Begrenzte Einzel-Timeouts und fehlerhafte Clients beenden
den Listener nicht. Nicht vorhanden sind IPv6, TLS/HTTPS oder SMB.

## Verifikation

Der Referenznachweis besteht aus mehreren Ebenen:

```powershell
python -m unittest discover -s test -p "test_*.py" -v
.\scripts\test-reist-package.ps1 -Target qemu -Video vga
.\scripts\test-reist-runtime.ps1 -Mode normal
python .\scripts\run_qemu_smoke.py --image build\reist-os.img --sata --expect-reist-probe
```

Paketabhängig kommen FDD-Hotplug, PS/2, Netzwerkparser, Storage-Recovery,
Fault-Injection, Framebuffer/Surface, PCI-Audio, Watchdog und Handover hinzu.
Ein grüner Host- oder QEMU-Test ersetzt keine Langzeit-, EMV-, Stromausfall-
oder breite Zielhardwarequalifikation.

`S0.6a` führt die erste fest begrenzte Update-Parser-Kampagne aus. Mit einem
expliziten 32-Bit-Seed werden 16 strukturierte und 48 Einbitfehler gegen den
echten Offline-Bundle-Consumer und Inactive-Slot-Einstieg geprüft. Alle 64
Fälle scheitern vor einem Output-Image; Quellimage, signierter Kernel und
Signatur bleiben SHA-256-identisch. Die Kampagne ist deterministisch und auf
höchstens 128 Fälle begrenzt. Sie ersetzt weder Coverage-gesteuertes Fuzzing
noch Soak-, VMware- oder Hardwareevidenz.

`S0.6b` ergänzt beide nativen Image-Builds um ein begrenztes SPDX-2.3-JSON-
SBOM. Es erfasst Kernel, detached Signatur, BIOS-Image und die nichtrekursiv
paketierten Ring-3-Programme mit exakter Größe im SPDX-Kommentarfeld sowie
SHA-1 und SHA-256. Ein strukturell unabhängiger Validator prüft Pfadgrenzen,
Eindeutigkeit, Kapazitäten,
Dokumentstruktur, Beziehungen und jedes aktuelle Artefakt erneut; der QEMU-
Paket-Gate verlangt das Ergebnis. Die Grenzen sind 160 Dateien, 128 MiB je
Datei, 512 MiB Gesamteingang und 2 MiB Dokumentgröße. `NOASSERTION` markiert
weiter ungeklärte Lizenz- und Copyrightdaten. Reproduzierbarkeit, vollständige
Quellen-/Abhängigkeitsabdeckung, Lizenzfreigabe, Vulnerability-Analyse und
signierte Provenienz bleiben offen.

`S0.6c` bindet den Abschluss der automatisierten Forschungsbaseline an den
versionierten Vertrag `safety/automated_s0_gate.toml`. Der unabhängige
Validator akzeptiert ausschließlich `REIST-research`, QEMU i386 und VMware
i386 sowie die feste Host-, Paket- und Laufzeitmatrix. Am 23. August 2026
bestanden 1001 Hosttests, beide frischen Referenzpakete, QEMU-PIT, Watchdog,
Storage-Recovery, vier Speichergrößen, Framebuffer und das begrenzte VMware-
Containment. Der Status lautet bewusst `automated-emulator-complete`.
Zielhardware-WCET, externe Monitor-/Fence-Hardware, physische Fault-Injection,
reproduzierbare Builds, signierte Provenienz, Langzeit-Soak, Online-Verteilung,
Anti-Rollback, unveränderliche Recovery, Produktionsschlüssel und
Zertifizierung bleiben offene manuelle oder produktbezogene Nachweise.

## Wichtigste offene Grenzen

- `CHKDSK.PRG` besitzt capability-gebundene, bestätigte und journalisierte
  Reparaturpfade für genau eine eindeutig beschädigte FAT12-Spiegelkopie und
  eindeutig überlange reguläre Dateiketten. Bei eindeutig kurzen, normal
  EOC-terminierten Dateien kann es außerdem die Directory-Größe auf die
  lesbare Kettenkapazität begrenzen und reine unerreichbare Allokationen
  explizit verwerfen. Ausreichend lange reine reguläre Dateiloops werden am
  Sollende getrennt; reine Directory-Rücksprünge werden nach vollständigem
  eindeutigen Inhaltsscan beendet. Kombinierte Short-Loops begrenzen zusätzlich
  atomar die Directory-Größe; reine Excess-Tail-Crosslinks werden ohne Änderung
  der einzigen Sollkette getrennt; reine ungültige Unterverzeichnisgrößen
  werden nach vollständigem Inhaltsscan nullgesetzt. Reservierte Startcluster-
  und Größenfelder ansonsten gültiger Volume-Label-Einträge werden ebenfalls
  ausschließlich auf null normalisiert. Eindeutig besessene, normal
  terminierte Restketten von Nullgrößendateien können bestätigt freigegeben
  und ihre Startcluster nullgesetzt werden. Positive Größen ohne Startcluster
  werden bei reiner Short-Diagnose auf null begrenzt. Reine mehrfach benötigte
  reguläre Dateiketten können vollständig in freie Cluster kopiert und so
  getrennt werden. Reine Same-Parent-Crosslinks strikt leerer einclusteriger
  Unterverzeichnisse lassen sich ebenso kopieren und umbinden. Eindeutig
  attribuierbare nichtleere, mehrclusterige und parentübergreifende
  Directory-Aliase werden ohne Kettenänderung auf genau einen kanonischen
  Parent reduziert. Vollständig gültige Orphan-Ketten lassen sich unter
  `FOUND.000` retten. Die feste Journal-/Remap-/Defektkartenprüfung und der
  QEMU-Maintenance-/Remountnachweis sind automatisiert; mehrdeutige
  Verzeichnisreparaturen bleiben gesperrt
- die reale FAT12-Power-Loss-/Reconnect-Matrix auf Zielhardware bleibt eine
  manuelle Benutzerabnahme; VMware-Reconnect ist automatisiert, ersetzt diese
  Hardwareevidenz aber nicht
- journalisiertes Schreiben für EXT2 und weitere Backends; fremde FAT12- und
  FAT32-Medien sind bis zu einem nachgewiesenen Vertrag bewusst read-only
- unabhängige Supervisor-, Fence- und Failover-Hardware
- breite reale AHCI-/PCI-IDE-/PS/2-/BIOS-Kompatibilitätsmatrix
- allgemeiner USB/xHCI-, Composite-HID-, Mass-Storage- und Hotplug-Lebenszyklus
- Migration der verbleibenden GUI-Programme auf Surface-Clients sowie
  allgemeine 3D-/Multi-Monitor-Grafikbeschleunigung
- SMP, IOMMU/DMA-Isolation, UEFI, Secure Boot und NVMe
- formale Nachweise, Langzeit-Stresstests und Zertifizierung
