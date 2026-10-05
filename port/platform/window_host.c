/*
 * port/platform/window_host.c
 *
 * The window, the renderer device on it and the real-time pacing
 * (window_host.h).
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "host_config.h"
#include "host_fs.h"
#include "host_loop.h"
#include "hotkeys.h"
#include "input_record.h"
#include "input_sdl.h"
#include "rd.h"
#include "rd_tex.h"
#include "rhi.h"
#include "sched.h"
#include "trace_host.h"
#include "ui_host.h"
#include "video_options.h"
#include "window_host.h"

/* The window's first client size: 4:3, three times 320 x 240. */
#define WINDOW_W 960
#define WINDOW_H 720
/* A host this far behind the vsync deadlines stops trying to catch up. */
#define RESYNC_NS 100000000ull
/* Package Q1: slow-step lines a stats block logs at most (the rest are
   counted in its first line) */
#define SLOW_STEP_LINES 5

_Static_assert(SDLK_F11 == ICO_HOTKEY_KEY_F11, "hotkeys.h: SDL3's SDLK_F11");

_Static_assert(SDLK_F12 == ICO_HOTKEY_KEY_F12, "hotkeys.h: SDL3's SDLK_F12");

/* Package Q1: F12's dump (port/render/rd_dump.c; rd.h is outside this
   package, so it is declared here) */
bool rd_DumpOnDemand(const char *dumpPath, const char *pngPath);
/* The game's state the mouse capture follows (common/include/main.h): the
   boy exists in a stage, and the game is neither paused nor loading. */
extern void *boyGObj;
extern int game_pause;
extern int data_loading;

static SDL_Window *s_window;

static int s_captured;

static Uint64 s_deadline;

static int s_open;

/* renderer wave 7 (R7a): the display options last applied
   (port/game/video_options.h, docs/port/DISPLAY.md) */
static unsigned s_videoSerial;

static int s_fullscreen;

/* renderer wave 7 (R7b): the presentation loop (ico_window_pace) */
static struct {
    int framerate;           /* ico_video_framerate() as last applied */
    unsigned cutSerial;      /* ico_video_cut_serial() last passed on */
    uint32_t frame;          /* rd_FrameNumber() last seen */
    uint32_t presentedFrame; /* the frame of the last present */
    Uint64 tickAt, tickPrev; /* the pace deadlines (simulated time) the last two frames closed at */
    Uint64 lastPresent;
    Uint64 cost; /* how long the last rd_Present took */
    /* the rate log */
    Uint64 statAt;
    unsigned statPresents, statFrames;
    /* F2: simulated vsyncs paced, and the lag the pacer dropped (each time
       it was more than RESYNC_NS behind: the simulation ran slower than
       real time by that much) */
    unsigned statVsyncs, statResyncs;
    Uint64 statDropped;
    uint32_t statFrameNo; /* rd_FrameNumber() at the block's start */
    /* P1: the simulation step's real time (from the end of one pace to the
       start of the next), for the 10 s line */
    Uint64 paceEnd;
    double stepSumMs, stepMaxMs;
    unsigned stepCount;
    int mailbox; /* rhi_PreferMailbox as last applied: vsync on and presenting between ticks */
    /* Q1: the slow-step lines ([dev] slow_step_ms, 0 off): the threshold,
       the block's count and lines, ico_window_pump's time, the renderer's
       texture decodes and pipeline creations at the last pace's end */
    double slowMs, pumpMs;
    unsigned slowSteps, slowLogged;
    uint32_t decodes, pipeCreates;
    /* S2: the present clock alpha is taken from (rd.h RdPresentClock) */
    RdPresentClock clock;
    int legacyAlpha; /* ICO_RD_S2_LEGACY=1 (read in video_settings) */
    /* Q1: F11, the stats line every second until this time (0: off) */
    Uint64 fastUntil;
} s_pres;

/* P1: the renderer's per-replay records (rd.h RdPerfRecord): summed over
   the 10 s block for the window's second line, and with [dev] perf_log =
   true written one line each into logs/ico-pc-perf.csv
   (docs/port/RENDER_API.md "Performance") */
static struct {
    int csvTried;
    FILE *csv;
    unsigned n, gpuN;
    double total, maxTotal, interp, wait, acquire, upload, walk, bind, submit, present, readback,
        fence;
    double gpu, maxGpu, gpuList[13], gpuUpload, gpuPresent;
    uint64_t draws, passes, pipeBinds, groupBinds, groups, barriers, copies, bytes, meshBytes;
    uint64_t texUploads, meshUploads, dateSnaps, exact, pipeCreates;
    uint64_t bufCreated, bufDestroyed, texCreated, texDestroyed, allocs, fenceWaits, waitIdles,
        readbacks;
} s_perf;

