"""Actual Surface manager and kernel buffer mediator, with bounded host backends."""
import tempfile
import unittest
import sys
import time
from pathlib import Path
from test_gui_browser_source import run_host
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from measure_cpp_baseline import suppress_windows_test_dialogs

class HighResolutionSurfaceTests(unittest.TestCase):
    def test_incomplete_diagnostic_cannot_replace_geometry(self):
        from run_qemu_browser_resolution import ResolutionProof
        names='slot gen pid visible min max x y w h focus minx miny maxx maxy taskx tasky cw ch configured acked paint capture armed'.split()
        values=[1,2,9,1,0,0,4,4,1600,900,1,1,1,1,1,1,1,1594,870,2,2,12,0,0]
        complete='WINDOW_STATE '+' '.join(f'{k}={v}' for k,v in zip(names,values))+'\n'
        broken='WINDOW_STATE slot=1 gen=2 pid=9 vBROWSER_INPUT_READY\nisible=1 min=0\n'
        proof=ResolutionProof.__new__(ResolutionProof)
        proof.deadline=time.monotonic()+1;proof.text=lambda:complete+broken
        self.assertEqual(proof.state(pid=9,visible=1)['cw'],1594)

    def test_viewport_requires_scene_and_buffer_geometry(self):
        from run_qemu_browser_resolution import ResolutionProof
        proof=ResolutionProof.__new__(ResolutionProof)
        proof.deadline=time.monotonic()+1
        incomplete='BROWSER_VIEWPORT width=1594 height=870\n'
        stale='BROWSER_VIEWPORT width=1594 height=870 scene=1576 view=772 bufferw=800 bufferh=600 scroll=0 frames=3\n'
        complete=stale.replace('bufferw=800 bufferh=600','bufferw=1594 bufferh=870')
        records=iter([incomplete,stale,complete])
        proof.text=lambda:next(records)
        self.assertEqual(proof.viewport(0,{'cw':1594,'ch':870},0)['bufferw'],1594)

    def test_kernel_buffer_mediator(self):
        suppress_windows_test_dialogs()
        source=(ROOT/'drivers/video/framebuffer.c').read_text()
        storage=source[source.index('#define FB_SURFACE_BUFFER_CAPACITY'):source.index('static bool surface_buffer_storage_pending;')+len('static bool surface_buffer_storage_pending;')]
        operations=source[source.index('static int surface_buffer_prepare_storage'):source.index('bool framebuffer_draw_text_pixels(')]
        host=(ROOT/'test/test_high_resolution_surface_host.c').read_text().replace('@STORAGE@',storage).replace('@OPERATIONS@',operations)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'mediator.c';path.write_text(host)
            for opt in ('-O0','-O2'):
                with self.subTest(opt=opt): run_host([str(path)],flags=[opt,'-I.'])

    def test_configure_authority(self):
        suppress_windows_test_dialogs()
        source=r'''
#include <assert.h>
#include <string.h>
#include "userspace/gui/compositor/desktop_surface.h"
int main(void) {
    static desktop_surface_manager_t m;
    reist_gui_surface_owner_t owner={1,1}; reist_gui_surface_handle_t h;
    reist_gui_surface_configure_t q;
    desktop_surface_initialize(&m);
    assert(!desktop_surface_create(&m,owner,REIST_GUI_SURFACE_ROLE_TOPLEVEL,800,600,&h,&q));
    assert(!desktop_surface_ack_configure(&m,owner,h,q.serial));
    unsigned sizes[][2]={{1600,900},{2560,1440},{4096,1024},{1024,768},{800,600}};
    for(unsigned i=0;i<5;++i) {
        unsigned old=m.slots[h.id-1].width;
        assert(!desktop_surface_reconfigure(&m,owner,h,sizes[i][0],sizes[i][1],&q));
        assert(m.slots[h.id-1].width==old);
        assert(desktop_surface_reconfigure(&m,owner,h,900,600,&q)!=0);
        assert(!desktop_surface_ack_configure(&m,owner,h,q.serial));
        assert(m.slots[h.id-1].width==sizes[i][0]);
    }
    static desktop_surface_manager_t before; before=m;
    assert(desktop_surface_reconfigure(&m,owner,h,4096,4096,&q)!=0);
    assert(!memcmp(&before,&m,sizeof(m)));
    assert(desktop_surface_reconfigure(&m,owner,h,0,900,&q)!=0);
    assert(desktop_surface_reconfigure(&m,owner,h,0xffffffff,900,&q)!=0);
    assert(!memcmp(&before,&m,sizeof(m)));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'configure.c';path.write_text(source)
            for opt in ('-O0','-O2'):
                with self.subTest(opt=opt):
                    run_host([str(path),'userspace/gui/compositor/desktop_surface.c',
                              'userspace/gui/lib/font_catalog.c'],flags=[opt,'-I.'])

if __name__=='__main__': unittest.main()
