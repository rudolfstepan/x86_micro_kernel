# Architekturüberblick

Stand: 20. August 2026

Dieses Dokument beschreibt die aktuelle 32-Bit-x86-Architektur. Das System
startet ausschließlich über den eigenen BIOS-Bootloader. Einen alternativen
Legacy-Einstieg gibt es nicht mehr.

Das neue Ziel ist ein branchenunabhängiges High-Assurance-Forschungsbetriebssystem.
Der heutige Stand ist jedoch ein nicht zertifizierter Forschungsprototyp.
Zielanforderungen, Fehlerreaktion und Nachweisregeln definiert der
[REIST High-Assurance Core Contract](HIGH_ASSURANCE_CORE_CONTRACT.md);
Medizin, Raumfahrt, Industrie und FPGA sind getrennte Referenzprofile. Dieses
Dokument beschreibt weiterhin ehrlich den ausführbaren Ist-Zustand.

## High-Assurance-Grenze und Wiederherstellungsmodell

Der heutige modulare Monolith ist eine gemeinsame Fehlerdomäne: Ein Ring-0-
Speicherfehler kann Scheduler, Treiber, Dateisystem und Diagnose gleichzeitig
beschädigen. `panic()` sichert Diagnose und hält danach die CPU an. Das ist ein
kontrollierter Stopp, aber noch kein fail-operationaler Betrieb.

Das Zielmodell ordnet Fehler nach ihrer nachweisbaren Reichweite:

| Fehlerdomäne | Zielreaktion |
|---|---|
| Ring-3-App oder nichtkritischer Dienst | einfrieren, Zustand verwerfen, aus bekannt gutem Image neu starten |
| Treiber oder Gerät | I/O sperren, Gerät zurücksetzen, Ersatzgerät/-kanal übernehmen |
| Safety-Dienst | Ausgang sicher halten, Hot-Standby übernehmen lassen, ausgefallene Instanz neu qualifizieren |
| Kernelintegrität zweifelhaft | Knoten einzäunen; unabhängiger Supervisor schaltet auf Standby und startet ihn kontrolliert neu |
| CPU, RAM, Strom oder Takt nicht vertrauenswürdig | externer Hardwarekanal hält den sicheren Zustand und isoliert den Rechner |

In-Place-Weiterlauf nach unbekannter Kernelkorruption ist ausdrücklich kein
zulässiger Recovery-Mechanismus. Unterbrechungsfreie wesentliche Leistung bei
einem Kernel-Panic ist nur mit einer unabhängigen zweiten Ausführungs- und
Überwachungsdomäne glaubwürdig erreichbar. Redundante Kanäle werden gegen
gemeinsame Fehlerursachen bewertet; zwei identische Instanzen allein sind kein
ausreichender Nachweis.

Stacküberlauf soll vor Datenkorruption durch nicht gemappte Guardpages erkannt
werden. Statische Stackbudgets, Watermarks und Rekursionsverbote verhindern die
Mehrzahl der Fälle. Ein eigener, vorallokierter Exception-/Double-Fault-/NMI-
Stack muss Diagnose und Eskalation auch dann ermöglichen, wenn der normale
Kernelstack unbrauchbar ist. Der Fatalpfad darf nicht allokieren, blockieren
oder von Dateisystem, Netzwerk, Desktop bzw. normalem Logging abhängen.

Desktop, Netzwerk, Massenspeicher und allgemeine Diagnose sind nichtkritische
Partitionen. Keine profilabhängige Essential Function darf von ihrer
Verfügbarkeit oder ihrem Timing abhängen. Der Übergang zu diesem Zielmodell ist
als Sicherheits-Gate S0 in der Roadmap geplant. Wesentliche Mechanismen sind
umgesetzt, das Gate ist wegen offener Persistenz-, Hardware-Failover- und
Zielhardware-Nachweise aber noch nicht abgeschlossen.

## Bootkette

```text
BIOS
  -> arch/x86/boot/bios/stage1_mbr.asm
  -> Manifest in der aktiven RAW-Bootpartition
  -> arch/x86/boot/bios/stage2_bios.asm
  -> A20, E820, ELF32-Laden und CRC32-Prüfung
  -> optional VBE-LFB (1024x768x32, Rückfall 800x600x32)
  -> Protected Mode
  -> Multiboot-1-kompatibler Handoff
  -> arch/x86/boot/multiboot.asm
  -> kernel_main()
```