/* The renderer's settings from the display options and the window's pixel
   size. */
static void video_settings(RdSettings *rs, int w, int h)
{
    IcoVideoOptions o;

    ico_video_get(&o);
    ico_video_set_window(w, h);
    memset(rs, 0, sizeof(*rs));
    rs->preset = o.preset == ICO_VIDEO_ENHANCED ? RD_PRESET_ENHANCED : RD_PRESET_ORIGINAL;
    rs->outputWidth = (uint32_t)(w > 0 ? w : WINDOW_W);
    rs->outputHeight = (uint32_t)(h > 0 ? h : WINDOW_H);
    rs->aspect = ico_video_aspect();
    rs->vsync = (uint8_t)(o.vsync != 0);
    rs->filterUpgrade = (uint8_t)o.filter;
    rs->fullHeightScene = (uint8_t)(o.fullHeight != 0);
    rs->sceneWidth = (uint32_t)o.resW;
    rs->sceneHeight = (uint32_t)o.resH;
    rs->sceneScale = (float)o.resScale;
    /* R7b: rd presents between ticks, in both presets (F2) */
    {
        const char *e = getenv("ICO_RD_S2_LEGACY");

        s_pres.legacyAlpha = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    s_pres.framerate = ico_video_framerate();
    rs->interpolate = (uint8_t)(s_pres.framerate != ICO_FRAMERATE_ORIGINAL);
    /* P1: presenting between ticks with vsync on, the swapchain prefers the
       mailbox mode: no tearing, and a present never waits for the display,
       so it cannot hold the simulation back (DISPLAY.md) */
    s_pres.mailbox = rs->vsync && rs->interpolate;
    rhi_PreferMailbox(s_pres.mailbox != 0);
}

/* Applies the options changed since the last call (the Settings menu's
   ico_video_set, Alt+Enter; force: a resize, for aspect "auto" and
   resolution "window"): fullscreen through SDL, the rest through
   rd_SetSettings at the next frame (rd recreates the targets and the
   swapchain as needed). */
static void video_apply(int force)
{
    IcoVideoOptions o;
    RdSettings rs;
    int w = 0, h = 0;

    if (!force && ico_video_serial() == s_videoSerial) {
        return;
    }
    s_videoSerial = ico_video_serial();
    ico_video_get(&o);
    if (o.fullscreen != s_fullscreen) {
        /* no mode set: SDL's borderless fullscreen at the desktop
           resolution; the resize event follows */
        SDL_SetWindowFullscreen(s_window, o.fullscreen != 0);
        s_fullscreen = o.fullscreen;
    }
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    const int mailbox = s_pres.mailbox;
    video_settings(&rs, w, h);
    rd_SetSettings(&rs);
    if (mailbox != s_pres.mailbox && w > 0 && h > 0) {
        /* P1: the present mode follows the frame rate option (the swapchain
           is recreated) */
        rd_ResizeOutput((uint32_t)w, (uint32_t)h);
    }
}

int ico_window_open(unsigned int gsW, unsigned int gsH)
{
    RdSettings rs;
    int w = 0, h = 0;

    /* [video] backend (config.toml) or backend= (ico-pc.ini): "vulkan", the
       default, or "d3d12" where the build has it */
    const char *backend = ico_config_get_string("video.backend", "vulkan");
    if (!rhi_CreateBackend(backend)) {
        fprintf(stderr, "window: renderer backend \"%s\" is not in this build; using vulkan\n",
                backend);
        backend = "vulkan";
        rhi_CreateBackend(backend);
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "window: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    s_window = SDL_CreateWindow("ICO", WINDOW_W, WINDOW_H,
                                (rhi_Backend() == RHI_BACKEND_VULKAN ? SDL_WINDOW_VULKAN : 0) |
                                    SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (s_window == NULL) {
        fprintf(stderr, "window: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    /* the replay, the present and the FMV picture run on the host stack in
       the host FP mode, not on the game's 256 KB fiber stacks
       (docs/port/PLATFORM.md "Fiber stacks and host calls") */
    rd_SetHostCall(ico_sched_call_on_host);
    {
        IcoVideoOptions o;

        ico_video_get(&o);
        s_videoSerial = ico_video_serial();
        if (o.fullscreen) {
            SDL_SetWindowFullscreen(s_window, true);
            SDL_SyncWindow(s_window);
            s_fullscreen = 1;
        }
    }
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    video_settings(&rs, w, h);
    if (!rd_Init(gsW, gsH, &rs, s_window)) {
        fprintf(stderr,
                "window: no usable %s device (rd_Init failed; the rhi lines above say "
                "why)\n",
                backend);
        SDL_DestroyWindow(s_window);
        s_window = NULL;
        SDL_Quit();
        return -1;
    }
    /* the whole reachable pipeline set before the first frame, so the
       game never waits on a pipeline compile (F2; the time is logged).  P1:
       after rd_Init, which makes the device: F2 called it before, where it
       created nothing, and every pipeline was compiled by the replay that
       first drew with it (tens of ms each on a GPU driver) */
    rd_PrecreatePipelines();
    {
        IcoVideoOptions o;
        char res[32], fr[16];

        ico_video_get(&o);
        fprintf(stderr,
                "window: %dx%d pixels%s, %s on %s, %s preset (resolution %s, aspect %s, "
                "texture filter %s, %s height, framerate %s), vsync %s\n",
                w, h, o.fullscreen ? " fullscreen" : "",
                rhi_Backend() == RHI_BACKEND_D3D12 ? "D3D12" : "Vulkan", rhi_AdapterName(),
                o.preset == ICO_VIDEO_ENHANCED ? "Enhanced" : "Original",
                ico_video_resolution_name(&o, res, sizeof(res)), ico_video_aspect_name(o.aspect),
                ico_video_filter_name(o.filter), o.fullHeight ? "full" : "half",
                ico_video_framerate_name(ico_video_framerate(), fr, sizeof(fr)),
                o.vsync ? "on" : "off");
        if (o.vsync) {
            fprintf(stderr, "window: present mode %s\n",
                    rhi_PresentMailbox() ? "mailbox (vsync without waiting on the display)"
                                         : "fifo");
        }
    }
    s_pres.cutSerial = ico_video_cut_serial();
    /* Q1: [dev] slow_step_ms (default 8; 0 off), docs/port/CONFIG.md */
    s_pres.slowMs = ico_config_get_float("dev.slow_step_ms", 8.0);
    s_deadline = SDL_GetTicksNS();
    s_open = 1;
    /* Phase 6 (6B): the port's runtime text and popups (port/ui) */
    ui_HostInit();
    {
        char dir[ICO_PATH_MAX], path[ICO_PATH_MAX];

        ico_host_pref_dir(dir, sizeof(dir));
        ico_path_join(path, sizeof(path), dir, "config.toml");
        ico_input_sdl_init(path);
    }
    return 0;
}

static void set_capture(int want)
{
    if (want == s_captured) {
        return;
    }
    s_captured = want;
    SDL_SetWindowRelativeMouseMode(s_window, want != 0);
    ico_input_sdl_set_capture(want);
}

static void toggle_fullscreen(void)
{
    /* the fullscreen option flips (in memory, not saved; the Settings menu
       sees it): video_apply sets SDL's borderless fullscreen at the desktop
       resolution and the presenter boxes the picture (rd_ResizeOutput
       follows) */
    IcoVideoOptions o;

    ico_video_get(&o);
    o.fullscreen = !s_fullscreen;
    ico_video_set(&o);
    video_apply(0);
}

/* B3: the renderer's device was removed, reset or hung (the driver
   crashed or was updated, the GPU was unplugged): nothing more can be drawn,
   so instead of a frozen window the player gets one message and the session
   ends through the normal quit path (atexit closes the window). The backend
   logged the reason. */
static int device_lost_quit(void)
{
    if (!rhi_DeviceLost()) {
        return 0;
    }
    fprintf(stderr, "window: the graphics device was lost; quitting\n");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ICO PC",
                             "The graphics device stopped responding (the driver was reset or "
                             "the GPU was removed).\n\nThe game has to close. Your last save on "
                             "the memory card is kept; logs/ico-pc.log names the reason.",
                             s_window);
    return 1;
}

/* Q1: F12, the frame on the screen as an rd dump and a PNG in
   <pref>/dumps, for a bug report (docs/port/TESTING.md) */
static void frame_dump(void)
{
    char pref[ICO_PATH_MAX], dir[ICO_PATH_MAX], dump[ICO_PATH_MAX], png[ICO_PATH_MAX];
    char stamp[32];
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);

    if (tm == NULL || strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm) == 0) {
        snprintf(stamp, sizeof(stamp), "unknown");
    }
    ico_host_pref_dir(pref, sizeof(pref));
    ico_path_join(dir, sizeof(dir), pref, "dumps");
    ico_make_dir(dir);
    ico_frame_dump_paths(dir, stamp, ico_host_vsync_count(), dump, sizeof(dump), png, sizeof(png));
    const int ok = rd_DumpOnDemand(dump, png);
    /* the recording up to this moment, for the same report */
    ico_input_record_flush();
    fprintf(stderr, "window: F12 at vsync %u, Main tick %u, frame %u: %s %s and %s\n",
            ico_host_vsync_count(), ico_host_main_ticks(), (unsigned)rd_FrameNumber(),
            ok ? "wrote" : "could not write all of", dump, png);
}

/* Q1: F11, the stats lines every second for 30 s */
static void stats_fast_toggle(void)
{
    s_pres.fastUntil = ico_stats_fast_toggle(SDL_GetTicksNS(), s_pres.fastUntil);
    fprintf(stderr, "window: F11: the stats lines every %s\n",
            s_pres.fastUntil ? "second for 30 s" : "10 s again");
}

int ico_window_pump(void)
{
    SDL_Event e;
    int quit = 0;
    const Uint64 t0 = SDL_GetTicksNS();

    if (device_lost_quit()) {
        return 0;
    }

    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            quit = 1;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ico_hotkey_for(e.key.key, e.key.repeat) == ICO_HOTKEY_FRAME_DUMP) {
                frame_dump();
            } else if (ico_hotkey_for(e.key.key, e.key.repeat) == ICO_HOTKEY_STATS_FAST) {
                stats_fast_toggle();
            } else if (e.key.key == SDLK_F11 || e.key.key == SDLK_F12) {
                /* a held key's repeats: nothing */
            } else if (e.key.key == SDLK_ESCAPE) {
                quit = 1;
            } else if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) &&
                       (e.key.mod & SDL_KMOD_ALT) != 0) {
                if (!e.key.repeat) {
                    toggle_fullscreen();
                }
            } else {
                ico_input_sdl_event(&e);
            }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (e.window.data1 > 0 && e.window.data2 > 0) {
                rd_ResizeOutput((uint32_t)e.window.data1, (uint32_t)e.window.data2);
                /* R7a: aspect "auto" and resolution "window" follow the size */
                ico_video_set_window(e.window.data1, e.window.data2);
                video_apply(1);
            }
            break;
        default:
            ico_input_sdl_event(&e);
            break;
        }
    }
    video_apply(0); /* R7a: the Settings menu's changes */
    /* R7b: a hard camera cut or stage change the game signalled during the
       step that just ran: the frame it recorded is not blended from the
       one before (the step closes the previous frame before the game's
       threads run, so the open frame is the one the cut belongs to) */
    if (ico_video_cut_serial() != s_pres.cutSerial) {
        s_pres.cutSerial = ico_video_cut_serial();
        rd_CameraCut();
    }
    set_capture((SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0 && boyGObj != NULL &&
                game_pause == 0 && data_loading == 0);
    ico_input_sdl_update();
    /* Phase 6 (6B): the popup overlay's clock, and the popup recorded into
       the open frame once per game frame.  rd presents inside rd_EndFrame
       and has no post-present overlay hook yet, so the popup is drawn into
       the frame's list 12 (port/ui/popup.h, docs/port/UI.md) */
    ui_HostVsync(ico_host_main_ticks());
    s_pres.pumpMs = (double)(SDL_GetTicksNS() - t0) / 1e6;
    return !quit;
}

