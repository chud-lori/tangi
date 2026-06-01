#include "platform.h"

#include <stdlib.h>
#include <string.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

struct platform_inhibitor {
	IOPMAssertionID sys;        /* prevent system idle sleep */
	IOPMAssertionID disp;       /* prevent display idle sleep (optional) */
	int has_disp;
};

static int create_assertion(CFStringRef type, const char *reason, IOPMAssertionID *out)
{
	CFStringRef r = CFStringCreateWithCString(kCFAllocatorDefault, reason,
	                                          kCFStringEncodingUTF8);
	IOReturn rc = IOPMAssertionCreateWithName(type, kIOPMAssertionLevelOn, r, out);
	if (r != NULL)
		CFRelease(r);
	return rc == kIOReturnSuccess ? 0 : -1;
}

platform_inhibitor *platform_inhibit_start(int display, int lid, char *errbuf, size_t errlen)
{
	(void)lid; /* lid-close on macOS is handled by the lid guard (pmset), not IOKit */

	struct platform_inhibitor *h = calloc(1, sizeof(*h));
	if (h == NULL) {
		if (errlen) snprintf(errbuf, errlen, "out of memory");
		return NULL;
	}

	if (create_assertion(kIOPMAssertionTypePreventUserIdleSystemSleep,
	                     "tangi: keeping system awake", &h->sys) != 0) {
		if (errlen) snprintf(errbuf, errlen, "IOPMAssertionCreate failed");
		free(h);
		return NULL;
	}

	if (display) {
		if (create_assertion(kIOPMAssertionTypePreventUserIdleDisplaySleep,
		                    "tangi: keeping display awake", &h->disp) == 0)
			h->has_disp = 1;
		/* If the display assertion fails we still keep the system awake. */
	}

	return h;
}

void platform_inhibit_stop(platform_inhibitor *h)
{
	if (h == NULL)
		return;
	if (h->has_disp)
		IOPMAssertionRelease(h->disp);
	IOPMAssertionRelease(h->sys);
	free(h);
}

const char *platform_backend(void)
{
	return "IOKit";
}
