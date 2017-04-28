//
//  BarcodePlugin.cpp
//  DevApplication
//
//  Created by Andreas Schacherbauer on 15/05/15.
//  Copyright (c) 2015 Wikitude. All rights reserved.
//

#include "BarcodePlugin.h"

#include <iostream>
#include <sstream>

BarcodePlugin::BarcodePlugin(int cameraFrameWidth, int cameraFrameHeight) :
Plugin("com.wikitude.android.barcodePlugin"),
_worldNeedsUpdate(0),
_image(cameraFrameWidth, cameraFrameHeight, "Y800", nullptr, 0)
{
}

BarcodePlugin::~BarcodePlugin()
{
}


void BarcodePlugin::initialize() {
    _imageScanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);
}

void BarcodePlugin::destroy() {
    _image.set_data(nullptr, 0);
}

void BarcodePlugin::cameraFrameAvailable(const wikitude::sdk::Frame& cameraFrame_) {
    int frameWidth = cameraFrame_.getSize().width;
    int frameHeight = cameraFrame_.getSize().height;

    _image.set_data(cameraFrame_.getData(), frameWidth * frameHeight);

    int n = _imageScanner.scan(_image);

    if ( n != _worldNeedsUpdate ) {
        if ( n ) {
            std::ostringstream javaScript;
            javaScript << "document.getElementById('loadingMessage').innerHTML = 'Code Content: ";

            zbar::Image::SymbolIterator symbol = _image.symbol_begin();
            javaScript << symbol->get_data();

            javaScript << "';";

            addToJavaScriptQueue(javaScript.str());

        }
    }

    _worldNeedsUpdate = n;
}

void BarcodePlugin::update(const std::list<wikitude::sdk::RecognizedTarget>& recognizedTargets_) {
}