/* P1: logs/ico-pc-perf.csv, opened on the first record when [dev] perf_log
   is true */
static void perf_csv_open(void)
{
    char dir[ICO_PATH_MAX], logs[ICO_PATH_MAX], path[ICO_PATH_MAX];

    s_perf.csvTried = 1;
    if (!ico_config_get_bool("dev.perf_log", 0)) {
        return;
    }
    ico_host_exe_dir(dir, sizeof(dir));
    ico_path_join(logs, sizeof(logs), dir, "logs");
    ico_path_join(path, sizeof(path), logs, "ico-pc-perf.csv");
    s_perf.csv = ico_fopen(path, "w"); /* a UTF-8 path (host_fs.h) */
    if (s_perf.csv == NULL) {
        fprintf(stderr, "window: perf_log: cannot write %s\n", path);
        return;
    }
    fprintf(stderr, "window: perf_log: one line per replay in %s\n", path);
    fprintf(s_perf.csv,
            "replay,frame,interpolated,keep,presented,total_ms,interp_ms,wait_ms,acquire_ms,"
            "upload_ms,walk_ms,bind_ms,submit_ms,present_ms,readback_ms,fence_wait_ms,"
            "buffers_created,buffers_destroyed,textures_created,textures_destroyed,mem_allocs,"
            "mem_frees,bind_groups,pipeline_binds,bind_group_binds,draws,render_passes,barriers,"
            "copies,fence_waits,wait_idles,readbacks,texture_uploads,mesh_uploads,temp_clears,"
            "date_snapshots,exact_blends,pipeline_creates,upload_bytes,mesh_upload_bytes,gpu_valid,"
            "gpu_ms,"
            "gpu_upload_ms");
    for (int l = 0; l < 13; l++) {
        fprintf(s_perf.csv, ",gpu_list%d_ms", l);
    }
    fprintf(s_perf.csv, ",gpu_present_ms,start_ms,alpha,first_of_tick\n");
}

