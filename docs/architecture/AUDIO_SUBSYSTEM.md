# REIST-Audiosubsystem

Stand: 20. August 2026.

Dieses Dokument beschreibt die erste öffentliche Audioarchitektur von REIST.
Sie stellt einen begrenzten PCM-Wiedergabepfad bereit, ohne Anwendungen oder
Gerätetreibern physische DMA-Adressen, freie MMIO-Zugriffe oder Kernelautorität
zu geben. Die Quell-API folgt vertrauten PCM-Begriffen, behauptet aber keine
Binärkompatibilität mit ALSA, OSS, CoreAudio oder WASAPI.

## Unterstützungsgrenze

Version 1 unterstützt genau einen Wiedergabestream:

- interleaved `S16_LE`, stereo, 48 kHz;
- höchstens 24 Frames pro IPC-Nachricht und 15360 Frames pro Stream;
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
Prozessgeneration gebunden. Nach deren Ende wird der Endpoint vollständig
entzogen und der optionale Service über seinen normalen begrenzten
Supervisorpfad neu erzeugt, bevor ein anderer Client verbunden wird. Dadurch
kann kein neuer Prozess eine noch wartende Response des Vorgängers übernehmen.

Treiber und Service haben getrennte Adressräume, Quoten, Generationen und
Restartbudgets. Ein Fehler im optionalen Audiosubsystem darf Shell, Desktop,
Storage und andere Prozesse nicht beenden. Nach erschöpftem Budget bleibt
Audio isoliert und das System meldet `DEGRADED`.

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
usr/lib/libreistaudio.a
usr/lib/pkgconfig/reist-audio.pc
```

Die Systempartition enthält `/libexec/reist/hda.prg`,
`/libexec/reist/audio.prg`, `/sbin/audioinfo.prg` und
`/usr/bin/audiotest.prg`, `/usr/bin/wavplay.prg` sowie die Testdatei
`/usr/share/sounds/440hz.wav`. Die Diagnoseprogramme sind über die normale
Ring-3-Shell erreichbar:

```text
C:\> AUDIOINFO
C:\> AUDIOTEST
C:\> WAVPLAY
C:\> WAVPLAY /usr/share/sounds/440hz.wav
```

`AUDIOTEST` erzeugt einen begrenzten 440-Hz-Testton, startet und stoppt den
Stream und schließt ihn anschließend. Sein 50-ms-Ring enthält 22 vollständige
Perioden einer ganzzahlig erzeugten Dreieckswelle. Damit bleibt der zyklische
HDA-Puffer an seiner Grenze phasenstetig und ist größer als ein 20-ms-
VMware-Hostblock. Der QEMU-Runtimetest führt diese Folge
fünfmal und damit häufiger als das Fehler-Restart-Budget aus. Normale
Clientwechsel rotieren den Dienst-Endpunkt administrativ, ohne als
Dienstfehler zu zählen. Der Test akzeptiert nur eine korrekt formatierte,
unterbrechungsfreie Stereo-S16-Aufzeichnung mit 435 bis 445 Hz. VMware stellt
ein virtuelles `hdaudio`
bereit; reale Codecs und physische Ausgänge benötigen weiterhin einen eigenen
Hardwaretest.

`WAVPLAY` liest RIFF/WAVE-PCM mit ein oder zwei Kanälen, 16 Bit Little Endian
und 48 kHz. Mono wird kontrolliert auf die zwei Kanäle der öffentlichen ABI
dupliziert. Der Parser untersucht höchstens 512 Headerbytes und 16 Chunks;
komprimierte, gleitkommacodierte, falsch ausgerichtete oder anders abgetastete
Dateien werden abgewiesen. Wegen des zyklischen ABI-v1-DMA-Puffers kopiert der
Player höchstens 2400 Frames in statischen Ring-3-Speicher und wiederholt diese
Vorschau zwei Sekunden. Streaming, Resampling und ein allgemeiner Decoder sind
damit ausdrücklich noch nicht behauptet. Die unveränderte fünfsekündige
440-Hz-Testdatei stammt aus dem unter CC0-1.0 veröffentlichten Projekt
[TestToneSet](https://github.com/AkiyukiOkayasu/TestToneSet).

## Nachweis und verbleibende Risiken

Hosttests prüfen ABI-Größen, Request-Korrelation, Short Writes,
Parameterdekodierung und 0-dB-Gain. Source- und Pakettests prüfen
Default-Deny-Domänen, vollständig vermitteltes DMA, Shell-Erreichbarkeit und
identische QEMU-/VMware-Systemlayouts. Ein headless VMware-Bootsmoke verlangt
das HDA-Profil und `REIST_AUDIO SERVICE_READY`. Der QEMU-Gastnachweis prüft
tatsächliche nicht stumme PCM-Ausgabe und die Wiederverwendung nach `stop`.
Der manuelle VMware-Nachweis vom 20. August 2026 bestätigt mit der paketierten
440-Hz-WAV-Datei sowohl hörbare Ausgabe als auch den erwarteten Pegel über den
vollständig aktivierten DAC-/Mixer-/Pin-Pfad.

Noch nicht nachgewiesen sind hörbare Ausgabe auf dem ASUS-Zielsystem,
codec-spezifische Pin-Routing-Varianten, MSI, IOMMU-Direktzuweisung und die oben
aufgeführten Erweiterungen. Fehlt ein sicher akzeptierbares Profil, bleibt das
Audiosubsystem nicht verfügbar; REIST fällt nicht auf unkontrolliertes DMA
zurück.
