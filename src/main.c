#include "daemon.h"
#include "duration.h"
#include "ipc.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#ifndef TANGI_VERSION
#define TANGI_VERSION "0.1.0"
#endif

static void usage(FILE *out)
{
	fprintf(out,
"tangi — keep your computer awake (a tiny cross-platform caffeinate)\n"
"\n"
"Usage:\n"
"  tangi <duration>     start/reset: stay awake for the given time\n"
"  tangi on             stay awake indefinitely (until stopped)\n"
"  tangi add <duration> add more time to the current session\n"
"  tangi status         show remaining time (default with no args)\n"
"  tangi stop           release and allow sleep again\n"
"\n"
"Options:\n"
"  -d, --display        also keep the display awake\n"
"  -h, --help           show this help\n"
"  -v, --version        show version\n"
"\n"
"Duration: combine d/h/m/s, e.g. 45s, 30m, 2h, 1h30m, 1d12h.\n"
"          A bare number is seconds (90 == 90s).\n"
"\n"
"Examples:\n"
"  tangi 1h30m          stay awake for 90 minutes\n"
"  tangi add 10m        extend the current session by 10 minutes\n"
"  tangi -d 25m         stay awake 25 min and keep the screen on\n");
}

/* Parse an "R <indef> <remaining> <elapsed> <display>" line and print it. */
static void print_status_line(const char *line)
{
	int indef = 0, disp = 0;
	long rem = 0, el = 0;
	if (sscanf(line, "R %d %ld %ld %d", &indef, &rem, &el, &disp) != 4) {
		printf("tangi: unexpected reply from daemon\n");
		return;
	}

	char ebuf[32];
	duration_format(el, ebuf, sizeof(ebuf));
	const char *dtag = disp ? " \xC2\xB7 display kept awake" : "";

	if (indef) {
		printf("\xE2\x98\x95 tangi awake \xE2\x80\x94 indefinite\n");
		printf("   elapsed %s%s\n", ebuf, dtag);
	} else {
		char rbuf[32];
		duration_format(rem, rbuf, sizeof(rbuf));
		printf("\xE2\x98\x95 tangi awake \xE2\x80\x94 %s remaining\n", rbuf);
		printf("   elapsed %s%s\n", ebuf, dtag);
	}
}

/* Send one command to a running daemon and print the status it returns. */
static int talk_and_print(const char *cmd, int require_running)
{
	int fd = ipc_connect();
	if (fd < 0) {
		if (require_running)
			fprintf(stderr, "tangi: not running\n");
		else
			printf("tangi: not running\n");
		return require_running ? 1 : 0;
	}

	if (ipc_send_line(fd, cmd) != 0) {
		fprintf(stderr, "tangi: failed to send command\n");
		close(fd);
		return 1;
	}

	char reply[256];
	if (ipc_recv_line(fd, reply, sizeof(reply)) != 0) {
		fprintf(stderr, "tangi: no reply from daemon\n");
		close(fd);
		return 1;
	}
	close(fd);

	if (strncmp(reply, "ERR ", 4) == 0) {
		fprintf(stderr, "tangi: %s\n", reply + 4);
		return 1;
	}
	print_status_line(reply);
	return 0;
}

static int cmd_status(void)
{
	return talk_and_print("STATUS", 0);
}

static int cmd_add(long secs)
{
	char line[64];
	snprintf(line, sizeof(line), "ADD %ld", secs);
	return talk_and_print(line, 1);
}

static int cmd_stop(void)
{
	int fd = ipc_connect();
	if (fd < 0) {
		printf("tangi: not running\n");
		return 0;
	}
	ipc_send_line(fd, "STOP");
	char reply[64];
	ipc_recv_line(fd, reply, sizeof(reply));
	close(fd);
	printf("tangi: stopped, sleep allowed\n");
	return 0;
}

