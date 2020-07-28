#include "pch.h"
#include "CodecUtil.h"
#include "BochanEncoder.h"

bochan::BochanEncoder::BochanEncoder(BufferPool* bufferPool) : AudioEncoder(bufferPool) {}

bochan::BochanEncoder::~BochanEncoder() {
    deinitialize();
}

bool bochan::BochanEncoder::initialize(BochanCodec bochanCodec, int sampleRate, unsigned long long bitRate) {
    if (initialized) {
        deinitialize();
    }
    BOCHAN_DEBUG("Encoding with codec '{}' at {} SR, {} BPS...", bochanCodec, sampleRate, bitRate);
    this->bochanCodec = bochanCodec;
    this->sampleRate = sampleRate;
    this->bitRate = bitRate;
    this->sampleFormat = CodecUtil::getCodecSampleFormat(bochanCodec);
    codecId = CodecUtil::getCodecId(bochanCodec);
    if (codecId == AV_CODEC_ID_NONE) {
        BOCHAN_ERROR("Failed to get codec ID for codec '{}'!", bochanCodec);
        deinitialize();
        return false;
    }
    BOCHAN_DEBUG("Using codec ID '{}'...", codecId);
    codec = avcodec_find_encoder(codecId);
    if (!codec) {
        BOCHAN_ERROR("Failed to get encoder for codec ID '{}'!", codecId);
        deinitialize();
        return false;
    }
    BOCHAN_DEBUG("Using encoder '{}'...", codec->long_name);
    if (!CodecUtil::isSampleRateSupported(codec, sampleRate)) {
        BOCHAN_ERROR("Sample rate {} is not supported by this codec!", sampleRate);
        deinitialize();
        return false;
    }
    if (!CodecUtil::isFormatSupported(codec, sampleFormat)) {
        BOCHAN_ERROR("Format '{}' is not supported by this codec!", sampleFormat);
        deinitialize();
        return false;
    }
    context = avcodec_alloc_context3(codec);
    if (!context) {
        BOCHAN_ERROR("Failed to allocate context!");
        deinitialize();
        return false;
    }
    context->sample_fmt = sampleFormat;
    context->bit_rate = bitRate;
    context->sample_rate = sampleRate;
    context->channel_layout = CodecUtil::CHANNEL_LAYOUT;
    context->channels = CodecUtil::CHANNELS;
    if (int ret = avcodec_open2(context, codec, nullptr); ret < 0) {
        char err[ERROR_BUFF_SIZE] = { 0 };
        av_strerror(ret, err, ERROR_BUFF_SIZE);
        BOCHAN_ERROR("Failed to open codec: {}", err);
        deinitialize();
        return false;
    }
    if (!context->frame_size) {
        context->frame_size = CodecUtil::DEFAULT_FRAMESIZE;
        BOCHAN_DEBUG("Unrestricted frame size, setting to {}.", CodecUtil::DEFAULT_FRAMESIZE);
    }
    packet = av_packet_alloc();
    if (!packet) {
        BOCHAN_ERROR("Failed to allocate packet!");
        deinitialize();
        return false;
    }
    frame = av_frame_alloc();
    if (!frame) {
        BOCHAN_ERROR("Failed to allocate frame!");
        deinitialize();
        return false;
    }
    frame->nb_samples = context->frame_size;
    frame->format = context->sample_fmt;
    frame->channel_layout = context->channel_layout;
    frame->channels = context->channels;
    if (int ret = av_frame_get_buffer(frame, 0); ret < 0) {
        char err[ERROR_BUFF_SIZE] = { 0 };
        av_strerror(ret, err, ERROR_BUFF_SIZE);
        BOCHAN_ERROR("Failed to allocate frame buffer: {}", err);
        deinitialize();
        return false;
    }
    bytesPerSample = av_get_bytes_per_sample(context->sample_fmt);
    CodecUtil::printDebugInfo(context);
    initialized = true;
    return true;
}

void bochan::BochanEncoder::deinitialize() {
    BOCHAN_DEBUG("Deinitializing encoder...");
    initialized = false;
    if (frame) {
        av_frame_free(&frame);
    }
    if (packet) {
        av_packet_free(&packet);
    }
    if (context) {
        avcodec_free_context(&context);
    }
    sampleFormat = AVSampleFormat::AV_SAMPLE_FMT_NONE;
    bytesPerSample = 0;
    codec = nullptr;
    codecId = AVCodecID::AV_CODEC_ID_NONE;
    bochanCodec = BochanCodec::None;
    sampleRate = 0;
    bitRate = 0ULL;
}

