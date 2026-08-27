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

    def test_high_mmio_uses_shared_tables_and_one_bounded_range_shootdown(self):
        source = (ROOT / "arch/x86/mm/paging.c").read_text(encoding="utf-8")
        self.assertIn("kernel_high_page_tables", source)
        init_start = source.index("void init_paging")
        init_body = source[init_start:source.index("bool paging_is_enabled",
                                                   init_start)]
        self.assertIn("directory_index = USER_PAGE_END", init_body)
        self.assertIn("kernel_high_page_tables[", init_body)

        range_start = source.index("static void *map_kernel_identity_range")
        range_body = source[range_start:source.index("void *map_kernel_mmio",
                                                     range_start)]
        self.assertIn("KERNEL_MMIO_MAX_BYTES", range_body)
        self.assertEqual(range_body.count("page_table_lock_acquire_irq()"), 1)
        self.assertEqual(range_body.count("tlb_shootdown_or_panic("), 1)
        self.assertIn("(existing & ~(PAGE_SIZE - 1U)) != page", range_body)
        self.assertIn("(existing_flags & PAGE_USER) != 0U", range_body)
        self.assertIn("bool compatible_wc", range_body)
        self.assertIn("PAGE_PAT_INDEX_1", range_body)

        mmio_start = source.index("void *map_kernel_mmio")
        mmio_body = source[mmio_start:source.index(
            "void *map_kernel_write_combining", mmio_start)]
        self.assertIn("map_kernel_identity_range", mmio_body)
        self.assertNotIn("map_page(", mmio_body)

    def test_e1000_maps_the_complete_register_window(self):
        source = (ROOT / "drivers/net/e1000.c").read_text(encoding="utf-8")
        self.assertIn("#define E1000_MMIO_SIZE", source)
        self.assertIn("map_mmio_region(bar0, E1000_MMIO_SIZE)", source)

    def test_process_directories_share_high_kernel_mmio_mappings(self):
        source = (ROOT / "arch/x86/mm/paging.c").read_text(encoding="utf-8")
        start = source.index("page_directory_t *create_page_directory")
        body = source[start:source.index("page_directory_t *paging_kernel_directory", start)]
        self.assertIn("i >= USER_PAGE_END", body)
        self.assertIn("entries[i] = page_directory[i]", body)

        free_start = source.index("void free_page_directory")
        free_body = source[free_start:source.index("\nint unmap_page", free_start)]
        self.assertIn("i < USER_PAGE_END", free_body)

    def test_user_copies_validate_the_complete_range(self):
        source = (ROOT / "arch/x86/mm/paging.c").read_text(encoding="utf-8")
        self.assertIn("int copy_from_user", source)
        self.assertIn("int copy_to_user", source)
        self.assertIn("copy_string_from_user", source)
        self.assertGreaterEqual(source.count("user_range_accessible("), 3)

    def test_userspace_allocations_do_not_use_the_kernel_heap(self):
        source = (ROOT / "kernel/syscall/syscall_table.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("process_user_malloc", source)
        self.assertIn("process_user_realloc", source)
        self.assertNotIn("(uintptr_t)k_malloc((size_t)arg1)", source)
        libc = (ROOT / "lib/libc/stdlib.c").read_text(encoding="utf-8")
        self.assertIn("k_malloc(size)", libc)
        self.assertIn("k_realloc(ptr, new_size)", libc)
        self.assertIn("k_free(ptr)", libc)
        self.assertNotIn("syscall(SYS_MALLOC", libc)


if __name__ == "__main__":
    unittest.main()
