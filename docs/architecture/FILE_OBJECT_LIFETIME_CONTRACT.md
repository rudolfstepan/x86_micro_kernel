# R3.38: gemeinsame Lebensdauer offener Dateiobjekte

Vertragsstand: 8. September 2026, Ausgang024e4ce7. Dieses Paket schliesst die
Autoritaetsgrenze zwischen vorhandenen Ring-3-Objekten und Legacy-VFS-Nodes.
Es fuehrt weder JavaScript-Schreibrechte noch ein neues persistentes Format ein.
Abnahme am 9. September 2026: gemeinsamer VFS-/Service-Schutz, Syscall129,
EXT2-Transaktions-/Recoveryadapter und Supervisor-Fencing sind angeschlossen.
Alle20 Gruppen bestanden: urspruengliche15 plus5 ausdruecklich freigegebene
Lifecycle-Pruefungen. Native O0/O2, beide Referenzimages, drei echte
Dateilebensdauer-Gastfaelle und bestehende JS-/Browsergaeste sind nachgewiesen.
Die kalte Terminierungsreparatur und ihre Grenzen sind unten dokumentiert;
exakte Belege, Fehlversuche und135 archivierte Referenzdateien in CURRENT_WORK.

## Nachinventur: weshalb ein zusaetzliches Kernelmechanismus-Paket erforderlich ist

- `fs/vfs/vfs.c`:256 registrierte Legacy-Nodes, `same_object`-Pruefung bei
  unlink/rmdir und beiden Rename-Seiten. Diese Tabelle enthaelt keine der16
  Objekte aus `userspace/programs/storage_service.c`. Deren vier Slots pro
  Client und Service-/Ownergenerationen sind eine getrennte Verwaltung.
- Der Ring-3-FAT-Locator ist Sektor,32-Byte-Directory-Offset, Startcluster und
  eine Signatur aus Zeit-/Clusterfeldern. Ein erneutes Lesen dieser Bytes ist
  kein unabhaengiger Wiederverwendungszaehler. EXT2 bindet Inodenummer und
  i_generation, aber auch dies ersetzt keine gemeinsame Unlink-Lebensdauer.
- FAT32-Legacy-Nodes identifizieren Objekte ueber Elterncluster und die elf
  Aliasbytes; FAT12 ueber Directory-Sektor/-Slot. EXT2-Legacy-Nodes haben
  bereits `node->inode`. Diese vorhandenen Identitaeten sind zu projizieren,
  nicht durch einen neuen Kernelpfadparser zu duplizieren.
- `drivers/block/ata_journal.{h,c}` ist ein transportneutraler20-Sektor-Kern
  fuer Journalv2. `kernel/init/filesystem_safety.c` koppelt seine Transaktion
  an Legacy-VFS-Mutationen. Ein zweiter Ring-3-Journalbesitzer auf demselben
  Volume darf diesen laufenden Besitzer nicht umgehen.
- FAT12 besitzt zusaetzlich eigene Journal-, Remap- und Replikatvertraege.
  FAT12-/FAT32-Datentraegerformate sind nicht allein wegen aehnlicher API
  austauschbar. Dieses Paket aendert keines dieser Formate oder Writeback-Pfade.
- Storage-Wartungs-Leases haben15s Laufzeit und blockieren ganze Volumes.
  Sie sind kein Ersatz fuer lang lebende Dateiobjekte: Das wuerde unbeteiligte
  Programme/Dateien blockieren. Ein neuer paralleler Clean/Build ist ebenfalls
  keine Recoverystrategie.
- `SYS_STORAGE_BLOCK_WRITE` prueft die aktuelle Storage-Dienstgeneration,
  Ressource und Medienzustand. Daraus folgt keine implizite objektgebundene
  Benutzer-Schreibberechtigung und keine sichere gemeinsame Objektlebensdauer.
- Auch lesende EXT2-Serviceeinstiege koennen Journal-Recovery ausloesen.
  Diese Medienwirkung muss bei der gemeinsamen Ausschlussgrenze mit erfasst
  werden; bloss `unlink`/`rename` mit einem zusaetzlichen Check zu versehen reicht
  nicht aus.

