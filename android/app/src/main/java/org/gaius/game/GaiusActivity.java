// SPDX-License-Identifier: GPL-3.0-or-later
// Gaius -- android/app/src/main/java/org/gaius/game/GaiusActivity.java
//
// The game's activity: SDL's, plus the import of the player's Caesar files
// (platform/game_import.hpp). importGameFolder opens the system's folder
// picker (ACTION_OPEN_DOCUMENT_TREE, no storage permission needed); the chosen
// folder's files, and those of its subfolders, are copied on a background
// thread into the app's external files folder, game/, where Gaius looks for
// them (platform/paths.hpp). The native side polls isImporting and
// importedFileCount.
package org.gaius.game;

import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class GaiusActivity extends SDLActivity {
    private static final String TAG = "Gaius";
    private static final int PICK_GAME_FOLDER = 4711;

    private static volatile boolean importing = false;
    private static volatile int copied = 0;

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

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_GAME_FOLDER || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final Uri tree = data.getData();
        final File target = new File(getExternalFilesDir(null), "game");
        importing = true;
        copied = 0;
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    target.mkdirs();
                    copyTree(getContentResolver(), tree, DocumentsContract.getTreeDocumentId(tree), target);
                } catch (Exception e) {
                    Log.w(TAG, "import failed: " + e);
                } finally {
                    importing = false;
                }
            }
        }, "GaiusImport").start();
    }

    private static void copyTree(ContentResolver resolver, Uri tree, String documentId, File into) throws Exception {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor c = resolver.query(children, columns, null, null, null)) {
            if (c == null) return;
            while (c.moveToNext()) {
                String id = c.getString(0);
                String name = c.getString(1);
                String mime = c.getString(2);
                if (name == null || name.contains("/") || name.equals("..")) continue;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    File sub = new File(into, name);
                    sub.mkdirs();
                    copyTree(resolver, tree, id, sub);
                    continue;
                }
                Uri file = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                try (InputStream in = resolver.openInputStream(file);
                     OutputStream out = new FileOutputStream(new File(into, name))) {
                    if (in == null) continue;
                    byte[] buffer = new byte[65536];
                    int n;
                    while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
                }
                copied++;
            }
        }
    }
}
