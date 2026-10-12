/* Host implementation of the fault-injectable FIO_WriteFile stub. */
#include "dryos.h"

long host_fio_fail_after = -1;
void (*host_fio_write_hook)(void) = NULL;

long host_fio_written = 0;

int host_fio_write(FILE* f, const void* buf, size_t size)
{
    if (host_fio_write_hook)
    {
        void (*hook)(void) = host_fio_write_hook;
        host_fio_write_hook = NULL; /* fire once */
        hook();
    }

    if (host_fio_fail_after >= 0 && host_fio_written + (long)size > host_fio_fail_after)
    {
        return -1; /* simulated card-full / I/O error */
    }

    host_fio_written += (long)size;
    return (int)fwrite(buf, 1, size, f);
}
