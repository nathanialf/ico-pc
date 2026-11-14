/* rhi_subpass_verdict.h: the subpass read's decision at a Vulkan start and
 * the verdict its probe keeps across starts (port/rhi/vk/vk_device.c
 * vkr_subpass_read_decide reads it, port/render/rd_subpass_probe.c writes
 * it; docs/port/RHI.md, "The subpass read's probe").  No Vulkan header:
 * pure functions and one small file's reads and writes, built and tested
 * on every host (port/rhi/test/rhi_subpass_verdict_test.c).
 *
 * The subpass read (vk_renderpass.c) lets DATE's stencil setup read the
 * scene's colour inside its pass on a device whose passes are VkRenderPass
 * objects without dynamic rendering's local read (issue 179's Mali-G52 on
 * Vulkan 1.1, the Adreno 740 under Qualcomm's row, the Intel driver row).
 * Its shape is the device's for the whole run (every single-sampled RGBA8
 * render target with the input-attachment usage, in GENERAL, every pass of
 * one such target and a depth target with an input attachment and a
 * self-dependency: render pass compatibility makes it the pipelines'
 * too), so it is decided when the device is made, from a verdict an
 * earlier start's probe kept for this device and driver:
 *   kept      on, no probe (DATE's self-test still runs every start)
 *   refused   off: today's path, the R8 snapshot
 *   probing   the start that probed it ended before its verdict (a device
 *             loss, a GPU hang the watchdog ended, a crash, the app ended
 *             by the system): refused for good, the file says so
 *   none      (no file, another driver, another format) on for this start,
 *             the mark "probing" written at once (flushed to the disc), and
 *             the renderer's probe writes the verdict after DATE's
 *             self-test (rd_subpass_probe.c); a clean exit of a start that
 *             never reached the probe takes the mark away again (the next
 *             start probes)
 * Before the verdict, the device's class this release (RhiSubpassClass,
 * RHI_SUBPASS_PROBE_*_DEFAULT): only the Intel driver row's devices are
 * probed; Arm's, Qualcomm's own driver and the others are off by default
 * (no probe, no shape, no file read or written).
 * Off without a probe where it cannot decide: no GPU timestamps (the
 * PowerVR B-Series), no file to keep the verdict in (the tools and the
 * tests: no pipeline cache file, unless ICO_VK_SUBPASS_VERDICT names one).
 * [dev] subpass_read (ICO_VK_SUBPASS_READ) forces it: 1 on without the
 * probe, 0 off; ICO_VK_FAKE_NO_DST keeps it off.
 *
 * The file: one line, "v1 <key> <state>: <why>\n", the key the device's
 * vendor, device and driver version ("13b5:72120000:80200000",
 * rhi_subpass_verdict_key), next to the pipeline cache file
 * (rhi_subpass_verdict_path).  A line of another key or format is "none":
 * a driver update probes again. */
#ifndef PORT_RHI_RHI_SUBPASS_VERDICT_H
#define PORT_RHI_RHI_SUBPASS_VERDICT_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#define RHI_SUBPASS_VERDICT_NAME "ico-subpass-read.txt"
#define RHI_SUBPASS_VERDICT_MAX 512 /* a line, at most */

typedef enum RhiSubpassVerdict {
    RHI_SUBPASS_NONE = 0,
    RHI_SUBPASS_PROBING,
    RHI_SUBPASS_KEPT,
    RHI_SUBPASS_REFUSED,
} RhiSubpassVerdict;

/* what the device does at this start (rhi_subpass_decide) */
typedef enum RhiSubpassDecision {
    RHI_SUBPASS_OFF = 0,     /* today's path */
    RHI_SUBPASS_ON,          /* the read on, no probe */
    RHI_SUBPASS_PROBE,       /* the read on for this start's probe */
    RHI_SUBPASS_OFF_DEFAULT, /* off by default on this class: no probe, no shape */
} RhiSubpassDecision;

