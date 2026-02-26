package com.fesily.openboardview;

import org.libsdl.app.SDLActivity;

import android.Manifest;
import android.os.Build;
import android.os.Bundle;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.content.pm.PackageManager;
import android.util.DisplayMetrics;
import android.util.Log;
import android.widget.Toast;
import android.net.Uri;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.documentfile.provider.DocumentFile;

import java.io.IOException;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;
import java.util.List;

import android.provider.DocumentsContract;

import java.io.OutputStream;

import android.os.Looper;

public class OBVActivity extends SDLActivity {
    private static final int FOLDER_SELECT_CODE = 1;
    private static final String TAG = "[OBV]";
    private static final String PREF_NAME = "OBVPrefs";
    private static final String PREF_WORKING_FOLDER = "working_folder_uri";
    private static OBVActivity activity;

    @Override
    protected String[] getLibraries() {
        return new String[]{
                "SDL2",
                // "SDL2_image",
                // "SDL2_mixer",
                // "SDL2_net",
                // "SDL2_ttf",
                "openboardview"
        };
    }

    // -----------------------------------------------------------------------
    // 工作文件夹持久化
    // -----------------------------------------------------------------------

    private static SharedPreferences getPrefs() {
        return activity.getSharedPreferences(PREF_NAME, MODE_PRIVATE);
    }

    /** 取得持久化保存的工作文件夹 URI，未设置则返回 null */
    private static Uri getWorkingFolderUri() {
        String uriStr = getPrefs().getString(PREF_WORKING_FOLDER, null);
        return uriStr != null ? Uri.parse(uriStr) : null;
    }

