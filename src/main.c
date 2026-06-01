#include "daemon.h"
#include "duration.h"
#include "ipc.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#ifdef __APPLE__
#include <sys/file.h>      /* flock */
#include <mach-o/dyld.h>   /* _NSGetExecutablePath */
#endif

#ifndef TANGI_VERSION
#define TANGI_VERSION "0.1.0"
#endif

/*
 * Lid mode (macOS).
 *
 * macOS has no public way to keep a laptop awake with the lid closed via a
 * power assertion, so tangi disables lid-close sleep globally with
 * `pmset disablesleep 1` (admin) and restores it with `... 0`. Because the
 * daemon is unprivileged and detached (no terminal to prompt for a password
 * later), a separate root "lid guard" is started from the foreground — where
 * the sudo password prompt works — and then daemonizes.
 *
 * The guard ties its own lifetime to the session: it polls the daemon and
 * restores sleep when the session ends or lid intent goes back to 0, so sleep
 * is always re-enabled even though no privileged process is left waiting.
 */

#ifdef __APPLE__

/* Is a lid guard already running (per the pidfile)? */
static int lidguard_alive(const char *pidfile)
{
	FILE *f = fopen(pidfile, "r");
	if (f == NULL)
		return 0;
	long pid = 0;
	int got = fscanf(f, "%ld", &pid);
	fclose(f);
	if (got != 1 || pid <= 0)
		return 0;
	/* kill(pid, 0): 0 -> alive; EPERM -> alive but owned by root; else stale. */
	if (kill((pid_t)pid, 0) == 0 || errno == EPERM)
		return 1;
	return 0;
}

static int self_path(char *buf, uint32_t n)
{
	return _NSGetExecutablePath(buf, &n) == 0 ? 0 : -1;
}

/* fork/exec `pmset -a disablesleep <on>` (we already run as root here). */
static void run_pmset(int on)
{
	pid_t p = fork();
	if (p == 0) {
		int dn = open("/dev/null", O_RDWR);
		if (dn >= 0) { dup2(dn, STDOUT_FILENO); dup2(dn, STDERR_FILENO); }
		execl("/usr/bin/pmset", "pmset", "-a", "disablesleep", on ? "1" : "0", (char *)NULL);
		_exit(127);
	}
	if (p > 0)
		waitpid(p, NULL, 0);
}

static volatile sig_atomic_t lg_stop = 0;
static void lg_on_signal(int sig) { (void)sig; lg_stop = 1; }

/*
 * The root lid guard. Disables lid-close sleep, then watches the daemon and
 * restores it the moment the session ends or lid intent is cleared.
 * Internal entry point: `tangi __lidguard <socket> <pidfile>`.
 */
static int lidguard_main(const char *sock, const char *pidfile)
{
	/* Daemonize so the foreground sudo returns once the password is accepted. */
	if (fork() != 0) _exit(0);
	setsid();
	if (fork() != 0) _exit(0);

	/* Single-instance: hold an exclusive lock on the pidfile. */
	int lf = open(pidfile, O_RDWR | O_CREAT, 0644);
	if (lf < 0)
		_exit(1);
	if (flock(lf, LOCK_EX | LOCK_NB) != 0)
		_exit(0); /* another guard already running */

	char pidbuf[16];
	int n = snprintf(pidbuf, sizeof(pidbuf), "%d\n", (int)getpid());
	if (ftruncate(lf, 0) == 0)
		(void)!write(lf, pidbuf, (size_t)n);

	int dn = open("/dev/null", O_RDWR);
	if (dn >= 0) {
		dup2(dn, STDIN_FILENO); dup2(dn, STDOUT_FILENO); dup2(dn, STDERR_FILENO);
		if (dn > STDERR_FILENO) close(dn);
	}

	signal(SIGTERM, lg_on_signal);
	signal(SIGINT, lg_on_signal);
	signal(SIGHUP, lg_on_signal);

	run_pmset(1); /* disable lid-close (and all) sleep */

	int misses = 0;
	while (!lg_stop) {
		sleep(2);
		int fd = ipc_connect_path(sock);
		if (fd < 0) {
			/* Tolerate a transient miss before concluding the daemon is gone. */
			if (++misses >= 2)
				break;
			continue;
		}
		misses = 0;
		ipc_send_line(fd, "STATUS");
		char reply[128];
		int ok = ipc_recv_line(fd, reply, sizeof(reply));
		close(fd);
		if (ok == 0) {
			int indef, disp, lid;
			long rem, el;
			if (sscanf(reply, "R %d %ld %ld %d %d", &indef, &rem, &el, &disp, &lid) == 5
			    && lid == 0)
				break; /* lid intent cleared by a reset */
		}
	}

	run_pmset(0); /* always re-enable sleep */
	unlink(pidfile);
	_exit(0);
}

