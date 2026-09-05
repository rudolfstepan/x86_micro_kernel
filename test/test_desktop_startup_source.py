import tempfile
import tomllib
import unittest
from pathlib import Path
from test_gui_browser_source import run_host

ROOT = Path(__file__).resolve().parents[1]

# Compile the real startup reader. Mock only the OS/service boundary.
HOST = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define main static desktop_application_main
#include "userspace/gui/compositor/desktop.c"
#undef main

static unsigned mode, opened, closed, reads, reports;
static uint64_t now;
void x86os_puts(const char *text) { (void)text; }
void x86os_print_number(int number) { (void)number; }
void x86os_putchar(char ch) { (void)ch; }
int x86os_monotonic_ms(uint64_t *value) { *value=++now; return 0; }
int x86os_reist_report(uint32_t event, uint32_t sequence) {
    assert(event==X86OS_REIST_REPORT_PROGRESS && sequence==reports+1);
    ++reports; return mode==6 ? -5 : 0;
}
int reist_vfs_file_open(const char *path, uint32_t timeout, reist_vfs_file_handle_t *handle) {
    assert(!strcmp(path,DESKTOP_FONT_PATH) && timeout==DESKTOP_FILE_READ_TIMEOUT_MS);
    if (mode==1) return -2;
    ++opened; *handle=7; return 0;
}
int reist_vfs_file_fstat(reist_vfs_file_handle_t handle, x86os_file_info_t *info) {
    assert(handle==7 && opened && !closed); memset(info,0,sizeof(*info));
    info->type=X86OS_FILE; info->size=mode==2 ? 65 : 17; return 0;
}
int reist_vfs_file_read_bulk(reist_vfs_file_handle_t handle, void *target, size_t size) {
    assert(handle==7 && opened && !closed && size<=X86OS_STORAGE_BULK_MAX_BYTES);
    ++reads;
    if (mode==3) return 0;
    if (mode==4) return (int)size+1;
    if (mode==7) return -110;
    size_t amount=size>7 ? 7 : size;
    memset(target,'x',amount); return (int)amount;
}
int reist_vfs_file_close(reist_vfs_file_handle_t handle) {
    assert(handle==7 && opened && !closed); ++closed; return mode==5 ? -5 : 0;
}
int main(void) {
    for (mode=0; mode<8; ++mode) {
        opened=closed=reads=reports=0; now=0;
        unsigned char bytes[66]; memset(bytes,0xcc,sizeof(bytes));
        size_t length=99; uint32_t sequence=1; uint64_t heartbeat=0;
        int result=read_file_bounded_progress(DESKTOP_FONT_PATH,bytes+1,64,&length,
                                               1,&sequence,&heartbeat);
        assert(bytes[0]==0xcc && bytes[65]==0xcc);
        assert(closed==opened && opened==(mode!=1));
        if (mode==0) {
            assert(!result && length==17 && reads==3 && reports==3 && sequence==4 && heartbeat);
            for (unsigned i=1; i<=17; ++i) assert(bytes[i]=='x');
        } else assert(result<0 && !length);
        if (mode==1 || mode==2) assert(!reads && !reports);
        if (mode==3 || mode==4 || mode==7) assert(reads==1 && !reports);
    }
    puts("DESKTOP_STARTUP_READER_HOST_OK"); return 0;
}
'''


class DesktopStartupTests(unittest.TestCase):
    def test_real_reader_publication_cleanup_and_lifecycle(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "startup.c"
            source.write_text(HOST, encoding="utf-8")
            run_host([str(source)], flags=["-I.", "-Iuserspace/sdk/include",
                     "-Iuserspace/storage/include", "-Iuserspace/config/include",
                     "-Wno-unused-function"])

    def test_qemu_runtime_gets_matching_reference_build(self):
        queue = tomllib.loads((ROOT / "automation/reist-s03b.toml").read_text())
        package = next(p for p in queue["packages"] if p["id"]=="R3.7-browser-http-navigation")
        self.assertIn(".\\scripts\\test-reist-package.ps1 -Target vmware -Video vga", package["package_tests"])
        self.assertEqual(package["package_tests"][-1], ".\\scripts\\test-reist-package.ps1 -Target qemu -Video vga")
        self.assertEqual(package["runtime_tests"], [
            ".\\scripts\\test-reist-runtime.ps1 -Mode curl-client -Target qemu -Video vga",
            ".\\scripts\\test-reist-runtime.ps1 -Mode runtime-desktop-browser -Target qemu -Video vga"])
        self.assertIn("test_unicode_raster()", (ROOT / "userspace/programs/guest_test.c").read_text())


if __name__ == "__main__":
    unittest.main()
