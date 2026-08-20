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

## HDA-Zustandsautomat

Der HDA-Treiber prüft Controller-Version, GCAP, Outputstreams, Codecpräsenz und
Widgettopologie. Reset-, Codecverb- und Stream-Waits besitzen feste Pollgrenzen
und geben bei Fristablauf einen Fehler zurück. Der Ausgangsverstärker wird über
den standardisierten Parameter `Output Amplifier Capabilities` abgefragt; der
Offset der Capability bestimmt den gültigen 0-dB-Gain. Es wird kein
codec- oder emulatorabhängiger Lautstärkewert fest eingebaut.

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
`/usr/bin/audiotest.prg`. Beide Diagnoseprogramme sind über die normale
Ring-3-Shell erreichbar:

```text
C:\> AUDIOINFO
C:\> AUDIOTEST
```

`AUDIOTEST` erzeugt einen begrenzten 440-Hz-Testton, startet und stoppt den
Stream und schließt ihn anschließend. Der QEMU-Runtimetest führt diese Folge
zweimal aus und akzeptiert nur eine korrekt formatierte, nicht stumme
Stereo-S16-Aufzeichnung. VMware stellt ein virtuelles `hdaudio` bereit; reale
Codecs und physische Ausgänge benötigen weiterhin einen eigenen Hardwaretest.

## Nachweis und verbleibende Risiken

Hosttests prüfen ABI-Größen, Request-Korrelation, Short Writes,
Parameterdekodierung und 0-dB-Gain. Source- und Pakettests prüfen
Default-Deny-Domänen, vollständig vermitteltes DMA, Shell-Erreichbarkeit und
identische QEMU-/VMware-Systemlayouts. Der Gastnachweis prüft tatsächliche
nicht stumme PCM-Ausgabe und die Wiederverwendung nach `stop`.

Noch nicht nachgewiesen sind hörbare Ausgabe auf dem ASUS-Zielsystem,
codec-spezifische Pin-Routing-Varianten, MSI, IOMMU-Direktzuweisung und die oben
aufgeführten Erweiterungen. Fehlt ein sicher akzeptierbares Profil, bleibt das
Audiosubsystem nicht verfügbar; REIST fällt nicht auf unkontrolliertes DMA
zurück.
