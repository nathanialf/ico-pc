#!/usr/bin/env python3
"""port/data/test/make_raw_cd.py: a raw CD image (.bin, MODE2/2352) and the
.cue naming it, made from a 2048-byte-sector .iso, for the vfs_disc_jpn_bin
and vfs_disc_jpn_cue tests (port/data/CMakeLists.txt).

Each 2048-byte sector of the .iso becomes a 2352-byte mode 2 form 1 sector:
the 12-byte sync pattern (00, ten FF, 00), the header (the sector's address
as BCD minutes, seconds and frames, 150 frames after the start, then mode
2), the 8-byte subheader (00 00 08 00, twice: form 1 data), the 2048 user
bytes, and 280 zero bytes where the EDC and ECC go. port/data/iso9660.c
reads the user bytes at byte 24 and ignores the EDC and ECC, so zeros there
read the same; a dump of the disc has the real ones.

    make_raw_cd.py ISO OUT_DIR NAME

writes OUT_DIR/NAME.bin and OUT_DIR/NAME.cue (each through a .tmp and a
rename, so a second test never reads a half-written file), and skips both
when they are already there and newer than the .iso. Prints SKIPPED and
exits 0 when the .iso is absent.
"""
import os
import sys

SECTOR = 2048
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
SUBHEADER = b"\x00\x00\x08\x00" * 2
TAIL = b"\x00" * (2352 - 24 - SECTOR)
CHUNK = 512  # sectors per read


def bcd(v):
    return ((v // 10) << 4) | (v % 10)


def header(lba):
    f = lba + 150
    return bytes((bcd(f // (60 * 75)), bcd((f // 75) % 60), bcd(f % 75), 2))


def fresh(path, iso):
    return os.path.exists(path) and os.path.getmtime(path) >= os.path.getmtime(iso)


def main(argv):
    if len(argv) != 4:
        sys.stderr.write("usage: make_raw_cd.py ISO OUT_DIR NAME\n")
        return 2
    iso, out_dir, name = argv[1], argv[2], argv[3]
    if not os.path.exists(iso):
        print("make_raw_cd: SKIPPED: %s is absent" % iso)
        return 0
    size = os.path.getsize(iso)
    if size % SECTOR != 0:
        sys.stderr.write("make_raw_cd: %s is not a whole number of 2048-byte sectors\n" % iso)
        return 1
    os.makedirs(out_dir, exist_ok=True)
    bin_path = os.path.join(out_dir, name + ".bin")
    cue_path = os.path.join(out_dir, name + ".cue")
    if not (fresh(bin_path, iso) and os.path.getsize(bin_path) == size // SECTOR * 2352):
        tmp = bin_path + ".tmp"
        lba = 0
        with open(iso, "rb") as src, open(tmp, "wb") as dst:
            while True:
                data = src.read(SECTOR * CHUNK)
                if not data:
                    break
                out = bytearray()
                for i in range(0, len(data), SECTOR):
                    out += SYNC + header(lba) + SUBHEADER + data[i : i + SECTOR] + TAIL
                    lba += 1
                dst.write(out)
        os.replace(tmp, bin_path)
        print("make_raw_cd: wrote %s (%d sectors)" % (bin_path, lba))
    if not fresh(cue_path, iso):
        tmp = cue_path + ".tmp"
        with open(tmp, "w", newline="\r\n") as cue:
            cue.write('FILE "%s.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' % name)
        os.replace(tmp, cue_path)
        print("make_raw_cd: wrote %s" % cue_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
