/**
 * Ultra-lightweight, Zero-Allocation YUV422 (UYVY) & Raw Full-Res Baseline JPEG Encoder
 * Designed specifically for DIGIC 4 (Canon EOS 5D Mark II).
 *
 * Guarantees:
 * - ZERO heap allocation (no malloc/free, no memory fragmentation).
 * - ZERO stack overflow risk (all working matrices and buffers in static BSS).
 * - Direct streaming I/O in 4KB chunks via FIO_WriteFile.
 * - Integer-only AAN / LLM 2D Forward DCT.
 * - Streaming 8-line strip debayer & film simulation pipeline (< 100 KB RAM).
 */

#include <dryos.h>
#include <stdint.h>
#include <string.h>
#include <bmp.h>
#include <raw.h>
#include "jpeg_enc.h"

// Standard JPEG Quantization Tables (50% quality base)
static const uint8_t default_qtable_y[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99
};

static const uint8_t default_qtable_c[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

static const uint8_t zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

// Standard Huffman Tables (DC and AC for Luminance and Chrominance)
static const uint8_t dc_lum_bits[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t dc_lum_val[]    = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static const uint8_t dc_chrom_bits[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const uint8_t dc_chrom_val[]    = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static const uint8_t ac_lum_bits[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const uint8_t ac_lum_val[]    = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
};

static const uint8_t ac_chrom_bits[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const uint8_t ac_chrom_val[]    = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
};

// Static BSS Lookup & Workspace structures
typedef struct {
    uint16_t code;
    uint8_t  len;
} huff_code_t;

static uint16_t qtab_y[64];
static uint16_t qtab_c[64];

/*
 * Inverse of zigzag[]: maps a natural (row-major) coefficient index to its
 * position in the zigzag-ordered entropy stream.
 */
static const uint8_t natural_to_zigzag[64] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

/*
 * Quantisation reciprocals in natural order, Q(RECIP_BITS) fixed point.
 * Each entry folds the quantiser step, the AAN output scale
 * (8 * aan[row] * aan[col]) and the FDCT input pre-scale (1 << FDCT_PASS_BITS),
 * so the hot loop needs one 32x32->64 multiply instead of a division.
 */
#define FDCT_PASS_BITS 2
#define RECIP_BITS 24
#define AC_COEF_LIMIT 1023
#define DC_COEF_LIMIT 2047
static int32_t recip_y[64];
static int32_t recip_c[64];
static huff_code_t huff_dc_lum[16];
static huff_code_t huff_dc_chrom[16];
static huff_code_t huff_ac_lum[256];
static huff_code_t huff_ac_chrom[256];

#define OUT_BUF_SIZE 4096
static uint8_t out_buf[OUT_BUF_SIZE];
static uint32_t out_buf_pos = 0;
static FILE* out_file = NULL;

static uint32_t bit_buffer = 0;
static int bit_count = 0;

// Working blocks in BSS (eliminates stack consumption!)
static int32_t dct_workspace[64];
static int16_t y0_block[64];
static int16_t y1_block[64];
static int16_t u_block[64];
static int16_t v_block[64];
static int16_t q_block[64];

// Strip buffer for Full-Res debayering: 8 lines x 5632 pixels x 2 bytes = 90,112 bytes
#define STRIP_MAX_WIDTH 5632
#define STRIP_LINES 8
static uint8_t strip_buf[STRIP_MAX_WIDTH * STRIP_LINES * 2] __attribute__((aligned(4)));

// Static Exif APP1 Payload Buffer (512 bytes in BSS, zero heap/stack overhead)
static uint8_t exif_payload[512] __attribute__((aligned(4)));

// Fast Gamma / White Balance LUTs for Raw Debayering (3 KB in BSS)
static uint8_t lut_r[1024];
static uint8_t lut_g[1024];
static uint8_t lut_b[1024];

#ifndef PA
#define PA ((int)(p->a))
#define PB ((int)(p->b_lo | (p->b_hi << 12)))
#define PC ((int)(p->c_lo | (p->c_hi << 10)))
#define PD ((int)(p->d_lo | (p->d_hi << 8)))
#define PE ((int)(p->e_lo | (p->e_hi << 6)))
#define PF ((int)(p->f_lo | (p->f_hi << 4)))
#define PG ((int)(p->g_lo | (p->g_hi << 2)))
#define PH ((int)(p->h))
#endif

#ifndef QA
#define QA ((int)(q->a))
#define QB ((int)(q->b_lo | (q->b_hi << 12)))
#define QC ((int)(q->c_lo | (q->c_hi << 10)))
#define QD ((int)(q->d_lo | (q->d_hi << 8)))
#define QE ((int)(q->e_lo | (q->e_hi << 6)))
#define QF ((int)(q->f_lo | (q->f_hi << 4)))
#define QG ((int)(q->g_lo | (q->g_hi << 2)))
#define QH ((int)(q->h))
#endif

static inline uint8_t clamp_u8(int32_t val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

static void flush_out_buf() {
    if (out_buf_pos > 0 && out_file) {
        FIO_WriteFile(out_file, out_buf, out_buf_pos);
        out_buf_pos = 0;
    }
}

static inline void write_byte(uint8_t b) {
    if (out_buf_pos >= OUT_BUF_SIZE) {
        flush_out_buf();
    }
    out_buf[out_buf_pos++] = b;
}

static void write_bits(uint32_t code, int len) {
    bit_buffer |= (code & ((1 << len) - 1)) << (32 - bit_count - len);
    bit_count += len;
    while (bit_count >= 8) {
        uint8_t b = (uint8_t)(bit_buffer >> 24);
        write_byte(b);
        if (b == 0xFF) {
            write_byte(0x00); // Byte stuffing
        }
        bit_buffer <<= 8;
        bit_count -= 8;
    }
}

static void flush_bits() {
    if (bit_count > 0) {
        uint8_t b = (uint8_t)(bit_buffer >> 24);
        write_byte(b);
        if (b == 0xFF) {
            write_byte(0x00);
        }
        bit_buffer = 0;
        bit_count = 0;
    }
    flush_out_buf();
}

static void build_huff_table(const uint8_t* bits, const uint8_t* val, int val_count, huff_code_t* table) {
    uint16_t code = 0;
    int k = 0;
    for (int len = 1; len <= 16; len++) {
        for (int i = 0; i < bits[len - 1]; i++) {
            if (k < val_count) {
                uint8_t v = val[k++];
                table[v].code = code;
                table[v].len = len;
                code++;
            }
        }
        code <<= 1;
    }
}

static void build_recip_table(const uint16_t* qtab, int32_t* recip) {
    /* AAN output scale factors: aan[0] = 1, aan[k] = sqrt(2) * cos(k * pi / 16). */
    static const float aan_scale[8] = {
        1.0f, 1.387039845f, 1.306562965f, 1.175875602f,
        1.0f, 0.785694958f, 0.541196100f, 0.275899379f
    };
    const float pre_scale = (float)(8 << FDCT_PASS_BITS);
    const float one = (float)(1 << RECIP_BITS);

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            int n = row * 8 + col;
            float divisor = (float)qtab[n] * aan_scale[row] * aan_scale[col] * pre_scale;
            recip[n] = (int32_t)(one / divisor + 0.5f);
        }
    }
}

static void init_tables(int quality) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    int scale = (quality < 50) ? (5000 / quality) : (200 - quality * 2);

    for (int i = 0; i < 64; i++) {
        int v_y = (default_qtable_y[i] * scale + 50) / 100;
        if (v_y < 1) v_y = 1;
        if (v_y > 255) v_y = 255;
        qtab_y[i] = v_y;

        int v_c = (default_qtable_c[i] * scale + 50) / 100;
        if (v_c < 1) v_c = 1;
        if (v_c > 255) v_c = 255;
        qtab_c[i] = v_c;
    }

    build_recip_table(qtab_y, recip_y);
    build_recip_table(qtab_c, recip_c);

    memset(huff_dc_lum, 0, sizeof(huff_dc_lum));
    memset(huff_dc_chrom, 0, sizeof(huff_dc_chrom));
    memset(huff_ac_lum, 0, sizeof(huff_ac_lum));
    memset(huff_ac_chrom, 0, sizeof(huff_ac_chrom));

    build_huff_table(dc_lum_bits, dc_lum_val, sizeof(dc_lum_val), huff_dc_lum);
    build_huff_table(dc_chrom_bits, dc_chrom_val, sizeof(dc_chrom_val), huff_dc_chrom);
    build_huff_table(ac_lum_bits, ac_lum_val, sizeof(ac_lum_val), huff_ac_lum);
    build_huff_table(ac_chrom_bits, ac_chrom_val, sizeof(ac_chrom_val), huff_ac_chrom);
}

// Fixed-point Integer 1D FDCT (Chen / LLM 8-point algorithm)
#define CONST_BITS 13
#define FIX(x) ((int32_t)((x) * (1 << CONST_BITS) + 0.5f))

static void fdct_1d(int32_t* data) {
    int32_t tmp0 = data[0] + data[7];
    int32_t tmp7 = data[0] - data[7];
    int32_t tmp1 = data[1] + data[6];
    int32_t tmp6 = data[1] - data[6];
    int32_t tmp2 = data[2] + data[5];
    int32_t tmp5 = data[2] - data[5];
    int32_t tmp3 = data[3] + data[4];
    int32_t tmp4 = data[3] - data[4];

    // Even part
    int32_t tmp10 = tmp0 + tmp3;
    int32_t tmp13 = tmp0 - tmp3;
    int32_t tmp11 = tmp1 + tmp2;
    int32_t tmp12 = tmp1 - tmp2;

    data[0] = (tmp10 + tmp11);
    data[4] = (tmp10 - tmp11);

    int32_t z1 = (tmp12 + tmp13) * FIX(0.707106781f) >> CONST_BITS;
    data[2] = (tmp13 + z1);
    data[6] = (tmp13 - z1);

    // Odd part
    tmp10 = tmp4 + tmp5;
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;

    int32_t z5 = (tmp10 - tmp12) * FIX(0.382683432f) >> CONST_BITS;
    int32_t z2 = (tmp10 * FIX(0.541196100f) >> CONST_BITS) + z5;
    int32_t z4 = (tmp12 * FIX(1.306562965f) >> CONST_BITS) + z5;
    int32_t z3 = tmp11 * FIX(0.707106781f) >> CONST_BITS;

    int32_t z11 = tmp7 + z3;
    int32_t z13 = tmp7 - z3;

    data[5] = (z13 + z2);
    data[3] = (z13 - z2);
    data[1] = (z11 + z4);
    data[7] = (z11 - z4);
}

static inline int16_t quantize_coef(int32_t coef, int32_t recip, int limit) {
    int32_t mag = (coef < 0) ? -coef : coef;
    int32_t q = (int32_t)(((int64_t)mag * recip + (1 << (RECIP_BITS - 1))) >> RECIP_BITS);
    if (q > limit) q = limit;
    return (int16_t)((coef < 0) ? -q : q);
}

// 2D AAN FDCT on an 8x8 block, quantised and written in zigzag order.
static void fdct_2d(const int16_t* in, int16_t* out, const int32_t* recip) {
    // Rows (input pre-scaled to keep precision through the fixed-point butterflies)
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            dct_workspace[i * 8 + j] = (int32_t)in[i * 8 + j] << FDCT_PASS_BITS;
        }
        fdct_1d(&dct_workspace[i * 8]);
    }

    // Columns
    for (int j = 0; j < 8; j++) {
        int32_t col[8];
        for (int i = 0; i < 8; i++) col[i] = dct_workspace[i * 8 + j];
        fdct_1d(col);
        for (int i = 0; i < 8; i++) {
            int n = i * 8 + j;
            int limit = (n == 0) ? DC_COEF_LIMIT : AC_COEF_LIMIT;
            out[natural_to_zigzag[n]] = quantize_coef(col[i], recip[n], limit);
        }
    }
}

