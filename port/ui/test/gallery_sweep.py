#!/usr/bin/env python3
"""port/ui/test/gallery_sweep.py: every stream and a sample of every bank's
effects through the music gallery, in one headless run.

ctest gallery_sweep.
Copies the headless ico_pc into a fresh folder with an ico-pc.ini and a pad
script and runs it on the disc image: the boot to the title, Settings >
Extras > Music, then the ICO_GALLERY_PLAY script:

  stream:47            a fixed 8 s step, during which the pad presses R1
                       (the next entry plays) and L1 (the previous one)
  dwell:95             from here each entry lasts until it has ended, or
                       95 s (a longer piece is cut there)
  stream:1 .. 104      every stream (2 to 5, the e3/ ones, are not on the
                       disc and not on the list)
  dwell:10, bank:K.J   the first, middle and last effect of every bank
  leave:0              the page leaves, as on Triangle

and reads the log: per item its open (the engine's line), its position
every second, its end line (or the cut), any wrap, any failure, and the WAV
dump's level after its start.  It prints the table of the streams and the
effects, and fails when a listed stream does not play, does not reach its
end (within END_TOL of its total; a piece longer than the dwell must have
moved at the audio clock's rate to its cut), plays past its end or wraps,
when a listed total is not the file's (the disc file is the table's sectors
plus the 0x5C000-byte ring pad), when R1 or L1 does not start the next
entry, when an effect is not keyed or its bar wraps (a looping sample plays
one pass), when the title's banks and theme are
not restored, or on any "gallery: failed".

  gallery_sweep.py <ico_pc> <disc image> <work folder> [--report FILE]
Exit 77 without the disc image.
"""

import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # works under python -I too
from headless_common import PadScript, fresh_work, rms_after  # noqa: E402

DWELL = 95          # seconds a stream may play (longer ones are cut there)
SE_DWELL = 10       # an effect's
BANKS = 68          # the sound effects' bank sections on the PAL disc
NOT_ON_DISC = {2, 3, 4, 5}
END_TOL = 0.35      # seconds the end line may fall short of the total
RING_PAD = 0x5C000  # the bytes each .int carries past its table's sectors
PAGE_OPEN = 785     # Main tick of the Cross on Music
FIRST = PAGE_OPEN + 50
# the video mode the run is given (config.toml [video] video_mode) and its Main
# ticks a second, ((60 - 10 * PAL) / 2): 25 at "pal50", 30 at "60hz"
VIDEO_MODE = "60hz"
TPS = {"pal50": 25, "60hz": 30}[VIDEO_MODE]
# one entry of each group the streams and banks above do not reach: an
# ambience, a com_v voice, an effect from a bank the title does not hold;
# with the streams' 47 (soundtrack), 87 (scene) and 101 (voice), every group
# is checked to play under its group's name, with its engine line and sound
GROUP_PLAY = ["env:61", "se:130", "se:1167"]
GROUPS = [("soundtrack", 47), ("scene", 87), ("voice", 101), ("ambience", 437), ("voice", 130),
          ("se", 1167)]


def pad_script(src):
    pad = PadScript(src)
    pad.to_extras()
    pad.press("0040")                         # Music (tick 785)
    pad.press("0008", at=FIRST + 3 * TPS)     # R1 while stream 47 plays
    pad.press("0004", at=FIRST + 6 * TPS)     # L1 while the next one plays
    return pad.text(), FIRST + 3 * TPS, FIRST + 6 * TPS


class Item:
    def __init__(self, group, key, label, line):
        self.group, self.key, self.label, self.line = group, key, label, line
        self.kind = None          # stream / effect
        self.info = None          # (path, sectors, bytes, seconds, loop, hz, ch, disc)
        self.frame = None         # the engine's open / key frame
        self.positions = []       # (elapsed, total)
        self.end = None           # elapsed at the end line
        self.cut = None           # elapsed at the cut
        self.wraps = []
        self.fails = []
        self.notes = []
        self.level = 0.0
        self.closed = None        # the engine's close line
        self.onepass = None       # an effect's "stopped after one pass" (its sample's seconds)


