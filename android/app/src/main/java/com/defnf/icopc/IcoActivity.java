// IcoActivity: SDL's activity (org.libsdl.app, copied from the SDL release
// by tools/fetch_android.sh) with the program's libraries: libSDL3.so, then
// libmain.so (the CMake target ico_pc), whose SDL_main SDL calls.
package com.defnf.icopc;

import android.app.ActivityManager;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Rect;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.os.PowerManager;
import android.provider.OpenableColumns;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.WindowManager;

import androidx.core.content.ContextCompat;
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
    // so too (res/values-v28 and values-v30/styles.xml); this sets it on the
    // window before SDL's onCreate (the window exists by then). SDL itself
    // only sets it on Android 11 and later, and by editing the same
    // attributes. Android 8 (API 26 and 27, issue 158) has no cutout mode:
    // the field is API 28, and reading it there would end onCreate.
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30
                    ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                    : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
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

    // The phone's thermal state and whether the screen is being recorded,
    // for the log (issue 123: a throttled GPU or a recorder holding frames
    // back looks like a stall). thermalHeadroom and thermalStatus answer
    // the native side's asks (host_android.c ico_host_thermal, on SDL's
    // main thread); the two listeners push every change (nativeThermalStatus
    // from Android 10, nativeScreenRecording from Android 15). Every call is
    // behind its API level, a missing native method is ignored, and a
    // failure of any of it only means no words in the log. The listener
    // fields are Objects so that this class loads on Android 8 without the
    // newer classes.
    private static native void nativeThermalStatus(int status);
    private static native void nativeScreenRecording(int state);
    private static PowerManager sPower;
    private Object mThermalListener;
    private Object mRecordingCallback;

    private static PowerManager power() {
        if (sPower == null && sInstance != null) {
            sPower = (PowerManager) sInstance.getSystemService(Context.POWER_SERVICE);
        }
        return sPower;
    }

    // PowerManager.getThermalHeadroom(0): the forecast of the thermal
    // state now, 1.0 the point where the system starts to throttle; NaN
    // before Android 11, when the phone gives none, or when asked too often
    public static float thermalHeadroom() {
        try {
            PowerManager pm = power();
            if (Build.VERSION.SDK_INT >= 30 && pm != null) {
                return pm.getThermalHeadroom(0);
            }
        } catch (RuntimeException e) {
            // no answer is the answer
        }
        return Float.NaN;
    }

    // PowerManager.getCurrentThermalStatus: 0 (none) to 6 (shutdown), -1
    // before Android 10 or when the phone gives none
    public static int thermalStatus() {
        try {
            PowerManager pm = power();
            if (Build.VERSION.SDK_INT >= 29 && pm != null) {
                return pm.getCurrentThermalStatus();
            }
        } catch (RuntimeException e) {
            // no answer is the answer
        }
        return -1;
    }

    private void pushThermalStatus(int status) {
        if (!SDLActivity.mBrokenLibraries) {
            try {
                nativeThermalStatus(status);
            } catch (UnsatisfiedLinkError e) {
                // libmain.so without the method
            }
        }
    }

    private void pushScreenRecording(int state) {
        if (!SDLActivity.mBrokenLibraries) {
            try {
                nativeScreenRecording(state);
            } catch (UnsatisfiedLinkError e) {
                // libmain.so without the method
            }
        }
    }

    // while the activity is visible: the thermal status listener (Android
    // 10) and the screen recording callback (Android 15, the
    // DETECT_SCREEN_RECORDING permission in the manifest, a normal one;
    // addScreenRecordingCallback answers with the state at registration)
    private void startThermalWatch() {
        if (Build.VERSION.SDK_INT >= 29 && mThermalListener == null) {
            try {
                PowerManager pm = power();
                if (pm != null) {
                    PowerManager.OnThermalStatusChangedListener l =
                            status -> pushThermalStatus(status);
                    pm.addThermalStatusListener(ContextCompat.getMainExecutor(this), l);
                    mThermalListener = l;
                }
            } catch (RuntimeException | LinkageError e) {
                mThermalListener = null;
            }
        }
        if (Build.VERSION.SDK_INT >= 35 && mRecordingCallback == null) {
            try {
                java.util.function.Consumer<Integer> cb = state -> pushScreenRecording(state);
                int state = getWindowManager().addScreenRecordingCallback(
                        ContextCompat.getMainExecutor(this), cb);
                mRecordingCallback = cb;
                pushScreenRecording(state);
            } catch (RuntimeException | LinkageError e) {
                // no permission, or the system refuses: no recording words
                mRecordingCallback = null;
            }
        }
    }

    @SuppressWarnings("unchecked")
    private void stopThermalWatch() {
        if (Build.VERSION.SDK_INT >= 29 && mThermalListener != null) {
            try {
                PowerManager pm = power();
                if (pm != null) {
                    pm.removeThermalStatusListener(
                            (PowerManager.OnThermalStatusChangedListener) mThermalListener);
                }
            } catch (RuntimeException | LinkageError e) {
                // already gone
            }
            mThermalListener = null;
        }
        if (Build.VERSION.SDK_INT >= 35 && mRecordingCallback != null) {
            try {
                getWindowManager().removeScreenRecordingCallback(
                        (java.util.function.Consumer<Integer>) mRecordingCallback);
            } catch (RuntimeException | LinkageError e) {
                // already gone
            }
            mRecordingCallback = null;
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        applyTaskIcon();
        startThermalWatch();
        try {
            if (mLayoutTracker == null) {
                mLayoutTracker = new WindowInfoTrackerCallbackAdapter(
                        WindowInfoTracker.getOrCreate(this));
            }
            // ContextCompat's executor: Context.getMainExecutor is API 28
            // (issue 158, Android 8)
            mLayoutTracker.addWindowLayoutInfoListener(this,
                    ContextCompat.getMainExecutor(this), mLayoutListener);
        } catch (RuntimeException | LinkageError e) {
            // no fold reports: the layout treats the screen as flat (a
            // missing method or class is a LinkageError, not an exception)
            mLayoutTracker = null;
        }
        sendScreen();
    }

    @Override
    protected void onStop() {
        stopThermalWatch();
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

    // The recents card's icon (host_android.c ico_android_task_icon, on SDL's
    // main thread): the PNG the game made from the title's I
    // (<files>/icon/ico-pc-<disc>.png), decoded, kept, and set as the task's
    // icon on the UI thread.  null when it was started, else the reason,
    // which the native side logs.  The launcher icon is a compiled resource
    // and stays; some launchers ignore a task description's icon.
    private static Bitmap sTaskIcon;

    public static String setTaskIcon(String path) {
        IcoActivity a = sInstance;
        if (a == null) {
            return "no activity";
        }
        // Straight alpha kept: the PNG's background is alpha 0, so the card's
        // own colour shows round the letter (no RGB_565, no fill).
        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
        Bitmap bmp = BitmapFactory.decodeFile(path, opts);
        if (bmp == null) {
            return "the PNG could not be decoded";
        }
        if (!bmp.hasAlpha()) {
            return "the PNG decoded without an alpha channel";
        }
        sTaskIcon = bmp;
        a.runOnUiThread(() -> a.applyTaskIcon());
        return null;
    }

    // A recreated activity resets its description, so onStart sets it again.
    // The Bitmap constructor is deprecated from 33, but it is the only form
    // that takes a run-time image (the int and Builder forms take a resource
    // id).
    @SuppressWarnings("deprecation")
    private void applyTaskIcon() {
        Bitmap bmp = sTaskIcon;
        if (bmp == null) {
            return;
        }
        try {
            setTaskDescription(new ActivityManager.TaskDescription(getString(R.string.app_name), bmp));
        } catch (RuntimeException e) {
            // a plain card is better than a crash
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

    // Options > Extras > Back up your data (host_android.c
    // ico_android_create_file, on the game's thread, as pickFile): the
    // system's "save as" screen, ACTION_CREATE_DOCUMENT, with the name it
    // suggests and the file's type. Its answer, the new document's URI,
    // comes to onActivityResult like a pick's and goes to nativePickDone
    // with the same gen. null when the screen was started, else the reason
    // (starting "no app" when no app takes the intent).
    private static final int CREATE_REQUEST = 0x1C0E;

    public static String createFile(int gen, String name, String mime) {
        IcoActivity a = sInstance;
        if (a == null) {
            return "no activity";
        }
        try {
            Intent create = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            create.addCategory(Intent.CATEGORY_OPENABLE);
            create.setType(mime);
            create.putExtra(Intent.EXTRA_TITLE, name);
            pickNote("create " + gen + ": CREATE_DOCUMENT goes to " + resolved(a, create));
            sPickGen = gen;
            a.startActivityForResult(create, CREATE_REQUEST);
            return null;
        } catch (ActivityNotFoundException e) {
            return "no app for CREATE_DOCUMENT (" + e.toString() + ")";
        } catch (RuntimeException e) {
            return e.toString();
        }
    }

    // A descriptor for a URI a picker or the "save as" screen answered
    // (host_android.c ico_android_open_fd): the content resolver's, mode
    // "r", "w" or "wt", detached so the native side owns and closes it. -1
    // when it cannot be opened; the log line names the exception's kind
    // only (its text can name the player's file).
    public static int openFd(String uri, String mode) {
        IcoActivity a = sInstance;
        if (a == null) {
            pickNote("open " + mode + " failed: no activity");
            return -1;
        }
        try {
            ParcelFileDescriptor pfd = a.getContentResolver().openFileDescriptor(Uri.parse(uri),
                    mode);
            if (pfd == null) {
                pickNote("open " + mode + " failed: no descriptor");
                return -1;
            }
            return pfd.detachFd();
        } catch (Exception e) {
            // FileNotFoundException, SecurityException, a mode the
            // provider does not take (IllegalArgumentException)
            pickNote("open " + mode + " failed: " + e.getClass().getSimpleName());
            return -1;
        }
    }

    // The name the system shows for a URI (host_android.c
    // ico_android_display_name), or null.
    public static String displayName(String uri) {
        IcoActivity a = sInstance;
        if (a == null) {
            return null;
        }
        try (Cursor c = a.getContentResolver().query(Uri.parse(uri),
                new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                return c.getString(0);
            }
        } catch (Exception e) {
            // no name: the native side says "the chosen file"
        }
        return null;
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

    // The activity came back to the front: counted on the native side
    // (host_android.c nativeResumed), so a picker that closes without any
    // answer still ends the wait (issue 135). A missing native method is
    // ignored.
    private static native void nativeResumed();

    @Override
    protected void onResume() {
        super.onResume();
        if (!SDLActivity.mBrokenLibraries) {
            try {
                nativeResumed();
            } catch (UnsatisfiedLinkError e) {
                // libmain.so without the method: the timeout ends the wait
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_REQUEST && requestCode != CREATE_REQUEST) {
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
        pickNote((requestCode == CREATE_REQUEST ? "create " : "pick ") + sPickGen
                + ": onActivityResult resultCode " + resultCode
                + (resultCode == RESULT_OK ? " (OK)" : resultCode == RESULT_CANCELED
                        ? " (CANCELED)" : "")
                + ", data " + (data == null ? "null" : "present")
                + ", " + (uri != null ? "a file" : why != null ? "no file: " + why
                        : "no file"));
        try {
            nativePickDone(sPickGen, uri, why);
        } catch (UnsatisfiedLinkError e) {
            // libmain.so without the method: nobody is waiting
        }
    }
}