// Huffman encode 8x8 quantized block
static void encode_block(int16_t* block, int16_t* prev_dc, const huff_code_t* dc_tab, const huff_code_t* ac_tab) {
    // DC
    int diff = block[0] - *prev_dc;
    *prev_dc = block[0];

    int abs_diff = (diff < 0) ? -diff : diff;
    int cat = 0;
    while (abs_diff) { cat++; abs_diff >>= 1; }

    write_bits(dc_tab[cat].code, dc_tab[cat].len);
    if (cat > 0) {
        if (diff < 0) diff += (1 << cat) - 1;
        write_bits(diff, cat);
    }

    // AC
    int r = 0;
    for (int k = 1; k < 64; k++) {
        int val = block[k];
        if (val == 0) {
            r++;
        } else {
            while (r > 15) {
                write_bits(ac_tab[0xF0].code, ac_tab[0xF0].len); // ZRL
                r -= 16;
            }
            int abs_v = (val < 0) ? -val : val;
            cat = 0;
            while (abs_v) { cat++; abs_v >>= 1; }

            uint8_t sym = (r << 4) | cat;
            write_bits(ac_tab[sym].code, ac_tab[sym].len);
            if (val < 0) val += (1 << cat) - 1;
            write_bits(val, cat);
            r = 0;
        }
    }
    if (r > 0) {
        write_bits(ac_tab[0x00].code, ac_tab[0x00].len); // EOB
    }
}

