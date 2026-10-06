#!/usr/bin/env python3
"""port/ui/test/credits_headless.py: Extras > Credits in the headless game.

ctest credits_headless.  Copies the headless ico_pc into a fresh folder with an
ico-pc.ini (unlock_credits=1, the developer key that unlocks the row) and a
pad script, and runs it on the disc image: the boot to the title, then
Settings > Extras > Credits.  The ending plays from the staff roll's first
scene to the title.  The log must show, in order:

  credits: enter                       the row started the playback
  credits: stage 60 up                 STAFF1 loaded
  staff roll: start ... (Extras > Credits)
  credits: the ending's song is playing
  staff roll: the port credit is posted (Extras > Credits)
  credits: back at the title           the flags put back, the flag cleared
  stage_no 62 -> 1                     the title's stage after the last scene

then 1,500 Main ticks of the title (the boy's records put back: without
them its camera fell through a null boy), exit code 0, and no "credits:
failed".  No save is written: the saves folder after the run holds
what a boot-only run of the same length leaves (nothing, on an empty card
folder), compared by name and content.

  credits_headless.py <ico_pc> <disc image> <work folder>
Exit 77 without the disc image.
"""

import hashlib
import os
import subprocess
import sys

from headless_common import PadScript, fresh_work

# the boot: port/input/pad-boot.txt's presses up to the opening demo's skip
# (the title shows at about Main tick 399), then Down to Settings, Cross, Down six
# times to Extras, Cross, Down twice to Credits, Cross (headless_common.py)
# the roll takes about 5,900 Main ticks from the STAFF1 stage; the title is
# back about 6,000 ticks after the Cross (Main tick 6,840), and the run goes
# on for 1,500 ticks of title
TICKS = 8400


def pad_script(src):
    pad = PadScript(src)
    pad.to_extras()
    pad.press("4000")          # Models
    pad.press("4000")          # Credits
    pad.press("0040")          # Credits (tick 815)
    # Triangle and Start during the roll: the playback has no skip, as the
    # real ending has none, so these must change nothing
    pad.press("0010", at=3000)
    pad.press("0800")
    return pad.text()


def listing(folder):
    out = {}
    for root, _, files in os.walk(folder):
        for name in files:
            p = os.path.join(root, name)
            with open(p, "rb") as f:
                out[os.path.relpath(p, folder)] = hashlib.sha256(f.read()).hexdigest()
    return out


def run(exe, iso, work, ticks, pad, unlock):
    exe_copy = fresh_work(exe, work)
    with open(os.path.join(work, "pad.txt"), "w") as f:
        f.write(pad)
    with open(os.path.join(work, "ico-pc.ini"), "w") as f:
        f.write("iso=%s\nsaves=%s\nticks=%d\ntrace=0\npad_script=%s\n%s" %
                (os.path.abspath(iso), os.path.join(work, "saves"), ticks,
                 os.path.join(work, "pad.txt"), "unlock_credits=1\n" if unlock else ""))
    r = subprocess.run([exe_copy], cwd=work,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=270)
    log = open(os.path.join(work, "logs", "ico-pc.log"), errors="replace").read()
    return r.returncode, log, listing(os.path.join(work, "saves"))


def main():
    exe, iso, work = sys.argv[1:4]
    if not os.path.isfile(iso):
        print("credits_headless: no disc image at %s; skipped" % iso)
        return 77
    src = os.path.dirname(os.path.abspath(__file__))
    root = os.path.normpath(os.path.join(src, "..", "..", ".."))
    pad = pad_script(os.path.join(root, "port", "input", "pad-boot.txt"))
    fails = []

    # the baseline: the same boot and presses, the row locked (no key, no
    # achievements file): the card folder a run leaves without a playback
    rc0, log0, saves0 = run(exe, iso, os.path.join(work, "locked"), 1200, pad, False)
    if rc0 != 0:
        fails.append("locked run: exit code %d" % rc0)
    if "credits: locked" not in log0:
        fails.append("locked run: no 'credits: locked'")
    if "credits: enter" in log0:
        fails.append("locked run: the playback started")

    rc, log, saves = run(exe, iso, os.path.join(work, "play"), TICKS, pad, True)
    if rc != 0:
        fails.append("exit code %d" % rc)
    lines = log.splitlines()
    for ln in lines:
        if "credits: failed" in ln:
            fails.append(ln)
    want = ["credits: enter",
            "credits: stage 60 up",
            "staff roll: start, 962 lines from the disc and 18 of the port's (Extras > Credits)",
            "credits: the ending's song is playing",
            "staff roll: the port credit is posted (Extras > Credits)",
            "credits: back at the title",
            "stage_no 62 -> 1"]
    at = 0
    for w in want:
        idx = [i for i, ln in enumerate(lines) if w in ln and i >= at]
        if not idx:
            fails.append("no '%s' (in order)" % w)
            continue
        print("%5d  %s" % (idx[0], lines[idx[0]].strip()[:150]))
        at = idx[0]
    for bad in ("stage_no 62 -> 39", "stage_no 39 ->", "achievements: unlocked"):
        if bad in log:
            fails.append("'%s' in the log" % bad)
    if saves != saves0:
        fails.append("the saves folder differs from the locked run's: %s vs %s" %
                     (sorted(saves), sorted(saves0)))
    for f in fails:
        print("FAIL:", f)
    print("credits_headless: %s" % ("ok" if not fails else "%d failure(s)" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
