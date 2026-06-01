#ifndef TANGI_DURATION_H
#define TANGI_DURATION_H

#include <stddef.h>

/*
 * Parse a human duration string into seconds.
 *
 * Accepts a sequence of <number><unit> tokens with units d/h/m/s, e.g.
 * "1h30m", "90m", "45s", "2h", "1d12h". A bare integer with no unit is
 * treated as seconds ("3600" == "3600s").
 *
 * Returns 0 on success and stores the total in *out_secs.
 * Returns -1 on a malformed string or overflow.
 */
int duration_parse(const char *s, long *out_secs);

/*
 * Format a duration in seconds into a compact string like "1h30m5s".
 * Only the largest non-zero units are shown; seconds are always shown
 * when the total is below a minute. Writes at most n bytes (NUL-terminated).
 */
void duration_format(long secs, char *buf, size_t n);

#endif /* TANGI_DURATION_H */
