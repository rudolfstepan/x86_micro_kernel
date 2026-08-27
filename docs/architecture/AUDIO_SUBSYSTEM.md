# REIST-Audiosubsystem

Stand: 27. August 2026.

Dieses Dokument beschreibt die erste öffentliche Audioarchitektur von REIST.
Sie stellt einen begrenzten PCM-Wiedergabepfad bereit, ohne Anwendungen oder
Gerätetreibern physische DMA-Adressen, freie MMIO-Zugriffe oder Kernelautorität
zu geben. Die Quell-API folgt vertrauten PCM-Begriffen, behauptet aber keine
Binärkompatibilität mit ALSA, OSS, CoreAudio oder WASAPI.

## Unterstützungsgrenze

Version 1 unterstützt genau einen Wiedergabestream:

- interleaved `S16_LE`, stereo, 48 kHz;
- 24 Frames im unveränderten IPC-v1-Format oder 504 Frames im append-only
  IPC-v2-Bulkformat sowie höchstens 15360 Frames pro Stream;
- synchrone, durch 1 bis 5000 ms begrenzte Transaktionen;
- Intel High Definition Audio, PCI-Klasse `04:03:00`;
- einen gleichzeitig geöffneten Clientstream ohne Mixing.

Capture, Resampling, Lautstärke-Policy, mehrere Clients, MIDI, AC'97, USB Audio
Class und HDMI/DisplayPort-Audio sind nicht implementiert. Nicht unterstützte
Formate werden abgewiesen und nicht stillschweigend konvertiert.

## Architektur und Fehlergrenzen

```text
Ring-3-Anwendung
  -> libreistaudio.a / <reist/audio.h>
     -> versioniertes Audio-Service-IPC
        -> überwachter Audio-Service (Ring 3)
           -> internes, nicht öffentliches Treiber-IPC
              -> überwachter HDA-Treiber (Ring 3)
                 -> generischer Device-Resource-Mediator (Ring 0)
                    -> PCI-HDA-Controller
```

Anwendungen besitzen nur eine Capability zum öffentlichen Audio-Service. Der
Service besitzt keine Gerätecapability. Nur die eigene HDA-Treiberdomäne darf
die für ihr unveränderliches Startprofil freigegebenen Device-Operationen
aufrufen. Controller-, Codec- und Streamzustandsautomaten bleiben in Ring 3;
Ring 0 validiert ausschließlich Identität, Ressourcen, Registerregeln, IRQ,
DMA und Bus-Mastering.

Eine Servicegeneration wird geschützt an genau eine lebende Client-PID samt
Prozessgeneration gebunden. Bei einem geordneten
`reist_audio_shutdown()` sendet die Bibliothek zuerst das append-only,
antwortlose Protokollkommando `RELEASE` und entzieht danach ihre
Peer-Capability. Erst wenn der Dienst diese Reihenfolge als bewaffnetes
`EPIPE` beobachtet, räumt er den Stream auf und meldet die Sessionfreigabe an
den Supervisor. Dieser löscht ausschließlich die passende Clientbindung
idempotent; zuvor prüft Ring 0 am exakten Endpoint, dass weder eine
Peer-Capability noch eine wartende Nachricht verblieben ist. Die saubere
Servicegeneration bleibt erhalten. Weil `RELEASE` keine Antwort erzeugt, kann
keine alte Abschlussantwort zum nächsten Client wandern. Stirbt ein Client
ohne diese Folge, wird der Endpoint weiterhin
vollständig entzogen und der optionale Service über seinen normalen begrenzten
Supervisorpfad neu erzeugt, bevor ein anderer Client verbunden wird.

Treiber und Service haben getrennte Adressräume, Quoten, Generationen und
Restartbudgets. Ein Fehler im optionalen Audiosubsystem darf Shell, Desktop,
Storage und andere Prozesse nicht beenden. Nach erschöpftem Budget bleibt
Audio isoliert und das System meldet `DEGRADED`.

