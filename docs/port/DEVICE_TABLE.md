# The device table

One list of hardware rows (`port/rhi/rhi_device_table.c`, `rhi_device_table.h`). A row matches a
device's identity and sets renderer-internal technical choices: a path, a workaround, an attempt on
or off. It never sets a player setting: no Options item, resolution, aspect, frame rate or effect
is a choice. Capabilities (what the device offers) and self-tests (what a start measures) decide
everything else at their call sites, and the `[dev]` keys and the tests' environment overrides
stay there too.

## Files

| File | What it holds |
| --- | --- |
| `port/rhi/rhi_device_table.h` | The identity, the choices and their values, the row and setting structs, the evaluation, the start-up lines and the summary clause. |
| `port/rhi/rhi_device_table.c` | The rows, the matcher, the defaults, the tests' fakes, the lines. Pure C: no Vulkan, SDL, I/O or environment. |
| `port/rhi/rhi_device_table.cmake` | The library `ico_device_table` (linked PUBLIC by `ico_rhi`, every backend library, `ico_platform` and `ico_thread`, which holds the attempt ledger) and its three tests; the pure predicates' tests link it too. |
| `port/rhi/vk/vk_device_table.c` | The Vulkan backend's one evaluation of the picked device with the tests' fakes (`g_vkr.devTable`), and its start-up lines. |
| `port/rhi/gl/gl_device.c`, `d3d11/d3d11_device.c`, `d3d12/d3d12_device.c` | The other backends' evaluation (no fakes) and start-up lines, after their device line. |
| `port/platform/memory_floor.c`, `present_fallback.c`, `window_present_fallback.c`, `android/gpu_driver_android.c`, `attempt_ledger.c` | The platform's reads: the free-memory floor's driver class, the present fallback's drivers, FIFO from the first swapchain, the Android build's stock driver hint, and the attempt ledger's words where no row holds. |
| `port/render/rd_device_rows.c`, `rd_device_rows.h` | The renderer's reads: `rd__device_value` (in `rd_profile.c`) gives a choice of the current device, or with no device the backend's defaults; the renderer's pure predicates that keep their signatures. |
| `port/rhi/test/rhi_device_table_test.c` | The table's own rules, the lines of five devices, and the list below (`--docs`, `--check-docs`). |
| `port/rhi/test/rhi_device_table_equiv_test.c`, `rhi_device_table_ref.inc` | The proof that the table changes no behaviour (below). |

## The identity

What a row can match (`RhiDeviceId`): the backend, the build's OS (`__ANDROID__`, `ICO_IOS`,
`_WIN32`, `__APPLE__`, else Linux), MoltenVK (`RhiLimits.moltenvk`, the tests'
`ICO_VK_FAKE_LIMITS=moltenvk` included), a driver package the Android build loaded, the PCI vendor
and device ids, the Vulkan driver id, the device's Vulkan version, the device name and
`RhiLimits.driverText`. A row may also match on four derived facts, which are choices resolved
before the choices it sets: `TILER`, `TILE_PHONE`, `PROFILE_CLASS` and `TILE_CLASS`.

No row is keyed on a phone model or SoC. Two rows are keyed on a GPU name (`mali-g52`,
`powervr-dxt-48-1536`), and two on the OS alone (`android`, `ios`), as the rules they replace are.

"Qualcomm's own driver" is tested three ways, each kept as it is: by its id (vendor 0x5143, driver
id 8 or 0), by its text (vendor 0x5143, driver id not 18, neither Mesa nor Turnip in the driver
text), and by the vendor alone (`qualcomm-vendor`, `adreno`). The Turnip test (driver id 18, or
Turnip in the text) has no vendor test. They differ only on identities no log has shown (a driver
id of 0 with Turnip or Mesa in the text, the vendor through MoltenVK); unifying them is a change of
behaviour for those, left for a later release with its expected differences.

