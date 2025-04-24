//
// Created by standa on 05.01.25.
//
#include "state_storage.h"

StateStorage::StateStorage(android_app *app) {
    // Check if the current thread is already attached to the JVM
    if (app->activity->vm->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) != JNI_OK) {
        // Attach the thread to the JVM
        if (app->activity->vm->AttachCurrentThread(&env_, nullptr) != JNI_OK) {
            throw std::runtime_error("Failed to attach thread!");
        }
    }

    context_ = app->activity->clazz;
};

void StateStorage::SaveStreamingConfig(const StreamingConfig &streamingConfig) {
    jclass contextClass = env_->GetObjectClass(context_);
    jmethodID getSharedPreferences = env_->GetMethodID(contextClass, "getSharedPreferences",
                                                      "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");

    jstring prefsName = env_->NewStringUTF("StreamingConfigPrefs");
    jobject sharedPreferences = env_->CallObjectMethod(context_, getSharedPreferences, prefsName, 0);
    env_->DeleteLocalRef(prefsName);

    jclass prefsClass = env_->GetObjectClass(sharedPreferences);
    jmethodID edit = env_->GetMethodID(prefsClass, "edit",
                                      "()Landroid/content/SharedPreferences$Editor;");
    jobject editor = env_->CallObjectMethod(sharedPreferences, edit);

    jclass editorClass = env_->GetObjectClass(editor);
    jmethodID putString = env_->GetMethodID(editorClass, "putString",
                                           "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");

    {
        SaveKeyValuePair(editor, putString, "headset_ip", IpToString(streamingConfig.headset_ip));
        SaveKeyValuePair(editor, putString, "jetson_ip", IpToString(streamingConfig.jetson_ip));
        SaveKeyValuePair(editor, putString, "port_left", streamingConfig.portLeft);
        SaveKeyValuePair(editor, putString, "port_right", streamingConfig.portRight);
        SaveKeyValuePair(editor, putString, "codec", streamingConfig.codec);
        SaveKeyValuePair(editor, putString, "encoding_quality", streamingConfig.encodingQuality);
        SaveKeyValuePair(editor, putString, "bitrate", streamingConfig.bitrate);
        SaveKeyValuePair(editor, putString, "resolution", streamingConfig.resolution.getLabel());
        SaveKeyValuePair(editor, putString, "video_mode", streamingConfig.videoMode);
        SaveKeyValuePair(editor, putString, "fps", streamingConfig.fps);
    }


    jmethodID apply = env_->GetMethodID(editorClass, "apply", "()V");
    env_->CallVoidMethod(editor, apply);

    env_->DeleteLocalRef(editor);
    env_->DeleteLocalRef(editorClass);
    env_->DeleteLocalRef(sharedPreferences);
    env_->DeleteLocalRef(prefsClass);
    env_->DeleteLocalRef(contextClass);
}

bool StateStorage::SaveKeyValuePair(jobject editor, jmethodID putString, const std::string& key, const std::string& value) {
    // Convert key and value to jstring
    jstring jKey = env_->NewStringUTF(key.c_str());
    jstring jValue = env_->NewStringUTF(value.c_str());

    // Call the Java method
    env_->CallObjectMethod(editor, putString, jKey, jValue);

    // Clean up local references
    env_->DeleteLocalRef(jKey);
    env_->DeleteLocalRef(jValue);

    return true;
}


bool StateStorage::SaveKeyValuePair(jobject editor, jmethodID putString, const std::string& key, const int value) {
    return SaveKeyValuePair(editor, putString, key, std::to_string(value));
}


StreamingConfig StateStorage::LoadStreamingConfig() {

    jclass contextClass = env_->GetObjectClass(context_);
    jmethodID getSharedPreferences = env_->GetMethodID(contextClass, "getSharedPreferences", "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");

    jstring prefsName = env_->NewStringUTF("StreamingConfigPrefs");
    jobject sharedPreferences = env_->CallObjectMethod(context_, getSharedPreferences, prefsName, 0);
    env_->DeleteLocalRef(prefsName);

    jclass prefsClass = env_->GetObjectClass(sharedPreferences);
    jmethodID getString = env_->GetMethodID(prefsClass, "getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");

    StreamingConfig streamingConfig;
    try {

        streamingConfig.headset_ip = StringToIp(LoadValue(sharedPreferences, getString, "headset_ip"));
        streamingConfig.jetson_ip = StringToIp(LoadValue(sharedPreferences, getString, "jetson_ip"));
        streamingConfig.portLeft = std::stoi(LoadValue(sharedPreferences, getString, "port_left"));
        streamingConfig.portRight = std::stoi(LoadValue(sharedPreferences, getString, "port_right"));
        streamingConfig.codec = Codec(std::stoi(LoadValue(sharedPreferences, getString, "codec")));
        streamingConfig.encodingQuality = std::stoi(LoadValue(sharedPreferences, getString, "encoding_quality"));
        streamingConfig.bitrate = std::stoi(LoadValue(sharedPreferences, getString, "bitrate"));
        streamingConfig.resolution = CameraResolution::fromLabel(LoadValue(sharedPreferences, getString, "resolution"));
        streamingConfig.videoMode = VideoMode(std::stoi(LoadValue(sharedPreferences, getString, "video_mode")));
        streamingConfig.fps = std::stoi(LoadValue(sharedPreferences, getString, "fps"));

    } catch(std::exception) {
        env_->DeleteLocalRef(sharedPreferences);
        env_->DeleteLocalRef(prefsClass);
        env_->DeleteLocalRef(contextClass);

        return StreamingConfig();
        //throw std::runtime_error("Couldn't load from the shared preferences");
    }

    env_->DeleteLocalRef(sharedPreferences);
    env_->DeleteLocalRef(prefsClass);
    env_->DeleteLocalRef(contextClass);

    return streamingConfig;
}

std::string StateStorage::LoadValue(jobject& sharedPreferences, jmethodID& getString, const std::string& key) {
    jstring jKey = env_->NewStringUTF(key.c_str());
    jstring jDefaultValue = env_->NewStringUTF("unknown");
    jstring jResult = (jstring)env_->CallObjectMethod(sharedPreferences, getString, jKey, jDefaultValue);

    env_->DeleteLocalRef(jKey);
    env_->DeleteLocalRef(jDefaultValue);

    const char* resultCStr = env_->GetStringUTFChars(jResult, nullptr);
    std::string result(resultCStr);

    return result;
}
