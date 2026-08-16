# BASIC-Interpreter

Stand: 16. August 2026.

`BASIC.PRG` ist ein reguläres MYPR-Programm und läuft in Ring 3 mit dem
öffentlichen `x86os_*`-SDK. Es ist keine Kernelkomponente.

```text
C:\> BASIC
BASIC Interpreter v1.2
```

## Bedienung

| Befehl | Wirkung |
|---|---|
| `RUN` | gespeichertes Programm ausführen |
| `LIST` | Programm auflisten |
| `NEW` | Programm und Variablen löschen |
| `LOAD name` | `.BAS`-Datei über VFS laden |
| `SAVE name` | Programm über VFS speichern |
| `HELP` oder `?` | Hilfe anzeigen |
| `EXIT` oder `QUIT` | zur Shell zurückkehren |

Eine Eingabe mit Zeilennummer speichert oder ersetzt die Programmzeile.
Unterstützt werden `PRINT`, `INPUT`, `VAR`, `IF`, `GOTO`, `GOSUB`, `RET`,
`END` und `REM`.

```text
10 VAR A=1
20 PRINT A
30 VAR A=A+1
40 IF A=6 THEN GOTO 60
50 GOTO 20
60 END
RUN
```

`LOAD` validiert die Datei vollständig, bevor es das laufende Programm
ersetzt. Zu lange Zeilen, ungültige Nummern und zu große Programme werden
abgelehnt. `SAVE` und `LOAD` verwenden VFS und hängen bei Bedarf `.BAS` an.

## Grenzen

- höchstens 100 Programmslots und Zeilennummern unter 100
- 64 Zeichen interner Zeilenpuffer
- höchstens 64 kurze Variablennamen
- Ganzzahlarithmetik; keine Arrays, Fließkomma- oder Stringvariablen
- keine Kompatibilitätszusage zu einem vollständigen Microsoft BASIC

`BASIC_INTERPRETER_UPDATES.md` ist ein historisches Änderungsprotokoll.
