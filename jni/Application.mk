APP_STL := gnustl_static
APP_CPPFLAGS := -frtti -fexceptions -std=c++11
APP_PLATFORM := android-9
APP_MODULES := wikitudePlugins

APP_ABI := armeabi-v7a arm64-v8a x86
APP_CFLAGS := -DDEBUG_LOG
APP_OPTIM := debug
NDK_DEBUG := 1

NDK_TOOLCHAIN_VERSION := 4.9
