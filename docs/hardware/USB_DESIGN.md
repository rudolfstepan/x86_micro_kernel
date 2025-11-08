# USB Support Design for x86_micro_kernel

Purpose
- Provide a compact technical plan to add USB host support (enumeration, HID keyboard, then mass storage) compatible with the project's PCI layer and VFS.

Summary
- Add USB core + Host Controller Driver (HCD) for xHCI (preferred).
- Add hub support and class drivers: HID keyboard (first) and USB Mass Storage (to mount FAT32).
- Integrate with existing PCI helpers and kernel IRQ infrastructure.

High-level layers
1. PCI probe
   - Detect USB controllers via PCI class/subclass/prog-if or specific vendor/device IDs.
   - Enable device, read BARs, map MMIO.
2. USB core
   - Device, configuration, endpoint, transfer (control/bulk/interrupt) abstractions.
   - Synchronous control-transfer helper (GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION).
   - URB-like structure for transfers (start with synchronous).
3. Host Controller Driver (HCD)
   - xHCI HCD initial subset:
     - Map MMIO (BAR), init command/event rings, basic doorbell usage.
     - Support port reset, port status, submit control/bulk/interrupt transfers.
     - Use IRQs for event ring notifications; fallback to polling for early debug.
   - Later: EHCI/OHCI/UHCI if needed for older hardware.
4. Hub driver
   - Reset ports, read port status, enumerate root ports, handle hub interrupts.
5. Class drivers
   - HID keyboard: subscribe to interrupt IN endpoints; translate HID reports to key events forwarded to existing kb driver.
   - Mass Storage (Bulk-Only Transport): implement SCSI command wrap, read/write blocks, integrate with VFS as a block device exposing FAT32 mounting.

Repository mapping (proposed files)
- drivers/usb/
  - core.c, core.h
  - xhci.c, xhci.h
  - hub.c, hub.h
  - hid_kb.c
  - mass_storage.c
- include/usb/
  - usb.h (descriptor structs, constants), usb_core.h
- docs/hardware/
  - USB_DESIGN.md (this file)
- build changes
  - Add drivers/usb/* to Makefile and gpp_Makefile

Concrete incremental plan (MVP → iterative)
- Step 0 — Prep (1–2 hrs)
  - Add files and build rules (skeletons).
  - Add debug logging macros under drivers/usb for controlled verbosity.
- Step 1 — PCI integration + detection (half day)
  - Extend pci_probe to allow driver registration by class/subclass/prog-if or add helper:
    - Detect class 0x0C (Serial Bus Controller), subclass 0x03 (USB controller).
    - Match prog-if 0x30 for xHCI.
  - In probe callback: pci_enable_device(); read BAR (MMIO); pci_set_bus_master(...).
- Step 2 — Minimal USB core (1 day)
  - Implement USB descriptors parsing and a blocking control transfer helper using MMIO HCD ops.
  - Implement simple device structure and endpoint representation.
- Step 3 — xHCI HCD skeleton + port reset (2–4 days)
  - Map MMIO from BAR, initialize command/event rings (basic), implement port reset and read port status.
  - Implement minimal Transfer submission for control transfers to get device descriptor and set address.
- Step 4 — Enumeration + hub (1–2 days)
  - Reset root ports, run enumeration flow: GET_DESCRIPTOR, SET_ADDRESS, GET_CONFIGURATION, bind class drivers.
  - Implement simple hub handling for multiple ports.
- Step 5 — HID keyboard class (1–2 days)
  - Implement descriptor parsing to find interrupt IN endpoint and schedule polling/IRQ handling.
  - Translate reports to existing keyboard input pipeline.
- Step 6 — Mass storage class + VFS glue (2–4 days)
  - Implement Bulk-Only Transport with SCSI READ/WRITE commands; wrap as a block device and mount FAT32.
- Step 7 — Robustness, power, DMA, performance (ongoing)
  - Proper memory allocation for xHCI structures (aligned, physically contiguous), IRQ handling, timeouts, retries, hub power management.

Example: PCI detection sketch
```c
// C
// Check class/subclass/prog_if fields in pci_device_t
if (dev->class_code == 0x0C && dev->subclass_code == 0x03 && dev->prog_if == 0x30) {
    // xHCI controller
    probe_xhci(dev);
}
```

Key low-level considerations
- xHCI requires 64-byte alignment for many structures; use page-aligned allocations and ensure physical address mapping.
- Use pci_set_bus_master to enable DMA.
- Port reset requires specific delays (per spec) and status polling; implement timeouts and retries.
- Event ring / TRB semantics are intricate — start with minimal control transfers and small event handling.
- VM testing: QEMU/VMware support xHCI; use `-device qemu-xhci` or `-device usb-ehci` options for testing.

Testing plan
- Add verbose USB debug channel.
- QEMU smoke tests:
  - Boot kernel with virtual xHCI device and a passed-through USB keyboard → verify key events.
  - Attach a USB mass storage image → verify mount under /mnt/usb and file read.
- Real hardware:
  - Boot from ISO/USB on a test machine; verify keyboard and USB stick mount.
- Add scripted tests (QEMU + serial log checks) to `scripts/` for automation.

Risks & mitigations
- Complexity of xHCI: mitigate by implementing only the minimal subset necessary for enumeration and control transfers first.
- DMA/physical memory mapping: reuse existing mm/paging helpers; ensure physical addresses are exposed to HCD.
- Interrupt handling correctness: provide polling fallback for early bring-up to debug without IRQs.

Next concrete actions I can take (pick one)
- Create skeleton files under `drivers/usb/` and update Makefile.
- Implement PCI detection hook and a stub xhci probe that logs BAR and IRQ.
- Implement minimal USB core control transfer helper and a test that enumerates a single device in QEMU.
