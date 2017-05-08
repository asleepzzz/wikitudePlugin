//
//  YUVFrameInputPlugin.cpp
//  DevApplication
//
//  Created by Andreas Schacherbauer on 10/03/16.
//  Copyright © 2016 Wikitude. All rights reserved.
//

#include "YUVFrameInputPlugin.h"
#include <cstring>
#include <sstream>
#include <fstream>
#include <iostream>
#include <vector>

#include <android/log.h>

#include "jniHelper.h"

ANativeWindow*    mANativeWindow;
YUVFrameInputPlugin* YUVFrameInputPlugin::instance;

jobject customCameraActivity;

#define APPNAME "CustomCameraPlugin"

#define WT_GL_ASSERT( __gl_code__ ) { \
    __gl_code__; \
    GLenum __wt_gl_error_code__ = glGetError(); \
    if ( __wt_gl_error_code__ != GL_NO_ERROR ) { \
        __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, "OpenGL error %d occurred at line %d inside function %s", __wt_gl_error_code__, __LINE__, __PRETTY_FUNCTION__);\
    } \
}
#define WT_GL_ASSERT_AND_RETURN( __assign_to__, __gl_code__ ) { \
    __assign_to__ = __gl_code__; \
    GLenum __wt_gl_error_code__ = glGetError(); \
    if ( __wt_gl_error_code__ != GL_NO_ERROR ) { \
        __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, "OpenGL error %d occurred at line %d inside function %s", __wt_gl_error_code__, __LINE__, __PRETTY_FUNCTION__);\
    } \
}

const std::string scanlineVertexShaderSource = "\
attribute vec3 vPosition;\
attribute vec2 vTexCoords;\
\
varying mediump vec2 fTexCoords;\
\
uniform mat4 uModelMatrix;\
\
void main(void)\
{\
    gl_Position = uModelMatrix * vec4(vPosition, 1.0);\
    fTexCoords = vTexCoords;\
}";

const std::string scanlineFragmentShaderSource = "\
uniform lowp sampler2D uCameraFrameTexture;\
uniform mediump vec2 uViewportResolution;\
uniform highp float uTimeInSeconds;\
uniform lowp float uScanningAreaHeightInPixels;\
\
varying mediump vec2 fTexCoords;\
\
const lowp vec3 rgb2luma = vec3(0.216, 0.7152, 0.0722);\
\
const lowp vec3 ones = vec3(1.0);\
\
const lowp vec3 sobelPositive = vec3(1.0, 2.0, 1.0);\
const lowp vec3 sobelNegative = vec3(-1.0, -2.0, -1.0);\
\
const lowp float animationSpeed = 3.0;\
\
lowp float RGB2Luminance(in lowp vec3 rgb)\
{\
    return dot(rgb2luma * rgb, ones);\
}\
\
lowp float fallbackClamp(in lowp float lowLimit, in lowp float highLimit, in lowp float value)\
{\
    return min(max(value, lowLimit), highLimit);\
}\
\
void main()\
{\
    mediump vec2 sspPixelSize = vec2(1.0) / uViewportResolution.xy;\
    \
    lowp vec4 cameraFrameColor = texture2D(uCameraFrameTexture, fTexCoords);\
    \
    lowp float tl = RGB2Luminance(texture2D(uCameraFrameTexture, fTexCoords + -sspPixelSize.xy).rgb);\
    lowp float t = RGB2Luminance(texture2D(uCameraFrameTexture, fTexCoords + vec2(0.0, -sspPixelSize.y)).rgb);\
    lowp float tr = RGB2Luminance(texture2D(uCameraFrameTexture, fTexCoords + vec2(sspPixelSize.x, -sspPixelSize.y)).rgb);\
    \
    lowp float cl = RGB2Luminance(texture2D(uCameraFrameTexture, fTexCoords + vec2(-sspPixelSize.x, 0.0)).rgb);\
    lowp float c = RGB2Luminance(cameraFrameColor.rgb);\
    lowp float cr = RGB2Luminance(texture2D(uCameraFrameTexture, fTexCoords + vec2(sspPixelSize.x, 0.0)).rgb);\
    \
    lowp float bl = RGB2Luminance(texture2D(uCameraFrameTexture, fTexCoords + vec2(-sspPixelSize.x, sspPixelSize.y)).rgb);\
    lowp float b = RGB2Luminance(texture2D(uCameraFrameTexture, fTexCoords + vec2(0.0, sspPixelSize.y)).rgb);\
    lowp float br = RGB2Luminance(texture2D(uCameraFrameTexture, fTexCoords + vec2(sspPixelSize.x, sspPixelSize.y)).rgb);\
    \
    lowp float sobelX = dot(sobelNegative * vec3(tl, cl, bl) + sobelPositive * vec3(tr, cr, br), ones);\
    lowp float sobelY = dot(sobelNegative * vec3(tl, t, tr) + sobelPositive * vec3(bl, b, br), ones);\
    \
    lowp float sobel = length(vec2(sobelX, sobelY));\
    \
    mediump float sspScanlineY = sin(uTimeInSeconds * animationSpeed) * 0.5 + 0.5;\
    mediump float sspFragmentCenterY = gl_FragCoord.y / uViewportResolution.y;\
    \
    lowp float sspScanWindowHeight = uScanningAreaHeightInPixels * sspPixelSize.y;\
    \
    mediump float distanceToScanline = fallbackClamp(0.0, sspScanWindowHeight, distance(sspScanlineY, sspFragmentCenterY)) / sspScanWindowHeight;\
    \
    gl_FragColor = vec4(mix(mix(vec3(c), vec3(1.0, 0.549, 0.0392), step(0.27, sobel)), cameraFrameColor.rgb, smoothstep(0.3, 0.7, distanceToScanline)), 1.0);\
}";

