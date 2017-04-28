//
//  MarkerTrackingPlugin.cpp
//  SDKExamples
//
//  Created by Daniel Guttenberg on 23/09/15.
//  Copyright (c) 2015 Wikitude. All rights reserved.
//

#include "MarkerTrackingPlugin.h"

#include "Frame.h"
#include "RecognizedTarget.h"
#include "PositionableWrapper.h"

MarkerTrackingPlugin* MarkerTrackingPlugin::instance;

extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_MarkerTrackingPluginActivity_setDefaultDeviceOrientationLandscape(JNIEnv* env, jobject obj, jboolean isLandscape)
{
    MarkerTrackingPlugin::instance->setDefaultOrientationLandscape(isLandscape);
}

extern "C" JNIEXPORT void JNICALL
Java_com_wikitude_samples_MarkerTrackingPluginActivity_initNative(JNIEnv* env, jobject obj)
{
    env->GetJavaVM(&pluginJavaVM);
}

MarkerTrackingPlugin::MarkerTrackingPlugin() :
Plugin("markertracking"),
_projectionInitialized(false),
_width(0.0f),
_height(0.0f),
_scaleWidth(0.0f),
_scaleHeight(0.0f),
_defaultOrientationLandscape(false),
_updateDone(true),
_currentInterfaceOrientation(wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeRight)
{
    MarkerTrackingPlugin::instance = this;
}

MarkerTrackingPlugin::~MarkerTrackingPlugin()
{
}

void MarkerTrackingPlugin::surfaceChanged(wikitude::sdk::Size<int> renderSurfaceSize_, wikitude::sdk::Size<float> cameraSurfaceScaling_, wikitude::sdk::InterfaceOrientation interfaceOrientation_)
{
    std::lock_guard<std::mutex> lock(_interfaceOrientationMutex);
    _currentInterfaceOrientation = interfaceOrientation_;
}

void MarkerTrackingPlugin::setDefaultOrientationLandscape(bool defaultOrientationLandscape_)
{
    _defaultOrientationLandscape = defaultOrientationLandscape_;
}

void MarkerTrackingPlugin::cameraFrameAvailable(const wikitude::sdk::Frame& cameraFrame_)
{
    wikitude::sdk::Size<int> size = cameraFrame_.getSize();

    _width = size.width;
    _height = size.height;

    // _scaleWidth and _scaleHeight may be negative on Android.
    // This is not intended and is circumvented by the std::fabs calls
    _scaleWidth = std::fabs(cameraFrame_.getScaledWidth());
    _scaleHeight = std::fabs(cameraFrame_.getScaledHeight());

    const unsigned char* const data = cameraFrame_.getData();

    const cv::Mat frameLuminance = cv::Mat(_height, _width, CV_8UC1, const_cast<unsigned char*>(data));

    cv::Mat cameraMatrix = cv::Mat::zeros(3, 3, CV_32F);

    // calculate the focal length in pixels (fx, fy)
    // assumes an iPhone5
    const float focalLengthInMillimeter = 4.12f;
    const float CCDWidthInMillimeter = 4.536f;
    const float CCDHeightInMillimeter = 3.416f;

    const float focalLengthInPixelsX = _width * focalLengthInMillimeter / CCDWidthInMillimeter;
    const float focalLengthInPixelsY = _height * focalLengthInMillimeter / CCDHeightInMillimeter;

    cameraMatrix.at<float>(0, 0) = focalLengthInPixelsX;
    cameraMatrix.at<float>(1, 1) = focalLengthInPixelsY;

    // calculate the frame center (cx, cy)
    cameraMatrix.at<float>(0, 2) = 0.5f * _width;
    cameraMatrix.at<float>(1, 2) = 0.5f * _height;

    cameraMatrix.at<float>(2, 2) = 1.0f;

    const float markerSizeInMeters = 0.1f;

    _markers.clear();
    _detector.detect(frameLuminance, _markers, cameraMatrix, cv::Mat(), markerSizeInMeters);

    double viewMatrixData[16];

    wikitude::sdk::Matrix4 identity;
    for (int i = 0; i < 16; ++i) {
        viewMatrixData[i] = identity[i];
    }

    for (auto& marker : _markers) {
        // consider only marker #303
        if (marker.id == 303) {
            marker.calculateExtrinsics(markerSizeInMeters, cameraMatrix, cv::Mat(), false);
            marker.glGetModelViewMatrix(viewMatrixData);
        }
    }

    if (!_projectionInitialized) {
        const float fieldOfViewYDegree = 50.0f;
        const float nearZ = 0.1f;
        const float farZ = 100.0f;
        _projectionMatrix.perspective(fieldOfViewYDegree, _width / _height, nearZ, farZ);
        _projectionInitialized = true;
    }

    /* critical section begin */
    _markerMutex.lock();

    if (_updateDone) {

        _markersPrev = _markersCurr;
        _markersCurr = _markers;

        for (unsigned int i = 0; i < 16; ++i) {
            _viewMatrixData[i] = static_cast<float>(viewMatrixData[i]);
        }

        _updateDone = false;
    }

    /* critical section end */
    _markerMutex.unlock();
}

void MarkerTrackingPlugin::update(const std::list<wikitude::sdk::RecognizedTarget>& recognizedTargets_)
{
}