## Semantik und geschuetzte Grenze

Referenz ist der bereits dokumentierte REIST-VFS-BUSY-Vertrag mit POSIX-
Terminologie: open/close, unlink/rmdir/rename, errno. Anders als POSIX wird
Unlink-while-open weiterhin vor Wirkung mit EBUSY abgelehnt. Der neue
Mechanismus ist ausdruecklich keine POSIX-fcntl/flock- oder Directory-Sandbox-API.

Kernel: ausschliesslich feste Schluessel, Besitzer-/Mediengenerationen,
atomare Admission, Widerruf und Fencing. Keine neue Namensaufloesung,
Unicodepolitik, FAT-/EXT2-Parser, Journalformate oder Dateischreiboperationen.
Ring3: Pfade einmal aufloesen, Schluessel projizieren, Regeln/Transaktionen
ausfuehren und Ergebnisse pruefen. JS bleibt nativ eingeschraenkt.

Keine volumeweite Sperre waehrend der gesamten Handlelebensdauer. Bestehende
Objekte bleiben auch bei einer unbeteiligten Namespaceaenderung gueltig.
Eine Revision darf nur eine noch nicht veroeffentlichte Open-Admission verwerfen,
nicht alle bereits offenen Dateien invalidieren. Keine Warte-/Retry-Schleife
bei jedem Read und keine blockierenden RPCs im Browser-Eingabepfad.

## Gemeinsame Identitaet

Ein exakt32-Byte-Schluessel enthaelt kind/resource/object_a/object_b als vier
uint32_t, zwoelf Aliasbytes und ein reserviertes uint32_t:

| kind | object_a | object_b | Aliasbytes |
| --- | --- | --- | --- |
| FAT12=1 | Volume-relativer Directory-Sektor |32-Byte-ausgerichteter Byteoffset im Sektor |alle null |
| FAT32=2 |Elterncluster |0 |elf exakte 8.3-Aliasbytes, letztes Byte null |
| EXT2=3 |Inodenummer |0 |alle null |

FAT12/FAT32-LFN-, Casefold- und Mountaliasvarianten muessen auf denselben
Schluessel fuehren. Namen sind nach Open keine Autoritaet. Der Kernel bindet
den Schluessel an die bekannte physische Volume-Ausdehnung und Medienidentitaet;
Partition-/Parent-Aliase duerfen den Vergleich nicht umgehen. Nicht eindeutig
normalisierbare oder ueberlappende Mountvarianten liefern vor Wirkung einen
Fehler. Ein Medienfingerprint ist kein vom Skript frei waehlbarer Bezeichner.
Die vorhandenen Backend-Offsets sind relativ zu fs->drive. Erst der gemeinsame
Mediator addiert einen Partitionsoffset exakt einmal, prueft Ueberlauf und
logisches/physisches Ende und vergleicht die normalisierten Medienbereiche.

Vorhandene256 Legacy-Node-Slots bleiben erhalten. Fuer die16 Serviceobjekte
genuegt ein eigener fester, redundant geschuetzter Pin-Pool. Ein Pin bindet
Storage-PID/-Generation, Client-PID/-Generation, Schluessel und Medienbindung.
Die Gesamtzahl und vier Grants pro Client werden nicht vergroessert.
Ein32-Bit-Token benutzt24 Bit Slotgeneration und8 Bit Slotselektor; null ist
ungueltig. Erschoepfte Generationen werden pensioniert, nie umgebrochen.
Ein erratener Token schafft keine Autoritaet ausserhalb seiner Ownergeneration.

## Atomare Admission und Mutation

Ein32-/64-Bit-Headercheck oder lstat-then-open alleine loest das Rennen nicht:

1. Vor der Ring-3-Aufloesung einen monotonen64-Bit-Namespace-Stand aufnehmen.
   Waehrend einer unvereinbaren laufenden Mutation ist Admission geschlossen.
2. Pfad und finale Objektidentitaet im bestehenden Ring-3-Parser ermitteln.
3. Unter einer atomaren Pruefung genau diesen Stand und die Owner-/Medien-
   generation bestaetigen und den Pin registrieren. Hat eine Mutation begonnen,
   EAGAIN/EBUSY ohne Handlepublikation; kein stilles Reopen auf eine Ersatzdatei.
