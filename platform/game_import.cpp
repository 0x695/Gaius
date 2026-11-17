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

// The same with one string argument, returning a boolean.
bool call_activity_with(const char* method, const std::string& arg) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return false;
    jclass cls = env->GetObjectClass(activity);
    bool result = false;
    jmethodID id = env->GetStaticMethodID(cls, method, "(Ljava/lang/String;)Z");
    if (id) {
        jstring s = env->NewStringUTF(arg.c_str());
        result = env->CallStaticBooleanMethod(cls, id, s) == JNI_TRUE;
        env->DeleteLocalRef(s);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        result = false;
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return result;
}

}  // namespace

bool can_import_game_folder() { return true; }

bool search_device_for_game() { return call_activity("searchDevice", "()Z") == JNI_TRUE; }

bool can_export_saves() { return true; }

bool export_saves(const std::string& saves_dir) { return call_activity_with("exportSaves", saves_dir); }

bool import_game_folder() { return call_activity("importGameFolder", "()Z") == JNI_TRUE; }

bool import_in_progress(int* files_copied) {
    if (files_copied) {
        int n = 0;
        if (call_activity("importedFileCount", "()I", &n)) *files_copied = n;
    }
    return call_activity("isImporting", "()Z") == JNI_TRUE;
}

ImportResult import_result() {
    int status = 0;
    if (!call_activity("importStatus", "()I", &status)) return ImportResult::None;
    switch (status) {
        case 1: return ImportResult::Done;
        case 2: return ImportResult::NotFound;
        case 3: return ImportResult::Failed;
        case 4: return ImportResult::NoAccess;
        default: return ImportResult::None;
    }
}

#else

bool can_import_game_folder() { return false; }
bool import_game_folder() { return false; }
bool import_in_progress(int* files_copied) {
    if (files_copied) *files_copied = 0;
    return false;
}
ImportResult import_result() { return ImportResult::None; }
bool search_device_for_game() { return false; }
bool can_export_saves() { return false; }
bool export_saves(const std::string&) { return false; }

#endif

}  // namespace gaius::platform
