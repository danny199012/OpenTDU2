package org.opentdu2.game;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.net.Uri;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
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

    // ---- Updates page (native side polls these; work runs on a Java thread) ----
    // state: 0 idle, 1 busy, 2 done, 3 failed
    private volatile int httpState = 0;
    private volatile String httpResult = "";
    private volatile long httpDone = 0;
    private volatile long httpTotal = 0;

    public int getHttpState() { return httpState; }
    public String getHttpResult() { return httpResult; }
    public long getHttpDone() { return httpDone; }
    public long getHttpTotal() { return httpTotal; }

    /** GET a URL into httpResult (JSON from the GitHub API). */
    public void startHttpGet( final String address ) {
        httpState = 1;
        httpResult = "";
        new Thread( new Runnable() {
            @Override
            public void run() {
                try {
                    HttpURLConnection c = (HttpURLConnection) new URL( address ).openConnection();
                    c.setRequestProperty( "Accept", "application/vnd.github+json" );
                    c.setRequestProperty( "User-Agent", "OpenTDU2-Android" );
                    c.setConnectTimeout( 15000 );
                    c.setReadTimeout( 20000 );
                    int code = c.getResponseCode();
                    if ( code != 200 ) {
                        httpResult = "HTTP " + code;
                        httpState = 3;
                        return;
                    }
                    InputStream in = c.getInputStream();
                    java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
                    byte[] buf = new byte[8192];
                    int n;
                    while ( ( n = in.read( buf ) ) > 0 ) {
                        out.write( buf, 0, n );
                    }
                    in.close();
                    httpResult = out.toString( "UTF-8" );
                    httpState = 2;
                } catch ( Exception e ) {
                    httpResult = String.valueOf( e.getMessage() );
                    httpState = 3;
                }
            }
        } ).start();
    }

    /** Download a URL to <cache>/update.apk (follows redirects, as GitHub assets need). */
    public void startDownload( final String address ) {
        httpState = 1;
        httpResult = "";
        httpDone = 0;
        httpTotal = 0;
        new Thread( new Runnable() {
            @Override
            public void run() {
                try {
                    File dest = new File( getCacheDir(), "update.apk" );
                    HttpURLConnection c = (HttpURLConnection) new URL( address ).openConnection();
                    c.setInstanceFollowRedirects( true );
                    c.setRequestProperty( "User-Agent", "OpenTDU2-Android" );
                    c.setConnectTimeout( 15000 );
                    c.setReadTimeout( 30000 );
                    int code = c.getResponseCode();
                    if ( code != 200 ) {
                        httpResult = "HTTP " + code;
                        httpState = 3;
                        return;
                    }
                    httpTotal = c.getContentLengthLong();
                    InputStream in = c.getInputStream();
                    FileOutputStream out = new FileOutputStream( dest );
                    byte[] buf = new byte[65536];
                    int n;
                    while ( ( n = in.read( buf ) ) > 0 ) {
                        out.write( buf, 0, n );
                        httpDone += n;
                    }
                    out.close();
                    in.close();
                    httpResult = dest.getAbsolutePath();
                    httpState = 2;
                } catch ( Exception e ) {
                    httpResult = String.valueOf( e.getMessage() );
                    httpState = 3;
                }
            }
        } ).start();
    }

    /** Hand the downloaded APK to the system installer (asks for "install unknown apps" first). */
    public boolean installDownloadedApk() {
        try {
            if ( !getPackageManager().canRequestPackageInstalls() ) {
                startActivity( new Intent( Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                           Uri.parse( "package:" + getPackageName() ) ) );
                return false;
            }
            Uri uri = Uri.parse( "content://" + getPackageName() + ".apkprovider/update.apk" );
            Intent intent = new Intent( Intent.ACTION_VIEW );
            intent.setDataAndType( uri, "application/vnd.android.package-archive" );
            intent.addFlags( Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK );
            startActivity( intent );
            return true;
        } catch ( Exception e ) {
            return false;
        }
    }

    /** App-specific external folder (Android/data/<package>/files): reachable over USB, no permission needed. */
    public String getExternalFilesPath() {
        File dir = getExternalFilesDir( null );
        return dir != null ? dir.getAbsolutePath() : "";
    }
}
