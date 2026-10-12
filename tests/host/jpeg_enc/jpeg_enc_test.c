/*
 * Host test driver for modules/silent/jpeg_enc.c.
 *
 * Usage: jpeg_enc_test <in.uyvy> <width> <height> <quality> <out.jpg>
 * Reads a raw UYVY (YUV 4:2:2) frame, encodes it with the production encoder
 * compiled for the host, and writes a baseline JPEG for external decoding.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "jpeg_enc.h"

int main(int argc, char** argv)
{
    if (argc != 6)
    {
        fprintf(stderr, "usage: %s in.uyvy width height quality out.jpg\n", argv[0]);
        return 2;
    }

    int width = atoi(argv[2]);
    int height = atoi(argv[3]);
    int quality = atoi(argv[4]);
    size_t size = (size_t)width * (size_t)height * 2u;

    uint8_t* frame = (uint8_t*)malloc(size);
    if (!frame) return 3;

    FILE* in = fopen(argv[1], "rb");
    if (!in || fread(frame, 1, size, in) != size)
    {
        fprintf(stderr, "failed to read %zu bytes from %s\n", size, argv[1]);
        return 4;
    }
    fclose(in);

    int ok = encode_yuv422_to_jpeg(frame, width, height, width * 2, quality, NULL, argv[5]);
    free(frame);
    return ok ? 0 : 1;
}
