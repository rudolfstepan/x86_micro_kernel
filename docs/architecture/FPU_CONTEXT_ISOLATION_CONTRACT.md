# i386 FPU-Kontextisolation (R1.3)

Stand: 7. September 2026. Freigegebenes Voraussetzungspaket, noch nicht abgenommen.

## Fehler und Grenze

Der installierte i386-Compiler erzeugt fuer `double` x87-Instruktionen.
Der bisherige `swtch` sichert nur GPR/Stack, keine FP-Register oder Rundungs-
und Fehlerkontrollen. JavaScript darf auf dieser Grundlage nicht aktiviert
werden. Der Benutzer gibt die separate Kernelkorrektur ausdruecklich frei.
Nur der minimale CPU-Kontextmechanismus gehoert in Ring 0; Engine, Math/libc,
DOM und Webpolicy bleiben eigene Ring-3-Pakete.

Referenz: Intel SDM, Volume 1 Kapitel 10, Volume 2 `FXSAVE`/`FXRSTOR`, Volume 3
CPU-Kontrollregister und FP-Kontextverwaltung; [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
[INTEL-SA-00145](https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa-00145.html)
empfiehlt eager statt lazy Restore. Keine behauptete Zertifizierung,
vollstaendige Seitenkanalisolation oder generische Hardwareabnahme.

## Besitz und Lebenszyklus

Jeder Task und jeder CPU-lokale Idle-Kontext enthaelt ein kernelprivates,
16-Byte-ausgerichtetes 512-Byte-FXSAVE-Abbild. Die bisherigen GPR-Offsets
bleiben; der interne Kontext wird auf 544 Bytes erweitert. Keine neue
Userspace-ABI, kein Userpointer zum Restore, keine dynamische Allocation.
Task-/CPU-Kapazitaet, Quoten und Schedulerfenster bleiben unveraendert.

Vor READY einer neuen Taskgeneration: gesamter Kontext nullen, x87-Control
`0x037f`, MXCSR `0x1f80`, leere Tags und genullte Registerpayloads setzen.
Vor jedem tatsaechlichen Wechsel: ausgehenden Zustand sichern, eingehenden
Zustand wiederherstellen, erst danach Stackwechsel und Handoff-Freigabe.
Beim Exit darf nur das Speichern entfallen (`old == NULL`), niemals Restore.
Kein Lazy-FPU-Owner und kein #NM-Wiederanlauf. Reap nullt den ganzen Task;
neue Generationen erben weder Daten noch Ausnahme-/Rundungseinstellungen.

## CPU-Profil und Fehler

BSP und jeder AP pruefen CPUID FPU/FXSR/SSE vor FP-Instruktionen. Nur das
einheitliche Legacy-FXSAVE-Profil wird zugelassen: CR0.MP/NE an, EM/TS aus,
CR4.OSFXSR/OSXMMEXCPT an, OSXSAVE aus. Bereits aktiviertes OSXSAVE wird vor
Aenderungen abgewiesen, nicht mit unbekanntem erweiterten Zustand abgeschaltet.
Kontrollregister werden rueckgelesen; MXCSR-Masken muessen CPU-uebergreifend
uebereinstimmen (Intel-Fallback `0xffbf`, wenn die Hardware null meldet).
Die bekannten Bits umfassen auch AMDs Legacy-SSE Misaligned Exception Mask
(Bit17, [AMD APM](https://docs.amd.com/api/khub/documents/68GKiN0gMEd6bMddsmhPwg/content)).
Das Bit gehoert bereits zum FXSAVE-MXCSR, nicht zu XSAVE, und bleibt im
frischen Zustand null; unbekannte Bits bleiben verboten. Ein echter Ryzen-
Workstation-Boot stoppte mit der zu engen 16-Bit-Pruefung in Stufe5.
Die O0/O2-Regression prueft Standard-/AMD-Masken, ungueltige Bits, fehlende
Defaultkontrollen und gemischte CPUs ohne stillen Maskenschnitt.
Kein AVX-/XSAVE- oder SIMD-Compilerprofil fuer Anwendungen in diesem Paket.
Nicht unterstuetzte BSP-Hardware stoppt vor Taskausfuehrung diagnostiziert;
ein unpassender AP erreicht nicht ONLINE. Kein stiller unsicherer Fallback.

Kernel-C wird ohne FP/MMX/SSE erzeugt; ausschliesslich der kleine explizite
Architekturmechanismus und seine begrenzten AP-Probes verwenden FP-Assembler.
Unmaskierte Ring-3-FP-Ausnahmen und ungueltiges MXCSR durchlaufen vorhandene
#MF/#XM/#GP-Prozessbeendigung. Ungueltige kernelprivate Kontexte sind
Kernkorruption und bleiben fatal; keine Reparatur im laufenden Kernel.

## Eingefrorener Nachweis

Die Queue friert Dateiscope und Kommandos vor Implementierung ein. Host:
echtes i386-`switch.asm` vor/nach der Reparatur, O0/O2, Defaults, alle x87/XMM-
Register, Controls, wiederholte Wechsel und Null-old; echte Bootpolicy mit
Hardware-Testadapter fuer fehlende Features, Readback und CPU-Masken.
Gast: APIC, PIT, vier CPUs, jeweils zwei vollstaendige Ring-3-Laeufe mit
Preemption/Yield/Sleep, Eltern-/Kindzustand, Exit/Kill/Reuse, #MF/#XM/#GP und
anschliessendem Shellkommando. AP-Probes pruefen Initialzustand und Erhalt
ueber ihre bestehenden blockierenden Pfade. Unsupported-CPU-Gast stoppt vor
Ready. Zusaetzlich beide Referenzbuilds, Normal- und Browser-Input-Gastgate.
Keine Laufzeitbehauptung allein aus Quellmustern/Hosttests; kein Nachweis
tatsaechlicher Ring-3-Migration zwischen CPUs oder von Hardware-WCET.

### Offener Abnahmebefund vom 7. September

62 Hosttests und beide Referenzbuilds bestehen. Der erste APIC-Gastlauf
erreicht Registererhalt und echte #MF-Prozessbeendigung, aber nicht #XM:
unmaskiertes SSE-0/0 kehrt im installierten reinen QEMU-TCG zurueck
(GTEST-Status94 statt147). Das Gate bleibt fehlgeschlagen; die weiteren
Gastgates und der Workstation-Leistungsvergleich sind noch offen.

Ein [offizieller QEMU-Commit](https://github.com/qemu/qemu/commit/418b0f93d12a1589d5031405de857844f32e9ccc)
unterscheidet ausdruecklich SSE-Statusflags von nicht implementierten Traps.
Auch die am 7. September gelesenen offiziellen Quellen
[`ops_sse.h`](https://raw.githubusercontent.com/qemu/qemu/master/target/i386/ops_sse.h)
und [`fpu_helper.c`](https://raw.githubusercontent.com/qemu/qemu/master/target/i386/tcg/fpu_helper.c)
fuehren SSE-Divisionen ueber Softfloat-Status und dessen MXCSR-Abbildung aus.
Dies stuetzt die Emulatorlimitierung als Ursache; der genaue installierte
Development-Commit war nicht abrufbar. Keine pauschale Behauptung ueber alle
QEMU-Versionen oder Hardware-Acceleratoren.

Keine Kernelumgehung, Trap-Simulation, Erfolg bei fehlender Ausnahme oder
stille Abschwaechung der Abnahme. Eine verpflichtende echte Workstation-
Fehlerabnahme als Ersatz fuer den nicht erbrachten TCG-Nachweis ist erst
nach ausdruecklicher Aenderung der eingefrorenen Plattformzuordnung erlaubt.
Bis zur vollstaendigen Abnahme bleibt der Kandidat uncommittet und JavaScript
nicht freigegeben.

### Freigegebene Plattformzuordnung nach dem Befund

Der Benutzer genehmigt den Wechsel des verpflichtenden #XM-Nachweises zu
echter VMware Workstation und die Erweiterung von `run_vmware_mouse.ps1`.
QEMU APIC/PIT/SMP behalten alle anderen Pruefungen einschliesslich #MF und
ungueltigem MXCSR (#GP). Nur der nicht gelieferte #XM wird im expliziten
`fpu-tcg`-Profil ausgelassen; eigene Teilprofilmarker verhindern Verwechslung
mit dem vollstaendigen `fpu`-Nachweis. Keine automatische Emulatorerkennung
oder Erfolgsmeldung beim Ausbleiben einer erwarteten Ausnahme.

Das verpflichtende Workstation-Gate startet eine frische private Paketkopie
mit vier CPUs/1024 MiB versteckt, injiziert zweimal das volle `fpu`-Programm
und prueft echte #MF/#XM/#GP-Vektoren samt Status144/147/141, alle AP-Probes,
Kill/Reuse, neue Shellantworten und zehn Sekunden Stabilitaet. Gastdeadline
180 Sekunden; keine fremden VMs oder parallelen Compiler. Der Benchmarkpfad
und seine Grenzen bleiben unveraendert. Betroffene FPU-Host-/Referenzgates
werden erneut geprueft; vorhandene Fehler und unbeeinflusste PASS-Belege
bleiben bestehen. Die Queue friert diese Aenderung vor Umsetzung ein.

Aktueller Nachweisstand nach Umsetzung: Workstation besteht beide vollen
Fehler-/Reuse-Laeufe, alle APs, Shell und Stabilitaet (29 Sekunden).
Der QEMU-Teilprofiltest scheitert jetzt beim invalid-MXCSR-#GP: Status94 statt
141, waehrend Workstation denselben Code korrekt abfaengt. In den gelesenen
offiziellen Quellen ruft
[`gen_LDMXCSR`](https://raw.githubusercontent.com/qemu/qemu/master/target/i386/tcg/emit.c.inc)
den Helper auf; [`cpu_set_mxcsr`](https://raw.githubusercontent.com/qemu/qemu/master/target/i386/cpu.h)
uebernimmt die Bits ohne entsprechende Validierung. Keine behauptete
Gleichheit zum nicht abrufbaren installierten Development-Commit.
Die genehmigte Zuordnung umfasst bisher nur #XM; diese zweite Grenze bleibt
offen und die Gesamt-Abnahme stoppt. Keine stille Gateaenderung oder
Implementierungsfreigabe. Vollstaendige Befunde in CURRENT_WORK.md.

### Zusaetzlich freigegebene invalid-MXCSR-Zuordnung

Der Benutzer genehmigt anschliessend auch den verpflichtenden Nachweis des
invalid-MXCSR-#GP auf Workstation. Dort bleiben alle drei urspruenglichen
Fehlerausloeser unveraendert. QEMU behaelt einen echten #GP13 mit Status141,
ausgeloest durch ein fehl-ausgerichtetes FXRSTOR-Abbild, weiterhin gefolgt von
Pruefung des Elternzustands und einer frischen Kindgeneration. TCG-Marker
benennen ausdruecklich `gp_align=141` und `sse=workstation-required`.
Keine Ausnahmesimulation, keine automatische Erfolgs-/Fallback-Erkennung
und kein Kernelworkaround. Alle anderen Pruefungen bleiben verbindlich.
Betroffene Host-/Referenz-/Workstation-Gates werden mit den endgueltigen
Artefakten erneut ausgefuehrt; alle vorhandenen Fehl- und PASS-Belege bleiben.

## Zusaetzlicher VMware-Leistungsschutz (Benutzerauftrag waehrend Umsetzung)

Den beobachteten schnellen Stand vor einem neuen Build separat sichern.
Eingefrorener Zusatz: drei frische, abwechselnde Vorher/Nachher-Workstation-
Paare mit unveraendertem `run_vmware_mouse.ps1 -Benchmark`, vier CPUs,
1024 MiB, versteckten eigenen VM-Kopien und ohne parallele Compiler/VMs.
CPU-Single-/Multi-Median jeweils mindestens 95 Prozent des Vorhermedians;
alle Rohwerte, Image-/Harness-Digests und alte Fehlbelege bleiben erhalten.
Vorher-Nachher ist ein Regressionsindikator, kein statistischer WCET-Nachweis.
RAM/HDD-Zeilen ebenfalls festhalten; wegen kurzer, millisekundenquantisierter
Laufzeiten keine 5-Prozent-Praezision behaupten. Benchmark und seine bisherigen
Mindestwerte unveraendert lassen. Das Screenshot-Ergebnis (Single 4194.30,
Multi 4051.85 MOp/s; RAM je 16000 MiB/s; HDD 12800/42666.66 KiB/s) ist eine
Benutzerbeobachtung, noch keine eigene Messung oder gesicherte 10x-Ursache.

Der verpflichtende Vergleich besteht inzwischen: Single-Median
3947.58/4098.25 MOp/s (vorher/nachher), Multi-Median 4013.98/4074.92 MOp/s,
jeweils ueber 95 Prozent. Sechs frische Workstation-Laeufe, keine parallelen
VMs/Compiler; Ausgangsdisks und VMX rueckgeprueft unveraendert. Artefakt
`build/codex-agent/r13-fpu/vmware-paired.json` enthaelt alle Rohwerte und
Digests. Die einzige Benchmark-Harness-Korrektur fordert bei ueberlagertem
Bootprompt nach den anderen Bereitschaftsmarkern einmalig eine leere
Shellzeile an; Benchmarkpfad, Messung, Fristen und Grenzwerte unveraendert.
Die vorherige an fehlendem Prompt gescheiterte Reihe bleibt separat erhalten.