const std::string fullscreenTextureFragmentShader = "\
uniform lowp sampler2D uCameraFrameTexture;\
\
varying mediump vec2 fTexCoords;\
\
void main() {\
    gl_FragColor = vec4(texture2D(uCameraFrameTexture, fTexCoords).rgb, 1.0);\
}";

const std::string augmentationVertexShaderSource = "\
uniform mat4 uModelViewProjectionMatrix;\
\
attribute vec3 vPosition;\
attribute vec2 vTexCoords;\
\
varying mediump vec2 fTexCoords;\
\
void main() {\
    gl_Position = uModelViewProjectionMatrix * vec4(vPosition, 1.0);\
    fTexCoords = vTexCoords;\
}";

const std::string augmentationFragmentShaderSource = "\
varying mediump vec2 fTexCoords;\
\
void main()\
{\
    gl_FragColor = vec4(vec3(1.0, 0.549, 0.0392), 1.0);\
}";

unsigned char* as_unsigned_char_array(JNIEnv* env, jbyteArray array, int& arrayLen) {
    int len = env->GetArrayLength (array);
    arrayLen = len;
    unsigned char* buf = new unsigned char[len];
    env->GetByteArrayRegion (array, 0, len, reinterpret_cast<jbyte*>(buf));
    return buf;
}

extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_CustomCameraActivity_initNative(JNIEnv* env, jobject obj)
{
    env->GetJavaVM(&pluginJavaVM);
    customCameraActivity = env->NewGlobalRef(obj);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_CustomCameraActivity_notifyNewCameraFrame(JNIEnv* env, jobject obj, jint widthLuminance, jint heightLuminance, jbyteArray luminance, jint pixelStrideLuminance, jint rowStrideLuminance, jint widthChrominance, jint heightChrominance, jbyteArray chromaBlue, jint pixelStrideBlue, jint rowStrideBlue, jbyteArray chromaRed, jint pixelStrideRed, jint rowStrideRed)
{
    int luminanceDataSize = -1;
    unsigned char* const luminanceData = as_unsigned_char_array(env, luminance, luminanceDataSize);

    int chromaBlueDataSize = -1;
    unsigned char* const chromaBlueData = as_unsigned_char_array(env, chromaBlue, chromaBlueDataSize);

    int chromaRedDataSize = -1;
    unsigned char* const chromaRedData = as_unsigned_char_array(env, chromaRed, chromaRedDataSize);

    // frame data may be interlaced or padded with dummy data
    // divide by the pixel stride to get the count of useable frame data bytes
    const int correctedLuminanceDataSize = widthLuminance * heightLuminance;
    const int correctedBlueDataSize = widthChrominance * heightChrominance;
    const int correctedRedDataSize = widthChrominance * heightChrominance;

    size_t frameDataSize = correctedLuminanceDataSize + correctedBlueDataSize + correctedRedDataSize;

    __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, "frameDataSize=%u, correctedLuminanceDataSize=%d,"
            " correctedBlueDataSize=%d, correctedRedDataSize=%d",
            frameDataSize,
            correctedLuminanceDataSize,
            correctedBlueDataSize,
            correctedRedDataSize);

    std::shared_ptr<unsigned char> frameData = std::shared_ptr<unsigned char>(new unsigned char[frameDataSize], [](unsigned char* data) {
        delete [] data;
    });

    // as Y, Cb, and Cr originate from different image planes they could have different
    // memory alignments (pixelStride, rowStride), therefore each channel has a dedicated
    // loop although Cb and Cr could be handled simultaneously in the majority of cases

    // Y
    unsigned char* const luminanceStart = frameData.get();

    unsigned char* YDestPtr = luminanceStart;

    unsigned char* YSrcPtr = luminanceData;
    const unsigned char* const YSrcPtrEnd = luminanceData + luminanceDataSize;

    for (; YSrcPtr < YSrcPtrEnd; YDestPtr += widthLuminance, YSrcPtr += rowStrideLuminance) {
        for (unsigned int i = 0; i < widthLuminance; ++i) {
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

    for (; CbSrcPtr < CbSrcPtrEnd; CbDestPtr += widthChrominance * 2, CbSrcPtr += rowStrideBlue) {
        for (unsigned int i = 0; i < widthChrominance; ++i) {
            CbDestPtr[2 * i + 1] = CbSrcPtr[pixelStrideBlue * i];
        }
    }

    // Cr
    unsigned char* CrDestPtr = chromaStart;

    unsigned char* CrSrcPtr = chromaRedData;
    const unsigned char* const CrSrcPtrEnd = chromaRedData + chromaRedDataSize;

    for (; CrSrcPtr < CrSrcPtrEnd; CrDestPtr += widthChrominance * 2, CrSrcPtr += rowStrideRed) {
        for (unsigned int i = 0; i < widthChrominance; ++i) {
            CrDestPtr[2 * i] = CrSrcPtr[pixelStrideRed * i];
        }
    }

    delete [] luminanceData;
    delete [] chromaBlueData;
    delete [] chromaRedData;

    YUVFrameInputPlugin::instance->notifyNewImageBufferData(frameData);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_CustomCameraActivity_notifyNewCameraFrameN21(JNIEnv* env, jobject obj, jbyteArray cameraFrame)
{
    int frameSize = -1;
    unsigned char* cameraFramePtr = as_unsigned_char_array(env, cameraFrame, frameSize);

    std::shared_ptr<unsigned char> frameData = std::shared_ptr<unsigned char>(cameraFramePtr, [](unsigned char* data) {
        delete [] data;
    });

    YUVFrameInputPlugin::instance->notifyNewImageBufferData(frameData);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_CustomCameraActivity_setDefaultDeviceOrientationLandscape(JNIEnv* env, jobject obj, jboolean isLandscape)
{
    YUVFrameInputPlugin::instance->setDefaultOrientationLandscape(isLandscape);
    YUVFrameInputPlugin::instance->getRenderSettings().setBaseOrientationLandscape(isLandscape);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_CustomCameraActivity_setImageSensorRotation(JNIEnv* env, jobject obj, jint rotation)
{
    YUVFrameInputPlugin::instance->setImageSensorRotation(rotation);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_CustomCameraActivity_setCameraFieldOfView(JNIEnv* env, jobject obj, jdouble fieldOfView)
{

    YUVFrameInputPlugin::instance->setCameraFieldOfView(fieldOfView);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_CustomCameraActivity_setFrameSize(JNIEnv* env, jobject obj, jint frameWidth, jint frameHeight)
{
    YUVFrameInputPlugin::instance->setFrameSize(frameWidth, frameHeight);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_CustomCameraActivity_setNativeSurface(JNIEnv* env,jobject thiz, jobject surface)
{
    __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, "setNativeSurface");
    JavaVMResource vm(pluginJavaVM);
    if (mANativeWindow == NULL) {
        mANativeWindow = ANativeWindow_fromSurface(env, surface);
    }

    if (mANativeWindow == NULL) {
        __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, "ANativeWindow_fromSurface error");
    }
}

YUVFrameInputPlugin::YUVFrameInputPlugin()
:
InputPlugin("plugin.input.yuv"),
_frameId(0),
_jniInitialized(false),
_defaultOrientationLandscape(false),
_imageSensorRotation(0),
_initialized(false),
_scaledWidth(1.0f),
_scaledHeight(1.0f),
_renderingInitialized(false),
_surfaceInitialized(false),
_frameSizeInitialized(false),
_scanlineShaderHandle(0),
_fullscreenTextureShader(0),
_augmentationShaderHandle(0),
_programHandle(0),
_vertexBuffer(0),
_indexBuffer(0),
_positionAttributeLocation(0),
_texCoordAttributeLocation(0),
_lumaTexture(0),
_chromaTexture(0),
_frameBufferObject(0),
_frameBufferTexture(0),
_defaultFrameBuffer(0),
_frameWidth(-1),
_frameHeight(-1),
_indices{0, 1, 2, 2, 3, 0}
{
    YUVFrameInputPlugin::instance = this;

    startTime = std::chrono::system_clock::now();

    _fboCorrectionMatrix.scale(1.0f, -1.0f, 1.0f);
}

YUVFrameInputPlugin::~YUVFrameInputPlugin()
{
    ANativeWindow_release(mANativeWindow);
    releaseFramebufferObject();
    releaseShaderProgram();
    releaseVertexBuffers();
    releaseFrameTextures();

    JavaVMResource vm(pluginJavaVM);
    vm.env->DeleteGlobalRef(customCameraActivity);
}

void YUVFrameInputPlugin::initialize() {

    callInitializedJNIMethod(_pluginInitializedMethodId);

    _initialized = true;
}

void YUVFrameInputPlugin::pause() {

    releaseFramebufferObject();
    releaseFrameTextures();
    releaseVertexBuffers();
    releaseShaderProgram();

    _renderingInitialized.store(false);

    _running = false;

    callInitializedJNIMethod(_pluginPausedMethodId);
}

void YUVFrameInputPlugin::resume(unsigned int pausedTime_) {

    _running = true;

    callInitializedJNIMethod(_pluginResumedMethodId);
}

void YUVFrameInputPlugin::destroy() {
    _initialized = false;

    callInitializedJNIMethod(_pluginDestroyedMethodId);
}

void YUVFrameInputPlugin::startRender() {


}

jbyteArray as_byte_array(unsigned char* buf, int len) {
    JavaVMResource vm(pluginJavaVM);
    jbyteArray array = vm.env->NewByteArray (len);
    vm.env->SetByteArrayRegion (array, 0, len, reinterpret_cast<jbyte*>(buf));
    return array;
}

void YUVFrameInputPlugin::endRender() {

    if (!_frameSizeInitialized.load()) {
        return;
    }

    if (!_renderingInitialized.load()) {
        _renderingInitialized.store(setupRendering());
    }

    // render();

    { // shared_ptr auto release scope
        std::shared_ptr<unsigned char> presentableFrameData = getPresentableInputFrameData();
        if (presentableFrameData) {
            updateFrameTexturesData(_frameWidth, _frameHeight, presentableFrameData.get());
        }
    }

    WT_GL_ASSERT(glDisable(GL_DEPTH_TEST));

    WT_GL_ASSERT(glUseProgram(_programHandle));

    WT_GL_ASSERT(glBindBuffer(GL_ARRAY_BUFFER, _vertexBuffer));
    WT_GL_ASSERT(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBuffer));

    WT_GL_ASSERT(glEnableVertexAttribArray(_positionAttributeLocation));
    WT_GL_ASSERT(glEnableVertexAttribArray(_texCoordAttributeLocation));

    WT_GL_ASSERT(glVertexAttribPointer(_positionAttributeLocation, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0));
    WT_GL_ASSERT(glVertexAttribPointer(_texCoordAttributeLocation, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)(sizeof(float) * 3)));

    bindFrameTextures();

    bindFrameBufferObject();

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    const float viewportWidth = viewport[2] - viewport[0];
    const float viewportHeight = viewport[3] - viewport[1];

    WT_GL_ASSERT(glViewport(0, 0, _frameWidth, _frameHeight));
    WT_GL_ASSERT(glClear(GL_COLOR_BUFFER_BIT));
    WT_GL_ASSERT(glDrawElements(GL_TRIANGLES, sizeof(_indices)/sizeof(_indices[0]), GL_UNSIGNED_SHORT, 0));
    unbindFrameBufferObject();

    WT_GL_ASSERT(glViewport(viewport[0], viewport[1], viewport[2], viewport[3]));

    WT_GL_ASSERT(glClear(GL_COLOR_BUFFER_BIT));

    WT_GL_ASSERT(glActiveTexture(GL_TEXTURE0));
    WT_GL_ASSERT(glBindTexture(GL_TEXTURE_2D, _frameBufferTexture));

    { // mutex auto release scope
    std::unique_lock<std::mutex> lock(_currentlyRecognizedTargetsMutex);

        if (!_currentlyRecognizedTargets.empty()) {

            // just use one of the targets found, should be sufficient to get the point across
            const wikitude::sdk::RecognizedTarget targetToDraw = _currentlyRecognizedTargets.front();

            // early unlock to minimize locking duration
            lock.unlock();

            WT_GL_ASSERT(glUseProgram(_fullscreenTextureShader));
            setVertexShaderUniforms(_fullscreenTextureShader);

            GLint fullscreenTextureUniformLocation;
            WT_GL_ASSERT_AND_RETURN(fullscreenTextureUniformLocation, glGetUniformLocation(_fullscreenTextureShader, "uCameraFrameTexture"));
            WT_GL_ASSERT(glUniform1i(fullscreenTextureUniformLocation, 0));

            setVertexAttributes(_fullscreenTextureShader);

            WT_GL_ASSERT(glDrawElements(GL_TRIANGLES, sizeof(_indices)/sizeof(_indices[0]), GL_UNSIGNED_SHORT, 0));

            const float TARGET_HEIGHT_IN_PIXELS = 320.0f;

            // TODO: can be computed in the ctor
            wikitude::sdk::Matrix4 quadReuseMatrix;
            // scale that transforms the fullscreen quad (side length = 2) into a screen
            // aligned quad with side length = 1, there avoiding additional vertex data to be
            // required
            // sadly no attribute-less rendering available on OpenGL ES 2 due to the lack of
            // gl_VertexId
            quadReuseMatrix.scale(0.5f, 0.5f, 1.0f);

            wikitude::sdk::Matrix4 targetHeightScaling;
            targetHeightScaling.scale(TARGET_HEIGHT_IN_PIXELS, TARGET_HEIGHT_IN_PIXELS, 1.0f);

            wikitude::sdk::Matrix4 modelView = targetToDraw.getModelViewMatrix();
            wikitude::sdk::Matrix4 projection = targetToDraw.getProjectionMatrix();

            wikitude::sdk::Matrix4 sensorRotationCompensation;

            float sensorRotationCompensationAngle = static_cast<float>(_imageSensorRotation);
            if (!_defaultOrientationLandscape) {
                sensorRotationCompensationAngle += 90.0f;
            }

            sensorRotationCompensation.rotateZ(sensorRotationCompensationAngle);

            wikitude::sdk::Matrix4 modelViewProjection = sensorRotationCompensation * projection * modelView * targetHeightScaling * quadReuseMatrix;

            WT_GL_ASSERT(glUseProgram(_augmentationShaderHandle));
            GLint modelViewProjectionLocation;
            WT_GL_ASSERT_AND_RETURN(modelViewProjectionLocation, glGetUniformLocation(_augmentationShaderHandle, "uModelViewProjectionMatrix"));
            WT_GL_ASSERT(glUniformMatrix4fv(modelViewProjectionLocation, 1, GL_FALSE, modelViewProjection.get()));

            setVertexAttributes(_augmentationShaderHandle);

            WT_GL_ASSERT(glDrawElements(GL_TRIANGLES, sizeof(_indices)/sizeof(_indices[0]), GL_UNSIGNED_SHORT, 0));
        } else {
            WT_GL_ASSERT(glUseProgram(_scanlineShaderHandle));
            setVertexShaderUniforms(_scanlineShaderHandle);

            setFragmentShaderUniforms(viewportWidth, viewportHeight);
            setVertexAttributes(_scanlineShaderHandle);

            WT_GL_ASSERT(glDrawElements(GL_TRIANGLES, sizeof(_indices)/sizeof(_indices[0]), GL_UNSIGNED_SHORT, 0));
        }
    }

    int size = 1080*1920*4;
    GLubyte *data = (GLubyte *)malloc(size);
    WT_GL_ASSERT(glReadPixels(0, 0 , 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, data));

    WT_GL_ASSERT(glBindBuffer(GL_ARRAY_BUFFER, 0));
    WT_GL_ASSERT(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    WT_GL_ASSERT(glUseProgram(0));

    WT_GL_ASSERT(glEnable(GL_DEPTH_TEST));

    if (data != nullptr) {
        JavaVMResource vm(pluginJavaVM);
        //unsigned char *arr = new unsigned char[10];

        jbyteArray array = vm.env->NewByteArray(size);
        vm.env->SetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte*>(data));

        vm.env->CallVoidMethod(customCameraActivity, _pluginEndRenderId, array);
        //vm.env->CallVoidMethod(customCameraActivity, _pluginEndRenderId, as_byte_array(frame.get(),
        //            setting.getInputFrameSize().width * setting.getInputFrameSize().height * 3));
    }

    /*
    std::shared_ptr<unsigned char> frame = getPresentableInputFrameData();
    wikitude::sdk::InputFrameSettings &setting = getFrameSettings();

    if (frame != nullptr) {
        JavaVMResource vm(pluginJavaVM);
        //unsigned char *arr = new unsigned char[10];
        int size = 460800;

        jbyteArray array = vm.env->NewByteArray(size);
        vm.env->SetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte*>(frame.get()));

        vm.env->CallVoidMethod(customCameraActivity, _pluginEndRenderId, array);
        //vm.env->CallVoidMethod(customCameraActivity, _pluginEndRenderId, as_byte_array(frame.get(),
        //            setting.getInputFrameSize().width * setting.getInputFrameSize().height * 3));
    }
    */
}

void YUVFrameInputPlugin::update(const std::list<wikitude::sdk::RecognizedTarget>& recognizedTargets_) {
    if ( !_jniInitialized ) {
        JavaVMResource vm(pluginJavaVM);
        jclass customCameraActivityClass = vm.env->FindClass("com/wikitude/samples/CustomCameraActivity");

        _pluginInitializedMethodId = vm.env->GetMethodID(customCameraActivityClass, "onInputPluginInitialized", "()V");
        _pluginPausedMethodId = vm.env->GetMethodID(customCameraActivityClass, "onInputPluginPaused", "()V");
        _pluginResumedMethodId = vm.env->GetMethodID(customCameraActivityClass, "onInputPluginResumed", "()V");
        _pluginDestroyedMethodId = vm.env->GetMethodID(customCameraActivityClass, "onInputPluginDestroyed", "()V");
        _pluginEndRenderId =  vm.env->GetMethodID(customCameraActivityClass, "onRenderEnd", "([B)V");
        _jniInitialized = true;


        callInitializedJNIMethod(_pluginInitializedMethodId);
        callInitializedJNIMethod(_pluginResumedMethodId);
    }

    { // mutex auto release scope
        std::lock_guard<std::mutex> lock(_currentlyRecognizedTargetsMutex);
        _currentlyRecognizedTargets = std::list<wikitude::sdk::RecognizedTarget>(recognizedTargets_);
    }
}

void YUVFrameInputPlugin::cameraFrameAvailable(const wikitude::sdk::Frame& cameraFrame_) {
}

void YUVFrameInputPlugin::surfaceChanged(wikitude::sdk::Size<int> renderSurfaceSize_, wikitude::sdk::Size<float> cameraSurfaceScaling_, wikitude::sdk::InterfaceOrientation interfaceOrientation_) {
    wikitude::sdk::Matrix4 scaleMatrix;
    scaleMatrix.scale(cameraSurfaceScaling_.width, cameraSurfaceScaling_.height, 1.0f);

    float sensorRotation = static_cast<float>(_imageSensorRotation);

    switch (interfaceOrientation_)
    {
        case wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortrait:
        {
            wikitude::sdk::Matrix4 rotationToPortrait;
            rotationToPortrait.rotateZ(sensorRotation + 0.0f);

            _orientationMatrix = rotationToPortrait;
            break;
        }
        case wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortraitUpsideDown:
        {
            wikitude::sdk::Matrix4 rotationToUpsideDown;
            rotationToUpsideDown.rotateZ(sensorRotation + 180.0f);

            _orientationMatrix = rotationToUpsideDown;
            break;
        }
        case wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeLeft:
        {
            wikitude::sdk::Matrix4 rotationToLandscapeLeft;
            rotationToLandscapeLeft.rotateZ(sensorRotation + 270.0f);

            _orientationMatrix = rotationToLandscapeLeft;
            break;
        }
        case wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeRight:
        {
            wikitude::sdk::Matrix4 rotationToLandscapeLeft;
            rotationToLandscapeLeft.rotateZ(sensorRotation + 90.0f);

            _orientationMatrix = rotationToLandscapeLeft;
            break;
        }
    }

    _modelMatrix = scaleMatrix * _orientationMatrix * _fboCorrectionMatrix;

    _surfaceInitialized.store(true);
}

bool YUVFrameInputPlugin::requestsInputFrameRendering() {
    return false;
}

bool YUVFrameInputPlugin::requestsInputFrameProcessing() {
    return true;
}

bool YUVFrameInputPlugin::allowsUsageByOtherPlugins() {
    return true;
}

void YUVFrameInputPlugin::notifyNewImageBufferData(std::shared_ptr<unsigned char> imageBufferData_) {
    if ( _running ) {
        notifyNewInputFrame(++_frameId, imageBufferData_);
    }
}

void YUVFrameInputPlugin::callInitializedJNIMethod(jmethodID methodId_) {
    if ( _jniInitialized ) {
        JavaVMResource vm(pluginJavaVM);
        vm.env->CallVoidMethod(customCameraActivity, methodId_);
    }
}

void YUVFrameInputPlugin::setCameraFieldOfView(double fieldOfView_) {
    getFrameSettings().setFrameFieldOfView(fieldOfView_);
}

void YUVFrameInputPlugin::setDefaultOrientationLandscape(bool defaultOrientationLandscape_){
    _defaultOrientationLandscape = defaultOrientationLandscape_;
}

void YUVFrameInputPlugin::setImageSensorRotation(int imageSensorRotation_) {
    _imageSensorRotation = imageSensorRotation_;
}

void YUVFrameInputPlugin::setFrameSize(int frameWidth_, int frameHeight_) {

    _frameWidth = frameWidth_;
    _frameHeight = frameHeight_;

    getFrameSettings().setInputFrameSize({frameWidth_, frameHeight_});

    _frameSizeInitialized.store(true);
}

bool YUVFrameInputPlugin::setupRendering() {
    _vertices[0] = (Vertex){{1.0f, -1.0f, 0}, {1.0f, 0.0f}};
    _vertices[1] = (Vertex){{1.0f, 1.0f, 0}, {1.0f, 1.0f}};
    _vertices[2] = (Vertex){{-1.0f, 1.0f, 0}, {0.0f, 1.0f}};
    _vertices[3] = (Vertex){{-1.0f, -1.0f, 0}, {0.0f, 0.0f}};

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &_defaultFrameBuffer);

    _scanlineShaderHandle = createShaderProgram(scanlineVertexShaderSource, scanlineFragmentShaderSource);
    createShaderProgram(YUVFrameShaderSourceObject());
    _fullscreenTextureShader = createShaderProgram(scanlineVertexShaderSource, fullscreenTextureFragmentShader);
    _augmentationShaderHandle = createShaderProgram(augmentationVertexShaderSource, augmentationFragmentShaderSource);
    createVertexBuffers();
    createFrameTextures(_frameWidth, _frameHeight);
    createFrameBufferObject(_frameWidth, _frameHeight);

    return true;
}

void YUVFrameInputPlugin::render() {
    if (!_renderingInitialized.load()) {
        __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, "Rendering not initialized. Skipping render() function.");
        return;
    }

    if (!_surfaceInitialized.load()) {
        __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, "Surface not initialized. Skipping render() function.");
        return;
    }

}

void YUVFrameInputPlugin::createVertexBuffers()
{
    WT_GL_ASSERT(glGenBuffers(1, &_vertexBuffer));
    WT_GL_ASSERT(glBindBuffer(GL_ARRAY_BUFFER, _vertexBuffer));
    WT_GL_ASSERT(glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(Vertex), &_vertices, GL_STATIC_DRAW));

    WT_GL_ASSERT(glGenBuffers(1, &_indexBuffer));
    WT_GL_ASSERT(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBuffer));
    WT_GL_ASSERT(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(_indices), _indices, GL_STATIC_DRAW));
}

