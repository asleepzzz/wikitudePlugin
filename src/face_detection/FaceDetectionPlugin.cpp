//
//  FaceDetectionPlugin.cpp
//  Native Examples
//
//  Created by Alami Yacine on 29/07/15.
//  Copyright (c) 2015 Wikitude. All rights reserved.
//

#include "FaceDetectionPlugin.h"
#include "FaceDetectionPluginConnector.h"

#include <vector>
#include <iostream>
#include <sstream>
#include "jniHelper.h"

FaceDetectionPlugin* FaceDetectionPlugin::instance;
std::string FaceDetectionPlugin::_databasePath;

FaceDetectionPlugin::FaceDetectionPlugin(int cameraFrameWidth_, int cameraFrameHeight_, FaceDetectionPluginObserver* observer_ ) :
Plugin("com.wikitude.android.FaceDetectionPlugin"),
_isDatabaseLoaded(false),
_isBaseOrientationLandscape(false),
_grayFrame(cameraFrameHeight_, cameraFrameWidth_, CV_8UC1),
_cameraFrameWidth(cameraFrameWidth_),
_cameraFrameHeight(cameraFrameHeight_),
_scaledCameraFrameWidth(0.f),
_scaledCameraFrameHeight(0.f),
_aspectRatio((float)cameraFrameHeight_ / (float)cameraFrameWidth_),
_interfaceOrientation(wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortrait),
_observer(observer_)
{
    setEnabled(false);
    FaceDetectionPlugin::instance = this;
}

FaceDetectionPlugin::~FaceDetectionPlugin() {
	delete _observer;
	FaceDetectionPlugin::instance = nullptr;
}

void FaceDetectionPlugin::initialize() {
    /* Intentionally Left Blank */
}

void FaceDetectionPlugin::destroy() {
    /* Intentionally Left Blank */
}

void FaceDetectionPlugin::cameraFrameAvailable(const wikitude::sdk::Frame& cameraFrame_) {
	if (!_isDatabaseLoaded) {
		_isDatabaseLoaded =	_cascadeDetector.load(_databasePath);
		if(!_isDatabaseLoaded) {
			return;
		}
	}
	_scaledCameraFrameWidth = cameraFrame_.getScaledWidth();
	_scaledCameraFrameHeight = cameraFrame_.getScaledHeight();

	wikitude::sdk::Size<int> frameSize = cameraFrame_.getSize();

	std::memcpy(_grayFrame.data,cameraFrame_.getData(), frameSize.height*frameSize.width*sizeof(unsigned char));

	cv::Mat smallImg = cv::Mat(frameSize.height * 0.5f, frameSize.width * 0.5f, CV_8UC1);


	cv::resize(_grayFrame, smallImg, smallImg.size(), CV_INTER_AREA);

    /* Depending on the device orientation, the camera frame needs to be rotated in order to detect faces in it */

    wikitude::sdk::InterfaceOrientation currentInterfaceOrientation;
    { // auto release scope
        std::unique_lock<std::mutex>(_interfaceOrientationMutex);

        currentInterfaceOrientation = _interfaceOrientation;
    }

    if (adjustInterfaceOrientation(currentInterfaceOrientation, _isBaseOrientationLandscape) == wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortrait) {
        cv::transpose(smallImg, smallImg);
        cv::flip(smallImg, smallImg, 1);
    }
    else if (adjustInterfaceOrientation(currentInterfaceOrientation, _isBaseOrientationLandscape) == wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortraitUpsideDown) {
        cv::transpose(smallImg, smallImg);
        cv::flip(smallImg, smallImg, 0);
    }
    else if (adjustInterfaceOrientation(currentInterfaceOrientation, _isBaseOrientationLandscape) == wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeLeft) {
        cv::flip(smallImg, smallImg, -1);
    }
    else if (adjustInterfaceOrientation(currentInterfaceOrientation, _isBaseOrientationLandscape) == wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeRight) {
        // nop for landscape right
    }

	cv::Rect crop = cv::Rect(smallImg.cols / 4, smallImg.rows / 4, smallImg.cols / 2,smallImg.rows / 2);

	cv::Mat croppedImg = smallImg(crop);


	_result.clear();
	_cascadeDetector.detectMultiScale(croppedImg, _result, 1.1, 2, 0, cv::Size(20, 20));

    if ( _result.size() ) {
        convertFaceRectToModelViewMatrix(croppedImg, _result.at(0), currentInterfaceOrientation);
        _observer->faceDetected(_modelViewMatrix);
    } else {
        _observer->faceLost();
    }
}

void FaceDetectionPlugin::update(const std::list<wikitude::sdk::RecognizedTarget> &recognizedTargets_) {
}

