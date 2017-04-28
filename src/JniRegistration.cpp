#include <jni.h>

#include "jniHelper.h"
#include "Plugin.h"
//#include "FaceDetectionPluginConnector.h"
//#include "FaceDetectionPlugin.h"
#include "BarcodePlugin.h"
//#include "MarkerTrackingPlugin.h"
#include "YUVFrameInputPlugin.h"
#include "SimpleInputPlugin.h"


JavaVM* pluginJavaVM;

extern "C" JNIEXPORT jlongArray JNICALL Java_com_wikitude_common_plugins_internal_PluginManagerInternal_createNativePlugins(JNIEnv *env, jobject thisObj, jstring jPluginName) {

    env->GetJavaVM(&pluginJavaVM);

    int numberOfPlugins = 1;

    jlong cPluginsArray[numberOfPlugins];

    JavaStringResource pluginName(env, jPluginName);

    if (pluginName.str == "face_detection") {
        //FaceDetectionPluginConnector* connector = new FaceDetectionPluginConnector();
        //cPluginsArray[0] = (jlong) new FaceDetectionPlugin(640, 480, connector);
    } else if (pluginName.str == "barcode") {
    	cPluginsArray[0] = (jlong) new BarcodePlugin(640, 480);
    } else if ( pluginName.str == "customcamera" ) {
        cPluginsArray[0] = (jlong) new YUVFrameInputPlugin();
    } else if ( pluginName.str == "markertracking") {
        //cPluginsArray[0] = (jlong) new MarkerTrackingPlugin();
    } else if ( pluginName.str == "simple_input_plugin" ) {
        cPluginsArray[0] = (jlong) new SimpleInputPlugin();
    }

    jlongArray jPluginsArray = env->NewLongArray(numberOfPlugins);
    if (jPluginsArray != nullptr) {
        env->SetLongArrayRegion(jPluginsArray, 0, numberOfPlugins, cPluginsArray);
    }

    return jPluginsArray;
}
