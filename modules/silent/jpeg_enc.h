#ifndef _JPEG_ENC_H_
#define _JPEG_ENC_H_

#include <stdint.h>

struct raw_info;

/**
 * Encodes a UYVY (YUV 4:2:2) buffer directly to a standard Baseline JPEG file.
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
 * @param out_path  Output path on CF/SD card (e.g. "B:/DCIM/100CANON/IMG_0001.JPG").
 * @return 1 on success, 0 on failure.
 */
int encode_yuv422_to_jpeg(const uint8_t* uyvy_data, int width, int height, int pitch, int quality, const char* out_path);

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
 * @param out_path    Output path on CF card (e.g. "B:/DCIM/100CANON/IMG_0001.JPG").
 * @return 1 on success, 0 on failure.
 */
int encode_raw_fullres_to_jpeg(const struct raw_info* raw, int quality, int film_preset, const char* out_path);

/**
 * Applies film simulation preset to a YUV422 (UYVY) buffer in-place.
 * 
 * @param uyvy_data   Pointer to start of UYVY image buffer.
 * @param width       Image width in pixels.
 * @param height      Image height in pixels.
 * @param pitch       Byte stride per row.
 * @param film_preset Film simulation stock (0: Off, 1: Portra 400, 2: Velvia 50, 3: Cinestill 800T, 4: Tri-X 400, 5: Classic Chrome).
 */
void apply_film_simulation_yuv422(uint8_t* uyvy_data, int width, int height, int pitch, int film_preset);

#endif // _JPEG_ENC_H_