// Fast, Zero-Allocation 16-bit Galois LFSR for Analog Film Grain Injection
static uint32_t grain_lfsr = 0xACE1u;

static inline int32_t next_film_grain(int strength)
{
    if (strength <= 0) return 0;
    // Galois LFSR 16-bit polynomial: taps at 16, 14, 13, 11 (0xB400)
    grain_lfsr = (grain_lfsr >> 1) ^ (-(grain_lfsr & 1u) & 0xB400u);
    int32_t noise = (int32_t)(grain_lfsr & 0x0F) - 7; // range -7 to +8
    switch (strength)
    {
        case 1: return (noise >> 2); // subtle (+- 2)
        case 2: return (noise >> 1); // medium (+- 4)
        case 3: return noise;        // heavy (+- 8)
        default: return 0;
    }
}

static inline void apply_film_stock(uint32_t preset, uint8_t* py0, uint8_t* py1, uint8_t* pu, uint8_t* pv, int grain_strength)
{
    int32_t y0 = *py0;
    int32_t y1 = *py1;
    int32_t u  = *pu;
    int32_t v  = *pv;

    switch (preset)
    {
        case 1: // Kodak Portra 400: Soft highlights, lifted shadows, warm skin tones
            y0 = clamp_u8(16 + ((y0 * 240) >> 8));
            y1 = clamp_u8(16 + ((y1 * 240) >> 8));
            u  = clamp_u8(128 + (((u - 128) * 235) >> 8));
            v  = clamp_u8(128 + (((v - 128) * 287) >> 8));
            break;

        case 2: // Fuji Velvia 50: Vivid high-contrast S-curve, punchy greens and blues
            y0 = (y0 < 128) ? clamp_u8((y0 * y0) >> 7) : clamp_u8(255 - (((255 - y0) * (255 - y0)) >> 7));
            y1 = (y1 < 128) ? clamp_u8((y1 * y1) >> 7) : clamp_u8(255 - (((255 - y1) * (255 - y1)) >> 7));
            u  = clamp_u8(128 + (((u - 128) * 358) >> 8));
            v  = clamp_u8(128 + (((v - 128) * 346) >> 8));
            break;

        case 3: // Cinestill 800T: Tungsten cool shadows, warm red halation
            u  = clamp_u8(128 + (((u - 128) * 320) >> 8));
            v  = clamp_u8(128 + (((v - 128) * 230) >> 8));
            if (y0 > 185) v = clamp_u8(v + ((y0 - 185) >> 2));
            if (y1 > 185) v = clamp_u8(v + ((y1 - 185) >> 2));
            break;

        case 4: // Kodak Tri-X 400: Punchy high-contrast monochrome
            y0 = (y0 < 128) ? clamp_u8((y0 * 220) >> 8) : clamp_u8(110 + (((y0 - 128) * 290) >> 8));
            y1 = (y1 < 128) ? clamp_u8((y1 * 220) >> 8) : clamp_u8(110 + (((y1 - 128) * 290) >> 8));
            u  = 128;
            v  = 128;
            break;

        case 5: // Classic Chrome: Muted documentary colors, hard shadow gradations
            if (y0 < 64) y0 = clamp_u8((y0 * 220) >> 8);
            if (y1 < 64) y1 = clamp_u8((y1 * 220) >> 8);
            u  = clamp_u8(128 + (((u - 128) * 192) >> 8));
            v  = clamp_u8(128 + (((v - 128) * 192) >> 8));
            break;

        default:
            break;
    }

    if (grain_strength > 0)
    {
        y0 = clamp_u8(y0 + next_film_grain(grain_strength));
        y1 = clamp_u8(y1 + next_film_grain(grain_strength));
    }

    *py0 = (uint8_t)y0;
    *py1 = (uint8_t)y1;
    *pu  = (uint8_t)u;
    *pv  = (uint8_t)v;
}