static void perf_csv_line(const RdPerfRecord *r)
{
    FILE *f = s_perf.csv;

    fprintf(f,
            "%u,%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%u,%u,"
            "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%llu,%llu,%u,%.3f,%.3f",
            r->replay, r->frame, r->interpolated, r->keep, r->presented, r->totalMs, r->interpMs,
            r->waitMs, r->acquireMs, r->uploadMs, r->walkMs, r->bindMs, r->submitMs, r->presentMs,
            r->readbackMs, r->fenceWaitMs, r->buffersCreated, r->buffersDestroyed,
            r->texturesCreated, r->texturesDestroyed, r->memoryAllocs, r->memoryFrees,
            r->bindGroups, r->pipelineBinds, r->bindGroupBinds, r->draws, r->renderPasses,
            r->barriers, r->copies, r->fenceWaits, r->waitIdles, r->readbacks, r->textureUploads,
            r->meshUploads, r->tempClears, r->dateSnapshots, r->exactBlends, r->pipelineCreates,
            (unsigned long long)r->uploadBytes, (unsigned long long)r->meshUploadBytes, r->gpuValid,
            r->gpuMs, r->gpuUploadMs);
    for (int l = 0; l < 13; l++) {
        fprintf(f, ",%.3f", r->gpuListMs[l]);
    }
    fprintf(f, ",%.3f,%.3f,%.4f,%u\n", r->gpuPresentMs, r->startMs, (double)r->alpha,
            r->firstOfTick);
}

