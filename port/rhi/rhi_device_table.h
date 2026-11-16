/* rhi_device_table.h: the device table, one list of hardware rows (v0.11.7;
 * docs/port/DEVICE_TABLE.md).  Pure C: no Vulkan, SDL, I/O or getenv; built
 * and tested on every host (port/rhi/test/rhi_device_table_test.c, and
 * rhi_device_table_equiv_test.c, which proves the rows give what the
 * predicates spread over the backends, the renderer and the platform give
 * today on every identity of a matrix over every device that sent a log).
 *
 * A row matches a device's identity (GPU vendor, device id or name, driver
 * id and text, Vulkan version, backend, OS) and sets renderer-internal
 * technical choices: a path, a workaround, an attempt on or off.  Never a
 * player setting: no Options item, resolution, aspect, frame rate or
 * effect is a choice (the three tile-based memory limits on what Options
 * offer, MSAA_MAX4, SCENE_CAP_4096 and MSAA_BUDGET, are hardware limits,
 * kept as they are).  Every setting carries its issue, evidence, the
 * version it came in and the condition to revisit it.  A choice no row
 * sets keeps its default, which is today's behaviour on a device without
 * a row; capabilities and self-tests decide the rest at their call sites,
 * as the [dev] keys and the tests' environment overrides do.
 *
 * Several rows may set one choice: equal values agree (the first row in
 * table order is named the setter); different values need an explicit
 * override link (RhiDeviceRow.overrides), and rhi_device_table_test fails on
 * any identity of its matrix where two matching rows differ without one.
 * Derived facts (TILER, TILE_PHONE, PROFILE_CLASS, TILE_CLASS) are choices
 * too, resolved in enum order, so a row may match on a fact resolved before
 * the choices it sets. */
#ifndef PORT_RHI_RHI_DEVICE_TABLE_H
#define PORT_RHI_RHI_DEVICE_TABLE_H

#include <stddef.h>
#include <stdint.h>

/* The index compaction's class defaults (port/render/rd_vu_compact.h, which
 * includes this header; the values of the rows mali-g52 and
 * powervr-dxt-48-1536, read by rd_vu_compact.c through
 * rhi_device_table_row_value): 1 on where its self-test passes, 0 off.
 * RD_VU_COMPACT_ARM_DEFAULT is 0 since v0.11.7: on in v0.11.6, the
 * Mali-G52 of issue 179 drew a black picture from its first 3D picture on
 * (every picture's GPU work stopped after list 0's start, the fences still
 * signalled), and the index compaction, the first index buffer the GPU
 * wrote on that device, is the likeliest of v0.11.6's changes there
 * (lavapipe draws the reporter's loss dumps the same with it on and off,
 * and the bounds audit, rd_vu_audit.h, found no access out of range: not
 * proven); off there, as v0.11.5 drew.  A -D on the compiler's command line
 * flips either for an rc build: it must reach this library and the
 * renderer alike (CMAKE_C_FLAGS does). */
#ifndef RD_VU_COMPACT_DEFAULT
#define RD_VU_COMPACT_DEFAULT 1
#endif
#ifndef RD_VU_COMPACT_ARM_DEFAULT
#define RD_VU_COMPACT_ARM_DEFAULT 0
#endif

/* ------------------------------------------------------------ the identity */

/* the backend (rhi.h RhiBackendKind's values; Metal is never selected) */
enum { RHI_DT_VULKAN = 0, RHI_DT_D3D12 = 1, RHI_DT_OPENGL = 3, RHI_DT_D3D11 = 4 };

/* the build's OS */
enum {
    RHI_DT_LINUX = 0,
    RHI_DT_WINDOWS,
    RHI_DT_MACOS,
    RHI_DT_ANDROID,
    RHI_DT_IOS,
    RHI_DT_OS_COUNT
};

/* the OS this file is compiled for, with the same macros today's rules read
   (__ANDROID__, ICO_IOS, _WIN32) */