bool bochan::BochanEncoder::isInitialized() const {
    return initialized;
}

bochan::BochanCodec bochan::BochanEncoder::getCodec() const {
    return bochanCodec;
}

int bochan::BochanEncoder::getSampleRate() const {
    return sampleRate;
}

unsigned long long bochan::BochanEncoder::getBitRate() const {
    return bitRate;
}

int bochan::BochanEncoder::getSamplesPerFrame() const {
    return initialized ? context->frame_size : 0;
}

int bochan::BochanEncoder::getInputBufferByteSize() const {
    return initialized ? context->frame_size * sizeof(uint16_t) * context->channels : 0;
}

bool bochan::BochanEncoder::hasExtradata() {
    return initialized && context->extradata != nullptr;
}

bochan::ByteBuffer* bochan::BochanEncoder::getExtradata() {
    if (!hasExtradata()) {
        return nullptr;
    }
    ByteBuffer* result = bufferPool->getBuffer(context->extradata_size);
    memcpy(result->getPointer(), context->extradata, context->extradata_size);
    return result;
}

std::vector<bochan::ByteBuffer*> bochan::BochanEncoder::encode(ByteBuffer* samples) {
    assert(samples->getUsedSize() == getInputBufferByteSize());
    if (int ret = av_frame_make_writable(frame); ret < 0) {
        char err[ERROR_BUFF_SIZE] = { 0 };
        av_strerror(ret, err, ERROR_BUFF_SIZE);
        BOCHAN_ERROR("Failed to ensure writable frame: {}", err);
        return {};
    }
    size_t expectedSamples = static_cast<size_t>(frame->nb_samples * frame->channels);
    size_t providedSamples = samples->getUsedSize() / sizeof(uint16_t);
    if (providedSamples != expectedSamples) {
        BOCHAN_ERROR("Failed to encode audio frame! Expected {} samples, got {}.",
                     expectedSamples, providedSamples);
        return {};
    }
    switch (frame->format) {
        case AVSampleFormat::AV_SAMPLE_FMT_S16P:
        {
            uint16_t* uint16ptr = reinterpret_cast<uint16_t*>(samples->getPointer());
            for (int i = 0; i < frame->nb_samples; ++i) {
                for (int j = 0; j < frame->channels; ++j) {
                    reinterpret_cast<uint16_t*>(frame->data[j])[i] = uint16ptr[i * frame->channels + j];
                }
            }
            break;
        }
        case AVSampleFormat::AV_SAMPLE_FMT_S16:
        {
            memcpy(frame->data[0], samples, samples->getUsedSize());
            break;
        }
        case AVSampleFormat::AV_SAMPLE_FMT_FLTP:
        {
            int16_t* int16ptr = reinterpret_cast<int16_t*>(samples->getPointer());
            for (int i = 0; i < frame->nb_samples; ++i) {
                for (int j = 0; j < frame->channels; ++j) {
                    reinterpret_cast<float*>(frame->data[j])[i] = CodecUtil::int16ToFloat(int16ptr[i * frame->channels + j]);
                }
            }
            break;
        }
        case AVSampleFormat::AV_SAMPLE_FMT_FLT:
        {
            CodecUtil::int16ToFloat(samples, reinterpret_cast<float*>(frame->data[0]));
            break;
        }
        default:
        {
            BOCHAN_ERROR("Encountered unsupported decoder format {}!", context->sample_fmt);
            return {};
        }
    }
    if (int ret = avcodec_send_frame(context, frame); ret < 0) {
        char err[ERROR_BUFF_SIZE] = { 0 };
        av_strerror(ret, err, ERROR_BUFF_SIZE);
        BOCHAN_ERROR("Failed to send frame to encoder: {}", err);
        return {};
    }
    std::vector<ByteBuffer*> result;
    while (true) {
        int ret = avcodec_receive_packet(context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            char err[ERROR_BUFF_SIZE] = { 0 };
            av_strerror(ret, err, ERROR_BUFF_SIZE);
            BOCHAN_ERROR("Failed to encode audio frame: {}", err);
            return {};
        }
        ByteBuffer* buff = bufferPool->getBuffer(packet->size);
        memcpy(buff->getPointer(), packet->data, packet->size);
        result.push_back(buff);
    }
    return result;
}
