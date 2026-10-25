# The June 2001 demo's VU1 programs against the retail ones

The demo (SCUS-97158, `ico_region` JUN26) ships its own VU1 text for `cluster`,
`mesh`, `normal_c` and `normal_l`; `particle` is the same text on every disc.
The decomp carries the demo's text as `VERSION_JUN26` blocks in
`ico2/vusrc/*.vsm` (decomp commit 9bd61a825; its own gate proves each text is
its ELF's). `tools/vu_variant_diff.py` runs cpp on each file with and without
`-DVERSION_JUN26`, normalises it (comments, blanks, spelling of numbers) and writes
`jun26_vs_pal.diff`; the ctest `vu1_variant_diff` regenerates it from the decomp
when `ICO_DECOMP_DIR` names one and compares it byte for byte. This table has
one row per hunk of that diff (`program #n` is the `@@` header's).

Classification: "scheduling only" is the same dataflow (the same values reach
the same output quadwords on every clip path) in a different order or with other
registers; "semantic" is a different value or a different set of writes.
The classification was made by reading each hunk against the source and then
by `tools/vu_variant_equiv.py`, a symbolic executor of the vertex loops (see
below), whose result is `jun26_vs_pal_equiv.txt` (ctest `vu1_variant_equiv`).

## Hunks

| Hunk | Routine | The instructions | Classification |
|---|---|---|---|
| cluster #1 | START_CLUSTER_1_SPEC, after NOCLIP_CLUSTER_1_SPEC | JUN26 ends the front-face path with `b NEXT_CLUSTER_1_SPEC` and puts the back-face path (`CLIP_CLUSTER_1_SPEC: iaddiu vi07, vi00, 3`, `sq.xyzw vf27, 2(vi03)`) above one loop tail | scheduling only: the loop tail is shared, not repeated |
| cluster #2 | the same | the tail PAL repeats after the back-face path (`add.xy vf24, vf24, vf25` / `ibne vi06, vi00, LOOP_CLUSTER_1_SPEC` / `iaddiu vi03, vi03, 3` / `xgkick vi01` / `b BEGIN_CLUSTER_1_SPEC`) is gone | scheduling only: the other half of cluster #1 |
| mesh #1 | START_MESH_LIGHT, loop head | `LOOP_MESH_LIGHT:` moves to the vertex loads (`lqi vf15, vf16, vf17` in place of `lq 0/1/2(vi02)` and `iaddiu vi02, vi02, 3`); `iaddi vi04, vi04, -1` moves to the head | scheduling only: the loop is no longer software-pipelined |
| mesh #2 | START_MESH_LIGHT, loop body | `add.xy vf17, vf17, vf23` (the UV offset of SET_UVOFFSET, mem[2].xy) leaves the prologue and sits in the loop beside `div q, vf00w, vf28w`; PAL's loop has no such add | semantic: the lit loop (code 22, no specular) adds the UV offset to every vertex; PAL adds it to the first vertex only, vertices 2..n keep the packet's ST. Nothing differs while the offset is zero |
| mesh #3 | START_MESH_LIGHT, loop tail | `div q, vf00w, vf28w` and `maxx.xyzw vf29, vf29, vf00x` (the light clamp, mesh.vsm:262's twin) leave the tail; `ibne vi04, vi00, LOOP_MESH_LIGHT` follows `iaddiu vi03, vi03, 3` | scheduling only: the `maxx` (clamp the light factors at 0) still runs on every vertex, at the loop head (shared line before the `#ifndef`); proven by the executor and by dropping that line in a mutation, which makes it differ |
| mesh #4 | START_MESH_LIGHT_SPEC, loop head | as mesh #1 | scheduling only |
| mesh #5 | START_MESH_LIGHT_SPEC, loop body | as mesh #2 and #3 for the specular loop; the I-bit line is spelled `mul[i].xyz vf22, vf18, vf29` / `loi 255` (255.0 = 0x437f0000, PAL's spelling); `mul.xyz vf29, vf29, vf29` and the `div` move | scheduling only: PAL's specular loop already has the UV add in the loop (mesh.vsm:400) |
| mesh #6 | START_MESH_LIGHT_SPEC, tail | `maxx.xyzw vf29, vf29, vf00x` leaves `iaddiu vi03, vi03, 3`'s line | scheduling only: the clamp is at the head, as mesh #3 |
| mesh #7 | START_MESH_NOLIGHT, loop | pipelined loop regrouped (`move.xyzw vf22, vf18` + `ftoi0.xyzw vf22, vf22` in place of `ftoi0 vf22, vf18`; `mulq.xyz vf28, vf28, q` in place of `move.w vf29, vf28` + `mulq.xyz vf29, vf28, q`) | scheduling only |
| normal_c #1 | START_NORMAL_C, prologue | PAL's `move.z vf23, vf00` (vf23.z = 0) and `add.xyz vf21, vf17, vf23` are `nop` and a later `add.xy vf17, vf17, vf23`; `iaddiu vi07, vi00, 0xd0` is the clip mask 208 of PAL | scheduling only: the clip mask is the same 0xd0 in both texts (the plan expected a new mask); the only value difference is the sign of a zero in the third lane of the ST quadword (PAL adds +0.0 to it) |
| normal_c #2 | START_NORMAL_C, loop | loop regrouped as mesh #1; the two exit paths (`xgkick vi01` / `b BEGIN_NORMAL_C`) become one `NEXT_NORMAL_C` tail with `ibne vi04, vi00, LOOP_NORMAL_C` and `iaddiu vi03, vi03, 3`; the clip test reads the same three `fmand` | scheduling only |
| normal_c #3 | START_NORMAL_C_NOCLIP | `vf23.z` handling as normal_c #1 | scheduling only |
| normal_c #4 | START_NORMAL_C_NOCLIP, loop | regrouped as normal_c #2 | scheduling only |
| normal_c #5 | START_NORMAL_C_NOCLIP, tail | the same tail rewrite | scheduling only |
| normal_l #1 | START_NORMAL_L, prologue | `iaddiu vi03, vi01, 1` in place of `iaddi vi03, vi01, -2` (the loop adds 3 at the tail instead of the head) | scheduling only: the same output quadwords |
| normal_l #2 | START_NORMAL_L, loop head | `div`, `iaddi vi04, vi04, -1` regrouped | scheduling only |
| normal_l #3 | START_NORMAL_L, loop body and tail | `maxx.xyzw vf29, vf29, vf00x` (the light clamp) moves earlier in the same iteration, `ftoi4` of both colours moves above the tail; one shared `NEXT_NORMAL_L` tail | scheduling only |

`particle` has no hunk. `normal_l`'s SPEC and REF loops, `cluster`'s other loops,
the scissor programs and every `SET_*` routine are identical in both texts: no
hunk lies in them.

Semantic differences: **1** (mesh #2). `VU1_JUN26_SEMANTIC_DIFFS` in
`port/render/vu1_ref/vu1_ref.h` is that count, and `tools/vu_variant_diff.py
--check-table` (ctest `vu1_variant_table`) holds the header, this table and the
diff in agreement.

## What the executor assumes

`tools/vu_variant_equiv.py` runs each START routine of both texts on the same
symbolic packet, matrices and constants for four vertices. Every clip test and
back-face flag forks (256 paths for the three-test loops). A float is the
expression that computes it (nothing reassociated; `x + 0` and `1 * x` reduce;
the sign of a zero is not tracked). It compares the output quadwords (as
expressions) and the `xgkick` address and contents. It assumes: both slots of a
line read the state before the line; a branch has one delay slot; `fmand` and
`fsand` read the flags of the latest FMAC op issued at least four lines
earlier (no stalls are modelled; `ftoi`/`itof` set none); `div` is visible at
the next `mulq`. A mutation of the demo's text (dropping a `maxx`, changing a
clip mask) is reported as a difference. Not covered: the cycle timing, the
MSCAL entry tables and the `SET_*` routines (identical text in both).
