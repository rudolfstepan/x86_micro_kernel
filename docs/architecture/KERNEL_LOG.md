# Begrenzter Kernel-Log

## Zweck und Standardbezug

REIST hält Ring-0-Konsolenausgaben zusätzlich in einem flüchtigen Speicherlog,
damit frühe Boot- und Gerätediagnosen nicht vom 80x25-VGA-Scrollback abhängen.
Benennung und Benutzerablauf orientieren sich am Linux-Werkzeug `dmesg` und am
klassischen `more`-Pager. Es wird weder Linux-`/dev/kmsg`- noch
POSIX-Pipe-Kompatibilität behauptet; REIST verwendet einen versionierten,
fest begrenzten Snapshot-ABI.

## Ring-0-Vertrag

- Der Ring enthält genau 32768 Zeichen in statischem Speicher.
- Ausschließlich Ausgaben aus dem Kernelkontext von `printf`/`klog` werden
  erfasst. Ring-3-Terminalausgaben gelangen nicht zurück in den Kernel-Log.
- Producer reservieren atomar eine monotone Zeichensequenz und publizieren den
  Slot erst nach dem Zeichen. Reader übernehmen nur zusammenhängende Slots mit
  passender Sequenz vor und nach dem Lesen.
- Bei Kapazitätsüberlauf werden die ältesten Zeichen überschrieben und über
  `oldest_cursor`, `dropped` und `overwritten` erkennbar gemacht.
- Nach Ausschöpfung des 32-Bit-Sequenzraums wird geschlossen kein weiterer
  Slot reserviert; der saturierende Drop-Zähler bleibt lesbar.
- Capture und Read verwenden weder Heap, VFS, Formatierung noch blockierende
  Warteoperationen.

## Ring-3-Lese-ABI

Der append-only Syscall 125 liest pro Aufruf höchstens 256 Zeichen. Version,
Strukturgröße, Flags, Cursor, Zielbereich und Nichtüberlappung von Steuer- und
Datenpuffer werden vor dem Kopieren validiert. Der erste Aufruf kann explizit
am ältesten noch vorhandenen Zeichen beginnen. Jeder Aufruf liefert einen
Snapshot-Head und den nächsten Cursor; Anwendungen müssen an ihrem ersten Head
stoppen, damit fortlaufende Kernelmeldungen keinen unendlichen Read erzeugen.
Die exklusive Syscall-Grenze des Compatibility-Prozessprofils umfasst den
append-only Index 125. Engere Probe-, Treiber- und Dienstprofile erhalten den
Logzugriff nicht implizit.

## Benutzerwerkzeug

`DMESG.PRG` liegt in `/sbin` und ist über die normale Ring-3-Shell als `DMESG`
erreichbar. Standardmäßig pausiert es nach 22 Zeilen:

- Leertaste: nächste Bildschirmseite und Bildschirm löschen;
- Enter: genau eine weitere Zeile;
- Q: Ausgabe beenden.

`DMESG --no-pager` gibt denselben begrenzten Snapshot ohne Pause aus. Das
Werkzeug besitzt ein festes 256-Byte-Lesefenster, höchstens 160 Reads und acht
begrenzte Wiederholungen für einen noch nicht vollständig publizierten Slot.
Ein abgewiesener Aufruf gibt zusätzlich den negativen Fehlercode aus.
