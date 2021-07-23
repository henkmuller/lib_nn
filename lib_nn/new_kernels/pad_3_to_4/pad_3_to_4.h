#ifndef _PAD_3_TO_4_H
#define _PAD_3_TO_4_H

#include <stdint.h>

/** Function that pads an image with 3-byte values with a 0.
 * The output image must be word aligned. This function solves the general
 * case and calls an optimised assembly version for the bulk copy.
 *
 * @param    outputs    output values, every word contains 3 bytes and a zero
 * @param    inputs     input values, RGBRGBRGBRGB...
 * @param    N_3        number of blocks of 3 bytes to copy
 *
 * @returns  The inner product
 */
extern void pad_3_to_4(int32_t outputs[], int8_t inputs[], uint32_t N_24);

/** Function that pads an image with 3-byte values with a 0.
 * This functions is highly optimised, but has constraints on the
 * alignment of the input image and on the number of bytes to be copied
 * Use ``pad_3_to_4`` to not have any constraints.
 *
 * The input image must be double word aligned.
 * The output image must be word aligned.
 * It copies the image in chunks of 24 bytes
 *
 * @param    outputs    output values, every word contains 3 bytes and a zero
 * @param    inputs     input values, RGBRGBRGBRGB...
 * @param    N_24       number of blocks of 24 bytes to copy
 *
 * @returns  The inner product
 */
extern void pad_3_to_4_asm(int32_t outputs[], int64_t inputs[], uint32_t N_24);

#endif
