#ifndef TANGI_PLATFORM_H
#define TANGI_PLATFORM_H

#include <stddef.h>

/* Opaque handle holding an OS-level "stay awake" lock. */
typedef struct platform_inhibitor platform_inhibitor;

/*
 * Acquire a keep-awake lock.
 *
 *   display != 0  also prevent the display from sleeping (where supported).
 *   lid     != 0  also prevent lid-close from sleeping, where the lock layer
 *                 itself can do it (Linux logind). On macOS the lid is handled
 *                 out-of-band by the lid guard, so this flag is ignored here.
 *
 * Returns a handle on success, or NULL on failure (writing a human-readable
 * reason into errbuf, which may be empty).
 */
platform_inhibitor *platform_inhibit_start(int display, int lid, char *errbuf, size_t errlen);

/* Release the lock and free the handle. Safe to call with NULL. */
void platform_inhibit_stop(platform_inhibitor *h);

/* Short name of the backend in use, e.g. "IOKit" or "logind". */
const char *platform_backend(void);

#endif /* TANGI_PLATFORM_H */
