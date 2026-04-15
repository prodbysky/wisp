#ifndef DFT_INCLUDED
#define DFT_INCLUDED

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#define DFT_SIZE 512
#define NYQUIST_LIMIT (DFT_SIZE / 2.0)


static float dft_shared_buf[DFT_SIZE];
static size_t dft_shared_buf_head = 0;
static volatile bool dft_shared_buf_ready = false;

// assumes float samples (interleaved stereo)
void fill_dft_buffer_callback(void* samples, uint32_t n_samples) {
    float* s = (float*)samples;

    for (int i = 0; i < n_samples; i++) {
        float l = s[i*2];
        float r = s[i*2+1];
        dft_shared_buf[dft_shared_buf_head++] = fabsf(l) > fabsf(r) ? l : r;
        if (dft_shared_buf_head >= DFT_SIZE) {
            dft_shared_buf_ready = true;
            dft_shared_buf_head = 0;
            return;
        }
    }
}

typedef struct {
    float real;
    float imag;
} Complex;

void compute_dft(float* in, Complex* out, int N) {
    for (int k = 0; k < N; k++) {
        float real = 0.0f;
        float imag = 0.0f;

        for (int n = 0; n < N; n++) {
            float angle = 2.0f * 3.14159265358979323846f * k * n / N;
            real += in[n] * cosf(angle);
            imag -= in[n] * sinf(angle);
        }

        out[k].real = real;
        out[k].imag = imag;
    }
}


#else
#error "Don't include this twice :)"
#endif