Stage 1 ist exakt ein MBR-Sektor. Stage 2 liest Kernel und Manifest per BIOS
EDD/INT 13h, validiert 32-Bit-i386-ELF und lädt nur `PT_LOAD`-Segmente in
zulässige physische Bereiche. Bereiche mit `p_memsz > p_filesz` werden für
BSS genullt. Im Framebuffer-Build wählt Stage 2 unmittelbar vor dem
Protected-Mode-Handoff einen linearen 32-Bit-Direct-Color-VBE-Modus und
veröffentlicht dessen Metadaten. Jeder Fehler stellt BIOS-Modus 03h wieder her
und lässt das Framebuffer-Flag ungültig. Die Multiboot-Struktur bleibt eine
interne Übergabeschnittstelle; sie bedeutet nicht, dass der native Weg GRUB
benötigt.

## Kernelinitialisierung

`kernel_main()` arbeitet in klaren Stufen:

1. Multiboot-Magic und Informationszeiger prüfen
2. Bootinformationen und Speicherkarte auswerten
3. VGA oder den vom nativen VBE-Pfad übergebenen Framebuffer initialisieren
4. Kernel-Allocator initialisieren
5. TSS, GDT, IDT, Exceptions, IRQs, PIT und PS/2-Tastatur aufsetzen
6. APIC-Timer gegen PIT kalibrieren oder PIT-Scheduler-Fallback wählen sowie
   PCI und experimentelles USB-Probing starten
7. Netzwerktreiber passend zu erkannten PCI-Geräten registrieren
8. ATA/PCI-IDE, AHCI/SATA und Disketten erkennen
9. MBR-Partitionen veröffentlichen, Storage-Safety/-Service initialisieren
10. VFS initialisieren und bevorzugtes Root-Volume deterministisch mounten
11. überwachte Ring-3-Dienste und Supervisor-Worker starten
12. bei einem echten Framebuffer `DESKTOP.PRG`, sonst `SHELL.PRG` starten

COM1 wird früh initialisiert, damit auch Fehler vor der VGA-Shell in einem
seriellen Log sichtbar bleiben. Die gemeinsame Anzeige spiegelt Console-Text
auch im Framebuffer-Modus genau einmal nach COM1.

## CPU-Tabellen und Interrupts

Der verbindliche Kontext- und Lockingvertrag steht in
[SYNCHRONIZATION_CONTRACT.md](SYNCHRONIZATION_CONTRACT.md).

- Die GDT enthält Kernelsegmente und einen TSS-Deskriptor.
- Die IDT deckt CPU-Ausnahmen, Hardware-IRQs und den Syscall-Einstieg ab.
- Assembly-Stubs sichern den Registerzustand und rufen C-Handler auf.
- IRQ0 schreibt die monotone 64-Bit-PIT-Zeit fort und weckt fällige Sleeper.
- Der lokale APIC-Timer wird gegen PIT kalibriert und liefert im Normalfall
  die Scheduler-Ereignisse.
- Ohne LAPIC übernimmt IRQ0 nach dem PIC-EOI das Scheduler-Quantum.
- Präemptionskritische Kernelbereiche verwenden eigene Guards.

IRQ-Handler sollen kurze Hardwarearbeit erledigen und keine dauerhaften
VGA-Debugmeldungen ausgeben. Insbesondere Netzwerk-RX wird über die
gemeinsame `netdev`-Schicht weiterverarbeitet, ohne die Shellausgabe mit jedem
Interrupt zu unterbrechen.

## Zeitquellen und Timer

Der aktive monotone Clocksource ist der PIT. IRQ0 akkumuliert den tatsächlich
programmierten PIT-Divisor als Millisekunden plus Bruchteil in einem
64-Bit-Zähler. Damit hängt die Zeit nicht mehr an einem 32-Bit-Wrap und der
gerundete Divisor wird nicht fälschlich als exakt 1 kHz behandelt. Da ein
64-Bit-Zugriff auf i386 nicht atomar ist, lesen Kernel und Syscall den Zähler
mit lokal gesperrten Interrupts; IRQ0 ist der einzige Schreiber.

Der LAPIC ist kein unabhängiger Clocksource, sondern der bevorzugte
Scheduler-Timer. Beim Start läuft er zunächst maskiert als One-shot, wird über
ein PIT-Zeitfenster kalibriert und danach periodisch auf das Scheduler-Quantum
programmiert. Ist kein lokaler APIC vorhanden, teilt der PIT-Pfad seine IRQs
auf dasselbe Quantum herunter. Der Kontextwechsel erfolgt dort erst, nachdem
der PIC den EOI erhalten hat.

