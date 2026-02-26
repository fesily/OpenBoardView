#include "platform.h"
#include "BoardView.h"
#include "utils.h"
#include <SDL.h>
#include <jni.h>
#include <fstream>
#include <sstream>
#include <filesystem>

/*
 * Because we can't call C++ inside a JNI function
 */
void loadFileWrapper(char* path) {
	std::string paths = std::string(path);
	free(path);
	app.LoadFile(paths);
}

#define JAVA_FUNC(name) extern "C" JNIEXPORT void JNICALL Java_com_fesily_openboardview_OBVActivity_##name
constexpr auto java_package = "com/fesily/openboardview/OBVActivity";

JAVA_FUNC(openFileWrapper) (JNIEnv * env, jclass clazz, jstring filePath) {
    const char *utf = env->GetStringUTFChars(filePath, 0);
    if (utf) {
        char *path = SDL_strdup(utf);
        env->ReleaseStringUTFChars(filePath, utf);
        loadFileWrapper(path);
    }
}

const filesystem::path show_file_picker(bool filterBoards) {
	JNIEnv *env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	jclass activity = env->FindClass(java_package);
	jmethodID openFilePicker= env->GetStaticMethodID(activity, "openFilePicker", "()V");

	env->CallStaticVoidMethod(activity, openFilePicker);

	env->DeleteLocalRef(activity);
	return {}; // We have to wait for the result, it will call the above JNI function
}

void select_working_folder() {
	JNIEnv *env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	jclass activity = env->FindClass(java_package);
	jmethodID openFolderPicker = env->GetStaticMethodID(activity, "openFolderPicker", "()V");
	env->CallStaticVoidMethod(activity, openFolderPicker);
	env->DeleteLocalRef(activity);
}

static bool isSafUri(const std::string& path) {
	constexpr std::string_view prefix = "content://";
	return path.rfind(prefix, 0) == 0;
}

std::string file_read_text(const std::string &path) {
	if (!isSafUri(path)) {
		// Fall back to native for regular filesystem paths
		if (!std::filesystem::exists(path)) return {};
		std::ifstream fin(path);
		if (!fin) return {};
		std::ostringstream ss;
		ss << fin.rdbuf();
		return ss.str();
	}

	JNIEnv *env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	jclass activity = env->FindClass(java_package);
	jmethodID readDoc = env->GetStaticMethodID(activity, "readDocumentFile", "(Ljava/lang/String;)[B");

	jstring juriStr = env->NewStringUTF(path.c_str());
	jbyteArray jresult = reinterpret_cast<jbyteArray>(
			env->CallStaticObjectMethod(activity, readDoc, juriStr));
	env->DeleteLocalRef(juriStr);
	env->DeleteLocalRef(activity);

	if (!jresult || env->ExceptionCheck()) {
		env->ExceptionClear();
		return {};
	}

	jsize len = env->GetArrayLength(jresult);
	std::string result(len, '\0');
	env->GetByteArrayRegion(jresult, 0, len, (jbyte*)result.data());
	env->DeleteLocalRef(jresult);
	return result;
}

bool file_write_text(const std::string &path, const std::string &content) {
	if (!isSafUri(path)) {
		std::ofstream fout(path, std::ios::trunc | std::ios::out);
		if (!fout) return false;
		fout << content;
		return fout.good();
	}

	JNIEnv *env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	jclass activity = env->FindClass(java_package);
	jmethodID writeDoc = env->GetStaticMethodID(activity, "writeDocumentFile", "(Ljava/lang/String;[B)Z");

	jstring juriStr = env->NewStringUTF(path.c_str());
	jbyteArray jdata = env->NewByteArray((jsize)content.size());
	env->SetByteArrayRegion(jdata, 0, (jsize)content.size(), (const jbyte*)content.data());

	jboolean ok = env->CallStaticBooleanMethod(activity, writeDoc, juriStr, jdata);

	env->DeleteLocalRef(juriStr);
	env->DeleteLocalRef(jdata);
	env->DeleteLocalRef(activity);

	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return false;
	}
	return ok;
}

