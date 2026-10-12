/* Host-side stub reproducing only the raw.h layout consumed by jpeg_enc.c (copied field order from src/raw.h). */
#ifndef HOST_STUB_RAW_H
#define HOST_STUB_RAW_H

struct raw_pixblock
{
    unsigned int b_hi: 2;
    unsigned int a: 14;
    unsigned int c_hi: 4;
    unsigned int b_lo: 12;
    unsigned int d_hi: 6;
    unsigned int c_lo: 10;
    unsigned int e_hi: 8;
    unsigned int d_lo: 8;
    unsigned int f_hi: 10;
    unsigned int e_lo: 6;
    unsigned int g_hi: 12;
    unsigned int f_lo: 4;
    unsigned int h: 14;
    unsigned int g_lo: 2;
} __attribute__((packed, aligned(2)));

struct raw_info
{
    void* buffer;
    int height, width, pitch;
    int black_level;
    int white_level;
    struct { int x, y, width, height; } jpeg;
};

#endif
