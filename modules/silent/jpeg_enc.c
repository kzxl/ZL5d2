/**
 * Ultra-lightweight, Zero-Allocation YUV422 (UYVY) to Baseline JPEG Encoder
 * Designed specifically for DIGIC 4 (Canon EOS 5D Mark II).
 *
 * Guarantees:
 * - ZERO heap allocation (no malloc/free, no memory fragmentation).
 * - ZERO stack overflow risk (all working matrices and buffers in static BSS).
 * - Direct streaming I/O in 4KB chunks via FIO_WriteFile.
 * - Integer-only AAN / LLM 2D Forward DCT.
 */

#include <dryos.h>
#include <stdint.h>
#include <string.h>
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

// 2D FDCT on 8x8 block
static void fdct_2d(int16_t* in, int16_t* out, const uint16_t* qtable) {
    // Rows
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            dct_workspace[i * 8 + j] = in[i * 8 + j];
        }
        fdct_1d(&dct_workspace[i * 8]);
    }

    // Columns
    for (int j = 0; j < 8; j++) {
        int32_t col[8];
        for (int i = 0; i < 8; i++) col[i] = dct_workspace[i * 8 + j];
        fdct_1d(col);
        for (int i = 0; i < 8; i++) {
            int32_t val = (col[i] + 4) >> 3; // rescale
            int q = qtable[zigzag[i * 8 + j]];
            int qval = (val >= 0) ? (val + q / 2) / q : (val - q / 2) / q;
            out[zigzag[i * 8 + j]] = (int16_t)qval;
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

int encode_yuv422_to_jpeg(const uint8_t* uyvy_data, int width, int height, int pitch, int quality, const char* out_path) {
    if (!uyvy_data || width <= 0 || height <= 0 || !out_path) {
        printf("[JPEG_ENC] Invalid args: uyvy=%p w=%d h=%d path=%s\n", uyvy_data, width, height, out_path ? out_path : "NULL");
        return 0;
    }

    out_file = FIO_CreateFile(out_path);
    if (!out_file) {
        FILE* lf = FIO_CreateFileOrAppend("A:/silent.log");
        if (lf) {
            FIO_WriteFile(lf, "FIO_CreateFile FAILED: ", 23);
            FIO_WriteFile(lf, out_path, strlen(out_path));
            FIO_WriteFile(lf, "\n", 1);
            FIO_CloseFile(lf);
        }
        return 0;
    }
    else {
        FILE* lf = FIO_CreateFileOrAppend("A:/silent.log");
        if (lf) {
            FIO_WriteFile(lf, "FIO_CreateFile SUCCESS: ", 24);
            FIO_WriteFile(lf, out_path, strlen(out_path));
            FIO_WriteFile(lf, "\n", 1);
            FIO_CloseFile(lf);
        }
    }

    init_tables(quality);
    out_buf_pos = 0;
    bit_buffer = 0;
    bit_count = 0;

    // 1. SOI (Start of Image)
    write_byte(0xFF); write_byte(0xD8);

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
    // Component 1: Y (H=2, V=1) -> 4:2:2 sampling!
    write_byte(0x01); write_byte(0x21); write_byte(0x00);
    // Component 2: Cb (H=1, V=1)
    write_byte(0x02); write_byte(0x11); write_byte(0x01);
    // Component 3: Cr (H=1, V=1)
    write_byte(0x03); write_byte(0x11); write_byte(0x01);

    // 5. DHT (Huffman Tables)
    // DC Lum
    write_byte(0xFF); write_byte(0xC4);
    write_byte(0x00); write_byte(3 + sizeof(dc_lum_bits) + sizeof(dc_lum_val));
    write_byte(0x00); // DC 0
    for (int i = 0; i < 16; i++) write_byte(dc_lum_bits[i]);
    for (size_t i = 0; i < sizeof(dc_lum_val); i++) write_byte(dc_lum_val[i]);

    // DC Chrom
    write_byte(0xFF); write_byte(0xC4);
    write_byte(0x00); write_byte(3 + sizeof(dc_chrom_bits) + sizeof(dc_chrom_val));
    write_byte(0x01); // DC 1
    for (int i = 0; i < 16; i++) write_byte(dc_chrom_bits[i]);
    for (size_t i = 0; i < sizeof(dc_chrom_val); i++) write_byte(dc_chrom_val[i]);

    // AC Lum
    write_byte(0xFF); write_byte(0xC4);
    write_byte(0x00); write_byte(3 + sizeof(ac_lum_bits) + sizeof(ac_lum_val));
    write_byte(0x10); // AC 0
    for (int i = 0; i < 16; i++) write_byte(ac_lum_bits[i]);
    for (size_t i = 0; i < sizeof(ac_lum_val); i++) write_byte(ac_lum_val[i]);

    // AC Chrom
    write_byte(0xFF); write_byte(0xC4);
    write_byte(0x00); write_byte(3 + sizeof(ac_chrom_bits) + sizeof(ac_chrom_val));
    write_byte(0x11); // AC 1
    for (int i = 0; i < 16; i++) write_byte(ac_chrom_bits[i]);
    for (size_t i = 0; i < sizeof(ac_chrom_val); i++) write_byte(ac_chrom_val[i]);

    // 6. SOS (Start of Scan)
    write_byte(0xFF); write_byte(0xDA);
    write_byte(0x00); write_byte(12);  // Length = 12
    write_byte(0x03);                   // 3 components
    write_byte(0x01); write_byte(0x00); // Y: DC 0, AC 0
    write_byte(0x02); write_byte(0x11); // Cb: DC 1, AC 1
    write_byte(0x03); write_byte(0x11); // Cr: DC 1, AC 1
    write_byte(0x00); write_byte(0x3F); write_byte(0x00); // Spectral selection

    // 7. Encode MCUs (16x8 pixels per MCU in 4:2:2)
    int16_t prev_dc_y = 0;
    int16_t prev_dc_u = 0;
    int16_t prev_dc_v = 0;

    int mcu_w = 16;
    int mcu_h = 8;
    int mcus_x = (width + mcu_w - 1) / mcu_w;
    int mcus_y = (height + mcu_h - 1) / mcu_h;

    for (int my = 0; my < mcus_y; my++) {
        int py0 = my * mcu_h;
        for (int mx = 0; mx < mcus_x; mx++) {
            int px0 = mx * mcu_w;

            // Extract MCU pixels from UYVY buffer:
            for (int r = 0; r < 8; r++) {
                int y_coord = py0 + r;
                if (y_coord >= height) y_coord = height - 1;
                const uint8_t* row = uyvy_data + y_coord * pitch;

                for (int c = 0; c < 8; c++) {
                    // Left 8 pixels (Y0)
                    int x0 = px0 + c;
                    if (x0 >= width) x0 = width - 1;
                    int pair_idx0 = (x0 >> 1) * 4;
                    y0_block[r * 8 + c] = ((x0 & 1) ? row[pair_idx0 + 3] : row[pair_idx0 + 1]) - 128;

                    // Right 8 pixels (Y1)
                    int x1 = px0 + 8 + c;
                    if (x1 >= width) x1 = width - 1;
                    int pair_idx1 = (x1 >> 1) * 4;
                    y1_block[r * 8 + c] = ((x1 & 1) ? row[pair_idx1 + 3] : row[pair_idx1 + 1]) - 128;

                    // Subsampled U and V (8 samples for 16 horizontal pixels)
                    int xu = px0 + (c * 2);
                    if (xu >= width) xu = width - 2;
                    int pair_u = (xu >> 1) * 4;
                    u_block[r * 8 + c] = row[pair_u + 0] - 128; // U
                    v_block[r * 8 + c] = row[pair_u + 2] - 128; // V
                }
            }

            // Encode Y0 (8x8)
            fdct_2d(y0_block, q_block, qtab_y);
            encode_block(q_block, &prev_dc_y, huff_dc_lum, huff_ac_lum);

            // Encode Y1 (8x8)
            fdct_2d(y1_block, q_block, qtab_y);
            encode_block(q_block, &prev_dc_y, huff_dc_lum, huff_ac_lum);

            // Encode Cb/U (8x8)
            fdct_2d(u_block, q_block, qtab_c);
            encode_block(q_block, &prev_dc_u, huff_dc_chrom, huff_ac_chrom);

            // Encode Cr/V (8x8)
            fdct_2d(v_block, q_block, qtab_c);
            encode_block(q_block, &prev_dc_v, huff_dc_chrom, huff_ac_chrom);
        }
    }

    flush_bits();

    // 8. EOI (End of Image)
    write_byte(0xFF); write_byte(0xD9);
    flush_out_buf();

    FIO_CloseFile(out_file);
    out_file = NULL;
    return 1;
}