/* The device classes and whether the probe runs at their first start this
 * release (the owner's decision for v0.11.6; an rc variant flips one): the
 * Intel driver row's devices are probed; Arm's (the Mali-G52 of issue 179)
 * and Qualcomm's own driver (the Adreno 740 under Qualcomm's row) are not:
 * both lost the device before, and the read is a new kind of GPU work at
 * start-up for one pass a picture; any other VkRenderPass device (a [dev]
 * legacy_passes desktop, another vendor's Vulkan 1.1 driver) neither.  Off
 * by default means no probe, no shape: today's snapshot path, nothing new
 * at start-up.  [dev] subpass_read = on still forces the read on any of
 * them; ICO_VK_SUBPASS_PROBE_CLASS=1|0 (the tests alone) overrides a class's
 * default, so the probe runs under the Arm fakes on lavapipe. */
#define RHI_SUBPASS_PROBE_INTEL_DEFAULT 1
#define RHI_SUBPASS_PROBE_ARM_DEFAULT 0
#define RHI_SUBPASS_PROBE_QCOM_DEFAULT 0
#define RHI_SUBPASS_PROBE_OTHER_DEFAULT 0

typedef enum RhiSubpassClass {
    RHI_SUBPASS_CLASS_OTHER = 0,
    RHI_SUBPASS_CLASS_INTEL, /* the Intel driver row (rhi_pass_row.h) */
    RHI_SUBPASS_CLASS_ARM,   /* vendor 0x13B5 */
    RHI_SUBPASS_CLASS_QCOM,  /* Qualcomm's own driver */
} RhiSubpassClass;

/* the class's default this release: 1 the probe runs, 0 off by default */
static inline int rhi_subpass_class_default(RhiSubpassClass c)
{
    return c == RHI_SUBPASS_CLASS_INTEL  ? RHI_SUBPASS_PROBE_INTEL_DEFAULT
           : c == RHI_SUBPASS_CLASS_ARM  ? RHI_SUBPASS_PROBE_ARM_DEFAULT
           : c == RHI_SUBPASS_CLASS_QCOM ? RHI_SUBPASS_PROBE_QCOM_DEFAULT
                                         : RHI_SUBPASS_PROBE_OTHER_DEFAULT;
}

/* the words of a class off by default (the device line's and the attempt
 * ledger's why: "off by default (<these>)") */
#define RHI_SUBPASS_OFF_LOST_WORDS                                                                 \
    "off on this driver this release: a new kind of GPU work at start-up on a driver that "        \
    "lost the device before; one pass a picture"
#define RHI_SUBPASS_OFF_OTHER_WORDS "probed on the Intel driver row only this release"

static inline const char *rhi_subpass_class_off_words(RhiSubpassClass c)
{
    return c == RHI_SUBPASS_CLASS_ARM || c == RHI_SUBPASS_CLASS_QCOM ? RHI_SUBPASS_OFF_LOST_WORDS
                                                                     : RHI_SUBPASS_OFF_OTHER_WORDS;
}

/* what rhi_subpass_decide asks the caller to write before it goes on */
typedef enum RhiSubpassWrite {
    RHI_SUBPASS_WRITE_NOTHING = 0,
    RHI_SUBPASS_WRITE_PROBING, /* the mark */
    RHI_SUBPASS_WRITE_REFUSED, /* an earlier start ended inside its probe */
} RhiSubpassWrite;

static inline const char *rhi_subpass_verdict_word(RhiSubpassVerdict v)
{
    return v == RHI_SUBPASS_PROBING   ? "probing"
           : v == RHI_SUBPASS_KEPT    ? "kept"
           : v == RHI_SUBPASS_REFUSED ? "refused"
                                      : "none";
}

/* "vvvv:dddddddd:rrrrrrrr" (hex): the device and its driver */
static inline void rhi_subpass_verdict_key(char *out, size_t cap, uint32_t vendor, uint32_t device,
                                           uint32_t driverVersion)
{
    if (cap > 0) {
        snprintf(out, cap, "%04x:%08x:%08x", (unsigned)vendor, (unsigned)device,
                 (unsigned)driverVersion);
    }
}

/* the file beside the pipeline cache file cachePath (its folder: the part
 * up to the last '/' or '\\'; none: the current folder); 0 when cachePath
 * is NULL or empty, or the path does not fit */
