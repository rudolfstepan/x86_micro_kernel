# Shell, Befehle und Pfade

Stand: 16. August 2026.

Die Shell orientiert sich bei Navigation und Dateibefehlen an MS-DOS, nutzt
intern aber ausschließlich kanonische VFS-Pfade. Der Prompt zeigt das aktuelle
DOS-Laufwerk und Verzeichnis, beispielsweise `C:\TOOLS>`.

## Befehle

| Zweck | Befehl und Alias |
|---|---|
| Hilfe/Bildschirm | `HELP`, `CLS`, `CLEAR`, `ECHO` |
| Auflisten | `DIR [pfad]`, `LS [pfad]` |
| Verzeichnis wechseln | `CD [pfad]`, `CHDIR [pfad]` |
| Verzeichnis anlegen | `MD pfad`, `MKDIR pfad` |
| Verzeichnis entfernen | `RD pfad`, `RMDIR pfad` |
| Datei anzeigen | `TYPE datei`, `OPEN datei` |
| Datei anlegen/löschen | `MKFILE datei`, `DEL datei`, `ERASE`, `RMFILE` |
| Kopieren | `COPY quelle ziel` |
| Laufwerke | `DRIVES`, `MOUNT laufwerk`, `C:`, `hdd0p2:` |
| Programme | `RUN datei.PRG`, `EXEC datei.PRG`, `PS`, `KILL`, `BASIC` |
| Netzwerk | `GETIP`, `IFCONFIG`, `PING`, `ARP`, `NET` |
| Diagnose | `MEM`, `DUMP`, `PCI`, `IRQ`, `SYS`, `DATETIME` |

Der reguläre Bootpfad lädt `SHELL.PRG` als Ring-3-Command-Line-Interpreter.
Die fest einkompilierte Kernel-Shell ist ausschließlich die Rettungskonsole,
falls das Userspace-Programm nicht geladen werden kann oder beendet wird.

Die Userspace-Shell verwaltet einen eigenen `PATH`. Das aktuelle Verzeichnis
wird zuerst geprüft, anschließend die mit Semikolon getrennten
Suchverzeichnisse. Standardmäßig verweist `PATH` auf das Stammverzeichnis des
Bootlaufwerks.

Befehlsnamen sind unabhängig von Groß-/Kleinschreibung. Argumente werden
nicht pauschal großgeschrieben. Dateinamen auf FAT werden beim Nachschlagen
case-insensitiv behandelt.

## Pfadformen

Alle folgenden Formen werden vom selben Resolver verarbeitet:

```text
README.TXT             relativ zum aktuellen Verzeichnis
.\README.TXT           explizit relativ
..\BIN\APP.PRG         mit Elternverzeichnis
\DOCS\README.TXT       absolut auf aktuellem Laufwerk
C:\DOCS\README.TXT     DOS-Laufwerk
C:DOCS\README.TXT      relativ zum gemerkten C:-Verzeichnis
hdd0p2:/DOCS/README.TXT nativer Partitionsname
/hdd0p2/DOCS/README.TXT ältere kompatible VFS-Schreibweise
```

`/` und `\` dürfen gemischt werden. Mehrfache Trennzeichen und `.` werden
entfernt; `..` steigt höchstens bis zur Laufwerkswurzel auf. Ein zu langer
oder ungültiger Pfad wird abgelehnt, nicht abgeschnitten.

## Laufwerkswechsel

Gemountete Festplatten-/Partitionsvolumes werden ab `C:` und Disketten ab
`A:` zugeordnet. Das Root-Volume erhält `C:`; konkrete Gerätenamen und
Resource-IDs werden immer mit `DRIVES` ermittelt:

```text
hdd0p2 -> C:  hdd1p1 -> D:
fdd0 -> A:    fdd1 -> B:
```

Ein reines Laufwerkstoken wechselt das aktive Laufwerk:

```text
C:\DOCS> D:
D:\> C:
C:\DOCS>
```

Jedes Laufwerk merkt sein eigenes aktuelles Verzeichnis. Ein Pfad mit
explizitem Laufwerk greift auf dieses Laufwerk zu, ohne bei reinen Lese- oder
Dateioperationen den Prompt dauerhaft umzuschalten. `CD D:\TOOLS` wechselt
dagegen bewusst Laufwerk und Verzeichnis.

## Einheitliche VFS-Verwendung

`DIR`, `CD`, `TYPE`, Mutationen, `COPY`, `RUN` und `EXEC` rufen keine globale
FAT-Sonder-API mehr auf. Dadurch kann eine Datei nicht mehr in `DIR`
erscheinen und gleichzeitig für `TYPE` „nicht gefunden“ sein, nur weil beide
Befehle unterschiedliche Pfadschichten verwenden.

`TYPE` liest Dateien blockweise und benötigt weder eine NUL-Terminierung noch
einen komplett im Speicher liegenden Inhalt. `COPY` überschreibt kein
vorhandenes Ziel und entfernt ein unvollständiges Ziel nach einem Fehler.
`CD` übernimmt einen neuen Pfad erst, nachdem VFS dessen Verzeichnisstatus
bestätigt hat.

## Parser

- maximal 256 Zeichen pro Eingabezeile
- maximal 16 Argumente
- Leerzeichen und Tabs trennen Argumente
- doppelte Anführungszeichen schützen Leerzeichen in einem Argument
- zu lange Eingaben, zu viele Argumente und offene Anführungszeichen werden
  als Syntaxfehler gemeldet

Beispiel:

```text
C:\> ECHO "ein Argument mit Leerzeichen"
```

## Zeilenbearbeitung

| Taste | Wirkung |
|---|---|
| Links/Rechts | Cursor innerhalb der Zeile bewegen |
| Pos1/Ende | Anfang/Ende der Eingabe |
| Entf | Zeichen unter dem Cursor löschen |
| Rücktaste | Zeichen vor dem Cursor löschen |
| Hoch/Runter | Verlauf durchsuchen und Entwurf wiederherstellen |
| `Ctrl+C` | aktuelle Eingabe verwerfen |
| `Ctrl+L` | Bildschirm löschen und Eingabe neu zeichnen |
| `Ctrl+U` | komplette Zeile löschen |
| `Ctrl+K` | vom Cursor bis Zeilenende löschen |

`HISTORY` zeigt bis zu 50 gespeicherte Befehle. Unmittelbar aufeinanderfolgende
Duplikate werden nicht erneut aufgenommen.

Tab vervollständigt das aktuelle Wort. Am Zeilenanfang werden Built-ins,
Aliase und `.PRG`-Programme aus dem aktuellen Verzeichnis und `PATH`
durchsucht; die Erweiterung `.PRG` muss dabei nicht eingegeben werden. Bei
Argumenten werden Dateien und Verzeichnisse relativ zum aktuellen oder
explizit angegebenen Pfad ergänzt. Eindeutige Verzeichnisse erhalten einen
abschließenden Backslash.

## Beispiele

```text
C:\> DIR
C:\> MD TEST
C:\> CD TEST
C:\TEST> MKFILE INFO.TXT
C:\TEST> TYPE ..\README.TXT
C:\TEST> COPY ..\HELLO.PRG APP.PRG
C:\TEST> RUN APP.PRG
C:\TEST> CD \
C:\> RD TEST
```

Ein nicht leeres Verzeichnis oder ein aktives aktuelles Verzeichnis wird vom
Dateisystem bzw. der Shell nicht blind entfernt; die konkrete Unterstützung
hängt vom gemounteten Dateisystemadapter ab.