#if defined(__ANDROID__)
#define RHI_DT_OS_BUILD RHI_DT_ANDROID
#elif defined(ICO_IOS)
#define RHI_DT_OS_BUILD RHI_DT_IOS
#elif defined(_WIN32)
#define RHI_DT_OS_BUILD RHI_DT_WINDOWS
#elif defined(__APPLE__)
#define RHI_DT_OS_BUILD RHI_DT_MACOS
#else
#define RHI_DT_OS_BUILD RHI_DT_LINUX
#endif

typedef struct RhiDeviceId {
    uint8_t backend;      /* RHI_DT_VULKAN, ... */
    uint8_t os;           /* RHI_DT_LINUX, ... (the build's) */
    uint8_t moltenvk;     /* RhiLimits.moltenvk: rhi_driver_is_moltenvk, or the tests'
                             ICO_VK_FAKE_LIMITS=moltenvk */
    uint8_t customDriver; /* Android: a driver package the game loaded (rhi_set_vulkan_loader) */
    uint32_t vendorId;    /* the real one (RhiLimits.vendorId); ICO_VK_FAKE_VENDOR is a fake */
    uint32_t deviceId;
    uint32_t driverId;         /* VkDriverId, 0 unknown */
    uint32_t apiVersion;       /* the device's Vulkan version (VK_MAKE_API_VERSION packing) */
    const char *deviceName;    /* rhi_adapter_name(); NULL unknown */
    const char *driverText;    /* RhiLimits.driverText exactly ("device ...; driver ...,
                                   version ..."); NULL unknown */
    const char *driverVersion; /* the version text, for the start line alone; NULL none */
} RhiDeviceId;

/* ------------------------------------------------------------- the choices */

