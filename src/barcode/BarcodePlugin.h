//
//  BarCodePlugin.h
//  DevApplication
//
//  Created by Andreas Schacherbauer on 15/05/15.
//  Copyright (c) 2015 Wikitude. All rights reserved.
//

#ifndef __DevApplication__BarCodePlugin__
#define __DevApplication__BarCodePlugin__

#include <zbar.h>
#include "jni.h"

#include <Plugin.h>
#include <Frame.h>
#include <RecognizedTarget.h>

extern JavaVM* pluginJavaVM;

class BarcodePlugin : public wikitude::sdk::Plugin {
public:
    BarcodePlugin(int cameraFrameWidth, int cameraFrameHeight);
    virtual ~BarcodePlugin();

    virtual void initialize();
    virtual void destroy();

    virtual void cameraFrameAvailable(const wikitude::sdk::Frame& cameraFrame_);
    virtual void update(const std::list<wikitude::sdk::RecognizedTarget>& recognizedTargets_);


protected:
    int                             _worldNeedsUpdate;
    
    zbar::Image                     _image;
    zbar::ImageScanner              _imageScanner;
    
private:
    jmethodID                       _methodId;
    bool							_jniInitialized;
};

#endif /* defined(__DevApplication__BarCodePlugin__) */