void YUVFrameInputPlugin::releaseVertexBuffers()
{
    if (_vertexBuffer) {
        WT_GL_ASSERT(glDeleteBuffers(1, &_vertexBuffer));
        _vertexBuffer = 0;
    }

    if (_indexBuffer) {
        WT_GL_ASSERT(glDeleteBuffers(1, &_indexBuffer));
        _vertexBuffer = 0;
    }
}

GLuint YUVFrameInputPlugin::compileShader(const std::string& shaderSource, const GLenum shaderType)
{
    GLuint shaderHandle;
    WT_GL_ASSERT_AND_RETURN(shaderHandle, glCreateShader(shaderType));

    const char* shaderString = shaderSource.c_str();
    GLint shaderStringLength = static_cast<GLint>(shaderSource.length());

    WT_GL_ASSERT(glShaderSource(shaderHandle, 1, &shaderString, &shaderStringLength));
    WT_GL_ASSERT(glCompileShader(shaderHandle));

    GLint compileSuccess;
    WT_GL_ASSERT(glGetShaderiv(shaderHandle, GL_COMPILE_STATUS, &compileSuccess));
    if (GL_FALSE == compileSuccess)
    {
        GLchar messages[256];
        WT_GL_ASSERT(glGetShaderInfoLog(shaderHandle, sizeof(messages), 0, &messages[0]));
        __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, "Error compiling shader: %s", messages);
    }

    return shaderHandle;
}

