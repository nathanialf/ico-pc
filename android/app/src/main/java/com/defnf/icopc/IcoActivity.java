// IcoActivity: SDL's activity (org.libsdl.app, copied from the SDL release
// by tools/fetch_android.sh) with the program's libraries: libSDL3.so, then
// libmain.so (the CMake target ico_pc), whose SDL_main SDL calls.
package com.defnf.icopc;

import org.libsdl.app.SDLActivity;

public class IcoActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }
}
