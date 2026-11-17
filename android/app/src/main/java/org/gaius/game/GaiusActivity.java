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
//
// Since 2026-11-17 a zip of the game is taken too: in the folder picked (when it
// holds no game folder), and by searchDevice, which looks through the phone's
// own storage and any SD card for a game folder or a zip with one in it. That
// search needs the "All files access" permission (MANAGE_EXTERNAL_STORAGE,
// Android 11 and later; READ_EXTERNAL_STORAGE before), which the system's
// settings page grants; the picker needs none. exportSaves zips Gaius's saves
// folder and lets the player choose where to put the zip.
package org.gaius.game;

import android.Manifest;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.util.Log;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

public class GaiusActivity extends SDLActivity {
    private static final String TAG = "Gaius";
    private static final int PICK_GAME_FOLDER = 4711;
    private static final int EXPORT_SAVES = 4712;
    private static final int STORAGE_PERMISSION = 4713;

    // What importStatus() says once an import is over (platform::ImportResult).
    private static final int STATUS_NONE = 0;       // nothing imported yet, or the picker was dismissed
    private static final int STATUS_DONE = 1;       // the game's files were copied
    private static final int STATUS_NOT_FOUND = 2;  // the folder holds no complete Caesar US release
    private static final int STATUS_FAILED = 3;     // reading or writing failed part-way
    private static final int STATUS_NO_ACCESS = 4;  // the device search was not given the storage permission

    // The files Gaius cannot play without (platform::kEssentialGameFiles), in any letter case.
    private static final String[] ESSENTIAL = {"empire2.001", "houses.pl8", "houses2.pl8", "fixts.pl8", "moremen.pl8",
                                               "shade.256", "font1.pl8"};
    private static final int SEARCH_DEPTH = 4;
    // The device search: how deep below a storage root, and the largest zip it opens.
    private static final int DEVICE_DEPTH = 5;
    private static final long ZIP_LIMIT = 400L * 1024 * 1024;

