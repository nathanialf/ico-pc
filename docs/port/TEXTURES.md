# Texture decodes off the game thread

How a game texture's decode leaves the game thread: the decode pool's workers, the job a store
makes, the points that wait for the jobs, why the result is the synchronous decode's byte for
byte, the queue's bound, the `[dev]` key, the log lines and the tests. The texture cache itself
(keys, formats, padding, updates, levels) is in [RENDER.md](RENDER.md#textures); this document
sits apart because RENDER.md is at its size cap.

## Files

| File | What it holds |
| --- | --- |
| `port/render/rd_tex.c` | the stores (`store`, `rdtex_store`, `rdtex_store_levels`), the copy of the caller's bytes (`copySource`), the decode a job runs (`rdtex__job_run`) and its result put into the texture (`rdtex__job_finish`) |
| `port/render/rd_tex_decode.h`, `rd_tex_decode.c` | the job (`RdTexJob`), the pool's size (`choose`), its workers (`workerMain`), the settle order (`rdtex__decode_add`) and the wait (`rdtex_settle`) |
| `port/render/rd_upload.c`, `rd_dump.c`, `rd_core.c` | the settles: before the ring estimate (`rd__texture_ring_bytes`), the uploads (`rd__upload_dirty_textures`), a frame dump (`rd__dump_frame`) and in `rd_shutdown`; the new texture's texels taken from the job (`rd__tex_take_pixels`) |
| `port/platform/ico_thread.h` | the threads, mutex, conditions and the hardware thread count (`ico_thread_cpu_count`), its own library `ico_thread` |

## A store

`rdtex_store` (and `rdtex_store_levels`) does at once everything a later call can see: the
format check `rdtex_decode` would make (`decodeCheck`), the cache entry, the choice between
re-expanding the entry's rd texture in place and making a new one (the source format follows from
the format and the TEXA mode, not from the texels), the retire of the old texture, the entry's
generation and sampler, and the counts (`RdTexCacheStats`). A new texture is made with zero
texels. The decode itself is a job (`RdTexJob`): the base's texels decoded and the TEXA applied,
then for `rdtex_store_levels` the further levels and their authored test (`levelsAuthored`).

With the pool on and fewer than `RDTEX_DECODE_QUEUE_MAX` (256) jobs waiting, the job takes a copy
of the images' bytes (the texels `rdtex_image_bytes` counts, the CLUT entries an index can reach,
a CLUT the levels share once) and joins the queue; the game may change or free its bytes as soon
as the call returns (the CLUT scroll; the recoloured CLUT on `texHostTexture`'s stack). Otherwise
(the pool off, the queue full, no memory for the copy) the job runs at once from the caller's
bytes. A job that ran at once is put into its texture at once when nothing waits, else it joins
the list behind the queued ones, so the order of the stores is kept.

## The settle

`rdtex_settle` decodes on the calling thread the queued jobs no worker has taken, waits for the
ones a worker is running, then calls `rdtex__job_finish` on every job in the stores' order: the
re-expansion through `rd_update_texture` (which keeps a texture whose texels did not change from
being uploaded again), or the new texture's texels replaced by the job's; then the levels as the
synchronous store set them (`rd__tex_set_levels` with none, then the authored levels) and the
`textures: "<name>" N mipmap levels, brightness ratio R: ...` line. A job whose texture is gone
by then (destroyed after two frame ticks, its slot made again under a new generation) puts
nothing anywhere.

Everything that reads a game texture's CPU texels settles first: the ring estimate and the
uploads at the start of every replay, the frame dump, `rd_shutdown` and `rdtex_reset`. The game's
recording never reads them, so the frame's commands, the cache's entries and the uploads are the
synchronous store's, and the alpha scan the upload keeps (`rd__tex_alpha_scan`) is taken from
the same texels. The lavapipe corpus is expected to be byte-identical with the pool on and off.
The decode and the authored test are integer and plain double arithmetic on the job's own
memory; the authored test's double ratios run in the worker's floating-point mode, not the
simulation's, which moves a ratio by an ulp at most against a threshold (1.8) that the disc's
textures miss by 0.28 or more.

## The pool's size

`[dev] texture_decode_threads` in `ico-pc.ini` (exported as `ICO_RD_TEXTURE_DECODE_THREADS`): `auto`,
the default, 2 workers, or 1 on a machine with 2 hardware threads or fewer; `0` the game thread
decodes every texture as before, the A/B; `1` to `4` that many. Unset without a window (the tests,
`rd_replay_tool`) it is 0. `rdtex_set_decode_threads` sets it for the rest of the run (the tests).
The workers start at the first queued job; a worker the system will not start leaves its share to
the others and to the settle.

## Log lines

- `rd: texture decodes: 2 worker threads (4 hardware threads)`, or `on the game thread ([dev]
  texture_decode_threads = 0)` or `on the game thread (no window)`: at start, again only when it
  changes.
- `rd: waited N ms for texture decodes (K on T worker threads since the last replay, the last
  "<name>"; W waits over 20 ms so far)`: a settle that waited more than 20 ms (the first 8, then
  every 100th).
- `rd: texture decodes: worker thread N of M could not start; ...`: once.
- The window's `window: the game step was late by M ms (loading: ...)` line names the decodes of a
  step the sound was held back in ([AUDIO.md](AUDIO.md#the-outputs-clock)).

## Tests

`rd_tex_pool` (`port/render/test/rd_tex_pool_test.c`): 200 textures of every format, CLUT form and
TEXA mode, one in five with two further levels, updates queued behind their creates, the caller's
bytes overwritten after each store: with 2 workers every texture's texels, source format, baked
TEXA, GS levels and alpha range equal the run with the pool off, and so do the cache's counts;
300 stores with 1 worker and no settle queue 256 and decode 44 on the game thread with the same
result; on a device the uploads' alpha scans agree (77 without one). `ico_thread` checks the
hardware thread count; `host_config` the key's values.