static inline void apply_recipe_curve(
    int32_t * py0, int32_t * py1, int32_t * pu, int32_t * pv,
    int highlight, int shadow, int saturation, int wb_ab, int wb_gm
)
{
    int32_t y0 = *py0;
    int32_t y1 = *py1;
    int32_t u  = *pu;
    int32_t v  = *pv;

    if (highlight != 0)
    {
        if (y0 > 128) {
            int32_t dy = y0 - 128;
            y0 = clamp_u8(128 + ((dy * (256 + highlight * 28)) >> 8));
        }
        if (y1 > 128) {
            int32_t dy = y1 - 128;
            y1 = clamp_u8(128 + ((dy * (256 + highlight * 28)) >> 8));
        }
    }

    if (shadow != 0)
    {
        if (y0 < 128) {
            if (shadow > 0) y0 = clamp_u8(y0 + shadow * 6);
            else y0 = clamp_u8((y0 * (256 + shadow * 36)) >> 8);
        }
        if (y1 < 128) {
            if (shadow > 0) y1 = clamp_u8(y1 + shadow * 6);
            else y1 = clamp_u8((y1 * (256 + shadow * 36)) >> 8);
        }
    }

    if (saturation != 0)
    {
        int32_t sat_mult = 256 + saturation * 44;
        u = clamp_u8(128 + (((u - 128) * sat_mult) >> 8));
        v = clamp_u8(128 + (((v - 128) * sat_mult) >> 8));
    }

    if (wb_ab != 0)
    {
        u = clamp_u8(u - wb_ab * 2);
        v = clamp_u8(v + wb_ab * 2);
    }

    if (wb_gm != 0)
    {
        u = clamp_u8(u - wb_gm * 2);
        v = clamp_u8(v - wb_gm * 2);
    }

    *py0 = y0;
    *py1 = y1;
    *pu  = u;
    *pv  = v;
}

void apply_film_recipe_yuv422(
    uint8_t* uyvy_data,
    int width,
    int height,
    int pitch,
    const struct film_recipe* recipe
)
{
    if (!uyvy_data || !recipe || !recipe->active) return;

    for (int y = 0; y < height; y++)
    {
        uint32_t* row = (uint32_t*)(uyvy_data + y * pitch);
        int pairs = width / 2;
        for (int i = 0; i < pairs; i++)
        {
            uint32_t val = row[i];
            uint8_t u  = (uint8_t)(val & 0xFF);
            uint8_t y0 = (uint8_t)((val >> 8) & 0xFF);
            uint8_t v  = (uint8_t)((val >> 16) & 0xFF);
            uint8_t y1 = (uint8_t)((val >> 24) & 0xFF);

            if (recipe->base_stock > 0 && recipe->base_stock <= 5)
            {
                apply_film_stock((uint32_t)recipe->base_stock, &y0, &y1, &u, &v, 0);
            }

            int32_t iy0 = y0, iy1 = y1, iu = u, iv = v;
            apply_recipe_curve(&iy0, &iy1, &iu, &iv,
                recipe->highlight, recipe->shadow, recipe->saturation,
                recipe->wb_ab, recipe->wb_gm);

            if (recipe->grain > 0)
            {
                iy0 = clamp_u8(iy0 + next_film_grain(recipe->grain));
                iy1 = clamp_u8(iy1 + next_film_grain(recipe->grain));
            }

            row[i] = (uint32_t)clamp_u8(iu) | ((uint32_t)clamp_u8(iy0) << 8) | ((uint32_t)clamp_u8(iv) << 16) | ((uint32_t)clamp_u8(iy1) << 24);
        }
    }
}

void apply_film_simulation_yuv422(uint8_t* uyvy_data, int width, int height, int pitch, int film_preset, int grain_strength)
{
    struct film_recipe r;
    memset(&r, 0, sizeof(r));
    r.active = 1;
    r.base_stock = film_preset;
    r.grain = grain_strength;
    apply_film_recipe_yuv422(uyvy_data, width, height, pitch, &r);
}

static void init_raw_gamma_luts(int black, int white)
{
    int range = white - black;
    if (range < 2000) range = 14000;

    for (int i = 0; i < 1024; i++)
    {
        int raw_val = (i << 4);
        if (raw_val > range) raw_val = range;

        int norm = (raw_val * 1024) / range;
        uint32_t val = (uint32_t)(norm * 64);
        uint32_t root = 0;
        uint32_t bit = 1 << 14;
        while (bit > 0)
        {
            if (val >= root + bit)
            {
                val -= root + bit;
                root = (root >> 1) + bit;
            }
            else
            {
                root >>= 1;
            }
            bit >>= 2;
        }
        if (root > 255) root = 255;

        // Daylight white balance gains (Canon 5D2): R ~ 2.05x, G = 1.0x, B ~ 1.55x
        int r_val = (root * 525) >> 8;
        int g_val = (int)root;
        int b_val = (root * 397) >> 8;

        lut_r[i] = (r_val > 255) ? 255 : (uint8_t)r_val;
        lut_g[i] = (g_val > 255) ? 255 : (uint8_t)g_val;
        lut_b[i] = (b_val > 255) ? 255 : (uint8_t)b_val;
    }
}