/* Fork+detach a daemon that owns the given listening socket. */
static int spawn_daemon(int listen_fd, long secs, int indefinite, int display)
{
	int p[2];
	if (pipe(p) != 0) {
		perror("tangi: pipe");
		return 1;
	}

	pid_t child = fork();
	if (child < 0) {
		perror("tangi: fork");
		return 1;
	}

	if (child == 0) {
		/* Intermediate child: detach from the controlling terminal. */
		setsid();
		pid_t grandchild = fork();
		if (grandchild < 0)
			_exit(1);
		if (grandchild == 0) {
			/* Daemon: owns listen_fd and the write end of the pipe. */
			close(p[0]);
			daemon_run(listen_fd, p[1], secs, indefinite, display);
			_exit(0); /* not reached */
		}
		_exit(0);
	}

	/* Launcher: keep only the read end and wait for the readiness report. */
	close(p[1]);
	close(listen_fd);
	waitpid(child, NULL, 0); /* reap the intermediate child */

	char line[256];
	ssize_t r = read(p[0], line, sizeof(line) - 1);
	close(p[0]);
	if (r <= 0) {
		fprintf(stderr, "tangi: daemon failed to start\n");
		return 1;
	}
	line[r] = '\0';
	/* strip newline */
	char *nl = strchr(line, '\n');
	if (nl) *nl = '\0';

	if (line[0] == 'E') {
		fprintf(stderr, "tangi: %s\n", line + 1);
		return 1;
	}
	return 0; /* 'O' -> ready */
}

static int start(long secs, int indefinite, int display)
{
	/* If a daemon is already running, just reset/extend it in place. */
	int fd = ipc_connect();
	if (fd >= 0) {
		char line[64];
		if (indefinite)
			snprintf(line, sizeof(line), "INDEF %d", display);
		else
			snprintf(line, sizeof(line), "SET %ld %d", secs, display);
		close(fd);
		return talk_and_print(line, 1);
	}

	/* No daemon: bind the socket here so bind errors surface synchronously. */
	char path[256];
	if (ipc_socket_path(path, sizeof(path)) != 0) {
		fprintf(stderr, "tangi: socket path too long\n");
		return 1;
	}
	unlink(path); /* clear any stale socket from a crashed daemon */

	int lf = socket(AF_UNIX, SOCK_STREAM, 0);
	if (lf < 0) {
		perror("tangi: socket");
		return 1;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	if (bind(lf, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror("tangi: bind");
		close(lf);
		return 1;
	}
	chmod(path, 0600);

	if (listen(lf, 8) != 0) {
		perror("tangi: listen");
		close(lf);
		unlink(path);
		return 1;
	}

	if (spawn_daemon(lf, secs, indefinite, display) != 0) {
		unlink(path);
		return 1;
	}

	/* spawn_daemon closed lf in this process; report status via a fresh query. */
	return talk_and_print("STATUS", 1);
}

int main(int argc, char **argv)
{
	int display = 0;
	const char *pos[8];
	int npos = 0;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0 || strcmp(a, "help") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0) {
			printf("tangi %s (%s)\n", TANGI_VERSION, platform_backend());
			return 0;
		}
		if (strcmp(a, "-d") == 0 || strcmp(a, "--display") == 0) {
			display = 1;
			continue;
		}
		if (npos < (int)(sizeof(pos) / sizeof(pos[0])))
			pos[npos++] = a;
	}

	if (npos == 0)
		return cmd_status();

	const char *cmd = pos[0];

	if (strcmp(cmd, "status") == 0 || strcmp(cmd, "st") == 0)
		return cmd_status();
	if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "off") == 0)
		return cmd_stop();
	if (strcmp(cmd, "on") == 0 || strcmp(cmd, "forever") == 0)
		return start(0, 1, display);

	if (strcmp(cmd, "add") == 0 || strcmp(cmd, "extend") == 0 || strcmp(cmd, "more") == 0) {
		if (npos < 2) {
			fprintf(stderr, "tangi: 'add' needs a duration, e.g. tangi add 10m\n");
			return 2;
		}
		long secs;
		if (duration_parse(pos[1], &secs) != 0 || secs <= 0) {
			fprintf(stderr, "tangi: invalid duration '%s'\n", pos[1]);
			return 2;
		}
		return cmd_add(secs);
	}

	/* Otherwise treat the first positional as a start duration. */
	long secs;
	if (duration_parse(cmd, &secs) != 0 || secs <= 0) {
		fprintf(stderr, "tangi: unknown command or invalid duration '%s'\n\n", cmd);
		usage(stderr);
		return 2;
	}
	return start(secs, 0, display);
}
