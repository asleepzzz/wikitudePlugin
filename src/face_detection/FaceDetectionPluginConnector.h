//
//  FaceDetectionPluginConnector.h
//  Native Examples
//
//  Created by Andreas Schacherbauer on 05/08/15.
//  Copyright (c) 2015 Wikitude. All rights reserved.
//

#ifndef __Native_Examples__FaceDetectionPluginConnector__
#define __Native_Examples__FaceDetectionPluginConnector__


#include "FaceDetectionPlugin.h"
#include "jni.h"

extern JavaVM* pluginJavaVM;
extern jobject faceDetectionActivityObj;

class FaceDetectionPluginConnector : public FaceDetectionPluginObserver {

public:
    FaceDetectionPluginConnector();
    virtual ~FaceDetectionPluginConnector();

    virtual void faceDetected(const float* modelViewMatrix);
    virtual void faceLost();

    virtual void projectionMatrixChanged(const float* projectionMatrix);

    virtual void renderDetectedFaceAugmentation();

private:
    jmethodID                       _faceDetectedId;
    jmethodID                       _faceLostId;
    jmethodID                       _projectionMatrixChangedId;
    jmethodID 						_renderDetectedFaceAugmentation;
};

#endif /* defined(__Native_Examples__FaceDetectionPluginConnector__) */
