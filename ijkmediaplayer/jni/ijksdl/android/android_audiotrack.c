/*****************************************************************************
 * ijksdl_android_audiotrack.c
 *****************************************************************************
 *
 * copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "android_audiotrack.h"

#include <assert.h>
#include "ijkutil/ijkutil_android.h"
#include "../ijksdl_inc_internal.h"
#include "../ijksdl_audio.h"

typedef struct AudioChannelMapEntry {
    Uint8 sdl_channel;
    int android_channel;
} AudioChannelMapEntry;
static AudioChannelMapEntry g_audio_channel_map[] = {
    { 2, CHANNEL_OUT_STEREO },
    { 1, CHANNEL_OUT_MONO },
};

typedef struct AudioFormatMapEntry {
    SDL_AudioFormat sdl_format;
    int android_format;
} AudioFormatMapEntry;
static AudioFormatMapEntry g_audio_format_map[] = {
    { AUDIO_S16, ENCODING_PCM_16BIT },
    { AUDIO_U8, ENCODING_PCM_8BIT },
};

static Uint8 find_sdl_channel(int android_channel)
{
    for (int i = 0; i < NELEM(g_audio_channel_map); ++i) {
        AudioChannelMapEntry *entry = &g_audio_channel_map[i];
        if (entry->android_channel == android_channel)
            return entry->sdl_channel;
    }
    return 0;
}

static int find_android_channel(int sdl_channel)
{
    for (int i = 0; i < NELEM(g_audio_channel_map); ++i) {
        AudioChannelMapEntry *entry = &g_audio_channel_map[i];
        if (entry->sdl_channel == sdl_channel)
            return entry->android_channel;
    }
    return CHANNEL_OUT_INVALID;
}

static Uint8 find_sdl_format(int android_format)
{
    for (int i = 0; i < NELEM(g_audio_format_map); ++i) {
        AudioFormatMapEntry *entry = &g_audio_format_map[i];
        if (entry->android_format == android_format)
            return entry->sdl_format;
    }
    return 0;
}

static int find_android_format(int sdl_format)
{
    for (int i = 0; i < NELEM(g_audio_format_map); ++i) {
        AudioFormatMapEntry *entry = &g_audio_format_map[i];
        if (entry->sdl_format == sdl_format)
            return entry->android_format;
    }
    return ENCODING_INVALID;
}

typedef struct SDL_AndroidAudioTrack {
    jobject thiz;

    SDL_AndroidAudioTrack_Spec spec;

    jbyteArray buffer;
    int buffer_capacity;
    int min_buffer_size;
} SDL_AndroidAudioTrack;

typedef struct audio_track_fields_t {
    jclass clazz;

    jmethodID constructor;
    jmethodID getMinBufferSize;
    jmethodID play;
    jmethodID pause;
    jmethodID flush;
    jmethodID stop;
    jmethodID release;
    jmethodID write_byte;
} audio_track_fields_t;
static audio_track_fields_t g_clazz;

int sdl_audiotrack_global_init(JNIEnv *env)
{
    jclass clazz;

    clazz = (*env)->FindClass(env, "android/media/AudioTrack");
    IJK_CHECK_RET(clazz, -1, "missing AudioTrack");

    // FindClass returns LocalReference
    g_clazz.clazz = (*env)->NewGlobalRef(env, clazz);
    IJK_CHECK_RET(clazz, -1, "AudioTrack NewGlobalRef failed");

    g_clazz.constructor = (*env)->GetMethodID(env, g_clazz.clazz, "<init>", "(IIIIII)V");
    IJK_CHECK_RET(g_clazz.constructor, -1, "missing AudioTrack.<init>");

    g_clazz.getMinBufferSize = (*env)->GetStaticMethodID(env, g_clazz.clazz, "getMinBufferSize", "(III)I");
    IJK_CHECK_RET(g_clazz.getMinBufferSize, -1, "missing AudioTrack.getMinBufferSize");

    g_clazz.play = (*env)->GetMethodID(env, g_clazz.clazz, "play", "()V");
    IJK_CHECK_RET(g_clazz.play, -1, "missing AudioTrack.play");

    g_clazz.pause = (*env)->GetMethodID(env, g_clazz.clazz, "pause", "()V");
    IJK_CHECK_RET(g_clazz.pause, -1, "missing AudioTrack.pause");

    g_clazz.flush = (*env)->GetMethodID(env, g_clazz.clazz, "flush", "()V");
    IJK_CHECK_RET(g_clazz.pause, -1, "missing AudioTrack.flush");

    g_clazz.stop = (*env)->GetMethodID(env, g_clazz.clazz, "stop", "()V");
    IJK_CHECK_RET(g_clazz.pause, -1, "missing AudioTrack.stop");

    g_clazz.release = (*env)->GetMethodID(env, g_clazz.clazz, "release", "()V");
    IJK_CHECK_RET(g_clazz.pause, -1, "missing AudioTrack.release");

    g_clazz.write_byte = (*env)->GetMethodID(env, g_clazz.clazz, "write", "([BII)I");
    IJK_CHECK_RET(g_clazz.pause, -1, "missing AudioTrack.write");

    return 0;
}

static void sdl_audiotrack_get_default_spec(SDL_AndroidAudioTrack_Spec *spec)
{
    assert(spec);
    spec->stream_type = STREAM_MUSIC;
    spec->sample_rate_in_hz = 0;
    spec->channel_config = CHANNEL_OUT_STEREO;
    spec->audio_format = ENCODING_PCM_16BIT;
    spec->buffer_size_in_bytes = 0;
    spec->mode = MODE_STREAM;
}

static int audiotrack_get_min_buffer_size(JNIEnv *env, SDL_AndroidAudioTrack_Spec *spec)
{
    int retval = (*env)->CallStaticIntMethod(env, g_clazz.clazz, g_clazz.getMinBufferSize,
        (int) spec->sample_rate_in_hz,
        (int) spec->channel_config,
        (int) spec->audio_format);
    if ((*env)->ExceptionCheck(env)) {
        ALOGE("sdl_audiotrack_get_min_buffer_size: getMinBufferSize: Exception:");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return -1;
    }

    return retval;
}
SDL_AndroidAudioTrack *sdl_audiotrack_new_from_spec(JNIEnv *env, SDL_AndroidAudioTrack_Spec *spec)
{
    assert(spec);

    switch (spec->channel_config) {
    case CHANNEL_OUT_MONO:
        break;
    case CHANNEL_OUT_STEREO:
        break;
    default:
        ALOGE("sdl_audiotrack_new_from_spec: invalid channel %d", spec->channel_config);
        return NULL;
    }

    switch (spec->audio_format) {
    case ENCODING_PCM_16BIT:
        break;
    case ENCODING_PCM_8BIT:
        break;
    default:
        ALOGE("sdl_audiotrack_new_from_spec: invalid format %d", spec->audio_format);
        return NULL;
    }

    int min_buffer_size = audiotrack_get_min_buffer_size(env, spec);
    if (min_buffer_size <= 0) {
        ALOGE("sdl_audiotrack_new: sdl_audiotrack_get_min_buffer_size: return %d:", min_buffer_size);
        return NULL;
    }

    jobject thiz = (*env)->NewObject(env, g_clazz.clazz, g_clazz.constructor,
        (int) spec->stream_type, (int) spec->sample_rate_in_hz, (int) spec->channel_config,
        (int) spec->audio_format, (int) min_buffer_size, (int) spec->mode);
    if (!thiz || (*env)->ExceptionCheck(env)) {
        ALOGE("sdl_audiotrack_new: NewObject: Exception:");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        return NULL;
    }

    SDL_AndroidAudioTrack *atrack = (SDL_AndroidAudioTrack*) malloc(sizeof(SDL_AndroidAudioTrack));
    if (!atrack) {
        (*env)->CallVoidMethod(env, g_clazz.clazz, atrack->thiz, g_clazz.release);
        (*env)->DeleteLocalRef(env, thiz);
        return NULL;
    }
    memset(atrack, 0, sizeof(SDL_AndroidAudioTrack));

    atrack->spec = *spec;
    atrack->min_buffer_size = min_buffer_size;

    atrack->thiz = (*env)->NewGlobalRef(env, thiz);
    (*env)->DeleteLocalRef(env, thiz);

    return atrack;
}

SDL_AndroidAudioTrack *sdl_audiotrack_new_from_sdl_spec(JNIEnv *env, SDL_AudioSpec *sdl_spec)
{
    SDL_AndroidAudioTrack_Spec atrack_spec;

    sdl_audiotrack_get_default_spec(&atrack_spec);
    atrack_spec.sample_rate_in_hz = sdl_spec->freq;
    atrack_spec.channel_config = find_android_channel(sdl_spec->channels);
    atrack_spec.audio_format = find_android_format(sdl_spec->format);
    atrack_spec.buffer_size_in_bytes = sdl_spec->size;

    // TODO: 9 consider spec.sample
    return sdl_audiotrack_new_from_spec(env, &atrack_spec);
}

void sdl_audiotrack_free(JNIEnv *env, SDL_AndroidAudioTrack* atrack)
{
    if (atrack->buffer) {
        (*env)->DeleteGlobalRef(env, atrack->buffer);
        atrack->buffer = NULL;
    }
    atrack->buffer_capacity = 0;

    if (atrack->thiz) {
        (*env)->DeleteGlobalRef(env, atrack->thiz);
        atrack->thiz = NULL;
    }

    free(atrack);
}

void sdl_audiotrack_get_target_spec(SDL_AndroidAudioTrack *atrack, SDL_AudioSpec *sdl_spec)
{
    SDL_AndroidAudioTrack_Spec *atrack_spec = &atrack->spec;

    sdl_spec->freq = atrack_spec->sample_rate_in_hz;
    sdl_spec->channels = find_sdl_channel(atrack_spec->channel_config);
    sdl_spec->format = find_sdl_format(atrack_spec->audio_format);
    sdl_spec->size = atrack_spec->buffer_size_in_bytes;
    sdl_spec->silence = 0;
    sdl_spec->padding = 0;
}

int sdl_audiotrack_get_min_buffer_size(SDL_AndroidAudioTrack* atrack)
{
    return atrack->min_buffer_size;
}

void sdl_audiotrack_play(JNIEnv *env, SDL_AndroidAudioTrack *atrack)
{
    (*env)->CallVoidMethod(env, g_clazz.clazz, atrack->thiz, g_clazz.play);
    if ((*env)->ExceptionCheck(env)) {
        ALOGE("sdl_audiotrack_play: play: Exception:");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return;
    }
}

void sdl_audiotrack_pause(JNIEnv *env, SDL_AndroidAudioTrack *atrack)
{
    (*env)->CallVoidMethod(env, g_clazz.clazz, atrack->thiz, g_clazz.pause);
    if ((*env)->ExceptionCheck(env)) {
        ALOGE("sdl_audiotrack_pause: pause: Exception:");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return;
    }
}

void sdl_audiotrack_flush(JNIEnv *env, SDL_AndroidAudioTrack *atrack)
{
    (*env)->CallVoidMethod(env, g_clazz.clazz, atrack->thiz, g_clazz.flush);
    if ((*env)->ExceptionCheck(env)) {
        ALOGE("sdl_audiotrack_flush: flush: Exception:");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return;
    }
}

void sdl_audiotrack_stop(JNIEnv *env, SDL_AndroidAudioTrack *atrack)
{
    (*env)->CallVoidMethod(env, g_clazz.clazz, atrack->thiz, g_clazz.stop);
    if ((*env)->ExceptionCheck(env)) {
        ALOGE("sdl_audiotrack_stop: stop: Exception:");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return;
    }
}

void sdl_audiotrack_release(JNIEnv *env, SDL_AndroidAudioTrack *atrack)
{
    (*env)->CallVoidMethod(env, g_clazz.clazz, atrack->thiz, g_clazz.release);
    if ((*env)->ExceptionCheck(env)) {
        ALOGE("sdl_audiotrack_release: release: Exception:");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return;
    }
}

int sdl_audiotrack_reserve_buffer(JNIEnv *env, SDL_AndroidAudioTrack *atrack, int len)
{
    if (atrack->buffer && len <= atrack->buffer_capacity)
        return len;

    if (atrack->buffer) {
        (*env)->DeleteLocalRef(env, atrack->buffer);
        atrack->buffer = NULL;
        atrack->buffer_capacity = 0;
    }

    int capacity = IJKMAX(len, atrack->min_buffer_size);
    jbyteArray buffer = (*env)->NewByteArray(env, capacity);
    if (!buffer || (*env)->ExceptionCheck(env)) {
        ALOGE("sdl_audiotrack_reserve_buffer: NewByteArray: Exception:");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        return -1;
    }

    atrack->buffer_capacity = capacity;
    atrack->buffer = (*env)->NewGlobalRef(env, buffer);
    (*env)->DeleteLocalRef(env, buffer);
    return capacity;
}

int sdl_audiotrack_write_byte(JNIEnv *env, SDL_AndroidAudioTrack *atrack, uint8_t *data, int len)
{
    if (len <= 0)
        return len;

    int reserved = sdl_audiotrack_reserve_buffer(env, atrack, len);
    if (reserved < len)
        return -1;

    (*env)->SetByteArrayRegion(env, atrack->buffer, 0, len, (jbyte*) data);
    if ((*env)->ExceptionCheck(env)) {
        ALOGE("sdl_audiotrack_write_byte: SetByteArrayRegion: Exception:");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        return -1;
    }

    int retval = (*env)->CallIntMethod(env, atrack->thiz, g_clazz.write_byte,
        atrack->buffer, 0, len);
    if ((*env)->ExceptionCheck(env)) {
        ALOGE("sdl_audiotrack_write_byte: flush: Exception:");
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionDescribe(env);
            (*env)->ExceptionClear(env);
        }
        return -1;
    }

    return retval;
}