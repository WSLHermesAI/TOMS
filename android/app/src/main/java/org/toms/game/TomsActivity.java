package org.toms.game;

import org.libsdl.app.SDLActivity;

// SDL3 is linked statically into libmain.so (the game, src/game/src/main_sdl.cpp), so there is no
// separate libSDL3.so to load: just the C++ runtime and the game.
public class TomsActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "main" };
    }
}
