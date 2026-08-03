# FAT12-Status

FAT12 dient primär Diskettenabbildern und ist über VFS registriert. Der
aktuelle Shellpfad greift nicht direkt auf eine globale FAT12-Instanz zu.

## Implementiert und getestet

- Validierung des FAT12-Bootsektors und der 1,44-MiB-Geometrie
- Lesen von FAT12-Clusterketten
- Dateihandles mit stabil kopiertem Open-Modus
- Seek-/Positionsverhalten über Clustergrenzen
- Unterverzeichnis-Lebensdauer ohne ungültige temporäre Zeiger
- VFS-Adapter für die von der Shell verwendeten Operationen
- Hostregression in `test/test_fat12_host.c`

Disketten werden als `fdd0`, `fdd1` erkannt und DOS-artig `A:`, `B:`
zugeordnet. Ist eine Festplatte vorhanden, liegen Disketten typischerweise
unter `/mnt/fdd0` beziehungsweise `/mnt/fdd1`.

```text
C:\> DRIVES
C:\> DIR A:\
C:\> TYPE A:\README.TXT
```

## Grenzen

- klassische FAT12-Kapazitäts- und Geometriegrenzen
- im Kern 8.3-Kurznamen; keine vollständige LFN-Unterstützung
- keine Journaling- oder Crash-Recovery-Funktionen
- Verhalten realer Diskettencontroller ist weniger umfassend getestet als
  die Hostabbilder

`FAT12_ANALYSIS.md` bleibt als historischer Detailbericht erhalten und kann
bereits behobene Probleme beschreiben.
