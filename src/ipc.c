#include "ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

int ipc_socket_path(char *buf, size_t n)
{
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (dir == NULL || dir[0] == '\0')
		dir = "/tmp";

	int len = snprintf(buf, n, "%s/tangi-%u.sock", dir, (unsigned)getuid());
	if (len < 0 || (size_t)len >= n)
		return -1;

	/* AF_UNIX paths are bounded by sun_path. */
	struct sockaddr_un probe;
	if ((size_t)len >= sizeof(probe.sun_path))
		return -1;

	return 0;
}

int ipc_connect(void)
{
	char path[256];
	if (ipc_socket_path(path, sizeof(path)) != 0)
		return -1;

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

int ipc_send_line(int fd, const char *line)
{
	size_t len = strlen(line);
	char buf[512];
	if (len + 1 >= sizeof(buf))
		return -1;
	memcpy(buf, line, len);
	buf[len] = '\n';

	size_t total = len + 1;
	size_t off = 0;
	while (off < total) {
		ssize_t w = write(fd, buf + off, total - off);
		if (w <= 0)
			return -1;
		off += (size_t)w;
	}
	return 0;
}

int ipc_recv_line(int fd, char *buf, size_t n)
{
	size_t off = 0;
	while (off + 1 < n) {
		char c;
		ssize_t r = read(fd, &c, 1);
		if (r == 0)
			break; /* EOF */
		if (r < 0)
			return -1;
		if (c == '\n')
			break;
		buf[off++] = c;
	}
	buf[off] = '\0';
	return off > 0 ? 0 : -1;
}
