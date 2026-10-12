/*
 * Negative / contract tests for modules/silent/jpeg_enc.c (host build).
 * Each case prints PASS/FAIL; the process exit code is the number of failures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dryos.h"
#include "raw.h"
#include "jpeg_enc.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

/*
 * Returns a buffer of `size` bytes whose last byte sits directly before an
 * inaccessible page, so any over-read faults instead of passing silently.
 */
static void* alloc_against_guard_page(size_t size)
{
    size_t page = 4096;
    size_t span = ((size + page - 1) / page) * page;
#ifdef _WIN32
    uint8_t* base = VirtualAlloc(NULL, span + page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base) return NULL;
    DWORD old;
    VirtualProtect(base + span, page, PAGE_NOACCESS, &old);
#else
    uint8_t* base = mmap(NULL, span + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    mprotect(base + span, page, PROT_NONE);
#endif
    return base + span - size;
}

#define W 64
#define H 32

static uint8_t frame[H * W * 2] __attribute__((aligned(4)));
static int failures = 0;
static int total = 0;

static void check(int cond, const char* name)
{
    total++;
    if (!cond) failures++;
    printf("%s %s\n", cond ? "PASS" : "FAIL", name);
}

static int file_exists(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static long file_size(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static int files_equal(const char* a, const char* b)
{
    FILE* fa = fopen(a, "rb");
    FILE* fb = fopen(b, "rb");
    int equal = (fa && fb);
    while (equal)
    {
        int ca = fgetc(fa);
        int cb = fgetc(fb);
        if (ca != cb) equal = 0;
        if (ca == EOF || cb == EOF) break;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return equal;
}

static void fill_frame(void)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W * 2; x++)
            frame[y * W * 2 + x] = (uint8_t)((x * 7 + y * 13) & 0xFF);
}

/* --- Re-entrancy probe ---------------------------------------------------- */
static int nested_result = -1;
static const char* nested_path = NULL;

static void reenter_encoder(void)
{
    nested_result = encode_yuv422_to_jpeg(frame, W, H, W * 2, 85, NULL, nested_path);
}

int main(int argc, char** argv)
{
    const char* dir = (argc > 1) ? argv[1] : ".";
    char path[512], path2[512];
    fill_frame();

    /* 1. Input contract: invalid geometry is rejected before any file is created. */
    struct { int w, h, pitch; const char* name; } bad[] = {
        { 1,   H, 2,       "reject width 1" },
        { 15,  H, 30,      "reject width below one MCU (15)" },
        { 63,  H, 126,     "reject odd width (63)" },
        { W,   H, W * 2 - 2, "reject pitch smaller than width*2" },
        { W,   0, W * 2,   "reject height 0" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        snprintf(path, sizeof(path), "%s/bad_%u.jpg", dir, (unsigned)i);
        remove(path);
        int rc = encode_yuv422_to_jpeg(frame, bad[i].w, bad[i].h, bad[i].pitch, 85, NULL, path);
        check(rc == 0 && !file_exists(path), bad[i].name);
    }

    /* 2. Write failure (card full) returns 0 and removes the partial file. */
    snprintf(path, sizeof(path), "%s/ioerror.jpg", dir);
    remove(path);
    host_fio_written = 0;
    host_fio_fail_after = 100;
    int rc_io = encode_yuv422_to_jpeg(frame, W, H, W * 2, 85, NULL, path);
    host_fio_fail_after = -1;
    check(rc_io == 0, "write failure returns 0");
    check(!file_exists(path), "write failure removes partial file");

    /* 4. Re-entrant call while an encode is in progress is refused; outer call succeeds. */
    snprintf(path, sizeof(path), "%s/outer.jpg", dir);
    snprintf(path2, sizeof(path2), "%s/nested.jpg", dir);
    remove(path); remove(path2);
    nested_path = path2;
    host_fio_write_hook = reenter_encoder;
    int rc_outer = encode_yuv422_to_jpeg(frame, W, H, W * 2, 85, NULL, path);
    check(nested_result == 0, "nested encode refused while busy");
    check(rc_outer == 1 && file_size(path) > 0, "outer encode completes after refused nested call");

    /* 5. Encoder is usable again after an error (busy flag released on failure paths). */
    snprintf(path, sizeof(path), "%s/after_error.jpg", dir);
    remove(path);
    check(encode_yuv422_to_jpeg(frame, W, H, W * 2, 85, NULL, path) == 1, "encoder reusable after failure");

    /* 6. Oversized EXIF strings do not overflow the payload; file is still produced. */
    static char long_model[400];
    memset(long_model, 'M', sizeof(long_model) - 1);
    long_model[sizeof(long_model) - 1] = '\0';
    static char long_film[200];
    memset(long_film, 'F', sizeof(long_film) - 1);
    long_film[sizeof(long_film) - 1] = '\0';
    struct jpeg_exif_tags exif;
    memset(&exif, 0, sizeof(exif));
    exif.model = long_model;
    exif.film_name = long_film;
    snprintf(path, sizeof(path), "%s/long_exif.jpg", dir);
    remove(path);
    check(encode_yuv422_to_jpeg(frame, W, H, W * 2, 85, &exif, path) == 1, "long EXIF strings encode");

    /* 7. Nominal EXIF file; segment structure and tag values are verified by run_psnr_test.py. */
    memset(&exif, 0, sizeof(exif));
    exif.model = "Canon EOS 5D Mark II";
    exif.film_name = "Portra 400";
    exif.iso = 400;
    snprintf(path, sizeof(path), "%s/nominal_exif.jpg", dir);
    remove(path);
    check(encode_yuv422_to_jpeg(frame, W, H, W * 2, 85, &exif, path) == 1, "nominal EXIF encode");

    /* 8. Full-res: jpeg.x + jpeg.width beyond the raw row must be clamped, not over-read. */
    enum { RAW_W = 64, RAW_H = 16, RAW_PITCH = RAW_W * 14 / 8 };
    uint8_t* raw_buf = alloc_against_guard_page(RAW_H * RAW_PITCH);
    if (raw_buf) {
        memset(raw_buf, 0x5A, RAW_H * RAW_PITCH);
        struct raw_info raw;
        memset(&raw, 0, sizeof(raw));
        raw.buffer = raw_buf;
        raw.width = RAW_W;
        raw.height = RAW_H;
        raw.pitch = RAW_PITCH;
        raw.black_level = 2048;
        raw.white_level = 15000;
        raw.jpeg.x = 8;
        raw.jpeg.y = 0;
        raw.jpeg.width = RAW_W; /* 8 + 64 > 64: needs clamping to 48 */
        raw.jpeg.height = RAW_H;
        snprintf(path, sizeof(path), "%s/fullres_clamped.jpg", dir);
        remove(path);
        check(encode_raw_fullres_to_jpeg(&raw, 85, NULL, NULL, path) == 1 && file_size(path) > 0,
              "full-res width clamped to raw row (no over-read)");
    } else {
        check(0, "full-res guard-page allocation");
    }

    /* 9. Recipe encode (S7): the source buffer is read-only and output matches in-place apply + encode. */
    {
        enum { PAD = 16, RP = W * 2 + PAD }; /* padded pitch exercises the strip copy stride */
        static uint8_t src[H * RP] __attribute__((aligned(4)));
        static uint8_t pristine[H * RP];
        static uint8_t ref[H * RP] __attribute__((aligned(4)));
        for (int i = 0; i < H * RP; i++) src[i] = (uint8_t)((i * 31 + (i >> 7)) & 0xFF);
        memcpy(pristine, src, sizeof(src));
        memcpy(ref, src, sizeof(src));

        struct film_recipe recipe;
        memset(&recipe, 0, sizeof(recipe));
        recipe.active = 1;
        recipe.base_stock = 2;
        recipe.highlight = 1;
        recipe.shadow = -1;
        recipe.saturation = 2;
        recipe.wb_ab = 3;
        recipe.wb_gm = -2;
        recipe.grain = 0; /* deterministic: the grain LFSR state is shared across calls */

        apply_film_recipe_yuv422(ref, W, H, RP, &recipe);
        snprintf(path, sizeof(path), "%s/recipe_ref.jpg", dir);
        snprintf(path2, sizeof(path2), "%s/recipe_strip.jpg", dir);
        remove(path); remove(path2);
        int rc_ref = encode_yuv422_to_jpeg(ref, W, H, RP, 85, NULL, path);
        int rc_strip = encode_yuv422_recipe_to_jpeg(src, W, H, RP, 85, &recipe, NULL, path2);
        check(rc_ref == 1 && rc_strip == 1, "recipe strip encode succeeds");
        check(memcmp(src, pristine, sizeof(src)) == 0, "recipe strip encode leaves source untouched");
        check(files_equal(path, path2), "recipe strip output equals in-place apply + encode");
        {
            char plain[512];
            snprintf(plain, sizeof(plain), "%s/recipe_plain.jpg", dir);
            remove(plain);
            int rc_plain = encode_yuv422_to_jpeg(src, W, H, RP, 85, NULL, plain);
            check(rc_plain == 1 && !files_equal(plain, path2), "recipe output differs from ungraded encode");
        }

        /* Height not a multiple of 8: the last partial strip must be handled identically. */
        enum { HP = 21 };
        memcpy(ref, pristine, sizeof(ref));
        apply_film_recipe_yuv422(ref, W, HP, RP, &recipe);
        remove(path); remove(path2);
        rc_ref = encode_yuv422_to_jpeg(ref, W, HP, RP, 85, NULL, path);
        rc_strip = encode_yuv422_recipe_to_jpeg(src, W, HP, RP, 85, &recipe, NULL, path2);
        check(rc_ref == 1 && rc_strip == 1 && files_equal(path, path2), "recipe strip handles partial last strip");

        /* Grain active: source must still be untouched. */
        recipe.grain = 3;
        remove(path2);
        rc_strip = encode_yuv422_recipe_to_jpeg(src, W, H, RP, 85, &recipe, NULL, path2);
        check(rc_strip == 1 && memcmp(src, pristine, sizeof(src)) == 0, "recipe with grain leaves source untouched");

        /* NULL / inactive recipe is identical to the plain encoder. */
        remove(path); remove(path2);
        rc_ref = encode_yuv422_to_jpeg(src, W, H, RP, 85, NULL, path);
        rc_strip = encode_yuv422_recipe_to_jpeg(src, W, H, RP, 85, NULL, NULL, path2);
        check(rc_ref == 1 && rc_strip == 1 && files_equal(path, path2), "NULL recipe equals plain encode");
    }

    /* 10. Recipe encode rejects widths beyond the internal strip buffer instead of overflowing it. */
    {
        enum { WIDE = 5632 + 2, WH = 8 };
        uint8_t* wide = calloc((size_t)WIDE * 2 * WH, 1);
        struct film_recipe recipe;
        memset(&recipe, 0, sizeof(recipe));
        recipe.active = 1;
        recipe.base_stock = 1;
        snprintf(path, sizeof(path), "%s/recipe_wide.jpg", dir);
        remove(path);
        int rc = wide ? encode_yuv422_recipe_to_jpeg(wide, WIDE, WH, WIDE * 2, 85, &recipe, NULL, path) : -1;
        check(rc == 0 && !file_exists(path), "recipe encode rejects width above strip buffer");
        free(wide);
    }

    printf("\n%d/%d assertions passed\n", total - failures, total);
    return failures;
}
