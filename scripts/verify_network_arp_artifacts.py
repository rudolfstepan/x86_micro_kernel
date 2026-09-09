"""R3.41a: unchanged actual kernels/92 programs, only network service changes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from run_qemu_math import ROOT,digest,kernel_digest
from verify_file_object_guard_artifacts import program_paths
from verify_text_artifacts import read_fat_file


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--evidence',type=Path,required=True)
    args=parser.parse_args();evidence=args.evidence.resolve();allowed=(ROOT/'build/codex-agent/r341a-network-arp').resolve()
    if evidence.exists() or evidence==allowed or not evidence.is_relative_to(allowed):parser.error('new r341a subdirectory required')
    evidence.mkdir(parents=True);start=time.monotonic();report={'passed':False,'programs':{},'images':{}}
    try:
        baseline=json.loads((ROOT/'build/codex-agent/r340-fat32-recovery/artifacts/protected.json').read_text())
        paths=program_paths()
        if not baseline['passed'] or len(paths)!=93 or set(paths)!=set(baseline['programs']):raise ValueError('accepted inventory mismatch')
        for name in paths:
            actual=digest(ROOT/'build/programs'/name)
            if name!='REIST.PRG' and actual!=baseline['programs'][name]:raise ValueError('protected payload drift '+name)
            report['programs'][name]=actual
        if report['programs']['REIST.PRG']==baseline['programs']['REIST.PRG']:raise ValueError('network service not rebuilt')
        for target,image,kernel in (('qemu',ROOT/'build/reist-os.img',ROOT/'build/kernel.bin'),('vmware',ROOT/'build/vmware/reist-os/reist-os-flat.vmdk',allowed/'kernel-vmware.bin')):
            actual=kernel_digest(image)
            if actual!=digest(kernel) or actual!=baseline['images'][target]['kernel_sha256']:raise ValueError('kernel drift '+target)
            for name,expected in report['programs'].items():
                if hashlib.sha256(read_fat_file(image,paths[name])).hexdigest()!=expected:raise ValueError('stale packaged payload '+target+'/'+name)
            report['images'][target]={'sha256':digest(image),'kernel_sha256':actual}
        for directory in ('kernel','drivers','fs','arch','include','userspace/sdk'):
            changed=subprocess.check_output(['git','diff','--name-only','7d87119c','--',directory],cwd=ROOT,timeout=15)
            if changed.strip():raise ValueError('protected source drift '+directory)
        report['passed']=True
    except (OSError,ValueError,subprocess.SubprocessError) as error:report['error']=str(error)
    report['elapsed_seconds']=round(time.monotonic()-start,3)
    (evidence/'protected.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('NETWORK_ARP_ARTIFACTS','PASS' if report['passed'] else 'FAIL: '+report['error'])
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