def parse(log):
    items, current = [], None
    misc_fails, absent, blank = [], [], {}
    for n, ln in enumerate(log.splitlines()):
        m = re.match(r"gallery: playing (\w+) (\d+) \((.*)\)$", ln)
        if m:
            current = Item(m.group(1), int(m.group(2)), m.group(3), n)
            items.append(current)
            continue
        m = re.match(r"gallery: failed (\w+) (\d+): no such entry in the list", ln)
        if m:
            absent.append((m.group(1), int(m.group(2))))
            continue
        m = re.match(r"gallery: stream (\d+) \(.*\): the disc's file has an end block at byte "
                     r"0x([0-9A-F]+) \(blank from there\): ([\d.]+) s", ln)
        if m:
            blank[int(m.group(1))] = (int(m.group(2), 16), float(m.group(3)))
            continue
        if current is None:
            if "gallery: failed" in ln:
                misc_fails.append(ln)
            continue
        m = re.match(r"gallery: stream (\d+) file (\S+): (\d+) sectors \((\d+) bytes, ([\d.]+) s\), "
                     r"loop start (\d+), (\d+) Hz, (\d+) channels; the disc's file (-?\d+) bytes", ln)
        if m and int(m.group(1)) == current.key:
            current.kind = "stream"
            current.info = (m.group(2), int(m.group(3)), int(m.group(4)), float(m.group(5)),
                            int(m.group(6)), int(m.group(7)), int(m.group(8)), int(m.group(9)))
            continue
        m = re.search(r"gallery: (?:stream (\d+) .*opened|effect (\d+) from bank) .*at audio frame (\d+)", ln)
        if m and int(m.group(1) or m.group(2)) == current.key and current.frame is None:
            current.kind = current.kind or ("stream" if m.group(1) else "effect")
            current.frame = int(m.group(3))
            continue
        m = re.match(r"gallery: (stream|effect) (\d+) at ([\d.]+) s of ([\d.]+) s", ln)
        if m and int(m.group(2)) == current.key:
            current.positions.append((float(m.group(3)), float(m.group(4))))
            continue
        m = re.match(r"gallery: (stream|effect) (\d+) ended at ([\d.]+) s of ([\d.]+) s", ln)
        if m and int(m.group(2)) == current.key:
            current.end = float(m.group(3))
            current.positions.append((float(m.group(3)), float(m.group(4))))
            continue
        m = re.match(r"gallery: (stream|effect) (\d+) cut at ([\d.]+) s of ([\d.]+) s", ln)
        if m and int(m.group(2)) == current.key:
            current.cut = float(m.group(3))
            continue
        m = re.match(r"gallery: (stream|effect) (\d+) wrapped at ([\d.]+) s to ([\d.]+) s", ln)
        if m and int(m.group(2)) == current.key:
            current.wraps.append((float(m.group(3)), float(m.group(4))))
            continue
        m = re.match(r"gallery: effect (\d+) stopped after one pass of its looping sample \(([\d.]+) s\)", ln)
        if m and int(m.group(1)) == current.key:
            current.onepass = float(m.group(2))
            continue
        m = re.match(r"gallery: stream (\d+) closed at its end: (\d+) of (\d+) bytes played", ln)
        if m and int(m.group(1)) == current.key:
            current.closed = (int(m.group(2)), int(m.group(3)))
            continue
        if "gallery: failed" in ln:
            current.fails.append(ln[len("gallery: "):])
    return items, misc_fails, absent, blank


