#pragma once
#ifndef RESAMPLER_H
#define RESAMPLER_H

#include "engine_config.h"

struct HermiteResampler {
    int   src_rate = 44100;
    int   dst_rate = kAudioSampleRate; 
    float pos      = 0.f;

    int16_t history[2] = {0, 0};

    void setRates(int src, int dst) {
        src_rate = src > 0 ? src : 44100;
        dst_rate = dst > 0 ? dst : 44100;
        pos = 0.f;
        memset(history, 0, sizeof(history));
    }

    static inline int16_t cubic_hermite(float A, float B, float C, float D, float t) {
        float a = -0.5f * A + 1.5f * B - 1.5f * C + 0.5f * D;
        float b = A - 2.5f * B + 2.0f * C - 0.5f * D;
        float c = -0.5f * A + 0.5f * C;
        
        float out = a * t * t * t + b * t * t + c * t + B;
        
        if (out < -32768.0f) return -32768;
        if (out > 32767.0f) return 32767;
        return (int16_t)out;
    }

    int process(const int16_t* in,  int in_frames,
                      int16_t* out, int out_capacity)
    {
        if (!in || !out || in_frames <= 0 || out_capacity <= 0)
            return 0;

        int s_rate = src_rate > 0 ? src_rate : 44100;
        int d_rate = dst_rate > 0 ? dst_rate : 44100;

        if (s_rate == d_rate) {
            int n = fast_min_t(in_frames, out_capacity);
            memcpy(out, in, (size_t)n * sizeof(int16_t) * 2);
            return n;
        }

        const float step = (float)s_rate / (float)d_rate;
        int out_frames = 0;

        while (out_frames < out_capacity) {
            int   i0 = (int)pos;
            float t  = pos - (float)i0;

            if (i0 >= in_frames) break;

            int16_t s[4][2]; 
            for (int k = -1; k <= 2; k++) {
                int idx = i0 + k;
                if (idx < 0) {
                    s[k+1][0] = history[0];
                    s[k+1][1] = history[1];
                } else if (idx >= in_frames) {
                    s[k+1][0] = in[(in_frames - 1) * 2 + 0];
                    s[k+1][1] = in[(in_frames - 1) * 2 + 1];
                } else {
                    s[k+1][0] = in[idx * 2 + 0];
                    s[k+1][1] = in[idx * 2 + 1];
                }
            }

            out[out_frames * 2 + 0] = cubic_hermite(s[0][0], s[1][0], s[2][0], s[3][0], t);
            out[out_frames * 2 + 1] = cubic_hermite(s[0][1], s[1][1], s[2][1], s[3][1], t);

            pos += step;
            out_frames++;
        }

        if (in_frames > 0) {
            history[0] = in[(in_frames - 1) * 2 + 0];
            history[1] = in[(in_frames - 1) * 2 + 1];
        }

        pos -= (float)(int)pos;
        return out_frames;
    }
};

#endif