Der vorhandene HPET-Code ist experimentell und nicht Teil des aktiven
Zeitpfads. HPET bleibt bis zur zentralen, validierten ACPI-/MMIO- und
Interrupt-Routing-Schicht in R5.1 zurückgestellt.

## Speicher

Der Kernel verwendet einen eigenen Frame- und Heap-Allocator in
`mm/kmalloc.c` und die x86-Paging-Unterstützung in
`arch/x86/mm/paging.c`. Die vom Bootloader übergebene E820-Karte wird sortiert
und überlappungsfrei normalisiert. Reservierte Firmwarebereiche,
Multiboot-Strukturen und Module haben Vorrang vor nutzbaren Einträgen. Eine
beschädigte oder abgeschnittene Karte und Kapazitätsfehler der festen
Regionstabellen brechen die Speicherkartenübernahme fail-closed ab.

Der gemeinsam in alle Adressräume eingeblendete, Supervisor-only Kernelanteil
ist ein Directmap der ersten 1 GiB und endet exakt bei `USER_BASE` auf
`0x40000000`. Der Frame-Allocator verwaltet nur vollständige nutzbare
E820-Seiten innerhalb dieses Fensters. Ein umlaufender Next-Fit-Hinweis
begrenzt wiederholte Suchen sowohl nach einzelnen als auch nach
zusammenhängenden Frames. Von E820 gemeldeter Speicher oberhalb 1 GiB wird
erkannt und in der Statistik ausgewiesen, ohne Highmem-/`kmap`-Fenster aber
nicht vergeben. Für den verwalteten Bestand gilt:

```text
managed = reserved + allocated + free
```

Der Kernel-Heap beginnt mit ungefähr 1 MiB und wächst bei Bedarf um mindestens
256 KiB. Jede Erweiterung reserviert zusammenhängende Frames innerhalb des
Directmaps dauerhaft als Heap-Backing. Eine adresssortierte Blockliste teilt
Blöcke und vereinigt ausschließlich physisch angrenzende freie Nachbarn; über
Lücken getrennte Arenen bleiben getrennt. Der Heap schrumpft derzeit nicht.
Da Allokation und Freigabe die IRQ-sperrenden Heap-/Frame-Locks verwenden und
eine Suche auslösen können, sind `k_malloc`, `k_realloc` und `k_free` nicht für
harten IRQ-Kontext bestimmt.

Der Programmlader liest MYPR-Dateien in einen passend großen temporären
Kernel-Heap-Puffer und validiert dort das vollständige PRG-v1-Image. Danach
kopiert er den gespeicherten Anteil in private Prozessseiten ab `0x40000000`,
nullt BSS und gibt den Stagingpuffer auf Erfolgs- und Fehlerpfaden wieder frei.
Der Ring-3-Start hängt deshalb nicht mehr von einem festen physischen
Stagingbereich ab und funktioniert auch in der 32-MiB-Testkonfiguration.

Programmtasks laufen in Ring 3 mit eigener Seitentabelle und Userstack. Zwei
nicht-präsente Guardpages begrenzen jeden Userstack nach unten und oben. Der
gemeinsam eingeblendete Kernelanteil bleibt Supervisor-only. Syscall-Pointer
werden vor Zugriffen bereichsweise geprüft und über Kopierfunktionen zwischen
User- und Kerneladressraum übertragen.

Die Speicherstatistik unterscheidet E820-erkannt, tatsächlich verwaltet,
reserviert, alloziert und frei. Zusätzlich meldet sie Heap-Backing, belegte
und freie Payloadbytes, den größten freien Block und die Zahl der
Wachstumsarenen. `used + free` kann wegen Blockheadern und Alignment kleiner
als die gesamte Heap-Kapazität sein.

## Scheduler und Prozesse

Für `*_locked`-, Präemptions- und Sleep-APIs gilt der
[Synchronisationsvertrag](SYNCHRONIZATION_CONTRACT.md).

Der Scheduler verwaltet bis zu 32 Tasks mit je 8 KiB Stack und den Zuständen
ready, running, sleeping, waiting, finished und dem internen Übergangszustand
reaping. Ein Prozessdatensatz ordnet PID, Generation, Task-ID, Namen und
Programmbild zu. Timerpräemption kann für kritische Operationen vorübergehend
unterdrückt werden.

