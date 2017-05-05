//
//  YUVFrameInputPlugin.h
//  DevApplication
//
//  Created by Andreas Schacherbauer on 10/03/16.
//  Copyright © 2016 Wikitude. All rights reserved.
//

#ifndef YUVFrameInputPlugin_h
#define YUVFrameInputPlugin_h

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "jni.h"

#include <Frame.h>
#include <InputPlugin.h>
#include <Matrix4.h>
#include <RecognizedTarget.h>
#include <InterfaceOrientation.h>

#include <YUVFrameShaderSourceObject.hpp>


extern JavaVM* pluginJavaVM;


class YUVFrameInputPlugin : public wikitude::sdk::InputPlugin {
public:
    YUVFrameInputPlugin();
    virtual ~YUVFrameInputPlugin();

    // from Plugin
    void initialize();
    void pause();
    void resume(unsigned int pausedTime_);
    void destroy();

    void startRender();
    void endRender();

    void update(const std::list<wikitude::sdk::RecognizedTarget>& recognizedTargets_);
    void cameraFrameAvailable(const wikitude::sdk::Frame& cameraFrame_);

    void surfaceChanged(wikitude::sdk::Size<int> renderSurfaceSize_, wikitude::sdk::Size<float> cameraSurfaceScaling_, wikitude::sdk::InterfaceOrientation interfaceOrientation_);

    bool requestsInputFrameRendering();
    bool requestsInputFrameProcessing();
    bool allowsUsageByOtherPlugins();

    void notifyNewImageBufferData(std::shared_ptr<unsigned char> imageBufferData_);

    void setCameraFieldOfView(double fieldOfView_);
    void setDefaultOrientationLandscape(bool defaultOrientationLandscape_);
    void setImageSensorRotation(int rotation);
    void setFrameSize(int frameWidth_, int frameHeight_);

protected:
    void callInitializedJNIMethod(jmethodID methodId_);

public:
    static YUVFrameInputPlugin* 			instance;

protected:
    long                    _frameId;

    bool                    _initialized;
    bool                    _running;

private:
    jmethodID               _pluginInitializedMethodId;
    jmethodID               _pluginPausedMethodId;
    jmethodID               _pluginResumedMethodId;
    jmethodID               _pluginDestroyedMethodId;
    jmethodID               _pluginEndRenderId;

    bool                    _jniInitialized;

    bool                    _defaultOrientationLandscape;

    int                     _imageSensorRotation;
    int                     _frameWidth;
    int                     _frameHeight;

    std::mutex _scaledWidthAndHeightMutex;
    float _scaledWidth;
    float _scaledHeight;

    bool setupRendering();
    void render();

    void createVertexBuffers();
    void releaseVertexBuffers();

    GLuint compileShader(const std::string& shaderSource, const GLenum shaderType);
    GLuint createShaderProgram(const std::string& vertexShaderSource, const std::string& fragmentShaderSource);
    void createShaderProgram(const YUVFrameShaderSourceObject& shaderObject);
    void releaseShaderProgram();

    void setVertexShaderUniforms(GLuint shaderHandle);
    void setFragmentShaderUniforms(float viewportWidth, float viewportHeight);
    void setVertexAttributes(GLuint shaderHandle);

    void createFrameTextures(GLsizei width, GLsizei height);
    void updateFrameTexturesData(GLsizei width, GLsizei height, const unsigned char* const frameData);
    void bindFrameTextures();
    void releaseFrameTextures();

    void createFrameBufferObject(GLsizei width, GLsizei height);
    void bindFrameBufferObject();
    void unbindFrameBufferObject();
    void releaseFramebufferObject();

    std::atomic_bool _renderingInitialized;
    std::atomic_bool _surfaceInitialized;
    std::atomic_bool _frameSizeInitialized;

    GLuint _scanlineShaderHandle;
    GLuint _fullscreenTextureShader;
    GLuint _augmentationShaderHandle;

    GLuint _programHandle;

    GLuint _vertexBuffer;
    GLuint _indexBuffer;

    GLuint _positionAttributeLocation;
    GLuint _texCoordAttributeLocation;

    GLuint _lumaTexture;
    GLuint _chromaTexture;

    GLint _defaultFrameBuffer;
    GLuint _frameBufferObject;
    GLuint _frameBufferTexture;

    struct Vertex
    {
        GLfloat position[3];
        GLfloat texCoord[2];
    };

    Vertex _vertices[4];

    const GLushort _indices[6];

    wikitude::sdk::Matrix4 _orientationMatrix;
    wikitude::sdk::Matrix4 _fboCorrectionMatrix;
    wikitude::sdk::Matrix4 _modelMatrix;

    std::chrono::time_point<std::chrono::system_clock> startTime;
    std::chrono::time_point<std::chrono::system_clock> currentTime;

    std::mutex _currentlyRecognizedTargetsMutex;
    std::list<wikitude::sdk::RecognizedTarget> _currentlyRecognizedTargets;
};

#endif /* YUVFrameInputPlugin_h */