typedef enum RhiDeviceChoice {
    /* the derived facts, in the order rows may read them */
    RHI_DC_TILER = 0,     /* bool: RhiLimits.tiler, a tile-based GPU */
    RHI_DC_TILE_PHONE,    /* bool: RhiLimits.tilePhone (rhi_tile_rule.h) */
    RHI_DC_PROFILE_CLASS, /* RHI_DT_CLASS_*: rd_profile.c classOf */
    RHI_DC_TILE_CLASS,    /* 0 / 1 / 2: rd_profile.c rd__profile_tile_class */
    /* the Vulkan backend's */
    RHI_DC_DYNAMIC_STATE_OFF,   /* bool: dynamic depth/blend state off by default */
    RHI_DC_MEMORY_BUDGET_OFF,   /* bool: VK_EXT_memory_budget not asked */
    RHI_DC_PASS_OBJECTS,        /* RHI_DT_PASS_*: the driver rows (rhi_pass_row.h) */
    RHI_DC_SUBPASS_CLASS,       /* RHI_DT_SUBPASS_*: rhi_subpass_verdict.h's class */
    RHI_DC_ATTACHMENT_READ_OFF, /* bool: the rasterization-order attachment read not offered */
    RHI_DC_VU_FALLBACK_PLAIN,   /* RHI_DT_VUSET_*: the VU fallback's plain set */
    RHI_DC_CAREFUL_PIPELINES,   /* bool: the careful start-up set */
    RHI_DC_VU_PREPASS_OFF,      /* bool: the VU pre-pass off by the table */
    RHI_DC_COMPUTE_RECORD_OFF,  /* bool: the compute record not offered (no row) */
    RHI_DC_COMPILER_SERIAL,     /* bool: pipeline compiles one at a time */
    RHI_DC_PIPE_ROBUST_ASK,     /* bool: VK_EXT_pipeline_robustness asked (skin check) */
    /* the platform's */
    RHI_DC_FIFO_FROM_START,    /* bool: FIFO from the first swapchain with vsync off */
    RHI_DC_FALLBACK_ELIGIBLE,  /* bool: the present fallback's switch can be taken */
    RHI_DC_FALLBACK_SWITCH,    /* RHI_DT_SWITCH_*: the switch allowed (yes) or GATE_OTHER */
    RHI_DC_MEMORY_FLOOR_CLASS, /* RHI_DT_FLOOR_*: memory_floor.h's driver class */
    RHI_DC_TURNIP_NOTE,        /* bool: the hang box names a Turnip package */
    /* the renderer's */
    RHI_DC_SKIN_CHECK_DEFAULT, /* bool: the skin check on by default with a window */
    RHI_DC_SKIN_AS_BEFORE,     /* bool: the skin check issues v0.11.2's work */
    RHI_DC_BLUR_FOLD_OFF,      /* bool: the profile's blur fold off */
    RHI_DC_TILE_COLUMNS,       /* bool: the profile's tile-based columns */
    RHI_DC_LAMPS_MS,           /* bool: the lamps' 4-sample pass's class (Vulkan) */
    RHI_DC_LAMPS_FILLPASTE,    /* bool: the crack fill in the paste's pass's class (Vulkan) */
    RHI_DC_LAMPS_AREA,         /* bool: the lamps' area's class (Vulkan, pass area) */
    RHI_DC_FILL11_CLASS,       /* 0 / 1 / 2: rd_fill11.c classOk */
    RHI_DC_SHADOW_TILE_CLASS,  /* 0 / 1 / 2: the shadow tiles' class */
    RHI_DC_STENCIL_CUT,        /* bool: the stencil cut's class */
    RHI_DC_VU_COMPUTE_CLASS,   /* RHI_DT_VUCOMP_*: the VU record by compute */
    RHI_DC_NO_VOLUME_CLASS,    /* bool: the no-volume window's class */
    RHI_DC_NO_VOLUME_BREAK,    /* bool: the no-volume break mode's device */
    RHI_DC_COMPACTION_CLASS,   /* RHI_DT_CMP_*: the index compaction's class */
    RHI_DC_COMPACTION_DEFAULT, /* bool: the class's default (RD_VU_COMPACT*_DEFAULT) */
    RHI_DC_MSAA_MAX4,          /* bool: MSAA above 4 samples not offered (a limit) */
    RHI_DC_SCENE_CAP_4096,     /* bool: the scene at most 4096 a side (a limit) */
    RHI_DC_MSAA_BUDGET,        /* bool: the MSAA scene halved to its budget (a limit) */
    RHI_DC_SHOT_LATE,          /* bool: the state picture's late shot */
    RHI_DC_LATE_PASS_TILER,    /* bool: RD_LATE_DEFAULT under Vulkan, else the join */
    RHI_DC_UBER_AUTO,          /* bool: the uber shader on under auto */
    RHI_DC_FOG_ORDER_TILER,    /* bool: the tile-based fog order */
    RHI_DC_CULL_FRUSTUM,       /* bool: cull frustum by default */
    RHI_DC_COUNT
} RhiDeviceChoice;

/* PROFILE_CLASS (rd_profile.c ProfileClass's order) */
enum {
    RHI_DT_CLASS_DESKTOP = 0,
    RHI_DT_CLASS_D3D12,
    RHI_DT_CLASS_OPENGL,
    RHI_DT_CLASS_D3D11,
    RHI_DT_CLASS_ADRENO_QUALCOMM,
    RHI_DT_CLASS_ADRENO_TURNIP,
    RHI_DT_CLASS_MALI,
    RHI_DT_CLASS_POWERVR,
    RHI_DT_CLASS_APPLE,
    RHI_DT_CLASS_TILER,
    RHI_DT_CLASS_COUNT
};

/* PASS_OBJECTS */
enum { RHI_DT_PASS_NONE = 0, RHI_DT_PASS_INTEL, RHI_DT_PASS_QUALCOMM };

/* SUBPASS_CLASS (rhi_subpass_verdict.h RhiSubpassClass's meaning) */
enum { RHI_DT_SUBPASS_OTHER = 0, RHI_DT_SUBPASS_INTEL, RHI_DT_SUBPASS_ARM, RHI_DT_SUBPASS_QCOM };