GLuint YUVFrameInputPlugin::createShaderProgram(const std::string& vertexShaderSource, const std::string& fragmentShaderSource)
{
    GLuint vertexShaderHandle = compileShader(vertexShaderSource, GL_VERTEX_SHADER);
    GLuint fragmentShaderHandle = compileShader(fragmentShaderSource, GL_FRAGMENT_SHADER);

    GLuint programHandle;
    WT_GL_ASSERT_AND_RETURN(programHandle, glCreateProgram());

    WT_GL_ASSERT(glAttachShader(programHandle, vertexShaderHandle));
    WT_GL_ASSERT(glAttachShader(programHandle, fragmentShaderHandle));
    WT_GL_ASSERT(glLinkProgram(programHandle));

    GLint linkSuccess;
    WT_GL_ASSERT(glGetProgramiv(programHandle, GL_LINK_STATUS, &linkSuccess));
    if (linkSuccess == GL_FALSE)
    {
        GLchar messages[256];
        WT_GL_ASSERT(glGetProgramInfoLog(programHandle, sizeof(messages), 0, &messages[0]));
        __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, "Error linking program: %s", messages);
    }

    WT_GL_ASSERT(glDeleteShader(vertexShaderHandle));
    WT_GL_ASSERT(glDeleteShader(fragmentShaderHandle));

    return programHandle;
}