Dynamisch angelegte Kernel-Taskstacks liegen nun in einer reservierten
virtuellen Arena. Jeder 8-KiB-Stack besitzt darunter und darüber eine
nicht-präsente 4-KiB-Seite; seine zwei Datenseiten werden aus unabhängigen
physischen Frames abgebildet. Der statische 8-KiB-Boot-/Rescue-Stack besitzt
ebenfalls eine volle untere Guardpage, die unmittelbar beim Aktivieren von
Paging nicht-präsent wird. Schedulergrenzen prüfen Stackslot, Mappings und den
gespeicherten beziehungsweise aktuellen ESP-Bereich.

Vektor 8 verwendet eine zweite TSS als Task-Gate mit eigenem vorallokiertem
4-KiB-Emergency-Stack. Damit hängt Double-Fault-Eskalation nicht vom
möglicherweise zerstörten normalen Stack ab. Der aktuelle Minimalpfad schreibt
einen festen Crashrecord, sendet mit begrenztem Pollbudget
`REIST_FATAL DOUBLE_FAULT` an COM1 und hält anschließend kontrolliert. Externer
Watchdog, persistenter Boot-übergreifender Crashrecord und realer Failover sind
noch folgende REIST-Schritte.

Jeder Task besitzt genau einen intrusiven Wait-Queue-Knoten und kann deshalb
höchstens auf ein Ereignis gleichzeitig warten. Die generischen Operationen
unterstützen FIFO-Einreihung, Wake-one, Wake-all, geordnete 64-Bit-Deadlines
und das Entfernen eines wartenden Tasks. Ihr Synchronisationsvertrag ist auf
dem aktuellen Single-Core-System: Bedingung prüfen, Queue verändern und
Taskstatus wechseln erfolgen mit deaktivierten Interrupts. Wakeup markiert den
Task als ready; beliebige Geräte-IRQs wechseln nicht unmittelbar den Kontext.

`wait(pid)` prüft den Kindstatus und registriert den Eltern-Task atomar auf der
kindeigenen Exit-Queue. Exit und Kill veröffentlichen den Status vor Wake-all.
Kill, Exit und Task-Slot-Wiederverwendung lösen bestehende Queue-Mitgliedschaft,
damit kein veralteter intrusiver Knoten zurückbleibt.

Beim Reaping werden Besitzerzeiger und Prozessgeneration unter deaktivierten
Interrupts validiert. Seitentabelle und Kernelstack werden atomar vom Task
abgetrennt und der Slot wechselt zu `TASK_REAPING`. Die proportional teure
Freigabe der Userseitentabellen, Frames und Heapallokation erfolgt danach mit
aktivierten Hardware-Interrupts, während Taskpräemption unterdrückt bleibt.
Erst ein kurzer abschließender kritischer Abschnitt setzt den leeren Slot
wieder auf `finished` und damit wiederverwendbar. Das verhindert Slot-ABA und
hält lange Page-Directory-Walks aus dem IRQ-gesperrten Abschnitt heraus.

`sleep_ms` reiht den aktuellen Task nach seiner absoluten 64-Bit-Deadline ein,
setzt ihn auf sleeping und schaltet zu einem lauffähigen Task oder in den
gesicherten Kernelkontext. IRQ0 setzt abgelaufene Sleeper wieder auf ready.
`yield` gibt das aktuelle Quantum unmittelbar an den nächsten bereiten Task ab;
ohne Konkurrent bleibt der Aufrufer running.

`RUN`/`EXEC` laden eine MYPR-Datei über VFS, validieren den vollständigen
Header und erzeugen erst danach einen Task. Der Startup-Code externer
Programme ruft `main()` auf und endet über den Exit-Syscall.

## Syscalls

Die öffentliche SDK-Schicht kapselt den Low-Level-Einstieg über `INT 0x80`.
Sie bietet Terminal-, Speicher-, Datei-, Verzeichnis-, Prozess- und
Zeitoperationen. Externe Programme sollen nur
`userspace/sdk/include/x86os.h` einbinden, keine internen Kernelheader.

Neue Funktionen hängen Nummern an die bestehende ABI an, ohne alte Programme
umzunummerieren:

