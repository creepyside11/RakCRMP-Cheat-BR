#!/usr/bin/env python3
"""
Packs RakCRMP Cheat into a stock RakCRMP APK:
  * patch the dex that loads libblackrussia-client.so so it also loads our loader
  * drop classes8.dex (rakcheat.Loader) + lib/arm64-v8a/librakcheat.so
  * zipalign + apksigner
"""
import argparse, os, re, shutil, subprocess, sys, tempfile, zipfile

def run(cmd, **kw):
    print('$', ' '.join(cmd))
    p = subprocess.run(cmd, **kw)
    if p.returncode != 0:
        raise SystemExit('command failed: %s' % ' '.join(cmd))

def smali_cp(tools):
    jars = [
        'baksmali.jar', 'smali.jar', 'dexlib2.jar', 'util.jar',
        'guava.jar', 'jcommander.jar', 'failureaccess.jar', 'antlr.jar',
    ]
    out = [os.path.join(tools, j) for j in jars if os.path.exists(os.path.join(tools, j))]
    return ':'.join(out)

def find_dex(names, z):
    for n in names:
        if re.fullmatch(r'classes\d*\.dex', n or ''):
            data = z.read(n)
            if b'blackrussia-client' in data:
                return n
    return None

def patch_smali_tree(root):
    needle = b'blackrussia-client'
    hits = []
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if not fn.endswith('.smali'):
                continue
            p = os.path.join(dirpath, fn)
            data = open(p, 'rb').read()
            if needle in data:
                hits.append(p)
    if not hits:
        raise SystemExit('no smali file references blackrussia-client')
    patched = 0
    for p in hits:
        lines = open(p, 'r', encoding='utf-8', errors='replace').read().split('\n')
        out = []
        armed = False
        for i, line in enumerate(lines):
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
    ap.add_argument('--base', required=True)
    ap.add_argument('--lib', required=True, help='librakcheat.so (arm64)')
    ap.add_argument('--loader-dex', required=True, help='classes8.dex with rakcheat.Loader')
    ap.add_argument('--tools', required=True, help='dir with smali jars')
    ap.add_argument('--out', required=True)
    ap.add_argument('--android-home', default=os.environ.get('ANDROID_HOME', ''))
    ap.add_argument('--keystore', required=True)
    ap.add_argument('--ks-pass', required=True)
    ap.add_argument('--key-alias', default='rakcrmp')
    args = ap.parse_args()

    bt = None
    for cand in sorted(os.listdir(os.path.join(args.android_home, 'build-tools')), reverse=True) \
            if args.android_home and os.path.isdir(os.path.join(args.android_home, 'build-tools')) else []:
        bt = os.path.join(args.android_home, 'build-tools', cand)
        break
    zipalign = os.path.join(bt, 'zipalign') if bt else 'zipalign'
    apksigner = os.path.join(bt, 'apksigner') if bt else 'apksigner'
    print('zipalign:', zipalign)
    print('apksigner:', apksigner)

    work = tempfile.mkdtemp(prefix='rakcheat-')
    try:
        zin = zipfile.ZipFile(args.base, 'r')
        names = zin.namelist()
        dexname = find_dex(names, zin)
        if not dexname:
            raise SystemExit('dex with blackrussia-client not found')
        print('target dex:', dexname)

        dexbytes = zin.read(dexname)
        dexpath = os.path.join(work, 'target.dex')
        open(dexpath, 'wb').write(dexbytes)

        cp = smali_cp(args.tools)
        smalidir = os.path.join(work, 'smali')
        os.makedirs(smalidir, exist_ok=True)
        run(['java', '-Xmx3g', '-cp', cp, 'org.jf.baksmali.Main', 'd', dexpath, '-o', smalidir])
        patch_smali_tree(smalidir)
        newdex = os.path.join(work, 'patched.dex')
        run(['java', '-Xmx3g', '-cp', cp, 'org.jf.smali.Main', 'a', smalidir, '-o', newdex])
        print('patched dex size:', os.path.getsize(newdex))

        tmpapk = os.path.join(work, 'unsigned.apk')
        with zipfile.ZipFile(tmpapk, 'w') as zout:
            for item in zin.infolist():
                if item.filename == dexname:
                    data = open(newdex, 'rb').read()
                    zout.writestr(zipfile.ZipInfo(item.filename), data, compress_type=zipfile.ZIP_DEFLATED)
                    continue
                if item.filename.endswith('/'):
                    continue
                data = zin.read(item.filename)
                zout.writestr(item, data, compress_type=item.compress_type)
            zout.write(args.loader_dex, 'classes8.dex', compress_type=zipfile.ZIP_DEFLATED)
            zout.write(args.lib, 'lib/arm64-v8a/librakcheat.so', compress_type=zipfile.ZIP_STORED)
        zin.close()

        aligned = os.path.join(work, 'aligned.apk')
        run([zipalign, '-p', '-f', '4', tmpapk, aligned])
        run([apksigner, 'sign', '--ks', args.keystore, '--ks-pass', 'pass:' + args.ks_pass,
             '--key-pass', 'pass:' + args.ks_pass, '--ks-key-alias', args.key_alias,
             '--v1-signing-enabled', 'true', '--v2-signing-enabled', 'true',
             '--out', args.out, aligned])
        run([apksigner, 'verify', '--print-certs', args.out])
        print('OK ->', args.out, os.path.getsize(args.out), 'bytes')
    finally:
        shutil.rmtree(work, ignore_errors=True)

if __name__ == '__main__':
    main()
