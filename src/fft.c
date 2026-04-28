#include "fft.h"

static float fft_shared_buf[FFT_SIZE];
static size_t fft_shared_buf_head = 0;
static volatile bool fft_shared_buf_ready = false;

static void fft_internal(float _Complex* buf, int n);

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

void compute_fft(float _Complex *out) {
    for (int i = 0; i < FFT_SIZE; i++) {
        out[i] = CMPLX(fft_shared_buf[i], 0);
    }

    fft_internal(out, FFT_SIZE);
}


bool get_fft_ready() {
    return fft_shared_buf_ready;
}

float* get_fft_shared_buf() {
    return fft_shared_buf;
}

void fft_consumed() {
    fft_shared_buf_ready = false;
}



static void fft_internal(float _Complex* buf, int n) {
    if (n <= 1) return;

    int half = n / 2;

    float _Complex even[half];
    float _Complex odd[half];

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

        float _Complex t = CMPLX(
            cos_a * creal(odd[k]) - sin_a * cimag(odd[k]),
            cos_a * cimag(odd[k]) + sin_a * creal(odd[k])
        );

        buf[k] = CMPLX(
            creal(even[k]) + creal(t),
            cimag(even[k]) + cimag(t)
        );
        buf[k + half] = CMPLX(
            creal(even[k]) - creal(t),
            cimag(even[k]) - cimag(t)
        );
    }
}
