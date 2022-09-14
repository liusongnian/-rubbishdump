/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "StreamHAL"

#include "Stream.h"
#include "common/all-versions/HidlSupport.h"
#include "common/all-versions/default/EffectMap.h"
#include "Util.h"

#include <inttypes.h>

#include <HidlUtils.h>
#include <android/log.h>
#include <hardware/audio.h>
#include <hardware/audio_effect.h>
#include <media/AudioContainers.h>
#include <media/TypeConverter.h>
#include <util/CoreUtils.h>

namespace android {
namespace hardware {
namespace audio {
namespace CPP_VERSION {
namespace implementation {

using ::android::hardware::audio::common::CPP_VERSION::implementation::HidlUtils;
using ::android::hardware::audio::common::utils::splitString;

Stream::Stream(bool isInput, audio_stream_t* stream) : mIsInput(isInput), mStream(stream) {
    (void)mIsInput;  // prevent 'unused field' warnings in pre-V7 versions.
}

Stream::~Stream() {
    mStream = nullptr;
}

// static
Result Stream::analyzeStatus(const char* funcName, int status) {
    return util::analyzeStatus("stream", funcName, status);
}

// static
Result Stream::analyzeStatus(const char* funcName, int status,
                             const std::vector<int>& ignoreErrors) {
    return util::analyzeStatus("stream", funcName, status, ignoreErrors);
}

char *Stream::halGetParameters(const char *keys) {
    return mStream->get_parameters(mStream, keys);
}

int Stream::halSetParameters(const char *keysAndValues) {
    return mStream->set_parameters(mStream, keysAndValues);
}

// Methods from ::android::hardware::audio::CPP_VERSION::IStream follow.
Return<uint64_t> Stream::getFrameSize()  {
    // Needs to be implemented by interface subclasses. But can't be declared as pure virtual,
    // since interface subclasses implementation do not inherit from this class.
    LOG_ALWAYS_FATAL("Stream::getFrameSize is pure abstract");
    return uint64_t {};
}

Return<uint64_t> Stream::getFrameCount()  {
    int halFrameCount;
    Result retval = getParam(AudioParameter::keyFrameCount, &halFrameCount);
    return retval == Result::OK ? halFrameCount : 0;
}

Return<uint64_t> Stream::getBufferSize()  {
    return mStream->get_buffer_size(mStream);
}

Return<void> Stream::getSupportedProfiles(getSupportedProfiles_cb _hidl_cb) {
    String8 halListValue;
    Result result = getParam(AudioParameter::keyStreamSupportedFormats, &halListValue);
    hidl_vec<AudioProfile> profiles;
    if (result != Result::OK) {
        _hidl_cb(result, profiles);
        return Void();
    }
    // Ensure that the separator is one character, despite that it's defined as a C string.
    static_assert(sizeof(AUDIO_PARAMETER_VALUE_LIST_SEPARATOR) == 2);
    std::vector<std::string> halFormats =
            splitString(halListValue.string(), AUDIO_PARAMETER_VALUE_LIST_SEPARATOR[0]);
    hidl_vec<AudioFormat> formats;
    (void)HidlUtils::audioFormatsFromHal(halFormats, &formats);
    std::vector<AudioProfile> tempProfiles;
    for (const auto& format : formats) {
        audio_format_t halFormat;
        if (status_t status = HidlUtils::audioFormatToHal(format, &halFormat); status != NO_ERROR) {
            continue;
        }
        AudioParameter context;
        context.addInt(String8(AUDIO_PARAMETER_STREAM_FORMAT), int(halFormat));
        // Query supported sample rates for the format.
        result = getParam(AudioParameter::keyStreamSupportedSamplingRates, &halListValue, context);
        if (result != Result::OK) break;
        std::vector<std::string> halSampleRates =
                splitString(halListValue.string(), AUDIO_PARAMETER_VALUE_LIST_SEPARATOR[0]);
        hidl_vec<uint32_t> sampleRates;
        sampleRates.resize(halSampleRates.size());
        for (size_t i = 0; i < sampleRates.size(); ++i) {
            sampleRates[i] = std::stoi(halSampleRates[i]);
        }
        // Query supported channel masks for the format.
        result = getParam(AudioParameter::keyStreamSupportedChannels, &halListValue, context);
        if (result != Result::OK) break;
        std::vector<std::string> halChannelMasks =
                splitString(halListValue.string(), AUDIO_PARAMETER_VALUE_LIST_SEPARATOR[0]);
        hidl_vec<AudioChannelMask> channelMasks;
        (void)HidlUtils::audioChannelMasksFromHal(halChannelMasks, &channelMasks);
        // Create a profile.
        if (channelMasks.size() != 0 && sampleRates.size() != 0) {
            tempProfiles.push_back({.format = format,
                                    .sampleRates = std::move(sampleRates),
                                    .channelMasks = std::move(channelMasks)});
        }
    }
    // Legacy get_parameter does not return a status_t, thus can not advertise of failure.
    // Note that the method must not return an empty list if this capability is supported.
    if (!tempProfiles.empty()) {
        profiles = tempProfiles;
    } else {
        result = Result::NOT_SUPPORTED;
    }
    _hidl_cb(result, profiles);
    return Void();
}

Return<void> Stream::getAudioProperties(getAudioProperties_cb _hidl_cb) {
    audio_config_base_t halConfigBase = {mStream->get_sample_rate(mStream),
                                         mStream->get_channels(mStream),
                                         mStream->get_format(mStream)};
    AudioConfigBase configBase = {};
    status_t status = HidlUtils::audioConfigBaseFromHal(halConfigBase, mIsInput, &configBase);
    _hidl_cb(Stream::analyzeStatus("get_audio_properties", status), configBase);
    return Void();
}

Return<Result> Stream::setAudioProperties(const AudioConfigBaseOptional& config) {
    audio_config_base_t halConfigBase = AUDIO_CONFIG_BASE_INITIALIZER;
    bool formatSpecified, sRateSpecified, channelMaskSpecified;
    status_t status = HidlUtils::audioConfigBaseOptionalToHal(
            config, &halConfigBase, &formatSpecified, &sRateSpecified, &channelMaskSpecified);
    if (status != NO_ERROR) {
        return Stream::analyzeStatus("set_audio_properties", status);
    }
    if (sRateSpecified) {
        if (Result result = setParam(AudioParameter::keySamplingRate,
                                     static_cast<int>(halConfigBase.sample_rate));
            result != Result::OK) {
            return result;
        }
    }
    if (channelMaskSpecified) {
        if (Result result = setParam(AudioParameter::keyChannels,
                                     static_cast<int>(halConfigBase.channel_mask));
            result != Result::OK) {
            return result;
        }
    }
    if (formatSpecified) {
        if (Result result =
                    setParam(AudioParameter::keyFormat, static_cast<int>(halConfigBase.format));
            result != Result::OK) {
            return result;
        }
    }
    return Result::OK;
}

Return<Result> Stream::addEffect(uint64_t effectId)  {
    effect_handle_t halEffect = EffectMap::getInstance().get(effectId);
    ALOGI("addEffect(): halEffect = %p, effectId = %" PRIu64, halEffect, effectId);
    if (halEffect != NULL) {
        return analyzeStatus("add_audio_effect", mStream->add_audio_effect(mStream, halEffect));
    } else {
        ALOGW("Invalid effect ID passed from client: %" PRIu64, effectId);
        return Result::INVALID_ARGUMENTS;
    }
}

Return<Result> Stream::removeEffect(uint64_t effectId)  {
    effect_handle_t halEffect = EffectMap::getInstance().get(effectId);
    ALOGI("removeEffect(): halEffect = %p, effectId = %" PRIu64, halEffect, effectId);
    if (halEffect != NULL) {
        return analyzeStatus("remove_audio_effect",
                             mStream->remove_audio_effect(mStream, halEffect));
    } else {
        ALOGW("Invalid effect ID passed from client: %" PRIu64, effectId);
        return Result::INVALID_ARGUMENTS;
    }
}

Return<Result> Stream::standby()  {
    return analyzeStatus("standby", mStream->standby(mStream));
}

Return<Result> Stream::setHwAvSync(uint32_t hwAvSync) {
    return setParam(AudioParameter::keyStreamHwAvSync, static_cast<int>(hwAvSync));
}

Return<void> Stream::getDevices(getDevices_cb _hidl_cb) {
    int halDevice = 0;
    Result retval = getParam(AudioParameter::keyRouting, &halDevice);
    hidl_vec<DeviceAddress> devices;
    if (retval == Result::OK) {
        devices.resize(1);
        retval = Stream::analyzeStatus(
                "get_devices",
                CoreUtils::deviceAddressFromHal(static_cast<audio_devices_t>(halDevice), nullptr,
                                                &devices[0]));
    }
    _hidl_cb(retval, devices);
    return Void();
}

Return<Result> Stream::setDevices(const hidl_vec<DeviceAddress>& devices) {
    // FIXME: can the legacy API set multiple device with address ?
    if (devices.size() > 1) {
        return Result::NOT_SUPPORTED;
    }
    DeviceAddress address{};
    if (devices.size() == 1) {
        address = devices[0];
    }
    return setParam(AudioParameter::keyRouting, address);
}

Return<void> Stream::getParameters(const hidl_vec<ParameterValue>& context,
                                   const hidl_vec<hidl_string>& keys, getParameters_cb _hidl_cb) {
    getParametersImpl(context, keys, _hidl_cb);
    return Void();
}

Return<Result> Stream::setParameters(const hidl_vec<ParameterValue>& context,
                                     const hidl_vec<ParameterValue>& parameters) {
    return setParametersImpl(context, parameters);
}

Return<Result>  Stream::start() {
    return Result::NOT_SUPPORTED;
}

Return<Result>  Stream::stop() {
    return Result::NOT_SUPPORTED;
}

Return<void>  Stream::createMmapBuffer(int32_t minSizeFrames __unused,
                                       createMmapBuffer_cb _hidl_cb) {
    Result retval(Result::NOT_SUPPORTED);
    MmapBufferInfo info;
    _hidl_cb(retval, info);
    return Void();
}

Return<void>  Stream::getMmapPosition(getMmapPosition_cb _hidl_cb) {
    Result retval(Result::NOT_SUPPORTED);
    MmapPosition position;
    _hidl_cb(retval, position);
    return Void();
}

Return<Result> Stream::close()  {
    return Result::NOT_SUPPORTED;
}

Return<void> Stream::debug(const hidl_handle& fd, const hidl_vec<hidl_string>& /* options */) {
    if (fd.getNativeHandle() != nullptr && fd->numFds == 1) {
        analyzeStatus("dump", mStream->dump(mStream, fd->data[0]));
    }
    return Void();
}

} // namespace implementation
}  // namespace CPP_VERSION
}  // namespace audio
}  // namespace hardware
}  // namespace android
