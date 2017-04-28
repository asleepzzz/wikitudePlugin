/*
 * jniHelper.cpp
 *
 *  Created on: Aug 22, 2011
 *      Author: wolfgang
 */

#include "jniHelper.h"

JavaVMResource::JavaVMResource(JavaVM* javaVM) :
	env(NULL), javaVM(javaVM), isAttached(false) {
	jint status = javaVM->GetEnv((void **) &env, JNI_VERSION_1_4);
	if (status < 0) {
		// CALLBACK UNDEFINED
		status = javaVM->AttachCurrentThread(&env, NULL);
		if (status < 0) {
			// failed to attach current thread!
			env = NULL;
			//ERROR("JavaVMResource: Failed to get JavaVM environment.");
			return;
		}
		this->isAttached = true;
	}
	env->PushLocalFrame(JNI_REFS_DEFAULT);
}

JavaVMResource::~JavaVMResource() {
	if (env) {
		env->PopLocalFrame(0);
	}
	if (this->isAttached) {
		javaVM->DetachCurrentThread();
	}
}


JavaStringResource::JavaStringResource(JNIEnv* env, const jstring string) : env(env), javaString(string), str(""), javaStrPtr(NULL) {
	if (!this->javaString || env->GetStringLength(string) == 0)
	{
		// leave empty string
		return;
	}

	this->javaStrPtr = env->GetStringUTFChars(this->javaString, NULL);
	if (this->javaStrPtr == NULL) {
		/* OutOfMemoryError already thrown */
		return;
	}
	this->str.append(javaStrPtr);
}

JavaStringResource::~JavaStringResource() {
	if (this->javaStrPtr) {
		env->ReleaseStringUTFChars(this->javaString, this->javaStrPtr);
	}
}