| Nummer | SDK/API | Vertrag |
|---:|---|---|
| 2 | `x86os_delay(uint32_t)` | kompatibler, für Ring 3 blockierender Delay |
| 12 | `x86os_uptime_ms()` | niedriges 32-Bit-Wort der monotonen Zeit |
| 13 | `x86os_memory_kb()` | verwalteter physischer Speicher in KiB, auf 32 Bit gesättigt |
| 40 | `x86os_yield()` | freiwillige Abgabe an einen bereiten Task |
| 41 | `x86os_sleep_ms(uint32_t)` | blockierender Millisekunden-Sleep |
| 42 | `x86os_monotonic_ms(uint64_t *)` | geprüfte 64-Bit-Ausgabe per User-Pointer |
| 43 | `x86os_memory_stats(x86os_memory_stats_t *)` | versionierte v1-Speicherstatistik per geprüftem Copyout |
| 44 | `x86os_display_info(x86os_display_info_t *)` | versionierte Framebuffergeometrie, RGB-Masken und Schriftmetrik |
| 45 | `x86os_fill_rect(...)` | geclipptes Rechteck in `0x00RRGGBB` |
| 46 | `x86os_draw_text_pixels(...)` | geclippte Pixelschrift mit höchstens 256 Zeichen je Aufruf |
| 115 | `x86os_draw_text_pixels_clipped(...)` | pixelgenauer Textclip für partielle Compositor-Redraws |

Der generische Syscall-Rückgabekanal bleibt 32 Bit breit. Die monotone
64-Bit-Zeit wird deshalb mit `copy_to_user` in einen zuvor validierten
Userspace-Puffer geschrieben. Ring-0-Aufrufe des historischen Delay-Pfads
bleiben aktive Kernel-/Hardware-Wartevorgänge; nur ein Ring-3-Aufrufer wird in
die Sleep-Queue eingereiht.

Die v1-Struktur für Syscall 43 ist 88 Byte groß: Auf `uint32_t version` und
`uint32_t struct_size` folgen zehn `uint64_t`-Felder. Der SDK-Wrapper übergibt
Pointer, Strukturgröße und `X86OS_MEMORY_STATS_VERSION`; der Kernel lehnt eine
unbekannte Version oder einen zu kleinen Puffer vor `copy_to_user` ab. Sie
trennt insbesondere `detected_usable_bytes` von `managed_bytes`, während der
kompatible Syscall 13 ausschließlich die verwalteten KiB liefert.

Die Display-ABI v1 verwendet ebenfalls `version` und `struct_size`. Der Kernel
kopiert Requests und Text über die geprüften User-Copy-Hilfen, clippt Rechtecke
und Text am sichtbaren Bereich und wandelt `0x00RRGGBB` in das vom
Bootloader gemeldete Pixelformat. Der append-only Textclip-Syscall 115 bewahrt
die alte 32-Byte-Textrequest-ABI und ergänzt eine eigene 48-Byte-Requeststruktur
mit geprüftem Clip-Rechteck. Der lineare Framebuffer bleibt ausschließlich
Supervisor-MMIO und wird nicht in Ring 3 gemappt.

Die Dateiinformation erweitert den bestehenden Namen/Typ/Größen-Präfix
append-only um `create_time`, `modify_time` und `access_time`. `stat` sowie
`readdir_batch` kopieren diese Felder nach Ring 3. Syscall 108,
`x86os_touch()`, aktualisiert die FAT-Zeitfelder über VFS; die alte
Syscallnummerierung bleibt unverändert.

## Grafik, Desktop und Surface-Clients

Ein `VIDEO=framebuffer`-Build lässt den nativen BIOS-Loader bevorzugt
1024x768x32 und danach 800x600x32 anfordern. Nur ein erfolgreich gesetzter
linearer Direct-Color-Modus aktiviert den Framebuffertreiber. Ohne ihn bleibt
VGA-Text aktiv und der normale Shellstart unverändert.

Auf einem echten Framebuffer startet der Kernel den kanonischen
`/usr/gui/bin/desktop.prg` vor `SHELL.PRG`; nach einem VGA-Textboot kann die
Ring-3-Shell denselben Compositor starten und damit den validierten QEMU-,
VMware- oder vorbereiteten VBE-Pfad aktivieren. `Esc` deaktiviert eine solche
Laufzeitsitzung und stellt VGA samt Shell wieder her.