    private static volatile boolean importing = false;
    private static volatile int copied = 0;
    private static volatile int status = STATUS_NONE;
    // A device search asked for while the permission was being granted: it starts when the player comes back.
    private static volatile boolean searchPending = false;
    private static File exportZip = null;

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
        if (requestCode == EXPORT_SAVES) {
            finishExport(resultCode == RESULT_OK && data != null ? data.getData() : null);
            return;
        }
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
                        // No folder: a zip of the game in the folder picked, or below it.
                        result = STATUS_NOT_FOUND;
                        List<String> zips = new ArrayList<String>();
                        findZips(resolver, tree, root, 0, zips);
                        for (String zipId : zips) {
                            // One unreadable zip does not end the search: the next one is tried.
                            File cached = new File(getCacheDir(), "import.zip");
                            try {
                                try (InputStream in = resolver.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, zipId));
                                     OutputStream out = new FileOutputStream(cached)) {
                                    if (in == null) continue;
                                    copyStream(in, out);
                                }
                                if (importZip(cached, target)) {
                                    result = STATUS_DONE;
                                    break;
                                }
                            } catch (Exception e) {
                                Log.w(TAG, "zip skipped: " + e);
                            } finally {
                                cached.delete();
                            }
                        }
                    } else {
                        // Copied beside the game folder and put in its place only once whole, so a failure part-way
                        // leaves the game that was there.
                        File fresh = freshFolder(target);
                        copyTree(resolver, tree, game, fresh);
                        replaceFolder(fresh, target);
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

    // ---- The device search ---------------------------------------------------------------------------------

    private static boolean hasStorageAccess(SDLActivity activity) {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        if (Build.VERSION.SDK_INT >= 23)
            return activity.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        return true;
    }

    // Called from native code on SDL's thread: look through the phone's storage for the game. Without the permission
    // the system's page for it opens first, and the search starts when the player comes back with it granted.
    public static boolean searchDevice() {
        final SDLActivity activity = (SDLActivity) SDLActivity.getContext();
        if (activity == null || importing) return false;
        status = STATUS_NONE;  // what an earlier import said is not this search's answer
        if (hasStorageAccess(activity)) {
            ((GaiusActivity) activity).startDeviceSearch();
            return true;
        }
        searchPending = true;
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    if (Build.VERSION.SDK_INT >= 30) {
                        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                                   Uri.parse("package:" + activity.getPackageName()));
                        activity.startActivity(intent);
                    } else if (Build.VERSION.SDK_INT >= 23) {
                        activity.requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE}, STORAGE_PERMISSION);
                    }
                } catch (Exception e) {
                    Log.w(TAG, "no permission page: " + e);
                    searchPending = false;
                }
            }
        });
        return true;
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!searchPending) return;
        // Back from the permission page: search if it was granted, else say so -- and forget the request either way,
        // so a permission granted later for another reason does not start a search (and replace the game) by itself.
        searchPending = false;
        if (hasStorageAccess(this)) startDeviceSearch();
        else status = STATUS_NO_ACCESS;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != STORAGE_PERMISSION || !searchPending) return;
        searchPending = false;
        if (hasStorageAccess(this)) startDeviceSearch();
        else status = STATUS_NO_ACCESS;
    }

    // The storage roots: the phone's own, and each SD card's (the part of an app folder before /Android/).
    private List<File> storageRoots() {
        List<File> roots = new ArrayList<File>();
        roots.add(Environment.getExternalStorageDirectory());
        File[] appDirs = getExternalFilesDirs(null);
        if (appDirs != null) {
            for (File dir : appDirs) {
                if (dir == null) continue;
                String path = dir.getAbsolutePath();
                int cut = path.indexOf("/Android/");
                if (cut <= 0) continue;
                File root = new File(path.substring(0, cut));
                boolean known = false;
                for (File r : roots) known |= r.getAbsolutePath().equals(root.getAbsolutePath());
                if (!known) roots.add(root);
            }
        }
        return roots;
    }

    private void startDeviceSearch() {
        if (importing) return;
        final File target = new File(getExternalFilesDir(null), "game");
        importing = true;
        copied = 0;
        status = STATUS_NONE;
        new Thread(new Runnable() {
            @Override
            public void run() {
                int result = STATUS_NOT_FOUND;
                try {
                    List<File> folders = new ArrayList<File>();
                    List<File> zips = new ArrayList<File>();
                    for (File root : storageRoots()) {
                        Log.i(TAG, "searching " + root + (root.canRead() ? "" : " (not readable)"));
                        walk(root, 0, folders, zips);
                    }
                    Log.i(TAG, "found " + folders.size() + " game folders and " + zips.size() + " zips");
                    // A folder called US first (GOG's), then any folder, then the zips.
                    File best = null;
                    for (File f : folders)
                        if (best == null || (f.getName().equalsIgnoreCase("US") && !best.getName().equalsIgnoreCase("US"))) best = f;
                    if (best != null) {
                        File fresh = freshFolder(target);
                        copyFiles(best, fresh);
                        replaceFolder(fresh, target);
                        result = STATUS_DONE;
                    } else {
                        for (File zip : zips) {
                            // One unreadable zip (damaged, encrypted, odd names) does not end the search.
                            Log.i(TAG, "trying " + zip);
                            try {
                                if (importZip(zip, target)) {
                                    result = STATUS_DONE;
                                    break;
                                }
                            } catch (Exception e) {
                                Log.w(TAG, "zip skipped: " + e);
                            }
                        }
                    }
                } catch (Exception e) {
                    Log.w(TAG, "device search failed: " + e);
                    result = STATUS_FAILED;
                } finally {
                    Log.i(TAG, "device search: status " + result + ", " + copied + " files");
                    status = result;
                    importing = false;
                }
            }
        }, "GaiusSearch").start();
    }

    private void walk(File dir, int depth, List<File> folders, List<File> zips) {
        File[] children = dir.listFiles();
        if (children == null) return;
        Set<String> names = new HashSet<String>();
        for (File child : children) {
            if (child.isFile()) {
                String lower = child.getName().toLowerCase(Locale.ROOT);
                names.add(lower);
                if (lower.endsWith(".zip") && child.length() <= ZIP_LIMIT) zips.add(child);
            }
        }
        boolean complete = true;
        for (String file : ESSENTIAL) complete &= names.contains(file);
        if (complete) {
            // Gaius's own copy is not a find.
            if (!dir.getAbsolutePath().contains("/Android/data/")) folders.add(dir);
            return;
        }
        if (depth >= DEVICE_DEPTH) return;
        for (File child : children) {
            if (!child.isDirectory() || child.getName().startsWith(".")) continue;
            if (depth == 0 && child.getName().equals("Android")) continue;
            walk(child, depth + 1, folders, zips);
        }
    }

    private static void copyFiles(File from, File into) throws Exception {
        File[] children = from.listFiles();
        if (children == null) return;
        for (File child : children) {
            if (child.isDirectory()) {
                File sub = new File(into, child.getName());
                sub.mkdirs();
                copyFiles(child, sub);
            } else {
                try (InputStream in = new FileInputStream(child); OutputStream out = new FileOutputStream(new File(into, child.getName()))) {
                    copyStream(in, out);
                }
                copied++;
            }
        }
    }

    // The zip's folder that holds every file Gaius needs (one called US first), extracted into `target` in place of
    // what was there. False if the zip holds no complete game.
    private static boolean importZip(File file, File target) throws Exception {
        try (ZipFile zip = new ZipFile(file)) {
            Map<String, Set<String>> byFolder = new HashMap<String, Set<String>>();
            for (Enumeration<? extends ZipEntry> e = zip.entries(); e.hasMoreElements();) {
                ZipEntry entry = e.nextElement();
                if (entry.isDirectory()) continue;
                String name = entry.getName().replace('\\', '/');
                int slash = name.lastIndexOf('/');
                String folder = slash < 0 ? "" : name.substring(0, slash + 1);
                Set<String> set = byFolder.get(folder);
                if (set == null) byFolder.put(folder, set = new HashSet<String>());
                set.add(name.substring(slash + 1).toLowerCase(Locale.ROOT));
            }
            String chosen = null;
            for (Map.Entry<String, Set<String>> f : byFolder.entrySet()) {
                boolean complete = true;
                for (String need : ESSENTIAL) complete &= f.getValue().contains(need);
                if (!complete) continue;
                boolean us = isUsFolder(f.getKey());
                if (chosen == null || (us && !isUsFolder(chosen))) chosen = f.getKey();
            }
            if (chosen == null) return false;
            File fresh = freshFolder(target);
            for (Enumeration<? extends ZipEntry> e = zip.entries(); e.hasMoreElements();) {
                ZipEntry entry = e.nextElement();
                String name = entry.getName().replace('\\', '/');
                if (entry.isDirectory() || !name.startsWith(chosen)) continue;
                String rest = name.substring(chosen.length());
                if (rest.isEmpty() || rest.contains("..")) continue;
                File out = new File(fresh, rest);
                File parent = out.getParentFile();
                if (parent != null) parent.mkdirs();
                try (InputStream in = zip.getInputStream(entry); OutputStream o = new FileOutputStream(out)) {
                    copyStream(in, o);
                }
                copied++;
            }
            replaceFolder(fresh, target);
            return true;
        }
    }

    // "Caesar/US/" -> true: the folder's own name, not any name ending in "us" (bonus/, campus/).
    private static boolean isUsFolder(String zipFolder) {
        String f = zipFolder.endsWith("/") ? zipFolder.substring(0, zipFolder.length() - 1) : zipFolder;
        int slash = f.lastIndexOf('/');
        return f.substring(slash + 1).equalsIgnoreCase("US");
    }

    // An empty folder beside `target` to copy into; replaceFolder then puts it in target's place.
    private static File freshFolder(File target) {
        File fresh = new File(target.getParentFile(), target.getName() + ".new");
        deleteContents(fresh);
        fresh.mkdirs();
        return fresh;
    }

    private static void replaceFolder(File fresh, File target) throws Exception {
        File old = new File(target.getParentFile(), target.getName() + ".old");
        deleteContents(old);
        old.delete();
        if (target.exists() && !target.renameTo(old)) throw new Exception("cannot move " + target);
        if (!fresh.renameTo(target)) {
            old.renameTo(target);  // put the old game back
            throw new Exception("cannot move " + fresh);
        }
        deleteContents(old);
        old.delete();
    }

    // The zips in or below a picked folder, as far down as the game folder is looked for.
    private static void findZips(ContentResolver resolver, Uri tree, String documentId, int depth, List<String> into) {
        for (Entry e : list(resolver, tree, documentId)) {
            if (e.dir) {
                if (depth < SEARCH_DEPTH) findZips(resolver, tree, e.id, depth + 1, into);
            } else if (e.name.toLowerCase(Locale.ROOT).endsWith(".zip") && e.size <= ZIP_LIMIT) {
                into.add(e.id);
            }
        }
    }

    private static void copyStream(InputStream in, OutputStream out) throws Exception {
        byte[] buffer = new byte[65536];
        int n;
        while ((n = in.read(buffer)) > 0) out.write(buffer, 0, n);
    }

    // ---- Exporting the saves ------------------------------------------------------------------------------

    // Called from native code: the saves folder's files in a zip, then the system's "save as" page for it.
    public static boolean exportSaves(final String savesDir) {
        final SDLActivity activity = (SDLActivity) SDLActivity.getContext();
        if (activity == null) return false;
        try {
            File dir = new File(savesDir);
            File[] files = dir.listFiles();
            if (files == null || files.length == 0) return false;
            File zipFile = new File(activity.getCacheDir(), "gaius-saves.zip");
            try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(zipFile))) {
                for (File f : files) {
                    if (!f.isFile() || f.getName().endsWith(".tmp")) continue;
                    zip.putNextEntry(new ZipEntry("saves/" + f.getName()));
                    try (InputStream in = new FileInputStream(f)) {
                        copyStream(in, zip);
                    }
                    zip.closeEntry();
                }
            }
            exportZip = zipFile;
        } catch (Exception e) {
            Log.w(TAG, "export failed: " + e);
            return false;
        }
        final String name = "gaius-saves-" + new SimpleDateFormat("yyyyMMdd-HHmm", Locale.ROOT).format(new Date()) + ".zip";
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("application/zip");
                intent.putExtra(Intent.EXTRA_TITLE, name);
                try {
                    activity.startActivityForResult(intent, EXPORT_SAVES);
                } catch (Exception e) {
                    Log.w(TAG, "no save-as page: " + e);
                }
            }
        });
        return true;
    }

    private void finishExport(final Uri uri) {
        final File zipFile = exportZip;
        exportZip = null;
        if (zipFile == null) return;
        if (uri == null) {  // the save-as page was cancelled
            zipFile.delete();
            return;
        }
        new Thread(new Runnable() {
            @Override
            public void run() {
                boolean ok = false;
                try (InputStream in = new FileInputStream(zipFile); OutputStream out = getContentResolver().openOutputStream(uri)) {
                    if (out != null) {
                        copyStream(in, out);
                        ok = true;
                    }
                } catch (Exception e) {
                    Log.w(TAG, "export failed: " + e);
                }
                zipFile.delete();
                final boolean done = ok;
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        Toast.makeText(GaiusActivity.this, done ? "Saves exported" : "Could not write the zip", Toast.LENGTH_LONG).show();
                    }
                });
            }
        }, "GaiusExport").start();
    }

    private static final class Entry {
        final String id, name;
        final boolean dir;
        final long size;

        Entry(String id, String name, boolean dir, long size) {
            this.id = id;
            this.name = name;
            this.dir = dir;
            this.size = size;
        }
    }

    private static List<Entry> list(ContentResolver resolver, Uri tree, String documentId) {
        List<Entry> out = new ArrayList<Entry>();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE};
        try (Cursor c = resolver.query(children, columns, null, null, null)) {
            if (c == null) return out;
            while (c.moveToNext()) {
                String name = c.getString(1);
                if (name == null || name.contains("/") || name.equals("..")) continue;
                long size = c.isNull(3) ? 0 : c.getLong(3);
                out.add(new Entry(c.getString(0), name, DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2)), size));
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