/* VU_FALLBACK_PLAIN */
enum { RHI_DT_VUSET_EXTENDED = 0, RHI_DT_VUSET_TABLE, RHI_DT_VUSET_TURNIP };

/* FALLBACK_SWITCH (present_fallback.h: GATE_OTHER, GATE_YES) */
enum { RHI_DT_SWITCH_OTHER = 0, RHI_DT_SWITCH_YES };

/* MEMORY_FLOOR_CLASS (memory_floor.h IcoMemoryFloorDriver's values) */
enum { RHI_DT_FLOOR_UNKNOWN = 0, RHI_DT_FLOOR_QUALCOMM, RHI_DT_FLOOR_OTHER };

/* VU_COMPUTE_CLASS */
enum { RHI_DT_VUCOMP_NONE = 0, RHI_DT_VUCOMP_CLASS, RHI_DT_VUCOMP_MEASURED };

/* COMPACTION_CLASS (attempt_ledger.h ICO_ATT_CMP_*'s values) */
enum { RHI_DT_CMP_OTHER = 0, RHI_DT_CMP_G52, RHI_DT_CMP_DXT };

/* ---------------------------------------------------------------- the rows */

/* a match field's three states (0: either) */
enum { RHI_DT_ANY = 0, RHI_DT_NO = 1, RHI_DT_YES = 2 };

/* the driver tests today's rules use, each kept as it is (unifying them is a
 * behaviour change for identities no log has shown, left for later):
 *   QCOM_ID       vendor 0x5143 with driver id 8 or 0
 *                 (rhi_driver_is_qualcomm_proprietary)
 *   QCOM_TEXT     vendor 0x5143, driver id not 18, neither "mesa" nor
 *                 "turnip" in the driver text (memory_floor.c,
 *                 present_fallback.c)
 *   MESA_TEXT     driver id 18, or "mesa" or "turnip" in the driver text
 *                 (present_fallback.c's ineligible drivers beside vendor 0)
 *   TURNIP        driver id 18, or "turnip" in the driver text, no vendor
 *                 test (rd_profile.c rd__driver_is_turnip)
 *   INTEL_WIN_ID  driver id 5, or id 0 on a Windows build (rhi_pass_row.c)
 * The words in the text are ASCII letters in any case. */
enum {
    RHI_DT_DRV_ANY = 0,
    RHI_DT_DRV_QCOM_ID,
    RHI_DT_DRV_QCOM_TEXT,
    RHI_DT_DRV_MESA_TEXT,
    RHI_DT_DRV_TURNIP,
    RHI_DT_DRV_INTEL_WIN_ID
};

#define RHI_DT_MAX_VENDORS 6

typedef struct RhiDeviceMatch { /* every field "any" unless set */
    uint32_t backends;          /* bits 1u << RHI_DT_*; 0 any */
    uint32_t oses;              /* bits 1u << RHI_DT_<os>; 0 any */
    uint8_t vendorCount;        /* 0 any */
    uint32_t vendors[RHI_DT_MAX_VENDORS];
    uint32_t deviceMask, device;    /* deviceMask 0: any */
    uint8_t driver;                 /* RHI_DT_DRV_* */
    uint32_t apiMax;                /* 0 none: the Vulkan version at most this */
    const char *nameHas;            /* the device name holds it (case as written) */
    const char *textHas;            /* the driver text holds it (ASCII letters in any case) */
    uint8_t moltenvk, customDriver; /* RHI_DT_ANY / NO / YES */
    uint8_t tiler, tilePhone;       /* the derived facts: RHI_DT_ANY / NO / YES */
    uint32_t profileClasses;        /* bits 1u << RHI_DT_CLASS_*; 0 any */
} RhiDeviceMatch;

typedef struct RhiDeviceSetting {
    uint16_t choice;      /* RhiDeviceChoice */
    int32_t value;        /* the choice's value */
    const char *what;     /* the start line's words for what it sets */
    const char *reason;   /* the phrase today's code prints or passes for it, verbatim
                             (RhiLimits.vuFallbackPlain, rhi_vk_pass_row's, ...); NULL none */
    const char *issue;    /* "issue 123", "issues 93 and 107", "" none */
    const char *evidence; /* the log, dump or commit it rests on */
    const char *since;    /* "v0.7.8" */
    const char *revisit;  /* the condition to look at it again */
} RhiDeviceSetting;