Der Desktop besitzt Maus- und Tastaturfokus, Z-Order, implizites
Pointer-Capture, verschiebbare und achtseitig skalierbare Fenster sowie einen
begrenzten Explorer. Ordnerinhalte werden als Icons gerendert;
Dateizuordnungen stammen aus `/etc/reist/filetypes.conf`. Notepad und Image
Viewer bleiben als separate Ring-3-Prozesse in compositorverwalteten Fenstern
aktiv. Die versionierte, generationsgebundene Surface-/Event-IPC kennt
Configure/Ack, Retained-Fill/Text, XRGB8888-Buffer, Damage, Commit und Close.
Der Compositor allein besitzt globale Platzierung und Displaypublikation;
Clients erhalten weder Framebufferautorität noch globale Koordinaten. Noch
nicht migrierte Programme verwenden eine begrenzte Vollbildbrücke.

## Console-Eingabe

Nur die PS/2-Tastatur speist die Console-Eingabe. COM1 ist Diagnoseausgabe und
injiziert keine Tastencodes. Ein blockierendes `getchar` prüft den
Eingabepuffer und registriert den Task unter derselben
IRQ-geschützten Synchronisationsgrenze auf `input_waiters`; dadurch kann
zwischen Leerprüfung und Blockieren kein Zeichen-Wakeup verloren gehen.
IRQ1 beziehungsweise der begrenzte PS/2-Polling-Fallback wecken die Leser;
jeder prüft den level-getriggerten Pufferzustand erneut und reiht sich bei
Bedarf wieder ein. Frühe
Kernelkontexte oder Aufrufe mit deaktivierter Präemption, die nicht sicher
blockieren können, behalten einen HLT-basierten Fallback.

## Dateisysteme

Die VFS-Mounttabelle wählt anhand des längsten passenden Mountpfades einen
Adapter. FAT32, FAT12 und EXT2 werden beim Boot registriert. Shell und
Programmlader arbeiten mit absoluten VFS-Pfaden; DOS-Laufwerksnotation wird
vorher im Shellresolver normalisiert.

### Dateimetadaten und Userspace-Werkzeuge

Die VFS-Eintragsstruktur führt Typ, Größe, Inode, Attribute und drei
Zeitstempel. `fs/vfs/vfs_time.h` validiert FAT-Kalenderwerte und konvertiert
sie in Sekunden seit 1970-01-01. Die Konvertierung ist bewusst unabhängig von
libc und begrenzt; ungültige On-Disk-Werte werden als Zeit `0` gemeldet.
FAT-Schreibzeiten besitzen die FAT-Auflösung von zwei Sekunden, FAT-
Zugriffszeiten nur Tagesauflösung. Eine Zeitzonenverwaltung ist nicht Teil des
aktuellen RTC-Vertrags.

Die Systemprogramme liegen wie bei Linux nach Funktion unter `/bin`, `/sbin`
und `/usr/bin`. Die Dateiverwaltung umfasst `rename`, `stat`, `df`, `touch`,
`tree`, `find` und `rm --recursive`; Shell-Aliase sind `ren`, `mv` und `cp`.
Traversierungen sind auf 16 Ebenen und 512 Einträge begrenzt. `rm` verweigert
Root-Pfade und löscht Verzeichnisse nur mit explizitem `--recursive`.

Öffentliche VFS- sowie synchrone ATA-/AHCI-/FDD-Transaktionen laufen nach dem
[Synchronisationsvertrag](SYNCHRONIZATION_CONTRACT.md) mit aktiven IRQs unter
einem nestbaren Präemptionsguard. Netzwerk- und HPET-IRQs quittieren nur die
Hardware und markieren Arbeit; begrenzte Netzwerk-Polling-Pässe verarbeiten
sie anschließend im Foreground-Kontext.

## Treiber

| Bereich | Aktuelle Komponenten |
|---|---|
| Block | ATA/PCI-IDE, AHCI/SATA, MBR-Partitionen, Floppy |
| Eingabe | PS/2-Tastatur mit IRQ1/Poll-Fallback, experimentelles xHCI-HID für Boot-Tastatur und -Maus; COM1 nur Ausgabe |
| Anzeige | VGA-Text, VBE/QEMU-DISPI/VMware-SVGA, Shadowbuffer, Damage-/Frame-ABI und Ring-3-Surface-Compositor |
| Bus | PCI, USB-Hostcontroller-Probing |
| Netzwerk | E1000, RTL8139, RTL8168/8111G und NE2000 über `netdev` |
| Audio | kernelvermitteltes PCI-HDA-Geräteprofil, Ring-3-HDA-Treiber und getrennter PCM-Service |
| Zeit | monotone 64-Bit-PIT-Zeit, RTC, kalibrierter lokaler APIC-Timer mit PIT-Fallback; HPET noch inaktiv |