    /** 持久化保存工作文件夹 URI，同时申请可持久化权限 */
    private static void saveWorkingFolderUri(Uri uri) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
            try {
                activity.getContentResolver().takePersistableUriPermission(
                        uri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                );
            } catch (SecurityException e) {
                Log.w(TAG, "Could not take persistable permission: " + e);
            }
        }
        getPrefs().edit().putString(PREF_WORKING_FOLDER, uri.toString()).apply();
        Log.d(TAG, "Working folder saved: " + uri);
    }

    // -----------------------------------------------------------------------
    // 打开文件夹选择器（选择工作目录）
    // -----------------------------------------------------------------------

    public static void exportFolderPicker() {
        openFolderPicker();
    }

    public static void openFolderPicker() {
        if (!Looper.getMainLooper().isCurrentThread()) {
            activity.runOnUiThread(OBVActivity::openFolderPicker);
            return;
        }
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
            Toast.makeText(activity, "Android version too old to use folder picker.", Toast.LENGTH_SHORT).show();
            return;
        }
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
            activity.startActivityForResult(
                    Intent.createChooser(intent, "Select working folder"),
                    FOLDER_SELECT_CODE
            );
        } catch (android.content.ActivityNotFoundException ex) {
            Toast.makeText(activity, "Please install a File Manager.", Toast.LENGTH_SHORT).show();
        }
    }

    // -----------------------------------------------------------------------
    // 文件选择器（读取 SAF 文件夹，直接传 content:// URI 给 C++）
    // -----------------------------------------------------------------------

    public static void openFilePicker() {
        if (!Looper.getMainLooper().isCurrentThread()) {
            activity.runOnUiThread(OBVActivity::openFilePicker);
            return;
        }

        Uri folderUri = getWorkingFolderUri();
        if (folderUri == null) {
            // 还没有设置工作文件夹，先让用户选择
            new AlertDialog.Builder(activity)
                    .setTitle("Select Working Folder")
                    .setMessage("No working folder set. Please select a folder that contains board files.")
                    .setPositiveButton("Select Folder", (d, w) -> openFolderPicker())
                    .setNegativeButton("Cancel", null)
                    .show();
            return;
        }

        DocumentFile folder = DocumentFile.fromTreeUri(activity, folderUri);
        if (folder == null || !folder.isDirectory()) {
            Log.w(TAG, "Saved working folder is no longer accessible, clearing.");
            getPrefs().edit().remove(PREF_WORKING_FOLDER).apply();
            Toast.makeText(activity, "Working folder is no longer accessible. Please re-select.", Toast.LENGTH_LONG).show();
            openFolderPicker();
            return;
        }

        // 收集所有文件（含子目录，显示相对路径）
        List<String> displayNames = new ArrayList<>();
        List<Uri> fileUris = new ArrayList<>();
        collectFiles(folder, "", displayNames, fileUris);

        if (displayNames.isEmpty()) {
            new AlertDialog.Builder(activity)
                    .setTitle("No Files Found")
                    .setMessage("The selected folder contains no files.\n\nFolder: " + folderUri.getPath())
                    .setPositiveButton("Change Folder", (d, w) -> openFolderPicker())
                    .setNegativeButton("Cancel", null)
                    .show();
            return;
        }

        new AlertDialog.Builder(activity)
                .setTitle("Select File")
                .setItems(displayNames.toArray(new String[0]), (dialog, which) -> {
                    Uri selectedUri = fileUris.get(which);
                    Log.d(TAG, "File selected: " + selectedUri);
                    openFileWrapper(selectedUri.toString());
                })
                .setNeutralButton("Change Folder", (d, w) -> openFolderPicker())
                .setNegativeButton("Cancel", null)
                .show();
    }

    /**
     * 递归收集 DocumentFile 文件夹下的所有文件（不含目录本身）。
     * displayNames 中的条目格式为 "subdir/filename.brd"。
     */
    private static boolean isExcludedFile(@NonNull String name) {
        String lower = name.toLowerCase();
        return lower.endsWith(".yaml") || lower.endsWith(".yml")
                || lower.endsWith(".sqlite3") || lower.endsWith(".db");
    }

    private static void collectFiles(@NonNull DocumentFile dir, @NonNull String prefix,
                                     @NonNull List<String> displayNames,
                                     @NonNull List<Uri> fileUris) {
        for (DocumentFile child : dir.listFiles()) {
            String name = child.getName();
            if (name == null) continue;
            if (child.isDirectory()) {
                collectFiles(child, prefix + name + "/", displayNames, fileUris);
            } else if (child.isFile() && !isExcludedFile(name)) {
                displayNames.add(prefix + name);
                fileUris.add(child.getUri());
            }
        }
    }

    // -----------------------------------------------------------------------
    // 供 C++ JNI 调用：读取 SAF 文件内容
    // -----------------------------------------------------------------------

    public static byte[] readFile(String suri) throws IOException {
        Uri uri = Uri.parse(suri);
        InputStream inputStream = activity.getContentResolver().openInputStream(uri);
        if (inputStream == null) {
            throw new IOException("Cannot open input stream for: " + suri);
        }
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        byte[] data = new byte[4096];
        int nRead;
        while ((nRead = inputStream.read(data, 0, data.length)) != -1) {
            buffer.write(data, 0, nRead);
        }
        buffer.flush();
        inputStream.close();
        return buffer.toByteArray();
    }

    /**
     * Read a document by its content:// URI. Returns null if not accessible or not found.
     */
    public static byte[] readDocumentFile(String documentUriStr) {
        try {
            Uri uri = Uri.parse(documentUriStr);
            InputStream is = activity.getContentResolver().openInputStream(uri);
            if (is == null) return null;
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = is.read(buf)) != -1) baos.write(buf, 0, n);
            is.close();
            return baos.toByteArray();
        } catch (Exception e) {
            Log.d(TAG, "readDocumentFile: " + e.getMessage() + " uri=" + documentUriStr);
            return null;
        }
    }

    /**
     * Write data to a document given its content:// URI.
     * If the document does not yet exist, it is created inside the same folder within the
     * working tree, derived from the URI's document-ID path.
     * Returns true on success.
     */
    public static boolean writeDocumentFile(String documentUriStr, byte[] data) {
        try {
            Uri uri = Uri.parse(documentUriStr);

            // First try: open existing document for writing (truncating)
            try (OutputStream os = activity.getContentResolver().openOutputStream(uri, "wt")) {
                if (os != null) {
                    os.write(data);
                    return true;
                }
            } catch (Exception ignored) {}

            // Second try: create the document via its parent folder
            Uri treeUri = getWorkingFolderUri();
            if (treeUri == null) {
                Log.e(TAG, "writeDocumentFile: no working folder saved");
                return false;
            }
            String docId = DocumentsContract.getDocumentId(uri);
            int lastSlash = docId.lastIndexOf('/');
            String fileName   = lastSlash >= 0 ? docId.substring(lastSlash + 1) : docId;
            String parentDocId = lastSlash >= 0
                    ? docId.substring(0, lastSlash)
                    : DocumentsContract.getTreeDocumentId(treeUri);

            Uri parentUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, parentDocId);
            Uri newUri = DocumentsContract.createDocument(
                    activity.getContentResolver(), parentUri, "application/octet-stream", fileName);
            if (newUri == null) {
                Log.e(TAG, "writeDocumentFile: createDocument failed for " + fileName);
                return false;
            }
            try (OutputStream os = activity.getContentResolver().openOutputStream(newUri)) {
                if (os == null) return false;
                os.write(data);
            }
            return true;
        } catch (Exception e) {
            Log.e(TAG, "writeDocumentFile failed for " + documentUriStr + ": " + e);
            return false;
        }
    }

    // -----------------------------------------------------------------------
    // Native 方法
    // -----------------------------------------------------------------------

    public static native void openFileWrapper(String filePath);

    // -----------------------------------------------------------------------
    // 生命周期
    // -----------------------------------------------------------------------

    @Override
    protected void onCreate(Bundle bundle) {
        this.activity = this;
        Log.d(TAG, "started");
        super.onCreate(bundle);
        checkPerms();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == FOLDER_SELECT_CODE) {
            if (resultCode == RESULT_OK && data != null) {
                Uri folderUri = data.getData();
                if (folderUri != null) {
                    saveWorkingFolderUri(folderUri);
                    Toast.makeText(activity, "Working folder set.", Toast.LENGTH_SHORT).show();
                    // 文件夹选好后立即打开文件选择器
                    openFilePicker();
                } else {
                    Log.e(TAG, "Folder URI is null");
                }
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    // -----------------------------------------------------------------------
    // 权限检查
    // -----------------------------------------------------------------------

    void checkPerms() {
        // No need to ask for perms before Marshmallow
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return;
        }

        DisplayMetrics metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getMetrics(metrics);
        setScreenDensity(metrics.density);
    }

    public native void setScreenDensity(float density);
}