typedef struct RhiDeviceRow {
    const char *id;         /* "adreno-740-qualcomm" */
    const char *matchWords; /* "Qualcomm's own driver by its id on device 0x43050A01" */
    RhiDeviceMatch match;
    const char *overrides; /* the row ids, space-separated, this one beats where both match */
    const RhiDeviceSetting *settings;
    uint8_t settingCount;
} RhiDeviceRow;

/* the tests' environment fakes the backend reads and hands over (the table
 * itself reads no environment); each keeps today's scope exactly:
 *   vendorSet/vendor  ICO_VK_FAKE_VENDOR=<id>: TILE_PHONE where the id is a
 *                     tile-based vendor's, and SUBPASS_CLASS's Arm and
 *                     Qualcomm parts from that id (not over the Intel row)
 *   passRowQualcomm   ICO_VK_FAKE_PASS_ROW=qualcomm: PASS_OBJECTS Qualcomm's
 *                     unless the Intel row holds
 *   adreno            ICO_VK_FAKE_ADRENO=1: ATTACHMENT_READ_OFF
 *   serial            ICO_VK_FAKE_SERIAL=1: COMPILER_SERIAL where no row sets it
 *   noPrepass         ICO_VK_FAKE_NO_PREPASS=1: VU_PREPASS_OFF where no row sets it
 *   compactArm        ICO_RD_VU_COMPACT_ARM=1: COMPACTION_CLASS the Mali-G52's
 * (ICO_VK_FAKE_LIMITS=moltenvk is RhiDeviceId.moltenvk itself.) */
typedef struct RhiDeviceFakes {
    uint8_t vendorSet;
    uint32_t vendor;
    uint8_t passRowQualcomm, adreno, serial, noPrepass, compactArm;
} RhiDeviceFakes;

/* The fakes' environment as the backend read it (NULL unset), each parsed
 * as its call site parsed it before the table (the Vulkan backend reads them
 * once a device is picked, vk_device_table.c):
 *   vendor      ICO_VK_FAKE_VENDOR: a number strtoul(s, 0) takes whole, at
 *               most 0xFFFFFFFF (vk_device.c's tile rule and subpass class)
 *   passRow     ICO_VK_FAKE_PASS_ROW: "qualcomm"
 *   adreno      ICO_VK_FAKE_ADRENO: set, not empty, not starting with '0'
 *   serial      ICO_VK_FAKE_SERIAL: starting with '1' (vk_pipeline.c)
 *   noPrepass   ICO_VK_FAKE_NO_PREPASS: as adreno
 *   compactArm  ICO_RD_VU_COMPACT_ARM: "1" (the renderer's, rd_vu_compact.c) */
typedef struct RhiDeviceFakeEnv {
    const char *vendor, *passRow, *adreno, *serial, *noPrepass, *compactArm;
} RhiDeviceFakeEnv;

void rhi_device_fakes_parse(const RhiDeviceFakeEnv *env, RhiDeviceFakes *out);

/* the fakes' names (RhiDeviceChoices.faked) */
enum {
    RHI_DT_FAKE_NONE = 0,
    RHI_DT_FAKE_VENDOR,
    RHI_DT_FAKE_PASS_ROW,
    RHI_DT_FAKE_ADRENO,
    RHI_DT_FAKE_SERIAL,
    RHI_DT_FAKE_NO_PREPASS,
    RHI_DT_FAKE_COMPACT_ARM
};

#define RHI_DT_MAX_ROWS 40

