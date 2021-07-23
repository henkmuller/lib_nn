#include <stdio.h>
#include <stdint.h>
#include "pad_3_to_4.h"

/** Function that copies a single pixel of 3 bytes
 * @param outputs  pointer to the outputs array - incremented by 4 bytes
 * @param inputs   pointer to the input array - incremented by 3 bytes
 * @param N_3      number of pixels will be decremented by 1.
 */
static inline void pad_3_to_4_single(int32_t **outputs, int8_t **inputs, uint32_t *N_3) {
    for(uint32_t i = 0; i < 3; i++) {
        (*(int8_t**)outputs)[i] = (*inputs)[i];
    }
    (*(int8_t**)outputs)[3] = 0;
    *inputs += 3;
    *outputs += 1;
    *N_3 -= 1;
}

void pad_3_to_4(int32_t outputs[], int8_t inputs[], uint32_t N_3) {
    // First copy single pixels until the input pointer is aligned
    // That will happen as it is incremented in steps of 3
    // But we may run out of pixels before it happens
    while((((uint32_t)inputs) & 7) != 0 && N_3 != 0) {
        pad_3_to_4_single(&outputs, &inputs, &N_3);
    }
    
    // Now figure out whether the total number of pixels to be copied
    // Is a multiple of 24; if not, remember what the remainder is
    uint32_t tail_N_3 = N_3 & 7;    // remaining blocks of 3
    uint32_t N_24 = N_3 >> 3;       // Blocks of 24

    // Now copy the bulk of the data in blocks of 24
    if (N_24 != 0) {
        pad_3_to_4_asm(outputs, (int64_t *)inputs, N_24);
    }

    // Finally, if there is a remainder, copy them a pixel at a time
    if (tail_N_3 != 0) {
        // Adjust the inputs and outputs pointer to point to the remainder.
        inputs +=  (N_24 << 3) * 3;
        outputs += (N_24 << 3);
        while(tail_N_3 != 0) {
            pad_3_to_4_single(&outputs, &inputs, &tail_N_3);
        }
    }
}
