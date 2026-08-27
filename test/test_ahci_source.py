import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AhciProbeContractTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_probe_requires_ahci_class_and_valid_memory_bar(self):
        header = self.read("drivers/block/ahci.h")
        source = self.read("drivers/block/ahci.c")
        kernel = self.read("kernel/init/kernel.c")
        self.assertIn("AHCI_PCI_CLASS 0x01U", header)
        self.assertIn("AHCI_PCI_SUBCLASS 0x06U", header)
        self.assertIn("AHCI_PCI_PROG_IF 0x01U", header)
        self.assertIn("device->bar[5]", source)
        self.assertIn("device->class_code", source)
        self.assertIn("ahci_init();", kernel)
        self.assertIn("map_mmio_region(controller->abar", source)
        self.assertIn("AHCI_RESET_TIMEOUT_MS", source)
        self.assertIn("AHCI_RESET_MAX_POLLS", source)
        self.assertIn("AHCI_LINK_TIMEOUT_MS", source)
        self.assertIn("ahci_wait_sata_ports", source)
        self.assertIn("AHCI_PORT_CMD_SUD", source)
        self.assertIn("AHCI_PORT_CMD_POD", source)
        self.assertIn("AHCI_PORT_SCTL", source)
        self.assertIn("signature == 0U", source)
        self.assertIn("AHCI_PORT_SSTS", source)
        self.assertIn("AHCI_PORT_SIG", source)
        self.assertIn("AHCI_SIG_ATA", source)
        self.assertIn("__attribute__((aligned(1024)))", source)
        self.assertIn("ahci_dma_address_valid", source)
        self.assertIn("AHCI_PORT_CLB", source)
        self.assertIn("AHCI_PORT_FB", source)
        self.assertIn("ahci_build_identify_command", source)
        self.assertIn("IDENTIFY DEVICE", source)
        self.assertIn("prdt_length = 1U", source)
        self.assertIn("byte_count_and_interrupt", source)
        self.assertIn("AHCI_PORT_CI", source)
        self.assertIn("AHCI_PORT_IS_TFES", source)
        self.assertIn("AHCI_PORT_TFD_DRQ", source)
        self.assertIn("AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ", source)
        self.assertIn("AHCI_PORT_SERR", source)
        self.assertIn("AHCI_COMMAND_TIMEOUT_MS", source)
        self.assertIn("#define AHCI_COMMAND_TIMEOUT_MS 5000U", source)
        self.assertIn("#define AHCI_ACTIVE_POLL_LIMIT 4096U", source)
        wait_start = source.index("static bool ahci_wait_for_poll_tick(")
        wait_end = source.index("static bool ahci_reset(", wait_start)
        wait = source[wait_start:wait_end]
        self.assertIn("irq_enabled()", wait)
        self.assertIn("irq_in_context()", wait)
        self.assertIn('volatile("pause"', wait)
        self.assertIn('volatile("hlt"', wait)
        execute_start = source.index("static bool ahci_execute_command(")
        execute_end = source.index("static bool ahci_port_acquire(",
                                   execute_start)
        execute = source[execute_start:execute_end]
        self.assertGreaterEqual(
            execute.count("ahci_wait_for_poll_tick(poll)"), 2)
        self.assertIn("ahci_stop_port(controller->mmio, port)", source)
        self.assertIn("engine_running", source)
        self.assertIn("REIST_AHCI_FAULT_INJECTION", source)
        self.assertIn("REIST_AHCI_FAULT_TFES", source)
        self.assertIn("REIST_AHCI_FAULT_TFD", source)
        self.assertIn("injected_timeout", source)
        self.assertIn("injected_tfes", source)
        self.assertIn("injected_tfd_error", source)
        self.assertIn("injected fault mode", source)
        self.assertIn("command & ~AHCI_PORT_CMD_ST", source)
        self.assertIn("command & ~AHCI_PORT_CMD_FRE", source)
        self.assertIn("ahci_identify_word", source)
        self.assertIn("identify_valid_ports", source)
        self.assertIn("sector_count", header)
        self.assertIn("sector_size", header)
        self.assertIn("ahci_publish_drives", source)
        self.assertIn("DRIVE_TYPE_AHCI", source)
        self.assertIn("hdd%u", source)
        self.assertIn("pci_set_bus_master", source)
        self.assertIn("AHCI_ATA_READ_DMA_EXT", source)
        self.assertIn("AHCI_ATA_WRITE_DMA_EXT", source)
        self.assertIn("AHCI_ATA_FLUSH_CACHE_EXT", source)
        self.assertIn("ahci_port_acquire", source)
        self.assertIn("write_verify_buffers", source)
        self.assertIn("memcmp(dma_buffer, expected, 512U)", source)
        self.assertIn("write verification failed", source)
        self.assertIn("ahci_probe_diagnostics", source)

    def test_partition_batch_reads_follow_ahci_parent(self):
        source = self.read("drivers/block/ata.c")
        batch_read = source.split("bool ata_read_sectors(", 1)[1].split(
            "bool ata_read_sector(", 1)[0]
        self.assertIn("if (parent->type == DRIVE_TYPE_AHCI)", batch_read)
        self.assertIn("ahci_read_sector(parent, absolute + index", batch_read)
        self.assertLess(
            batch_read.index("if (parent->type == DRIVE_TYPE_AHCI)"),
            batch_read.index("ata_read_sectors_pio_impl(parent->base"))

    def test_hotplug_requalification_resets_and_reidentifies_port(self):
        header = self.read("drivers/block/ahci.h")
        source = self.read("drivers/block/ahci.c")
        start = source.index("bool ahci_requalify_drive(")
        end = source.index("static bool ahci_write_sector_internal", start)
        recovery = source[start:end]
        self.assertIn("ahci_stop_port", recovery)
        self.assertIn("AHCI_PORT_SCTL", recovery)
        self.assertIn("pit_monotonic_ms", recovery)
        self.assertIn("AHCI_RESET_TIMEOUT_MS", recovery)
        self.assertIn("ahci_build_identify_command", recovery)
        self.assertIn("ahci_parse_identify", recovery)
        self.assertIn("controller->sector_count[port]", recovery)
        self.assertIn("memcmp(controller->model[port], drive->model", recovery)
        self.assertIn("ahci_requalify_drive", header)
        self.assertIn("ahci_write_sector_recovery", header)
        self.assertLess(recovery.index("ahci_stop_port"),
                        recovery.index("ahci_build_identify_command"))
        self.assertLess(recovery.index("ahci_build_identify_command"),
                        recovery.index("ahci_parse_identify"))

    def test_port_transactions_use_bounded_sleepable_mutexes(self):
        source = self.read("drivers/block/ahci.c")
        header = self.read("drivers/block/ahci.h")
        storage = self.read("kernel/init/storage_safety.c")
        self.assertIn("kernel_mutex_t port_mutex", source)
        self.assertIn("AHCI_PORT_LOCK_TIMEOUT_MS", source)
        self.assertIn("kernel_mutex_lock_until(", source)
        self.assertIn("kernel_mutex_unlock(", source)
        self.assertNotIn("static bool port_busy", source)

        acquire_start = source.index("static bool ahci_port_acquire(")
        acquire_end = source.index("static void ahci_port_release(",
                                   acquire_start)
        acquire = source[acquire_start:acquire_end]
        self.assertIn("KASSERT_NOT_IRQ()", acquire)
        self.assertIn("KASSERT_CAN_SLEEP()", acquire)
        self.assertNotIn("irq_save()", acquire)

        fence_start = source.index("void ahci_fence_writes(")
        fence_end = source.index("void ahci_restore_writes_after_recovery(",
                                 fence_start)
        fence = source[fence_start:fence_end]
        self.assertLess(fence.index("writes_fenced = true"),
                        fence.index("ahci_ports_acquire"))
        self.assertIn("ahci_ports_release", fence)

        self.assertIn("bool ahci_writes_quiescent(void);", header)
        self.assertIn("ahci_writes_quiescent()", storage)

    def test_normal_write_rechecks_fence_after_port_lock(self):
        source = self.read("drivers/block/ahci.c")
        start = source.index("static bool ahci_write_sector_internal(")
        end = source.index("bool ahci_write_sector(", start)
        write = source[start:end]
        acquired = write.index("ahci_port_acquire(")
        recheck = write.index("writes_fenced && !recovery", acquired)
        release = write.index("ahci_port_release(", recheck)
        self.assertLess(acquired, recheck)
        self.assertLess(recheck, release)


if __name__ == "__main__":
    unittest.main()