4. Erst nach erfolgreichem Pin das Serviceobjekt veroeffentlichen. Bei jedem
   Fehler davor den Pin explizit freigeben; Close/Delegation/Owner-Reap muessen
   die Pins exakt und idempotent mitfuehren. Delegation pinnt den bestehenden
   Schluessel neu, ohne Pfadlookup oder ein Zeitfenster ohne Quellpin.

Namespacebegin prueft Quell- und ggf. existierendes Zielobjekt gegen BEIDE
Tabellen und reserviert den Ausschluss vor der ersten Medienwirkung. Die
Pruefung und Reservation sind atomar gegen eine gleichzeitige Pinaufnahme.
Ein Legacy-Open muss auch gegen eine bereits laufende Ring-3-Mutation geordnet
sein. Source-/Destination-Checks ausserhalb dieser Reservation genuegen nicht.

Lockreihenfolge: vorhandener VFS-Operationsmutex vor kleinem Pin-Metadatenlock;
niemals rueckwaerts. Kein VFS/PIO/IPC unter dem Metadatenlock. IRQ-Abschaltung
alleine ist keine SMP-Synchronisation. Ein begrenzter atomarer Try-Lock kann
EBUSY liefern; kein Spin bis zur Freigabe. Geschuetzte Einzelrecords bleiben
innerhalb CRITICAL_OBJECT_MAX_PAYLOAD64. Integritaetsfehler sperren geschlossen,
anstatt beschaedigte Ownerdaten still zu verwerfen.

## Kleines append-only Vermittlungs-ABI

Vorbehaltlich unveraenderter Baseline wird Syscall129 FILE_OBJECT_GUARD
angehaengt;0..128 und PROCESS_RESTRICT128 bleiben unveraendert. Nur die aktuelle
autorisierte Storage-Dienstgeneration erhaelt diesen Syscall. Kompatibilitaets-,
Browser-, Script-, Compositor- und sonstige Profile erhalten keine neue Ambient-
Autoritaet. SDK-Wrapper inline halten, damit fremde Programme nicht umgelinkt
werden muessen. Bestehende READ/DATA/ALL-Rechte erhalten kein WRITE-Bit.

V1-Request exakt112 Byte: version/size/operation/flags (16), zwei Schluessel
(64), epoch/deadline_ms (je uint64_t), token/client_pid/client_generation/
reserved (je32 Bit). Keine Pointer oder Pfadbytes im Payload. Syscall prueft
den ganzen Ein-/Ausgabebereich und alle unbenutzten Nullfelder vor Wirkung.
Operationen SNAPSHOT1, PIN2, RELEASE3, MUTATION_BEGIN4, MUTATION_END5, VERIFY6.
Epoch/Token werden nur in vollstaendig validierte Ergebnisse geschrieben;
ein fehlgeschlagener Copyout darf keinen unauffindbaren Pin hinterlassen.
VERIFY bestaetigt den gehaltenen Pin samt Owner-/Service-/Medienbindung vor
jeder Serviceobjektoperation, ohne Pfadlookup. Ein vom Kernel widerrufener Pin
darf nicht nur wegen eines noch belegten Ring-3-Slots weiterbenutzt werden.
Vorhandene Maintenance-/Unmount-Pruefungen muessen beide Open-Tabellen beachten;
Medienwechsel oder administrative Neuveroeffentlichung widerrufen alte Bindungen.

Der Mediator lehnt ueberlappende Zweitmounts bereits vor dem Backend-Mount ab.
Nach einem Medienwiderruf bleiben alte Legacy-Mounts einschliesslich geteilter
Root-Nodes gesperrt: Close/Unmount, danach explizites Remount statt Erneuerung
alter Handles. Normale Namespace-Epochen widerrufen solche Mounts nicht.
Rohe Service-Write-/Flush-Syscalls werden unter demselben VFS-Mutex geordnet:
normale gemountete Volumes brauchen eine laufende eigene Mutation; bisherige
exklusive Wartung oder ungemountete Medien behalten ihren bestehenden Pfad.
Ein normaler BLOCK_WRITE-Auftrag ohne Reservation darf den Schutz nicht umgehen
und liefert EACCES vor Medienwirkung. Das ist keine neue Benutzerautoritaet.

