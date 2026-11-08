#!/usr/bin/env python3
"""
Build the RakCRMP Cheat APK from the stock launcher APK.

Nothing about the app is rebuilt: the dex that loads libblackrussia-client.so
gets one `invoke-static` for our loader (the loader class is added to that same
dex, so no extra classesN.dex appears), lib/arm64-v8a/librakcheat.so is added,
and every other resource is copied unchanged. ZIP layout is rebuilt with Python,
then Android's zipalign and apksigner produce a standard installable APK using
the stock launcher's signing certificate.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))



def run(cmd, **kw):
    print('$', ' '.join(cmd))
    p = subprocess.run(cmd, **kw)
    if p.returncode != 0:
        raise SystemExit('command failed: %s' % ' '.join(cmd))


def smali_cp(tools):
    jars = ['baksmali.jar', 'smali.jar', 'dexlib2.jar', 'util.jar',
            'guava.jar', 'jcommander.jar', 'failureaccess.jar', 'antlr.jar']
    return ':'.join(os.path.join(tools, j) for j in jars
                    if os.path.exists(os.path.join(tools, j)))


def find_dex(names, z):
    for n in names:
        if re.fullmatch(r'classes\d*\.dex', n or ''):
            if b'blackrussia-client' in z.read(n):
                return n
    return None


def patch_smali_tree(root):
    hits = []
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if fn.endswith('.smali'):
                p = os.path.join(dirpath, fn)
                if b'blackrussia-client' in open(p, 'rb').read():
                    hits.append(p)
    if not hits:
        raise SystemExit('no smali file references blackrussia-client')
    for p in hits:
        lines = open(p, 'r', encoding='utf-8', errors='replace').read().split('\n')
        out, armed, patched = [], False, 0
        for line in lines:
            out.append(line)
            if 'const-string' in line and 'blackrussia-client' in line:
                armed = True
                continue
            if armed and 'loadLibrary' in line:
                out.append('    invoke-static {}, Lrakcheat/Loader;->load()V')
                armed = False
                patched += 1
        if patched:
            open(p, 'w', encoding='utf-8').write('\n'.join(out))
            print('patched', p)
            return patched
    raise SystemExit('loadLibrary call not found after const-string')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', required=True, help='stock RakCRMP launcher apk')
    ap.add_argument('--lib', required=True, help='librakcheat.so (arm64-v8a)')
    ap.add_argument('--tools', required=True, help='dir with smali jars')
    ap.add_argument('--loader-smali', required=True, help='rakcheat/Loader.smali')
    ap.add_argument('--out', required=True)
    ap.add_argument('--testkey', required=True, help='dir with testkey.pk8 + testkey.x509.pem')
    ap.add_argument('--android-home', default=os.environ.get('ANDROID_HOME', ''))
    args = ap.parse_args()

    work = tempfile.mkdtemp(prefix='rakcheat-')
    try:
        zin = zipfile.ZipFile(args.base, 'r')
        dexname = find_dex(zin.namelist(), zin)
        if not dexname:
            raise SystemExit('dex with blackrussia-client not found')
        print('target dex:', dexname)

        dexpath = os.path.join(work, 'target.dex')
        open(dexpath, 'wb').write(zin.read(dexname))

        cp = smali_cp(args.tools)
        smalidir = os.path.join(work, 'smali')
        os.makedirs(smalidir, exist_ok=True)
        run(['java', '-Xmx3g', '-cp', cp, 'org.jf.baksmali.Main', 'd', dexpath, '-o', smalidir])
        patch_smali_tree(smalidir)

        # the loader class goes into the same dex: no extra classesN.dex
        pkg = os.path.join(smalidir, 'rakcheat')
        os.makedirs(pkg, exist_ok=True)
        shutil.copyfile(args.loader_smali, os.path.join(pkg, 'Loader.smali'))

        newdex = os.path.join(work, 'patched.dex')
        run(['java', '-Xmx3g', '-cp', cp, 'org.jf.smali.Main', 'a', smalidir, '-o', newdex])
        print('patched dex size:', os.path.getsize(newdex))

        unsigned = os.path.join(work, 'unsigned.apk')
        with zipfile.ZipFile(args.base, 'r') as zin, zipfile.ZipFile(unsigned, 'w') as zout:
            for item in zin.infolist():
                if item.is_dir():
                    zout.writestr(item, b'')
                    continue
                if item.filename == dexname:
                    data = open(newdex, 'rb').read()
                else:
                    data = zin.read(item.filename)
                zi = zipfile.ZipInfo(item.filename, date_time=item.date_time)
                zi.compress_type = item.compress_type
                zi.external_attr = item.external_attr
                zi.internal_attr = item.internal_attr
                zi.create_system = item.create_system
                zi.flag_bits = item.flag_bits | 0x800
                zout.writestr(zi, data)
            zout.write(args.lib, 'lib/arm64-v8a/librakcheat.so', compress_type=zipfile.ZIP_STORED)

        build_tools = os.path.join(args.android_home, 'build-tools')
        versions = sorted(os.listdir(build_tools), reverse=True)
        if not versions:
            raise SystemExit('Android build-tools not found')
        bt = os.path.join(build_tools, versions[0])
        zipalign = os.path.join(bt, 'zipalign')
        apksigner = os.path.join(bt, 'apksigner')
        aligned = os.path.join(work, 'aligned.apk')
        run([zipalign, '-P', '16', '-f', '4', unsigned, aligned])
        key_der = os.path.join(args.testkey, 'testkey.pk8')
        run([apksigner, 'sign', '--key', key_der,
             '--cert', os.path.join(args.testkey, 'testkey.x509.pem'),
             '--v1-signing-enabled', 'false', '--v2-signing-enabled', 'true',
             '--v3-signing-enabled', 'false', '--out', args.out, aligned])
        run([apksigner, 'verify', '--verbose', '--print-certs', args.out])
        print('signed OK ->', args.out, os.path.getsize(args.out), 'bytes')
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
