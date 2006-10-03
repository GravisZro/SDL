/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2006 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"
#include "SDL_coreaudio.h"

#define DEBUG_COREAUDIO 1

typedef struct COREAUDIO_DeviceList
{
    AudioDeviceID id;
    const char *name;
} COREAUDIO_DeviceList;

static COREAUDIO_DeviceList *inputDevices = NULL;
static int inputDeviceCount = 0;
static COREAUDIO_DeviceList *outputDevices = NULL;
static int outputDeviceCount = 0;

static void
free_device_list(COREAUDIO_DeviceList **devices, int *devCount)
{
    if (*devices) {
        int i = *devCount;
        while (i--)
            SDL_free((void *) (*devices)[i].name);
        SDL_free(*devices);
        *devices = NULL;
    }
    *devCount = 0;
}


static void
build_device_list(int iscapture, COREAUDIO_DeviceList **devices, int *devCount)
{
    Boolean outWritable = 0;
    OSStatus result = noErr;
    UInt32 size = 0;
    AudioDeviceID *devs = NULL;
    UInt32 i = 0;
    UInt32 max = 0;

    free_device_list(devices, devCount);

    result = AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices,
                                         &size, &outWritable);

    if (result != kAudioHardwareNoError)
        return;

    devs = (AudioDeviceID *) alloca(size);
    if (devs == NULL)
        return;

    max = size / sizeof (AudioDeviceID);
    *devices = (COREAUDIO_DeviceList *) SDL_malloc(max * sizeof (**devices));
    if (*devices == NULL)
        return;

    result = AudioHardwareGetProperty(kAudioHardwarePropertyDevices,
                                     &size, devs);
    if (result != kAudioHardwareNoError)
        return;

    for (i = 0; i < max; i++) {
        CFStringRef cfstr = NULL;
        char *ptr = NULL;
        AudioDeviceID dev = devs[i];
        AudioBufferList *buflist = NULL;
        int usable = 0;
        CFIndex len = 0;

        result = AudioDeviceGetPropertyInfo(dev, 0, iscapture,
                                      kAudioDevicePropertyStreamConfiguration,
                                      &size, &outWritable);
        if (result != noErr)
            continue;

        buflist = (AudioBufferList *) SDL_malloc(size);
        if (buflist == NULL)
            continue;

        result = AudioDeviceGetProperty(dev, 0, iscapture,
                                      kAudioDevicePropertyStreamConfiguration,
                                      &size, buflist);

        if (result == noErr) {
            UInt32 j;
            for (j = 0; j < buflist->mNumberBuffers; j++) {
                if (buflist->mBuffers[j].mNumberChannels > 0) {
                    usable = 1;
                    break;
                }
            }
        }

        SDL_free(buflist);

        if (!usable)
            continue;

        size = sizeof (CFStringRef);
        result = AudioDeviceGetProperty(dev, 0, iscapture,
                                        kAudioObjectPropertyName,
                                        &size, &cfstr);

        if (result != kAudioHardwareNoError)
            continue;

        len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(cfstr),
                                                kCFStringEncodingUTF8);

        ptr = (char *) SDL_malloc(len + 1);
        usable = ( (ptr != NULL) &&
                (CFStringGetCString(cfstr,ptr,len+1,kCFStringEncodingUTF8)) );

        CFRelease(cfstr);

        if (usable) {
            len = strlen(ptr);
            /* Some devices have whitespace at the end...trim it. */
            while ((len > 0) && (ptr[len-1] == ' ')) {
                len--;
            }
            usable = (len > 0);
        }

        if (!usable) {
            SDL_free(ptr);
        } else {
            ptr[len] = '\0';

            #if DEBUG_COREAUDIO
            printf("COREAUDIO: Found %s device #%d: '%s' (devid %d)\n",
                    ((iscapture) ? "capture" : "output"),
                    (int) *devCount, ptr, (int) dev);
            #endif

            (*devices)[*devCount].id = dev;
            (*devices)[*devCount].name = ptr;
            (*devCount)++;
        }
    }
}

static int
find_device_id(const char *devname, int iscapture, AudioDeviceID *id)
{
    int i = ((iscapture) ? inputDeviceCount : outputDeviceCount);
    COREAUDIO_DeviceList *devs = ((iscapture) ? inputDevices : outputDevices);
    while (i--) {
        if (SDL_strcmp(devname, devs->name) == 0) {
            *id = devs->id;
            return 1;
        }
        devs++;
    }

    return 0;
}


/* Audio driver functions */

