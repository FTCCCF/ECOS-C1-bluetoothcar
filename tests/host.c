#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voice/voice.h"

int main(int argc, char **argv) {
    if (argc!=3) return 2;
    FILE *source=fopen(argv[1], "rb"), *output=fopen(argv[2], "wb");
    if (!source || !output) return 3;
    char magic[8]; uint32_t count;
    if (fread(magic, 1, 8, source)!=8 || memcmp(magic, "VOICE001", 8) || fread(&count, 4, 1, source)!=1) return 4;
    for (unsigned i=0; i<count; ++i) {
        uint32_t header[3];
        if (fread(header, 4, 3, source)!=3 || header[0]>100000) return 5;
        int16_t *pcm=malloc(header[0]*sizeof(*pcm));
        if (!pcm || fread(pcm, 2, header[0], source)!=header[0]) return 6;
        int8_t input[VOICE_INPUTS]; int32_t mfcc[VOICE_INPUTS]; voice_result_t result;
        if (voice_frontend(pcm, header[0], header[1], header[2], input, mfcc)) {
            fprintf(stderr, "Frontend rejected row %u: samples=%u rate_q16=%u start=%u\n", i, header[0], header[1], header[2]);
            return 7;
        }
        voice_infer(input, &result);
        if (fwrite(input, 1, VOICE_INPUTS, output)!=VOICE_INPUTS ||
            fwrite(mfcc, 4, VOICE_INPUTS, output)!=VOICE_INPUTS ||
            fwrite(result.logits_q16, 4, 5, output)!=5 ||
            fwrite(result.probability_q24, 4, 5, output)!=5 ||
            fwrite(&result.class_id, sizeof(int), 1, output)!=1 ||
            fwrite(&result.decision, sizeof(int), 1, output)!=1) return 8;
        free(pcm);
    }
    fclose(source); fclose(output); return 0;
}
