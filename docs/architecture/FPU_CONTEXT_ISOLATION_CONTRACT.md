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