static int COREAUDIO_OpenAudio(_THIS, const char *devname, int iscapture);
static void COREAUDIO_WaitAudio(_THIS);
static void COREAUDIO_PlayAudio(_THIS);
static Uint8 *COREAUDIO_GetAudioBuf(_THIS);
static void COREAUDIO_CloseAudio(_THIS);
static void COREAUDIO_Deinitialize(void);

/* Audio driver bootstrap functions */

static int
COREAUDIO_Available(void)
{
    return (1);
}

static int
COREAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    /* !!! FIXME: should these _really_ be static? */
    build_device_list(0, &outputDevices, &outputDeviceCount);
    build_device_list(1, &inputDevices, &inputDeviceCount);

    /* Set the function pointers */
    impl->OpenAudio = COREAUDIO_OpenAudio;
    impl->WaitAudio = COREAUDIO_WaitAudio;
    impl->PlayAudio = COREAUDIO_PlayAudio;
    impl->GetAudioBuf = COREAUDIO_GetAudioBuf;
    impl->CloseAudio = COREAUDIO_CloseAudio;
    impl->Deinitialize = COREAUDIO_Deinitialize;

    return 1;
}

AudioBootStrap COREAUDIO_bootstrap = {
    "coreaudio", "Mac OS X CoreAudio",
    COREAUDIO_Available, COREAUDIO_Init
};


static void
COREAUDIO_Deinitialize(void)
{
    free_device_list(&outputDevices, &outputDeviceCount);
    free_device_list(&inputDevices, &inputDeviceCount);
}


/* The CoreAudio callback */
static OSStatus
outputCallback(void *inRefCon,
              AudioUnitRenderActionFlags *ioActionFlags,
              const AudioTimeStamp * inTimeStamp,
              UInt32 inBusNumber, UInt32 inNumberFrames,
              AudioBufferList *ioDataList)
{
    SDL_AudioDevice *this = (SDL_AudioDevice *) inRefCon;
    AudioBuffer *ioData = &ioDataList->mBuffers[0];
    UInt32 remaining, len;
    void *ptr;

    /*
     * !!! FIXME: I'm not sure if you can ever have more than one
     *            buffer, or what this signifies, or what to do with it...
     */
    if (ioDataList->mNumberBuffers != 1) {
        return noErr;
    }

    /* Only do anything if audio is enabled and not paused */
    if (!this->enabled || this->paused) {
        SDL_memset(ioData->mData, this->spec.silence, ioData->mDataByteSize);
        return 0;
    }

    /* No SDL conversion should be needed here, ever, since we accept
       any input format in OpenAudio, and leave the conversion to CoreAudio.
     */
    /*
       assert(!this->convert.needed);
       assert(this->spec.channels == ioData->mNumberChannels);
     */

    remaining = ioData->mDataByteSize;
    ptr = ioData->mData;
    while (remaining > 0) {
        if (this->hidden->bufferOffset >= this->hidden->bufferSize) {
            /* Generate the data */
            SDL_memset(this->hidden->buffer, this->spec.silence,
                       this->hidden->bufferSize);
            SDL_mutexP(this->mixer_lock);
            (*this->spec.callback) (this->spec.userdata, this->hidden->buffer,
                                    this->hidden->bufferSize);
            SDL_mutexV(this->mixer_lock);
            this->hidden->bufferOffset = 0;
        }

        len = this->hidden->bufferSize - this->hidden->bufferOffset;
        if (len > remaining)
            len = remaining;
        SDL_memcpy(ptr,
                    (char *) this->hidden->buffer + this->hidden->bufferOffset,
                    len);
        ptr = (char *) ptr + len;
        remaining -= len;
        this->hidden->bufferOffset += len;
    }

    return 0;
}

static OSStatus
inputCallback(void *inRefCon,
              AudioUnitRenderActionFlags *ioActionFlags,
              const AudioTimeStamp * inTimeStamp,
              UInt32 inBusNumber, UInt32 inNumberFrames,
              AudioBufferList *ioData)
{
    //err = AudioUnitRender(afr->fAudioUnit, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, afr->fAudioBuffer);
    // !!! FIXME: write me!
    return noErr;
}


/* Dummy functions -- we don't use thread-based audio */
void
COREAUDIO_WaitAudio(_THIS)
{
    return;
}

void
COREAUDIO_PlayAudio(_THIS)
{
    return;
}

Uint8 *
COREAUDIO_GetAudioBuf(_THIS)
{
    return (NULL);
}