static void debayer_raw_8lines(
    const struct raw_info* raw,
    int start_y,
    uint8_t* strip,
    int width,
    int strip_pitch
)
{
    int raw_y0 = raw->jpeg.y + start_y;
    int raw_x0 = raw->jpeg.x;
    int raw_pitch = raw->pitch;
    uint8_t* raw_buf = (uint8_t*)raw->buffer;
    int black = (raw->black_level > 0) ? raw->black_level : 1024;

    for (int pair = 0; pair < 4; pair++)
    {
        int line_even = pair * 2;
        int line_odd  = line_even + 1;

        int ry_even = raw_y0 + line_even;
        int ry_odd  = raw_y0 + line_odd;

        if (ry_even >= raw->height) ry_even = (raw->height - 2) & ~1;
        if (ry_odd  >= raw->height) ry_odd  = raw->height - 1;
        if (ry_even < 0) ry_even = 0;
        if (ry_odd  < 0) ry_odd  = 1;

        struct raw_pixblock* row_p = (struct raw_pixblock*)(raw_buf + ry_even * raw_pitch + (raw_x0 / 8) * 14);
        struct raw_pixblock* row_q = (struct raw_pixblock*)(raw_buf + ry_odd  * raw_pitch + (raw_x0 / 8) * 14);

        uint8_t* out_even = strip + line_even * strip_pitch;
        uint8_t* out_odd  = strip + line_odd  * strip_pitch;

        for (int col = 0; col < width; col += 8)
        {
            int pb_idx = col / 8;
            struct raw_pixblock* p = &row_p[pb_idx];
            struct raw_pixblock* q = &row_q[pb_idx];

            int p_pix[8] = { PA, PB, PC, PD, PE, PF, PG, PH };
            int q_pix[8] = { QA, QB, QC, QD, QE, QF, QG, QH };

            for (int k = 0; k < 4; k++)
            {
                int raw_r  = p_pix[k * 2];
                int raw_ga = p_pix[k * 2 + 1];
                int raw_gb = q_pix[k * 2];
                int raw_b  = q_pix[k * 2 + 1];

                int ir  = (raw_r  - black) > 0 ? (((raw_r  - black) >> 4) < 1024 ? ((raw_r  - black) >> 4) : 1023) : 0;
                int iga = (raw_ga - black) > 0 ? (((raw_ga - black) >> 4) < 1024 ? ((raw_ga - black) >> 4) : 1023) : 0;
                int igb = (raw_gb - black) > 0 ? (((raw_gb - black) >> 4) < 1024 ? ((raw_gb - black) >> 4) : 1023) : 0;
                int ib  = (raw_b  - black) > 0 ? (((raw_b  - black) >> 4) < 1024 ? ((raw_b  - black) >> 4) : 1023) : 0;

                int r = lut_r[ir];
                int b = lut_b[ib];
                int g_even = lut_g[(iga + igb) >> 1];
                int g_odd  = g_even;

                int y0_even = (77 * r + 150 * lut_g[iga] + 29 * b) >> 8;
                int y1_even = (77 * r + 150 * g_even + 29 * b) >> 8;
                int u_even  = 128 + ((-43 * r - 85 * g_even + 128 * b) >> 8);
                int v_even  = 128 + ((128 * r - 107 * g_even - 21 * b) >> 8);

                int y0_odd  = (77 * r + 150 * lut_g[igb] + 29 * b) >> 8;
                int y1_odd  = (77 * r + 150 * g_odd + 29 * b) >> 8;
                int u_odd   = u_even;
                int v_odd   = v_even;

                int out_x = (col + k * 2) * 2;
                if (out_x + 3 < strip_pitch)
                {
                    out_even[out_x + 0] = clamp_u8(u_even);
                    out_even[out_x + 1] = clamp_u8(y0_even);
                    out_even[out_x + 2] = clamp_u8(v_even);
                    out_even[out_x + 3] = clamp_u8(y1_even);

                    out_odd[out_x + 0]  = clamp_u8(u_odd);
                    out_odd[out_x + 1]  = clamp_u8(y0_odd);
                    out_odd[out_x + 2]  = clamp_u8(v_odd);
                    out_odd[out_x + 3]  = clamp_u8(y1_odd);
                }
            }
        }
    }
}

