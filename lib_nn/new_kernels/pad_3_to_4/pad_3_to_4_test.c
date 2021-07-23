#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pad_3_to_4.h"

#define MAXPIXELS 512
#define MAXOFFSET 8

static int32_t outputs[MAXPIXELS ];
static int32_t ref_outputs[MAXPIXELS];
static int8_t inputs[MAXPIXELS * 3 + MAXOFFSET];

static void memsetrandom(int8_t *data, uint32_t N) {
    for(int i = 0; i < N; i++) {
        *data++ = rand();
    }
}

void reference(int8_t *outputs, int8_t *inputs, int N_3) {
    for(int i = 0; i < N_3; i++) {
        for(int j = 0; j < 3; j++) {
            *outputs++ = *inputs++;
        }
        *outputs++ = 0;
    }
}

int main(void) {
    for(int N_3 = 1; N_3 < MAXPIXELS; N_3++) {
        for(int offset = 0; offset < MAXOFFSET; offset++) {
            memsetrandom((int8_t *)outputs, N_3*4);
            memsetrandom((int8_t *)ref_outputs, N_3*4);
            memsetrandom((int8_t *)inputs, N_3*3 + MAXOFFSET);
            reference((int8_t *)ref_outputs, inputs + offset, N_3);
            pad_3_to_4(outputs, inputs + offset, N_3);
            if (memcmp(outputs, ref_outputs, N_3*4) != 0) {
                printf("ERROR\n");
                exit(1);
            }
        }
        if (N_3 > 24) {
            N_3 += 16;
        }
        printf("%3d done\n", N_3);
    }
    return 0;
}
