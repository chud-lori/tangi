#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Two Linux backends:
 *
 *   HAVE_SYSTEMD  -> talk to systemd-logind over D-Bus (sd_bus) and hold the
 *                    inhibitor file descriptor it returns. No child process.
 *
 *   otherwise     -> fork/exec `systemd-inhibit ... sleep infinity` and hold
 *                    the lock for the lifetime of that child. Zero link-time
 *                    dependencies; needs the systemd-inhibit binary at runtime.
 */

#ifdef HAVE_SYSTEMD

#include <systemd/sd-bus.h>

struct platform_inhibitor {
	sd_bus *bus;
	int fd; /* the inhibitor lock fd; closing it releases the lock */
};

platform_inhibitor *platform_inhibit_start(int display, char *errbuf, size_t errlen)
{
	const char *what = display ? "idle:sleep:handle-lid-switch" : "idle:sleep";

	struct platform_inhibitor *h = calloc(1, sizeof(*h));
	if (h == NULL) {
		if (errlen) snprintf(errbuf, errlen, "out of memory");
		return NULL;
	}
	h->fd = -1;

	int rc = sd_bus_default_system(&h->bus);
	if (rc < 0) {
		if (errlen) snprintf(errbuf, errlen, "cannot connect to system bus: %s", strerror(-rc));
		free(h);
		return NULL;
	}

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	rc = sd_bus_call_method(h->bus,
	                        "org.freedesktop.login1",
	                        "/org/freedesktop/login1",
	                        "org.freedesktop.login1.Manager",
	                        "Inhibit",
	                        &err, &reply,
	                        "ssss", what, "tangi", "Keeping system awake", "block");
	if (rc < 0) {
		if (errlen) snprintf(errbuf, errlen, "logind Inhibit failed: %s",
		                     err.message ? err.message : strerror(-rc));
		sd_bus_error_free(&err);
		sd_bus_unref(h->bus);
		free(h);
		return NULL;
	}

	int lock_fd = -1;
	rc = sd_bus_message_read(reply, "h", &lock_fd);
	if (rc < 0 || lock_fd < 0) {
		if (errlen) snprintf(errbuf, errlen, "no inhibitor fd returned");
		sd_bus_message_unref(reply);
		sd_bus_error_free(&err);
		sd_bus_unref(h->bus);
		free(h);
		return NULL;
	}

	/* The fd is owned by the message; dup it so it survives unref. */
	h->fd = dup(lock_fd);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);

	if (h->fd < 0) {
		if (errlen) snprintf(errbuf, errlen, "dup() failed");
		sd_bus_unref(h->bus);
		free(h);
		return NULL;
	}

	return h;
}

void platform_inhibit_stop(platform_inhibitor *h)
{
	if (h == NULL)
		return;
	if (h->fd >= 0)
		close(h->fd);
	if (h->bus != NULL)
		sd_bus_unref(h->bus);
	free(h);
}

const char *platform_backend(void)
{
	return "logind";
}

#else /* !HAVE_SYSTEMD -- subprocess fallback */

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

struct platform_inhibitor {
	pid_t pid;
};

platform_inhibitor *platform_inhibit_start(int display, char *errbuf, size_t errlen)
{
	const char *what = display ? "idle:sleep:handle-lid-switch" : "idle:sleep";

	struct platform_inhibitor *h = calloc(1, sizeof(*h));
	if (h == NULL) {
		if (errlen) snprintf(errbuf, errlen, "out of memory");
		return NULL;
	}

	pid_t pid = fork();
	if (pid < 0) {
		if (errlen) snprintf(errbuf, errlen, "fork failed");
		free(h);
		return NULL;
	}

	if (pid == 0) {
		/* Child: hold the lock until killed. */
		char what_arg[64];
		snprintf(what_arg, sizeof(what_arg), "--what=%s", what);
		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
		}
		execlp("systemd-inhibit", "systemd-inhibit", what_arg,
		       "--who=tangi", "--why=Keeping system awake", "--mode=block",
		       "sleep", "infinity", (char *)NULL);
		_exit(127); /* exec failed */
	}

	/* Parent: give the child a moment, then make sure it didn't exec-fail. */
	usleep(50000);
	int status;
	if (waitpid(pid, &status, WNOHANG) == pid) {
		if (errlen) snprintf(errbuf, errlen, "systemd-inhibit not available");
		free(h);
		return NULL;
	}

	h->pid = pid;
	return h;
}

void platform_inhibit_stop(platform_inhibitor *h)
{
	if (h == NULL)
		return;
	if (h->pid > 0) {
		kill(h->pid, SIGTERM);
		waitpid(h->pid, NULL, 0);
	}
	free(h);
}

const char *platform_backend(void)
{
	return "systemd-inhibit";
}

#endif /* HAVE_SYSTEMD */
