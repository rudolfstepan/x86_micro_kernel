# Nativer BIOS-Boot ohne GRUB

Stand: 16. August 2026.

REIST OS startet QEMU, VMware und reale Legacy-BIOS-PCs über dasselbe
64-MiB-Raw-Image. GRUB, ISO und QEMUs `-kernel`-Abkürzung gehören nicht zum
Referenzpfad.

## Erzeugen

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
```

Der Build ist standardmäßig inkrementell. `-Clean` erzwingt einen vollständigen
Neuaufbau. Die wichtigsten Ergebnisse sind:

```text
build/reist-os.img
build/reist-os.vmdk
build/vmware/reist-os/reist-os.vmx
build/vmware/reist-os/START-VMWARE.cmd
build/kernel.bin.sig
```

Vor der Imageerzeugung signiert der Research-Build das exakte `kernel.bin`
nach RFC 8017 mit RSA-2048-PSS/SHA-256, MGF1-SHA-256 und 32 Byte Salt. Die
unabhängige Prüfung verwendet `safety/boot_trust_policy.json`, kontrolliert den
gepinnten SHA-256-Fingerprint des DER-SubjectPublicKeyInfo und akzeptiert nur
eine 256-Byte-Signatur. OpenSSL 3 wird dafür als Buildwerkzeug benötigt.

Der eingecheckte private Schlüssel unter `test/fixtures` ist absichtlich eine
öffentliche, kompromittierte Research-Testfixture für automatisierte Builds.
Der Signierer lehnt diese Policy im Release-Modus ab. Ein späteres Release
benötigt eine externe Policy und einen außerhalb des Repositories verwahrten
privaten Schlüssel.

## Datenträgerlayout

```text
LBA 0             MBR / Stage 1
LBA 2048          aktive RAW-Bootpartition (Typ 0xDA)
                  Manifest, Stage 2 und ELF32-Kernel
LBA 8192          FAT32-LBA-Systempartition (Typ 0x0C)
                  Label "X86 SYSTEM", Programme und Daten
```

Stage 1 lädt das Manifest und Stage 2. Das versionierte Manifest v2 belegt
einen 512-Byte-Sektor. Sein 80-Byte-Header enthält unveränderte Stage-2- und
Kernel-LBA-Felder, Kernelgröße, diagnostische CRC32 und ab Offset 48 den
exakten 32-Byte-SHA-256-Digest des Kernelartefakts gemäß NIST FIPS 180-4.
Stage 1 und Stage 2 lehnen andere Magic-, Versions- oder Headerwerte ab;
Stage 2 kopiert den Digest vor Wiederverwendung des Manifestpuffers in festen
Speicher und verlangt einen gesetzten Wert.

Nach jeder Imageerzeugung prüft `scripts/validate_boot_manifest.py` unabhängig
vom Erzeuger das HDD- beziehungsweise Floppy-Layout, alle Extents, die
Manifest-Prüfsumme sowie SHA-256 und CRC32 über die tatsächlichen Kernelbytes.
Ein Fehler bricht den Build geschlossen ab. Stage 2 verwendet BIOS EDD/INT
13h und berechnet SHA-256 sowie die diagnostische CRC32 im selben begrenzten
32-KiB-Lese-/Cache-Durchlauf über exakt `kernel_size` Bytes. Die SHA-256-
Kompression verwendet einen festen 64-Wort-Schedule und FIPS-180-4-Padding mit
64-Bit-Bitlänge. Erst nach erfolgreichem Digestvergleich validiert Stage 2 die
ELF32-Segmente, richtet optional VBE ein und übergibt im Protected Mode an den
Kernel. Der Kernel erkennt anschließend die Blockgeräte und Partitionen erneut
über native Treiber.

Hostgate und Bootloader liefern Integritäts- und Provenienzevidenz für das
erzeugte Image. Die hostseitige RSA-PSS-Signatur authentifiziert das
Kernelartefakt nur gegenüber dem Buildgate; sie ist noch nicht in Manifest und
Stage 2 gebunden. Der Loader besitzt daher weiterhin keinen Signatur-
Vertrauensanker. Ein Angreifer mit Schreibzugriff auf Image und Manifest könnte
beide gemeinsam ersetzen. Secure Boot beziehungsweise kryptografisch
authentifizierter Boot bleibt ausdrücklich nicht implementiert.

## Root-Volume

Beim Festplattenboot wird genau eine strukturell gültige FAT32-Partition mit
Label `X86 SYSTEM` als `/` und DOS-Laufwerk `C:` ausgewählt. Doppelte Labels,
ein nicht lesbares bevorzugtes Volume oder ein fehlgeschlagener Root-Mount
führen fail-closed zu keinem Ersatz-Root. Beim echten Boot von Diskette bleibt
die zugehörige BIOS-FDD-Ressource das bevorzugte Root-Volume.

Auf einem SATA-Gast lautet das Systemvolume typischerweise `hdd0p2`; die
konkrete Resource-ID ist nicht fest kodiert und wird mit `DRIVES` ermittelt.

## QEMU

Der normale QEMU-Start behält ATA/IDE als Regressionspfad:

```powershell
.\scripts\run-windows.ps1 -NoBuild
```

Der AHCI/SATA-Pfad wird explizit gestartet:

```powershell
.\scripts\run-windows.ps1 -NoBuild -Sata
python .\scripts\run_qemu_smoke.py --image build\reist-os.img --sata --expect-reist-probe
```

## VMware

Das erzeugte VMware-Paket verwendet Legacy BIOS und bindet die persistente
VMDK an `sata0:0` an. Die generierte VMX ist die Referenz; manuelle IDE-
Konfigurationen sind nicht mehr der aktuelle VMware-Weg.

```powershell
.\build\vmware\reist-os\START-VMWARE.cmd
```

Details stehen in [VMWARE.md](../hardware/VMWARE.md).

## Reale Hardware

```powershell
.\scripts\build-windows.ps1 -Target real_hw -Video vga
```

Das Schreiben auf eine physische Platte ist destruktiv. Nur ein
zielgebundenes Installationsskript verwenden und vor Bestätigung
`PhysicalDrive`, Modell, Seriennummer und Größe prüfen. Das allgemeine Backend
ist `scripts/install-physical-disk.ps1`; vorhandene `.cmd`-Wrapper sind an die
konkret dokumentierte Platte gebunden. Nach erfolgreichem Schreiben muss die
Platte offline bleiben, bevor sie vom USB-Adapter getrennt wird.

Erforderlich sind Legacy BIOS beziehungsweise CSM und deaktivierter Secure
Boot. UEFI-Boot, NVMe und eine allgemeine USB-Mass-Storage-Bootzusage sind
nicht implementiert. Native PCI-IDE- und AHCI/SATA-Pfade sind vorhanden, aber
nicht für jede Firmware-/Controllerkombination qualifiziert.

## Diagnose

- früher Bootfehler: Stage-1-/Stage-2-Ausgabe und COM1-Log prüfen
- kein Root: Storage-Probezähler, Partitionen und `X86 SYSTEM`-Label prüfen
- Panic bei Programmladen: Phase, Komponente, Operation, Subject und Result
  vom Panic-Screen übernehmen
- beschreibbares Systemvolume: `MKDIR`, `SAVE`, `COPY` oder `GTEST` verwenden
- Resource- und Transportzuordnung: `DRIVES`
