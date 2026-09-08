"""Read both actual images; new kernel/JS workers, protected performance payloads."""
import argparse, hashlib, json, re, time
from pathlib import Path
from run_qemu_math import ROOT, digest, kernel_digest
from verify_text_artifacts import read_fat_file

PINNED={
    'BENCHMARK.PRG':'b001fb18597e4122dc1dad928649c8c281c71bea0cee7b19887074e13facbfb3',
    'MATHTEST.PRG':'0bee6c4057aac105bb7eb87f63869902ccde11078fc40c69f258430b77c467c6',
    'TEXTTEST.PRG':'63776333af8e28e97e5a91196826194c471893d2fe3180f2d27f50ce202cf279',
    'CURL.PRG':'aa3e619cade08e7172ebee80f192c0db728a0a5b987b23319e11448c6fc1b7cc',
    'JSTEST.PRG':'723765e1e695274750366d36858b1c5812239033c6e9847616fca2357904d440',
}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence',type=Path,required=True)
    args=parser.parse_args(); args.evidence.mkdir(parents=True,exist_ok=False)
    report={'baseline':'270754bd','passed':False,'images':{},'programs':{}}
    start=time.monotonic()
    try:
        for name,wanted in PINNED.items():
            if digest(ROOT/'build/programs'/name)!=wanted: raise ValueError('Protected program drift '+name)
        windows=(ROOT/'scripts/build-windows.ps1').read_text()
        paths={name:path for path,name in re.findall(r"'([^']+)' = '([A-Z0-9]+\.PRG)'",windows)}
        makefile=(ROOT/'Makefile').read_text()
        aliases={}
        for name,path in paths.items():
            # Shell/layout contract of this package: the two JS commands.
            # Windows also packages unrelated optional GUI fixtures. All its
            # actual program bytes are still checked in both images below.
            if name in ('JSWORK.PRG','JSIPCTST.PRG') and path+'=$(SYSTEM_PROGRAM_DIR)/'+name not in makefile:
                raise ValueError('Makefile/Windows image layout differs '+name)
            stem=Path(path).stem
            alias=path if len(stem)<=8 else str(Path(path).with_name(stem[:6]+'~1.prg')).replace('\\','/')
            if alias in aliases.values(): raise ValueError('Ambiguous short alias '+alias)
            aliases[name]=alias
        for required in (*PINNED,'JSWORK.PRG','JSIPCTST.PRG','BROWSER.PRG','HTMLWORK.PRG','DESKTOP.PRG'):
            if required not in paths: raise ValueError('Missing packaged program '+required)
        for name in paths: report['programs'][name]=digest(ROOT/'build/programs'/name)
        for target,image,kernel in (
            ('qemu',ROOT/'build/reist-os.img',ROOT/'build/kernel.bin'),
            ('vmware',ROOT/'build/vmware/reist-os/reist-os-flat.vmdk',
             ROOT/'build/codex-agent/r334-script-domain/kernel-vmware.bin')):
            actual=kernel_digest(image)
            if actual!=digest(kernel): raise ValueError('Kernel/image mismatch '+target)
            for name,wanted in report['programs'].items():
                # Image builder's native 8.3 alias. No colliding six-character
                # prefixes exist among the frozen packaged long PRG names.
                content=read_fat_file(image,aliases[name])
                if hashlib.sha256(content).hexdigest()!=wanted: raise ValueError('Stale image '+target+'/'+name)
            report['images'][target]={'sha256':digest(image),'kernel_sha256':actual}
        report['passed']=True
    except (OSError,ValueError) as error: report['error']=str(error)
    report['elapsed_seconds']=round(time.monotonic()-start,3)
    (args.evidence/'protected.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('SCRIPT_DOMAIN_ARTIFACTS '+('PASS' if report['passed'] else 'FAIL: '+report['error']))
    return 0 if report['passed'] else 1
if __name__=='__main__': raise SystemExit(main())
