# Nativer BIOS-Boot ohne GRUB

Stand: 30. August 2026.

REIST OS startet QEMU, VMware und reale Legacy-BIOS-PCs über dasselbe
512-MiB-Raw-Image. GRUB, ISO und QEMUs `-kernel`-Abkürzung gehören nicht zum
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
  relativ 0       signiertes Manifest A
  relativ 1..64   feste Stage-2-Reserve
  relativ 96      signiertes Manifest B
  relativ 97      Boot-Control-Record v1, Kopie 1
  relativ 98      Boot-Control-Record v1, Kopie 2
  relativ 128     ELF32-Kernel A
  relativ 3136    ELF32-Kernel B
LBA 8192          FAT32-LBA-Systempartition (Typ 0x0C)
                  Label "X86 SYSTEM", Programme und Daten
```

Stage 1 lädt ohne Manifestparser exakt 64 Sektoren Stage 2 ab dem festen
partitionrelativen LBA 1 und wählt zunächst Kandidat A. Das versionierte
Manifest v3 belegt
einen 512-Byte-Sektor. Sein 336-Byte-Header enthält unveränderte Stage-2- und
Kernel-LBA-Felder, Kernelgröße, diagnostische CRC32 und ab Offset 48 den
exakten 32-Byte-SHA-256-Digest des Kernelartefakts gemäß NIST FIPS 180-4.
Ab Offset 80 folgt die verpflichtende 256-Byte-RSA-PSS-Signatur.
Stage 2 lehnt andere Magic-, Versions- oder Headerwerte ab und erzwingt für A
und B die festen Manifest-/Kernel-Paare 0/128 beziehungsweise 96/3136. Bei
einem Fehler von A vor dem Kernel-Handoff wird B genau einmal von Grund auf
geprüft; danach wird geschlossen gestoppt. Stage 2 kopiert den Digest vor
Wiederverwendung des Manifestpuffers in festen
Speicher und verlangt einen gesetzten Wert.

Jeder Boot-Control-Record belegt genau 512 Byte. Der 64-Byte-Header enthält
`REISTBC1`, Version und Headergröße, eine monotone 64-Bit-Sequenz, bestätigten
Slot A oder B, optional `pending=B` ausgehend von A, verbleibende Versuche, das feste Limit zwei,
eine Erfolgsmaske und CRC32; alle reservierten Bytes müssen null sein. Stage 2
akzeptiert eine gültige Kopie, zwei identische Kopien gleicher Sequenz oder
zwei konsistente benachbarte Sequenzen. Es schreibt die ältere/ungültige Kopie
zuerst und verifiziert jeden BIOS-EDD-Schreibvorgang durch erneutes Lesen.

`scripts/update_native_boot_slot.py` erzeugt immer ein neues Output-Image,
verifiziert Kernel und RSA-PSS-Signatur vor der Kopie, schreibt ausschließlich
Kernel und Manifest des jeweils inaktiven Slots A oder B und veröffentlicht
erst danach den Pending-Record. Ein Pending-Start wird vor der Kernelprüfung
persistent dekrementiert. Nach
vollständiger Manifest-, Digest-, Signatur- und ELF-Prüfung schreibt Stage 2
einen CRC-geschützten 64-Byte-Handoff nach `0x4E00`. Der Kernel kopiert und
validiert ihn vor der Speicherverwaltung, ordnet die feste Geometrie genau
einer `0xDA`-Bootpartition zu und veröffentlicht sie erst nach `BOOT_OK` mit
Syscall 117 an die aktuelle Storage-Service-Generation. Der Ring-3-Dienst
liest Manifest und beide Control-Sektoren erneut, verlangt dieselbe Sequenz
und bestätigt B mit zwei Flush-/Read-back-verifizierten Writes. Bestätigtes B
startet direkt; ein B-Fehler schreibt bestätigt A vor dem A-Start.

Nach jeder Imageerzeugung prüft `scripts/validate_boot_manifest.py` unabhängig
vom Erzeuger das HDD- beziehungsweise Floppy-Layout, alle Extents, die
Manifest-Prüfsumme sowie SHA-256 und CRC32 über die tatsächlichen Kernelbytes.
Beim HDD-Image müssen beide Kandidaten vollständig gültig und
nicht überlappend sein; die Rescue-Diskette bleibt single-slot. Ein Fehler
bricht den Build geschlossen ab. Stage 2 verwendet BIOS EDD/INT
13h und berechnet SHA-256 sowie die diagnostische CRC32 im selben begrenzten
32-KiB-Lese-/Cache-Durchlauf über exakt `kernel_size` Bytes. Die SHA-256-
Kompression verwendet einen festen 64-Wort-Schedule und FIPS-180-4-Padding mit
64-Bit-Bitlänge. Nach erfolgreichem Digestvergleich prüft Stage 2 die Signatur
mit fester 2048-Bit-Arithmetik, Exponent 65537, MGF1-SHA-256 und exakt 32 Byte
Salt gegen den einkompilierten Research-Modulus. Erst danach validiert Stage 2
die ELF32-Segmente, richtet optional VBE ein und übergibt im Protected Mode an
den Kernel. Der Kernel erkennt anschließend die Blockgeräte und Partitionen
erneut über native Treiber.

Hostgate und Stage 2 authentifizieren das Kernelartefakt relativ zum in Stage 2
gebundenen Research-Schlüssel. Ein Angreifer kann Kernel und Manifest nicht
mehr gemeinsam austauschen, ohne an der Signaturprüfung zu scheitern. Stage 1
und Stage 2 selbst liegen jedoch weiterhin auf dem beschreibbaren Image und
sind nicht durch Firmware, TPM oder einen unveränderlichen ROM-Anker gebunden.
Ein Angreifer mit Schreibzugriff könnte daher den Loader samt Schlüsselanker
ersetzen. Secure Boot, Anti-Rollback und eine vollständige kryptografische
Firmware-Vertrauenskette bleiben ausdrücklich nicht implementiert.
`scripts/create_boot_update_bundle.py` erzeugt für die Offline-Verteilung ein
festes `.rup`-Bundle. Sein 512-Byte-v1-Header enthält exakte Größen,
Algorithmus-ID, Kernel-SHA-256, die feste 256-Byte-Signatur, den gepinnten
SPKI-SHA-256 und CRC32; der ELF-Payload ist auf die 3008 Sektoren eines Slots
begrenzt. `scripts/verify_boot_update_bundle.py` implementiert einen vom
Erzeuger getrennten Strukturparser und lehnt unbekannte Felder, Truncation,
Nachlaufdaten sowie Digest-, Policy- oder Signaturfehler ab. Erst danach darf
`update_native_boot_slot.py --bundle ...` den vorhandenen atomaren
Inactive-Slot-Pfad verwenden. Das Bundle enthält keine Slotwahl oder
Rollbackautorität.

Noch nicht implementiert sind Online-Updateverteilung, TUF-/Uptane-Rollen und
Expiry, unveränderliches Recovery-Image, Release-Key-Verwahrung und
Anti-Rollback.

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

Der Quellbuild liegt weiterhin als `reist-os.img` in seinem gewählten
Ausgabeordner. Zusätzlich wird ausschließlich für `real_hw` nach erneuter
Manifestprüfung atomar `build/reist-os-real-hw.img` veröffentlicht. Die
zielgebundenen Installer verwenden nur dieses Artefakt; dadurch kann ein
späterer QEMU- oder VMware-Build nicht versehentlich als Hardwareimage auf
eine physische Platte geschrieben werden.

Das Schreiben auf eine physische Platte ist destruktiv. Nur ein
zielgebundenes Installationsskript verwenden und vor Bestätigung
`PhysicalDrive`, Modell, Seriennummer und Größe prüfen. Das allgemeine Backend
ist `scripts/install-physical-disk.ps1`; vorhandene `.cmd`-Wrapper sind an die
konkret dokumentierte Platte gebunden. Nach erfolgreichem Schreiben muss die
Platte offline bleiben, bevor sie vom USB-Adapter getrennt wird.

Für die dokumentierte Samsung-SSD lautet der Aufruf aus dem Repository:

```powershell
.\scripts\install-reist-samsung-120gb.cmd
```

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
