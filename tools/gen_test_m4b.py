#!/usr/bin/env python3
"""Builds a tiny fake .m4b (no real audio) to unit-test the demuxer: python3 gen_test_m4b.py out.m4b"""
import struct, sys

def box(t, payload=b''):
    return struct.pack('>I4s', 8 + len(payload), t) + payload
def full(t, ver, fl, payload=b''):
    return box(t, struct.pack('>I', (ver << 24) | fl) + payload)

N_AUDIO, SZ = 100, 10
audio = b''.join(bytes([k]) * SZ for k in range(N_AUDIO))
texts = [b'Intro', b'Middle part', 'Caf\u00e9 end'.encode()]
text_samples = [struct.pack('>H', len(t)) + t for t in texts]

ftyp = box(b'ftyp', b'M4B \x00\x00\x00\x00M4B mp42isom')
mdat_payload = audio + b''.join(text_samples)
mdat = box(b'mdat', mdat_payload)
base = len(ftyp) + 8                      # file offset of mdat payload

def esds():
    asc = bytes([0x12, 0x10])             # AAC-LC 44.1k stereo
    dsi = bytes([0x05, len(asc)]) + asc
    dcd_body = bytes([0x40, 0x15]) + b'\x00\x00\x00' + struct.pack('>II', 64000, 64000) + dsi
    dcd = bytes([0x04, len(dcd_body)]) + dcd_body
    es_body = struct.pack('>HB', 1, 0) + dcd + bytes([0x06, 0x01, 0x02])
    es = bytes([0x03, len(es_body)]) + es_body
    return full(b'esds', 0, 0, es)

def mp4a():
    body = b'\x00' * 6 + struct.pack('>H', 1) + struct.pack('>HHIHHHHI', 0, 0, 0, 2, 16, 0, 0, 44100 << 16) + esds()
    return box(b'mp4a', body)

def stbl_audio():
    stsd = full(b'stsd', 0, 0, struct.pack('>I', 1) + mp4a())
    stts = full(b'stts', 0, 0, struct.pack('>III', 1, N_AUDIO, 1024))
    stsc = full(b'stsc', 0, 0, struct.pack('>IIII', 1, 1, 50, 1))
    stsz = full(b'stsz', 0, 0, struct.pack('>II', 0, N_AUDIO) + struct.pack('>%dI' % N_AUDIO, *([SZ] * N_AUDIO)))
    stco = full(b'stco', 0, 0, struct.pack('>III', 2, base, base + 50 * SZ))
    return box(b'stbl', stsd + stts + stsc + stsz + stco)

def trak(tid, handler, timescale, dur, stbl, tref=b''):
    tkhd = full(b'tkhd', 0, 7, struct.pack('>IIII', 0, 0, tid, 0) + b'\x00' * 60)
    mdhd = full(b'mdhd', 0, 0, struct.pack('>IIII', 0, 0, timescale, dur) + b'\x00\x00\x00\x00')
    hdlr = full(b'hdlr', 0, 0, struct.pack('>I4s', 0, handler) + b'\x00' * 12 + b'x\x00')
    minf = box(b'minf', stbl)
    return box(b'trak', tkhd + tref + box(b'mdia', mdhd + hdlr + minf))

# text track (chapters) : 3 samples, 1s/1s/rest at timescale 1000
tbase = base + len(audio)
t_stsd = full(b'stsd', 0, 0, struct.pack('>I', 1) + box(b'text', b'\x00' * 6 + struct.pack('>H', 1) + b'\x00' * 43))
t_stts = full(b'stts', 0, 0, struct.pack('>IIII', 2, 1, 800, 1) [:12] + struct.pack('>II', 1, 800) + struct.pack('>II', 1, 600)) if False else \
         full(b'stts', 0, 0, struct.pack('>I', 2) + struct.pack('>II', 2, 800) + struct.pack('>II', 1, 600))
t_stsc = full(b'stsc', 0, 0, struct.pack('>IIII', 1, 1, 3, 1))
t_stsz = full(b'stsz', 0, 0, struct.pack('>II', 0, 3) + struct.pack('>3I', *[len(s) for s in text_samples]))
t_stco = full(b'stco', 0, 0, struct.pack('>II', 1, tbase))
tstbl = box(b'stbl', t_stsd + t_stts + t_stsc + t_stsz + t_stco)

tref = box(b'tref', box(b'chap', struct.pack('>I', 2)))
trak_a = trak(1, b'soun', 44100, N_AUDIO * 1024, stbl_audio(), tref)
trak_t = trak(2, b'text', 1000, 2200, tstbl)

def item(t, payload, flags=1):
    return box(t, full(b'data', 0, flags, struct.pack('>I', 0) + payload)[8:] if False else box(b'data', struct.pack('>II', flags, 0) + payload))
ilst = box(b'ilst', item(b'\xa9nam', b'Test Book') + item(b'\xa9ART', 'Ann\u00e9 Author'.encode()) +
           item(b'covr', b'\xff\xd8' + b'J' * 40 + b'\xff\xd9', 13))
meta = full(b'meta', 0, 0, full(b'hdlr', 0, 0, struct.pack('>I4s', 0, b'mdir') + b'\x00' * 12 + b'\x00') + ilst)
# Nero chpl with 2 chapters (fewer than the text track -> text track should win)
def chap(ms100ns, name): return struct.pack('>QB', ms100ns, len(name)) + name
chpl = box(b'chpl', struct.pack('>B3sIB', 1, b'\x00\x00\x00', 0, 2) + chap(0, b'A') + chap(10000000, b'B'))
udta = box(b'udta', meta + chpl)
moov = box(b'moov', full(b'mvhd', 0, 0, b'\x00' * 96) + trak_a + trak_t + udta)

open(sys.argv[1], 'wb').write(ftyp + mdat + moov)
