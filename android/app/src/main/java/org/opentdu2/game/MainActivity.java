package org.opentdu2.game;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.net.Uri;
import android.os.Environment;
import android.provider.Settings;

import org.libsdl.app.SDLActivity;

/**
 * The OpenTDU2 game activity. The engine builds as libopentdu2.so with SDL3
 * statically linked inside (SDL_SHARED OFF / SDL_STATIC ON in
 * cmake/Dependencies.cmake), so there is no separate libSDL3.so to load —
 * loading "SDL3" would throw UnsatisfiedLinkError. SDL's JNI_OnLoad lives in
 * libopentdu2.so, and getMainSharedObject() resolves to it as the last entry;
 * the engine's main() (source/main.cpp, SDL_main.h) runs on SDL's SDLThread.
 *
 * The integrated launcher (source/launcher/otdu_launcher.cpp) calls
 * hasAllFilesAccess() / requestAllFilesAccess() over JNI to manage the
 * MANAGE_EXTERNAL_STORAGE grant for the game data on shared storage.
 */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "opentdu2"
        };
    }

    /**
     * Called from native (JNI): the launcher follows the device orientation
     * (portrait or landscape, phone or tablet); the game itself is landscape.
     */
    public void setLandscapeOnly( final boolean landscapeOnly ) {
        runOnUiThread( new Runnable() {
            @Override
            public void run() {
                setRequestedOrientation( landscapeOnly
                    ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                    : ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR );
            }
        } );
    }

    /** Called from native (JNI): Environment.isExternalStorageManager(). */
    public boolean hasAllFilesAccess() {
        try {
            return Environment.isExternalStorageManager();
        } catch (Exception e) {
            return false;
        }
    }

    /** Called from native (JNI): open the system All-Files-Access page. */
    public void requestAllFilesAccess() {
        try {
            Intent intent = new Intent(
                Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e) {
            try {
                startActivity(new Intent(
                    Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (Exception e2) {
                // No such screen on this vendor build; nothing else to try.
            }
        }
    }
}
