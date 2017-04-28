//
//  SimpleInputPlugin.h
//
//  Created by Alexander Bendl on 02/03/17.
//  Copyright © 2017 Wikitude. All rights reserved.
//

#ifndef SimpleInputPlugin_h
#define SimpleInputPlugin_h

#include "jni.h"
#include <InputPlugin.h>
#include <RecognizedTarget.h>


extern JavaVM* pluginJavaVM;


class SimpleInputPlugin : public wikitude::sdk::InputPlugin {
public:
    SimpleInputPlugin();

    virtual ~SimpleInputPlugin();

    // from Plugin
    void initialize() override;
    void pause() override;
    void resume(unsigned int pausedTime_) override;
    void destroy() override;
    void update(const std::list<wikitude::sdk::RecognizedTarget>& recognizedTargets_) override;

    // from Input Plugin
    bool requestsInputFrameRendering() override;
    bool requestsInputFrameProcessing() override;
    bool allowsUsageByOtherPlugins() override;

    void notifyNewImageBufferData(std::shared_ptr<unsigned char> imageBufferData_);

protected:
    void callInitializedJNIMethod(jmethodID methodId_);

public:
    static SimpleInputPlugin* instance;

protected:
    long _frameId;
    bool _running;

private:
    jmethodID _pluginInitializedMethodId;
    jmethodID _pluginPausedMethodId;
    jmethodID _pluginResumedMethodId;
    jmethodID _pluginDestroyedMethodId;

    bool _jniInitialized;
};

#endif /* SimpleInputPlugin_h */