Auch der geräteautoritätslose Audio-Service darf nach R6.2f auf SMP-Systemen
AP-only laufen. Jede Generation verbindet sich weiterhin auf dem BSP mit der
aktuellen HDA-Generation, führt Self-Test aus und veröffentlicht Endpoint,
Gesundheit und Ready, bevor die ECC-geschützte Zielmaske angewandt wird. Vor
jeder wegen Crash oder stale Endpoint erforderlichen Sessionrotation kehrt die
Generation sleepfähig auf den BSP zurück; erst danach beginnt der
präemptionsgeschützte Fence-/Reap-Commit. Die Zielmaske bleibt über Rotationen
erhalten, stale Endpoints jedoch nicht. Ein sauberer `RELEASE`-Wechsel benötigt
keine Rotation.

Auf SMP-Systemen konstruiert der HDA-Treiber jede Generation zunächst auf dem
BSP, bindet dort den kernelbesessenen DMA-Pool und IRQ-Endpunkt, entdeckt den
Codec und veröffentlicht Self-Test sowie Gesundheit. Erst nach der globalen
Scheduler-Freigabe erhält genau diese geschützte Generation die Online-AP-
Maske. Audio-Service, Supervisor und Legacy-PIC-Hard-IRQ bleiben BSP-affin.
Schon vor dieser Konstruktion bleibt der geladene Treiberprozess im Zustand
`PREPARED`: Der Supervisor setzt seine Anfangsaffinität, claimt das Gerät und
committet PID, Prozessgeneration, Device-Handle und Supervisor-Handle in den
geschützten Kontrollrecord. Erst danach wird der Task lauffähig. Ein Fehler in
dieser Folge beendet die vorbereitete Generation und fenced sowie recovered
einen bereits erfolgten Claim innerhalb der vorhandenen Deadline. Damit kann
parallele HDA-/Video-Initialisierung keinen Bootstrap vor der
Owner-Publikation mehr beobachten.
Der 500-ms-Treiberheartbeat besitzt wegen begrenzter SMP-Scheduling- und PIC-
Latenz ein festes Fünf-Sekunden-Fenster; Fence-Deadline von einer Sekunde und
Restartbudget drei bleiben unverändert.
Für Controller ohne PCI-Funktionsreset installiert das HDA-Profil zusätzlich
ein unveränderliches, datengetriebenes GCTL-Resetrezept. Der generische
Mediator validiert Region, Breite, Maske, Assert-/Deassert-Werte, höchstens 100
Polls und die absolute Recovery-Deadline vor dem ersten Claim. Beim Fence sind
IRQ und Bus-Mastering bereits entzogen und der DMA-Pool genullt; erst nach dem
erfolgreichen Registerreset darf eine neue Generation claimen. Der Mediator
enthält dabei keine HDA-Zustandsmaschine oder gerätespezifische Konstante.

## Öffentliche API

Der normative Header ist `<reist/audio.h>`. Ein Client verwendet die Folge

```text
reist_audio_init
-> reist_audio_get_info
-> reist_audio_open
-> reist_audio_write
-> reist_audio_start
-> reist_audio_stop
-> reist_audio_close
-> reist_audio_shutdown
```

Kontext, Info und IPC-Nachricht tragen feste Größen und Versionen. Streamhandles
bestehen aus ID und Generation; ein Handle einer früheren Service- oder
Streamgeneration erhält keine neue Autorität. Jede Response muss Version,
Größe, Command und Request-ID der Anfrage entsprechen.