Die sechsteilige EXT2-IO-Struktur und alte Wrapper bleiben unveraendert.
Explizite `_guarded`-Einstiege injizieren einen Hostcallback fuer den festen
Request, nehmen vor Lookup die Epoche auf und beenden jede erworbene Reservation.
Nur erfolgreicher Journalabschluss setzt DURABLE_COMMIT; schon der Versuch
einer Medienwirkung setzt UNKNOWN, nicht erst eine erfolgreiche Rueckgabe.
Erst eine neue private Testkompilierung mit REIST_OBJECT_GUARD_FAULT_TEST
enthaelt die Gast-Fehlerausloesung; normale Images besitzen diese Branches nicht.

MUTATION_END unterscheidet explizit nachgewiesen keine Wirkung, bestaetigten
dauerhaften Abschluss und unklaren Ausgang. Der Backendaufrufer muss diese
Information liefern; errno oder Close alleine sind keine Commit-Evidenz.
Erfolgreiche Journal-Recovery mit anschliessender begrenzter EAGAIN-Antwort
ist gesondert zu behandeln: kein Replay einer unbekannten Benutzertransaktion.

## Lebenszyklus, Recovery und Fehler

Mutationsreservationen laufen unter einer absoluten monotonen Deadline von
hoechstens5s. Keine Mutexbesitz-Vererbung ueber Prozesse oder unendliche Lease-
Verlaengerung. Alte Tokens/Generationen bleiben nach Reap ungueltig.
Prozess-Cleanup widerruft Pins exakt; bevor ein verstorbener oder abgelaufener
Mutationsbesitzer vergessen wird, muss sein moeglicherweise beschriebenes Medium
gefenct/quarantaenisiert sein. Der bestehende Storage-Supervisor vermittelt
diese Transition. Ein Service-Restart darf die Sperre nicht aufheben.

Abschluss mit unbekanntem Ausgang oder verlorene Reply: keine automatische
Wiederholung, kein Regrant. Nur nachgewiesene vorhandene Journal-Recovery darf
spaeter die qualifizierte Reintegration ermoeglichen. Neue automatische
Requalifizierungsregeln gehoeren nicht in dieses Paket.

Leseseitige EXT2-Recovery muss ebenfalls einen begrenzten exklusiven
Recovery-Abschnitt erwerben. Dazu darf kurzzeitig gegen alle Pins/Legacy-Nodes
des betroffenen Volumes geprueft werden; das ist kein volumeweiter Langzeitpin.
Keine nebenlaeufige Reparatur unter schon veroeffentlichten Dateiobjekten.
Ohne sichere Admission bleibt Recovery fail-closed. R3.37-COMMITTED-Regeln
bleiben unveraendert; Quarantaene darf nicht als erfolgreicher Rollback gelten.

## Eingefrorene Abnahme

### Freigegebene Terminierungsreparatur

Der Nutzer hat die nach dem reproduzierten Gastfehler angefragte Erweiterung
um Scheduler-Terminierung und ihre Regressionstests mit „mach weiter“ freigegeben.
Nur der kalte Lebenszykluspfad wird erweitert: unter Prozess -> Task-Lock wird
eine unbesessene, generationsgleiche Taskidentitaet atomar fuer Terminierung
reserviert, ihr Wait-Node entfernt und erst danach Process.terminating gesetzt.
READY, WAITING, SLEEPING und PREPARED sind zulassbar; CPU-Besitz/HANDOFF,
stale Generationen und bereits beendete oder reservierte Tasks werden vor
Seiteneffekten abgewiesen. Kein Remote-Kill eines noch CPU-besessenen Tasks.
Ein separater interner Cleanupzustand verhindert doppelte Ressourcenfreigabe;
FINISHED und Reap-Zulassung werden erst nach ihrem Abschluss publiziert.
Die bestehende process_begin_exit-Einmalzulassung bleibt unveraendert.

