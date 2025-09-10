package com.fesily.openboardview;

import org.libsdl.app.SDLActivity;

import android.Manifest;
import android.os.Build;
import android.os.Bundle;
import android.app.Activity;
import android.content.Intent;
import android.content.UriPermission;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.util.DisplayMetrics;
import android.util.Log;
import android.widget.Toast;
import android.net.Uri;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.documentfile.provider.DocumentFile;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.util.ArrayList;

import android.os.Looper;

public class OBVActivity extends SDLActivity {
    private static final int FILE_SELECT_CODE = 0;
    private static final int FOLDER_SELECT_CODE = 1;
    private static final String TAG = "[OBV]";
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

    public static void openFilePicker() {
        if (!Looper.getMainLooper().isCurrentThread()) {
            activity.runOnUiThread(OBVActivity::openFilePicker);
            return;
        }
        try {
            File privateDir = activity.getFilesDir(); // 或使用 getExternalFilesDir(null) 获取外部私有目录
            File[] files = privateDir.listFiles(); // 列出目录下的所有文件

            // 提取文件名列表
            ArrayList<String> fileNames = new ArrayList<>();
            if (files != null) {
                for (File file : files) {
                    if (file.isFile()) { // 只显示文件，忽略子目录
                        fileNames.add(file.getName());
                    }
                }
            }

            // 如果没有文件，提示用户
            if (fileNames.isEmpty()) {
                new AlertDialog.Builder(activity)
                        .setTitle("提示")
                        .setMessage("私有目录下没有文件")
                        .setPositiveButton("确定", null)
                        .show();
                return;
            }

            // 创建文件选择对话框
            new AlertDialog.Builder(activity)
                    .setTitle("选择文件")
                    .setItems(fileNames.toArray(new String[0]), (dialog, which) -> {
                        String selectedFileName = fileNames.get(which);
                        File selectedFile = new File(privateDir, selectedFileName);
                        // 处理选中的文件，例如读取内容
                        openFileWrapper(selectedFile.getAbsolutePath());
                    })
                    .setNegativeButton("取消", null)
                    .show();

        } catch (Exception e) {
            Log.e(TAG, "Error opening file picker: " + e.getMessage());
            Toast.makeText(activity, "Error accessing private files", Toast.LENGTH_SHORT).show();
        }
    }