void FaceDetectionPlugin::surfaceChanged(wikitude::sdk::Size<int> renderSurfaceSize_, wikitude::sdk::Size<float> cameraSurfaceScaling_, wikitude::sdk::InterfaceOrientation interfaceOrientation_) {

    { // auto release scope
        std::unique_lock<std::mutex>(_interfaceOrientationMutex);

        _interfaceOrientation = interfaceOrientation_;
    }

    calculateProjection(interfaceOrientation_, -1.f, 1.f, -1.f, 1.f, 0.f, 500.f);
    _observer->projectionMatrixChanged(_projectionMatrix);
}

void FaceDetectionPlugin::startRender() {
    _observer->renderDetectedFaceAugmentation();
}

wikitude::sdk::InterfaceOrientation FaceDetectionPlugin::adjustInterfaceOrientation(wikitude::sdk::InterfaceOrientation interfaceOrientation_, bool isBaseOrientationLandscape) {
    if (!isBaseOrientationLandscape) {
        return interfaceOrientation_;
    }
    else {
        switch (interfaceOrientation_) {
            case wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortrait:
                return wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeRight;
            case wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortraitUpsideDown:
                return wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeLeft;
            case wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeRight:
                return wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortraitUpsideDown;
            case wikitude::sdk::InterfaceOrientation::InterfaceOrientationLandscapeLeft:
                return wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortrait;
        }
    }
}

void FaceDetectionPlugin::convertFaceRectToModelViewMatrix(cv::Mat& frame_, cv::Rect& faceRect_, wikitude::sdk::InterfaceOrientation interfaceOrientation_) {

    float centeredX = (float)faceRect_.x + (faceRect_.width * .5f);
    float centeredY = (float)faceRect_.y + (faceRect_.height * .5f);

    float xtmp = centeredX * 4.0f;
    float ytmp = centeredY * 4.0f;

    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    float scaleX = 0.0f;
    float scaleY = 0.0f;

    if (adjustInterfaceOrientation(interfaceOrientation_, _isBaseOrientationLandscape) == wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortrait || adjustInterfaceOrientation(interfaceOrientation_, _isBaseOrientationLandscape) == wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortraitUpsideDown) {
        x = (xtmp / _cameraFrameWidth ) - 0.5f * _aspectRatio;
        y = (ytmp / _cameraFrameWidth ) - 0.5f;

        scaleX = ((float)faceRect_.width / (float)frame_.rows * _scaledCameraFrameWidth);
        scaleY = ((float)faceRect_.height / (float)frame_.cols);
    } else {
        x = (xtmp / _cameraFrameWidth ) - 0.5f;
        y = (ytmp / _cameraFrameWidth ) - 0.5f * _aspectRatio;

        scaleX = ((float)faceRect_.width / (float)frame_.cols);
        scaleY = ((float)faceRect_.height / (float)frame_.rows * _scaledCameraFrameHeight);
    }

    _modelViewMatrix[0] = scaleX;	_modelViewMatrix[4] = 0;		_modelViewMatrix[8]  = 0;		_modelViewMatrix[12] = x;
    _modelViewMatrix[1] = 0;		_modelViewMatrix[5] = scaleY;	_modelViewMatrix[9]  = 0;		_modelViewMatrix[13] = -y;
    _modelViewMatrix[2] = 0;		_modelViewMatrix[6] = 0;		_modelViewMatrix[10] = 1;		_modelViewMatrix[14] = z;
    _modelViewMatrix[3] = 0;		_modelViewMatrix[7] = 0;		_modelViewMatrix[11] = 0;		_modelViewMatrix[15] = 1;
}

void FaceDetectionPlugin::calculateProjection(wikitude::sdk::InterfaceOrientation interfaceOrientation_, float left, float right, float bottom, float top, float near, float far) {

    if (adjustInterfaceOrientation(interfaceOrientation_, _isBaseOrientationLandscape) == wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortrait || adjustInterfaceOrientation(interfaceOrientation_, _isBaseOrientationLandscape) == wikitude::sdk::InterfaceOrientation::InterfaceOrientationPortraitUpsideDown) {
        left *= _aspectRatio;
        right *= _aspectRatio;
    } else {
        top *= _aspectRatio;
        bottom *= _aspectRatio;
    }
    _projectionMatrix[0] = 2 / (right - left);	_projectionMatrix[4] = 0;                   _projectionMatrix[8]  = 0;                      _projectionMatrix[12] = -(right + left) / (right - left);
    _projectionMatrix[1] = 0;                   _projectionMatrix[5] = 2 / (top - bottom);	_projectionMatrix[9]  = 0;                      _projectionMatrix[13] = -(top + bottom) / (top - bottom);
    _projectionMatrix[2] = 0;                   _projectionMatrix[6] = 0;                   _projectionMatrix[10] = 2 / (far - near);		_projectionMatrix[14] = -(far + near) / (far - near);
    _projectionMatrix[3] = 0;                   _projectionMatrix[7] = 0;                   _projectionMatrix[11] = 0;                      _projectionMatrix[15] = 1;
}
