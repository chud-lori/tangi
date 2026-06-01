#include "daemon.h"
#include "ipc.h"
#include "platform.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/socket.h>

struct state {
	time_t started;
	time_t deadline;
	int indefinite;
	int display;
	int lid;
	int active;   /* periodically declare user activity (keeps Slack online) */
	platform_inhibitor *inh;
};

/* Re-acquire the keep-awake lock if the display or lid preference changed. */
static void apply_locks(struct state *st, int want_display, int want_lid)
{
	if (want_display == st->display && want_lid == st->lid)
		return;
	char err[160];
	platform_inhibitor *fresh = platform_inhibit_start(want_display, want_lid, err, sizeof(err));
	if (fresh == NULL)
		return; /* keep the existing lock on failure */
	platform_inhibit_stop(st->inh);
	st->inh = fresh;
	st->display = want_display;
	st->lid = want_lid;
}

static volatile sig_atomic_t got_signal = 0;

static void on_signal(int sig)
{
	(void)sig;
	got_signal = 1;
}

static long remaining_secs(const struct state *st, time_t now)
{
	if (st->indefinite)
		return -1;
	long r = (long)(st->deadline - now);
	return r > 0 ? r : 0;
}

static void send_status(int fd, const struct state *st)
{
	time_t now = time(NULL);
	char line[128];
	snprintf(line, sizeof(line), "R %d %ld %ld %d %d %d",
	         st->indefinite ? 1 : 0,
	         remaining_secs(st, now),
	         (long)(now - st->started),
	         st->display ? 1 : 0,
	         st->lid ? 1 : 0,
	         st->active ? 1 : 0);
	ipc_send_line(fd, line);
}

/* Handle one client command. Returns 1 if the daemon should stop. */
static int handle_command(int conn_fd, struct state *st)
{
	char line[256];
	if (ipc_recv_line(conn_fd, line, sizeof(line)) != 0)
		return 0;

	if (strcmp(line, "STATUS") == 0) {
		send_status(conn_fd, st);
	} else if (strncmp(line, "ADD ", 4) == 0) {
		long secs = strtol(line + 4, NULL, 10);
		if (!st->indefinite && secs > 0)
			st->deadline += secs;
		send_status(conn_fd, st);
	} else if (strncmp(line, "SET ", 4) == 0) {
		long secs = 0;
		int want_disp = st->display;
		int want_lid = st->lid;
		int want_active = st->active;
		sscanf(line + 4, "%ld %d %d %d", &secs, &want_disp, &want_lid, &want_active);
		if (secs > 0) {
			st->indefinite = 0;
			st->started = time(NULL);
			st->deadline = st->started + secs;
		}
		st->active = want_active;
		apply_locks(st, want_disp, want_lid);
		send_status(conn_fd, st);
	} else if (strncmp(line, "INDEF", 5) == 0) {
		int want_disp = st->display;
		int want_lid = st->lid;
		int want_active = st->active;
		sscanf(line + 5, "%d %d %d", &want_disp, &want_lid, &want_active);
		st->indefinite = 1;
		st->started = time(NULL);
		st->active = want_active;
		apply_locks(st, want_disp, want_lid);
		send_status(conn_fd, st);
	} else if (strncmp(line, "LID ", 4) == 0) {
		int want_lid = st->lid;
		sscanf(line + 4, "%d", &want_lid);
		apply_locks(st, st->display, want_lid);
		send_status(conn_fd, st);
	} else if (strcmp(line, "STOP") == 0) {
		ipc_send_line(conn_fd, "BYE");
		return 1;
	} else {
		ipc_send_line(conn_fd, "ERR unknown command");
	}
	return 0;
}

static void detach_stdio(void)
{
	int devnull = open("/dev/null", O_RDWR);
	if (devnull >= 0) {
		dup2(devnull, STDIN_FILENO);
		dup2(devnull, STDOUT_FILENO);
		dup2(devnull, STDERR_FILENO);
		if (devnull > STDERR_FILENO)
			close(devnull);
	}
}

void daemon_run(int listen_fd, int handshake_fd, long initial_secs,
                int indefinite, int display, int lid, int active)
{
	char errbuf[160];
	errbuf[0] = '\0';

	platform_inhibitor *inh = platform_inhibit_start(display, lid, errbuf, sizeof(errbuf));
	if (inh == NULL) {
		char msg[200];
		snprintf(msg, sizeof(msg), "E%s\n",
		         errbuf[0] ? errbuf : "could not acquire keep-awake lock");
		(void)!write(handshake_fd, msg, strlen(msg));
		close(handshake_fd);
		_exit(1);
	}

	/* Report readiness to the launching process, then detach. */
	(void)!write(handshake_fd, "O\n", 2);
	close(handshake_fd);
	detach_stdio();

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);
	signal(SIGPIPE, SIG_IGN);

	struct state st;
	st.started = time(NULL);
	st.indefinite = indefinite;
	st.display = display;
	st.lid = lid;
	st.active = active;
	st.deadline = indefinite ? 0 : st.started + initial_secs;
	st.inh = inh;

	/* How often to declare user activity in active mode. Well under any chat
	 * app's "away" threshold so presence never lapses. */
	const long TAP_INTERVAL = 50;

	int stop = 0;
	while (!stop && !got_signal) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(listen_fd, &rfds);

		/* Wake at the earlier of: the deadline, or the next activity tap. */
		long timeout = -1; /* -1 => block indefinitely */
		if (!st.indefinite) {
			long r = remaining_secs(&st, time(NULL));
			if (r <= 0)
				break; /* timer expired */
			timeout = r;
		}
		if (st.active && (timeout < 0 || timeout > TAP_INTERVAL))
			timeout = TAP_INTERVAL;

		struct timeval tv;
		struct timeval *ptv = NULL;
		if (timeout >= 0) {
			tv.tv_sec = timeout;
			tv.tv_usec = 0;
			ptv = &tv;
		}

		int rc = select(listen_fd + 1, &rfds, NULL, NULL, ptv);
		if (rc < 0) {
			if (got_signal)
				break;
			continue;
		}
		if (rc == 0) {
			/* Timed out: either the session ended or it's time for a tap. */
			if (!st.indefinite && remaining_secs(&st, time(NULL)) <= 0)
				break;
			if (st.active)
				platform_user_active();
			continue;
		}

		if (FD_ISSET(listen_fd, &rfds)) {
			int conn = accept(listen_fd, NULL, NULL);
			if (conn < 0)
				continue;
			stop = handle_command(conn, &st);
			close(conn);
		}
	}

	platform_inhibit_stop(st.inh);

	char path[256];
	if (ipc_socket_path(path, sizeof(path)) == 0)
		unlink(path);
	close(listen_fd);
	_exit(0);
}
