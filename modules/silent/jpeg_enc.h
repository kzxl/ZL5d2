#ifndef _JPEG_ENC_H_
#define _JPEG_ENC_H_

#include <stdint.h>

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

#endif // _JPEG_ENC_H_
