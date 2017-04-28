//
//  SimpleInputPlugin.cpp
//
//  Created by Alexander Bendl on 02/03/17.
//  Copyright © 2017 Wikitude. All rights reserved.
//

#include <algorithm>
#include "SimpleInputPlugin.h"

#include "jniHelper.h"

SimpleInputPlugin* SimpleInputPlugin::instance;
jobject simpleInputPluginActivity;
bool rotatedCamera;


SimpleInputPlugin::SimpleInputPlugin()
        :
        InputPlugin("plugin.input.yuv.simple"),
        _jniInitialized(false) {
    SimpleInputPlugin::instance = this;
}

SimpleInputPlugin::~SimpleInputPlugin() {
    JavaVMResource vm(pluginJavaVM);
    vm.env->DeleteGlobalRef(simpleInputPluginActivity);
}

/**
 * Will be called once after your Plugin was successfully added to the Wikitude Engine. Initialize your plugin here.
 */
void SimpleInputPlugin::initialize() {
    getFrameSettings().setInputFrameColorSpace(wikitude::sdk::FrameColorSpace::YUV_420_NV21);
    callInitializedJNIMethod(_pluginInitializedMethodId);
}

/**
 * Will be called every time the Wikitude Engine pauses.
 */
void SimpleInputPlugin::pause() {
    _running = false;
    callInitializedJNIMethod(_pluginPausedMethodId);
}

/**
 * Will be called when the Wikitude Engine starts for the first time and after every pause.
 *
 * @param pausedTime_ the duration of the pause in milliseconds
 */
void SimpleInputPlugin::resume(unsigned int pausedTime_) {
    _running = true;
    callInitializedJNIMethod(_pluginResumedMethodId);
}

/**
 * Will be called when the Wikitude Engine shuts down.
 */
void SimpleInputPlugin::destroy() {
    callInitializedJNIMethod(_pluginDestroyedMethodId);
}

/**
 * Will be called after every image recognition cycle.
 *
 * @param recognizedTargets_ list of recognized targets, empty if no target was recognized
 */
void SimpleInputPlugin::update(const std::list<wikitude::sdk::RecognizedTarget>& recognizedTargets_) {
    if ( !_jniInitialized ) {
        JavaVMResource vm(pluginJavaVM);
        jclass simpleInputPluginActivityClass = vm.env->GetObjectClass(simpleInputPluginActivity);
        _pluginInitializedMethodId = vm.env->GetMethodID(simpleInputPluginActivityClass, "onInputPluginInitialized", "()V");
        _pluginPausedMethodId = vm.env->GetMethodID(simpleInputPluginActivityClass, "onInputPluginPaused", "()V");
        _pluginResumedMethodId = vm.env->GetMethodID(simpleInputPluginActivityClass, "onInputPluginResumed", "()V");
        _pluginDestroyedMethodId = vm.env->GetMethodID(simpleInputPluginActivityClass, "onInputPluginDestroyed", "()V");

        _jniInitialized = true;

        callInitializedJNIMethod(_pluginInitializedMethodId);
        callInitializedJNIMethod(_pluginResumedMethodId);
    }
}

/**
 * Defines if input frames should be rendered by the Wikitude SDK or not.
 */
bool SimpleInputPlugin::requestsInputFrameRendering() {
    return true;
}

/*
 * Defines if input frames should be processed by the Wikitude SDK or not.
 */
bool SimpleInputPlugin::requestsInputFrameProcessing() {
    return true;
}

/**
 * Defines if input frames should be forwarded to other registered plugins.
 */
bool SimpleInputPlugin::allowsUsageByOtherPlugins() {
    return true;
}

/**
 * Will be called when a new camera frame is available.
 */
void SimpleInputPlugin::notifyNewImageBufferData(std::shared_ptr<unsigned char> imageBufferData_) {
    if ( _running ) {
        // This notifies the SDK that there is a new frame to process.
        notifyNewInputFrame(++_frameId, imageBufferData_);
    }
}

void SimpleInputPlugin::callInitializedJNIMethod(jmethodID methodId_) {
    if ( _jniInitialized ) {
        JavaVMResource vm(pluginJavaVM);
        vm.env->CallVoidMethod(simpleInputPluginActivity, methodId_);
    }
}

/**
 * Initialize c++->java connection.
 */
extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_SimpleInputPluginActivity_initNative(JNIEnv* env, jobject obj) {
    env->GetJavaVM(&pluginJavaVM);
    simpleInputPluginActivity = env->NewGlobalRef(obj);
}

/**
 * Sets if the default orientation of the device is landscape to ensure correct rendering on those devices.
 * If this is not set the camera frame may be rotated incorrectly.
 */
extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_SimpleInputPluginActivity_setDefaultDeviceOrientationLandscape(JNIEnv* env, jobject obj, jboolean isLandscape) {
    SimpleInputPlugin::instance->getRenderSettings().setBaseOrientationLandscape(isLandscape);
}

/**
 * Sets rotation of the camera sensor to ensure correct rendering on those devices.
 * If this is not set the camera frame may be mirrored incorrectly.
 */
extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_SimpleInputPluginActivity_setImageSensorRotation(JNIEnv* env, jobject obj, jint rotation) {
    rotatedCamera = rotation == 90;
}

/**
 * Sets the frame field of view. This is important for the SDK to correctly calculate the position of the device.
 */
extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_SimpleInputPluginActivity_setCameraFieldOfView(JNIEnv* env, jobject obj, jfloat fieldOfView) {
    SimpleInputPlugin::instance->getFrameSettings().setFrameFieldOfView(fieldOfView);
}

/**
 * Sets the size of the frame in pixel. This has to be set during initialize() of the InputPlugin.
 */
extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_SimpleInputPluginActivity_setFrameSize(JNIEnv* env, jobject obj, jint frameWidth, jint frameHeight) {
    SimpleInputPlugin::instance->getFrameSettings().setInputFrameSize({frameWidth, frameHeight});
}

/**
 * Helper function to get unsigned char array from jByteArray.
 */
unsigned char* jByteArrayToUnsignedCharArray(JNIEnv* env, jbyteArray array, int& arrayLen) {
    int len = env->GetArrayLength(array);
    arrayLen = len;
    unsigned char* buf = new unsigned char[len];
    env->GetByteArrayRegion(array, 0, len, reinterpret_cast<jbyte*>(buf));
    return buf;
}

/**
 * Helper function to reverse the camera frame
 */
void rotateYUV180Degrees(unsigned char* in, unsigned char* out, int width, int height) {

    int luminanceSize = width * height;
    std::reverse_copy(in, in + luminanceSize, out);
    int posOut = luminanceSize;
    for (int posIn = luminanceSize*3/2-1; posIn >= luminanceSize; posIn--){
        if (posIn % 2 == 0){
            out[posOut] = in[posIn];
            posOut +=2 ;
        }else{
            out[posOut+1] = in[posIn];
        }
    }
}

/**
 * Will be called when a new frame from camera2 is available.
 */
extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_SimpleInputPluginActivity_notifyNewCameraFrame(
        JNIEnv* env, jobject obj, jint widthLuminance, jint heightLuminance, jbyteArray luminance, jint pixelStrideLuminance,
        jint rowStrideLuminance, jint widthChrominance, jint heightChrominance, jbyteArray chromaBlue, jint pixelStrideBlue,
        jint rowStrideBlue, jbyteArray chromaRed, jint pixelStrideRed, jint rowStrideRed) {

    int luminanceDataSize = -1;
    unsigned char* const luminanceData = jByteArrayToUnsignedCharArray(env, luminance, luminanceDataSize);

    int chromaBlueDataSize = -1;
    unsigned char* const chromaBlueData = jByteArrayToUnsignedCharArray(env, chromaBlue, chromaBlueDataSize);

    int chromaRedDataSize = -1;
    unsigned char* const chromaRedData = jByteArrayToUnsignedCharArray(env, chromaRed, chromaRedDataSize);

    // frame data may be interlaced or padded with dummy data
    // divide by the pixel stride to get the count of usable frame data bytes
    const int correctedLuminanceDataSize = widthLuminance * heightLuminance;
    const int correctedBlueDataSize = widthChrominance * heightChrominance;
    const int correctedRedDataSize = widthChrominance * heightChrominance;

    const int frameDataSize = correctedLuminanceDataSize + correctedBlueDataSize + correctedRedDataSize;
    std::shared_ptr<unsigned char> frameData = std::shared_ptr<unsigned char>(new unsigned char[frameDataSize], [](unsigned char* data) {
        delete[] data;
    });

    // as Y, Cb, and Cr originate from different image planes they could have different
    // memory alignments (pixelStride, rowStride), therefore each channel has a dedicated
    // loop although Cb and Cr could be handled simultaneously in the majority of cases

    // Y
    unsigned char* const luminanceStart = frameData.get();

    unsigned char* YDestPtr = luminanceStart;

    unsigned char* YSrcPtr = luminanceData;
    const unsigned char* const YSrcPtrEnd = luminanceData + luminanceDataSize;

    for ( ; YSrcPtr < YSrcPtrEnd; YDestPtr += widthLuminance, YSrcPtr += rowStrideLuminance ) {
        for ( unsigned int i = 0; i < widthLuminance; ++i ) {
            YDestPtr[i] = YSrcPtr[pixelStrideLuminance * i];
        }
    }

    // We require NV21 style memory alignment, thus the Cb and Cr are interlaced such that
    // Cr_0, Cb_0, Cr_1, Cb_1, ... Cr_n, Cb_n are consecutively placed in memory

    // Cb
    unsigned char* const chromaStart = frameData.get() + correctedLuminanceDataSize;

    unsigned char* CbDestPtr = chromaStart;

    unsigned char* CbSrcPtr = chromaBlueData;
    const unsigned char* const CbSrcPtrEnd = chromaBlueData + chromaBlueDataSize;

    for ( ; CbSrcPtr < CbSrcPtrEnd; CbDestPtr += widthChrominance * 2, CbSrcPtr += rowStrideBlue ) {
        for ( unsigned int i = 0; i < widthChrominance; ++i ) {
            CbDestPtr[2 * i + 1] = CbSrcPtr[pixelStrideBlue * i];
        }
    }

    // Cr
    unsigned char* CrDestPtr = chromaStart;

    unsigned char* CrSrcPtr = chromaRedData;
    const unsigned char* const CrSrcPtrEnd = chromaRedData + chromaRedDataSize;

    for ( ; CrSrcPtr < CrSrcPtrEnd; CrDestPtr += widthChrominance * 2, CrSrcPtr += rowStrideRed ) {
        for ( unsigned int i = 0; i < widthChrominance; ++i ) {
            CrDestPtr[2 * i] = CrSrcPtr[pixelStrideRed * i];
        }
    }

    delete[] luminanceData;
    delete[] chromaBlueData;
    delete[] chromaRedData;


    if (rotatedCamera) {
        std::shared_ptr<unsigned char> frameDataReversed = std::shared_ptr<unsigned char>(new unsigned char[frameDataSize], [](unsigned char* data) {
            delete[] data;
        });
        rotateYUV180Degrees(frameData.get(), frameDataReversed.get(), widthLuminance, heightLuminance);
        SimpleInputPlugin::instance->notifyNewImageBufferData(frameDataReversed);
    } else {
        SimpleInputPlugin::instance->notifyNewImageBufferData(frameData);
    }

}

/**
 * Will be called when a new frame from the old camera api is available.
 */
extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_SimpleInputPluginActivity_notifyNewCameraFrameN21(JNIEnv* env, jobject obj, jbyteArray cameraFrame) {
    int frameSize = -1;
    unsigned char* cameraFramePtr = jByteArrayToUnsignedCharArray(env, cameraFrame, frameSize);


    if (rotatedCamera) {
        std::shared_ptr<unsigned char> frameDataReversed = std::shared_ptr<unsigned char>(new unsigned char[frameSize], [](unsigned char* data) {
            delete[] data;
        });
        int widthLuminance = SimpleInputPlugin::instance->getFrameSettings().getInputFrameSize().width;
        int heightLuminance = SimpleInputPlugin::instance->getFrameSettings().getInputFrameSize().height;

        rotateYUV180Degrees(cameraFramePtr, frameDataReversed.get(), widthLuminance, heightLuminance);
        SimpleInputPlugin::instance->notifyNewImageBufferData(frameDataReversed);
    } else {
        std::shared_ptr<unsigned char> frameData = std::shared_ptr<unsigned char>(cameraFramePtr, [](unsigned char* data) {
            delete[] data;
        });
        SimpleInputPlugin::instance->notifyNewImageBufferData(frameData);
    }
}