def mmss(s):
    s = int(s)
    return "%d:%02d" % (s // 60, s % 60)


def judge_stream(it, dwell, blank, fixed):
    """(played, looped, observed, problems); fixed: the engine closes the
    stream itself at the end of its pass (the 'closed at its end' line)"""
    probs = list(it.fails)
    total = it.positions[-1][1] if it.positions else None
    if it.info:
        path, sectors, nbytes, secs, loop, hz, ch, disc = it.info
        if disc >= 0 and disc != nbytes + RING_PAD:
            probs.append("the disc's file is %d bytes, not the table's %d + the ring pad" % (disc, nbytes))
        want = blank[it.key][1] if it.key in blank else secs
        if total is not None and abs(total - want) > 0.05:
            probs.append("total shown %.1f s, the file's %.1f s" % (total, want))
    played = it.frame is not None and it.level >= 30
    if it.frame is None:
        probs.append("never opened")
    elif it.level < 30:
        probs.append("silent after its open (RMS %.0f)" % it.level)
    looped = bool(it.wraps)
    if it.wraps:
        probs.append("wrapped %s" % ", ".join("%.1f->%.1f" % w for w in it.wraps))
    els = [p[0] for p in it.positions]
    if any(b + 0.05 < a for a, b in zip(els, els[1:])):
        looped = True
        if not it.wraps:
            probs.append("position went back")
    if total is not None and els and max(els) > total + 0.05:
        probs.append("position %.1f s past the total" % max(els))
    if it.end is not None:
        observed = it.end
        if total is not None and it.end < total - END_TOL:
            probs.append("ended at %.1f s, %.1f s before its total" % (it.end, total - it.end))
        if fixed and it.closed is None:
            probs.append("closed by something else than the gallery's end")
    elif it.cut is not None:
        observed = it.cut
        if total is not None and total + 2 < dwell:
            probs.append("still sounding at %.1f s of %.1f s after the dwell" % (it.cut, total))
        # the curve: one second a second (the log line is once a second)
        rising = [e for e in els if e > 0.0]
        if len(rising) > 10:
            rate = (rising[-1] - rising[0]) / (len(rising) - 1)
            if not 0.95 <= rate <= 1.05:
                probs.append("position moved %.2f s a second" % rate)
        else:
            probs.append("no position curve before the cut")
    else:
        observed = els[-1] if els else None
        if it.frame is not None:
            probs.append("no end line")
    return played, looped, observed, probs


def main():
    ap = argparse.ArgumentParser(description="The gallery's every entry in one headless run.")
    ap.add_argument("exe", help="the headless ico_pc")
    ap.add_argument("iso", help="the disc image")
    ap.add_argument("work", help="the work folder")
    ap.add_argument("--report", help="write the table (Markdown) here")
    ap.add_argument("--reread", action="store_true",
                    help="judge a finished run's folder again, no run")
    ap.add_argument("--play",
                    help="a developer's run of these entries only (comma list; no checks)")
    args = ap.parse_args()
    exe, iso, work, report, reread = args.exe, args.iso, args.work, args.report, args.reread
    only = args.play.split(",") if args.play is not None else None
    if not os.path.isfile(iso):
        print("gallery_sweep: no disc image at %s; skipped" % iso)
        return 77
    if reread:
        return judge(work, True, 0, report, keep=True)
    src = os.path.dirname(os.path.abspath(__file__))
    root = os.path.normpath(os.path.join(src, "..", "..", ".."))
    exe_copy = fresh_work(exe, work)
    pad, _, _ = pad_script(os.path.join(root, "port", "input", "pad-boot.txt"))
    with open(os.path.join(work, "pad.txt"), "w") as f:
        f.write(pad)
    play = ["stream:47", "dwell:%d" % DWELL]
    play += ["stream:%d" % n for n in range(1, 105)]
    play += ["dwell:%d" % SE_DWELL]
    play += ["bank:%d.%d" % (k, j) for k in range(BANKS) for j in range(3)]
    play += GROUP_PLAY
    play += ["leave:0"]
    if only:
        play = ["stream:47", "dwell:%d" % DWELL] + only + ["leave:0"]
    # an upper bound: every stream its dwell and every effect its dwell, with
    # the opening's seconds; the run is stopped once the page has left
    ticks = FIRST + 8 * TPS + (104 * (DWELL + 4) + (3 * BANKS + len(GROUP_PLAY)) * (SE_DWELL + 3) +
                               60) * TPS
    with open(os.path.join(work, "config.toml"), "w") as f:
        f.write('[video]\nvideo_mode = "%s"\n' % VIDEO_MODE)
    with open(os.path.join(work, "ico-pc.ini"), "w") as f:
        f.write("iso=%s\nsaves=%s\nticks=%d\ntrace=0\nwatchdog=120\npad_script=%s\naudio_dump=%s\n" %
                (os.path.abspath(iso), os.path.join(work, "saves"), ticks,
                 os.path.join(work, "pad.txt"), os.path.join(work, "audio.wav")))
    env = dict(os.environ, ICO_GALLERY_PLAY=",".join(play))
    logp = os.path.join(work, "logs", "ico-pc.log")
    t0 = time.time()
    proc = subprocess.Popen([exe_copy], cwd=work, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    stopped = False
    seen = None
    while proc.poll() is None:
        time.sleep(2)
        try:
            with open(logp, errors="replace") as f:
                txt = f.read()
        except OSError:
            continue
        if "gallery: the title theme is requested again" in txt and seen is None:
            seen = time.time()
        if seen is not None and time.time() - seen > 10:
            proc.terminate()
            proc.wait(30)
            stopped = True
    rc = proc.returncode
    print("gallery_sweep: the run took %.0f s%s" % (time.time() - t0,
                                                     ", stopped after the page left" if stopped else ""))
    return judge(work, stopped, rc, report, keep=False)


def judge(work, stopped, rc, report, keep):
    """Reads the run's log and dump; prints the tables; 0 or 1."""
    logp = os.path.join(work, "logs", "ico-pc.log")
    with open(logp, errors="replace") as f:
        log = f.read()
    items, misc, absent, blank = parse(log)
    wav = os.path.join(work, "audio.wav")
    for it in items:
        if it.frame is not None:
            heard = [p[0] for p in it.positions] + [it.end or 0.0, it.cut or 0.0]
            it.level = rms_after(wav, it.frame, min(max(heard) + 0.5, DWELL + 1.0))
    fails = []
    if not stopped and rc != 0:
        fails.append("exit code %d" % rc)
    fails += misc

    # R1 and L1: the first three plays are 47, then R1's and L1's entries
    if len(items) < 3 or items[0].key != 47:
        fails.append("the opening stream 47 did not play")
    else:
        for name, it in (("R1", items[1]), ("L1", items[2])):
            if it.frame is None:
                fails.append("%s: entry %s %d did not open" % (name, it.group, it.key))
            print("%s: played %s %d (%s), opened at frame %s" % (name, it.group, it.key, it.label, it.frame))
    sweep = items[3:]
    # the engine of this change logs its own end; an older one does not
    fixed = "closed at its end" in log
    banks = sorted({k for kind, k in absent if kind == "bank"})
    nbanks = banks[0] if banks else BANKS
    print("bank sections played: %d" % nbanks)
    if banks and banks != list(range(nbanks, BANKS)):
        fails.append("bank sections missing below the last: %s" % banks)
    streams = {it.key: it for it in sweep if it.kind == "stream"}
    rows = []
    for n in range(1, 105):
        if n in NOT_ON_DISC:
            rows.append("| %d | e3/ | - | - | - | - | not on the disc, not listed |" % n)
            if ("stream", n) not in absent:
                fails.append("stream %d: expected 'no such entry'" % n)
            continue
        it = streams.get(n)
        if it is None:
            fails.append("stream %d: never played" % n)
            rows.append("| %d | ? | ? | ? | no | ? | not played |" % n)
            continue
        played, looped, observed, probs = judge_stream(it, DWELL, blank, fixed)
        path = it.info[0].replace("sound/ICO_ADPCM/", "") if it.info else "?"
        total = "%s (%.1f s)" % (mmss(it.info[3]), it.info[3]) if it.info else "?"
        obs = "-" if observed is None else ("%.1f s %s" % (observed, "end" if it.end is not None else "cut"))
        note = "; ".join(probs) if probs else ("ok" if it.end is not None else "ok (cut at the dwell)")
        if it.info and it.info[4]:
            note += " (loop start sector %d)" % it.info[4]
        if n in blank:
            note += " (the disc's file is blank from byte 0x%X: %.1f s of %.1f s play)" % (
                blank[n][0], blank[n][1], it.info[3] if it.info else 0.0)
        rows.append("| %d | %s | %s | %s | %s | %s | %s |" % (n, path, total, obs, "yes" if played else "NO",
                                                          "YES" if looped else "no", note))
        for p in probs:
            fails.append("stream %d (%s): %s" % (n, path, p))
    effects = [it for it in sweep if it.kind != "stream"]
    erows = []
    for it in effects:
        ok = it.frame is not None
        tot = it.positions[-1][1] if it.positions else 0.0
        obs = it.end if it.end is not None else it.cut
        note = "; ".join(it.fails) if it.fails else ""
        if not ok:
            fails.append("effect %d (%s): never keyed %s" % (it.key, it.label, note))
        # one pass: a looping sample stops after its
        # length, so an effect's bar never wraps, and one stopped so ends
        # rather than being cut at the dwell when it is shorter
        if it.wraps:
            fails.append("effect %d (%s): wrapped %s" % (it.key, it.label, ", ".join(
                "%.1f->%.1f" % w for w in it.wraps)))
        if it.onepass is not None and it.onepass + END_TOL < SE_DWELL and it.cut is not None:
            fails.append("effect %d (%s): stopped after one pass (%.2f s) but cut at the dwell" % (
                it.key, it.label, it.onepass))
        if it.onepass is not None and not note:
            note = "one pass of a looping sample"
        erows.append("| %d | %s | %s | %s | %s | %s |" % (it.key, it.label, "%.2f s" % tot if tot else "-:--",
                                                         "-" if obs is None else "%.1f s %s" % (obs, "end" if it.end is not None else "cut"),
                                                         "yes" if ok else "NO", note or ("loops" if it.cut is not None else "")))
        fails += ["effect %d: %s" % (it.key, f) for f in it.fails]
    for group, key in GROUPS:
        hit = [it for it in items if it.group == group and it.key == key]
        if not hit:
            fails.append("no 'gallery: playing %s %d'" % (group, key))
        elif hit[-1].frame is None:
            fails.append("%s %d: no engine line (stream opened / effect keyed)" % (group, key))
        elif hit[-1].level < 30:
            fails.append("%s %d: silent after frame %d (RMS %.0f)" % (group, key, hit[-1].frame,
                                                                    hit[-1].level))
        else:
            print("%-10s %5d: started at audio frame %d, peak RMS %.0f" % (group, key, hit[-1].frame,
                                                                          hit[-1].level))
    for want in ("gallery: the title theme has faded out", "gallery: script done",
                 "gallery: the title's stage banks are back",
                 "gallery: the title theme is requested again"):
        if want not in log:
            fails.append("no '%s'" % want)
    out = ["| stream | file | listed total | observed end / last position | played | looped | note |",
           "| --- | --- | --- | --- | --- | --- | --- |"] + rows
    out += ["", "| effect | name | sample | observed | keyed | note |", "| --- | --- | --- | --- | --- | --- |"]
    out += erows
    text = "\n".join(out) + "\n"
    print(text)
    if report:
        with open(report, "w") as f:
            f.write(text)
    for f in fails:
        print("FAIL:", f)
    print("gallery_sweep: %s" % ("ok" if not fails else "%d failure(s)" % len(fails)))
    if not fails and not keep:
        os.remove(wav)  # about 750 MB; kept when something failed
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
