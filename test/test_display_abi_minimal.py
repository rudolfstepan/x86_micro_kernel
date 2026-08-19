"""Contracts for the minimal versioned Ring-3 framebuffer ABI."""

from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function {signature}")


class MinimalDisplayAbiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.kernel_numbers = read("lib/libc/stdlib.h")
        cls.user_header = read("userspace/sdk/include/x86os.h")
        cls.user_sdk = read("userspace/sdk/x86os.c")
        cls.syscalls = read("kernel/syscall/syscall_table.c")
        cls.framebuffer_h = read("drivers/video/framebuffer.h")
        cls.framebuffer = read("drivers/video/framebuffer.c")
        cls.display_control_h = read("drivers/video/display_control.h")
        cls.display_control = read("drivers/video/display_control.c")
        cls.process = read("kernel/proc/process.c")
        cls.scheduler = read("kernel/sched/scheduler.c")
        cls.paging_h = read("arch/x86/mm/paging.h")
        cls.paging = read("arch/x86/mm/paging.c")
        cls.display = read("drivers/video/display.c")

    def test_frame_transaction_host_behavior(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="reist-display-frame-") as temp:
            executable = Path(temp) / "display-frame-test.exe"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I.", "test/test_display_frame_transaction_host.c",
                 "drivers/video/frame_transaction.c", "-o", str(executable)],
                cwd=ROOT, check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True, timeout=5)

    def test_syscall_numbers_are_appended_without_renumbering(self) -> None:
        for name, number in (
            ("DISPLAY_INFO", 44),
            ("FILL_RECT", 45),
            ("DRAW_TEXT", 46),
        ):
            with self.subTest(name=name):
                self.assertRegex(
                    self.kernel_numbers,
                    rf"(?m)^#define\s+SYS_{name}\s+{number}\b",
                )
                self.assertRegex(
                    self.user_header,
                    rf"\bX86OS_SYS_{name}\s*=\s*{number}\b",
                )
                self.assertIn(f"case SYS_{name}:", self.syscalls)

    def test_public_structs_are_versioned_and_do_not_expose_the_lfb(self) -> None:
        self.assertIn("#define X86OS_DISPLAY_ABI_VERSION 1U", self.user_header)
        for type_name in (
            "x86os_display_info_t",
            "x86os_display_rect_t",
            "x86os_display_text_t",
        ):
            typedef = re.search(
                rf"typedef\s+struct\s*\{{(?P<body>.*?)\}}\s*{type_name}\s*;",
                self.user_header,
                re.S,
            )
            self.assertIsNotNone(typedef, type_name)
            fields = typedef.group("body")
            self.assertRegex(fields, r"uint32_t\s+version\s*;")
            self.assertRegex(fields, r"uint32_t\s+struct_size\s*;")
        info_name = "x86os_display_info_t"
        info_end = self.user_header.index(info_name)
        info_start = self.user_header.rfind("typedef struct", 0, info_end)
        info = self.user_header[info_start:info_end]
        self.assertNotRegex(info, r"(?:address|pointer|lfb)")

    def test_kernel_copies_and_validates_all_user_requests(self) -> None:
        info = function(self.syscalls, "static int syscall_display_info(")
        self.assertIn("copy_from_user(&header", info)
        self.assertIn("copy_to_user(user_info, &info", info)
        self.assertIn("FRAMEBUFFER_DISPLAY_ABI_VERSION", info)
        self.assertIn("header.struct_size < sizeof(framebuffer_display_info_t)", info)

        rectangle = function(
            self.syscalls, "static int syscall_display_fill_rect("
        )
        self.assertIn("copy_from_user(&rect", rectangle)
        self.assertIn("rect.struct_size < sizeof(rect)", rectangle)
        self.assertIn("framebuffer_fill_rect(", rectangle)

        text = function(self.syscalls, "static int syscall_display_draw_text(")
        self.assertGreaterEqual(text.count("copy_from_user("), 2)
        self.assertIn("request.struct_size < sizeof(request)", text)
        self.assertIn("request.text_length > FRAMEBUFFER_DISPLAY_MAX_TEXT", text)
        self.assertIn("user_range_accessible(", text)
        self.assertIn("framebuffer_draw_text_pixels(", text)

    def test_no_framebuffer_reports_enodev(self) -> None:
        for signature in (
            "static int syscall_display_info(",
            "static int syscall_display_fill_rect(",
            "static int syscall_display_draw_text(",
        ):
            with self.subTest(signature=signature):
                block = function(self.syscalls, signature)
                self.assertRegex(
                    block,
                    r"if\s*\(\s*!framebuffer_available\(\)\s*\)\s*return\s+-19",
                )

    def test_driver_clips_pixel_operations_and_converts_packed_rgb(self) -> None:
        rectangle = function(self.framebuffer, "bool framebuffer_fill_rect(")
        for boundary in ("left < 0", "top < 0", "fb_width", "fb_height"):
            self.assertIn(boundary, rectangle)
        self.assertIn("framebuffer_native_color(rgb & 0x00FFFFFFU)", rectangle)
        self.assertEqual(rectangle.count("framebuffer_native_color("), 1)
        self.assertIn("framebuffer_store_native(pixel, native_color)", rectangle)
        glyphs = function(
            self.framebuffer, "bool framebuffer_draw_text_pixels("
        )
        self.assertIn("fb_width", glyphs)
        self.assertIn("fb_height", glyphs)
        self.assertIn("fb_draw_glyph_pixels(", glyphs)

    def test_driver_renders_into_a_fixed_shadow_and_blits_dwords(self) -> None:
        self.assertIn("FB_SHADOW_CAPACITY", self.framebuffer)
        self.assertIn("framebuffer_shadow", self.framebuffer)
        self.assertNotRegex(self.framebuffer, r"(?:malloc|kmalloc)\s*\(")
        self.assertIn("volatile uint32_t *destination", self.framebuffer)
        self.assertIn("framebuffer_present_rect", self.framebuffer)

    def test_framebuffer_write_combining_is_feature_checked_and_falls_back(self) -> None:
        self.assertIn("map_kernel_write_combining", self.paging_h)
        self.assertIn("CPUID_FEATURE_PAT", self.paging)
        self.assertIn("CPUID_FEATURE_MSR", self.paging)
        self.assertIn("CPUID_FEATURE_SSE", self.paging)
        self.assertIn("IA32_PAT_MSR", self.paging)
        self.assertIn("PAGE_PAT_INDEX_1", self.paging)
        mapping = function(self.framebuffer, "static void framebuffer_initialize(")
        self.assertLess(mapping.index("map_kernel_write_combining("),
                        mapping.index("map_kernel_mmio("))
        self.assertIn("if (mapping != NULL) fb_scanout_write_combining = true",
                      mapping)
        self.assertIn('__volatile__("sfence"', self.framebuffer)

    def test_sdk_hides_request_marshalling(self) -> None:
        info = function(self.user_sdk, "int x86os_display_info(")
        self.assertIn("X86OS_DISPLAY_ABI_VERSION", info)
        self.assertIn("sizeof(*info)", info)
        rectangle = function(self.user_sdk, "int x86os_fill_rect(")
        self.assertIn("x86os_display_rect_t rect", rectangle)
        text = function(self.user_sdk, "int x86os_draw_text_pixels(")
        self.assertIn("length > X86OS_DISPLAY_MAX_TEXT", text)
        self.assertIn("x86os_display_text_t request", text)

    def test_frame_control_is_append_only_owner_bound_and_cleaned_up(self) -> None:
        for source, prefix in (
            (self.display_control_h, "DISPLAY_CONTROL_"),
            (self.user_header, "X86OS_DISPLAY_"),
        ):
            self.assertIn(f"#define {prefix}FRAME_BEGIN 3U", source)
            self.assertIn(f"#define {prefix}FRAME_COMMIT 4U", source)
            self.assertIn(f"#define {prefix}FRAME_CANCEL 5U", source)
        control = function(self.syscalls, "static int syscall_display_control(")
        self.assertIn("process->pid", control)
        self.assertIn("process->generation", control)
        self.assertIn("framebuffer_frame_begin", control)
        self.assertIn("framebuffer_frame_commit", control)
        self.assertIn("framebuffer_frame_cancel", control)
        self.assertIn("copy_to_user", control)
        self.assertIn("framebuffer_frame_process_cleanup", self.process)
        self.assertEqual(
            self.scheduler.count("framebuffer_frame_process_cleanup("), 2
        )

    def test_frame_commit_batches_vmware_updates(self) -> None:
        publish = function(
            self.display_control, "void display_control_present_rects("
        )
        self.assertIn("DISPLAY_CONTROL_PRESENT_CAPACITY", publish)
        self.assertIn("SVGA_CMD_UPDATE", publish)
        self.assertEqual(publish.count("SVGA_REG_SYNC"), 1)
        commit = function(self.framebuffer, "int framebuffer_frame_commit(")
        self.assertIn("framebuffer_publish_damage", commit)
        self.assertIn("display_frame_prepare_commit", commit)
        framebuffer_publish = function(
            self.framebuffer, "static void framebuffer_publish_damage("
        )
        self.assertIn("display_control_present_rects", framebuffer_publish)

    def test_sdk_exposes_frame_transaction_without_new_syscall_number(self) -> None:
        for name in ("begin", "commit", "cancel"):
            wrapper = function(self.user_sdk, f"int x86os_display_frame_{name}(")
            self.assertIn("X86OS_SYS_DISPLAY_CONTROL", wrapper)
        begin = function(self.user_sdk, "int x86os_display_frame_begin(")
        self.assertIn("request.serial", begin)

    def test_long_raster_operations_remain_preemptible(self) -> None:
        for signature in (
            "static int syscall_display_fill_rect(",
            "static int syscall_display_draw_text(",
        ):
            block = function(self.syscalls, signature)
            self.assertNotIn("scheduler_preempt_disable", block)
            self.assertNotIn("irq_disable", block)

    def test_framebuffer_console_mirrors_serial_once(self) -> None:
        putchar = function(self.display, "void display_putchar(")
        self.assertIn("serial_write_char(SERIAL_COM1, c);", putchar)
        self.assertIn("else {\n        vga_write_char(c);", putchar)
        self.assertNotRegex(
            putchar,
            r"vga_write_char\s*\(\s*c\s*\)\s*;\s*serial_write_char",
        )


if __name__ == "__main__":
    unittest.main()
