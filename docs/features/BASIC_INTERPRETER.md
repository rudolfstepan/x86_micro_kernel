# BASIC-Interpreter

Der eingebaute BASIC-Interpreter wird mit `BASIC` aus der Kernel-Shell
gestartet. Er läuft derzeit als Kernelkomponente, nicht als isoliertes externes
MYPR-Programm.

```text
C:\> BASIC
BASIC Interpreter v1.2
```

## Editorbefehle

| Befehl | Wirkung |
|---|---|
| `RUN` | gespeichertes Programm ausführen |
| `LIST` | Programm auflisten |
| `NEW` | Programm und Variablen löschen |
| `LOAD name` | `.BAS`-Datei über VFS laden |
| `SAVE name` | Programm über VFS speichern |
| `HELP` oder `?` | Hilfe anzeigen |
| `EXIT` oder `QUIT` | zur Kernel-Shell zurückkehren |

Editorbefehle sind case-insensitiv. Eine Eingabe mit Zeilennummer speichert
oder ersetzt die entsprechende Programmzeile.

## Programmsprache

Unterstützte Anweisungen:

- `PRINT`
- `INPUT`
- `VAR`
- `IF`
- `GOTO`
- `GOSUB`
- `RET`
- `END`
- `REM`

Beispiel:

```text
10 VAR A=1
20 PRINT "HALLO"
30 PRINT A
40 IF A=5 THEN GOTO 70
50 VAR A=A+1
60 GOTO 30
70 END
RUN
```

Die genaue Ausdruckssyntax ist bewusst klein und nicht mit einem vollständigen
Microsoft BASIC gleichzusetzen.

## Laden und Speichern

Der Interpreter hängt bei Bedarf `.BAS` an und verwendet VFS. Die Datei wird
zeilenweise validiert; zu lange Zeilen, fehlende Zeilennummern, ungültige
Nummern oder ein zu großes Programm werden abgelehnt. Ein fehlgeschlagener
Ladevorgang ersetzt das bestehende Programm nicht teilweise.

```text
SAVE TEST
NEW
LOAD TEST
LIST
RUN
```

Die konkreten Dateisystemrechte hängen vom aktiven Mount ab. Im erzeugten
FAT32-VMware-Image sind Lesen und Schreiben vorgesehen.

## Grenzen

- höchstens 100 Programmslots, nutzbare Zeilennummern unter 100
- höchstens 64 Zeichen interner Zeilenpuffer
- höchstens 64 Variablen mit kurzen Namen
- Ganzzahlarithmetik
- keine Fließkommazahlen, Arrays oder Stringvariablen
- keine Ring-3-Isolation

`BASIC_INTERPRETER_UPDATES.md` ist ein historisches Änderungsprotokoll und
nicht die aktuelle Bedienungsreferenz.