static inline int rhi_subpass_verdict_path(char *out, size_t cap, const char *cachePath)
{
    if (cap == 0 || cachePath == NULL || cachePath[0] == '\0') {
        return 0;
    }
    const char *slash = NULL;
    for (const char *c = cachePath; *c; c++) {
        if (*c == '/' || *c == '\\') {
            slash = c;
        }
    }
    const size_t dir = slash ? (size_t)(slash - cachePath) + 1u : 0u;
    const size_t need = dir + sizeof(RHI_SUBPASS_VERDICT_NAME);
    if (need > cap) {
        return 0;
    }
    memcpy(out, cachePath, dir);
    memcpy(out + dir, RHI_SUBPASS_VERDICT_NAME, sizeof(RHI_SUBPASS_VERDICT_NAME));
    return 1;
}

/* The verdict text holds for key (RHI_SUBPASS_NONE for another key, another
 * format, an unknown state or no text), its why copied into why (cap
 * bytes, "" when none; the line's end not included) */
static inline RhiSubpassVerdict rhi_subpass_verdict_parse(const char *text, const char *key,
                                                          char *why, size_t whyCap)
{
    if (whyCap > 0) {
        why[0] = '\0';
    }
    if (text == NULL || key == NULL || strncmp(text, "v1 ", 3) != 0) {
        return RHI_SUBPASS_NONE;
    }
    const char *p = text + 3;
    const size_t kn = strlen(key);
    if (kn == 0 || strncmp(p, key, kn) != 0 || p[kn] != ' ') {
        return RHI_SUBPASS_NONE;
    }
    p += kn + 1;
    static const RhiSubpassVerdict kStates[] = {RHI_SUBPASS_PROBING, RHI_SUBPASS_KEPT,
                                                RHI_SUBPASS_REFUSED};
    RhiSubpassVerdict v = RHI_SUBPASS_NONE;
    for (size_t i = 0; i < sizeof kStates / sizeof kStates[0]; i++) {
        const char *w = rhi_subpass_verdict_word(kStates[i]);
        const size_t wn = strlen(w);
        if (strncmp(p, w, wn) == 0 && p[wn] == ':') {
            v = kStates[i];
            p += wn + 1;
            break;
        }
    }
    if (v == RHI_SUBPASS_NONE) {
        return RHI_SUBPASS_NONE;
    }
    while (*p == ' ') {
        p++;
    }
    size_t n = 0;
    while (p[n] && p[n] != '\n' && p[n] != '\r') {
        n++;
    }
    if (whyCap > 0) {
        if (n >= whyCap) {
            n = whyCap - 1u;
        }
        memcpy(why, p, n);
        why[n] = '\0';
    }
    return v;
}

/* the file's line for key in state v with why (NULL ""); the length
 * written, as snprintf's */
static inline int rhi_subpass_verdict_line(char *out, size_t cap, const char *key,
                                           RhiSubpassVerdict v, const char *why)
{
    return snprintf(out, cap, "v1 %s %s: %s\n", key ? key : "", rhi_subpass_verdict_word(v),
                    why ? why : "");
}

/* The decision at a start of a device whose passes are VkRenderPass
 * objects (the caller asks nothing elsewhere).  ask: [dev] subpass_read
 * (-1 unset, 0 off, 1 on: before anything else, as since v0.11.3); route:
 * the device has no local read (the read's route; with local read the
 * automatic decision is off); fakeNoDst:
 * ICO_VK_FAKE_NO_DST; haveFile: a verdict file is named; timestamps: the
 * queue writes GPU timestamps; cls: the device's class and classEnv the
 * tests' override of its default (-1 unset, 0, 1), after the timestamps and
 * before the verdict (a class off by default reads no verdict and writes
 * nothing); prev: the file's verdict for this device
 * and driver (prevWhy its words, NULL ""): a device made again in the same
 * run finds the mark this run wrote only after a loss (a clean shutdown
 * takes it away), which ends the probe as a loss in another run would.
 * The decision's words go into why (cap bytes), and *write says what to
 * write to the file before going on. */