/* P1: the finished records into the block's sums and the CSV */
static void perf_drain(void)
{
    RdPerfRecord r;

    while (rd_PerfPop(&r)) {
        if (!s_perf.csvTried) {
            perf_csv_open();
        }
        if (s_perf.csv != NULL) {
            perf_csv_line(&r);
        }
        s_perf.n++;
        s_perf.total += r.totalMs;
        s_perf.maxTotal = r.totalMs > s_perf.maxTotal ? r.totalMs : s_perf.maxTotal;
        s_perf.interp += r.interpMs;
        s_perf.wait += r.waitMs;
        s_perf.acquire += r.acquireMs;
        s_perf.upload += r.uploadMs;
        s_perf.walk += r.walkMs;
        s_perf.bind += r.bindMs;
        s_perf.submit += r.submitMs;
        s_perf.present += r.presentMs;
        s_perf.readback += r.readbackMs;
        s_perf.fence += r.fenceWaitMs;
        if (r.gpuValid) {
            s_perf.gpuN++;
            s_perf.gpu += r.gpuMs;
            s_perf.maxGpu = r.gpuMs > s_perf.maxGpu ? r.gpuMs : s_perf.maxGpu;
            s_perf.gpuUpload += r.gpuUploadMs;
            s_perf.gpuPresent += r.gpuPresentMs;
            for (int l = 0; l < 13; l++) {
                s_perf.gpuList[l] += r.gpuListMs[l];
            }
        }
        s_perf.draws += r.draws;
        s_perf.passes += r.renderPasses;
        s_perf.pipeBinds += r.pipelineBinds;
        s_perf.groupBinds += r.bindGroupBinds;
        s_perf.groups += r.bindGroups;
        s_perf.barriers += r.barriers;
        s_perf.copies += r.copies;
        s_perf.bytes += r.uploadBytes;
        s_perf.meshBytes += r.meshUploadBytes;
        s_perf.texUploads += r.textureUploads;
        s_perf.meshUploads += r.meshUploads;
        s_perf.dateSnaps += r.dateSnapshots;
        s_perf.exact += r.exactBlends;
        s_perf.pipeCreates += r.pipelineCreates;
        s_perf.bufCreated += r.buffersCreated;
        s_perf.bufDestroyed += r.buffersDestroyed;
        s_perf.texCreated += r.texturesCreated;
        s_perf.texDestroyed += r.texturesDestroyed;
        s_perf.allocs += r.memoryAllocs;
        s_perf.fenceWaits += r.fenceWaits;
        s_perf.waitIdles += r.waitIdles;
        s_perf.readbacks += r.readbacks;
    }
    if (s_perf.csv != NULL) {
        fflush(s_perf.csv);
    }
}

/* P1: a new block's sums (the CSV stays open) */
static void perf_reset(void)
{
    FILE *csv = s_perf.csv;
    const int tried = s_perf.csvTried;

    memset(&s_perf, 0, sizeof(s_perf));
    s_perf.csv = csv;
    s_perf.csvTried = tried;
    s_pres.stepSumMs = s_pres.stepMaxMs = 0.0;
    s_pres.stepCount = 0;
}

