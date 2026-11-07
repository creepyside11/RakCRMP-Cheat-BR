#!/usr/bin/env python3
"""
Byte-faithful APK repack + APK Signature Scheme v2 signing, pure Python.

Repack copies every entry's *compressed* payload, CRC, flags, timestamps and
local extra verbatim; only the entries you replace are recompressed. The APK
that comes out therefore differs from the input exactly where it must, which
is what keeps the app's resources (icons included) intact.

Signing reproduces what apksigner does for a v2-only APK:
  * chunked SHA-256 content digest over (contents | central dir | EOCD)
  * RSA-PKCS1v15 over the signed-data block, cert chain embedded
"""
import hashlib, struct, zlib

LOCAL_SIG = 0x04034B50
CD_SIG = 0x02014B50
EOCD_SIG = 0x06054B50
DD_SIG = 0x08074B50
ALIGN_ID = 0xD935

MAGIC = b"APK Sig Block 42"
V2_ID = 0x7109871A
SHA256_RSA = 0x0103
ATTR_STRIPPING_PROTECTION = 0xBEEFF00D
CHUNK = 1048576


# --------------------------------------------------------------------- repack
class Entry:
    __slots__ = ("name", "raw_name", "ver", "flags", "method", "mtime", "mdate",
                 "crc", "csize", "usize", "extra", "cmade", "cneed", "iattr",
                 "eattr", "off", "data", "descriptor")


def _read_entries(raw):
    eocd = raw.rfind(struct.pack("<I", EOCD_SIG))
    if eocd < 0:
        raise ValueError("no EOCD")
    cd_size, cd_off = struct.unpack_from("<II", raw, eocd + 12)
    entries, p = [], cd_off
    while p < cd_off + cd_size and struct.unpack_from("<I", raw, p)[0] == CD_SIG:
        (_, cmade, cneed, flags, method, mtime, mdate, crc, csize, usize,
         nlen, elen, clen, disk, iattr, eattr, off) = struct.unpack_from(
            "<IHHHHHHIIIHHHHHII", raw, p)
        e = Entry()
        e.raw_name = raw[p + 46:p + 46 + nlen]
        e.name = e.raw_name.decode("utf-8", "surrogateescape")
        e.cmade, e.cneed, e.flags, e.method = cmade, cneed, flags, method
        e.mtime, e.mdate, e.crc, e.csize, e.usize = mtime, mdate, crc, csize, usize
        e.iattr, e.eattr, e.off = iattr, eattr, off
        entries.append(e)
        p += 46 + nlen + elen + clen
    if p != cd_off + cd_size:
        raise ValueError("central directory size mismatch")

    entries.sort(key=lambda x: x.off)
    for idx, e in enumerate(entries):
        lh = struct.unpack_from("<IHHHHHIIIHH", raw, e.off)
        if lh[0] != LOCAL_SIG:
            raise ValueError("bad local header for " + e.name)
        e.ver, e.flags, e.method = lh[1], lh[2], lh[3]
        e.mtime, e.mdate = lh[4], lh[5]
        nlen, elen = lh[9], lh[10]
        e.extra = raw[e.off + 30 + nlen:e.off + 30 + nlen + elen]
        start = e.off + 30 + nlen + elen
        e.data = raw[start:start + e.csize]
        end = start + e.csize
        nxt = entries[idx + 1].off if idx + 1 < len(entries) else cd_off
        e.descriptor = None
        if e.flags & 0x08:
            gap = nxt - end
            if gap == 16 and struct.unpack_from("<I", raw, end)[0] == DD_SIG:
                e.descriptor = raw[end:end + 16]
            elif gap == 12:
                e.descriptor = raw[end:end + 12]
    return entries


def _strip_align(extra):
    out, p = b"", 0
    while p + 4 <= len(extra):
        hid, hlen = struct.unpack_from("<HH", extra, p)
        if p + 4 + hlen > len(extra):
            break
        if hid != ALIGN_ID:
            out += extra[p:p + 4 + hlen]
        p += 4 + hlen
    return out


def _align_extra(base, offset, name_len, align):
    header = 30 + name_len
    need = (align - ((offset + header + len(base)) % align)) % align
    pad = need if need >= 4 else need + 4
    extra = base + struct.pack("<HH", ALIGN_ID, pad - 4) + b"\x00" * (pad - 4)
    if (offset + header + len(extra)) % align:
        raise ValueError("alignment failed for %r" % base)
    return extra


