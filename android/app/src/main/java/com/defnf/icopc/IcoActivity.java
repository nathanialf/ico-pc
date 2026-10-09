// IcoActivity: SDL's activity (org.libsdl.app, copied from the SDL release
// by tools/fetch_android.sh) with the program's libraries: libSDL3.so, then
// libmain.so (the CMake target ico_pc), whose SDL_main SDL calls.
package com.defnf.icopc;

import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.WindowManager;

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
