#ifndef FFT_INCLUDED
#define FFT_INCLUDED

#include <complex.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FFT_SIZE 8192
#define HOP_SIZE 128
#define NYQUIST_LIMIT (FFT_SIZE / 2.0)

// assumes float samples (interleaved stereo)
void fill_fft_buffer_callback(void* samples, uint32_t n_samples);
void compute_fft(float _Complex* out);
bool get_fft_ready();
void fft_consumed();

float* get_fft_shared_buf();

#endif