std::vector<char> file_as_buffer(const filesystem::path &filepath, std::string &error_msg) {
	if (!isSafUri(filepath.string())){
		return file_as_buffer_native(filepath, error_msg);
	}
	JNIEnv *env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	jclass activity = env->FindClass(java_package);
	jmethodID readFile = env->GetStaticMethodID(activity, "readFile", "(Ljava/lang/String;)[B");

	jstring uris = env->NewStringUTF(filepath.string().c_str());
	jbyteArray jbuffer = reinterpret_cast<jbyteArray>(env->CallStaticObjectMethod(activity, readFile, uris));

	env->DeleteLocalRef(activity);

	// Catch any exception that occured in Java calls to report error message
	if(env->ExceptionCheck()) {
		jthrowable e = env->ExceptionOccurred();
		env->ExceptionDescribe(); // writes to logcat
		env->ExceptionClear();

		jclass clazz = env->GetObjectClass(e);
		jmethodID getMessage = env->GetMethodID(clazz, "getMessage", "()Ljava/lang/String;");
		jstring message = reinterpret_cast<jstring>(env->CallObjectMethod(e, getMessage));
		const char *mstr = env->GetStringUTFChars(message, NULL);
		error_msg = mstr;
		env->ReleaseStringUTFChars(message, mstr);
		env->DeleteLocalRef(message);
		env->DeleteLocalRef(clazz);
		env->DeleteLocalRef(e);

		return {};
	}

	//convert jbyteArray to vector<char>
	jsize len = env->GetArrayLength(jbuffer);
	std::vector<char> fileBuffer(len);
	env->GetByteArrayRegion(jbuffer, 0, len, (jbyte*)fileBuffer.data());

	return fileBuffer;
}

std::string get_asset_path(const char* asset) {
	std::string path = "/sdcard/openboardview";
	path += "/";
	path += asset;
	return path;
}

// Dummy, there is no proper way to search for or enumerate fonts so force Droid Sans
const std::string get_font_path(const std::string &name) {
	return "/system/fonts/DroidSans.ttf";
}

// Don't care about userdir and put everything in the same dir
// Either appplication external storage if available or internal storage
const std::string get_user_dir(const UserDir userdir) {
	std::string path;
	auto extState = SDL_AndroidGetExternalStorageState();
	if (extState & SDL_ANDROID_EXTERNAL_STORAGE_WRITE)
		path = std::string(SDL_AndroidGetExternalStoragePath());
	else
		path = std::string(SDL_AndroidGetInternalStoragePath());
	if (create_dirs(path))
		return path + "/";
	else
		return "./";
}

std::vector<char> load_font(const std::string &name) {
	// Map common font names to Android system font paths
	static const std::pair<const char *, const char *> fontMap[] = {
		{"Roboto",           "/system/fonts/Roboto-Regular.ttf"},
		{"Liberation Sans",  "/system/fonts/NotoSans-Regular.ttf"},
		{"DejaVu Sans",      "/system/fonts/NotoSans-Regular.ttf"},
		{"Arial",            "/system/fonts/NotoSans-Regular.ttf"},
		{"Helvetica",        "/system/fonts/NotoSans-Regular.ttf"},
	};

	std::string fontPath;
	for (const auto &entry : fontMap) {
		if (name == entry.first) {
			fontPath = entry.second;
			break;
		}
	}

	// Fall back to DroidSans for empty name or unknown fonts
	if (fontPath.empty()) {
		fontPath = "/system/fonts/DroidSans.ttf";
	}

	if (!filesystem::exists(fontPath)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Android font path not found: %s", fontPath.c_str());
		return {};
	}

	std::string error_msg;
	auto buf = file_as_buffer_native(fontPath, error_msg);
	if (buf.empty()) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot load Android font '%s' from %s: %s",
		            name.c_str(), fontPath.c_str(), error_msg.c_str());
	}
	return buf;
}

extern float screen_density;
JAVA_FUNC(setScreenDensity) (JNIEnv *env, jobject thiz,
                                                           jfloat density) {
    screen_density = density;
}
