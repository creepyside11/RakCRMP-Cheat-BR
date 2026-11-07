#!/usr/bin/env python3
"""
Build the RakCRMP Cheat APK from the stock launcher APK.

Nothing about the app is rebuilt: the dex that loads libblackrussia-client.so
gets one `invoke-static` for our loader (the loader class is added to that same
dex, so no extra classesN.dex appears), lib/arm64-v8a/librakcheat.so is added,
and every other entry keeps its exact compressed bytes. The result is signed
with APK Signature Scheme v2 using the same key as the stock launcher, so it
installs as a normal update: data, cache and launcher icon stay put.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apkpack


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
    ap.add_argument('--cert-sha256', default=None, help='pin the signer certificate')
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
        count = apkpack.repack(args.base, unsigned,
                               replace={dexname: open(newdex, 'rb').read()},
                               add=[('lib/arm64-v8a/librakcheat.so',
                                     open(args.lib, 'rb').read(), 0)])
        print('entries:', count)
        zin.close()

        key, _, cert_der = apkpack.load_key(os.path.join(args.testkey, 'testkey.pk8'),
                                            os.path.join(args.testkey, 'testkey.x509.pem'))
        signed = apkpack.sign_v2(open(unsigned, 'rb').read(), key, cert_der)
        open(args.out, 'wb').write(signed)

        errs, fp = apkpack.verify(args.out, args.cert_sha256)
        if errs:
            raise SystemExit('signed APK failed verification: %s' % errs)
        print('signed OK ->', args.out, os.path.getsize(args.out), 'bytes')
        print('signer sha256:', fp)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