`reist_audio_write` übernimmt Frames blockweise in den begrenzten Dienstpuffer.
Der normale Datenpfad verwendet einen einzelnen, von der v1-Queue getrennten
2048-Byte-Rendezvous-Slot je Endpoint. Geschützte Metadaten binden Version,
Länge, Endpoint- und Absendergeneration an CRC32 der Nutzlast; beschädigte
Blöcke werden vor Veröffentlichung verworfen. Damit benötigt eine maximale
Vorschau höchstens 31 statt 640 bestätigte Schreibtransaktionen. Der
HDA-Treiber zerlegt jeden 2016-Byte-PCM-Anteil weiterhin in höchstens zwei vom
Kernelmediator erlaubte DMA-Schreibvorgänge von je maximal 1024 Byte.
Wie bei POSIX `write` darf ein positiver kurzer Rückgabewert die bereits
akzeptierte Framezahl melden, falls eine spätere IPC-Teiltransaktion scheitert.
Ein negativer Wert bedeutet, dass in diesem Aufruf kein Frame akzeptiert wurde.
Wichtige Fehler sind `-11` für Kapazitätsdruck, `-16` für einen unzulässigen
Zustandswechsel, `-22` für ungültige Parameter, `-84` für eine fehlerhafte
Protokollantwort und `-110` für eine abgelaufene Deadline.

## Vollständig vermitteltes DMA

Das aktuelle Profil benötigt keine IOMMU-Zuweisung. Der Kernel besitzt einen
beim Binden und Entziehen genullten 64-KiB-DMA-Pool. Die ersten 4 KiB sind ein
für Ring 3 nicht beschreibbarer Deskriptorbereich. Der Treiber schreibt nur
PCM-Daten ab dem veröffentlichten Datenoffset und beschreibt eine BDL-Zeile
durch Offset, Länge und Flags. Erst der Mediator validiert diese Angaben,
konstruiert den 16-Byte-HDA-BDL-Eintrag mit der echten physischen Adresse und
versiegelt ihn gegen spätere Änderung.

Bus-Mastering wird erst in `ACTIVE` freigegeben. Nach `stop` deaktiviert der
Treiber das Gerät wieder bis `DMA_BOUND`, sodass der Pool ohne laufendes DMA
neu befüllt werden kann. Fehler bei Aktivierung, MMIO oder Deaktivierung
maskieren IRQs, nehmen Bus-Mastering zurück und fence'n die Gerätegeneration.

PCI-2.3-Endpunkte müssen das verifizierte `INTx Disable`-Bit verwenden. Das
VMware-HDA-Modell `15AD:1977` ignoriert dieses Bit jedoch. Ausschließlich sein
unveränderliches Geräteprofil erlaubt deshalb einen Legacy-PIC-Fallback: Die
gemeinsam genutzte IRQ-Leitung wird im Hard-IRQ-Pfad maskiert, alle
registrierten Shared-IRQ-Handler werden ausgeführt und die Leitung erst nach
dem generationstreuen Ring-3-Acknowledge wieder freigegeben. Ein Timeout hält
die Leitung fail-closed maskiert. Andere PCI-Modelle erhalten diese Ausnahme
nicht; Bus-Mastering bleibt unabhängig davon immer verifiziert abgeschaltet.

## HDA-Zustandsautomat

Der HDA-Treiber prüft Controller-Version, GCAP, Outputstreams, Codecpräsenz und
Widgettopologie. Reset-, Codecverb- und Stream-Waits besitzen feste Pollgrenzen
und geben bei Fristablauf einen Fehler zurück. Die Ausgangsverstärker von DAC
und Pin werden über den standardisierten Parameter
`Output Amplifier Capabilities` abgefragt; der Offset der Capability bestimmt
den gültigen 0-dB-Gain. Für die erste Wiedergabe wird jeder vorhandene
Ausgangsverstärker innerhalb seiner gemeldeten Schrittweite um höchstens 6 dB
angehoben; unterstützt ein Codec keinen positiven Gain, bleibt er bei 0 dB.
Wenn ein Widget keine eigenen Verstärkerparameter
überschreibt, werden sie entsprechend der HDA-Spezifikation vom Audio Function
Group übernommen. Es wird kein codec- oder emulatorabhängiger Lautstärkewert
fest eingebaut und PCM nicht digital übersteuert.