void
COREAUDIO_CloseAudio(_THIS)
{
    if (this->hidden != NULL) {
        OSStatus result = noErr;
        AURenderCallbackStruct callback;
        const AudioUnitElement output_bus = 0;
        const AudioUnitElement input_bus = 1;
        const int iscapture = this->hidden->isCapture;
        const AudioUnitElement bus = ((iscapture) ? input_bus : output_bus);
        const AudioUnitScope scope = ((iscapture) ? kAudioUnitScope_Output :
                                                    kAudioUnitScope_Input);

        /* stop processing the audio unit */
        result = AudioOutputUnitStop(this->hidden->audioUnit);

        /* Remove the input callback */
        memset(&callback, '\0', sizeof (AURenderCallbackStruct));
        result = AudioUnitSetProperty(this->hidden->audioUnit,
                                      kAudioUnitProperty_SetRenderCallback,
                                      scope, bus, &callback, sizeof (callback));

        CloseComponent(this->hidden->audioUnit);

        SDL_free(this->hidden->buffer);
        SDL_free(this->hidden);
        this->hidden = NULL;
    }
}


#define CHECK_RESULT(msg) \
    if (result != noErr) { \
        COREAUDIO_CloseAudio(this); \
        SDL_SetError("CoreAudio error (%s): %d", msg, (int) result); \
        return 0; \
    }

static int
find_device_by_name(_THIS, const char *devname, int iscapture)
{
    AudioDeviceID devid = 0;
    OSStatus result = noErr;
    UInt32 size = 0;
    UInt32 alive = 0;
    pid_t pid = 0;

    if (devname == NULL) {
        size = sizeof (AudioDeviceID);
        const AudioHardwarePropertyID propid =
                    ((iscapture) ? kAudioHardwarePropertyDefaultInputDevice :
                                   kAudioHardwarePropertyDefaultOutputDevice);

        result = AudioHardwareGetProperty(propid, &size, &devid);
        CHECK_RESULT("AudioHardwareGetProperty (default device)");
    } else {
        if (!find_device_id(devname, iscapture, &devid)) {
            SDL_SetError("CoreAudio: No such audio device.");
            return 0;
        }
    }

    size = sizeof (alive);
    result = AudioDeviceGetProperty(devid, 0, iscapture,
                                    kAudioDevicePropertyDeviceIsAlive,
                                    &size, &alive);
    CHECK_RESULT("AudioDeviceGetProperty (kAudioDevicePropertyDeviceIsAlive)");

    if (!alive) {
        SDL_SetError("CoreAudio: requested device exists, but isn't alive.");
        return 0;
    }

    size = sizeof (pid);
    result = AudioDeviceGetProperty(devid, 0, iscapture,
                                    kAudioDevicePropertyHogMode, &size, &pid);

    /* some devices don't support this property, so errors are fine here. */
    if ((result == noErr) && (pid != -1)) {
        SDL_SetError("CoreAudio: requested device is being hogged.");
        return 0;
    }

    this->hidden->deviceID = devid;
    return 1;
}


static int
prepare_audiounit(_THIS, const char *devname, int iscapture,
                  const AudioStreamBasicDescription *strdesc)
{
    OSStatus result = noErr;
    AURenderCallbackStruct callback;
    ComponentDescription desc;
    Component comp = NULL;
    int use_system_device = 0;
    UInt32 enableIO = 0;
    const AudioUnitElement output_bus = 0;
    const AudioUnitElement input_bus = 1;
    const AudioUnitElement bus = ((iscapture) ? input_bus : output_bus);
    const AudioUnitScope scope = ((iscapture) ? kAudioUnitScope_Output :
                                                kAudioUnitScope_Input);

    if (!find_device_by_name(this, devname, iscapture)) {
        SDL_SetError("Couldn't find requested CoreAudio device");
        return 0;
    }

    memset(&desc, '\0', sizeof(ComponentDescription));
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    comp = FindNextComponent(NULL, &desc);
    if (comp == NULL) {
        SDL_SetError("Couldn't find requested CoreAudio component");
        return 0;
    }

    /* Open & initialize the audio unit */
    result = OpenAComponent(comp, &this->hidden->audioUnit);
    CHECK_RESULT("OpenAComponent");

    // !!! FIXME: this is wrong?
    enableIO = ((iscapture) ? 1 : 0);
    result = AudioUnitSetProperty(this->hidden->audioUnit,
                                  kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Input, input_bus,
                                  &enableIO, sizeof (enableIO));
    CHECK_RESULT("AudioUnitSetProperty (kAudioUnitProperty_EnableIO input)");

    // !!! FIXME: this is wrong?
    enableIO = ((iscapture) ? 0 : 1);
    result = AudioUnitSetProperty(this->hidden->audioUnit,
                                  kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Output, output_bus,
                                  &enableIO, sizeof (enableIO));
    CHECK_RESULT("AudioUnitSetProperty (kAudioUnitProperty_EnableIO output)");

    result = AudioUnitSetProperty(this->hidden->audioUnit,
                                  kAudioOutputUnitProperty_CurrentDevice,
                                  kAudioUnitScope_Global, 0,
                                  &this->hidden->deviceID,
                                  sizeof (AudioDeviceID));
    CHECK_RESULT("AudioUnitSetProperty (kAudioOutputUnitProperty_CurrentDevice)");

    /* Set the data format of the audio unit. */
    result = AudioUnitSetProperty(this->hidden->audioUnit,
                                  kAudioUnitProperty_StreamFormat,
                                  scope, bus, strdesc, sizeof (*strdesc));
    CHECK_RESULT("AudioUnitSetProperty (kAudioUnitProperty_StreamFormat)");

    /* Set the audio callback */
    memset(&callback, '\0', sizeof (AURenderCallbackStruct));
    callback.inputProc = ((iscapture) ? inputCallback : outputCallback);
    callback.inputProcRefCon = this;
    result = AudioUnitSetProperty(this->hidden->audioUnit,
                                  kAudioUnitProperty_SetRenderCallback,
                                  scope, bus, &callback, sizeof (callback));
    CHECK_RESULT("AudioUnitSetProperty (kAudioUnitProperty_SetInputCallback)");

    /* Calculate the final parameters for this audio specification */
    SDL_CalculateAudioSpec(&this->spec);

    /* Allocate a sample buffer */
    this->hidden->bufferOffset = this->hidden->bufferSize = this->spec.size;
    this->hidden->buffer = SDL_malloc(this->hidden->bufferSize);

    result = AudioUnitInitialize(this->hidden->audioUnit);
    CHECK_RESULT("AudioUnitInitialize");

    /* Finally, start processing of the audio unit */
    result = AudioOutputUnitStart(this->hidden->audioUnit);
    CHECK_RESULT("AudioOutputUnitStart");

    /* We're running! */
    return 1;
}


