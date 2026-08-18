/**
 * @file drivers/usb/hid_kb.c
 * @brief Expliziter Platzhalter für USB-HID-Tastaturen.
 *
 * Layer: Ring-0 USB driver.
 * Contract: Hardwarezustand und Pufferbereiche werden vor sichtbaren Seiteneffekten geprüft.
 * Safety: Ohne Hostcontroller- und Enumerationvertrag wird kein Eingabegerät registriert.
 */
/* USB HID keyboard support requires a working host-controller stack first. */