void MarkerTrackingPlugin::updatePositionables(const std::unordered_map<std::string, wikitude::sdk_core::impl::PositionableWrapper*>& positionables_)
{
    std::unordered_map<std::string, wikitude::sdk_core::impl::PositionableWrapper*>::const_iterator it = positionables_.find("myPositionable");

    if (it == positionables_.end()) {
        return;
    }

    /* critical section start */
    _markerMutex.lock();

    if (!_updateDone) {

        _markersPrevUpdate = _markersPrev;
        _markersCurrUpdate = _markersCurr;

        for (const auto& marker : _markersCurrUpdate) {
            auto itFound = std::find_if(_markersPrevUpdate.begin(), _markersPrevUpdate.end(), [&](const aruco::Marker& other) -> bool { return other.id == marker.id; });

            if (itFound != _markersPrevUpdate.end()) {
                _markersPrevUpdate.erase(itFound);
            }
            else {
                std::cout << "recognized marker w/ id: " << marker.id << std::endl;

                it->second->enteredFieldOfVision();
            }
        }

        for (const auto& marker : _markersPrevUpdate) {
            std::cout << "lost marker w/ id: " << marker.id << std::endl;

            it->second->exitedFieldOfVision();
        }

        _updateDone = true;
    }

    /* critical section end */
    _markerMutex.unlock();

    wikitude::sdk::Matrix4 rotationToLandscapeLeft;
    rotationToLandscapeLeft.rotateZ(180.0f);

    wikitude::sdk::Matrix4 rotationToPortrait;
    rotationToPortrait.rotateZ(270.0f);

    wikitude::sdk::Matrix4 rotationToUpsideDown;
    rotationToUpsideDown.rotateZ(90.0f);

    wikitude::sdk::Matrix4 rotationToCorrectBaseOrientationLandscape;
    rotationToCorrectBaseOrientationLandscape.rotateZ(90.0f);

    wikitude::sdk::InterfaceOrientation currentInterfaceOrientation;
    {
        std::lock_guard<std::mutex> lock(_interfaceOrientationMutex);
        currentInterfaceOrientation = _currentInterfaceOrientation;
    }

    wikitude::sdk::Matrix4 aspectRatioCorrection;

    if (_defaultOrientationLandscape && (currentInterfaceOrientation == wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeRight || currentInterfaceOrientation == wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeLeft)) {
        aspectRatioCorrection.scale(_scaleHeight, _scaleWidth, 1.0f);
    } else {
        aspectRatioCorrection.scale(_scaleWidth, _scaleHeight, 1.0f);
    }

    wikitude::sdk::Matrix4 viewMatrix(_viewMatrixData);
    // OpenCV left handed coordinate system to OpenGL right handed coordinate system
    viewMatrix.scale(1.0f, -1.0f, 1.0f);

    wikitude::sdk::Matrix4 modelViewMatrix;

    if (_defaultOrientationLandscape) {
        modelViewMatrix *= rotationToCorrectBaseOrientationLandscape;
    }
    
    const float aspectRatio = _width / _height;
    
    if (currentInterfaceOrientation == wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortrait || currentInterfaceOrientation == wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortraitUpsideDown) {
        wikitude::sdk::Matrix4 portraitAndUpsideDownCorrection;
        portraitAndUpsideDownCorrection.scale(aspectRatio, 1.0f / aspectRatio, 1.0f);
        
        modelViewMatrix *= portraitAndUpsideDownCorrection;
    } else if (_defaultOrientationLandscape && (currentInterfaceOrientation == wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeRight || currentInterfaceOrientation == wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeLeft)) {
        
        wikitude::sdk::Matrix4 landscapeCorrection;
        landscapeCorrection.scale(1.0f / aspectRatio, aspectRatio, 1.0f);
        
        modelViewMatrix *= landscapeCorrection;
    }

    modelViewMatrix *= aspectRatioCorrection;

    switch (currentInterfaceOrientation) {
        case wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeRight:
            // nop
            // we don't like warnings and not having this case included would cause one
            break;
        case wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeLeft:
            modelViewMatrix *= rotationToLandscapeLeft;
            break;
        case wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortrait:
            modelViewMatrix *= rotationToPortrait;
            break;
        case wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortraitUpsideDown:
            modelViewMatrix *= rotationToUpsideDown;
            break;
    }

    modelViewMatrix *= viewMatrix;

    wikitude::sdk::Matrix4 identity;

    // due to the inner workings of the wikitude sdk, matrices need to be set
    // differently for 2d augmentations and 3d augmentations (models)
    
    // for 3d augmentations
    
    it->second->setWorldMatrix(identity.get());
    it->second->setViewMatrix(modelViewMatrix.get());
    it->second->setProjectionMatrix(_projectionMatrix.get());
    
    // for 2d augmentations
    
    //    wikitude::sdk::Matrix4 modelViewProjectionMatrix;
    //    modelViewProjectionMatrix *= _projectionMatrix;
    //    modelViewProjectionMatrix *= modelViewMatrix;
    //
    //    it->second->setWorldMatrix(modelViewProjectionMatrix.get());
    //    it->second->setViewMatrix(identity.get());
    //    it->second->setProjectionMatrix(identity.get());
}