/* P1: the block's second line: where a replay's time went, on average */
static void perf_log(void)
{
    const double n = s_perf.n ? (double)s_perf.n : 1.0;
    const double g = s_perf.gpuN ? (double)s_perf.gpuN : 1.0;
    char gpu[160] = "no GPU timestamps";

    if (s_perf.gpuN) {
        double scene = 0.0;

        for (int l = 0; l < 11; l++) {
            scene += s_perf.gpuList[l];
        }
        snprintf(gpu, sizeof(gpu),
                 "GPU %.2f ms (max %.2f): uploads %.2f, lists 0-10 %.2f, 11-12 %.2f, present %.2f",
                 s_perf.gpu / g, s_perf.maxGpu, s_perf.gpuUpload / g, scene / g,
                 (s_perf.gpuList[11] + s_perf.gpuList[12]) / g, s_perf.gpuPresent / g);
    }
    fprintf(stderr,
            "window: %u replays: CPU %.2f ms (max %.2f): interp %.2f, wait %.2f, acquire %.2f, "
            "upload %.2f, walk %.2f, bind %.2f, submit %.2f, present %.2f, readback %.2f "
            "(fence waits %.2f); %s; simulation step %.2f ms (max %.2f)\n",
            s_perf.n, s_perf.total / n, s_perf.maxTotal, s_perf.interp / n, s_perf.wait / n,
            s_perf.acquire / n, s_perf.upload / n, s_perf.walk / n, s_perf.bind / n,
            s_perf.submit / n, s_perf.present / n, s_perf.readback / n, s_perf.fence / n, gpu,
            s_pres.stepCount ? s_pres.stepSumMs / s_pres.stepCount : 0.0, s_pres.stepMaxMs);
    fprintf(stderr,
            "window: per replay %.0f draws, %.0f passes, %.0f pipeline and %.0f bind group binds, "
            "%.0f bind groups, %.0f barriers, %.0f copies, %.0f KB uploaded (%.0f KB meshes), "
            "%.1f textures and %.1f meshes uploaded, %.1f DATE snapshots, %.1f exact blends; in "
            "the block %llu buffers and %llu textures created, %llu and %llu destroyed, %llu "
            "memory allocations, %llu pipelines created, %llu fence waits, %llu wait-idles, %llu "
            "readbacks\n",
            s_perf.draws / n, s_perf.passes / n, s_perf.pipeBinds / n, s_perf.groupBinds / n,
            s_perf.groups / n, s_perf.barriers / n, s_perf.copies / n, s_perf.bytes / n / 1024.0,
            s_perf.meshBytes / n / 1024.0, s_perf.texUploads / n, s_perf.meshUploads / n,
            s_perf.dateSnaps / n, s_perf.exact / n, (unsigned long long)s_perf.bufCreated,
            (unsigned long long)s_perf.texCreated, (unsigned long long)s_perf.bufDestroyed,
            (unsigned long long)s_perf.texDestroyed, (unsigned long long)s_perf.allocs,
            (unsigned long long)s_perf.pipeCreates, (unsigned long long)s_perf.fenceWaits,
            (unsigned long long)s_perf.waitIdles, (unsigned long long)s_perf.readbacks);
    perf_reset();
}

/* R7b: presents and frames per second, every 10 s of real time, in both
   presentation modes. F2: "game frames" counts the frames shown (closed
   frames the pace saw); "frame numbers" also counts the ones the game
   dropped unshown (rd_DiscardFrame: fbKeep screens, gsb_UpdateGSSystem(1)),
   so a gap between the two is not a slowdown. The simulation's own rate
   is the vsyncs against the block's real time, and any lag the pacer had
   to drop (resyncs) says the step plus the presents took longer than real
   time; the longest replay names the frame that did. */
static void pace_log(Uint64 now)
{
    if (s_pres.statAt == 0) {
        uint32_t n;

        s_pres.statAt = now;
        s_pres.statPresents = s_pres.statFrames = s_pres.statVsyncs = s_pres.statResyncs = 0;
        s_pres.statDropped = 0;
        s_pres.statFrameNo = rd_FrameNumber();
        rd_ReplayTimeMax(1, &n);
        perf_drain();
        perf_reset();
        return;
    }
    if (now - s_pres.statAt < ico_stats_period_ns(now, s_pres.fastUntil)) {
        return;
    }
    const double sec = (double)(now - s_pres.statAt) / 1e9;
    uint32_t replays = 0;
    const double maxMs = rd_ReplayTimeMax(1, &replays);
    const uint32_t fn = rd_FrameNumber();

    if (!rd_InterpolationActive()) {
        s_pres.statPresents = replays; /* one present per replay */
    }
    fprintf(stderr,
            "window: %u presents and %u game frames (%u frame numbers) in %.1f s: %.1f "
            "presented fps, %.1f game fps; %u vsyncs (%.1f Hz simulated), %u resyncs dropping "
            "%.0f ms; longest replay %.1f ms of %u; %u steps over %.0f ms\n",
            s_pres.statPresents, s_pres.statFrames, fn - s_pres.statFrameNo, sec,
            s_pres.statPresents / sec, s_pres.statFrames / sec, s_pres.statVsyncs,
            s_pres.statVsyncs / sec, s_pres.statResyncs, (double)s_pres.statDropped / 1e6, maxMs,
            replays, s_pres.slowSteps, s_pres.slowMs);
    perf_drain();
    perf_log();
    s_pres.statAt = now;
    s_pres.statPresents = s_pres.statFrames = s_pres.statVsyncs = s_pres.statResyncs = 0;
    s_pres.statDropped = 0;
    s_pres.statFrameNo = fn;
    s_pres.slowSteps = s_pres.slowLogged = 0;
}

