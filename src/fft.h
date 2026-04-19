#ifndef FFT_INCLUDED
#define FFT_INCLUDED

#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define FFT_SIZE 8192
#define HOP_SIZE 128
#define NYQUIST_LIMIT (FFT_SIZE / 2.0)

static float fft_shared_buf[FFT_SIZE];
static size_t fft_shared_buf_head = 0;
static volatile bool fft_shared_buf_ready = false;

typedef struct {
    float real;
    float imag;
} Complex;

// assumes float samples (interleaved stereo)
void fill_fft_buffer_callback(void* samples, uint32_t n_samples) {
    float* s = (float*)samples;

    for (uint32_t i = 0; i < n_samples; i++) {
        float l = s[i * 2];
        float r = s[i * 2 + 1];

        float mono = 0.5f * (l + r);

        fft_shared_buf[fft_shared_buf_head++] = mono;

        if (fft_shared_buf_head >= FFT_SIZE) {
            fft_shared_buf_ready = true;

            memmove(fft_shared_buf, fft_shared_buf + HOP_SIZE, (FFT_SIZE - HOP_SIZE) * sizeof(float));

            fft_shared_buf_head = FFT_SIZE - HOP_SIZE;
        }
    }
}
static void fft_internal(Complex* buf, int n) {
    if (n <= 1) return;

    int half = n / 2;

    Complex even[half];
    Complex odd[half];

    for (int i = 0; i < half; i++) {
        even[i] = buf[i * 2];
        odd[i] = buf[i * 2 + 1];
    }

    fft_internal(even, half);
    fft_internal(odd, half);

    for (int k = 0; k < half; k++) {
        float angle = -2.0f * 3.14159265358979323846f * k / n;

        float cos_a = cosf(angle);
        float sin_a = sinf(angle);

        Complex t;
        t.real = cos_a * odd[k].real - sin_a * odd[k].imag;
        t.imag = cos_a * odd[k].imag + sin_a * odd[k].real;

        buf[k].real = even[k].real + t.real;
        buf[k].imag = even[k].imag + t.imag;
        buf[k + half].real = even[k].real - t.real;
        buf[k + half].imag = even[k].imag - t.imag;
    }
}

void compute_fft(float* in, Complex* out, int N) {
    for (int i = 0; i < N; i++) {
        out[i].real = in[i];
        out[i].imag = 0.0f;
    }

    fft_internal(out, N);
}

#else
#error "Don't include this twice :)"
#endif