typedef struct RhiDeviceChoices {
    int32_t value[RHI_DC_COUNT];
    int16_t row[RHI_DC_COUNT];        /* the setting row's index, -1 the default */
    int16_t setting[RHI_DC_COUNT];    /* its setting's index in that row */
    uint8_t faked[RHI_DC_COUNT];      /* RHI_DT_FAKE_*: a fake set the value */
    uint8_t matched[RHI_DT_MAX_ROWS]; /* the row's match holds */
    uint8_t applied[RHI_DT_MAX_ROWS]; /* matched, and one of its settings in force (a row whose
                                         every setting another row overrides is not) */
    uint8_t rowsMatched, rowsApplied;
    /* two matching rows that set one choice to different values without an
       override link: the first such (choice -1 none) */
    int16_t conflictChoice, conflictA, conflictB;
} RhiDeviceChoices;

/* The table's rows and their count */
const RhiDeviceRow *rhi_device_table_rows(size_t *count);
/* a row's index by its id, -1 none */
int rhi_device_table_find(const char *id);
/* the value the row id sets for choice, or fallback when it sets none */
int32_t rhi_device_table_row_value(const char *id, RhiDeviceChoice choice, int32_t fallback);

/* the choice's name ("TILER") and the words of one of its values
   ("qualcomm", "yes", "2") for the docs and the tests */
const char *rhi_device_choice_name(RhiDeviceChoice choice);
const char *rhi_device_choice_value_name(RhiDeviceChoice choice, int32_t value);
/* the default's words ("no", "by backend", ...) */
const char *rhi_device_choice_default_words(RhiDeviceChoice choice);
/* The words the attempt ledger (port/platform/attempt_ledger.c) gives its
   row where no table row sets the choice on this device, each the phrase
   its decider held before the table, byte for byte; NULL where no ledger
   row asks about the choice:
     PASS_OBJECTS      ico_attempt_decide_pass_objects (not applicable here)
     VU_COMPUTE_CLASS  ico_attempt_decide_vu_compute (not applicable here)
     NO_VOLUME_CLASS   ico_attempt_decide_no_volume_break (not applicable here)
     COMPACTION_CLASS  ico_attempt_decide_compaction (off by default: the
                       class of a device with no compaction row) */
const char *rhi_device_choice_na_words(RhiDeviceChoice choice);

/* one of the named driver tests on id (RHI_DT_DRV_*), and the any-case word
   test they use (s NULL: no) */
int rhi_device_driver_test(int test, const RhiDeviceId *id);
int rhi_device_text_has(const char *s, const char *word);

/* Every choice for id with the fakes (NULL none) applied */
void rhi_device_table_eval(const RhiDeviceId *id, const RhiDeviceFakes *fakes,
                           RhiDeviceChoices *out);
/* One choice for id without fakes, as rhi_device_table_eval gives it (the
   derived facts before it resolved first): the pure predicates that keep
   their signatures (rhi_pass_row.h, rhi_driver_version.h, rhi_tile_rule.h)
   build an identity from their arguments and read their choice here.
   reason (may be NULL): the setting row's reason, NULL the default */
int32_t rhi_device_table_choice(const RhiDeviceId *id, RhiDeviceChoice choice, const char **reason);
/* the reason the setting row gives (RhiDeviceSetting.reason; a fake's own
   words where a fake set it), NULL when the default holds or none */
const char *rhi_device_choice_reason(const RhiDeviceChoices *c, RhiDeviceChoice choice);

/* The call sites' reads (the Vulkan backend's, and the equivalence test's
 * proof of them, port/rhi/test/rhi_device_table_equiv_test.c):
 *   rhi_device_choice_why   the reason where the choice is non-zero, else
 *                           NULL (COMPILER_SERIAL, CAREFUL_PIPELINES,
 *                           VU_PREPASS_OFF, ...)
 *   rhi_device_pass_rows    PASS_OBJECTS as vk_device.c's two driver rows:
 *                           Intel's reason or NULL, Qualcomm's or NULL
 *   rhi_device_subpass_class  SUBPASS_CLASS with the run's passRowQcom
 *                           (Qualcomm's row applied to the passes, after
 *                           ICO_VK_LEGACY_PASSES): the Intel row first, then
 *                           Qualcomm's applied row, then the table's class
 *                           (RHI_DT_SUBPASS_*, rhi_subpass_verdict.h's order)
 *   rhi_device_vu_rows      the VU rows of vk_device.c (the Adreno 740's):
 *                           the fallback's plain set (VU_FALLBACK_PLAIN by the
 *                           table alone; Turnip's is the renderer's), the
 *                           careful set, the pre-pass off, the compute record
 *                           off: each its reason or NULL */