int
COREAUDIO_OpenAudio(_THIS, const char *devname, int iscapture)
{
    AudioStreamBasicDescription strdesc;
    SDL_AudioFormat test_format = SDL_FirstAudioFormat(this->spec.format);
    int valid_datatype = 0;

    /* Initialize all variables that we clean on shutdown */
    this->hidden = (struct SDL_PrivateAudioData *)
                        SDL_malloc((sizeof *this->hidden));
    if (this->hidden == NULL) {
        SDL_OutOfMemory();
        return (0);
    }
    SDL_memset(this->hidden, 0, (sizeof *this->hidden));

    this->hidden->isCapture = iscapture;

    /* Setup a AudioStreamBasicDescription with the requested format */
    memset(&strdesc, '\0', sizeof(AudioStreamBasicDescription));
    strdesc.mFormatID = kAudioFormatLinearPCM;
    strdesc.mFormatFlags = kLinearPCMFormatFlagIsPacked;
    strdesc.mChannelsPerFrame = this->spec.channels;
    strdesc.mSampleRate = this->spec.freq;
    strdesc.mFramesPerPacket = 1;

    while ((!valid_datatype) && (test_format)) {
        this->spec.format = test_format;
        /* Just a list of valid SDL formats, so people don't pass junk here. */
        switch (test_format) {
        case AUDIO_U8:
        case AUDIO_S8:
        case AUDIO_U16LSB:
        case AUDIO_S16LSB:
        case AUDIO_U16MSB:
        case AUDIO_S16MSB:
        case AUDIO_S32LSB:
        case AUDIO_S32MSB:
        case AUDIO_F32LSB:
        case AUDIO_F32MSB:
            valid_datatype = 1;
            strdesc.mBitsPerChannel = SDL_AUDIO_BITSIZE(this->spec.format);
            if (SDL_AUDIO_ISBIGENDIAN(this->spec.format))
                strdesc.mFormatFlags |= kLinearPCMFormatFlagIsBigEndian;

            if (SDL_AUDIO_ISFLOAT(this->spec.format))
                strdesc.mFormatFlags |= kLinearPCMFormatFlagIsFloat;
            else if (SDL_AUDIO_ISSIGNED(this->spec.format))
                strdesc.mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
            break;
        }
    }

    if (!valid_datatype) {      /* shouldn't happen, but just in case... */
        SDL_SetError("Unsupported audio format");
        return 0;
    }

    strdesc.mBytesPerFrame =
        strdesc.mBitsPerChannel * strdesc.mChannelsPerFrame / 8;
    strdesc.mBytesPerPacket =
        strdesc.mBytesPerFrame * strdesc.mFramesPerPacket;

    if (!prepare_audiounit(this, devname, iscapture, &strdesc)) {
        return 0;  /* prepare_audiounit() will call SDL_SetError()... */
    }

    return 1;  /* good to go. */
}

/* vi: set ts=4 sw=4 expandtab: */