void YUVFrameInputPlugin::createShaderProgram(const YUVFrameShaderSourceObject& shaderObject)
{
    _programHandle = createShaderProgram(shaderObject.getVertexShaderSource(), shaderObject.getFragmentShaderSource());

    WT_GL_ASSERT(glUseProgram(_programHandle));

    WT_GL_ASSERT_AND_RETURN(_positionAttributeLocation, glGetAttribLocation(_programHandle, shaderObject.getVertexPositionAttributeName().c_str()));
    WT_GL_ASSERT_AND_RETURN(_texCoordAttributeLocation, glGetAttribLocation(_programHandle, shaderObject.getTextureCoordinateAttributeName().c_str()));

    std::vector<std::string> uniformNames = shaderObject.getTextureUniformNames();
    std::size_t uniformNamesSize = uniformNames.size();
    for (int i = 0; i < uniformNamesSize; ++i)
    {
        WT_GL_ASSERT(glUniform1i(glGetUniformLocation(_programHandle, uniformNames[i].c_str()), i));
    }
}

void YUVFrameInputPlugin::releaseShaderProgram()
{
    if (_scanlineShaderHandle) {
        WT_GL_ASSERT(glDeleteProgram(_scanlineShaderHandle));
        _scanlineShaderHandle = 0;
    }

    if (_fullscreenTextureShader) {
        WT_GL_ASSERT(glDeleteProgram(_fullscreenTextureShader));
        _fullscreenTextureShader = 0;
    }

    if (_augmentationShaderHandle) {
        WT_GL_ASSERT(glDeleteProgram(_augmentationShaderHandle));
        _augmentationShaderHandle = 0;
    }

    if (_programHandle) {
        WT_GL_ASSERT(glDeleteProgram(_programHandle));
        _programHandle = 0;
    }
}

