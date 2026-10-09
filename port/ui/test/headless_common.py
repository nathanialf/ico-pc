"""port/ui/test/headless_common.py: what the headless-game tests share
(gallery_sweep.py, credits_headless.py).

  PadScript    the boot's presses from port/input/pad-boot.txt up to the
               title (BOOT_LAST), then presses from Main tick 560;
               to_extras() walks Settings > Extras
  fresh_work   a fresh work folder with a saves folder and a copy of ico_pc
  rms_after    the loudest 0.1 s of the WAV dump after an audio frame
"""

import array
import os
import shutil
import sys

# the opening demo's skip is pad-boot.txt's last press at or before this Main
# tick (the title shows at about 399: kanbanBoot's step 8 is at tick 318 in
# the log, the title 80 ticks on; it was 439 before the boot lost its stage
# reload and second card check)
BOOT_LAST = 400


class PadScript:
    def __init__(self, boot_src, start=560):
        self.lines = []
        with open(boot_src) as src:
            for ln in src:
                f = ln.split("#")[0].split()
                if f and f[0].isdigit() and int(f[0]) <= BOOT_LAST:
                    self.lines.append("%s %s" % (f[0], f[1]))
        self.t = start
        self.out = []

    def press(self, button, gap=15, at=None):
        """button (hex, port/input/pad_script.h) down for 3 ticks at `at`
        (default: the running tick), the next press `gap` ticks later"""
        if at is not None:
            self.t = at
        self.out.append("%d %s" % (self.t, button))
        self.out.append("%d 0000" % (self.t + 3))
        self.t += gap

    def to_extras(self):
        """Down to Settings, Cross, Down six times to Extras, Cross: the
        cursor on Extras' first row (Music)"""
        self.press("4000", 25)  # Settings
        self.press("0040", 45)  # open it
        for _ in range(6):
            self.press("4000")  # to Extras
        self.press("0040", 55)  # Extras

    def text(self):
        return "\n".join(self.lines + self.out) + "\n"


def fresh_work(exe, work):
    """work emptied, work/saves made, ico_pc copied in; the copy's path"""
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(os.path.join(work, "saves"))
    dst = os.path.join(work, os.path.basename(exe))
    shutil.copy2(exe, dst)
    return dst


def rms_after(wav, frame, seconds=4.0, win=4800):
    """The largest RMS of a 0.1 s window in [frame, frame + seconds) of the
    dump (16-bit stereo at 48 kHz, the left channel every fourth frame; read
    past its 44-byte header, which the run patches every second)."""
    try:
        f = open(wav, "rb")
    except OSError:
        return 0.0
    with f:
        f.seek(44 + frame * 4)
        data = f.read(int(seconds * 48000) * 4)
    a = array.array("h")
    a.frombytes(data[:len(data) // 2 * 2])
    if sys.byteorder != "little":
        a.byteswap()
    left = a[::8]  # every fourth frame's left sample
    step = win // 4
    best = 0.0
    for i in range(0, len(left) - step + 1, step):
        s = left[i:i + step]
        best = max(best, (sum(v * v for v in s) / len(s)) ** 0.5)
    return best
