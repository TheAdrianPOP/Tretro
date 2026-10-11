#include "frontend.h"





static constexpr int kTargetBuffered = 4096;
static constexpr int kPrimeWatermark = 2940;


void audio_stream_cb(int16_t* out, int frames, void* ud) {
    if (!out || frames <= 0) return;

    auto* ring = static_cast<DynRingBuffer<StereoFrame>*>(ud);
    Frontend& f_ref = Frontend::get();

    static int16_t s_last_l = 0;
    static int16_t s_last_r = 0;
    static bool    s_prebuffering = true;

    int buffered = f_ref.audio_buffered.load(rt_mo::relaxed);

    
    if (s_prebuffering) {
        if (buffered < kPrimeWatermark) {
            for (int i = 0; i < frames; i++) {
                s_last_l = (int16_t)((s_last_l * 7) / 8);
                s_last_r = (int16_t)((s_last_r * 7) / 8);
                out[i * 2 + 0] = s_last_l;
                out[i * 2 + 1] = s_last_r;
            }
            return;
        }
        s_prebuffering = false;
    }

    for (int i = 0; i < frames; i++) {
        StereoFrame f{0, 0};
        if (!ring || !ring->pop(f)) {
            f_ref.audio_underruns.fetch_add(1, rt_mo::relaxed);
            
            s_last_l = (int16_t)((s_last_l * 7) / 8);
            s_last_r = (int16_t)((s_last_r * 7) / 8);
            out[i * 2 + 0] = s_last_l;
            out[i * 2 + 1] = s_last_r;

            
            s_prebuffering = true;
        } else {
            f_ref.audio_buffered.fetch_sub(1, rt_mo::relaxed);
            s_last_l = f.left;
            s_last_r = f.right;
            out[i * 2 + 0] = f.left;
            out[i * 2 + 1] = f.right;
        }
    }
}


size_t audio_sample_batch_cb(const int16_t* data, size_t frames) {
    if (!data || frames == 0) return 0;
    Frontend& f = Frontend::get();

    
    
    
    
    
    float target_fps = (f.target_fps >= 10.0f) ? f.target_fps : 60.0f;
    float actual_fps = (f.perf.fps >= 10.0f)   ? f.perf.fps   : target_fps;
    float speed_ratio = actual_fps / target_fps;

    if (speed_ratio < 0.2f) speed_ratio = 0.2f;
    if (speed_ratio > 3.0f) speed_ratio = 3.0f;

    
    int buffered = f.audio_buffered.load(rt_mo::relaxed);
    float diff = (float)(buffered - kTargetBuffered) / (float)kTargetBuffered;
    if (diff < -1.0f) diff = -1.0f;
    if (diff >  1.0f) diff =  1.0f;
    float micro_skew = diff * 0.025f;

    int steered_src_rate = (int)(f.core_sample_rate * speed_ratio * (1.0f + micro_skew));
    if (steered_src_rate < 4000)   steered_src_rate = 4000;
    if (steered_src_rate > 384000) steered_src_rate = 384000;

    if (steered_src_rate != f.resampler.src_rate) {
        f.resampler.setRates(steered_src_rate, kAudioSampleRate);
    }

    if (f.resampler.src_rate == kAudioSampleRate) {
        for (size_t i = 0; i < frames; i++) {
            if (f.audio_ring.push({data[i * 2], data[i * 2 + 1]})) {
                f.audio_buffered.fetch_add(1, rt_mo::relaxed);
            } else {
                f.audio_overflows.fetch_add(1, rt_mo::relaxed);
            }
        }
        return frames;
    }

    
    constexpr int kMaxBatchOut = 4096;
    int16_t resampled[kMaxBatchOut * 2];

    size_t in_cursor = 0;
    while (in_cursor < frames) {
        int chunk_in = (int)(frames - in_cursor);
        if (chunk_in > 1024) chunk_in = 1024;

        int out_frames = f.resampler.process(
            data + (in_cursor * 2), chunk_in,
            resampled, kMaxBatchOut
        );

        for (int i = 0; i < out_frames; i++) {
            if (f.audio_ring.push({resampled[i * 2], resampled[i * 2 + 1]})) {
                f.audio_buffered.fetch_add(1, rt_mo::relaxed);
            } else {
                f.audio_overflows.fetch_add(1, rt_mo::relaxed);
            }
        }
        in_cursor += chunk_in;
    }

    return frames;
}


static int16_t s_single_batch[64 * 2];
static size_t  s_single_count = 0;

void audio_sample_flush() {
    if (s_single_count > 0) {
        audio_sample_batch_cb(s_single_batch, s_single_count);
        s_single_count = 0;
    }
}

void audio_sample_cb(int16_t left, int16_t right) {
    s_single_batch[s_single_count * 2 + 0] = left;
    s_single_batch[s_single_count * 2 + 1] = right;
    s_single_count++;

    if (s_single_count >= 64) {
        audio_sample_batch_cb(s_single_batch, s_single_count);
        s_single_count = 0;
    }
}