#!/usr/bin/env python3
"""port/ui/test/gallery_headless.py: the music gallery in the headless game.

ctest gallery_headless (docs/port/TESTING.md, docs/port/MUSIC.md "Testing").
Copies the headless ico_pc into a fresh folder with an ico-pc.ini and a pad
script, and runs it on the disc image: the boot to the title, then Settings
> Extras > Music, where ICO_GALLERY_PLAY plays one entry of each group (a
soundtrack stream, a scene stream, an ambience, a hint voice, a com_v voice,
an effect from a bank the title does not hold) and Triangle leaves.  The
log must show every entry played ("gallery: playing <group> <key>") with
the engine's line for it, the title's banks and theme restored, and no
"gallery: failed"; the WAV dump must sound after every start.

  gallery_headless.py <ico_pc> <disc image> <work folder>
Exit 77 without the disc image.
"""

import os
import re
import shutil
import struct
import subprocess
import sys
import wave

PLAY = "stream:47,stream:87,env:61,stream:101,se:130,se:1167"
WANT = [("soundtrack", 47), ("scene", 87), ("ambience", 437), ("voice", 101), ("voice", 130),
        ("se", 1167)]
# the boot: port/input/pad-boot.txt's presses up to the opening demo's skip
# (the title shows at Main tick 439), then Down to Settings, Cross, Down six
# times to Extras, Cross, Cross on Music; Triangle after the script
BOOT_LAST = 440
SCRIPT_END = 785 + 50 + 200 * (len(WANT) + 1)
TICKS = SCRIPT_END + 150


def pad_script(src):
    lines = []
    for ln in open(src):
        f = ln.split("#")[0].split()
        if f and f[0].isdigit() and int(f[0]) <= BOOT_LAST:
            lines.append("%s %s" % (f[0], f[1]))
    t = 560
    out = []

    def press(b, gap=15):
        nonlocal t
        out.append("%d %s" % (t, b))
        out.append("%d 0000" % (t + 3))
        t += gap

    press("4000", 25)          # Settings
    press("0040", 45)          # open it
    for _ in range(6):
        press("4000")          # to Extras
    press("0040", 55)          # Extras
    press("0040")              # Music (tick 785)
    t = SCRIPT_END + 20
    press("0010")              # Triangle: back to Extras
    return "\n".join(lines + out) + "\n"


def rms_after(wav, frame, seconds=4.0, win=4800):
    """The largest RMS of a 0.1 s window in [frame, frame + seconds)."""
    with wave.open(wav, "rb") as w:
        n = w.getnframes()
        if frame >= n:
            return 0.0
        w.setpos(frame)
        data = w.readframes(min(int(seconds * 48000), n - frame))
    samples = struct.unpack("<%dh" % (len(data) // 2), data)
    best = 0.0
    for i in range(0, len(samples) - win * 2, win * 2):
        s = samples[i:i + win * 2]
        r = (sum(v * v for v in s[::4]) / (len(s) // 4)) ** 0.5
        best = max(best, r)
    return best


def main():
    exe, iso, work = sys.argv[1:4]
    if not os.path.isfile(iso):
        print("gallery_headless: no disc image at %s; skipped" % iso)
        return 77
    src = os.path.dirname(os.path.abspath(__file__))
    root = os.path.normpath(os.path.join(src, "..", "..", ".."))
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(os.path.join(work, "saves"))
    shutil.copy2(exe, os.path.join(work, os.path.basename(exe)))
    with open(os.path.join(work, "pad.txt"), "w") as f:
        f.write(pad_script(os.path.join(root, "port", "input", "pad-boot.txt")))
    with open(os.path.join(work, "ico-pc.ini"), "w") as f:
        f.write("iso=%s\nsaves=%s\nticks=%d\ntrace=0\npad_script=%s\naudio_dump=%s\n" %
                (os.path.abspath(iso), os.path.join(work, "saves"), TICKS,
                 os.path.join(work, "pad.txt"), os.path.join(work, "audio.wav")))
    env = dict(os.environ, ICO_GALLERY_PLAY=PLAY)
    r = subprocess.run([os.path.join(work, os.path.basename(exe))], cwd=work, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=280)
    log = open(os.path.join(work, "logs", "ico-pc.log"), errors="replace").read()
    fails = []
    if r.returncode != 0:
        fails.append("exit code %d" % r.returncode)
    for ln in log.splitlines():
        if "gallery: failed" in ln:
            fails.append(ln)
    lines = log.splitlines()
    for group, key in WANT:
        idx = [i for i, ln in enumerate(lines) if ln.startswith("gallery: playing %s %d " % (group, key))]
        if not idx:
            fails.append("no 'gallery: playing %s %d'" % (group, key))
            continue
        # the engine's line after it: a stream opened, or an effect keyed
        frame = None
        for ln in lines[idx[0] + 1:idx[0] + 12]:
            m = re.search(r"(?:stream %d .*opened|effect %d from bank) .*at audio frame (\d+)" % (key, key), ln)
            if m:
                frame = int(m.group(1))
                break
        if frame is None:
            fails.append("%s %d: no engine line (stream opened / effect keyed)" % (group, key))
            continue
        level = rms_after(os.path.join(work, "audio.wav"), frame)
        print("%-10s %5d: started at audio frame %d, peak RMS %.0f" % (group, key, frame, level))
        if level < 30:
            fails.append("%s %d: silent after frame %d (RMS %.0f)" % (group, key, frame, level))
    for want in ("gallery: the title theme has faded out", "gallery: script done",
                 "gallery: the title's stage banks are back",
                 "gallery: the title theme is requested again"):
        if want not in log:
            fails.append("no '%s'" % want)
    for f in fails:
        print("FAIL:", f)
    print("gallery_headless: %s" % ("ok" if not fails else "%d failure(s)" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
