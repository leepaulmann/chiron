#include "pe.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>

#define RD16(p) (*(const uint16_t *)(p))
#define RD32(p) (*(const uint32_t *)(p))

// Offsets into the PE32 optional header.
enum {
    OPT_ENTRY = 16,
    OPT_IMAGE_BASE = 28,
    OPT_SIZE_OF_IMAGE = 56,
    OPT_SIZE_OF_HEADERS = 60,
    OPT_NUM_DIRS = 92,
    OPT_DIRS = 96,
};

enum { DIR_EXPORT = 0, DIR_IMPORT = 1, DIR_RELOC = 5, DIR_TLS = 9 };

static int fail(char *err, size_t errlen, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
    return -1;
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n > 0 ? n : 1);
    if (buf && fread(buf, 1, n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = n;
    return buf;
}

static void apply_relocs(uint8_t *base, uint32_t rva, uint32_t size, uint32_t delta) {
    uint8_t *p = base + rva, *end = p + size;
    while (p + 8 <= end) {
        uint32_t page = RD32(p), block = RD32(p + 4);
        if (block < 8) break;
        for (uint32_t i = 8; i + 2 <= block; i += 2) {
            uint16_t e = RD16(p + i);
            if ((e >> 12) == 3) *(uint32_t *)(base + page + (e & 0xfff)) += delta;
            // Type 0 is padding; nothing else appears in 32-bit images.
        }
        p += block;
    }
}

static int bind_imports(struct pe_image *img, uint32_t rva, pe_resolver resolve,
                        char *err, size_t errlen) {
    uint8_t *base = img->base;
    for (uint8_t *d = base + rva;; d += 20) {
        uint32_t ilt = RD32(d), name_rva = RD32(d + 12), iat = RD32(d + 16);
        if (!name_rva) break;
        const char *dll = (const char *)base + name_rva;
        uint32_t *src = (uint32_t *)(base + (ilt ? ilt : iat));
        uint32_t *dst = (uint32_t *)(base + iat);
        for (; *src; src++, dst++) {
            void *fn;
            if (*src & 0x80000000u)
                fn = resolve(dll, NULL, *src & 0xffff);
            else
                fn = resolve(dll, (const char *)base + *src + 2, 0);
            if (!fn) return fail(err, errlen, "%s: unresolved import from %s", img->name, dll);
            *dst = (uint32_t)fn;
        }
    }
    return 0;
}

int pe_load(const char *host_path, struct pe_image *img, pe_resolver resolve,
            char *err, size_t errlen) {
    memset(img, 0, sizeof(*img));
    const char *slash = strrchr(host_path, '/');
    snprintf(img->name, sizeof(img->name), "%s", slash ? slash + 1 : host_path);

    size_t len;
    uint8_t *file = read_file(host_path, &len);
    if (!file) return fail(err, errlen, "cannot read %s", host_path);

    int rc = -1;
    if (len < 0x40 || RD16(file) != 0x5a4d) {
        fail(err, errlen, "%s: not an MZ executable", img->name);
        goto out;
    }
    uint32_t pe = RD32(file + 0x3c);
    if (pe + 24 > len || RD32(file + pe) != 0x4550) {
        fail(err, errlen, "%s: no PE header", img->name);
        goto out;
    }
    const uint8_t *fh = file + pe + 4;
    const uint8_t *opt = fh + 20;
    if (RD16(fh) != 0x14c || RD16(opt) != 0x10b) {
        fail(err, errlen, "%s: not a 32-bit x86 PE image", img->name);
        goto out;
    }
    uint16_t nsec = RD16(fh + 2), optsz = RD16(fh + 16);
    img->is_dll = (RD16(fh + 18) & 0x2000) != 0;
    img->entry_rva = RD32(opt + OPT_ENTRY);
    img->pref_base = RD32(opt + OPT_IMAGE_BASE);
    img->size = RD32(opt + OPT_SIZE_OF_IMAGE);
    uint32_t hdr_size = RD32(opt + OPT_SIZE_OF_HEADERS);
    uint32_t ndirs = RD32(opt + OPT_NUM_DIRS);
#define DIR_RVA(i) ((i) < ndirs ? RD32(opt + OPT_DIRS + (i) * 8) : 0)
#define DIR_SIZE(i) ((i) < ndirs ? RD32(opt + OPT_DIRS + (i) * 8 + 4) : 0)

    // Everything is mapped RWX: terranx.exe patches its own code (_SELFMOD)
    // and later our hook layer writes jumps into .text.
    const int prot = PROT_READ | PROT_WRITE | PROT_EXEC;
    uint8_t *base = MAP_FAILED;
    if (!getenv("CHIRON_FORCE_RELOC"))
        base = mmap((void *)img->pref_base, img->size, prot,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (base == MAP_FAILED) {
        if (!DIR_RVA(DIR_RELOC)) {
            fail(err, errlen, "%s: preferred base 0x%x unavailable and no relocations",
                 img->name, img->pref_base);
            goto out;
        }
        base = mmap(NULL, img->size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED) {
            fail(err, errlen, "%s: cannot map %u bytes", img->name, img->size);
            goto out;
        }
    }
    img->base = base;

    memcpy(base, file, hdr_size < len ? hdr_size : len);
    const uint8_t *sec = opt + optsz;
    for (int i = 0; i < nsec; i++, sec += 40) {
        uint32_t vsize = RD32(sec + 8), va = RD32(sec + 12);
        uint32_t rawsize = RD32(sec + 16), rawoff = RD32(sec + 20);
        if (vsize && rawsize > vsize) rawsize = vsize;
        if (va + rawsize > img->size || rawoff + rawsize > len) {
            fail(err, errlen, "%s: section %d out of bounds", img->name, i);
            goto out;
        }
        memcpy(base + va, file + rawoff, rawsize);  // the rest stays zero-filled
    }

    if ((uint32_t)base != img->pref_base)
        apply_relocs(base, DIR_RVA(DIR_RELOC), DIR_SIZE(DIR_RELOC),
                     (uint32_t)base - img->pref_base);
    *(uint32_t *)(base + pe + 4 + 20 + OPT_IMAGE_BASE) = (uint32_t)base;

    if (DIR_RVA(DIR_IMPORT) && bind_imports(img, DIR_RVA(DIR_IMPORT), resolve, err, errlen))
        goto out;

    img->export_rva = DIR_RVA(DIR_EXPORT);
    img->export_size = DIR_SIZE(DIR_EXPORT);
    img->tls_rva = DIR_RVA(DIR_TLS);
    rc = 0;
out:
    if (rc && img->base) {
        munmap(img->base, img->size);
        img->base = NULL;
    }
    free(file);
    return rc;
}

void *pe_find_export(const struct pe_image *img, const char *name, int ordinal) {
    if (!img->export_rva) return NULL;
    const uint8_t *base = img->base, *dir = base + img->export_rva;
    uint32_t ord_base = RD32(dir + 16), nfuncs = RD32(dir + 20), nnames = RD32(dir + 24);
    const uint32_t *funcs = (const uint32_t *)(base + RD32(dir + 28));
    const uint32_t *names = (const uint32_t *)(base + RD32(dir + 32));
    const uint16_t *ords = (const uint16_t *)(base + RD32(dir + 36));

    uint32_t idx = UINT32_MAX;
    if (name) {
        for (uint32_t i = 0; i < nnames; i++)
            if (!strcmp((const char *)base + names[i], name)) {
                idx = ords[i];
                break;
            }
    } else if ((uint32_t)ordinal >= ord_base) {
        idx = ordinal - ord_base;
    }
    if (idx >= nfuncs || !funcs[idx]) return NULL;
    // Forwarded exports point back into the export directory; we have none
    // to forward to, so treat them as missing.
    if (funcs[idx] >= img->export_rva && funcs[idx] < img->export_rva + img->export_size)
        return NULL;
    return (void *)(base + funcs[idx]);
}
