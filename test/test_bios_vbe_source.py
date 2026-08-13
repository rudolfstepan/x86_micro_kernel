import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class BiosVbeSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (
            ROOT / "arch/x86/boot/bios/stage2_bios.asm"
        ).read_text(encoding="utf-8")

    def test_vbe_path_is_framebuffer_only_and_runs_at_final_handoff(self):
        start = self.source.split("start:", 1)[1].split("disk_error:", 1)[0]
        guarded_call = "%ifdef USE_FRAMEBUFFER\n" \
                       "    ; Keep BIOS text diagnostics available until"
        self.assertIn(guarded_call, start)
        self.assertLess(start.index("call print_string", start.index("msg_start")),
                        start.index("call setup_vbe_framebuffer"))
        self.assertLess(start.index("call setup_vbe_framebuffer"),
                        start.index("jmp enter_kernel"))

    def test_mode_search_prefers_1024_then_falls_back_to_800(self):
        setup = self.source.split("setup_vbe_framebuffer:", 1)[1].split(
            "find_vbe_mode:", 1
        )[0]
        preferred = setup.index("mov word [vbe_target_width], 1024")
        fallback = setup.index("mov word [vbe_target_width], 800")
        self.assertLess(preferred, fallback)
        self.assertIn("mov word [vbe_target_height], 768", setup)
        self.assertIn("mov word [vbe_target_height], 600", setup)

    def test_search_requires_supported_graphics_direct_color_lfb(self):
        search = self.source.split("find_vbe_mode:", 1)[1].split(
            "parse_elf_header:", 1
        )[0]
        self.assertIn("VBE_MODE_SUPPORTED | VBE_MODE_GRAPHICS | VBE_MODE_LFB", search)
        self.assertIn("cmp byte [es:VBE_MODE_INFO_ADDRESS + 25], 32", search)
        self.assertIn("cmp byte [es:VBE_MODE_INFO_ADDRESS + 27], VBE_MEMORY_DIRECT", search)
        self.assertIn("or bx, VBE_LFB_REQUEST", self.source)

    def test_channel_validation_cannot_wrap_and_rejects_overlap(self):
        search = self.source.split(".masks_ready:", 1)[1].split(
            "mov ax, [vbe_candidate_mode]", 1
        )[0]
        for channel in ("red", "green", "blue"):
            self.assertIn(f"movzx ax, byte [vbe_{channel}_size]", search)
            self.assertIn(f"movzx dx, byte [vbe_{channel}_position]", search)
            self.assertIn(f"mov [vbe_{channel}_end], ax", search)
        self.assertNotIn("add al, [vbe_", search)
        self.assertIn(".red_green_separate:", search)
        self.assertIn(".red_blue_separate:", search)
        self.assertIn(".channels_separate:", search)
        self.assertIn("cmp word [vbe_red_end], bx", search)
        self.assertIn("cmp word [vbe_green_end], ax", search)
        self.assertIn("cmp word [vbe_blue_end], ax", search)

    def test_lfb_range_matches_kernel_mmio_mapping_contract(self):
        search = self.source.split("; map_kernel_mmio uses", 1)[1].split(
            "mov ax, [vbe_candidate_mode]", 1
        )[0]
        self.assertIn("movzx eax, word [vbe_selected_pitch]", search)
        self.assertIn("movzx ecx, word [vbe_target_height]", search)
        self.assertIn("mul ecx", search)
        self.assertIn("test edx, edx", search)
        self.assertIn("add eax, ebx\n    jc .next_mode", search)
        self.assertIn("cmp eax, 0x40000000", search)
        self.assertIn("cmp ebx, 0xC0000000", search)
        self.assertLess(search.index("add eax, ebx"),
                        search.index("cmp eax, 0x40000000"))

    def test_success_publishes_multiboot_framebuffer_offsets_and_flag_last(self):
        publish = self.source.split(".mode_found:", 1)[1].split(".failed:", 1)[0]
        for offset in (88, 92, 96, 100, 104, 108, 109, 110, 111,
                       112, 113, 114, 115):
            self.assertIn(f"MB_INFO_ADDRESS + {offset}", publish)
        self.assertEqual(publish.count("MULTIBOOT_FLAG_FRAMEBUFFER"), 1)
        self.assertGreater(publish.index("MULTIBOOT_FLAG_FRAMEBUFFER"),
                           publish.index("MB_INFO_ADDRESS + 115"))

    def test_failure_restores_mode_03_and_invalidates_framebuffer(self):
        failure = self.source.split(".failed:", 1)[1].split(
            "find_vbe_mode:", 1
        )[0]
        self.assertIn("mov ax, 0x0003", failure)
        self.assertIn("and dword [es:MB_INFO_ADDRESS], 0xFFFFEFFF", failure)
        self.assertIn("mov di, MB_INFO_ADDRESS + 88", failure)
        self.assertIn("mov cx, 28 / 2", failure)

    def test_build_frontends_forward_framebuffer_define_to_stage2(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        stage2_commands = [line for line in makefile.splitlines()
                           if "stage2_bios.asm" in line and "$(AS)" in line]
        self.assertEqual(len(stage2_commands), 2)
        self.assertTrue(all("$(VIDEO_DEFINES)" in line
                            for line in stage2_commands))

        windows = (ROOT / "scripts/build-windows.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("if ($Video -eq 'framebuffer')", windows)
        self.assertIn("$stage2Arguments += '-DUSE_FRAMEBUFFER'", windows)
        self.assertIn("& $Nasm @stage2Arguments", windows)


if __name__ == "__main__":
    unittest.main()
