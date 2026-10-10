// IcoActivity: SDL's activity (org.libsdl.app, copied from the SDL release
// by tools/fetch_android.sh) with the program's libraries: libSDL3.so, then
// libmain.so (the CMake target ico_pc), whose SDL_main SDL calls.
package com.defnf.icopc;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.WindowManager;

import androidx.core.util.Consumer;
import androidx.window.java.layout.WindowInfoTrackerCallbackAdapter;
import androidx.window.layout.DisplayFeature;
import androidx.window.layout.FoldingFeature;
import androidx.window.layout.WindowInfoTracker;
import androidx.window.layout.WindowLayoutInfo;
import androidx.window.layout.WindowMetricsCalculator;

import org.libsdl.app.SDLActivity;

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
}
