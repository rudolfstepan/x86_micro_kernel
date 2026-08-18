# REIST-OS-Quellcodedokumentation

Stand: 18. August 2026

## Ziel

Die Kommentare im Quellcode bilden die technische Dokumentationsschicht direkt
an der Implementierung. Sie erklären Verträge und Entwurfsentscheidungen, die
aus einzelnen Anweisungen nicht zuverlässig ableitbar sind. Kommentare dürfen
weder Zertifizierung behaupten noch ein Verhalten versprechen, das nicht durch
Code und Tests nachgewiesen ist.

## Verbindlicher Modulheader

Jede gepflegte `.c`-, `.h`- und `.asm`-Datei beginnt vor Includes,
Include-Guards oder Instruktionen mit einem Modulheader. C-Dateien verwenden
`/** ... */`, Assemblerdateien die Kommentarzeichen ihres Assemblers. Der
Header enthält:

- `@file`: repositoryrelativer Dateiname,
- `@brief`: eine präzise Zusammenfassung der Verantwortung,
- `Layer`: Architektur- bzw. Ausführungsschicht,
- `Contract`: wichtigste Eingangs-, Zustands- und Fehlergarantien,
- `Safety`: Begrenzungen oder fail-closed Verhalten, soweit relevant.

Nicht zutreffende Sicherheitsangaben werden nicht künstlich ergänzt. Öffentliche
Header beschreiben den von Aufrufern sichtbaren Vertrag; Implementierungsdateien
beschreiben Ownership, Zustandsmodell und Seiteneffekte.

Beispiel:

```c
/**
 * @file kernel/ipc/ipc.c
 * @brief Capability-geschützte, begrenzte Interprozesskommunikation.
 *
 * Layer: Ring-0 kernel service.
 * Contract: Handles sind generationsgebunden; Nachrichten werden vor dem
 *           Veröffentlichen vollständig validiert und kopiert.
 * Safety: Endpunkte, Fähigkeiten, Nachrichtengröße und Wartezeit sind fest
 *         begrenzt. Ungültige Autorität führt ohne Seiteneffekt zum Fehler.
 */
```

## Inline-Kommentare

Ein Inline-Kommentar ist erforderlich, wenn mindestens einer dieser Punkte
nicht unmittelbar aus Typen und Funktionsnamen hervorgeht:

- Invariante oder zulässiger Zustandsübergang,
- Ownership, Lebensdauer oder Generation einer Ressource,
- Lock-, IRQ- oder Scheduler-Voraussetzung,
- Hardwareprotokoll, Registerreihenfolge oder erforderliche Barriere,
- Deadline, Retry-Budget oder Kapazitätsgrenze,
- Validierungsreihenfolge vor einem Seiteneffekt,
- fail-closed Entscheidung, Quarantäne oder Degradation,
- ABI-, On-Disk- oder Netzwerkformat und Byte-Reihenfolge,
- absichtlich idempotente Bereinigung oder Recovery.

Kommentare wiederholen keine Zuweisungen, Schleifen oder offensichtlichen
Bedingungen. Veraltete Erklärungen gelten als Defekt und müssen zusammen mit
dem Verhalten geändert werden.

## Funktionen und öffentliche Typen

Öffentliche Funktionen und nicht offensichtliche interne Zustandsmaschinen
erhalten Vertragskommentare. Je nach Bedarf beschreiben sie Vorbedingungen,
Parameter, Rückgabewerte, Seiteneffekte, Kontext (`IRQ`, Ring 0/3), Ownership
und Begrenzung. Triviale lokale Hilfsfunktionen benötigen keinen schematischen
Kommentar.

Öffentliche Strukturen dokumentieren Einheit und Bedeutung mehrdeutiger Felder,
insbesondere Versionen, Größen, Generationen, Zeitwerte, Bitmasken und
serialisierte Werte. Kommentare dürfen das Layout nicht verändern.

## Ableitung technischer Dokumentation

Die spätere Dokumentation darf Modulheader und API-Verträge extrahieren. Die
Architekturdokumente bleiben jedoch die normative Quelle für Systemgrenzen und
Assurance-Aussagen. Quellcodekommentare beschreiben den implementierten Stand;
Roadmap und Gap-Analyse beschreiben weiterhin offene Nachweise.

## Review-Regeln

Ein Dokumentationsänderung ist nur vollständig, wenn:

1. alle geänderten Aussagen mit der Implementierung übereinstimmen,
2. Kommentare keine ungebundene oder nicht vorhandene Recovery versprechen,
3. ABI und Binärlayout unverändert bleiben,
4. der normale Build und die relevanten Tests weiterhin bestehen und
5. neue produktive Quelldateien einen Modulheader besitzen.
