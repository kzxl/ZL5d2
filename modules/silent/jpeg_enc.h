#ifndef _JPEG_ENC_H_
#define _JPEG_ENC_H_

#include <stdint.h>

struct raw_info;

/**
 * Metadata container for embedding standard EXIF 2.2 APP1 headers in JPEG streams.
 * Eliminates all dynamic heap allocation (fixed offsets and stack parameters).
 */
struct jpeg_exif_tags {
    const char* model;        // Camera model, e.g. "Canon EOS 5D Mark II"
    const char* lens_name;    // Attached lens model, e.g. "EF50mm f/1.8 II"
    const char* film_name;    // Film Stock Name, e.g. "Kodak Portra 400"
    int iso;                  // Photographic ISO sensitivity (e.g. 100, 400, 1600)
    int shutter_num;          // Shutter speed numerator (e.g. 1)
    int shutter_den;          // Shutter speed denominator (e.g. 50 for 1/50s)
    int aperture_x10;         // Aperture value * 10 (e.g. 28 for f/2.8, 56 for f/5.6)
    int focal_len;            // Focal length in millimeters (e.g. 50)
    int grain_strength;       // Analog film grain injection level (0: Off, 1: Subtle, 2: Med, 3: Heavy)
    int year;                 // Calendar year (e.g. 2026)
    int month;                // Calendar month (1..12)
    int day;                  // Calendar day (1..31)
    int hour;                 // Hour (0..23)
    int min;                  // Minute (0..59)
    int sec;                  // Second (0..59)
};

/**
 * Encodes a UYVY (YUV 4:2:2) buffer directly to a standard Baseline JPEG file with optional EXIF.
 * 
 * Safety & Memory Guarantees:
 * - 0 dynamic heap allocations (0 bytes malloc/free).
 * - Fixed static BSS stream buffer (flushed in 4KB chunks).
 * - Stack usage < 64 bytes (all DCT blocks are static).
 * - Completely bounded integer arithmetic.
 *
 * @param uyvy_data Pointer to start of UYVY image buffer.
 * @param width     Image width in pixels (e.g. 720, 1024, 1920).
 * @param height    Image height in pixels (e.g. 480, 680, 1080).
 * @param pitch     Byte stride per row (e.g. width * 2).
 * @param quality   JPEG quality factor (1..100).
 * @param exif      Pointer to optional EXIF metadata tags (can be NULL).
 * @param out_path  Output path on CF/SD card (e.g. "A:/DCIM/100EOS5D/51780001.JPG").
 * @return 1 on success, 0 on failure.
 */
int encode_yuv422_to_jpeg(
    const uint8_t* uyvy_data,
    int width,
    int height,
    int pitch,
    int quality,
    const struct jpeg_exif_tags* exif,
    const char* out_path
);

/**
 * Encodes a 21.1MP full-resolution RAW sensor capture directly to a Baseline JPEG file.
 * 
 * Safety & Streaming Guarantees:
 * - Streaming 8-line MCU striping: memory footprint < 100 KB total.
 * - 0 dynamic heap allocations.
 * - In-pipeline fast Debayer + Film Simulation color grading.
 * - Bounded integer FDCT + Huffman stream direct to CF card via FIO_WriteFile.
 *
 * @param raw         Pointer to camera raw_info containing sensor buffer & dimensions.
 * @param quality     JPEG quality factor (1..100).
 * @param film_preset Film simulation stock (0: Off, 1: Portra 400, 2: Velvia 50, 3: Cinestill 800T, 4: Tri-X 400, 5: Classic Chrome).
 * @param exif        Pointer to optional EXIF metadata tags (can be NULL).
 * @param out_path    Output path on CF card (e.g. "A:/DCIM/100EOS5D/51780001.JPG").
 * @return 1 on success, 0 on failure.
 */
int encode_raw_fullres_to_jpeg(
    const struct raw_info* raw,
    int quality,
    int film_preset,
    const struct jpeg_exif_tags* exif,
    const char* out_path
);

/**
 * Applies film simulation preset and optional analog film grain to a YUV422 (UYVY) buffer in-place.
 * 
 * @param uyvy_data      Pointer to start of UYVY image buffer.
 * @param width          Image width in pixels.
 * @param height         Image height in pixels.
 * @param pitch          Byte stride per row.
 * @param film_preset    Film simulation stock (0: Off, 1: Portra 400, 2: Velvia 50, 3: Cinestill 800T, 4: Tri-X 400, 5: Classic Chrome).
 * @param grain_strength Analog film grain level (0: Off, 1: Subtle, 2: Medium, 3: Heavy).
 */
void apply_film_simulation_yuv422(
    uint8_t* uyvy_data,
    int width,
    int height,
    int pitch,
    int film_preset,
    int grain_strength
);

#endif // _JPEG_ENC_H_