/* The pacer more than RESYNC_NS behind: the lag is dropped (counted) */
static void resync(Uint64 now)
{
    s_pres.statResyncs++;
    s_pres.statDropped += now - s_deadline;
    s_deadline = now;
}

static void pace(int hz);

/* Q1: a step (ico_host_step to this pace) over [dev] slow_step_ms: what ran
   in it.  "other" is the host loop's work between the step and the pump:
   the trace line, the pad recording and the log flush. */
static void slow_step(double ms)
{
    IcoStepProfile p;
    const RdTexCacheStats *tc = rdtex_Stats();
    const RdStats *rs = rd_GetStats();

    s_pres.slowSteps++;
    if (s_pres.slowLogged >= SLOW_STEP_LINES) {
        return;
    }
    s_pres.slowLogged++;
    ico_host_step_profile(&p);
    const double other = ms - p.totalMs - s_pres.pumpMs;
    fprintf(stderr,
            "window: slow step %.1f ms at vsync %u (Main tick %u, stage %d%s): step %.1f "
            "(vsync callbacks %.1f, audio %.1f, game threads %.1f in %lu switches, achievements "
            "%.1f), pump %.1f, other %.1f; %u disc reads (%u sectors), %u texture decodes, %u "
            "pipelines created, memory card %s\n",
            ms, ico_host_vsync_count(), ico_host_main_ticks(), p.stage,
            p.loading ? ", loading" : "", p.totalMs, p.hooksMs, p.audioMs, p.threadsMs, p.switches,
            p.achMs, s_pres.pumpMs, other > 0.0 ? other : 0.0, p.cdReads, p.cdSectors,
            tc != NULL ? tc->decodes - s_pres.decodes : 0u,
            rs != NULL ? rs->pipelineCreates - s_pres.pipeCreates : 0u,
            p.mcPending ? "busy" : "idle");
}

void ico_window_pace(int hz)
{
    /* P1: the simulation step that ran since the last pace (the host loop
       calls ico_host_step, then this) */
    const Uint64 now = SDL_GetTicksNS();

    if (s_pres.paceEnd != 0 && now > s_pres.paceEnd) {
        const double ms = (double)(now - s_pres.paceEnd) / 1e6;

        s_pres.stepSumMs += ms;
        s_pres.stepMaxMs = ms > s_pres.stepMaxMs ? ms : s_pres.stepMaxMs;
        s_pres.stepCount++;
        if (s_pres.slowMs > 0.0 && ms > s_pres.slowMs) {
            slow_step(ms);
        }
    }
    pace(hz);
    perf_drain(); /* P1: every vsync, so the record queue never overflows */
    {
        const RdTexCacheStats *tc = rdtex_Stats();
        const RdStats *rs = rd_GetStats();

        s_pres.decodes = tc != NULL ? tc->decodes : 0;
        s_pres.pipeCreates = rs != NULL ? rs->pipelineCreates : 0;
    }
    s_pres.paceEnd = SDL_GetTicksNS();
}