def repack(src_path, dst_path, replace=None, add=None, deflate_level=9):
    """Rewrite the zip. `replace` maps entry name -> bytes, `add` is a list of
    (name, bytes, method) appended at the end."""
    replace = dict(replace or {})
    raw = open(src_path, "rb").read()
    entries = _read_entries(raw)
    out, cd = bytearray(), bytearray()

    def emit(name, method, payload, crc, csize, usize, ver, flags, mtime, mdate,
             cmade, cneed, iattr, eattr, base_extra, descriptor=None):
        extra = _strip_align(base_extra)
        if method == 0:
            extra = _align_extra(extra, len(out), len(name), 4)
        local_flags = flags & ~0x08
        hdr_off = len(out)
        out.extend(struct.pack("<IHHHHHIIIHH", LOCAL_SIG, ver, local_flags, method,
                               mtime, mdate, crc, csize, usize,
                               len(name), len(extra)))
        out.extend(name)
        out.extend(extra)
        data_off = len(out)
        out.extend(payload)
        cd.extend(struct.pack("<IHHHHHHIIIHHHHHII", CD_SIG, cmade, cneed,
                              local_flags, method, mtime, mdate, crc, csize,
                              usize, len(name), 0, 0, 0, iattr, eattr, hdr_off))
        cd.extend(name)

    for e in entries:
        name = e.raw_name
        if e.name in replace:
            data = replace.pop(e.name)
            crc = zlib.crc32(data) & 0xFFFFFFFF
            if e.method == 8:
                co = zlib.compressobj(deflate_level, zlib.DEFLATED, -15)
                payload = co.compress(data) + co.flush()
            else:
                payload = data
            emit(name, e.method, payload, crc, len(payload), len(data),
                 e.ver, e.flags, e.mtime, e.mdate, e.cmade, e.cneed,
                 e.iattr, e.eattr, e.extra, e.descriptor)
        else:
            emit(name, e.method, e.data, e.crc, e.csize, e.usize,
                 e.ver, e.flags, e.mtime, e.mdate, e.cmade, e.cneed,
                 e.iattr, e.eattr, e.extra, e.descriptor)

    if replace:
        raise ValueError("replace entries not present in the source: %s"
                         % ", ".join(replace))

    for name, data, method in (add or []):
        raw_name = name.encode("utf-8")
        crc = zlib.crc32(data) & 0xFFFFFFFF
        if method == 8:
            co = zlib.compressobj(deflate_level, zlib.DEFLATED, -15)
            payload = co.compress(data) + co.flush()
        else:
            payload = data
        emit(raw_name, method, payload, crc, len(payload), len(data),
             10, 0x800, 0, 0x21, 20, 20, 0, 0, b"", None)

    cd_off = len(out)
    out.extend(cd)
    out.extend(struct.pack("<IHHHHIIH", EOCD_SIG, 0, 0, len(entries) + len(add or []),
                           len(entries) + len(add or []), len(cd), cd_off, 0))
    open(dst_path, "wb").write(bytes(out))
    return len(entries) + len(add or [])


# --------------------------------------------------------------------- signing
def _lp(data):
    return struct.pack("<I", len(data)) + data


def _split_zip(raw):
    i = raw.rfind(b"PK\x05\x06")
    if i < 0:
        raise ValueError("EOCD not found")
    eocd = raw[i:]
    cd_off = struct.unpack_from("<I", eocd, 16)[0]
    if cd_off > i:
        raise ValueError("bad central directory offset")
    return raw[:cd_off], raw[cd_off:i], eocd


def content_digest(sections):
    chunk_sums = []
    for section in sections:
        for i in range(0, len(section), CHUNK):
            chunk = section[i:i + CHUNK]
            h = hashlib.sha256()
            h.update(b"\xa5")
            h.update(struct.pack("<I", len(chunk)))
            h.update(chunk)
            chunk_sums.append(h.digest())
    top = hashlib.sha256()
    top.update(b"\x5a")
    top.update(struct.pack("<I", len(chunk_sums)))
    for d in chunk_sums:
        top.update(d)
    return top.digest()


def _signer_value(digest, key, cert_der):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding
    pub = key.public_key().public_bytes(serialization.Encoding.DER,
                                        serialization.PublicFormat.SubjectPublicKeyInfo)
    digests = _lp(_lp(struct.pack("<I", SHA256_RSA) + _lp(digest)))
    certs = _lp(_lp(cert_der))
    attrs = _lp(_lp(struct.pack("<II", ATTR_STRIPPING_PROTECTION, 3)))
    signed_data = digests + certs + attrs
    signature = key.sign(signed_data, padding.PKCS1v15(), hashes.SHA256())
    signatures = _lp(_lp(struct.pack("<I", SHA256_RSA) + _lp(signature)))
    signer = _lp(signed_data) + signatures + _lp(pub)
    return _lp(_lp(signer))


