/*
 * port/platform/android/exit_info_android.c
 *
 * Android's exit records (exit_info.h): IcoActivity.exitRecords through
 * JNI, as host_android.c calls its static methods (the calling thread's JNI
 * environment, every local reference deleted, a pending exception cleared).
 */
#include "exit_info.h"

#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>

int exit_info_system_records(const char *logsDir, long long lastSeenMs, char *out, size_t size)
{
    JNIEnv *env;
    jobject act;
    jclass cls;
    jmethodID mid;
    jstring dir = NULL, answer = NULL;
    int rc = -1;

    if (size == 0) {
        return -1;
    }
    snprintf(out, size, "why\tno Java environment\n");
    env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    act = env != NULL ? (jobject)SDL_GetAndroidActivity() : NULL;
    if (env == NULL || act == NULL) {
        return -1;
    }
    cls = (*env)->GetObjectClass(env, act);
    if (cls == NULL) {
        snprintf(out, size, "why\tno activity class\n");
    } else {
        /* exitRecords(String logsDir, long lastSeenMs, int maxRecords,
           int traceMaxBytes, int descMax): the record text */
        mid = (*env)->GetStaticMethodID(env, cls, "exitRecords",
                                        "(Ljava/lang/String;JIII)Ljava/lang/String;");
        if (mid == NULL) {
            snprintf(out, size, "why\tthis app has no IcoActivity.exitRecords\n");
        } else if ((dir = (*env)->NewStringUTF(env, logsDir != NULL ? logsDir : "")) == NULL) {
            snprintf(out, size, "why\tno memory for the folder's name\n");
        } else {
            answer = (jstring)(*env)->CallStaticObjectMethod(
                env, cls, mid, dir, (jlong)lastSeenMs, (jint)EXIT_INFO_MAX_RECORDS,
                (jint)EXIT_INFO_TRACE_MAX_BYTES, (jint)(EXIT_INFO_DESC_MAX - 1));
            if ((*env)->ExceptionCheck(env)) {
                snprintf(out, size, "why\ta Java exception in exitRecords\n");
            } else if (answer == NULL) {
                snprintf(out, size, "why\texitRecords gave no answer\n");
            } else {
                const char *s = (*env)->GetStringUTFChars(env, answer, NULL);
                if (s != NULL) {
                    /* a text past the room is cut: its last line then reads
                       as malformed and is counted so */
                    snprintf(out, size, "%s", s);
                    (*env)->ReleaseStringUTFChars(env, answer, s);
                    rc = 0;
                } else {
                    snprintf(out, size, "why\tthe answer could not be read\n");
                }
            }
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
    if (answer != NULL) {
        (*env)->DeleteLocalRef(env, answer);
    }
    if (dir != NULL) {
        (*env)->DeleteLocalRef(env, dir);
    }
    if (cls != NULL) {
        (*env)->DeleteLocalRef(env, cls);
    }
    (*env)->DeleteLocalRef(env, act);
    return rc;
}