static void pace(int hz)
{
    /* PAL 50 Hz: 20 ms; NTSC 59.94 Hz: 16.683 ms (host_loop.c's simulated
       periods) */
    const Uint64 period = hz == 50 ? 20000000ull : 16683333ull;
    Uint64 now;

    s_deadline += period;
    s_pres.statVsyncs++;
    {
        const uint32_t fn = rd_FrameNumber();

        if (fn != s_pres.frame) {
            s_pres.frame = fn;
            s_pres.tickPrev = s_pres.tickAt;
            s_pres.tickAt = s_deadline - period;
            s_pres.statFrames++;
        }
    }
    if (!rd_InterpolationActive()) {
        /* framerate "original": rd_EndFrame presented the frame once; the
           picture is held until the next */
        now = SDL_GetTicksNS();
        if (now < s_deadline) {
            SDL_DelayPrecise(s_deadline - now);
        } else if (now - s_deadline > RESYNC_NS) {
            resync(now);
        }
        pace_log(SDL_GetTicksNS());
        return;
    }
    /* R7b: the simulation keeps its vsync cadence in simulated time; until
       this vsync's deadline the window presents as often as vsync (and the
       framerate cap) allow, each present at alpha = the real time since the
       last frame closed over the tick, between the last two frames (one
       tick of latency). A frame closes inside the step before this call:
       its time is the start of this vsync period. */
    /* F2: a present with vsync on blocks up to one display refresh; that
       wait is not the renderer being slow. The user's first Windows run
       (60 Hz display, 59.94 Hz simulation: refresh 16.67 ms against the
       16.68 ms period) sat on that edge and fell back to one present per
       game frame. Slow means a present costing more than a display refresh
       plus half a simulated period. */
    Uint64 slow = period;
    Uint64 gap = s_pres.framerate > 0 ? 1000000000ull / (Uint64)s_pres.framerate : 0;
    {
        const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
        const Uint64 refresh = dm != NULL && dm->refresh_rate > 1.0f
                                   ? (Uint64)(1e9 / (double)dm->refresh_rate)
                                   : period;

        slow = (refresh > period ? refresh : period) + period / 2;
        /* P1: "uncapped" with vsync on.  In mailbox mode (rhi_PreferMailbox)
           a present never waits for the display; two presents a refresh keep
           every refresh supplied with a fresh picture without drawing many
           that are never shown.  Under FIFO (no mailbox) one a refresh: the
           display's own rate, so a present rarely finds the queue full and
           waits.  Without vsync "uncapped" is back to back. */
        if (s_pres.framerate == ICO_FRAMERATE_UNCAPPED && s_pres.mailbox) {
            gap = rhi_PresentMailbox() ? refresh / 2 : refresh - refresh / 16;
        }
    }
    Uint64 tick = s_pres.tickPrev ? s_pres.tickAt - s_pres.tickPrev : 2 * period;
    tick = tick < period ? period : (tick > 4 * period ? 4 * period : tick);
    for (;;) {
        now = SDL_GetTicksNS();
        /* behind the deadline, or on a renderer slower than a vsync period
           per present (a software driver): one present per new frame, none
           more, so the presents never slow the simulation below the
           original's one replay per frame */
        /* P1: nor a further present of a frame already shown that would
           end past the deadline (its cost the last present's): the next
           simulation step is never late for one */
        if (s_pres.presentedFrame == s_pres.frame &&
            (now >= s_deadline || s_pres.cost > slow || now + s_pres.cost > s_deadline)) {
            if (now < s_deadline) {
                SDL_DelayPrecise(s_deadline - now);
            }
            break;
        }
        if (gap && s_pres.lastPresent && now - s_pres.lastPresent < gap) {
            const Uint64 wake = s_pres.lastPresent + gap;

            if (wake >= s_deadline) {
                if (now < s_deadline) {
                    SDL_DelayPrecise(s_deadline - now);
                }
                break;
            }
            SDL_DelayPrecise(wake - now);
            continue;
        }
        /* S2: alpha from the present clock (the measured time smoothed
           to the presents' steady cadence) against the simulated time the
           frame closed; the raw measured time jittered with the step and
           the sleeps before the present */
        float a =
            rd_PresentClockAlpha(&s_pres.clock, (double)now / 1e6, (double)s_pres.tickAt / 1e6,
                                 (double)tick / 1e6, (double)gap / 1e6);

        if (s_pres.legacyAlpha) {
            /* ICO_RD_S2_LEGACY=1: the measured time, as before S2 */
            const double r =
                now > s_pres.tickAt ? (double)(now - s_pres.tickAt) / (double)tick : 0.0;
            a = (float)(r > 0.999 ? 0.999 : r);
        }
        const Uint64 t0 = now;
        const int ok = rd_Present(a);
        now = SDL_GetTicksNS();
        s_pres.cost = now - t0;
        if (!ok) {
            /* a movie on the output, or nothing closed yet */
            if (now < s_deadline) {
                SDL_DelayPrecise(s_deadline - now);
            }
            break;
        }
        s_pres.lastPresent = t0;
        s_pres.presentedFrame = s_pres.frame;
        s_pres.statPresents++;
    }
    now = SDL_GetTicksNS();
    pace_log(now);
    if (now > s_deadline && now - s_deadline > RESYNC_NS) {
        resync(now);
    }
}

void ico_window_close(void)
{
    if (!s_open) {
        return;
    }
    s_open = 0;
    set_capture(0);
    ico_input_sdl_shutdown();
    ui_HostShutdown();
    rd_SetHostCall(NULL);
    rd_Shutdown();
    if (s_window != NULL) {
        SDL_DestroyWindow(s_window);
        s_window = NULL;
    }
    SDL_Quit();
}