    public static void exportFolderPicker() {
        Log.d(TAG, "Opening file picker");

        Intent folderIntent;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            folderIntent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        } else {
            Toast.makeText(activity, "Folder selection not supported on this Android version. Selecting file instead.", Toast.LENGTH_SHORT).show();
            return;
        }
        try {
            activity.startActivityForResult(Intent.createChooser(folderIntent, "Select a Folder"), FOLDER_SELECT_CODE);  // 建议使用不同的 request code，如 FOLDER_SELECT_CODE
        } catch (android.content.ActivityNotFoundException ex) {
            Toast.makeText(activity, "Please install a File Manager.", Toast.LENGTH_SHORT).show();
        }
    }

    public static byte[] readFile(String suri) throws IOException {
        Uri uri = Uri.parse(suri);

        if (!activity.takePersistentPerms(uri)) {
            Log.e(TAG, "Persistent read permission for " + uri + " not granted.");
            return new byte[0];
        }

        InputStream inputStream = activity.getContentResolver().openInputStream(uri);

        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        int nRead;
        byte[] data = new byte[1024];

        while ((nRead = inputStream.read(data, 0, data.length)) != -1) {
            buffer.write(data, 0, nRead);
        }

        buffer.flush();
        return buffer.toByteArray();
    }

    public static native void openFileWrapper(String filePath);

    @Override
    protected void onCreate(Bundle bundle) {
        this.activity = this;
        Log.d(TAG, "started");
        super.onCreate(bundle);
        checkPerms();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        switch (requestCode) {
            case FILE_SELECT_CODE: {
                if (resultCode == RESULT_OK) {
                    // Get the Uri of the selected file
                    Uri uri = data.getData();
                    openFileWrapper(uri.toString());
                }
                break;
            }
            case FOLDER_SELECT_CODE: {
                if (resultCode == RESULT_OK && data != null) {
                    Uri folderUri = data.getData();
                    if (folderUri != null) {
                        DocumentFile sourceFolder = DocumentFile.fromTreeUri(activity, folderUri);
                        if (sourceFolder != null && sourceFolder.isDirectory()) {
                            Handler mainHandler = new Handler(Looper.getMainLooper());
                            new Thread(() -> {
                                File destDir = new File(activity.getFilesDir().getPath());
                                String msg = "";
                                try {
                                    copyFolder(activity, sourceFolder, destDir);
                                    Log.d(TAG, "Folder copied to: " + destDir.getAbsolutePath());
                                    msg = "Folder copied successfully!";

                                } catch (IOException e) {
                                    Log.e(TAG, "Failed to copy folder: " + e.getMessage());
                                    msg = "Failed to copy folder: " + e.getMessage();
                                }
                                String finalMsg = msg;
                                mainHandler.post(() -> {
                                        Toast.makeText(activity, finalMsg, Toast.LENGTH_SHORT).show();
                                    });
                            }).start();

                        } else {
                            Log.e(TAG, "Invalid folder selected");
                            android.widget.Toast.makeText(activity, "Invalid folder selected", android.widget.Toast.LENGTH_SHORT).show();
                        }
                    }
                }
                break;
            }
        }

        super.onActivityResult(requestCode, resultCode, data);
    }

    // 递归复制文件夹内容
    private static void copyFolder(Activity activity, @NonNull DocumentFile sourceFolder, @NonNull File destDir) throws IOException {
        if (!destDir.exists() && !destDir.mkdirs()) {
            throw new IOException("Failed to create destination directory: " + destDir.getAbsolutePath());
        }

        for (DocumentFile file : sourceFolder.listFiles()) {
            String fileName = file.getName();
            if (fileName == null) {
                fileName = "unnamed_" + System.currentTimeMillis(); // 防止空文件名
            }
            File destFileOrDir = new File(destDir, fileName);

            if (file.isDirectory()) {
                // 递归复制子文件夹
                copyFolder(activity, file, destFileOrDir);
            } else if (file.isFile()) {
                // 复制文件
                try (InputStream in = activity.getContentResolver().openInputStream(file.getUri());
                     FileOutputStream out = new FileOutputStream(destFileOrDir)) {
                    if (in == null) {
                        throw new IOException("Failed to open input stream for: " + fileName);
                    }
                    byte[] buffer = new byte[4096];
                    int bytesRead;
                    while ((bytesRead = in.read(buffer)) != -1) {
                        out.write(buffer, 0, bytesRead);
                    }
                } catch (IOException e) {
                    Log.e(TAG, "Failed to copy file: " + fileName + ", error: " + e.getMessage());
                    throw e; // 继续抛出异常，通知上层调用者
                }
            }
        }
    }

    boolean takePersistentPerms(Uri uri) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
            try {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);

                for (UriPermission up : getContentResolver().getPersistedUriPermissions()) {
                    if (up.getUri().equals(uri) && up.isReadPermission()) {
                        return true;
                    }
                }
            } catch (SecurityException e) {
                Log.e(TAG, "Persistent permission error: " + e);
            }
            return false;
        }
        return true;
    }

    void checkPerms() {
        // No need to ask for perms before marshmallow
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return;
        }

        if (checkSelfPermission(Manifest.permission.MANAGE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.MANAGE_EXTERNAL_STORAGE}, 0);
        }

        DisplayMetrics metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getMetrics(metrics);
        float density = metrics.density;

        setScreenDensity(density);
    }

    public native void setScreenDensity(float density);
}