void YUVFrameInputPlugin::setVertexShaderUniforms(GLuint shaderHandle)
{
    GLint deviceOrientationLocation;
    WT_GL_ASSERT_AND_RETURN(deviceOrientationLocation, glGetUniformLocation(_scanlineShaderHandle, "uModelMatrix"));
    WT_GL_ASSERT(glUniformMatrix4fv(deviceOrientationLocation, 1, GL_FALSE, _modelMatrix.get()));
}

void YUVFrameInputPlugin::setFragmentShaderUniforms(float viewportWidth, float viewportHeight)
{
    GLint scanlineUniformLocation;
    WT_GL_ASSERT_AND_RETURN(scanlineUniformLocation, glGetUniformLocation(_scanlineShaderHandle, "uCameraFrameTexture"));
    WT_GL_ASSERT(glUniform1i(scanlineUniformLocation, 0));

    GLint scanlineUniformResolutionLocation;
    WT_GL_ASSERT_AND_RETURN(scanlineUniformResolutionLocation, glGetUniformLocation(_scanlineShaderHandle, "uViewportResolution"));
    WT_GL_ASSERT(glUniform2f(scanlineUniformResolutionLocation, viewportWidth, viewportHeight));

    currentTime = std::chrono::system_clock::now();
    std::chrono::duration<float> durationInSeconds = currentTime - startTime;
    GLint scanlineUniformTimeLocation;
    WT_GL_ASSERT_AND_RETURN(scanlineUniformTimeLocation, glGetUniformLocation(_scanlineShaderHandle, "uTimeInSeconds"));
    WT_GL_ASSERT(glUniform1f(scanlineUniformTimeLocation, durationInSeconds.count()));

    const float scanningAreaHeight = viewportHeight * 0.25;

    GLint scanlineUniformAreaHeight;
    WT_GL_ASSERT_AND_RETURN(scanlineUniformAreaHeight, glGetUniformLocation(_scanlineShaderHandle, "uScanningAreaHeightInPixels"));
    WT_GL_ASSERT(glUniform1f(scanlineUniformAreaHeight, scanningAreaHeight));
}