static inline RhiSubpassDecision rhi_subpass_decide(int ask, int route, int fakeNoDst, int haveFile,
                                                    int timestamps, RhiSubpassClass cls,
                                                    int classEnv, RhiSubpassVerdict prev,
                                                    const char *prevWhy, char *why, size_t cap,
                                                    RhiSubpassWrite *write)
{
    *write = RHI_SUBPASS_WRITE_NOTHING;
    const char *pw = prevWhy && prevWhy[0] ? prevWhy : "no words kept";
    if (ask == 0) {
        snprintf(why, cap, "[dev] subpass_read = off (ICO_VK_SUBPASS_READ=0)");
        return RHI_SUBPASS_OFF;
    }
    if (fakeNoDst) {
        snprintf(why, cap, "ICO_VK_FAKE_NO_DST%s",
                 ask == 1 ? ", over [dev] subpass_read = on" : "");
        return RHI_SUBPASS_OFF;
    }
    if (ask == 1) {
        snprintf(why, cap,
                 "[dev] subpass_read = on (ICO_VK_SUBPASS_READ=1): no probe, DATE's self-test "
                 "still decides");
        return RHI_SUBPASS_ON;
    }
    if (!route) {
        snprintf(why, cap, "the device's passes read their attachments by local read");
        return RHI_SUBPASS_OFF;
    }
    if (!haveFile) {
        snprintf(why, cap,
                 "no file to keep the probe's verdict in (no pipeline cache file: the tools and "
                 "the tests)");
        return RHI_SUBPASS_OFF;
    }
    if (!timestamps) {
        snprintf(why, cap,
                 "no GPU timestamps: the probe cannot time it, so DATE keeps its R8 snapshot");
        return RHI_SUBPASS_OFF;
    }
    if (classEnv == 0 || (classEnv != 1 && !rhi_subpass_class_default(cls))) {
        snprintf(why, cap, "%s",
                 classEnv == 0 ? "ICO_VK_SUBPASS_PROBE_CLASS=0: the tests' switch"
                               : rhi_subpass_class_off_words(cls));
        return RHI_SUBPASS_OFF_DEFAULT;
    }
    switch (prev) {
    case RHI_SUBPASS_KEPT:
        snprintf(why, cap, "kept by the probe of an earlier start (%.120s)", pw);
        return RHI_SUBPASS_ON;
    case RHI_SUBPASS_REFUSED:
        snprintf(why, cap, "refused by the probe of an earlier start (%.120s)", pw);
        return RHI_SUBPASS_OFF;
    case RHI_SUBPASS_PROBING:
        *write = RHI_SUBPASS_WRITE_REFUSED;
        snprintf(why, cap,
                 "refused for good: the start that probed it ended before its verdict (a device "
                 "loss, a GPU hang, a crash or the app ended; %.120s)",
                 pw);
        return RHI_SUBPASS_OFF;
    default:
        break;
    }
    *write = RHI_SUBPASS_WRITE_PROBING;
    snprintf(why, cap,
             "no verdict for this device and driver yet: on for this start's probe (DATE's "
             "self-test, then the read timed against the R8 snapshot)");
    return RHI_SUBPASS_PROBE;
}

/* The file's text into text (cap bytes, "" when there is none); 1 when it
 * was read */
static inline int rhi_subpass_verdict_read(const char *path, char *text, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    text[0] = '\0';
    FILE *fp = path && path[0] ? fopen(path, "rb") : NULL;
    if (fp == NULL) {
        return 0;
    }
    const size_t n = fread(text, 1, cap - 1u, fp);
    text[n] = '\0';
    fclose(fp);
    return 1;
}

/* Writes line as the file's whole text and flushes it to the disc (a mark
 * must outlive a GPU hang the watchdog ends); line NULL removes the file.
 * 1 on success */
static inline int rhi_subpass_verdict_write(const char *path, const char *line)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (line == NULL) {
        return remove(path) == 0;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return 0;
    }
    const size_t n = strlen(line);
    int ok = fwrite(line, 1, n, fp) == n && fflush(fp) == 0;
#if defined(_WIN32)
    ok = ok && _commit(_fileno(fp)) == 0;
#else
    ok = ok && fsync(fileno(fp)) == 0;
#endif
    return fclose(fp) == 0 && ok;
}

#endif
