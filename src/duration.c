#include "duration.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>

int duration_parse(const char *s, long *out_secs)
{
	if (s == NULL || *s == '\0')
		return -1;

	long total = 0;
	int saw_token = 0;
	const char *p = s;

	while (*p != '\0') {
		if (isspace((unsigned char)*p)) {
			p++;
			continue;
		}
		if (!isdigit((unsigned char)*p))
			return -1;

		/* Read the numeric part with overflow checking. */
		long value = 0;
		while (isdigit((unsigned char)*p)) {
			int digit = *p - '0';
			if (value > (LONG_MAX - digit) / 10)
				return -1;
			value = value * 10 + digit;
			p++;
		}

		/* Read an optional unit. Missing unit means seconds. */
		long mult = 1;
		switch (*p) {
		case 'd': case 'D': mult = 86400; p++; break;
		case 'h': case 'H': mult = 3600;  p++; break;
		case 'm': case 'M': mult = 60;    p++; break;
		case 's': case 'S': mult = 1;     p++; break;
		case '\0':
		default:
			/* bare number or trailing token: seconds */
			break;
		}

		if (value > LONG_MAX / mult)
			return -1;
		long secs = value * mult;
		if (total > LONG_MAX - secs)
			return -1;
		total += secs;
		saw_token = 1;
	}

	if (!saw_token)
		return -1;

	*out_secs = total;
	return 0;
}

void duration_format(long secs, char *buf, size_t n)
{
	if (n == 0)
		return;
	if (secs < 0) {
		snprintf(buf, n, "indefinite");
		return;
	}

	long d = secs / 86400; secs %= 86400;
	long h = secs / 3600;  secs %= 3600;
	long m = secs / 60;    secs %= 60;
	long s = secs;

	/*
	 * Show the largest unit and the next one only when it's non-zero, so it
	 * stays readable: at most two units, spaced, with noisy trailing zeros
	 * dropped. e.g. 8h42m49s -> "8h 42m", 2h -> "2h", 5m30s -> "5m 30s".
	 */
	if (d > 0)
		h > 0 ? snprintf(buf, n, "%ldd %ldh", d, h) : snprintf(buf, n, "%ldd", d);
	else if (h > 0)
		m > 0 ? snprintf(buf, n, "%ldh %ldm", h, m) : snprintf(buf, n, "%ldh", h);
	else if (m > 0)
		s > 0 ? snprintf(buf, n, "%ldm %lds", m, s) : snprintf(buf, n, "%ldm", m);
	else
		snprintf(buf, n, "%lds", s);
}
