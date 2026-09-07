"""Real browser publication regression, independent of a VM timing sample."""
from pathlib import Path
import tempfile
import unittest
from test_gui_browser_source import run_host

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    import re
    match = re.search(r'^static [^\n;]*\b' + name + r'\(', source, re.M)
    if not match:
        raise AssertionError('missing real function ' + name)
    start = match.start()
    brace = source.index('{', match.end())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


class BrowserSurfaceLatencyTests(unittest.TestCase):
    def test_ready_surface_broker_does_not_pay_idle_timer(self):
        for optimization in ('-O0', '-O2'):
            with self.subTest(optimization=optimization):
                run_host(['test/test_gui_surface_client_host.c', 'userspace/gui/lib/surface_client.c',
                    'userspace/gui/lib/font_catalog.c'], flags=[optimization, '-Iuserspace/sdk/include'])

    def test_escape_lookahead_preserves_next_key(self):
        source = (ROOT/'userspace/gui/compositor/desktop.c').read_text()
        enum = source[source.index('enum {\n    DESKTOP_KEY_NONE'):]
        real = enum[:enum.index('};')+2] + '\n'
        real += function(source, 'read_escape_byte') + function(source, 'read_key')
        harness = r'''
#include <assert.h>
#include <stdio.h>
static const unsigned char *input;
static unsigned sleeps;
static int x86os_getchar_nonblocking(void) { return *input ? *input++ : 0; }
static int x86os_sleep_ms(unsigned ms) { assert(ms==1); ++sleeps; return 0; }
@REAL@
int main(void) {
    input=(const unsigned char *)"\033\033";
    assert(read_key()==DESKTOP_KEY_ESCAPE);
    assert(read_key()==DESKTOP_KEY_ESCAPE && sleeps==20);
    assert(read_key()==DESKTOP_KEY_NONE);
    input=(const unsigned char *)"\033x"; sleeps=0;
    assert(read_key()==DESKTOP_KEY_ESCAPE && read_key()=='x' && !sleeps);
    input=(const unsigned char *)"\033\033[A";
    assert(read_key()==DESKTOP_KEY_ESCAPE && read_key()==DESKTOP_KEY_UP && !sleeps);
    input=(const unsigned char *)"\033[B\033[C\033[D";
    assert(read_key()==DESKTOP_KEY_DOWN && read_key()==DESKTOP_KEY_RIGHT && read_key()==DESKTOP_KEY_LEFT);
    assert(read_key()==DESKTOP_KEY_NONE);
    puts("DESKTOP_ESCAPE_LOOKAHEAD_OK"); return 0;
}
'''.replace('@REAL@', real)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'desktop-escape-lookahead.c'
            path.write_text(harness, encoding='utf-8')
            for optimization in ('-O0', '-O2'):
                with self.subTest(optimization=optimization):
                    run_host([str(path)], flags=[optimization])

    def test_real_publication_damage_and_fault_cleanup(self):
        source = (ROOT/'userspace/gui/apps/browser/main.c').read_text()
        real = source[:source.index('static void finish_load_turn(')]
        real += source[source.index('enum { TIME_READ'):source.index('static uint32_t timing_start(')]
        for name in ('timing_start', 'timing_end', 'viewport_height', 'render_result', 'publish_pixels'):
            real += function(source, name)
        harness = (ROOT/'test/test_browser_surface_latency_host.c').read_text().replace('@REAL@', real)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'browser-surface-latency.c'
            path.write_text(harness, encoding='utf-8')
            for optimization in ('-O0', '-O2'):
                with self.subTest(optimization=optimization):
                    run_host([str(path)], flags=[optimization, '-Iuserspace/sdk/include',
                        '-Iuserspace/storage/include', '-Wno-unused-function', '-Wno-unused-variable'])


if __name__ == '__main__':
    unittest.main()