def _build_block(pairs):
    body = b""
    for pair_id, value in pairs:
        body += struct.pack("<Q", len(value) + 4)
        body += struct.pack("<I", pair_id)
        body += value
    size = len(body) + 8 + len(MAGIC)
    return struct.pack("<Q", size) + body + struct.pack("<Q", size) + MAGIC


def sign_v2(raw, key, cert_der):
    contents, cd, eocd_raw = _split_zip(raw)

    eocd_hashed = bytearray(eocd_raw)
    struct.pack_into("<I", eocd_hashed, 16, len(contents))
    digest = content_digest((contents, cd, bytes(eocd_hashed)))

    block = _build_block([(V2_ID, _signer_value(digest, key, cert_der))])

    eocd_final = bytearray(eocd_raw)
    struct.pack_into("<I", eocd_final, 16, len(contents) + len(block))
    return contents + block + cd + bytes(eocd_final)


def load_key(pk8_path, cert_pem_path):
    from cryptography import x509
    from cryptography.hazmat.primitives.serialization import Encoding, load_der_private_key
    key = load_der_private_key(open(pk8_path, "rb").read(), password=None)
    cert = x509.load_pem_x509_certificate(open(cert_pem_path, "rb").read())
    return key, cert, cert.public_bytes(Encoding.DER)


# --------------------------------------------------------------------- verify
def verify(path, expect_cert_sha256=None):
    """Structural check: zip consistency, alignment, v2 digest + signature."""
    import zipfile
    raw = open(path, "rb").read()
    z = zipfile.ZipFile(path)
    errs = []

    for i in z.infolist():
        data = z.read(i.filename)
        if len(data) != i.file_size:
            errs.append("size %s" % i.filename)
        if (zlib.crc32(data) & 0xFFFFFFFF) != i.CRC:
            errs.append("crc %s" % i.filename)
        if i.compress_type != 0:
            continue
        o = i.header_offset
        nlen, elen = struct.unpack_from("<HH", raw, o + 26)
        if (o + 30 + nlen + elen) % 4:
            errs.append("alignment %s" % i.filename)

    eocd = raw.rfind(b"PK\x05\x06")
    cd_off = struct.unpack_from("<I", raw, eocd + 16)[0]
    if cd_off < 16 or raw[cd_off - 16:cd_off] != MAGIC:
        return ["no v2 signature block"], None
    size = struct.unpack_from("<Q", raw, cd_off - 24)[0]
    start = cd_off - size - 8
    p, value = start + 8, None
    while p < cd_off - 24:
        ln = struct.unpack_from("<Q", raw, p)[0]
        if struct.unpack_from("<I", raw, p + 8)[0] == V2_ID:
            value = raw[p + 12:p + 8 + ln]
        p += 8 + ln
    if value is None:
        return ["no v2 signer block"], None

    def lp(b, o):
        n = struct.unpack_from("<I", b, o)[0]
        return b[o + 4:o + 4 + n], o + 4 + n

    seq, _ = lp(value, 0)
    signer, _ = lp(seq, 0)
    signed_data, o = lp(signer, 0)
    sig_seq, o2 = lp(signer, o)
    pubkey, _ = lp(signer, o2)
    certs_blob, _ = lp(signed_data, lp(signed_data, 0)[1])

    from cryptography import x509
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import padding

    n = struct.unpack_from("<I", certs_blob, 0)[0]
    cert_der = certs_blob[4:4 + n]
    cert = x509.load_der_x509_certificate(cert_der)
    fp = hashlib.sha256(cert_der).hexdigest()
    if expect_cert_sha256 and fp != expect_cert_sha256:
        errs.append("cert fingerprint %s != %s" % (fp, expect_cert_sha256))

    # recompute the content digest the same way sign_v2 does
    contents, cd, eocd_real = _split_zip(raw)
    eocd_hashed = bytearray(eocd_real)
    struct.pack_into("<I", eocd_hashed, 16, len(contents))
    digest = content_digest((contents, cd, bytes(eocd_hashed)))

    dv, _ = lp(signed_data, 0)
    algo = struct.unpack_from("<I", dv, 0)[0]
    want, _ = lp(dv, 4)
    if algo == SHA256_RSA and want != digest:
        errs.append("v2 content digest mismatch")

    sigs, _ = lp(sig_seq, 0)
    sig, _ = lp(sigs, 4)
    try:
        cert.public_key().verify(sig, signed_data, padding.PKCS1v15(), hashes.SHA256())
    except Exception as exc:
        errs.append("v2 signature invalid (%s)" % type(exc).__name__)
    return errs, fp
