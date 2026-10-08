/* port/fmv/android/log/log.h: the one AOSP liblog call libmpeg2 makes under
 * __ANDROID__ (impeg2d_dec_hdr.c, a security event log on malformed
 * streams). The NDK has no <log/log.h>; the decoder already rejects the
 * stream there, so the event is dropped. On the include path of
 * ico_libmpeg2 for Android only (port/fmv/CMakeLists.txt). */
#ifndef ICO_FMV_ANDROID_LOG_LOG_H
#define ICO_FMV_ANDROID_LOG_LOG_H

#define android_errorWriteLog(tag, subTag) ((void)(tag), (void)(subTag))

#endif
