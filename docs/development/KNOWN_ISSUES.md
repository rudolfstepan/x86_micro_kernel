# Bekannte offene Probleme

Stand: 30. August 2026

## VMWARE-GUI-001: instabiler Retained-Surface-Resize

- Status: zurückgestellt, offen
- Betroffen: VMware-Gast mit SVGA-II/Desktop und mehreren vCPUs
- Nicht reproduziert: bisherige Tests auf echter Hardware
- Symptom: Nach interaktivem Resize von Notepad können verzögerte oder
  unvollständige Repaints auftreten; in einzelnen Läufen beendet sich die
  Anwendung oder der Desktop wird instabil.
- Bereits eingegrenzt: Configure/ACK verwendet die bestätigte Surface-Größe,
  der finale ACK invalidiert die vollständige Clientfläche und Resize benutzt
  keinen asynchronen VMware-RECT_COPY mehr. Die Beobachtung bleibt trotzdem
  offen und gilt nicht als geschlossen, bis sie auf VMware wiederholt und mit
  Gastdiagnose korreliert wurde.
- Nächste Untersuchung: serielle Gastlogs mit Prozessende-/Page-Fault-Marker,
  Configure- und Paint-Serials, SVGA-Fencezustand, vCPU-Anzahl sowie p95/p99
  von Resize-Event bis Frame-Commit erfassen. Mit einem und mehreren vCPUs und
  deaktivierter SVGA-Beschleunigung vergleichen.
- Abschlusskriterium: mindestens 100 automatisierte Resize-Sequenzen unter
  VMware ohne Prozessabbruch, verlorenen Damage oder fehlerhaften Frame sowie
  ein negativer Kontrolllauf auf echter Hardware.

Diese Notiz ist ein Fehlerdatensatz, kein Zertifizierungs- oder
Zuverlässigkeitsnachweis.
