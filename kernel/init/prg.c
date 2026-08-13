#include "kernel/init/prg.h"

/* MYPR v1 parsing lives in kernel/proc/program_image.c.  This translation
 * unit intentionally contains only the shared format assertions from prg.h;
 * historical in-kernel ELF and relocation loaders were never valid runtime
 * paths and have been removed. */
