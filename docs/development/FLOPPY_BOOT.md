# Bootfähige 1,44-MB-Diskette

Das Projekt erzeugt zusätzlich zum normalen Festplattenimage ein echtes
1,44-MB-BIOS-Diskettenimage:

```text
build/x86-microkernel-floppy.img
```

Es verwendet klassische BIOS-CHS-Zugriffe mit 80 Zylindern, zwei Köpfen und
18 Sektoren pro Spur. INT-13h-Erweiterungen, eine MBR-Partitionstabelle und
eine Festplatte werden zum Start nicht benötigt.

## Erzeugen

```powershell
.\scripts\build-windows.ps1 -Target real_hw -RunTests
```

Alternativ mit einer vollständig eingerichteten Make-Toolchain:

```bash
make floppy-image TARGET=real_hw VIDEO=vga
```

Der Generator bricht ab, falls Stage 2 oder der Kernel nicht mehr auf die
Diskette passen. Das Ergebnis ist immer exakt 1.474.560 Byte groß.

Kernel und Userspace-Programme werden standardmäßig als Release mit `-O2` und
`NDEBUG` gebaut. `build/kernel.bin` wird bereits beim Linken vollständig
gestrippt. Dadurch muss das langsame Diskettenlaufwerk keine reinen
Host-Debuginformationen lesen oder per CRC prüfen. Die `.PRG`-Programme werden
mit denselben Release-Einstellungen und Linker-Garbage-Collection erzeugt.

Beim Diskettenstart liest Stage 2 das Boot-ELF während der CRC32-Prüfung genau
einmal und hält es vorübergehend im RAM. ELF-Header und `PT_LOAD`-Segmente
werden anschließend aus diesem Cache übernommen. Der Festplattenpfad liest
weiterhin direkt vom BIOS-Datenträger und reserviert keinen Cachebereich.

Stage 1 und Stage 2 fassen aufeinanderfolgende Sektoren bis zum jeweiligen
Spurende in einem BIOS-Aufruf zusammen. Nach dem Kernelstart verwendet auch
der FDC-Treiber Mehrsektor-DMA: FAT, Verzeichnisse und zusammenhängende
Dateicluster werden spurweise gelesen. Dadurch entfallen die meisten
Controller-Kommandos und Interrupt-Wartezyklen. Falls ein älterer Controller
einen Mehrsektorzugriff ablehnt, fällt FAT12 automatisch auf Einzelsektoren
zurück.

## Test in QEMU

```bash
make run-floppy TARGET=real_hw VIDEO=vga
```

Oder unter Windows direkt:

```powershell
& 'C:\tmp\qemu-portable\qemu-system-i386.exe' -m 64M -boot a `
  -drive file=build/x86-microkernel-floppy.img,format=raw,if=floppy
```

## Auf eine echte Diskette schreiben

Unter Linux muss das Gerät vorher eindeutig bestimmt und ausgehängt werden.
Für ein klassisches erstes Diskettenlaufwerk ist es häufig `/dev/fd0`:

```bash
sudo dd if=build/x86-microkernel-floppy.img of=/dev/fd0 bs=512 conv=fsync
```

Bei einem USB-Diskettenlaufwerk ist es dagegen typischerweise ein `/dev/sdX`-
Gerät. Der Gerätename muss vor `dd` sorgfältig geprüft werden, da ein falsches
Ziel einen anderen Datenträger überschreibt.

Unter Windows schreibt das mitgelieferte Batch das Image auf Laufwerk `A:`:

```powershell
.\scripts\write-floppy.cmd
```

Es fordert Administratorrechte an, sperrt und dismountet das Volume und
schreibt mit Write-through direkt auf das Laufwerk. Danach wird die komplette
Diskette zurückgelesen und bytegenau geprüft. Normales Kopieren der
`.img`-Datei nach `A:` reicht nicht aus. Während des Vorgangs darf die
Diskette nicht entfernt werden; Programme und Explorer-Fenster, die `A:`
verwenden, müssen vorher geschlossen sein.

## Aufbau

- Sektor 0: FAT12-BPB und eigener Stage-1-Bootloader
- Sektor 1: geprüftes Bootmanifest
- folgende reservierte Sektoren: Stage 2 und ELF32-Kernel
- Rest: gültiges, beschreibbares FAT12-Dateisystem

Bootloader und Kernel liegen innerhalb des FAT12-reservierten Bereichs und
werden deshalb durch normale Dateioperationen nicht überschrieben. Nach dem
Start mountet der Kernel dieselbe Diskette als aktives Laufwerk.

Der FAT12-VFS ist beschreibbar. `SAVE`, `COPY`, `DEL`, `MKDIR` und `RMDIR`
funktionieren deshalb auch direkt auf `A:`. Dateien dürfen fragmentierte
Clusterketten verwenden, Unterverzeichnisse werden bei Bedarf erweitert und
beide FAT-Kopien werden synchron aktualisiert. Schreibzugriffe werden vom
FDC-Treiber wie Lesezugriffe bis zum Spurende gebündelt; ein vom Controller
gemeldeter Schreibschutz oder E/A-Fehler wird an den aufrufenden Prozess
zurückgegeben.

Vorausgesetzt werden ein BIOS mit Floppy-Bootunterstützung, ein
386-kompatibler 32-Bit-Prozessor und ausreichend RAM für den Kernel.
