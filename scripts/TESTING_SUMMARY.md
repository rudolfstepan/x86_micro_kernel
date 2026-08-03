# Testabdeckung

Stand: 3. August 2026.

Die Suite kombiniert reine Python-Tests mit kleinen, gegen ausgewählte
Kernelquellen kompilierten Host-Harnesses. Dadurch lassen sich Datenstrukturen
und Fehlerpfade reproduzierbar prüfen, ohne für jeden Fall den Kernel zu
booten.

## Abgedeckte Invarianten

### Nativer Bootdatenträger

- gültige MBR-Signatur und zwei nicht überlappende Partitionseinträge
- additive Manifest-Prüfsumme
- Kernel-CRC32 im Manifest
- ELF32-Klasse, i386-Maschine, Entry und `PT_LOAD`-Grenzen
- FAT32-Bootsektor, FSInfo und identische FAT-Kopien
- korrekte Datei-Clusterketten einschließlich mehrerer Cluster
- wachsende Root-Verzeichniskette
- eindeutige und gültige ASCII-8.3-Namen
- konsistente VMDK-/VMX- und VMware-Paketdateien

### Dateisystem und VFS

- Mount-/Unmount-Lebenszyklus
- Auswahl des längsten Mountpfades
- Pfadgrenzen ohne falsche Präfixtreffer
- FAT12-Clusterketten und Handle-Lebensdauer
- FAT32-Schreiben, Truncate, Kettenfreigabe und Verzeichniserweiterung
- EXT2-Partitionen, Verzeichnisse und indirekte Blöcke
- FAT32-Integration: `readdir` und `open` sehen dieselbe `README.TXT`

### Shell

- DOS-Buchstaben und native Laufwerkspräfixe
- relative/absolute Pfade, `/`, `\`, `.`, `..`
- Begrenzung an der Laufwerkswurzel
- VFS-Mountjoin und DOS-Anzeige
- Quellschutz: Dateibefehle und Programmlader dürfen nicht wieder auf den
  früheren globalen FAT32-Direktpfad zurückfallen

### Externe Programme

- externe temporäre C- und `.S`-Quellen
- freestanding i386-Link
- keine offenen Relokationen
- korrekter 28-Byte-MYPR-Header
- Ladebasis, Einstieg, Segmentgrenzen und BSS-Materialisierung
- Kernelvalidator für beschädigte oder zu große Images

## Laufzeitsmoke

Zusätzlich zur Hostsuite wurde das erzeugte VMware-Paket headless gestartet.
Das serielle Protokoll bestätigte:

- nativen BIOS-/MBR-Boot
- Kernelinitialisierung ohne Crash
- Mount von `hdd0` als `/`
- E1000-Initialisierung
- DHCP-Lease im gebridgten LAN
- Erreichen des Prompts `C:\>`

Die Shellbefehle selbst werden in diesem headless Smoke nicht per VMware-
Tastaturautomation eingegeben. Ihre Pfad-/VFS-Kernlogik wird stattdessen durch
die Hosttests abgedeckt; ein manueller GUI-Test bleibt für Eingabegeräte und
Bildschirmdarstellung sinnvoll.

## Nicht abgedeckt

- formaler Beweis von Speicher- oder Prozessisolation
- SMP und Mehrkern-Rennen
- lange Hardware-Stressläufe
- vollständige USB-Gerätematrix
- alle realen BIOS-/ATA-/WLAN-Kombinationen
- TCP/DNS, da diese Protokolle noch nicht implementiert sind
- Fuzzing aller Parser und Dateisystem-Metadaten

## Referenzbefehle

```powershell
python -m unittest discover -s test -p "test_*.py" -v
.\scripts\build-windows.ps1 -Target vmware -RunTests
```

Die tatsächlich ausgegebene Testanzahl ist maßgeblich; sie kann mit neuen
Testmethoden wachsen und wird deshalb nicht als feste Zahl in der
Dokumentation eingefroren.
