/* Host-side stub for DryOS file I/O, used only by the jpeg_enc host test harness. */
#ifndef HOST_STUB_DRYOS_H
#define HOST_STUB_DRYOS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*
 * Fault-injection and observation hooks (defined in host_fio.c):
 * - host_fio_fail_after: once this many bytes have been written in total,
 *   every further FIO_WriteFile reports a short write (-1 disables).
 * - host_fio_write_hook: called before each write; lets a test re-enter the encoder.
 */
extern long host_fio_fail_after;
extern long host_fio_written; /* total bytes written; reset before arming host_fio_fail_after */
extern void (*host_fio_write_hook)(void);

int host_fio_write(FILE* f, const void* buf, size_t size);

#define FIO_CreateFile(path)          fopen((path), "wb")
#define FIO_WriteFile(f, buf, size)   host_fio_write((f), (buf), (size))
#define FIO_CloseFile(f)              fclose(f)
#define FIO_RemoveFile(path)          remove(path)

static inline uint32_t cli(void) { return 0; }
static inline void sei(uint32_t old) { (void)old; }

#endif
