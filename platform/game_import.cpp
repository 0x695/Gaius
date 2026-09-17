// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/game_import.hpp"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace gaius::platform {

bool looks_like_game_folder(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return false;
    bool scenario = false, sprites = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string name = entry.path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (name == "EMPIRE2.001") scenario = true;
        if (name == "HOUSES.PL8") sprites = true;
    }
    return scenario && sprites;
}

#if defined(__ANDROID__)

namespace {

// Calls a static method of the running activity's class (GaiusActivity).
jboolean call_activity(const char* method, const char* signature, int* out_int = nullptr) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return JNI_FALSE;
    jclass cls = env->GetObjectClass(activity);
    jboolean result = JNI_FALSE;
    jmethodID id = env->GetStaticMethodID(cls, method, signature);
    if (id) {
        if (out_int) {
            *out_int = env->CallStaticIntMethod(cls, id);
            result = JNI_TRUE;
        } else {
            result = env->CallStaticBooleanMethod(cls, id);
        }
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        result = JNI_FALSE;
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return result;
}

}  // namespace

bool can_import_game_folder() { return true; }

bool import_game_folder() { return call_activity("importGameFolder", "()Z") == JNI_TRUE; }

bool import_in_progress(int* files_copied) {
    if (files_copied) {
        int n = 0;
        if (call_activity("importedFileCount", "()I", &n)) *files_copied = n;
    }
    return call_activity("isImporting", "()Z") == JNI_TRUE;
}

#else

bool can_import_game_folder() { return false; }
bool import_game_folder() { return false; }
bool import_in_progress(int* files_copied) {
    if (files_copied) *files_copied = 0;
    return false;
}

#endif

}  // namespace gaius::platform
