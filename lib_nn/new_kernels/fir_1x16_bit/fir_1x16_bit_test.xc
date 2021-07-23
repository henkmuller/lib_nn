#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "fir_1x16_bit.h"

/** Variables for the virtual assembler version below
 */
static int16_t macc_coeffs[16] = {
    0x7fff, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100,
    0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001
};

/** Function that transforms N 256 bit constants to a matrix with separate
 * bits. It performs a transpose operation: it converts a vector of 256
 * 16-bit values (a 256 x 16 1-bit matrix) to a 16x256 matrix of 1-bit
 * values. The 1-bit values do not represent precise powers of 2 however,
 * the highest bit value represents +/-32766 (rather than 32768), to cater
 * for the fact that we cannot deal with +32768 as a 16-bit value. This
 * means that all input coefficients must be in the range [-32766..32766]
 *
 * @param outputs array of output words, must be N / 2 words long
 * @param inputs  array of input coefficieints, each must be in the range [-32766..32766]
 * @param N       number of values - must be a multiple of 256 (the vector length)
 */
void boggle_16_to_1_bit(int32_t outputs[], int32_t inputs[], uint32_t N) {
    memset(outputs, 0, N*2);
    int k = 0;
    for(uint32_t i = 0; i < N; i++) {
        int32_t x = inputs[i]*2;
        for(int32_t j = 15; j >= 0; j--) {
            int32_t val = 1 << j;
            if (val == 0x8000) {
                val = 0x7fff;
            }
            if (x >= 0) {
                x -= val;
            } else {
                x += val;
                uint32_t bitnumber = (i&255) + (j&15)*256 + k * 256 * 16;
                uint32_t bytenumber = bitnumber >> 5;
                uint32_t thebit = bitnumber & 31;
                outputs[bytenumber] |= 1 << thebit;
            }
        }
        if (x != 0) {
            printf("%3d %4d %d\n", i, inputs[i], x);
        }
        if ((i & (255)) == 255) {
            k++;
        }
    }
}

/** Function that provides a reference implementation for the 1-bit
 * times 16-bit FIR. No boggling is required.
 */
int32_t fir_1x16_bit_ref(int32_t signal[], int32_t coeff[], uint32_t N) {
    int32_t acc = 0;
    N *= 256;
    for(int j = 0; j < N; j++) {
        uint32_t bitnumber = j;
        uint32_t bytenumber = bitnumber >> 5;
        uint32_t thebit = bitnumber & 31;
        int32_t value = (signal[bytenumber] >> thebit) & 1;
        acc += (1-(value * 2)) * coeff[j];
    }
    return acc;
}

/** Function that provides a virtual assembler version of the 1x16 bit FIR
 * It is exactly like the assembly version, but less efficient and some
 * unsafe nastiness with R11. 
 */
int32_t fir_1x16_bit_virtual_asm(int32_t signal[], int32_t coeff_1[], uint32_t N) {
    int32_t *c = coeff_1;
    int16_t tmp[18];

    asm volatile("ldc r11, 0x100");
    asm volatile("vsetc r11");
    asm volatile("vclrdr");
    for(int i = 0; i < N; i++) {
        asm volatile("vldc %0[0]" :: "r"(signal + i * 8));
        for(int j = 0; j < 16; j++) {
            asm volatile("vlmaccr1 %0[0]" :: "r"(c));
            c += 8;
        }
    }
    asm volatile("vstr %0[0]" :: "r" (tmp));
    asm volatile("vclrdr");
    asm volatile("vldc %0[0]" :: "r"(tmp));
    asm volatile("vlmaccr %0[0]" :: "r"(macc_coeffs));
    asm volatile("vstr %0[0]" :: "r" (tmp));
    asm volatile("vstd %0[0]" :: "r" (tmp+2));
    return tmp[2] << 16 | (tmp[0] & 0xffff);
}

#define MAX_N      5

int32_t coeffs_1[MAX_N * 256 / 2];
int32_t coeff[MAX_N * 256];
int32_t signal[MAX_N * 256 / 32];

int test_one_point(int32_t signal[], int32_t coeff_16[], int N) {
    int fail = 0;
    boggle_16_to_1_bit(coeffs_1, coeff_16, N*256);
    int32_t v0 = fir_1x16_bit_virtual_asm(signal, coeffs_1, N);
    int32_t v1 = fir_1x16_bit(signal, coeffs_1, N);
    int32_t v2 = fir_1x16_bit_ref(signal, coeff_16, N);
    if (v0 != v1 || v1 != v2) {
        printf("%08x %08x %08x\n", v0, v1, v2);
        fail = 1;
    }
    return fail;
}

int main(void) {
    int fail = 0;
    for(uint32_t N = 3; N < MAX_N; N++) {
        for(uint32_t sig = 0; sig < 10; sig++) {
            switch(sig) {
            case 0: memset(signal, 0x00, N * 256 / 8); break;
            case 1: memset(signal, 0xFF, N * 256 / 8); break;
                default:
                    for(int i = 0; i < N*256/32; i++) {
                        signal[i] = rand();
                    }
                    break;
            }
            for(uint32_t c = 0; c < 10; c++) {
                switch(c) {
                case 0:for(int i = 0; i < N*256; i++) coeff[i] = 0x7fff; break;
                case 1:for(int i = 0; i < N*256; i++) coeff[i] = -0x7fff; break;
                case 2:for(int i = 0; i < N*256; i++) coeff[i] = 0; break;
                default:
                    for(int i = 0; i < N*256; i++) {
                        coeff[i] = rand() & 0x7fff;
                        if (rand()&1) coeff[i] = -coeff[i];
                    }
                    break;
                }
                if (test_one_point(signal, coeff, N)) {
                    printf("FAIL\n");
                    fail = 1;
                }
                printf("%d %d %d done\n", N, sig, c);
            }
        }
    }
    if (!fail) {
        printf("PASS\n");
    }
    
    return fail;
}