static void encode_mcu_row(
    const uint8_t* mcu_row_uyvy,
    int width,
    int pitch,
    int valid_lines,
    int16_t* p_prev_dc_y,
    int16_t* p_prev_dc_u,
    int16_t* p_prev_dc_v
)
{
    int mcu_w = 16;
    int mcus_x = (width + mcu_w - 1) / mcu_w;

    for (int mx = 0; mx < mcus_x; mx++) {
        int px0 = mx * mcu_w;

        for (int r = 0; r < 8; r++) {
            int r_idx = (r < valid_lines) ? r : (valid_lines - 1);
            const uint8_t* row = mcu_row_uyvy + r_idx * pitch;

            for (int c = 0; c < 8; c++) {
                int x0 = px0 + c;
                if (x0 >= width) x0 = width - 1;
                int pair_idx0 = (x0 >> 1) * 4;
                y0_block[r * 8 + c] = ((x0 & 1) ? row[pair_idx0 + 3] : row[pair_idx0 + 1]) - 128;

                int x1 = px0 + 8 + c;
                if (x1 >= width) x1 = width - 1;
                int pair_idx1 = (x1 >> 1) * 4;
                y1_block[r * 8 + c] = ((x1 & 1) ? row[pair_idx1 + 3] : row[pair_idx1 + 1]) - 128;

                int xu = px0 + (c * 2);
                if (xu >= width) xu = width - 2;
                int pair_u = (xu >> 1) * 4;
                u_block[r * 8 + c] = row[pair_u + 0] - 128;
                v_block[r * 8 + c] = row[pair_u + 2] - 128;
            }
        }

        // Encode Y0 (8x8)
        fdct_2d(y0_block, q_block, recip_y);
        encode_block(q_block, p_prev_dc_y, huff_dc_lum, huff_ac_lum);

        // Encode Y1 (8x8)
        fdct_2d(y1_block, q_block, recip_y);
        encode_block(q_block, p_prev_dc_y, huff_dc_lum, huff_ac_lum);

        // Encode Cb/U (8x8)
        fdct_2d(u_block, q_block, recip_c);
        encode_block(q_block, p_prev_dc_u, huff_dc_chrom, huff_ac_chrom);

        // Encode Cr/V (8x8)
        fdct_2d(v_block, q_block, recip_c);
        encode_block(q_block, p_prev_dc_v, huff_dc_chrom, huff_ac_chrom);
    }
}