void YUVFrameInputPlugin::setVertexAttributes(GLuint shaderHandle)
{
    GLint vertexPositionAttributeLocation;
    WT_GL_ASSERT_AND_RETURN(vertexPositionAttributeLocation, glGetAttribLocation(_scanlineShaderHandle, "vPosition"));
    WT_GL_ASSERT(glEnableVertexAttribArray(vertexPositionAttributeLocation));
    WT_GL_ASSERT(glVertexAttribPointer(vertexPositionAttributeLocation, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0));

    GLint vertexTexCoordAttributeLocation;
    WT_GL_ASSERT_AND_RETURN(vertexTexCoordAttributeLocation, glGetAttribLocation(_scanlineShaderHandle, "vTexCoords"));
    WT_GL_ASSERT(glEnableVertexAttribArray(vertexTexCoordAttributeLocation));
    WT_GL_ASSERT(glVertexAttribPointer(vertexTexCoordAttributeLocation, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid *)(sizeof(float) * 3)));
}

void YUVFrameInputPlugin::createFrameTextures(GLsizei width, GLsizei height)
{
    WT_GL_ASSERT(glGenTextures(1, &_lumaTexture));
    WT_GL_ASSERT(glGenTextures(1, &_chromaTexture));

    WT_GL_ASSERT(glActiveTexture(GL_TEXTURE0));
    WT_GL_ASSERT(glBindTexture(GL_TEXTURE_2D, _lumaTexture));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    WT_GL_ASSERT(glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, nullptr));

    WT_GL_ASSERT(glBindTexture(GL_TEXTURE_2D, _chromaTexture));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    WT_GL_ASSERT(glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width / 2, height / 2, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, nullptr));
}

void YUVFrameInputPlugin::updateFrameTexturesData(GLsizei width, GLsizei height, const unsigned char* const frameData)
{
    WT_GL_ASSERT(glActiveTexture(GL_TEXTURE0));

    WT_GL_ASSERT(glBindTexture(GL_TEXTURE_2D, _lumaTexture));
    WT_GL_ASSERT(glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, frameData));

    WT_GL_ASSERT(glBindTexture(GL_TEXTURE_2D, _chromaTexture));
    WT_GL_ASSERT(glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width / 2, height / 2, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, frameData + width * height));
}

void YUVFrameInputPlugin::bindFrameTextures()
{
    WT_GL_ASSERT(glActiveTexture(GL_TEXTURE0));
    WT_GL_ASSERT(glBindTexture(GL_TEXTURE_2D, _lumaTexture));

    WT_GL_ASSERT(glActiveTexture(GL_TEXTURE1));
    WT_GL_ASSERT(glBindTexture(GL_TEXTURE_2D, _chromaTexture));
}

void YUVFrameInputPlugin::releaseFrameTextures()
{
    if (_lumaTexture) {
        WT_GL_ASSERT(glDeleteTextures(1, &_lumaTexture));
        _lumaTexture = 0;
    }

    if (_chromaTexture) {
        WT_GL_ASSERT(glDeleteTextures(1, &_chromaTexture));
        _chromaTexture = 0;
    }
}

void YUVFrameInputPlugin::createFrameBufferObject(GLsizei width, GLsizei height)
{
    WT_GL_ASSERT(glGenFramebuffers(1, &_frameBufferObject));
    WT_GL_ASSERT(glBindFramebuffer(GL_FRAMEBUFFER, _frameBufferObject));

    WT_GL_ASSERT(glGenTextures(1, &_frameBufferTexture));

    WT_GL_ASSERT(glActiveTexture(GL_TEXTURE0));
    WT_GL_ASSERT(glBindTexture(GL_TEXTURE_2D, _frameBufferTexture));

    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    WT_GL_ASSERT(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    WT_GL_ASSERT(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, _frameWidth, _frameHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr));

    WT_GL_ASSERT(glBindTexture(GL_TEXTURE_2D, 0));

    WT_GL_ASSERT(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _frameBufferTexture, 0));

    GLenum framebufferStatus;
    WT_GL_ASSERT_AND_RETURN(framebufferStatus, glCheckFramebufferStatus(GL_FRAMEBUFFER));
    if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
        __android_log_print(ANDROID_LOG_VERBOSE, APPNAME, "Framebuffer incomplete!");
    }
}

void YUVFrameInputPlugin::bindFrameBufferObject()
{
    WT_GL_ASSERT(glBindFramebuffer(GL_FRAMEBUFFER, _frameBufferObject));
}

void YUVFrameInputPlugin::unbindFrameBufferObject()
{
    WT_GL_ASSERT(glBindFramebuffer(GL_FRAMEBUFFER, _defaultFrameBuffer));
}

void YUVFrameInputPlugin::releaseFramebufferObject()
{
    if (_frameBufferTexture) {
        WT_GL_ASSERT(glDeleteTextures(1, &_frameBufferTexture));
        _frameBufferTexture = 0;
    }

    if (_frameBufferObject) {
        WT_GL_ASSERT(glDeleteTextures(1, &_frameBufferObject));
        _frameBufferObject = 0;
    }
}
