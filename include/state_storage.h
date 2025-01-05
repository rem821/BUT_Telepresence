//
// Created by standa on 05.01.25.
//
#pragma once

void SaveStreamingConfig(JNIEnv* env, jobject context, const Stre) {
    jclass contextClass = env->GetObjectClass(context);
    jmethodID getSharedPreferences = env->GetMethodID(contextClass, "getSharedPreferences", "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");

    jstring prefsName = env->NewStringUTF("MyAppPrefs");
    jobject sharedPreferences = env->CallObjectMethod(context, getSharedPreferences, prefsName, 0);
    env->DeleteLocalRef(prefsName);

    jclass prefsClass = env->GetObjectClass(sharedPreferences);
    jmethodID edit = env->GetMethodID(prefsClass, "edit", "()Landroid/content/SharedPreferences$Editor;");
    jobject editor = env->CallObjectMethod(sharedPreferences, edit);

    jclass editorClass = env->GetObjectClass(editor);
    jmethodID putString = env->GetMethodID(editorClass, "putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");

    jstring jKey = env->NewStringUTF(key.c_str());
    jstring jValue = env->NewStringUTF(value.c_str());
    env->CallObjectMethod(editor, putString, jKey, jValue);
    env->DeleteLocalRef(jKey);
    env->DeleteLocalRef(jValue);

    jmethodID apply = env->GetMethodID(editorClass, "apply", "()V");
    env->CallVoidMethod(editor, apply);

    // Clean up
    env->DeleteLocalRef(editor);
    env->DeleteLocalRef(editorClass);
    env->DeleteLocalRef(sharedPreferences);
    env->DeleteLocalRef(prefsClass);
    env->DeleteLocalRef(contextClass);
}