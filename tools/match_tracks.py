#!/usr/bin/env python3
"""tools/match_tracks.py: match the game's streamed music against the album.

Builds the evidence behind port/audio/track_names.c (docs/port/MUSIC.md,
"Names").  Nothing it reads is written anywhere but the report.

Inputs (never committed):
  --elf   the PAL base ELF (baserom/pal/baseelf.elf): adpcmFile, the stream
          table (0x69 records of 0x40 bytes at 0x00559D50: path, loop start,
          sectors, pitch in Hz, channels; fumi/include/adpcm_init.h)
  --iso   the PAL disc image: DATA.DF's directory gives each .int file
  --album the folder of the album's FLAC files

Two passes:

1. Durations (python3 standard library only).  A stream lasts
   sectors * 2048 / channels / 16 * 28 / pitch seconds (16-byte SPU ADPCM
   blocks of 28 samples, channels interleaved in 0x800-byte chunks; the
   pitch word is Hz, port/audio/stream.c st_adpcm_pitch).  An album track's
   length is its FLAC STREAMINFO total samples / sample rate.

2. Features (--features; needs numpy and soundfile, which have wheels:
   `pip install numpy soundfile` in a scratch venv).  Each stream is decoded
   from the disc (the SPU ADPCM decoder below, pure Python) and each album
   track read with soundfile; both become 12-bin chroma and log energy per
   0.1 s.  For every (stream, track) pair the shorter slides over the longer
   (at least 90 % of it overlapping) and the score is the mean cosine of the
   chroma frames where the shorter one is within 30 dB of its peak.  A
   stream takes the best track's title when the best score is at least
   0.85, or at least 0.5 with a margin of 0.2 over the runner-up and a
   z-score (best minus the median over all tracks, over their standard
   deviation) of at least 2.8.  Everything else is unmatched.

  python3 tools/match_tracks.py --elf baserom/pal/baseelf.elf \\
      --iso baserom/Ico_PAL.iso --album "<album folder>" [--features] [--emit-c]
"""

import argparse
import os
import struct
import sys

ADPCM_VA = 0x00559D50
ADPCM_ROWS = 0x69
DATA_DF = "DFDATAS/DATA.DF"
HOP = 0.1


# --- the ELF's stream table --------------------------------------------------