/*
 * Ensure a lid guard is running for the current session. Returns 0 if one is
 * (or already was) running, -1 if it could not be started (e.g. sudo declined).
 */
static int ensure_lidguard(void)
{
	char sock[256], pidfile[256], self[1024];
	if (ipc_socket_path(sock, sizeof(sock)) != 0 ||
	    ipc_lidfile_path(pidfile, sizeof(pidfile)) != 0 ||
	    self_path(self, sizeof(self)) != 0)
		return -1;

	if (lidguard_alive(pidfile))
		return 0;

	fprintf(stderr, "tangi: enabling lid mode needs admin \xE2\x80\x94 ");
	fflush(stderr);

	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execlp("sudo", "sudo", self, "__lidguard", sock, pidfile, (char *)NULL);
		_exit(127);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0))
		return -1;

	/* Give the guard a moment to write its pidfile, then confirm. */
	usleep(150000);
	return lidguard_alive(pidfile) ? 0 : -1;
}

#endif /* __APPLE__ */

static void usage(FILE *out)
{
	fprintf(out,
"tangi — keep your computer awake for as long as you want\n"
"\n"
"Usage:\n"
"  tangi <duration>     start/reset: stay awake for the given time\n"
"  tangi on             stay awake indefinitely (until stopped)\n"
"  tangi add <duration> add more time to the current session\n"
"  tangi lid <duration> stay awake even with the lid closed\n"
"  tangi status         show remaining time (default with no args)\n"
"  tangi stop           release and allow sleep again\n"
"\n"
"Options:\n"
"  -d, --display        also keep the display awake\n"
"  -l, --lid            also stay awake when the lid is closed\n"
"  -h, --help           show this help\n"
"  -v, --version        show version\n"
"\n"
"Duration: combine d/h/m/s, e.g. 45s, 30m, 2h, 1h30m, 1d12h.\n"
"          A bare number is seconds (90 == 90s).\n"
"\n"
"Lid mode keeps running with the lid shut. On macOS it needs admin (you'll be\n"
"asked for your password) and may let the machine run warm with the lid closed.\n"
"It is turned off automatically when the session stops.\n"
"\n"
"Examples:\n"
"  tangi 1h30m          stay awake for 90 minutes\n"
"  tangi add 10m        extend the current session by 10 minutes\n"
"  tangi -d 25m         stay awake 25 min and keep the screen on\n"
"  tangi lid 1h         keep working for an hour with the lid closed\n");
}