The profile's tile-based columns follow the profile class, while `TILER` follows the vendor list
and Android. They differ under MoltenVK on an Intel or AMD Mac: the class is Apple (tile-based
columns on), `TILER` is off (the desktop's MSAA, late pass, cull and fog order). That split is
kept.

## Resolving a choice

Every choice is resolved in its order (`RhiDeviceChoice`):

1. The matching rows that set it are collected.
2. If they all give the same value, there is no conflict; the first in table order is named the
   setter.
3. Otherwise the row that lists every other one of a different value in its `overrides` wins
   (`adreno-740-qualcomm` over `turnip`, `turnip-adreno` over `adreno`, `powervr-dxt-48-1536` over
   `powervr`, every class row over `other-tiler`).
4. Two matching rows of different values without a link are an error of the table:
   `rhi_device_table_equiv` fails on any identity of its matrix where that happens, naming both.

A row is in force when one of its settings is the value the choice took; a row that matched with
every setting overridden (`other-tiler` beside a class row) is named apart in the start-up line.

With no row, a choice keeps its default (the list below), which is today's behaviour on a device
without a row.

## The tests' fakes

The table reads no environment. The backend hands it the fakes it reads (`RhiDeviceFakes`, parsed
by `rhi_device_fakes_parse` as each call site parsed its variable before the table), each with its
exact scope: `ICO_VK_FAKE_VENDOR` (the tile-based phone, and the subpass class's Arm and Qualcomm
parts, not over the Intel row), `ICO_VK_FAKE_PASS_ROW=qualcomm`, `ICO_VK_FAKE_ADRENO`,
`ICO_VK_FAKE_SERIAL`, `ICO_VK_FAKE_NO_PREPASS` (a row's own words replace it) and
`ICO_RD_VU_COMPACT_ARM` (the Mali-G52's compaction class and its default). The Vulkan backend
reads the first five once, when the device is picked (`vk_device_table.c`); `ICO_RD_VU_COMPACT_ARM`
is the renderer's, read where the compaction decides (`port/render/rd_vu_compact.c`). The other
backends have no fakes. The start-up lines come from that one evaluation: a row's line says
"replaced by <variable>, a test" where a fake changed its value.

## The start-up lines and the summary

At device creation every backend evaluates the table once (`rhi_device_table_start`: the current
device's choices, `rhi_device_table_current`) and prints, after its device line:

```
rhi_vk: device table: <name> (device vvvv:dddd; driver ID n, version v; Vulkan a.b.c; <OS>): N of the table's M rows: <ids>[; overridden here: <ids>]
rhi_vk: device table: <id> (<what it matches>): <what it sets> (<issue>, <since>; revisit: <condition>); ...
```

one line for each row in force, or the header alone with "none of the table's M rows: every choice
as capability and self-tests decide". OpenGL, Direct3D 11 and Direct3D 12 print the same lines
with their own prefix (`rhi_gl:`, `rhi_d3d11:`, `rhi_d3d12:`) and no driver ID in the header
("(device 10de:1380; D3D11; Windows)"). No row is keyed on them; Direct3D 12's vendor id is 0, so
its header names `vendor-unknown` (the free-memory floor's cautious rule), and a GL driver whose
strings name no vendor does the same.

The header is the line that names the rows; it is a key line of a report's `report.txt`
(`port/platform/report.c`). The renderer's summary line carries their count alone, "device table
rows: 13 of 27 (the start lines name them)" or "device table rows: none of 27": the summary holds
at most 2047 bytes (`diag_host.c` `SUMMARY_MAX_BYTES`), issue 123's tablet filled 1810 of them in
v0.11.6, and the clause of ids would have taken 210 more. `rhi_device_table` builds the longest
summary (that line, the longest GPU name and driver text of every logged device, the clause with
every row in force) and fails with less than 128 bytes left. Example on the Mali-G52 MC2 of issue
179:

```
rhi_vk: device table: Mali-G52 MC2 (device 13b5:74021000; driver ID 9, version 32.1.0; Vulkan 1.1.177; Android): 8 of the table's 27 rows: tile-vendors, android, tiler, tile-phone, arm, mali, mali-g52, tile-classes; overridden here: other-tiler
rhi_vk: device table: mali-g52 (vendor 0x13B5, not MoltenVK, a device name with "Mali-G52"): the index compaction's Mali-G52 class (issue 179, ...); the index compaction off by default (issue 179, ...)
```

## Who reads the table

The Vulkan backend reads its choices from its one evaluation (`g_vkr.devTable`), through the
library's call-site reads (`rhi_device_choice_why`, `rhi_device_pass_rows`,
`rhi_device_subpass_class`, `rhi_device_vu_rows`), which the equivalence test calls too:

| Choice | Call site (`port/rhi/vk/`) |
| --- | --- |
| `TILER` | `vk_device.c` `vkr_fill_limits` (`RhiLimits.tiler`) |
| `TILE_PHONE` | `vk_device.c` `vkr_tile_rule_apply` (the barrier scopes, the submission wait, the loss rung; the line's words still name the vendor through `rhi_tile_vendor_name`) |
| `DYNAMIC_STATE_OFF` | `vk_device.c` (the dynamic depth and blend state; `ICO_VK_FAKE_NO_DYNAMIC` stays there) |
| `MEMORY_BUDGET_OFF` | `vk_device.c` (`VK_EXT_memory_budget`) |
| `PASS_OBJECTS` | `vk_device.c` `vkr_pass_row_apply` (`ICO_VK_LEGACY_PASSES` and `passRowQcom` stay there) |
| `SUBPASS_CLASS` | `vk_device.c` `vkr_subpass_read_decide` (Qualcomm's where its row holds the passes) |
| `ATTACHMENT_READ_OFF` | `vk_device.c` (the rasterization-order attachment read; `ICO_VK_ATTACHMENT_READ` stays there) |
| `PIPE_ROBUST_ASK` | `vk_device.c` (`VK_EXT_pipeline_robustness` with `ICO_RD_VU_SKIN_CHECK` unset) |
| `VU_FALLBACK_PLAIN` (table), `CAREFUL_PIPELINES`, `VU_PREPASS_OFF`, `COMPUTE_RECORD_OFF` | `vk_device.c` `vkr_vu_plain_only` (`RhiLimits.vuFallbackPlain`, `carefulPipelines`, `vuPrepassOff`, `computeStoresOff`) |
| `COMPILER_SERIAL` | `vk_pipeline.c` `vkr_compile_serial_init` |

The pure predicates keep their names and signatures, and build an identity of their arguments to
read one choice (`rhi_device_table_choice`): `rhi_vk_pass_row`, `rhi_vk_pass_row_qualcomm`
(`rhi_pass_row.c`), `rhi_driver_compiler_serial`, `rhi_driver_is_qualcomm_proprietary`
(the table's test by id; `rhi_driver_version.c`), `rhi_tile_phone`, `rhi_tile_vendor_name`
(`rhi_tile_rule.c`). Their tests (`rhi_vk_pass_row`, `rhi_driver_version`,
`rhi_vk_tile_rule`) are unchanged.

The renderer reads the current device's choices (`rhi_device_table_current`, the backend's one
evaluation with its fakes) through `rd__device_value` (`port/render/rd_device_rows.h`). Every
backend zeroes its limits at `rhi_init` and `rhi_shutdown` and fills them after the evaluation, so
while the limits are zero (no device: before the start, after the end, a run without a GPU) it
gives the backend's defaults, which are what the old rules read from the zeroed limits (a desktop
class, no tile-based GPU, no phone), whatever evaluation an earlier device left.

| Choice | Call site (`port/render/`) |
| --- | --- |
| `PROFILE_CLASS` | `rd_profile.c` (the profile's class and its words; `rd__profile_adreno`, `rd__profile_powervr`) |
| `TILER` | `rd_profile.c` (`RdProfile.tiler`) |
| `TILE_CLASS` | `rd_profile.c` `rd__profile_tile_class` (the lamps' fill-paste check and their words) |
| `TILE_COLUMNS`, `BLUR_FOLD_OFF` | `rd_profile.c` `rd__profile_init` (glow fold, uint views, local reads, aura box, pass area, pass cut, shadow merge, menu text at scale 2; the blur fold) |
| `LAMPS_MS`, `LAMPS_FILLPASTE`, `LAMPS_AREA` | `rd_lamps.c` `rd__lamps_init` (`rd__lamps_default_bits_of`, with Vulkan, the pass area and the release's `RD_LAMPS_MS_DEFAULT` and `RD_LAMPS_FILLPASTE_DEFAULT`), the 4-sample pass's class |
| `FILL11_CLASS` | `rd_fill11.c` `classOk` |
| `SHADOW_TILE_CLASS` | `rd_shadow_tile.c` (with `RD_SHADOW_TILE_DEFAULT`) |
| `STENCIL_CUT` | `rd_stencil_selftest.c` (the self-test still decides) |
| `VU_COMPUTE_CLASS` | `rd_vu_record.c` (the PowerVR class and the measured device; `ICO_RD_VU_COMPUTE_CLASS` and `RD_VU_COMPUTE_CLASS_DEFAULT` stay there) |
| `NO_VOLUME_CLASS`, `NO_VOLUME_BREAK` | `rd_no_volume.c` `classBase`, `breakMeasured` (with `RD_NO_VOLUME_DEFAULT`, `RD_NO_VOLUME_BREAK_DEFAULT`) |
| `COMPACTION_CLASS` | `rd_vu_compact.c` (the DXT; the class and its two defaults by the rows `mali-g52` and `powervr-dxt-48-1536`, `RD_VU_COMPACT_ARM_DEFAULT` and `RD_VU_COMPACT_DEFAULT` defined in `rhi_device_table.h`; `ICO_RD_VU_COMPACT_ARM` read there) |
| `SKIN_CHECK_DEFAULT`, `SKIN_AS_BEFORE` | `rd_vu_skin_check.c` (with a window; `ICO_RD_VU_SKIN_CHECK` stays there) |
| `VU_FALLBACK_PLAIN` (Turnip) | `rd_pipeline.c` `rd__vu_set_choose` (the turnip row's words; the test is `rd__driver_is_turnip`) |
| `MSAA_MAX4` | `rd_core.c` `rd_aa_supported_mask` |
| `SCENE_CAP_4096`, `MSAA_BUDGET`, `SHOT_LATE` | `rd_present.c` |
| `UBER_AUTO` | `rd_uber.c` |
| `FOG_ORDER_TILER` | `rd_fog_path.c` |

The release-wide switches the renderer ANDs with a class (`RD_LAMPS_MS_DEFAULT`,
`RD_LAMPS_FILLPASTE_DEFAULT`, `RD_SHADOW_TILE_DEFAULT`, `RD_FILL11_DEFAULT`,
`RD_VU_COMPUTE_CLASS_DEFAULT`, `RD_NO_VOLUME_DEFAULT`, `RD_NO_VOLUME_BREAK_DEFAULT`,
`RD_LATE_DEFAULT`) stay in their files: the rows hold the class, the call site the switch, and the
table never reads their values (unlike the compaction's two, which are the rows' values).

`VU_FALLBACK_PLAIN` (the `adreno-740-qualcomm` row and `turnip`) decides more than the modules: a
device it puts on the plain set has no side bands, and its skinned draws keep the PS2's region
drops under the camera rule (`g_rd.vuNoBeside`, issue 163). Its static meshes and grids
take the camera rule there as everywhere (issue 123's right wall;
`port/render/rd_plain_camera.c`, the attempt ledger's row `the camera rule for static meshes on
the plain set`): the choice's value and words are unchanged, so the table, its rows and
`rhi_device_table_equiv` are too. The tests put lavapipe on that reading with
`ICO_RD_VU_PLAIN=row` (the renderer's, not one of the table's fakes: the plain set by `[dev]` with
the row's drops; ctest `rd_plain_camera_ab`).

Two readers still read the profile's `TILER` fact (`RdProfile.tiler`, which the table sets) rather
than their own choice: `rd_late_pass.c` (`LATE_PASS_TILER`) and `rd_vis.c` (`CULL_FRUSTUM`). Their
tests set `RdProfile.tiler` by hand to stand for a tile-based GPU (`rd_vis_test`, and the
tile-based pipeline sets of `rd_state_test` and `rd_pixel_test`); reading the choice would change
what those tests see. The values agree on every identity (`rhi_device_table_equiv`).

The renderer's pure predicates keep their names and signatures and read the table on an identity
of their arguments (`rd_device_rows.c`): `rd__driver_is_turnip` (the table's Turnip test),
`rd__profile_vendor_adreno` (`PROFILE_CLASS` on a Vulkan identity of that vendor),
`rd__vu_class_measured` (`VU_COMPUTE_CLASS` on an Imagination identity of that name). Their tests
(`rd_state`, `rd_vu_skin_check_judge` and the rest of `rd_vu_skin_check_test`, `rd_smallcuts`) are
unchanged.

The platform reads its choices through pure predicates that keep their names and signatures and
build an identity of their arguments (the vendor id, the Vulkan driver id and
`RhiLimits.driverText`, which the window and the diagnostics hand them from `rhi_limits()`), so
their tests (`memory_floor`, `present_fallback`) are unchanged:

| Choice | Call site (`port/platform/`) |
| --- | --- |
| `MEMORY_FLOOR_CLASS` | `memory_floor.c` `ico_memory_floor_driver` (asked by `diag_host.c` `ico_diag_set_memory_floor_driver`, which keeps the class it is handed: the floor's cap, and whether a stop carries over to the next start; `[dev] pipeline_memory_floor_mb` stays over it) |
| `FALLBACK_ELIGIBLE` | `present_fallback.c` `ico_present_fallback_driver_eligible`, the first test of `ico_present_fallback_gate` |
| `FALLBACK_SWITCH` | `present_fallback.c` `ico_present_fallback_qualcomm_own`: the gate's yes, or its "other" on an eligible driver (`[dev] present_fallback`, vsync, the mailbox mode and the backend stay the gate's own terms) |
| `FIFO_FROM_START` | `present_fallback.c` `ico_present_fallback_start` (with the mode and vsync), and through it the question `window_present_fallback.c` hands the Vulkan backend for its first swapchain (`fifo_start_now`, with `rhi_limits()`'s driver id) |
| `TURNIP_NOTE` | `android/gpu_driver_android.c` `on_stock_qualcomm` (the Android build's OS, the driver package the window names, the device's vendor id): the hang and loss boxes' sentence about a Turnip package |

The attempt ledger (`attempt_ledger.c`, in `ico_thread`) takes from the table the words of a row
of its own where no table row sets the choice on the device, `rhi_device_choice_na_words`, each
the phrase its decider held before, byte for byte (the third column of the choices' list below):
`PASS_OBJECTS` (the row "Qualcomm passes as VkRenderPass objects"), `VU_COMPUTE_CLASS` ("VU record
by compute on the PowerVR"), `NO_VOLUME_CLASS` ("no-volume break") and `COMPACTION_CLASS` ("index
compaction", whose other-class words are an "off by default"). Where a row holds, the ledger's
reason is the row's own (`RhiLimits.passObjectsRow`: the setting's reason).

Left where they are, and why:

- `port/platform/window_host.c` and `diag_host.c` are not touched (both are close to the 262,144
  byte cap on a source file). They hold no device rule of their own: `diag_host.c` asks
  `ico_memory_floor_driver` and keeps its answer, and `window_host.c` reads `RhiLimits.tiler` for
  the performance lines' words and hands `ico_gpu_driver_android_device_up` the driver package.
- Words only, no decision: `gpu_driver.c` (the last-seen file's "Qualcomm" and "Turnip" words),
  `window_stall.c` (the phone model in a log line), `window_lines.c` and `window_perf_rank.c`
  (the tile-based words of the performance lines), `memory_floor.c`'s and `present_fallback.c`'s
  line texts ("Qualcomm's own driver").
- The ledger's sentences that name a device inside a longer reason ("the PowerVR D-Series
  DXT-48-1536: compute dispatches in place of the vertex stores", "the Mali-G52, its start-up
  self-test passed") and the compaction's "off by default on the Mali-G52: ..." stay in
  `attempt_ledger.c`: the last is also the `mali-g52` row's reason, but the decider is handed the
  class's default as an argument (its test passes 0 whatever `RD_VU_COMPACT_ARM_DEFAULT` the build
  has), so it cannot read a row whose text follows the build's define.
- The subpass read's two off-by-default sentences (`port/rhi/rhi_subpass_verdict.h`) stay with
  their decider in the RHI, which hands them to the ledger.
- Measured or persisted facts are not rows: the few-cores rule (measured core speeds), the hang
  limits, the pipeline-skip record, the loss ladder, the `[dev] turnip_debug` step (the loaded
  package's library name).

## Adding a row

1. **Evidence.** A reporter log or dump whose device line gives vendor:device, driver ID, driver
   version, Vulkan version and OS, and the failure or the measurement. Cite the issue and the
   evidence.
2. **The narrowest match.** The driver id and a version range before the vendor; the device id or
   GPU name before a class; a driver version bound where the vendor's fix is known; a phone model
   only when no GPU or driver field separates the devices, saying why.
3. **Only an existing choice.** A new choice needs a call site, a default equal to today's
   behaviour and a typed value; no Options item can be a choice.
4. **Provenance.** What it sets, the issue, the evidence, the release it comes in and the condition
   to revisit it (the driver version bound where one is known).
5. **The equivalence test.** Its copies stay as they are; the identities whose choices change must
   be the row's own match set, listed in the test as expected differences.
6. **This file.** Run `rhi_device_table_test --docs` and replace the list between the markers; the
   ctest `rhi_device_table_docs` fails on any difference.

## Tests

- `rhi_device_table` (`port/rhi/test/rhi_device_table_test.c`, pure): unique row ids, every
  setting's evidence, release and revisit condition, a row sets a choice once, the attempt
  ledger's words are one line and belong to a choice some row sets, rows read only the
  facts resolved before what they set, every override link names a row it shares a choice with,
  the start-up lines and the summary clause of the Mali-G52, the Adreno 740 on Qualcomm's driver,
  the PowerVR DXT-48-1536, the Intel HD 530 and an RTX 3090 word for word, the clause's cut, the
  noted clause, a row's value by id; the header of WARP on Direct3D 12, a GTX 750 Ti on Direct3D
  11 and llvmpipe on OpenGL; the summary's room (the longest summary leaves 128 bytes or more of
  2047, the longest header and row line fit the report's key line and the start lines' buffer
  with the same margin).
- `rhi_device_table_equiv` (`port/rhi/test/rhi_device_table_equiv_test.c`, pure): copies of every
  rule the table replaces (`rhi_device_table_ref.inc`, each naming its source lines) against the
  table, value and words byte for byte, over about 1.4 million identities (every vendor that sent
  a log and the rest of the tile-based list, the Adreno device ids beside the 740's, the GPU names
  the rows read, twelve driver ids, fourteen driver texts, the Vulkan versions around 1.3.215,
  five OSes, four backends, MoltenVK, a driver package), the tests' fakes in every combination on
  the logged devices, and the profile's run inputs; the pure files the copies come from are linked
  in and compared with them, and they now read the table: the comparison covers the moved
  predicates (`rhi_tile_phone` on every combination of its arguments, `rhi_tile_vendor`, the
  serial rule at any Vulkan version), the Vulkan call sites' reads as the backend composes them
  (the two pass rows, the subpass class with every way its Qualcomm row can apply, the VU rows,
  the serial rule's reason, the fakes' parse), and the one-choice read and the
  backends' evaluation against the whole evaluation on the logged devices on every backend and
  OS; the renderer's reads as its call sites compose them (`port/render/rd_device_rows.c` linked
  in: the class and its Adreno and PowerVR tests, the tile class and columns, the lamps' three
  classes with and without a device, the crack fill's and the shadow tiles' classes, the stencil
  cut's words and its want, the VU record's class and measured device, the no-volume window and
  its break, the compaction's DXT, the skin check's default and v0.11.2 work, the VU set's kind and
  words, the tile-based limits and orders) on every identity, again on every logged GPU of
  `DEVICES.md` at each Vulkan version of the matrix's edges with the profile's run inputs, and with
  no device (whatever an earlier device left) on every backend; the renderer's three pure
  predicates against their copies; the platform's reads: `ico_memory_floor_driver` and the
  present fallback's three driver tests against their copies on every identity, then on every
  vendor, driver id and driver text the gate on every combination of vsync, mailbox, backend and
  `[dev] present_fallback`, FIFO from the start on every mode, build and vsync, the window's
  swapchain question and the Android build's stock driver hint as their call sites compose them
  (copies taken before the move), and the attempt ledger's four phrases, from the table and as the
  deciders write them (`attempt_ledger.c` linked in); every row in force somewhere, every override
  link deciding some identity, no unlinked conflict. The one change since the copies were taken:
  v0.11.8 took off the queue's high global priority (issues 215 and 123: the `qualcomm-android`
  row's second setting, the choice `QUEUE_PRIORITY_HIGH`, its fake and `rhi_qprio.c`). The copy
  of its rule stays; the test counts the identities it asked on (it fails on none, a matrix that
  missed the row) and fails on any choice whose name holds `PRIORITY`. Every other comparison is
  the one made against v0.11.7's rules, so a pass shows the table's only change is that choice.
- `rhi_device_table_docs`: the list below against the table.

## The list

<!-- device-table:begin -->
<!-- generated by rhi_device_table_test --docs; the ctest rhi_device_table_docs fails on any difference -->

### The choices

| Choice | Default (no row) | The attempt ledger's words where no row sets it |
| --- | --- | --- |
| `TILER` | no | - |
| `TILE_PHONE` | no | - |
| `PROFILE_CLASS` | by backend: desktop (Vulkan), d3d12, opengl, d3d11 | - |
| `TILE_CLASS` | 2 on the desktop classes, else 0 | - |
| `DYNAMIC_STATE_OFF` | no | - |
| `MEMORY_BUDGET_OFF` | no | - |
| `PASS_OBJECTS` | none | not Qualcomm's own driver |
| `SUBPASS_CLASS` | other | - |
| `ATTACHMENT_READ_OFF` | no | - |
| `VU_FALLBACK_PLAIN` | extended | - |
| `CAREFUL_PIPELINES` | no | - |
| `VU_PREPASS_OFF` | no | - |
| `COMPUTE_RECORD_OFF` | no | - |
| `COMPILER_SERIAL` | no | - |
| `PIPE_ROBUST_ASK` | no | - |
| `FIFO_FROM_START` | no | - |
| `FALLBACK_ELIGIBLE` | yes | - |
| `FALLBACK_SWITCH` | other | - |
| `MEMORY_FLOOR_CLASS` | other | - |
| `TURNIP_NOTE` | no | - |
| `SKIN_CHECK_DEFAULT` | no | - |
| `SKIN_AS_BEFORE` | no | - |
| `BLUR_FOLD_OFF` | no | - |
| `TILE_COLUMNS` | no | - |
| `LAMPS_MS` | no | - |
| `LAMPS_FILLPASTE` | no | - |
| `LAMPS_AREA` | yes on tile class 2 off a tile-based phone | - |
| `FILL11_CLASS` | 1 on tile class 1, 2 on tile class 2 off a tile-based phone, else 0 | - |
| `SHADOW_TILE_CLASS` | TILE_CLASS | - |
| `STENCIL_CUT` | no | - |
| `VU_COMPUTE_CLASS` | none | not the PowerVR class under Vulkan |
| `NO_VOLUME_CLASS` | no | not the PowerVR or MoltenVK class under Vulkan |
| `NO_VOLUME_BREAK` | no | - |
| `COMPACTION_CLASS` | other | off by default: measured on the Mali-G52 only (and on the PowerVR D-Series DXT-48-1536's compute record) |
| `COMPACTION_DEFAULT` | the class's: RD_VU_COMPACT_ARM_DEFAULT on g52, RD_VU_COMPACT_DEFAULT on dxt, else no | - |
| `MSAA_MAX4` | no | - |
| `SCENE_CAP_4096` | no | - |
| `MSAA_BUDGET` | no | - |
| `SHOT_LATE` | no | - |
| `LATE_PASS_TILER` | no | - |
| `UBER_AUTO` | no | - |
| `FOG_ORDER_TILER` | no | - |
| `CULL_FRUSTUM` | no | - |

### The rows (27, in table order)

#### `tile-vendors`

Matches Vulkan on a Qualcomm, Arm, Imagination, Samsung, Apple or Broadcom GPU: backend Vulkan; vendor 0x5143, 0x13B5, 0x1010, 0x144D, 0x106B, 0x14E4.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `TILER` | yes | a tile-based GPU | - | v0.5.0 | commit a54e2f896 (the fog's copy on tile-based GPUs) | never: these vendors' GPUs are tile-based by design |

#### `android`

Matches Vulkan on Android: backend Vulkan; OS Android.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `TILER` | yes | a tile-based GPU (every Android GPU) | - | v0.5.2 | commit 6ba1c875a | never: a phone GPU missing from the vendor list would take the desktop defaults |
| `TILE_PHONE` | yes | a tile-based phone GPU: v0.11.2's wide barriers and single submission wait, the loss rung | issues 179 and 211 | v0.11.4 | commit 6fb557768; the Mali-G52 and the Adreno 710 lost the device in the first 3D scene on v0.11.3 and not on v0.11.2 (v0114/evidence/179, v0114/evidence/211) | a phone log without a loss under v0.11.3's scopes and wait |

#### `ios`

Matches Vulkan on iOS through MoltenVK: backend Vulkan; OS iOS; MoltenVK yes.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `TILE_PHONE` | yes | a tile-based phone GPU: v0.11.2's wide barriers and single submission wait, the loss rung | issues 179 and 211 | v0.11.4 | commit 6fb557768 (Apple's phone GPUs taken with Android's) | an iOS log under v0.11.3's scopes and wait |

#### `tiler`

Matches a tile-based GPU: a tile-based GPU (TILER) yes.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `DYNAMIC_STATE_OFF` | yes | dynamic depth and blend state off by default | - | v0.7.10 | commit b6aed5556: a PowerVR phone's pictures shook between ticks with it on and were steady with it off | a tile-based phone steady between ticks with [dev] dynamic_state = true |
| `MSAA_MAX4` | yes | MSAA up to 4 samples (a memory limit on what Options offer) | - | v0.6.0 | commit aaf47d91d | a queried memory budget in place of the class |
| `SCENE_CAP_4096` | yes | the scene at most 4096 pixels a side (a memory limit: phones share memory with the system) | - | v0.5.1 | commit ab60630ce | a queried memory budget in place of the class |
| `MSAA_BUDGET` | yes | the MSAA scene's scale halved to its memory budget (a limit) | - | v0.6.0 | commit 522a08a16 (RD_MSAA_TILER_BUDGET) | a queried memory budget in place of the class |
| `SHOT_LATE` | yes | the state picture's late shot | issue 179 | v0.11.5 | commit 771a52f71 | a phone log whose state pictures cost nothing with the old path |
| `LATE_PASS_TILER` | yes | list 11's late pass (RD_LATE_DEFAULT) under Vulkan | - | v0.11.5 | commit 7a78f02b4 | a tile-based phone's pass times without it |
| `UBER_AUTO` | yes | the uber shader on under auto | - | v0.10.0 | commit 01eff8f7c | a desktop log where it pays |
| `FOG_ORDER_TILER` | yes | the tile-based fog order | - | v0.5.2 | commit 336bc4e00 | never: a tile-based GPU's copy path first |
| `CULL_FRUSTUM` | yes | cull frustum by default | issue 179 | v0.11.2 | commit fa98a57ba: the occlusion test cost a phone's CPU more than it saved | a phone log where the occlusion test saves time |

#### `tile-phone`

Matches a tile-based phone GPU: a tile-based phone (TILE_PHONE) yes.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `MEMORY_BUDGET_OFF` | yes | VK_EXT_memory_budget not asked | - | v0.11.5 | commit 1859caa76 (a phone's free memory is the system's own measure) | never: Android's MemAvailable is the measure there |
| `SKIN_AS_BEFORE` | yes | the skin check issues v0.11.2's work | issue 211 | v0.11.4 | commit cf4256f8d | a phone log without a loss under the v0.11.3 check |

#### `intel-windows-1.3.215`

Matches Vulkan 1.3.215 or older on Intel's Windows driver: backend Vulkan; vendor 0x8086; driver: Intel's Windows driver (driver id 5, or id 0 on a Windows build); Vulkan 1.3.215 or older.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `PASS_OBJECTS` | intel | render passes as VkRenderPass objects, the store passes on a colour target ("Intel's Windows driver at Vulkan 1.3.215 or older (the final drivers for 6th to 10th gen graphics crash in vkCmdBeginRendering)") | - | v0.11.3 | an HD 530 on 101.2125 crashed in vkCmdBeginRendering (v0113/evidence/igpu) | never: Intel ships no newer driver for 6th to 10th gen graphics |
| `SUBPASS_CLASS` | intel | the subpass read probed | - | v0.11.6 | commit 7a717025c (RHI_SUBPASS_PROBE_INTEL_DEFAULT) | never while the row holds the passes |

#### `qualcomm-android`

Matches Vulkan on Android, Qualcomm's own driver by its id: backend Vulkan; OS Android; driver: Qualcomm's own driver by its id (vendor 0x5143, driver id 8 or 0).

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `PASS_OBJECTS` | qualcomm | render passes as VkRenderPass objects ("Qualcomm's own driver (issue 123: its GPU hangs inside a pass begun with vkCmdBeginRendering; VkRenderPass objects are the driver's oldest path, the same picture)") | issue 123 | v0.11.6 | the Adreno 740 (512.676) and Adreno 710 (512.800) hang inside a pass begun with vkCmdBeginRendering (v0117/evidence/123) | the reporter's next log: no hang inside a pass |

#### `qualcomm`

Matches Vulkan, Qualcomm's own driver by its id: backend Vulkan; driver: Qualcomm's own driver by its id (vendor 0x5143, driver id 8 or 0).

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `ATTACHMENT_READ_OFF` | yes | the rasterization-order attachment read not offered | issue 89 | v0.7.8 | an Adreno 830 on 512.800.72 drew wrong colours through it (commit 92347c556) | a Qualcomm driver that draws issue 89's frames right (no version known) |
| `SUBPASS_CLASS` | qcom | the subpass read off by default | issue 123 | v0.11.6 | commit 7a717025c | a probe on Qualcomm's driver without a loss |

#### `vendor-unknown`

Matches no vendor id (no device yet, or the D3D12 backend): vendor 0x0000.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `MEMORY_FLOOR_CLASS` | unknown | the free-memory floor's cautious rule (the driver not known) | issue 161 | v0.10.0 | commit f4b238e57 | never: no vendor id names no driver |
| `FALLBACK_ELIGIBLE` | no | the present fallback's switch not taken | issue 123 | v0.10.2 | commit d8624d3ba | never: no vendor id names no driver |

#### `qualcomm-text`

Matches Qualcomm's own driver by its text: driver: Qualcomm's own driver by its text (vendor 0x5143, driver id not 18, no Mesa or Turnip in the text).

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `MEMORY_FLOOR_CLASS` | qualcomm | the free-memory floor at 17 % up to 2048 MB, its stop kept to the next start | issue 161 | v0.10.0 | commit f4b238e57 (v096/evidence/161) | a Qualcomm driver log that starts without the system ending the app |
| `FALLBACK_SWITCH` | yes | the present fallback's switch to FIFO allowed | issue 123 | v0.10.2 | commit d8624d3ba; on a Mali-G52 the switch lengthened the blocks (issue 179) | a Qualcomm driver log whose mailbox presents do not block |

#### `qualcomm-text-android`

Matches Android, Qualcomm's own driver by its text: OS Android; driver: Qualcomm's own driver by its text (vendor 0x5143, driver id not 18, no Mesa or Turnip in the text).

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `FIFO_FROM_START` | yes | FIFO from the first swapchain with vsync off | issue 123 | v0.10.2 | commit fc627fcb2 | a Qualcomm driver log whose mailbox presents do not block |

#### `mesa-drivers`

Matches a Mesa driver (driver id 18, or Mesa or Turnip in the driver text): driver: a Mesa driver (driver id 18, or Mesa or Turnip in the text).

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `FALLBACK_ELIGIBLE` | no | the present fallback's switch not taken (a Mesa driver) | - | v0.10.2 | commit d8624d3ba | a Mesa driver log whose mailbox presents block |

#### `qualcomm-vendor`

Matches Vulkan, vendor 0x5143: backend Vulkan; vendor 0x5143.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `PIPE_ROBUST_ASK` | yes | VK_EXT_pipeline_robustness asked for the skin check (with robust access on) | issue 163 | v0.10.4 | commit 68d97aa77 | the skin check's end |

#### `adreno`

Matches Vulkan, vendor 0x5143, not MoltenVK: backend Vulkan; vendor 0x5143; MoltenVK no; overrides other-tiler.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `PROFILE_CLASS` | adreno_qualcomm | profile class Adreno, Qualcomm's driver | - | v0.7.10 | commit 85e9bc65e | never: a class |
| `SKIN_CHECK_DEFAULT` | yes | the skin check on with a window | issue 163 | v0.10.4 | commit 68d97aa77 | an Adreno log whose checks never fire |

#### `turnip-adreno`

Matches Vulkan, vendor 0x5143, not MoltenVK, Turnip: backend Vulkan; vendor 0x5143; driver: Turnip (driver id 18, or Turnip in the text; no vendor test); MoltenVK no; overrides adreno other-tiler.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `PROFILE_CLASS` | adreno_turnip | profile class Adreno, Turnip | - | v0.9.7 | commit 712e015fa | never: a class |

#### `turnip`

Matches Turnip (driver id 18, or Turnip in the driver text): driver: Turnip (driver id 18, or Turnip in the text; no vendor test).

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `VU_FALLBACK_PLAIN` | turnip | the VU fallback's plain set ("Turnip driver, issue 163") | issue 163 | v0.9.7 | a skinned draw's vertex showed as a sliver through the extended set (v0105/evidence/123) | a Mesa version a device test shows free of the sliver with the extended set |

#### `adreno-740-qualcomm`

Matches Vulkan, Qualcomm's own driver by its id on device 0x43050A01: backend Vulkan; vendor 0x5143; device 0x43050A01 under mask 0xFFFFFFFF; driver: Qualcomm's own driver by its id (vendor 0x5143, driver id 8 or 0); overrides turnip.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `VU_FALLBACK_PLAIN` | table | the VU fallback's plain set ("Adreno 740, issue 107") | issue 107 | v0.7.8 | the whole device froze compiling v0.7.5 to v0.7.7's VU vertex shaders (driver 512.676.53, compiler E031.41.03.47; commit d3f88bd03) | a driver newer than 512.676 building the extended set |
| `CAREFUL_PIPELINES` | yes | the careful start-up set ("Adreno 740, issues 93 and 107") | issues 93 and 107 | v0.7.8 | both devices died inside the blind set's 2D half with v0.7.7 (commit 4dd2f86c7) | a driver newer than 512.676 making the start-up set in parallel |
| `VU_PREPASS_OFF` | yes | the VU pre-pass off ("Adreno 740 on Qualcomm's driver, issue 123") | issue 123 | v0.11.1 | the first scene's replays drew noise and submission 369 never finished (driver 512.676.1, compiler E031.41.03.64; commit 85adff65e) | a driver newer than 512.676 (compiler E031.41.03.64) running the pre-pass without a hang |

#### `stock-qualcomm-android`

Matches Android, no driver package, vendor 0x5143: OS Android; vendor 0x5143; a driver package no.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `TURNIP_NOTE` | yes | the hang and loss boxes name a Turnip package | - | v0.9.5 | UI_STR_GPU_HANG_TURNIP | never: a hint, no renderer choice |

#### `arm`

Matches Vulkan, vendor 0x13B5: backend Vulkan; vendor 0x13B5.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `SUBPASS_CLASS` | arm | the subpass read off by default | issue 179 | v0.11.6 | commit 7a717025c | a probe on an Arm driver without a loss |

#### `mali`

Matches Vulkan, vendor 0x13B5, not MoltenVK: backend Vulkan; vendor 0x13B5; MoltenVK no; overrides other-tiler.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `PROFILE_CLASS` | mali | profile class Mali | - | v0.7.10 | commit 85e9bc65e | never: a class |
| `BLUR_FOLD_OFF` | yes | the blur fold off (the G52 is bound by its ALU) | - | v0.9.4 | commit cab17538a | a Mali log where the fold saves time |

#### `mali-g52`

Matches vendor 0x13B5, not MoltenVK, a device name with "Mali-G52": vendor 0x13B5; device name holds "Mali-G52"; MoltenVK no.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `COMPACTION_CLASS` | g52 | the index compaction's Mali-G52 class | issue 179 | v0.11.6 | commit 67dedc5d5 | never: a class |
| `COMPACTION_DEFAULT` | no | the index compaction off by default ("off by default on the Mali-G52: it blacked out the picture there in v0.11.6 (issue 179; RD_VU_COMPACT_ARM_DEFAULT 0)") | issue 179 | v0.11.7 | the moto g24 power's v0.11.6 pictures black from the first 3D picture on (v0117/evidence/179) | the cause found, or a G52 log of complete pictures with it on |

#### `powervr`

Matches Vulkan, vendor 0x1010, not MoltenVK: backend Vulkan; vendor 0x1010; MoltenVK no; overrides other-tiler.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `PROFILE_CLASS` | powervr | profile class PowerVR | - | v0.7.10 | commit 85e9bc65e | never: a class |
| `TILE_CLASS` | 1 | tile class 1 (the shadow tiles' and fill11's PowerVR class) | - | v0.11.5 | commit 71578e502 | never: a class |
| `LAMPS_MS` | yes | the lamps' 4-sample pass's class (RD_LAMPS_MS_DEFAULT decides) | - | v0.11.5 | commit f02f7b75a (the form the owner accepted on the PowerVR phone) | another class measured with it |
| `LAMPS_FILLPASTE` | yes | the crack fill in the paste's pass's class (RD_LAMPS_FILLPASTE_DEFAULT decides) | - | v0.11.5 | commit 7c69c3735 | another class measured with it |
| `LAMPS_AREA` | yes | the lamps' area's class | - | v0.11.5 | commit 653ad33bb | another class measured with it |
| `STENCIL_CUT` | yes | stencil outside the shadow window cut where its self-test passes | - | v0.11.5 | commit e6409f79b | another class measured with it |
| `VU_COMPUTE_CLASS` | class | the VU record by compute's class (off by default but on the measured device) | - | v0.11.6 | commit 2aea87a7b | a B-Series or Rogue log with it on |
| `NO_VOLUME_CLASS` | yes | the no-volume window's class (RD_NO_VOLUME_DEFAULT decides) | - | v0.11.6 | commit f575b4d3a | the window measured on the class |

#### `powervr-b-series`

Matches Vulkan, vendor 0x1010, "b-series" in the driver text: backend Vulkan; vendor 0x1010; driver text holds "b-series" (any case).

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `COMPILER_SERIAL` | yes | pipeline compiles one at a time, a 60 s pre-game limit ("PowerVR B-Series") | issue 188 | v0.10.2 | Imagination: the B-Series compiler is not thread-safe (BXM-8-256, 1.15@6133110; commit aad38ac8c) | a B-Series driver Imagination says is thread-safe |

#### `powervr-dxt-48-1536`

Matches Vulkan, vendor 0x1010, not MoltenVK, a device name with "PowerVR D-Series DXT-48-1536": backend Vulkan; vendor 0x1010; device name holds "PowerVR D-Series DXT-48-1536"; MoltenVK no; overrides powervr.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `VU_COMPUTE_CLASS` | measured | the VU record by compute (RD_VU_COMPUTE_CLASS_DEFAULT decides) | - | v0.11.6 | the fastest opening measured on the owner's Pixel 10 Pro Fold (commit 2aea87a7b) | never: measured |
| `NO_VOLUME_BREAK` | yes | the no-volume break mode's device | - | v0.11.6 | commit f575b4d3a | never: measured |
| `COMPACTION_CLASS` | dxt | the index compaction's DXT class | - | v0.11.6 | commit 67dedc5d5 | never: a class |
| `COMPACTION_DEFAULT` | yes | the index compaction's default (RD_VU_COMPACT_DEFAULT; on where its self-test passes) | - | v0.11.6 | commit 67dedc5d5 | never: measured |

#### `moltenvk`

Matches Vulkan through MoltenVK: backend Vulkan; MoltenVK yes; overrides other-tiler.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `PROFILE_CLASS` | apple | profile class Apple | - | v0.7.10 | commit 85e9bc65e | never: a class |
| `NO_VOLUME_CLASS` | yes | the no-volume window's class (RD_NO_VOLUME_DEFAULT decides) | - | v0.11.6 | commit f575b4d3a | the window measured on a Mac or an iPhone |

#### `other-tiler`

Matches Vulkan, a tile-based GPU of no class row: backend Vulkan; a tile-based GPU (TILER) yes.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `PROFILE_CLASS` | tiler | profile class another tile-based GPU | - | v0.7.10 | commit 85e9bc65e | never: a class |

#### `tile-classes`

Matches a tile-based profile class: profile class adreno_qualcomm, adreno_turnip, mali, powervr, apple, tiler.

| Choice | Value | What | Issue | Since | Evidence | Revisit |
| --- | --- | --- | --- | --- | --- | --- |
| `TILE_COLUMNS` | yes | the tile-based columns: glow fold off, uint views off, local reads where offered, aura box, pass area, pass cut, shadow merge, menu text baked at scale 2 | - | v0.7.10 | commits 85e9bc65e, 5c97fa012 and the v0.10.0 to v0.10.1 profile rows | each column on a measurement of its own |

<!-- device-table:end -->
