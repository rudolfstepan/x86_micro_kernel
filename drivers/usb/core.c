/**
 * @file drivers/usb/core.c
 * @brief Minimaler USB-Core-Zustand.
 *
 * Layer: Ring-0 USB driver.
 * Contract: Hardwarezustand und Pufferbereiche werden vor sichtbaren Seiteneffekten geprüft.
 * Safety: Unvollständige Transfers oder unbekannte Gerätetypen bleiben unpubliziert.
 */
#include "core.h"

/* The public USB PCI scan lives in usb_core.c.  This translation unit remains
 * as the home for future device-model-independent USB core code. */
