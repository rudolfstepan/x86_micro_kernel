# Boot Sector Debugging Guide

## Problem
Drives (HDD/FDD) werden erkannt, können aber nicht gemountet werden.

## Diagnose-Schritte

### 1. Prüfe Disk Images auf dem Host
```bash
# Zeige alle Boot-Sectoren
bash scripts/check_boot_sectors.sh

# Detaillierte Analyse
python3 scripts/dump_boot_sector.py disk.img
python3 scripts/dump_boot_sector.py disk1.img
python3 scripts/dump_boot_sector.py floppy.img
```

**Erwartete Werte (FAT32):**
- Bytes per sector: 512
- Sectors per cluster: 1
- Root cluster: 2  
- Boot signature: 0xAA55

### 2. Starte QEMU und teste
```bash
make run
```

### 3. Im Kernel-Terminal testen

#### Schritt A: Zeige erkannte Laufwerke
```
DRIVES
```

Erwartete Ausgabe:
```
Detected drives:
  hdd0 (0x012345678): ATA drive, Base: 0x1F0, Master
  hdd1 (0x0123456789): ATA drive, Base: 0x1F0, Slave
  fdd0: Floppy drive
```

#### Schritt B: Versuche zu mounten
```
MOUNT HDD0
```

**Was passiert:**
1. `init_fs()` liest Boot-Sector in lokalen Buffer
2. Zeigt erste 32 Bytes hex
3. Parse Filesystem-Typ aus Offset 82-89
4. Ruft `fat32_init_fs()` auf
5. `fat32_init_fs()` liest Boot-Sector NOCHMAL
6. Zeigt geparste Werte

**Debug-Output zu erwarten:**
```
Try to Init fs on ATA drive hdd0: MODEL with XXXXX sectors
  ATA base: 0x1F0, is_master: 1
Reading boot sector from LBA 0...
Boot sector read successful.
First 32 bytes of boot sector:
EB 58 90 6D 6B 66 73 2E 66 61 74 00 02 01 20 00
02 00 00 00 00 F8 00 00 20 00 08 00 00 00 00 00
Filesystem type: 'FAT32   '
Detected FAT32 filesystem on drive hdd0.

Boot sector first 32 bytes:
EB 58 90 6D 6B 66 73 2E 66 61 74 00 02 01 20 00
02 00 00 00 00 F8 00 00 20 00 08 00 00 00 00 00

Parsed boot sector:
  bytesPerSector: 512 (offset 11-12)
  sectorsPerCluster: 1 (offset 13)
  reservedSectorCount: 32 (offset 14-15)
  numberOfFATs: 2 (offset 16)
  rootEntryCount: 0 (offset 17-18)
  FATSize32: 788 (offset 36-39)
  rootCluster: 2 (offset 44-47)
  bootSignature: 0x29 (offset 66)
  Sector signature at 510-511: 0x55AA

FAT32 initialized: root cluster=2, bytes/sector=512, sectors/cluster=1
```

## Bekannte Probleme und Fixes

### Problem 1: Buffer wird falsch allokiert
```c
// FALSCH:
void* buffer[512];  // Array von 512 Pointern!

// RICHTIG:
uint8_t buffer[512];  // Array von 512 Bytes
```

### Problem 2: Doppelte Referenz beim ATA-Read
```c
// FALSCH:
ata_read_sector(base, 0, &buffer, is_master);

// RICHTIG:
ata_read_sector(base, 0, buffer, is_master);
```

### Problem 3: Boot-Sector wird zweimal gelesen
- Einmal in `init_fs()` (filesystem.c)
- Nochmal in `fat32_init_fs()` (fat32.c)

**Lösung:** Beide Lesevorgänge sollten identische Daten zeigen!

### Problem 4: Falsche Struktur-Offsets
Die `Fat32BootSector` Struktur muss `#pragma pack(push, 1)` verwenden!

```c
#pragma pack(push, 1)
struct Fat32BootSector {
    uint8_t jumpBoot[3];       // Offset 0-2
    uint8_t OEMName[8];        // Offset 3-10
    uint16_t bytesPerSector;   // Offset 11-12 ← Muss 512 sein!
    uint8_t sectorsPerCluster; // Offset 13   ← Muss 1 sein!
    // ...
};
#pragma pack(pop)
```

## Vergleich Host vs Kernel

### Host (Python Script):
```bash
python3 scripts/dump_boot_sector.py disk.img
```
Zeigt: Bytes per sector = 512, Root cluster = 2

### Kernel (QEMU):
```
MOUNT HDD0
```
Zeigt: bytesPerSector = ???, rootCluster = ???

**Wenn die Werte unterschiedlich sind:**
- ATA-Read funktioniert nicht korrekt
- Falscher Sektor wird gelesen
- Buffer-Corruption

## Debugging-Kommandos

### Zeige Rohbytes eines Sektors
```
DUMP 0 512
```
Sollte identisch mit `xxd -l 512 disk.img` sein

### Teste ATA-Read direkt
Im Kernel könnte man hinzufügen:
```c
void cmd_test_ata_read(int arg_count, const char** arguments) {
    uint8_t buffer[512];
    
    // Read LBA 0 from primary master
    if (ata_read_sector(0x1F0, 0, buffer, true)) {
        printf("ATA read successful:\n");
        for (int i = 0; i < 32; i++) {
            printf("%02X ", buffer[i]);
            if ((i+1) % 16 == 0) printf("\n");
        }
    } else {
        printf("ATA read FAILED\n");
    }
}
```

## Checkliste

- [ ] Host: `bash scripts/check_boot_sectors.sh` zeigt gültige Werte
- [ ] Kernel: `DRIVES` zeigt hdd0, hdd1, fdd0
- [ ] Kernel: `MOUNT HDD0` zeigt Debug-Output
- [ ] Kernel: Erste 32 Bytes identisch mit Host
- [ ] Kernel: bytesPerSector = 512
- [ ] Kernel: rootCluster = 2
- [ ] Kernel: Boot signature = 0xAA55

## Erwartetes Ergebnis

Nach erfolgreichem Mount:
```
MOUNT HDD0
FAT32 initialized: root cluster=2, bytes/sector=512, sectors/cluster=1

LS
README.TXT
TEST/
DOCS/
BIN/
sys/
```

## Wenn Mount fehlschlägt

1. Vergleiche Hex-Dump (Host vs Kernel)
2. Prüfe `ata_read_sector()` Return-Wert
3. Prüfe ATA Status-Register
4. Prüfe Timeout-Werte (QEMU vs Real HW)
5. Prüfe ob richtiger Drive ausgewählt (Master/Slave)
6. Prüfe ob richtiger Bus (Primary/Secondary)
