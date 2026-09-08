"""Pinned memory-only FreeType build; real outlines are rasterized in HTMLWORK."""
import hashlib
import os
import shutil
import subprocess
import tarfile
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path, PurePosixPath

ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'userspace/gui/apps/browser'
FT_SHA='dc49de6b01a266eef4876a4dd34d9842c475d3e28ff2eff63bd2fb760ab56261'
FONT_SHA='7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0'
ARCHIVE=ROOT/'third_party/freetype-2.14.3.tar.gz'
FONTS=ROOT/'assets/fonts/source/liberation-2.1.5.tar.gz'
SOURCES=('src/base/ftbase.c','src/base/ftdebug.c','src/sfnt/sfnt.c',
         'src/truetype/truetype.c','src/smooth/smooth.c')
UNDEFINE=('FT_CONFIG_OPTION_USE_LZW','FT_CONFIG_OPTION_USE_ZLIB','FT_CONFIG_OPTION_USE_BZIP2',
          'FT_CONFIG_OPTION_USE_PNG','FT_CONFIG_OPTION_USE_HARFBUZZ','FT_CONFIG_OPTION_USE_BROTLI',
          'FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES','FT_CONFIG_OPTION_POSTSCRIPT_NAMES',
          'TT_CONFIG_OPTION_BYTECODE_INTERPRETER','TT_CONFIG_OPTION_GX_VAR_SUPPORT',
          'TT_CONFIG_OPTION_EMBEDDED_BITMAPS','TT_CONFIG_OPTION_COLOR_LAYERS','FT_CONFIG_OPTION_SVG',
          'FT_CONFIG_OPTION_MAC_FONTS')

def unpack(archive,sha,destination):
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=sha:raise ValueError('font archive SHA256')
    if archive.with_suffix('').with_suffix('.sha256').read_text().split()[0]!=sha:raise ValueError('font sidecar SHA256')
    total=0; paths=set()
    with tarfile.open(archive,'r:gz') as source:
        for ordinal,entry in enumerate(source):
            if ordinal>=2048:raise ValueError('font archive entries')
            p=PurePosixPath(entry.name)
            if p.is_absolute() or '..' in p.parts or len(p.parts)<1:raise ValueError('font archive path')
            if entry.isdir():continue
            if not entry.isfile() or entry.size>8*1024*1024:raise ValueError('font archive member')
            rel=Path(*p.parts[1:]); total+=entry.size
            if not rel.parts or rel in paths or total>32*1024*1024:raise ValueError('font archive quota/duplicate')
            paths.add(rel); target=destination/rel;target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(source.extractfile(entry).read())

def extract(directory):
    root=Path(directory)/'freetype'; fonts=Path(directory)/'liberation'
    unpack(ARCHIVE,FT_SHA,root);unpack(FONTS,FONT_SHA,fonts)
    # Supported FreeType build configuration, no parser source rewriting.
    config='#include <freetype/config/ftoption.h>\n'+''.join('#undef '+x+'\n' for x in UNDEFINE)
    (root/'include/reist_ftoption.h').write_text(config,encoding='ascii')
    source=['/* Unmodified pinned TrueType faces; no offline rasterization. */',
            '#ifdef _WIN32\n#define FONT_SECTION ".section .rdata,\\\"dr\\\"\\n"\n#define FONT_END ".text\\n"\n'
            '#else\n#define FONT_SECTION ".pushsection .rodata.ttf,\\\"a\\\",@progbits\\n"\n#define FONT_END ".popsection\\n"\n#endif']
    for family in ('Serif','Sans'):
        for style in ('Regular','Bold','Italic','BoldItalic'):
            name='Liberation'+family+'-'+style+'.ttf';symbol='reist_ttf_'+family+'_'+style
            source.append('__asm__(FONT_SECTION\n'
                          f'".global {symbol}\\n{symbol}:\\n.incbin \\\"{(fonts/name).as_posix()}\\\"\\n"\n'
                          f'".global {symbol}_end\\n{symbol}_end:\\n" FONT_END);')
    (root/'font_data.c').write_text('\n'.join(source)+'\n',encoding='ascii')
    return root,fonts

def flags(root):
    return ['-DFT2_BUILD_LIBRARY','-DFT_CONFIG_OPTIONS_H="reist_ftoption.h"',
            '-include',str(APP/'font_stdlib.h'),'-I'+str(root/'include'),'-I'+str(root/'src')]

def sources(root):return [root/s for s in SOURCES]+[APP/'font_platform.c',root/'font_data.c']

def build(sdk,zig,incremental):
    output=sdk.library_dir/'libreistfont.a'
    dependencies=[Path(__file__),ARCHIVE,FONTS,ARCHIVE.with_suffix('').with_suffix('.sha256'),
                  FONTS.with_suffix('').with_suffix('.sha256'),APP/'font_stdlib.h',APP/'font_platform.c']
    license_dir=sdk.root/'usr/share/licenses/browser-fonts'
    products=[output,sdk.include_dir/'reist_ftoption.h',license_dir/'freetype.txt',license_dir/'liberation.txt']
    if incremental and all(p.exists() for p in products) and all(p.stat().st_mtime_ns<=output.stat().st_mtime_ns for p in dependencies):return
    from build_user_program import freestanding_compile_prefix
    with tempfile.TemporaryDirectory(prefix='reist-font-build-') as directory:
        root,fonts=extract(directory);env=os.environ.copy()
        env['ZIG_GLOBAL_CACHE_DIR']=str(sdk.root/'font-cache');env['ZIG_LOCAL_CACHE_DIR']=str(root/'cache')
        prefix=freestanding_compile_prefix(zig,[ROOT/'userspace/libc/include'])+flags(root)+['-Os','-ffunction-sections','-fdata-sections']
        objects=[root/('font'+str(i)+'.o') for i,_ in enumerate(sources(root))]
        def one(pair):subprocess.run([*prefix,'-c',str(pair[0]),'-o',str(pair[1])],env=env,check=True,timeout=60)
        with ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(one,zip(sources(root),objects)))
        archive=root/'libreistfont.a';subprocess.run([str(zig),'ar','rcs',str(archive),*map(str,objects)],env=env,check=True,timeout=30)
        shutil.copy2(archive,output);shutil.copytree(root/'include',sdk.include_dir,dirs_exist_ok=True)
        license_dir.mkdir(parents=True,exist_ok=True)
        shutil.copy2(root/'docs/FTL.TXT',license_dir/'freetype.txt')
        shutil.copy2(fonts/'LICENSE',license_dir/'liberation.txt')
