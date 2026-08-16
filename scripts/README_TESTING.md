# Tests ausführen

Stand: 16. August 2026.

REIST trennt hostseitige Quell-/Harness-Tests, den sauberen Paketbuild und
echte Gast-Laufzeitgates. Keine einzelne Ebene ersetzt die anderen.

## Schnelle Hostsuite

```powershell
python -m unittest discover -s test -p "test_*.py" -v
```

Sie benötigt Python, einen Host-C-Compiler für Harnesses und Zig/LLD für die
MYPR-Toolchain. Einzelne optionale Werkzeugtests können bei fehlender
Abhängigkeit übersprungen werden; der Referenzbuild soll ohne relevante Skips
laufen.

## Windows-Referenzbuild

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
```

Der Build ist inkrementell. Nur bei einem bewusst vollständig frischen Lauf
wird `-Clean` ergänzt.

## REIST-Paketgate

```powershell
.\scripts\test-reist-package.ps1 -Target qemu -Video vga
```

Dieses Gate erzeugt den sauberen Referenzstand und führt die vollständige
Hostsuite aus. In automatisierten Paketen bestimmt `automation/reist-s03b.toml`
die eingefrorenen Gates; sie werden nicht ad hoc ersetzt.

## Laufzeitgates

```powershell
.\scripts\test-reist-runtime.ps1 -Mode normal
.\scripts\test-reist-runtime.ps1 -Mode storage-recovery
.\scripts\test-reist-runtime.ps1 -Mode storage-io-failure
.\scripts\test-reist-runtime.ps1 -Mode fdd-hotplug
```

Weitere Modi prüfen PIT, Watchdog, Memory, ARP, ICMP, UDP, DHCP,
Netzwerkparser und Handover. Der explizite SATA-Smoke lautet:

```powershell
python .\scripts\run_qemu_smoke.py --image build\reist-os.img --sata --expect-reist-probe
```

PS/2 wird separat über echte QEMU-`sendkey`-Ereignisse geprüft:

```powershell
python .\scripts\run_qemu_ps2_smoke.py --image build\reist-os.img
```

## Klassische Make-Ziele

```bash
make test-unit
make test-images
make test-all
```

`test-images` bezieht optionale Legacy-Fixtures ein. Es ist nicht mit den
selbst erzeugenden nativen Image- und Gasttests gleichzusetzen.

## Interpretation

Ein grüner Quelltest belegt nur die geprüfte Struktur, ein Host-Harness nur
sein Modell und ein QEMU-/VMware-Smoke nur das konkrete virtuelle Profil.
Reale Hardware, Stromunterbrechung, Langzeitlast und unabhängige
Supervisor-/Fence-Hardware benötigen eigene Evidenz.
