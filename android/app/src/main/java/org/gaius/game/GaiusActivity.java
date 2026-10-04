// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius -- android/app/src/main/java/org/gaius/game/GaiusActivity.java
//
// The game's activity: SDL's, plus the import of the player's Caesar files
// (platform/game_import.hpp). importGameFolder opens the system's folder
// picker (ACTION_OPEN_DOCUMENT_TREE, no storage permission needed), starting in
// the Download folder where a downloaded or unpacked game usually is. In the
// folder the player chose, a background thread looks for the folder that holds
// Caesar's US files (a GOG install keeps them in a folder called US, below a top
// folder with the international release), and copies that one -- only that one --
// into the app's external files folder, game/, where Gaius looks for them
// (platform/paths.hpp). The native side polls isImporting and importedFileCount,
// and reads importStatus when it is over.
package org.gaius.game;

import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.provider.DocumentsContract;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

public class GaiusActivity extends SDLActivity {
    private static final String TAG = "Gaius";
    private static final int PICK_GAME_FOLDER = 4711;

    // What importStatus() says once an import is over (platform::ImportResult).
    private static final int STATUS_NONE = 0;       // nothing imported yet, or the picker was dismissed
    private static final int STATUS_DONE = 1;       // the game's files were copied
    private static final int STATUS_NOT_FOUND = 2;  // the folder holds no complete Caesar US release
    private static final int STATUS_FAILED = 3;     // reading or writing failed part-way

    // The files Gaius cannot play without (platform::kEssentialGameFiles), in any letter case.
    private static final String[] ESSENTIAL = {"empire2.001", "houses.pl8", "houses2.pl8", "fixts.pl8", "moremen.pl8",
                                               "shade.256", "font1.pl8"};
    private static final int SEARCH_DEPTH = 4;

    private static volatile boolean importing = false;
    private static volatile int copied = 0;
    private static volatile int status = STATUS_NONE;

    @Override
    protected String[] getLibraries() {
        return new String[] {"c++_shared", "SDL2", "main"};
    }

    // Called from native code on SDL's thread.
    public static boolean importGameFolder() {
        final SDLActivity activity = (SDLActivity) SDLActivity.getContext();
        if (activity == null || importing) return false;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                if (Build.VERSION.SDK_INT >= 26) {
                    try {
                        // Start in Download, where a game fetched to the phone is.
                        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                                        DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "primary:Download"));
                    } catch (Exception e) {
                        Log.w(TAG, "no initial folder: " + e);
                    }
                }
                try {
                    activity.startActivityForResult(intent, PICK_GAME_FOLDER);
                } catch (Exception e) {
                    Log.w(TAG, "no folder picker: " + e);
                }
            }
        });
        return true;
    }

    public static boolean isImporting() {
        return importing;
    }

    public static int importedFileCount() {
        return copied;
    }

    public static int importStatus() {
        return status;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_GAME_FOLDER || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final Uri tree = data.getData();
        final File target = new File(getExternalFilesDir(null), "game");
        importing = true;
        copied = 0;
        status = STATUS_NONE;
        new Thread(new Runnable() {
            @Override
            public void run() {
                int result = STATUS_FAILED;
                try {
                    ContentResolver resolver = getContentResolver();
                    String root = DocumentsContract.getTreeDocumentId(tree);
                    String game = findGameFolder(resolver, tree, root, 0);
                    if (game == null) {
                        result = STATUS_NOT_FOUND;
                    } else {
                        // Replace what an earlier import left (a top folder with the game in a folder inside it).
                        deleteContents(target);
                        target.mkdirs();
                        copyTree(resolver, tree, game, target);
                        result = STATUS_DONE;
                    }
                } catch (Exception e) {
                    Log.w(TAG, "import failed: " + e);
                } finally {
                    status = result;
                    importing = false;
                }
            }
        }, "GaiusImport").start();
    }

    private static final class Entry {
        final String id, name;
        final boolean dir;

        Entry(String id, String name, boolean dir) {
            this.id = id;
            this.name = name;
            this.dir = dir;
        }
    }

    private static List<Entry> list(ContentResolver resolver, Uri tree, String documentId) {
        List<Entry> out = new ArrayList<Entry>();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor c = resolver.query(children, columns, null, null, null)) {
            if (c == null) return out;
            while (c.moveToNext()) {
                String name = c.getString(1);
                if (name == null || name.contains("/") || name.equals("..")) continue;
                out.add(new Entry(c.getString(0), name, DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))));
            }
        }
        return out;
    }

    // The document id of the folder, in or below `documentId`, that holds every file Gaius needs; a folder called US
    // is tried before the others (GOG's top folder holds the international release too). Null if there is none.
    private static String findGameFolder(ContentResolver resolver, Uri tree, String documentId, int depth) {
        List<Entry> entries = list(resolver, tree, documentId);
        Set<String> names = new HashSet<String>();
        List<Entry> folders = new ArrayList<Entry>();
        for (Entry e : entries) {
            if (e.dir) folders.add(e);
            else names.add(e.name.toLowerCase(Locale.ROOT));
        }
        boolean complete = true;
        for (String file : ESSENTIAL) complete &= names.contains(file);
        if (complete) return documentId;
        if (depth >= SEARCH_DEPTH) return null;
        // US first, then the rest in the order the provider lists them.
        for (int pass = 0; pass < 2; ++pass) {
            for (Entry e : folders) {
                if ((pass == 0) != e.name.equalsIgnoreCase("US")) continue;
                String found = findGameFolder(resolver, tree, e.id, depth + 1);
                if (found != null) return found;
            }
        }
        return null;
    }

    private static void deleteContents(File dir) {
        File[] children = dir.listFiles();
        if (children == null) return;
        for (File child : children) {
            if (child.isDirectory()) deleteContents(child);
            child.delete();
        }
    }

    private static void copyTree(ContentResolver resolver, Uri tree, String documentId, File into) throws Exception {
        for (Entry e : list(resolver, tree, documentId)) {
            if (e.dir) {
                File sub = new File(into, e.name);
                sub.mkdirs();
                copyTree(resolver, tree, e.id, sub);
                continue;
            }
            Uri file = DocumentsContract.buildDocumentUriUsingTree(tree, e.id);
            try (InputStream in = resolver.openInputStream(file);
                 OutputStream out = new FileOutputStream(new File(into, e.name))) {
                if (in == null) continue;
                byte[] buffer = new byte[65536];
                int n;
                while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
            }
            copied++;
        }
    }
}