Die generierte VMware-Referenzmaschine verwendet SATA/AHCI, VMware SVGA,
PS/2-Tastatur, eine virtuelle xHCI-HID-Maus, E1000 und HDA. Physisches
HID-Passthrough ist ausdrücklich deaktiviert. QEMU behält ATA/IDE als
separaten Regressionspfad und stellt zusätzliche Grafik-, Surface- und
Audio-Gastnachweise bereit.

Der reale QEMU-Framebuffer-Boot bestätigt VBE-Handoff, Kernelinitialisierung
und den ersten Ring-3-Renderdurchlauf über den seriellen Marker `DESKTOP_OK`.
VGA- und VBE-Fehlerpfad bleiben getrennte, sichere Rückfälle.

## Verifikation des Speicherpfads

Die R1.2-Abnahme kombiniert Hostregressionen, Boot-Selbsttests und den realen
Ring-3-Smoke-Test. Der Gasttest prüft die Framezähler-Invariante, versionierte
Statistiken und User-Pointer, Allokation/Freigabe sowie 64 aufeinanderfolgende
Spawn/Exit/Wait/Reap-Zyklen ohne Frame- oder Heapdrift. Die QEMU-Matrix umfasst
32, 64, 256, 512 und 1024 MiB. Der 512-MiB-Referenzlauf wird sowohl mit dem
gegen PIT kalibrierten LAPIC-Timer als auch ohne APIC über den
PIT-Scheduler-Fallback ausgeführt; der High-Frame-Selbsttest schreibt und liest
zusätzlich einen Frame ab 256 MiB.

Noch nicht enthalten sind systematische Failure-Injection für jede einzelne
Teilallokation, ein Highmem-/`kmap`-Fenster für Speicher oberhalb 1 GiB und ein
für harten IRQ-Kontext geeigneter Allocator.

## Wichtige Grenzen

- BIOS/MBR statt UEFI
- i386 statt x86-64
- kein SMP-Scheduler
- feste Obergrenze von 32 Tasks und genau ein Wait-Ereignis pro Task
- der Frame-Allocator verwaltet höchstens die ersten 1 GiB; höherer
  E820-Speicher wird bis zu einer Highmem-/`kmap`-Lösung nur erkannt
- Kernel-Heap-Operationen sind nicht für harten IRQ-Kontext bestimmt
- Kernel- und Userstacks besitzen Guardpages; dynamisches Stackwachstum fehlt
- begrenzte UDP-/TCP-/DNS-ABI statt vollständiger POSIX-Sockets; kein IPv6
- HPET und IOAPIC warten auf die validierte ACPI-/Plattformschicht
- Surface-IPC und Window Manager sind vorhanden, aber erst Notepad und Image
  Viewer sind echte externe Fensterclients; weitere GUI-Programme verwenden
  noch die Vollbildbrücke
- USB und unterschiedliche reale VBE-Implementierungen sind nicht so
  umfassend verifiziert wie der VMware-VGA-/PS/2-/E1000-Weg

## Quellreferenzen

- `arch/x86/boot/bios/` – native Bootstufen
- `arch/x86/cpu/` – GDT, IDT, ISR, IRQ und Syscalls
- `arch/x86/mm/` und `mm/` – Paging, E820-Normalisierung, Frames, Heap und
  Speicherstatistik
- `config/klink.ld` – statischer Kernelstack und seine Guardpage
- `kernel/init/kernel.c` – Initialisierungsreihenfolge
- `kernel/sched/` und `kernel/proc/` – Tasks, Wait-Queues und Prozesse
- `kernel/time/` – monotone PIT-Zeit und kalibrierter LAPIC-Timer
- `kernel/shell/` – Shell und Pfadauflösung
- `fs/vfs/` – Mount- und Dateisystemabstraktion
- `drivers/video/` – VGA, Framebuffer, Console-Spiegelung und Zeichenprimitive
- `drivers/` – weitere Hardwaretreiber
- `userspace/sdk/` – externe Programmschnittstelle
- `userspace/gui/` – Session-Compositor, versionierte Control- und Surface-
  APIs, Bibliotheken, Anwendungen und Ressourcen; der Compositor liegt unter
  `userspace/gui/compositor/`