def elf_segments(data):
    if data[:4] != b"\x7fELF" or data[4] != 1:
        sys.exit("match_tracks: not a 32-bit ELF")
    phoff, = struct.unpack_from("<I", data, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
    segs = []
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<5I", data, phoff + i * phentsize)
        if p_type == 1:
            segs.append((p_vaddr, p_filesz, p_offset))
    return segs


def elf_read(data, segs, va, n):
    for v, size, off in segs:
        if v <= va and va + n <= v + size:
            return data[off + va - v: off + va - v + n]
    sys.exit("match_tracks: 0x%x is not in the ELF" % va)


def stream_table(elf_path):
    data = open(elf_path, "rb").read()
    segs = elf_segments(data)
    rows = []
    for i in range(ADPCM_ROWS):
        rec = elf_read(data, segs, ADPCM_VA + i * 0x40, 0x40)
        path = rec[:48].split(b"\0")[0].decode("latin1")
        loop, sectors, pitch, channels = struct.unpack_from("<4i", rec, 48)
        rows.append((path, loop, sectors, pitch, channels))
    return rows


def stream_seconds(row):
    _, _, sectors, pitch, channels = row
    if pitch <= 0 or channels <= 0:
        return 0.0
    return sectors * 2048 / channels / 16 * 28 / pitch


# --- the disc ------------------------------------------------------------------

def iso_find(f, path):
    """The (offset, size) of a file in an ISO9660 image (no Joliet)."""
    f.seek(16 * 2048)
    pvd = f.read(2048)
    root = pvd[156:156 + 34]
    lba, size = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
    for part in path.split("/"):
        f.seek(lba * 2048)
        d = f.read(size)
        i, found = 0, None
        while i < len(d):
            n = d[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            name = d[i + 33:i + 33 + d[i + 32]].decode("latin1").split(";")[0]
            if name.upper() == part.upper():
                found = (struct.unpack_from("<I", d, i + 2)[0], struct.unpack_from("<I", d, i + 10)[0])
                break
            i += n
        if found is None:
            sys.exit("match_tracks: %s not on the disc" % path)
        lba, size = found
    return lba * 2048, size


def data_df_dir(f):
    base, _ = iso_find(f, DATA_DF)
    f.seek(base)
    count, = struct.unpack("<I", f.read(4))
    entries = {}
    for _ in range(count):
        e = f.read(40)
        name = e[:32].split(b"\0")[0].decode("latin1")
        off, size = struct.unpack_from("<2I", e, 32)
        entries.setdefault(name.lower(), (base + off, size))  # the first one, as cdvd.c's search
    return entries


_F0 = (0, 60, 115, 98, 122)
_F1 = (0, 0, -52, -55, -60)


def decode_int(f, off, row):
    """The stream as mono floats (the channels' mean): SPU ADPCM, 0x800-byte
    rows of 0x800/n bytes per channel."""
    _, _, sectors, _, n = row
    f.seek(off)
    raw = f.read(sectors * 2048)
    chunk = 0x800 // n
    hist = [[0, 0] for _ in range(n)]
    chans = [[] for _ in range(n)]
    for base in range(0, len(raw), 0x800):
        for c in range(n):
            h = hist[c]
            out = chans[c]
            h1, h2 = h
            for b in range(base + c * chunk, base + (c + 1) * chunk, 16):
                hdr = raw[b]
                shift = hdr & 15
                filt = (hdr >> 4) & 7
                if filt > 4:
                    filt = 0
                if shift > 12:
                    shift = 9
                f0, f1 = _F0[filt], _F1[filt]
                for k in range(2, 16):
                    byte = raw[b + k]
                    for nib in (byte & 15, byte >> 4):
                        s = ((nib << 12) - ((nib & 8) << 13)) >> shift
                        s += (h1 * f0 + h2 * f1 + 32) >> 6
                        if s > 32767:
                            s = 32767
                        elif s < -32768:
                            s = -32768
                        h2, h1 = h1, s
                        out.append(s)
            h[0], h[1] = h1, h2
    m = min(len(c) for c in chans)
    return [sum(c[i] for c in chans) / (32768.0 * n) for i in range(m)]


# --- FLAC STREAMINFO -------------------------------------------------------------

def flac_seconds(path):
    with open(path, "rb") as f:
        if f.read(4) != b"fLaC":
            return None
        while True:
            h = f.read(4)
            if len(h) < 4:
                return None
            last, kind, length = h[0] >> 7, h[0] & 0x7F, int.from_bytes(h[1:4], "big")
            body = f.read(length)
            if kind == 0:
                v = int.from_bytes(body[10:18], "big")
                rate = v >> 44
                total = v & ((1 << 36) - 1)
                return total / rate if rate else None
            if last:
                return None


def album_tracks(folder):
    names = sorted(x for x in os.listdir(folder) if x.lower().endswith(".flac"))
    out = []
    for n in names:
        title = os.path.splitext(n)[0]
        if ". " in title[:5]:
            title = title.split(". ", 1)[1]
        out.append((n, title, flac_seconds(os.path.join(folder, n))))
    return out


# --- features ------------------------------------------------------------------------

def features(np, x, sr):
    n = int(sr * 0.2)
    hop = int(sr * HOP)
    if len(x) < n:
        x = np.pad(x, (0, n - len(x)))
    frames = np.lib.stride_tricks.sliding_window_view(x, n)[::hop]
    spec = np.abs(np.fft.rfft(frames * np.hanning(n), axis=1)) ** 2
    freq = np.fft.rfftfreq(n, 1 / sr)
    band = (freq > 55) & (freq < 4000)
    pc = np.round(69 + 12 * np.log2(freq[band] / 440)).astype(int) % 12
    chroma = np.zeros((spec.shape[0], 12))
    for k in range(12):
        chroma[:, k] = spec[:, band][:, pc == k].sum(1)
    energy = np.log10(spec[:, (freq > 30) & (freq < 8000)].sum(1) + 1e-9)
    chroma = np.log1p(chroma / (chroma.sum(1, keepdims=True) + 1e-12) * 100)
    chroma -= chroma.mean(1, keepdims=True)
    chroma /= np.linalg.norm(chroma, axis=1, keepdims=True) + 1e-9
    return chroma, energy


def best_alignment(np, a, b):
    """(score, offset in frames of the shorter in the longer)."""
    (ca, ea), (cb, eb) = a, b
    if len(ca) > len(cb):
        ca, ea, cb, eb = cb, eb, ca, ea
    n, m = len(ca), len(cb)
    loud = ea > ea.max() - 3
    best = (-9.0, 0)
    for off in range(-int(n * 0.1), m - n + int(n * 0.1) + 1):
        lo, hi = max(0, -off), min(n, m - off)
        if hi - lo < n * 0.9:
            continue
        w = loud[lo:hi]
        if w.sum() < 5:
            continue
        c = (ca[lo:hi] * cb[lo + off:hi + off]).sum(1)
        s = float((c * w).sum() / w.sum())
        if s > best[0]:
            best = (s, off)
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--iso", required=True)
    ap.add_argument("--album", required=True)
    ap.add_argument("--features", action="store_true", help="the chroma pass (numpy, soundfile)")
    ap.add_argument("--emit-c", action="store_true", help="print track_names.c's rows")
    args = ap.parse_args()

    rows = stream_table(args.elf)
    album = album_tracks(args.album)
    iso = open(args.iso, "rb")
    ddir = data_df_dir(iso)

    print("# streams (adpcmFile): seconds from the table")
    on_disc = {}
    for i, row in enumerate(rows):
        if i == 0:
            continue
        base = row[0].split("/")[-1].lower()
        on_disc[i] = ddir.get(base)
        print("%3d %-36s %7.1f s %s" % (i, row[0], stream_seconds(row), "" if on_disc[i] else "(not on the disc)"))
    print("# album: seconds from STREAMINFO")
    for n, t, s in album:
        print("    %-44s %7.1f s" % (t, s or 0))
    if not args.features:
        return 0

    import numpy as np
    import soundfile as sf

    feats = {}
    for i, row in enumerate(rows):
        if 0 < i <= 100 and on_disc.get(i):
            x = np.array(decode_int(iso, on_disc[i][0], row), dtype=np.float32)
            feats[i] = features(np, x, row[3])
            print("decoded %d" % i, file=sys.stderr)
    afeats = []
    for n, t, s in album:
        x, sr = sf.read(os.path.join(args.album, n), dtype="float32")
        if x.ndim > 1:
            x = x.mean(1)
        while sr > 48000 and sr % 2 == 0:
            x = x[:len(x) // 2 * 2].reshape(-1, 2).mean(1)
            sr //= 2
        afeats.append(features(np, x, sr))
        print("read %s" % t, file=sys.stderr)

    print("# assignments: stream -> album track (score c, margin, z, offset)")
    chosen = []
    for i in sorted(feats):
        scores = [best_alignment(np, feats[i], af) for af in afeats]
        # the re-recordings are not in the game
        cand = [(s[0], k, s[1]) for k, s in enumerate(scores) if "Re-Recording" not in album[k][1]]
        cand.sort(reverse=True)
        cs = np.array([c[0] for c in cand])
        c, k, off = cand[0]
        margin = c - cand[1][0]
        z = (c - float(np.median(cs))) / (float(cs.std()) + 1e-9)
        ok = c >= 0.85 or (c >= 0.5 and margin >= 0.2 and z >= 2.8)
        verdict = album[k][1] if ok else "unmatched"
        print("%3d %-34s -> %-24s c %.2f margin %.2f z %.1f offset %+.1f s%s" %
              (i, rows[i][0].replace("sound/ICO_ADPCM/", ""), verdict, c, margin, z, off * HOP,
               "" if ok else "  (best: %s)" % album[k][1]))
        if ok:
            chosen.append((i, rows[i][0].replace("sound/ICO_ADPCM/", ""), album[k][1], c, margin, z))
    if args.emit_c:
        for i, asset, title, c, margin, z in chosen:
            print('    {ICO_TRACK_STREAM, %d, "%s", "%s"}, /* c %.2f, margin %.2f, z %.1f */' %
                  (i, asset, title.replace('"', '\\"'), c, margin, z))
    return 0


if __name__ == "__main__":
    sys.exit(main())
