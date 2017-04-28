//
//  MarkerTrackingPlugin.h
//  SDKExamples
//
//  Created by Daniel Guttenberg on 23/09/15.
//  Copyright (c) 2015 Wikitude. All rights reserved.
//

#ifndef __SDKExamples__MarkerTrackingPlugin__
#define __SDKExamples__MarkerTrackingPlugin__

#include "jni.h"

#include "Plugin.h"
#include "Matrix4.h"

#include <aruco.h>

#include <mutex>
#include <vector>

namespace wikitude { namespace sdk_core {
    namespace impl {
        class PositionableWrapper;
    }
}}

extern JavaVM* pluginJavaVM;

class MarkerTrackingPlugin : public wikitude::sdk::Plugin {
public:
    MarkerTrackingPlugin();
    ~MarkerTrackingPlugin();

    virtual void surfaceChanged(wikitude::sdk::Size<int> renderSurfaceSize_, wikitude::sdk::Size<float> cameraSurfaceScaling_, wikitude::sdk::InterfaceOrientation interfaceOrientation_);

    virtual void setDefaultOrientationLandscape(bool defaultOrientationLandscape_);
    
    virtual void cameraFrameAvailable(const wikitude::sdk::impl::Frame& cameraFrame_);

    virtual void update(const std::list<wikitude::sdk::impl::RecognizedTarget>& recognizedTargets_);

    virtual void updatePositionables(const std::unordered_map<std::string, wikitude::sdk_core::impl::PositionableWrapper*>& positionables_);

    static MarkerTrackingPlugin* instance;

private:
    aruco::MarkerDetector _detector;
    std::vector<aruco::Marker> _markers;
    std::vector<aruco::Marker> _markersPrev;
    std::vector<aruco::Marker> _markersCurr;
    std::vector<aruco::Marker> _markersPrevUpdate;
    std::vector<aruco::Marker> _markersCurrUpdate;

    bool _projectionInitialized;
    float _width;
    float _height;
    float _scaleWidth;
    float _scaleHeight;
    bool _defaultOrientationLandscape;
    
    std::mutex _markerMutex;
    bool _updateDone;

    float _viewMatrixData[16];
    wikitude::sdk::Matrix4 _projectionMatrix;

    std::mutex _interfaceOrientationMutex;
    wikitude::sdk::InterfaceOrientation _currentInterfaceOrientation;
};

#endif /* defined(__SDKExamples__MarkerTrackingPlugin__) */