static inline void put_u16_le(uint8_t* buf, int offset, uint16_t val) {
    buf[offset] = (uint8_t)(val & 0xFF);
    buf[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
}

static inline void put_u32_le(uint8_t* buf, int offset, uint32_t val) {
    buf[offset] = (uint8_t)(val & 0xFF);
    buf[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
    buf[offset + 2] = (uint8_t)((val >> 16) & 0xFF);
    buf[offset + 3] = (uint8_t)((val >> 24) & 0xFF);
}

static inline void put_ifd_entry(uint8_t* buf, int offset, uint16_t tag, uint16_t type, uint32_t count, uint32_t val_or_offset) {
    put_u16_le(buf, offset + 0, tag);
    put_u16_le(buf, offset + 2, type);
    put_u32_le(buf, offset + 4, count);
    put_u32_le(buf, offset + 8, val_or_offset);
}

static void write_jpeg_exif(const struct jpeg_exif_tags* exif) {
    if (!exif) return;

    memset(exif_payload, 0, sizeof(exif_payload));

    // 1. TIFF Header (8 bytes at offset 0)
    exif_payload[0] = 'I';
    exif_payload[1] = 'I';
    put_u16_le(exif_payload, 2, 0x002A);
    put_u32_le(exif_payload, 4, 8); // Offset to IFD0

    // Fixed Structure Offsets
    int ifd0_offset = 8;
    int subifd_offset = 8 + 2 + 6 * 12 + 4; // 86
    int data_offset = subifd_offset + 2 + 6 * 12 + 4; // 164

    // Strings & Content
    const char* s_make = "Canon";
    const char* s_model = (exif->model && exif->model[0]) ? exif->model : "Canon EOS 5D Mark II";
    const char* s_soft = "Magic Lantern Zero 5D2";

    char s_date[24];
    if (exif->year > 2000) {
        snprintf(s_date, sizeof(s_date), "%04d:%02d:%02d %02d:%02d:%02d",
                 exif->year, exif->month, exif->day, exif->hour, exif->min, exif->sec);
    } else {
        snprintf(s_date, sizeof(s_date), "2026:10:11 08:30:00");
    }

    char s_desc[48];
    if (exif->film_name && exif->film_name[0]) {
        snprintf(s_desc, sizeof(s_desc), "%s Film Simulation", exif->film_name);
    } else {
        snprintf(s_desc, sizeof(s_desc), "Silent Picture JPEG");
    }

    char s_comment[64];
    memcpy(s_comment, "ASCII\0\0\0", 8);
    int txt_len = snprintf(s_comment + 8, sizeof(s_comment) - 8, "Film: %s",
                           exif->film_name ? exif->film_name : "Standard");
    int len_make = strlen(s_make) + 1;
    int len_model = strlen(s_model) + 1;
    int len_soft = strlen(s_soft) + 1;
    int len_date = strlen(s_date) + 1;
    int len_desc = strlen(s_desc) + 1;
    int len_comm = 8 + txt_len + 1;

    int off_make = data_offset;
    int off_model = off_make + len_make;
    int off_soft = off_model + len_model;
    int off_date = off_soft + len_soft;
    int off_desc = off_date + len_date;
    int off_exp = (off_desc + len_desc + 3) & ~3;
    int off_fn = off_exp + 8;
    int off_fl = off_fn + 8;
    int off_comm = off_fl + 8;

    // 2. IFD0 (6 entries at ifd0_offset)
    put_u16_le(exif_payload, ifd0_offset, 6);
    put_ifd_entry(exif_payload, ifd0_offset + 2 + 0 * 12, 0x010F, 2, len_make, off_make);
    put_ifd_entry(exif_payload, ifd0_offset + 2 + 1 * 12, 0x0110, 2, len_model, off_model);
    put_ifd_entry(exif_payload, ifd0_offset + 2 + 2 * 12, 0x0131, 2, len_soft, off_soft);
    put_ifd_entry(exif_payload, ifd0_offset + 2 + 3 * 12, 0x0132, 2, len_date, off_date);
    put_ifd_entry(exif_payload, ifd0_offset + 2 + 4 * 12, 0x010E, 2, len_desc, off_desc);
    put_ifd_entry(exif_payload, ifd0_offset + 2 + 5 * 12, 0x8769, 4, 1, subifd_offset);
    put_u32_le(exif_payload, ifd0_offset + 2 + 6 * 12, 0); // Next IFD = 0

    // 3. Exif SubIFD (6 entries at subifd_offset)
    int iso = exif->iso ? exif->iso : 100;
    int s_num = exif->shutter_num ? exif->shutter_num : 1;
    int s_den = exif->shutter_den ? exif->shutter_den : 50;
    int f_num = exif->aperture_x10 ? exif->aperture_x10 : 28;
    int f_den = 10;
    int fl_num = exif->focal_len ? exif->focal_len : 50;
    int fl_den = 1;

    put_u16_le(exif_payload, subifd_offset, 6);
    put_ifd_entry(exif_payload, subifd_offset + 2 + 0 * 12, 0x829A, 5, 1, off_exp);
    put_ifd_entry(exif_payload, subifd_offset + 2 + 1 * 12, 0x829D, 5, 1, off_fn);
    put_ifd_entry(exif_payload, subifd_offset + 2 + 2 * 12, 0x8827, 3, 1, (uint32_t)iso);
    put_ifd_entry(exif_payload, subifd_offset + 2 + 3 * 12, 0x9003, 2, len_date, off_date);
    put_ifd_entry(exif_payload, subifd_offset + 2 + 4 * 12, 0x920A, 5, 1, off_fl);
    put_ifd_entry(exif_payload, subifd_offset + 2 + 5 * 12, 0x9286, 7, len_comm, off_comm);
    put_u32_le(exif_payload, subifd_offset + 2 + 6 * 12, 0); // Next IFD = 0

    // 4. Data Area
    memcpy(exif_payload + off_make, s_make, len_make);
    memcpy(exif_payload + off_model, s_model, len_model);
    memcpy(exif_payload + off_soft, s_soft, len_soft);
    memcpy(exif_payload + off_date, s_date, len_date);
    memcpy(exif_payload + off_desc, s_desc, len_desc);

    put_u32_le(exif_payload, off_exp, s_num);
    put_u32_le(exif_payload, off_exp + 4, s_den);

    put_u32_le(exif_payload, off_fn, f_num);
    put_u32_le(exif_payload, off_fn + 4, f_den);

    put_u32_le(exif_payload, off_fl, fl_num);
    put_u32_le(exif_payload, off_fl + 4, fl_den);

    memcpy(exif_payload + off_comm, s_comment, len_comm);

    int total_tiff_len = off_comm + len_comm;
    if (total_tiff_len > (int)sizeof(exif_payload)) {
        total_tiff_len = sizeof(exif_payload);
    }

    // 5. Emit APP1 Marker & Length
    write_byte(0xFF);
    write_byte(0xE1);

    int app1_len = total_tiff_len + 6 + 2;
    write_byte((uint8_t)(app1_len >> 8));
    write_byte((uint8_t)(app1_len & 0xFF));

    // "Exif\0\0"
    write_byte('E'); write_byte('x'); write_byte('i'); write_byte('f');
    write_byte(0x00); write_byte(0x00);

    for (int i = 0; i < total_tiff_len; i++) {
        write_byte(exif_payload[i]);
    }
}

static void write_jpeg_headers(int width, int height, int quality, const struct jpeg_exif_tags* exif)
{
    init_tables(quality);
    out_buf_pos = 0;
    bit_buffer = 0;
    bit_count = 0;

    // 1. SOI (Start of Image)
    write_byte(0xFF); write_byte(0xD8);

    // Optional APP1 (EXIF Header)
    if (exif) {
        write_jpeg_exif(exif);
    }

    // 2. APP0 (JFIF Header)
    write_byte(0xFF); write_byte(0xE0);
    write_byte(0x00); write_byte(0x10); // Length = 16
    write_byte('J'); write_byte('F'); write_byte('I'); write_byte('F'); write_byte(0x00);
    write_byte(0x01); write_byte(0x01); // Version 1.1
    write_byte(0x00);                   // Units: None
    write_byte(0x00); write_byte(0x01); // X density
    write_byte(0x00); write_byte(0x01); // Y density
    write_byte(0x00); write_byte(0x00); // Thumbnail: 0x0

    // 3. DQT (Quantization Tables)
    write_byte(0xFF); write_byte(0xDB);
    write_byte(0x00); write_byte(132); // Length = 132
    write_byte(0x00); // Table 0 (Luminance)
    for (int i = 0; i < 64; i++) write_byte((uint8_t)qtab_y[zigzag[i]]);
    write_byte(0x01); // Table 1 (Chrominance)
    for (int i = 0; i < 64; i++) write_byte((uint8_t)qtab_c[zigzag[i]]);

    // 4. SOF0 (Baseline 4:2:2)
    write_byte(0xFF); write_byte(0xC0);
    write_byte(0x00); write_byte(17);  // Length = 17
    write_byte(0x08);                   // Precision = 8 bits
    write_byte((height >> 8) & 0xFF); write_byte(height & 0xFF);
    write_byte((width >> 8) & 0xFF); write_byte(width & 0xFF);
    write_byte(0x03);                   // 3 components (Y, Cb, Cr)
    write_byte(0x01); write_byte(0x21); write_byte(0x00); // Y  (H=2, V=1)
    write_byte(0x02); write_byte(0x11); write_byte(0x01); // Cb (H=1, V=1)
    write_byte(0x03); write_byte(0x11); write_byte(0x01); // Cr (H=1, V=1)

    // 5. DHT (Huffman Tables)
    write_byte(0xFF); write_byte(0xC4);
    write_byte(0x00); write_byte(3 + sizeof(dc_lum_bits) + sizeof(dc_lum_val));
    write_byte(0x00);
    for (int i = 0; i < 16; i++) write_byte(dc_lum_bits[i]);
    for (size_t i = 0; i < sizeof(dc_lum_val); i++) write_byte(dc_lum_val[i]);

    write_byte(0xFF); write_byte(0xC4);
    write_byte(0x00); write_byte(3 + sizeof(dc_chrom_bits) + sizeof(dc_chrom_val));
    write_byte(0x01);
    for (int i = 0; i < 16; i++) write_byte(dc_chrom_bits[i]);
    for (size_t i = 0; i < sizeof(dc_chrom_val); i++) write_byte(dc_chrom_val[i]);

    write_byte(0xFF); write_byte(0xC4);
    write_byte(0x00); write_byte(3 + sizeof(ac_lum_bits) + sizeof(ac_lum_val));
    write_byte(0x10);
    for (int i = 0; i < 16; i++) write_byte(ac_lum_bits[i]);
    for (size_t i = 0; i < sizeof(ac_lum_val); i++) write_byte(ac_lum_val[i]);

    write_byte(0xFF); write_byte(0xC4);
    write_byte(0x00); write_byte(3 + sizeof(ac_chrom_bits) + sizeof(ac_chrom_val));
    write_byte(0x11);
    for (int i = 0; i < 16; i++) write_byte(ac_chrom_bits[i]);
    for (size_t i = 0; i < sizeof(ac_chrom_val); i++) write_byte(ac_chrom_val[i]);

    // 6. SOS (Start of Scan)
    write_byte(0xFF); write_byte(0xDA);
    write_byte(0x00); write_byte(12);
    write_byte(0x03);
    write_byte(0x01); write_byte(0x00);
    write_byte(0x02); write_byte(0x11);
    write_byte(0x03); write_byte(0x11);
    write_byte(0x00); write_byte(0x3F); write_byte(0x00);
}

static void write_jpeg_trailer(void)
{
    flush_bits();
    write_byte(0xFF); write_byte(0xD9); // EOI
    flush_out_buf();
    if (out_file) {
        FIO_CloseFile(out_file);
        out_file = NULL;
    }
}

int encode_yuv422_to_jpeg(
    const uint8_t* uyvy_data,
    int width,
    int height,
    int pitch,
    int quality,
    const struct jpeg_exif_tags* exif,
    const char* out_path
) {
    if (!uyvy_data || width <= 0 || height <= 0 || !out_path) {
        printf("[JPEG_ENC] Invalid args: uyvy=%p w=%d h=%d path=%s\n", uyvy_data, width, height, out_path ? out_path : "NULL");
        return 0;
    }

    out_file = FIO_CreateFile(out_path);
    if (!out_file) {
        return 0;
    }

    write_jpeg_headers(width, height, quality, exif);

    int16_t prev_dc_y = 0;
    int16_t prev_dc_u = 0;
    int16_t prev_dc_v = 0;

    int mcu_h = 8;
    int mcus_y = (height + mcu_h - 1) / mcu_h;

    for (int my = 0; my < mcus_y; my++) {
        int py0 = my * mcu_h;
        int remaining = height - py0;
        int valid_lines = (remaining >= 8) ? 8 : remaining;

        const uint8_t* row_ptr = uyvy_data + py0 * pitch;
        encode_mcu_row(row_ptr, width, pitch, valid_lines, &prev_dc_y, &prev_dc_u, &prev_dc_v);
    }

    write_jpeg_trailer();
    return 1;
}

int encode_raw_fullres_to_jpeg(
    const struct raw_info* raw,
    int quality,
    const struct film_recipe* recipe,
    const struct jpeg_exif_tags* exif,
    const char* out_path
) {
    if (!raw || !raw->buffer || !out_path) {
        printf("[JPEG_FULLRES] Invalid args: raw=%p path=%s\n", raw, out_path ? out_path : "NULL");
        return 0;
    }

    int width = raw->jpeg.width;
    int height = raw->jpeg.height;

    if (width <= 0) width = raw->width & ~15;
    if (height <= 0) height = raw->height & ~7;
    if (width > STRIP_MAX_WIDTH) width = STRIP_MAX_WIDTH;

    out_file = FIO_CreateFile(out_path);
    if (!out_file) {
        printf("[JPEG_FULLRES] Failed to create output file: %s\n", out_path);
        return 0;
    }

    init_raw_gamma_luts(raw->black_level, raw->white_level);
    write_jpeg_headers(width, height, quality, exif);

    int16_t prev_dc_y = 0;
    int16_t prev_dc_u = 0;
    int16_t prev_dc_v = 0;

    int mcu_h = 8;
    int mcus_y = (height + mcu_h - 1) / mcu_h;
    int strip_pitch = width * 2;

    for (int my = 0; my < mcus_y; my++) {
        int py0 = my * mcu_h;
        debayer_raw_8lines(raw, py0, strip_buf, width, strip_pitch);

        if (recipe && recipe->active) {
            apply_film_recipe_yuv422(strip_buf, width, 8, strip_pitch, recipe);
        }

        encode_mcu_row(strip_buf, width, strip_pitch, 8, &prev_dc_y, &prev_dc_u, &prev_dc_v);

        if ((my & 63) == 0 && mcus_y > 0) {
            bmp_printf(FONT_MED, 0, 83, "JPEG: %d%% (%d/%d)", (my * 100) / mcus_y, my, mcus_y);
        }
    }

    write_jpeg_trailer();
    bmp_printf(FONT_MED, 0, 83, "Saved Full-Res JPEG %dx%d", width, height);
    return 1;
}
