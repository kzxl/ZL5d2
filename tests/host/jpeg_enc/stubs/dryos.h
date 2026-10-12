/* Host-side stub for DryOS file I/O, used only by the jpeg_enc host test harness. */
#ifndef HOST_STUB_DRYOS_H
#define HOST_STUB_DRYOS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FIO_CreateFile(path)          fopen((path), "wb")
#define FIO_WriteFile(f, buf, size)   ((int)fwrite((buf), 1, (size), (f)))
#define FIO_CloseFile(f)              fclose(f)

#endif