Die Wiedergaberoute wird nicht als feste Codec-Node-ID angenommen. Der Treiber
liest eine auf 16 Einträge begrenzte HDA-Connection-List und akzeptiert einen
direkten Pfad oder genau einen standardisierten Mixer/Selector zwischen DAC
und Ausgangspin. Bei einem Mixer wird nur der zum gewählten DAC gehörende
Input-Amp entstummt; andere Quellen bleiben unverändert. Tiefere oder
mehrdeutige Topologien werden in dieser Version abgewiesen, statt unbegrenzt
durch den Codecgraphen zu laufen.

Grundlage ist die
[Intel High Definition Audio Specification](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf).
Die QEMU-Referenz wird gegen die Upstream-Implementierungen von
[intel-hda](https://gitlab.com/qemu-project/qemu/-/raw/master/hw/audio/intel-hda.c)
und [hda-output](https://gitlab.com/qemu-project/qemu/-/raw/master/hw/audio/hda-codec.c)
geprüft. Diese Orientierung an Standards begründet keine Fremd-ABI.

## Installation und Bedienung

Das SDK installiert:

```text
usr/include/reist/audio.h
usr/include/reist/audio_wave.h
usr/lib/libreistaudio.a
usr/lib/pkgconfig/reist-audio.pc
```

Die Systempartition enthält `/libexec/reist/hda.prg`,
`/libexec/reist/audio.prg`, `/sbin/audioinfo.prg` und
`/usr/bin/audiotest.prg`, `/usr/bin/wavplay.prg` und die sechs Systemklänge
`startup.wav`,
`shutdown.wav`, `error.wav`, `notify.wav`, `trash-drop.wav` und
`trash-empty.wav`. Der grafische Client liegt unter
`/usr/gui/bin/soundplayer.prg`. Die Programme sind über die normale
Ring-3-Shell erreichbar:

```text
C:\> AUDIOINFO
C:\> AUDIOTEST
C:\> WAVPLAY
C:\> WAVPLAY /usr/share/sounds/startup.wav
C:\> SOUNDPLAYER /usr/share/sounds/startup.wav
```

`AUDIOTEST` erzeugt einen begrenzten 440-Hz-Testton, startet und stoppt den
Stream und schließt ihn anschließend. Sein 50-ms-Ring enthält 22 vollständige
Perioden einer ganzzahlig erzeugten Dreieckswelle. Damit bleibt der zyklische
HDA-Puffer an seiner Grenze phasenstetig und ist größer als ein 20-ms-
VMware-Hostblock. Der QEMU-Runtimetest führt diese Folge
fünfmal und damit häufiger als das Fehler-Restart-Budget aus. Geordnete
Clientwechsel geben dieselbe saubere Dienstgeneration frei; nur ein ohne
Peer-Freigabe beendeter Client erzwingt die administrative Endpointrotation,
ohne als Dienstfehler zu zählen. Der Test akzeptiert nur eine korrekt formatierte,
unterbrechungsfreie Stereo-S16-Aufzeichnung mit 435 bis 445 Hz. VMware stellt
ein virtuelles `hdaudio`
bereit; reale Codecs und physische Ausgänge benötigen weiterhin einen eigenen
Hardwaretest.

`WAVPLAY` liest RIFF/WAVE-PCM mit ein oder zwei Kanälen, 16 Bit Little Endian
und 48 kHz. Mono wird kontrolliert auf die zwei Kanäle der öffentlichen ABI
dupliziert. Der Parser untersucht höchstens 512 Headerbytes und 16 Chunks;
komprimierte, gleitkommacodierte, falsch ausgerichtete oder anders abgetastete
Dateien werden abgewiesen. Wegen des zyklischen ABI-v1-DMA-Puffers kopiert der
Player höchstens 15360 Frames in statischen Ring-3-Speicher. Er spielt genau
die geladene Dauer bis höchstens 320 ms plus eine feste 20-ms-Drain-Frist und
stoppt danach den zyklischen Stream; `--quiet` unterdrückt ausschließlich die
Konsolenausgabe für Desktopereignisse. Streaming, Resampling und ein
allgemeiner Decoder sind damit ausdrücklich noch nicht behauptet. Die
unveränderte fünfsekündige 440-Hz-Datei aus dem unter CC0-1.0
veröffentlichten Projekt
[TestToneSet](https://github.com/AkiyukiOkayasu/TestToneSet) bleibt nur eine
Host-Parserfixture und wird nicht in das Systemimage installiert.

Parser und bounded Preview-Loader gehören zur öffentlichen
`<reist/audio_wave.h>`-Schicht und werden von `WAVPLAY` und `SOUNDPLAYER`
gemeinsam verwendet. Jede Datei wird genau einmal geöffnet; bereits zusammen
mit dem 512-Byte-Header gelesene PCM-Daten werden direkt übernommen, danach
wird nur vorwärts weitergelesen. Die Desktop-Dateizuordnung `.wav` startet den grafischen
Player mit dem kanonischen Dateipfad. Dieser bietet Abspielen, Stoppen und
Schließen, hält aber dieselben Format- und Kapazitätsgrenzen ein und beginnt
die feste Vorschau beim Öffnen noch vor Konstruktion seiner Surface. Der Desktop delegiert ihm genau einen
generationgebundenen Surface-Endpunkt und wartet nicht auf das Ende der
Wiedergabe. Der Player malt ausschließlich lokale Retained-Paint-Befehle,
erhält lokale Eingabeereignisse über Surface-IPC und überträgt je
Eventloopdurchlauf höchstens eine feste Audio-Nutzlast. Dadurch publiziert der
Compositor auch während der Wiedergabe weiter seinen 500-ms-Heartbeat; die
frühere synchrone Vollbildbrücke ist für den Sound Player entfernt. Der
Sound Player begrenzt jede Audiotransaktion auf die öffentliche 500-ms-
Standardfrist. Diese Frist deckt den ebenfalls auf 500 ms begrenzten
Service-zu-HDA-Treiber-Start ab und kehrt bei schneller Hardware sofort zurück;
ein nicht reagierender optionaler Audiodienst scheitert weiterhin sichtbar,
statt den Fensteraufbau unbegrenzt aufzuhalten. Eine kürzere lokale 100-ms-
Frist ist unzulässig, weil VMware einen erfolgreich begonnenen HDA-Start erst
danach bestätigen kann und der Client sonst den Erfolg fälschlich als
`ETIMEDOUT` behandelt.

## Konfigurierbare Systemklänge

`/etc/reist/sounds.conf` verwendet `schema=reist.sounds/1`. Die sechs stabilen
Schlüssel `event.startup`, `event.shutdown`, `event.error` und
`event.notification` sowie `event.trash_drop` und `event.trash_empty` ordnen
Ereignisse einem kanonischen kleingeschriebenen
WAV-Pfad unter `/usr/share/sounds` oder dem Wert `none` zu. `enabled=false`
schaltet die gesamte Präsentationsschicht aus. Der Desktop übernimmt erst nach
vollständiger Parser- und Pfadvalidierung eine neue Tabelle; eine fehlende oder
ungültige Datei deaktiviert Klänge und verändert den GUI-Lifecycle nicht.
`event.notification` ist einem echten Informationsdialog vorbehalten. Normale
Ordnernavigation und erfolgreiche Datei- oder Programmaktivierung bleiben
still, damit kein kurzlebiger Systemklang mit einem explizit gestarteten
Audio-Client um den einzelnen generationengebundenen Dienstendpunkt konkurriert.
Die beiden Papierkorbereignisse werden erst nach erfolgreich committed
Verschieben beziehungsweise vollständig erfolgreichem endgültigem Löschen
publiziert; Teilfehler verwenden ausschließlich `event.error`.
`config.prg` akzeptiert dieselben begrenzten Schlüssel bereits als
Mutationsgrenze, sodass eine spätere Systemsteuerungsseite keine Desktop-ABI
ändern muss.

Der Compositor verbindet sich nicht selbst mit dem Audiodienst. Er startet
stattdessen höchstens zwei stille `wavplay.prg`-Kinder und prüft deren
Prozessgeneration nicht blockierend. Ein normaler Slot ist aktiv, solange sein
Kind lebt; Überlast wird auf genau ein priorisiertes Folgeereignis reduziert.
Shutdown darf den zweiten festen Slot verwenden, damit ein vorheriger kurzer
Klang das Sitzungsende nicht verschluckt. Erst nach verschwundener Live-
Identität ruft der Desktop `wait` auf und kann daher nicht auf einen lebenden
Klangprozess blockieren. Diagnose- und Runtime-Probes deaktivieren diese
Präsentationsklänge, damit ihre Audioevidenz ausschließlich vom jeweiligen
Testclient stammt.

Die sechs WAV-Dateien sind deterministisch aus
`scripts/generate_system_sounds.py` erzeugte Mono-S16_LE-Signale mit 48 kHz,
liegen jeweils unter der 15360-Frame-Grenze und werden unter CC0-1.0
freigegeben. Sie enthalten keine Microsoft- oder sonstigen Fremdsamples.

## Nachweis und verbleibende Risiken

Hosttests prüfen ABI-Größen, v1-Kompatibilität, den einzelnen CRC-geschützten
Bulk-Slot, Request-Korrelation, höchstens 31 Blöcke und Short Writes,
Parameterdekodierung und 0-dB-Gain. Source- und Pakettests prüfen
Default-Deny-Domänen, vollständig vermitteltes DMA, Shell-Erreichbarkeit und
identische QEMU-/VMware-Systemlayouts. Ein headless VMware-Bootsmoke verlangt
das HDA-Profil und `REIST_AUDIO SERVICE_READY`. Der QEMU-Gastnachweis prüft
tatsächliche nicht stumme PCM-Ausgabe und die Wiederverwendung nach `stop`.
Der SMP4-Nachweis verlangt zusätzlich eine autorisierte HDA-Geräteoperation auf
einem AP und fünf vollständige Playback-Zyklen. Jeder Zyklus muss vor dem
nächsten Client einen bestätigten `CLIENT_RELEASED`-Marker liefern; die
Referenzaufnahme umfasste 278332 Stereo-S16-Frames bei 440,4 Hz mit höchstens
einer Nullprobe Lücke.
Der ergänzende Restartlauf unterdrückt compile-time-begrenzt den Heartbeat der
ersten AP-Epoch. Nach Timeout, Fence, GCTL-Reset und Treiberneustart entdeckt
die erste Clientprobe den stale Service-Endpoint und löst die normale
Servicegenerationrotation aus. Die Ersatzgeneration bestand danach fünf
Playback-Zyklen mit 271216 Frames bei 440,4 Hz und `max-gap=1`.
Der manuelle VMware-Nachweis vom 20. August 2026 bestätigte mit der damals
paketierten 440-Hz-WAV-Datei sowohl hörbare Ausgabe als auch den erwarteten Pegel über den
vollständig aktivierten DAC-/Mixer-/Pin-Pfad.

Ein eigener compile-time-only Desktop-Audioprobe startet den Sound Player mit
dem paketierten `startup.wav` und parallel die Control Gallery als Surface-
Clients.
Er muss beide Ready-Marker, eine einzelne begrenzte, echte Stereo-S16-Ausgabe
und das Überschreiten der
Zwei-Sekunden-Compositor-Heartbeatgrenze beobachten, ohne
`COMPOSITOR_RESTARTED` oder `COMPOSITOR_DEGRADED` zu melden. Der Schalter ist
in normalen Images nicht definiert.

Noch nicht nachgewiesen sind hörbare Ausgabe auf dem ASUS-Zielsystem,
codec-spezifische Pin-Routing-Varianten, MSI, IOMMU-Direktzuweisung und die oben
aufgeführten Erweiterungen. Fehlt ein sicher akzeptierbares Profil, bleibt das
Audiosubsystem nicht verfügbar; REIST fällt nicht auf unkontrolliertes DMA
zurück.
