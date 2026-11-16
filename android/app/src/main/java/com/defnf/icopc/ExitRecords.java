package com.defnf.icopc;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

// Android's exit records of this app's earlier processes
// (ActivityManager.getHistoricalProcessExitReasons, Android 11, API 30) as
// the record text port/platform/exit_info.h reads: "sdk", "lmk", "total"
// and "why" lines, then one "record" line a record newer than lastSeenMs,
// the newest maxRecords of them, newest first, each of 11 tab-separated
// fields (pid, reason, status, importance, pss kB, rss kB, time ms, trace,
// description, summary). A record's trace (an ANR's text; from Android 12 a
// native crash's tombstone) is copied, at most traceMaxBytes of it, to
// logsDir/exit-trace-<time>.txt (.pb for a tombstone) for the newest two
// records that have one; only the newest two such files stay. Its own
// class so that IcoActivity never names an API 30 class.
final class ExitRecords {
    private static final String TRACE_HEAD = "exit-trace-";
    private static final int TRACES_KEPT = 2;

    private ExitRecords() {
    }

    static String read(Context ctx, String logsDir, long lastSeenMs, int maxRecords,
            int traceMaxBytes, int descMax) {
        StringBuilder out = new StringBuilder();
        out.append("sdk\t").append(Build.VERSION.SDK_INT).append('\n');
        try {
            ActivityManager am = (ActivityManager) ctx.getSystemService(Context.ACTIVITY_SERVICE);
            if (am == null) {
                return out.append("why\tno ActivityManager\n").toString();
            }
            out.append("lmk\t").append(ActivityManager.isLowMemoryKillReportSupported() ? 1 : 0)
                    .append('\n');
            // null: this app's own package; 0, 0: every pid, every record
            List<ApplicationExitInfo> all = am.getHistoricalProcessExitReasons(null, 0, 0);
            out.append("total\t").append(all.size()).append('\n');
            ArrayList<ApplicationExitInfo> fresh = new ArrayList<>();
            for (ApplicationExitInfo e : all) {
                if (e.getTimestamp() > lastSeenMs) {
                    fresh.add(e);
                }
            }
            Collections.sort(fresh, new Comparator<ApplicationExitInfo>() {
                @Override
                public int compare(ApplicationExitInfo a, ApplicationExitInfo b) {
                    return Long.compare(b.getTimestamp(), a.getTimestamp());
                }
            });
            int traces = 0;
            for (int i = 0; i < fresh.size() && i < maxRecords; i++) {
                ApplicationExitInfo e = fresh.get(i);
                String trace = "-";
                if (traces < TRACES_KEPT) {
                    trace = copyTrace(e, logsDir, traceMaxBytes);
                    if (!trace.equals("-")) {
                        traces++;
                    }
                }
                out.append("record\t").append(e.getPid())
                        .append('\t').append(e.getReason())
                        .append('\t').append(e.getStatus())
                        .append('\t').append(e.getImportance())
                        .append('\t').append(e.getPss())
                        .append('\t').append(e.getRss())
                        .append('\t').append(e.getTimestamp())
                        .append('\t').append(trace)
                        .append('\t').append(clean(e.getDescription(), descMax))
                        .append('\t').append(summary(e.getProcessStateSummary()))
                        .append('\n');
            }
            if (traces > 0) {
                prune(logsDir);
            }
        } catch (RuntimeException e) {
            out.append("why\t").append(clean("getHistoricalProcessExitReasons: " + e, 150))
                    .append('\n');
        }
        return out.toString();
    }

    // tabs and line ends to blanks, at most max characters ("" for null)
    private static String clean(String s, int max) {
        if (s == null) {
            return "";
        }
        String t = s.replace('\t', ' ').replace('\r', ' ').replace('\n', ' ');
        return t.length() > max ? t.substring(0, max) : t;
    }

    // the process state summary (last_run.h: ASCII, at most 128 bytes);
    // another byte becomes '?'
    private static String summary(byte[] b) {
        if (b == null) {
            return "";
        }
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < b.length && i < 128; i++) {
            int c = b[i] & 0xff;
            s.append(c >= 0x20 && c < 0x7f ? (char) c : '?');
        }
        return s.toString();
    }

    // the trace's file name, "-" none, "!<why>" when the copy failed
    private static String copyTrace(ApplicationExitInfo e, String logsDir, int maxBytes) {
        InputStream in;
        try {
            in = e.getTraceInputStream();
        } catch (IOException x) {
            return "!" + clean(x.toString(), 80);
        }
        if (in == null) {
            return "-";
        }
        String name = TRACE_HEAD + e.getTimestamp()
                + (e.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE ? ".pb" : ".txt");
        try (InputStream src = in;
                FileOutputStream dst = new FileOutputStream(new File(logsDir, name))) {
            byte[] buf = new byte[65536];
            int left = maxBytes;
            while (left > 0) {
                int n = src.read(buf, 0, Math.min(buf.length, left));
                if (n <= 0) {
                    break;
                }
                dst.write(buf, 0, n);
                left -= n;
            }
            return name;
        } catch (IOException x) {
            return "!" + clean(x.toString(), 80);
        }
    }

    // the newest TRACES_KEPT exit-trace files stay (by the time in the name)
    private static void prune(String logsDir) {
        File[] files = new File(logsDir).listFiles();
        if (files == null) {
            return;
        }
        ArrayList<File> traces = new ArrayList<>();
        for (File f : files) {
            if (f.getName().startsWith(TRACE_HEAD) && stamp(f.getName()) >= 0) {
                traces.add(f);
            }
        }
        Collections.sort(traces, new Comparator<File>() {
            @Override
            public int compare(File a, File b) {
                return Long.compare(stamp(b.getName()), stamp(a.getName()));
            }
        });
        for (int i = TRACES_KEPT; i < traces.size(); i++) {
            // a file not deleted is left for the next start's prune
            traces.get(i).delete();
        }
    }

    // the time in "exit-trace-<time>.<ext>", -1 for another name
    private static long stamp(String name) {
        int dot = name.indexOf('.', TRACE_HEAD.length());
        if (dot <= TRACE_HEAD.length()) {
            return -1;
        }
        try {
            return Long.parseLong(name.substring(TRACE_HEAD.length(), dot));
        } catch (NumberFormatException x) {
            return -1;
        }
    }
}
