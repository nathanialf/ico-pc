/*
 * port/platform/android/last_run_android.c
 *
 * last-run.txt's summary to the system (last_run_android.h).
 */
#include "last_run_android.h"

#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "last_run.h"

/* the method id looked up once; a failed lookup remembered (the app's
   Java without the method: one line, then nothing) */
static jmethodID s_mid;
static int s_failed;
static int s_said;

/* Calls the static Java method IcoActivity.processStateSummary(byte[]) ->
   String (null when the system took it, else why not: before Android 11,
   no activity, the system's refusal), as host_android.c calls its methods:
   the calling thread's JNI environment (SDL_GetAndroidJNIEnv attaches a
   thread that has none), every local reference deleted, a pending
   exception cleared.  The first outcome is logged, then only a change from
   taken to refused. */
static void hand_over(const char *summary)
{
    JNIEnv *env;
    jobject act;
    jclass cls;
    jbyteArray bytes = NULL;
    jstring answer = NULL;
    char why[160] = "";
    int ok = 0;
    size_t n = strlen(summary);

    if (s_failed) {
        return;
    }
    if (n > LAST_RUN_SUMMARY_MAX - 1) {
        n = LAST_RUN_SUMMARY_MAX - 1; /* the system's 128 bytes */
    }
    env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    act = env != NULL ? (jobject)SDL_GetAndroidActivity() : NULL;
    if (env == NULL || act == NULL) {
        return;
    }
    cls = (*env)->GetObjectClass(env, act);
    if (cls != NULL) {
        if (s_mid == NULL) {
            s_mid = (*env)->GetStaticMethodID(env, cls, "processStateSummary",
                                              "([B)Ljava/lang/String;");
            if (s_mid == NULL) {
                s_failed = 1;
                snprintf(why, sizeof(why), "this app has no IcoActivity.processStateSummary");
            }
        }
        if (s_mid != NULL) {
            bytes = (*env)->NewByteArray(env, (jsize)n);
            if (bytes == NULL) {
                snprintf(why, sizeof(why), "no memory for the summary");
            } else {
                (*env)->SetByteArrayRegion(env, bytes, 0, (jsize)n, (const jbyte *)summary);
                answer = (jstring)(*env)->CallStaticObjectMethod(env, cls, s_mid, bytes);
                if ((*env)->ExceptionCheck(env)) {
                    snprintf(why, sizeof(why), "a Java exception in processStateSummary");
                } else if (answer == NULL) {
                    ok = 1;
                } else {
                    const char *s = (*env)->GetStringUTFChars(env, answer, NULL);
                    snprintf(why, sizeof(why), "%s", s != NULL ? s : "refused");
                    if (s != NULL) {
                        (*env)->ReleaseStringUTFChars(env, answer, s);
                    }
                }
            }
        }
    } else {
        snprintf(why, sizeof(why), "no activity class");
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
    if (answer != NULL) {
        (*env)->DeleteLocalRef(env, answer);
    }
    if (bytes != NULL) {
        (*env)->DeleteLocalRef(env, bytes);
    }
    if (cls != NULL) {
        (*env)->DeleteLocalRef(env, cls);
    }
    (*env)->DeleteLocalRef(env, act);
    if (!s_said || (s_said == 1 && !ok)) {
        s_said = ok ? 1 : 2;
        if (ok) {
            fprintf(stderr,
                    "ico_pc: last-run: the system keeps a summary of where the run is in its exit "
                    "record (setProcessStateSummary, at each phase change): \"%.*s\"\n",
                    (int)n, summary);
        } else {
            fprintf(stderr, "ico_pc: last-run: no summary in the system's exit record: %s\n", why);
        }
    }
}

void last_run_android_start(void)
{
    last_run_set_summary_hook(hand_over);
}