Der native Nachweis verwendet echte Admission-, Terminate-, Dispatch-, Wait-
und Reap-Funktionen mit erzwungenem Umschaltpunkt vor dem Cleanup. Er prueft
Wake/Timeout, Wiederholung, veraltete Identitaet und CPU-Besitz O0/O2.
Fuenf bestehende SMP-/Wait-/IPC-/Scheduler-/Terminalgruppen ergaenzen die
urspruenglichen15 Gates; diese und die Gastfristen werden nicht abgeschwaecht.
Taskauswahl, Zeitabrechnung, CPU-local und Kontextwechsel bleiben unveraendert.

Scope und genaue Gatebefehle stehen in automation/reist-s03b.toml. Zuerst echte
Regressionen, dann vollstaendiger vertikaler Schnitt; keine spaetere
Schreibobjekt-/JS-Implementierung in demselben Paket.

- Native O0/O2: echter Metadaten-/VFS-/Servicecode, beide Registraturen,
  Aliasgleichheit, Epoch-Rennen an jeder Admissiongrenze, Capacity/Overflow,
  malformed/copyout, stale Owner/Service/Medium, abgeschwaechte Delegation,
  Closefehler, Corruption/Fencing und Cleanup. Bestehende ABI-/Namespace-/JS-
  Domainpruefungen bleiben erhalten. Keine Musterpruefung als Verhaltensersatz.
- Beide normalen Referenzimages mit OBJGDTST.PRG im Userspace-Shell-Suchpfad;
  Windows-/Makefile-Paritaet. Freier Pin erlaubt weiterhin Aenderungen an
  unbeteiligten Dateien. FAT12/FAT32 und EXT2 getrennt mit passenden Medien
  pruefen, aber im selben Lebensdauerpaket und mit denselben Kerninvarianten.
- Echter1024MiB-QEMU-Gast: geoeffnete Ring-3-Dateien gegen Legacy unlink/rename,
  Legacy-Opens gegen Ring-3-Namespace, Quell-/Zielaliase, Close/Ownerverlust,
  Delegation, Servicefault/-hang/-restart und frische Generationen; alte
  Handles erhalten nie Ersatzdateidaten. Unbeteiligte Shell/Dateien bleiben
  benutzbar. Eine private Fault-Fixture darf im gesicherten Testimage eine
  aktive Reservation verlieren; im Release entsteht daraus keine Testautoritaet.
- Bestehende JS-Runner-, eingeschraenkte Worker- und externe Browsergaeste.
  Kernelaenderungen sind erwartet und muessen gegen die jeweiligen neuen
  Imagebytes geprueft werden. Die fuenf bisherigen Schutzprogramme bleiben
  bytegleich; keine ungepruefte VMware-Performance-/WCET-Zusage. Scheduler,
  CPU-local und Framebuffer-Hotpaths sowie ATA-/AHCI-Journaling bleiben unberuehrt.

Der vorhandene FAT12-Adapter hat keinen Rename-Einstieg. Der Gast weist dort
Alias-Unlink, Dateierhalt, Close, Delegation und Ownerverlust nach; das weiterhin
abgelehnte Rename wird explizit als UNSUPPORTED markiert, nicht als BUSY-Proof
gezaehlt. Rename-Quell-/Zielnachweise nutzen die vorhandenen FAT32-/EXT2-Pfade.
Die alten Syscalls0..128 behalten auch ihre groben Fehlermappings (-2 fuer
Unlink, -5 fuer Rename); der native VFS-Test prueft zusaetzlich exakt EBUSY.

Hostcompiler <=90s, einzelne Hostbinaries <=30s, einzelne Gaeste <=180s;
ein Compiler-/VM-Verifikationsblock gleichzeitig. Private Medien und Logs
unter build/codex-agent/r338-file-lifetime/, keine sichtbaren VMs/Hostdialoge,
keine Agents/Pushes. Fehlende Scope-Datei, unabgeklaerte Autoritaet oder fremde
Aenderung stoppen vor weiterer Wirkung. Erst alle Gates, Scope-/Diffpruefung,
Archiv und Queueuebergang, dann Implementierungscommit. Vertragscommit allein
ist ausdruecklich keine Laufzeitabnahme.
