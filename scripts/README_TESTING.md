# Tests ausführen

Stand: 20. August 2026.

REIST trennt hostseitige Quell-/Harness-Tests, den sauberen Paketbuild und
echte Gast-Laufzeitgates. Keine einzelne Ebene ersetzt die anderen.

## Vollständige Hostsuite

```powershell
python -m unittest discover -s test -p "test_*.py" -v
```

Sie umfasst derzeit mehr als hundert Testmodule einschließlich Toolchain- und
Orchestrierungs-Selbsttests. Sie benötigt Python, einen Host-C-Compiler für
Harnesses und Zig/LLD für die MYPR-Toolchain und ist ein bewusstes CI- bzw.
Milestone-Gate, kein Schritt nach jeder grafischen Änderung.

## Windows-Referenzbuild

```powershell
.\scripts\build-windows.ps1 -Target vmware -RunTests
```

Der Build ist inkrementell. Ohne `-RunTests` entsteht schnell ein direkt
testbares Image; paketbezogene Quelltests werden separat aus der eingefrorenen
Paketliste ausgeführt. Nur bei einem bewusst vollständig frischen Lauf wird
`-Clean` ergänzt. Ein Zielwechsel invalidiert Kernelobjekte, erhält aber das
zielunabhängige Userspace-SDK und unveränderte PRGs.

Für die aktuelle Desktop-/Menü-Entwicklung steht der begrenzte innere
Testzyklus bereit:

```powershell
make test-desktop-host
```

## REIST-Paketgate

```powershell
.\scripts\test-reist-package.ps1 -Target qemu -Video vga
```

Dieses Gate erzeugt den konfigurierten Referenzstand und prüft dessen feste
Artefakte. In automatisierten Paketen bestimmt `automation/reist-s03b.toml`
die einmal auszuführenden gezielten Tests; sie werden nicht in jeder QEMU-,
Framebuffer- und VMware-Variante wiederholt. Nur der ausdrücklich angeforderte
Standalone-Lauf ergänzt die Vollsuite:

```powershell
.\scripts\test-reist-package.ps1 -Target qemu -Video vga -RunHostTests
```

## Laufzeitgates

```powershell
.\scripts\test-reist-runtime.ps1 -Mode normal
.\scripts\test-reist-runtime.ps1 -Mode storage-recovery
.\scripts\test-reist-runtime.ps1 -Mode storage-io-failure
.\scripts\test-reist-runtime.ps1 -Mode fdd-hotplug
```

Weitere Modi prüfen PIT, Watchdog, Memory, ARP, ICMP, UDP, DHCP,
Netzwerkparser, Handover, PCI-Audio sowie Runtime-Grafik und Surface-Clients.
Die aktuellen Desktopgates lauten:

```powershell
.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop
.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-metrics
.\scripts\test-reist-runtime.ps1 -Mode runtime-desktop-surface
```

Die öffentlichen Desktop-, Surface- und Notepad-Aufnahmen werden über
`.\scripts\capture-documentation.ps1` aus denselben Runtime-Probes erzeugt.
Ablage, Bildaussage und Aktualisierungsregeln stehen ausschließlich im
[Screenshot-Vertrag](../docs/assets/screenshots/README.md).

Der explizite SATA-Smoke lautet:

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