/* Parse an "R <indef> <remaining> <elapsed> <display> <lid>" line and print it. */
static void print_status_line(const char *line)
{
	int indef = 0, disp = 0, lid = 0;
	long rem = 0, el = 0;
	if (sscanf(line, "R %d %ld %ld %d %d", &indef, &rem, &el, &disp, &lid) < 4) {
		printf("tangi: unexpected reply from daemon\n");
		return;
	}

#ifdef __APPLE__
	/* On macOS the truth about lid mode is whether the guard is actually up. */
	char pidfile[256];
	if (ipc_lidfile_path(pidfile, sizeof(pidfile)) == 0)
		lid = lidguard_alive(pidfile);
#endif

	char ebuf[32];
	duration_format(el, ebuf, sizeof(ebuf));

	/* Build the trailing detail tag: "· display kept awake · lid closed ok". */
	char tag[80];
	tag[0] = '\0';
	if (disp)
		strncat(tag, " \xC2\xB7 display kept awake", sizeof(tag) - strlen(tag) - 1);
	if (lid)
		strncat(tag, " \xC2\xB7 lid-close sleep off", sizeof(tag) - strlen(tag) - 1);

	if (indef) {
		printf("\xE2\x98\x95 tangi awake \xE2\x80\x94 indefinite\n");
		printf("   elapsed %s%s\n", ebuf, tag);
	} else {
		char rbuf[32];
		duration_format(rem, rbuf, sizeof(rbuf));
		printf("\xE2\x98\x95 tangi awake \xE2\x80\x94 %s remaining\n", rbuf);
		printf("   elapsed %s%s\n", ebuf, tag);
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
static int spawn_daemon(int listen_fd, long secs, int indefinite, int display, int lid)
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
			daemon_run(listen_fd, p[1], secs, indefinite, display, lid);
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

/* On macOS, start the root lid guard when lid mode is requested. */
static void arrange_lid(int lid)
{
#ifdef __APPLE__
	if (!lid)
		return;
	if (ensure_lidguard() != 0)
		fprintf(stderr, "tangi: could not enable lid mode "
		                "(admin declined); lid-close will still sleep\n");
#else
	(void)lid; /* Linux handles the lid via the logind inhibitor in the daemon */
#endif
}

static int start(long secs, int indefinite, int display, int lid)
{
	/* If a daemon is already running, just reset/extend it in place. */
	int fd = ipc_connect();
	if (fd >= 0) {
		char line[64];
		if (indefinite)
			snprintf(line, sizeof(line), "INDEF %d %d", display, lid);
		else
			snprintf(line, sizeof(line), "SET %ld %d %d", secs, display, lid);
		close(fd);
		arrange_lid(lid);
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

	if (spawn_daemon(lf, secs, indefinite, display, lid) != 0) {
		unlink(path);
		return 1;
	}

	/* Daemon is up and listening; now bring up the lid guard if asked. */
	arrange_lid(lid);

	/* spawn_daemon closed lf in this process; report status via a fresh query. */
	return talk_and_print("STATUS", 1);
}

int main(int argc, char **argv)
{
#ifdef __APPLE__
	/* Internal: root lid guard, launched via sudo. Not for direct use. */
	if (argc == 4 && strcmp(argv[1], "__lidguard") == 0)
		return lidguard_main(argv[2], argv[3]);
#endif

	int display = 0;
	int lid = 0;
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
		if (strcmp(a, "-l") == 0 || strcmp(a, "--lid") == 0) {
			lid = 1;
			continue;
		}
		if (npos < (int)(sizeof(pos) / sizeof(pos[0])))
			pos[npos++] = a;
	}

	if (npos == 0) {
		if (lid || display) {
			fprintf(stderr, "tangi: -d/-l need a duration, e.g. tangi -l 1h\n");
			return 2;
		}
		return cmd_status();
	}

	const char *cmd = pos[0];

	if (strcmp(cmd, "status") == 0 || strcmp(cmd, "st") == 0)
		return cmd_status();
	if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "off") == 0)
		return cmd_stop();
	if (strcmp(cmd, "on") == 0 || strcmp(cmd, "forever") == 0)
		return start(0, 1, display, lid);

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

	/* `tangi lid <duration|on>` is shorthand for `tangi -l <duration|on>`. */
	if (strcmp(cmd, "lid") == 0) {
		if (npos < 2) {
			fprintf(stderr, "tangi: 'lid' needs a duration or 'on', e.g. tangi lid 1h\n");
			return 2;
		}
		if (strcmp(pos[1], "on") == 0 || strcmp(pos[1], "forever") == 0)
			return start(0, 1, display, 1);
		long secs;
		if (duration_parse(pos[1], &secs) != 0 || secs <= 0) {
			fprintf(stderr, "tangi: invalid duration '%s'\n", pos[1]);
			return 2;
		}
		return start(secs, 0, display, 1);
	}

	/* Otherwise treat the first positional as a start duration. */
	long secs;
	if (duration_parse(cmd, &secs) != 0 || secs <= 0) {
		fprintf(stderr, "tangi: unknown command or invalid duration '%s'\n\n", cmd);
		usage(stderr);
		return 2;
	}
	return start(secs, 0, display, lid);
}