const char *rhi_device_choice_why(const RhiDeviceChoices *c, RhiDeviceChoice choice);
void rhi_device_pass_rows(const RhiDeviceChoices *c, const char **intelRow, const char **qcomRow);
int rhi_device_subpass_class(const RhiDeviceChoices *c, int passRowQcom);

typedef struct RhiDeviceVuRows {
    const char *plain, *careful, *noPrepass, *noComputeRecord;
} RhiDeviceVuRows;

RhiDeviceVuRows rhi_device_vu_rows(const RhiDeviceChoices *c);

/* One evaluation a device (every backend at its start): the identity with
 * the fakes through the table, kept as the current device's choices
 * (rhi_device_table_current; the call sites read them), the summary's clause
 * noted (rhi_device_table_note), and the start-up lines (the header, then a
 * line a row in force) handed to emit one by one, without prefix or newline
 * (the backend logs them with its own).  Returns the current choices. */
typedef void (*RhiDeviceLineFn)(const char *line, void *user);
const RhiDeviceChoices *rhi_device_table_start(const RhiDeviceId *id, const RhiDeviceFakes *fakes,
                                               RhiDeviceLineFn emit, void *user);
/* the current device's choices, NULL before any start */
const RhiDeviceChoices *rhi_device_table_current(void);
/* the start-up lines' buffer (each line cut at RHI_DT_LINE_MAX - 1 bytes) */
#define RHI_DT_LINE_MAX 2048

/* The start-up lines (no prefix, no newline; each cut at cap - 1; the length
 * written):
 *   the header: "device table: <name> (device vvvv:dddd; driver ID n,
 *     version v; Vulkan a.b.c; <OS>): 8 of the table's 27 rows: id, id, ..."
 *     (another backend: "(device vvvv:dddd; OpenGL; <OS>)", no driver ID)
 *     (the rows in force; "; overridden here: id, ..." for rows that matched
 *     with every setting overridden), or "...: none of the table's 27 rows:
 *     every choice as capability and self-tests decide"
 *   a row in force: "device table: <id> (<match words>): <what> (<issue>,
 *     <since>; revisit: <revisit>); ..." (a setting another row or a test's
 *     fake replaced says so: "; overridden by <id>", "; replaced by <env>, a
 *     test") */
size_t rhi_device_table_header(char *out, size_t cap, const RhiDeviceId *id,
                               const RhiDeviceChoices *c);
size_t rhi_device_table_row_line(char *out, size_t cap, const RhiDeviceChoices *c, int row);
/* the summary's clause: "device table rows: 13 of 27 (the start lines name
   them)" or "device table rows: none of 27"; the ids are the start-up
   header's alone (a line of its own, and a key line of report.txt,
   port/platform/report.c): the summary line holds at most
   diag_host.c's 2048 bytes, and issue 123's tablet filled 1810 of them in
   v0.11.6 before the clause (rhi_device_table_test's summary bound).  At most
   RHI_DT_SUMMARY_MAX - 1 bytes. */
#define RHI_DT_SUMMARY_MAX 64
size_t rhi_device_table_summary(char *out, size_t cap, const RhiDeviceChoices *c);
/* The backend keeps its device's clause for the renderer's summary line
   (rd_core.c rd_summary_text): noted at device creation, NULL before any
   (another backend that does not evaluate the table) */
void rhi_device_table_note(const RhiDeviceChoices *c);
const char *rhi_device_table_noted(void);

#endif /* PORT_RHI_RHI_DEVICE_TABLE_H */
