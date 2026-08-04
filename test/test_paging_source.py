import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PagingSourceRegressionTests(unittest.TestCase):
    def test_kernel_enables_paging_before_hardware_initialization(self):
        source = (ROOT / "kernel/init/kernel.c").read_text(encoding="utf-8")
        paging = source.index("init_paging();")
        early = source.index("early_init();", paging)
        hardware = source.index("hardware_init();", early)
        self.assertLess(paging, early)
        self.assertLess(paging, hardware)

    def test_mmio_mapping_uses_uncached_kernel_pages(self):
        source = (ROOT / "arch/x86/mm/paging.c").read_text(encoding="utf-8")
        start = source.index("void *map_kernel_mmio")
        body = source[start:source.index("\nint map_page", start)]
        self.assertIn("paging_kernel_directory()", body)
        self.assertIn("PAGE_CACHE_DISABLE", body)

    def test_e1000_maps_the_complete_register_window(self):
        source = (ROOT / "drivers/net/e1000.c").read_text(encoding="utf-8")
        self.assertIn("#define E1000_MMIO_SIZE", source)
        self.assertIn("map_mmio_region(bar0, E1000_MMIO_SIZE)", source)


if __name__ == "__main__":
    unittest.main()
