# Tests ausführen

Die Tests laufen hostseitig und benötigen für den Kernumfang keinen
Emulatorstart. Der bevorzugte Windows-Komplettbefehl baut zusätzlich Kernel,
Bootloader, Programm und VMware-Paket:

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
```

Nur die Python-Test-Suite:

```powershell
python -m unittest discover -s test -p "test_*.py" -v
```

Mit Make:

```bash
make test-unit
```

## Abhängigkeiten

- Python 3
- ein Host-GCC für die C-Harnesses
- Zig für den End-to-End-Test der externen Programmtoolchain
- keine Rootrechte und kein gemountetes Image für `test-unit`

Fehlt ein optionales Werkzeug, markiert `unittest` die betroffenen Fälle als
übersprungen. Der vollständige Windows-Referenzbuild stellt Zig bereit und
soll ohne Überspringen der Toolchainprüfung laufen.

## Zusätzliche klassische Imageprüfungen

```bash
make test-images
make test-all
make test-verbose
```

Diese Ziele prüfen die separaten Legacy-Fixtures `disk.img`, `disk1.img` und
`floppy.img`, soweit sie vorhanden sind. Sie sind von den selbst erzeugenden
Tests des nativen 64-MiB-Bootimages zu unterscheiden.

## Testdateien

| Datei | Schwerpunkt |
|---|---|
| `test_native_boot_image.py` | MBR, Manifest, ELF, FAT32, VMDK und VMX |
| `test_disk_image_validator.py` | Fehlerfälle klassischer Images |
| `test_fs_host.py` | kompiliert und startet C-Harnesses |
| `test_vfs_host.c` | Mountlebenszyklus und Präfixgrenzen |
| `test_fat12_host.c` | Ketten, Seek und Verzeichnisse |
| `test_fat32_host.c` | Schreiben, Truncate und Verzeichniserweiterung |
| `test_ext2_host.c` | Partition, Verzeichnis und indirekte Blöcke |
| `test_shell_path_host.c` | DOS-/VFS-Pfadnormalisierung |
| `test_shell_source.py` | Schutz gegen regressierende Direkt-FAT-Aufrufe |
| `test_program_image_host.c` | MYPR-Header- und Größenprüfung |
| `test_user_program_toolchain.py` | externe C- und `.S`-Quellen bis PRG |

## Interpretation

Ein grüner Hosttest bestätigt die konkret kodierte Invariante. Er beweist
nicht automatisch Hardwarekompatibilität, Ring-3-Isolation oder fehlerfreien
Langzeitbetrieb. Deshalb wird zusätzlich ein VMware-Bootsmoke bis zum Prompt,
Mount und DHCP verwendet.
