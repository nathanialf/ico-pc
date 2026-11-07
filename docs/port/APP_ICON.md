# The application icon

Files: `port/platform/app_icon.c`, `app_icon.h` (paths, stamp, the wanted test, the atomic writes; the
header is the interface the capture and the window host use), `app_icon_image.c`, `app_icon_image.h`
(the image maths), `app_icon_ico.c` (the `.ico` writer and the `.tmp` + rename write), and
`app_icon_host.c` (the window build's wrappers: the user folder, the disc and the package label, the
PNG, the "icon changed" flag).

The program's icon is the title lettering's "I", made from the player's own disc and kept as files in
the user folder; nothing of it is in the repository or the download
([LEGAL.md](../LEGAL.md#what-is-not-in-this-repository-and-must-never-be-added)). The files are
`<user folder>/icon/` (`port/platform/app_icon.h`):

| file | holds |
| --- | --- |
| `ico-pc-<disc id>.png` | 256 x 256 RGBA, straight alpha, the image the window and the shortcuts use |
| `ico-pc-<disc id>.ico` | 16, 32 and 48 as 32-bit BGRA bottom-up BMP entries (a zero 1-bit mask) and 256 as a PNG entry; written on every platform, used on Windows |
| `ico-pc-<disc id>.txt` | the stamp: `ICO_VERSION_LABEL`, or `dev` for a build without a label |
| `ico-pc.png`, `ico-pc.ico` | copies of the current disc's pair, at names that do not change |

The disc id is the disc's (`ico_region_cur()->disc_id`, for example `SCES-50760`), so each disc keeps
its own icon. Every file is written as `<name>.tmp` and moved over the old one
(`ico_rename_replace`, as the extractor does), the stamp last, so the stamp only vouches for a
complete set. `ico_app_icon_wanted()` is 1 when the disc's PNG is missing or empty, or the stamp
differs from the package label; a build without a label (`dev`) regenerates only when the PNG is
missing. Each write logs `ico_pc: icon: written <path> (<disc id>, <label>)`; each refusal logs
`ico_pc: icon: refused ...` with the reason.

`ico_app_icon_deliver(a, b, w, h)` takes two captures of the same tick, the clear colour black (A)
and white (B), as RGBA8 with alpha 255 (`rd_capture_shot`'s form). The letter's coverage is the
difference between them: with B - A = 255 - alpha in every channel, alpha = 255 - max over the
channels of (B - A) and colour = A * 255 / alpha (`port/platform/app_icon_image.c`). The crop is the
box of alpha >= 8, made a square with a 10 % margin on each side, centred, kept inside the frame, and
resampled to 256 x 256 (an area average when shrinking, bilinear when growing, weighted by alpha). A
ghost check refuses a pair whose ink boxes (A's non-black pixels, B's non-white pixels) differ by
more than one pixel at any edge (the letter moved between the captures): the call answers
`ICO_APP_ICON_GHOST` and writes nothing. `ico_app_icon_deliver_single` is the fallback, a luma key
from one frame (alpha = the brightest channel), logged as such. The PNG is encoded in memory by
`rd_write_png_mem` (`port/render/rd_png.c`, the same bytes `rd_write_png` writes to a file); a
delivery raises a flag that `ico_app_icon_changed_take()` reads and clears.

The tests are `app_icon_image_test` (`app_icon_image`), `ico_icon_test` (`ico_icon`),
`app_icon_files_test` (`app_icon_files`) and `rd_png_mem_test` (`rd_png_mem`)
([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).
