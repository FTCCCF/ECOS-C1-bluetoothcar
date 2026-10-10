#include "voice.h"

/* Explicit nearest/ties-even, including negative values; no implementation-
 * dependent signed right shift is needed for the rounding operation. */
int32_t voice_round_shift(int64_t value, unsigned bits) {
    uint64_t magnitude = value < 0 ? (uint64_t)(-(value+1))+1 : (uint64_t)value;
    uint64_t integer = magnitude >> bits;
    uint64_t remainder = magnitude & (((uint64_t)1 << bits)-1);
    uint64_t half = (uint64_t)1 << (bits-1);
    if (remainder > half || (remainder == half && (integer & 1))) ++integer;
    return value < 0 ? -(int32_t)integer : (int32_t)integer;
}
