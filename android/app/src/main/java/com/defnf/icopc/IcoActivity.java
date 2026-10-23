// IcoActivity: SDL's activity (org.libsdl.app, copied from the SDL release
// by tools/fetch_android.sh) with the program's libraries: libSDL3.so, then
// libmain.so (the CMake target ico_pc), whose SDL_main SDL calls.
package com.defnf.icopc;

import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.WindowManager;

import androidx.core.content.FileProvider;
import androidx.core.util.Consumer;
import androidx.window.java.layout.WindowInfoTrackerCallbackAdapter;
import androidx.window.layout.DisplayFeature;
import androidx.window.layout.FoldingFeature;
import androidx.window.layout.WindowInfoTracker;
import androidx.window.layout.WindowLayoutInfo;
import androidx.window.layout.WindowMetricsCalculator;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.util.ArrayList;

public class IcoActivity extends SDLActivity {
    private static IcoActivity sInstance;
    private static Vibrator sVibrator;

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    // The picture reaches into the display cutout (the notch). The theme says
    // so too (res/values*/styles.xml); this sets it on the window before
    // SDL's onCreate (the window exists by then). SDL itself only sets it on
    // Android 11 and later, and by editing the same attributes.
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        WindowManager.LayoutParams lp = getWindow().getAttributes();
        lp.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30
                ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        getWindow().setAttributes(lp);
        sInstance = this;
        super.onCreate(savedInstanceState);
    }

    // The screen's density and fold for the touch controls' layout
    // (host_android.c ico_android_screen): dpi the density in dots per inch;
    // fold 0 none, 1 vertical, 2 horizontal; half 1 when the fold is bent
    // part way; sep 1 when it splits the screen in two; l, t, r, b its
    // bounds and winW, winH the window's size, all in the window's pixels.
    private static native void nativeScreen(int dpi, int fold, int half, int sep,
            int l, int t, int r, int b, int winW, int winH);

    // The system's window-layout reports (Jetpack WindowManager), which say
    // where a foldable's fold or hinge is and how far it is bent. Listened
    // to while the activity is visible; the last report is kept so a
    // configuration change (a new density) can send it again.
    private WindowInfoTrackerCallbackAdapter mLayoutTracker;
    private WindowLayoutInfo mLayoutInfo;
    private final Consumer<WindowLayoutInfo> mLayoutListener = new Consumer<WindowLayoutInfo>() {
        @Override
        public void accept(WindowLayoutInfo info) {
            mLayoutInfo = info;
            sendScreen();
        }
    };

    @Override
    protected void onStart() {
        super.onStart();
        try {
            if (mLayoutTracker == null) {
                mLayoutTracker = new WindowInfoTrackerCallbackAdapter(
                        WindowInfoTracker.getOrCreate(this));
            }
            mLayoutTracker.addWindowLayoutInfoListener(this, getMainExecutor(), mLayoutListener);
        } catch (RuntimeException e) {
            // no fold reports: the layout treats the screen as flat
            mLayoutTracker = null;
        }
        sendScreen();
    }

    @Override
    protected void onStop() {
        if (mLayoutTracker != null) {
            try {
                mLayoutTracker.removeWindowLayoutInfoListener(mLayoutListener);
            } catch (RuntimeException e) {
                // already gone
            }
        }
        super.onStop();
    }

    // The system's request to free memory (issue 93), with its level, to the
    // native side's log and start-up activity file (host_android.c
    // nativeTrimMemory), then SDL's own handler (SDL_EVENT_LOW_MEMORY, which
    // carries no level). Skipped when SDL could not load the libraries; a
    // missing native method is ignored.
    private static native void nativeTrimMemory(int level);

    @Override
    public void onTrimMemory(int level) {
        if (!SDLActivity.mBrokenLibraries) {
            try {
                nativeTrimMemory(level);
            } catch (UnsatisfiedLinkError e) {
                // libmain.so without the method: SDL's event only
            }
        }
        super.onTrimMemory(level);
    }

    // density is in the manifest's configChanges, so a new display size
    // setting (or moving to the other screen of a foldable) comes here
    // instead of restarting the activity
    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        sendScreen();
    }

    // The density, the first fold the last report lists and the window's
    // size, to the native side. Skipped when SDL could not load the
    // libraries (SDLActivity shows its error then); a missing native method
    // is ignored rather than ending the app.
    private void sendScreen() {
        if (SDLActivity.mBrokenLibraries) {
            return;
        }
        int dpi = getResources().getConfiguration().densityDpi;
        int fold = 0, half = 0, sep = 0;
        Rect bounds = new Rect();
        Rect win = new Rect();
        try {
            win = WindowMetricsCalculator.getOrCreate().computeCurrentWindowMetrics(this)
                    .getBounds();
        } catch (RuntimeException e) {
            // unknown window size: the native side then ignores the fold
        }
        if (mLayoutInfo != null) {
            for (DisplayFeature feature : mLayoutInfo.getDisplayFeatures()) {
                if (feature instanceof FoldingFeature) {
                    FoldingFeature f = (FoldingFeature) feature;
                    if (f.getOrientation() == FoldingFeature.Orientation.VERTICAL) {
                        fold = 1;
                    } else if (f.getOrientation() == FoldingFeature.Orientation.HORIZONTAL) {
                        fold = 2;
                    }
                    half = f.getState() == FoldingFeature.State.HALF_OPENED ? 1 : 0;
                    sep = f.isSeparating() ? 1 : 0;
                    bounds = f.getBounds();
                    break;
                }
            }
        }
        try {
            nativeScreen(dpi, fold, half, sep, bounds.left, bounds.top, bounds.right,
                    bounds.bottom, win.width(), win.height());
        } catch (UnsatisfiedLinkError e) {
            // libmain.so without the method: the layout works without it
        }
    }

    // The phone's vibration for the touch controls (host_android.c
    // ico_host_vibrate): the game's rumble when no controller is connected.
    // amplitude 1 to 255 for ms milliseconds, the native side re-issues it
    // while the rumble lasts; 0 stops it. The getter used is the one that
    // works on every Android this app runs on (29 and later); it is
    // deprecated from 31 but still answers there.
    @SuppressWarnings("deprecation")
    public static void vibrate(int ms, int amplitude) {
        try {
            if (sVibrator == null) {
                if (sInstance == null) {
                    return;
                }
                sVibrator = (Vibrator) sInstance.getSystemService(Context.VIBRATOR_SERVICE);
            }
            if (sVibrator == null || !sVibrator.hasVibrator()) {
                return;
            }
            if (amplitude <= 0 || ms <= 0) {
                sVibrator.cancel();
                return;
            }
            sVibrator.vibrate(VibrationEffect.createOneShot(ms, Math.min(amplitude, 255)));
        } catch (RuntimeException e) {
            // no vibration is better than a crash
        }
    }

    // An rc build's Settings > Share a bug report (host_android.c
    // ico_android_share_files, on SDL's main thread): the files at paths,
    // all under the app's files folder, offered to the system's share menu
    // as content URIs of the app's FileProvider (AndroidManifest.xml, its
    // authority <package>.reports, res/xml/report_paths.xml), read access
    // granted to the app the player picks. null when the menu was started,
    // else the reason, which the native side logs. BuildConfig.RC_TOOLS is
    // app/build.gradle's icoRcTools (tools/package_android.sh sets it for a
    // label ending in -rc<N> alone); without it the provider is disabled in
    // the manifest and this does nothing.
    public static String shareReport(String title, String[] paths) {
        if (!BuildConfig.RC_TOOLS) {
            return "not a release candidate build";
        }
        IcoActivity a = sInstance;
        if (a == null) {
            return "no activity";
        }
        try {
            String authority = a.getPackageName() + ".reports";
            ArrayList<Uri> uris = new ArrayList<>();
            ClipData clip = null;
            for (String p : paths) {
                Uri u = FileProvider.getUriForFile(a, authority, new File(p));
                uris.add(u);
                if (clip == null) {
                    clip = ClipData.newRawUri(new File(p).getName(), u);
                } else {
                    clip.addItem(new ClipData.Item(u));
                }
            }
            if (uris.isEmpty()) {
                return "no files";
            }
            Intent send = new Intent(Intent.ACTION_SEND_MULTIPLE);
            send.setType("application/octet-stream");
            send.putParcelableArrayListExtra(Intent.EXTRA_STREAM, uris);
            // the chosen app's read access: the grant flag with the URIs as
            // the intent's clip data too, which the chooser passes on
            send.setClipData(clip);
            send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            Intent chooser = Intent.createChooser(send, title);
            chooser.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            a.startActivity(chooser);
            return null;
        } catch (RuntimeException e) {
            return e.toString();
        }
    }

    // The file picker (host_android.c ico_android_pick_file, issue 135): the
    // app's own, not SDL's, whose single pending-dialog slot the app cannot
    // clear. pickFile runs on the game's thread (as SDL's showFileDialog
    // does) and starts the system's document picker, ACTION_OPEN_DOCUMENT,
    // or when no app takes that, ACTION_GET_CONTENT; the answer comes to
    // onActivityResult on the activity's thread and goes to nativePickDone
    // with gen, the native side's number for this pick, which drops an
    // answer for a pick it gave up on. Nothing is persisted: the native side
    // copies the file while the grant lasts.
    private static final int PICK_REQUEST = 0x1C0D;
    private static volatile int sPickGen;

    // answer: uri the chosen file's content URI, or null; why null, or the
    // reason the picker gave no usable answer (a null uri with a null why is
    // the picker closed without a file)
    private static native void nativePickDone(int gen, String uri, String why);

    // one line for the log ("ico_pc: file picker: <line>")
    private static native void nativePickNote(String line);

    private static void pickNote(String line) {
        try {
            nativePickNote(line);
        } catch (UnsatisfiedLinkError e) {
            // libmain.so without the method: no line
        }
    }

    private static Intent pickIntent(String action) {
        Intent intent = new Intent(action);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // no type filter: the native side checks what the file is
        intent.setType("*/*");
        return intent;
    }

    // null when a picker was started, else the reason: one starting "no app"
    // when no installed app takes either intent (the native side's NO PICKER
    // APP), any other text when the start failed some other way (NO PICKER)
    public static String pickFile(int gen) {
        IcoActivity a = sInstance;
        if (a == null) {
            return "no activity";
        }
        pickNote("pick " + gen + ": the activity is "
                + (SDLActivity.mIsResumedCalled ? "resumed" : "not resumed")
                + (SDLActivity.mHasFocus ? ", focused" : ", not focused")
                + ", native state " + SDLActivity.mCurrentNativeState);
        String openWhy;
        try {
            Intent open = pickIntent(Intent.ACTION_OPEN_DOCUMENT);
            pickNote("pick " + gen + ": OPEN_DOCUMENT goes to " + resolved(a, open));
            // the gen before the start: a picker that answers at once
            // must answer with this pick's gen, not the previous one's
            sPickGen = gen;
            a.startActivityForResult(open, PICK_REQUEST);
            return null;
        } catch (ActivityNotFoundException e) {
            openWhy = e.toString();
        } catch (RuntimeException e) {
            return e.toString();
        }
        pickNote("pick " + gen + ": no app for OPEN_DOCUMENT (" + openWhy
                + "); trying GET_CONTENT");
        try {
            Intent get = pickIntent(Intent.ACTION_GET_CONTENT);
            pickNote("pick " + gen + ": GET_CONTENT goes to " + resolved(a, get));
            // the gen before the start: a picker that answers at once
            // must answer with this pick's gen, not the previous one's
            sPickGen = gen;
            a.startActivityForResult(get, PICK_REQUEST);
            return null;
        } catch (ActivityNotFoundException e) {
            return "no app for OPEN_DOCUMENT (" + openWhy + ") nor GET_CONTENT ("
                    + e.toString() + ")";
        } catch (RuntimeException e) {
            return e.toString();
        }
    }

    // the app an intent would start, for the log (the manifest's <queries>
    // makes it visible on Android 11 and later)
    private static String resolved(IcoActivity a, Intent intent) {
        try {
            ComponentName c = intent.resolveActivity(a.getPackageManager());
            return c != null ? c.flattenToShortString() : "no app";
        } catch (RuntimeException e) {
            return "unknown (" + e.toString() + ")";
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_REQUEST) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        String uri = null;
        String why = null;
        if (resultCode == RESULT_OK && data != null) {
            Uri u = data.getData();
            if (u == null) {
                // some pickers answer with the clip data alone
                ClipData clip = data.getClipData();
                if (clip != null && clip.getItemCount() > 0) {
                    u = clip.getItemAt(0).getUri();
                }
            }
            if (u != null) {
                uri = u.toString();
            } else {
                why = "the picker answered without a file";
            }
        }
        if (SDLActivity.mBrokenLibraries) {
            return;
        }
        try {
            nativePickDone(sPickGen, uri, why);
        } catch (UnsatisfiedLinkError e) {
            // libmain.so without the method: nobody is waiting
        }
    }
}
