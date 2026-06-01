#ifndef TANGI_IPC_H
#define TANGI_IPC_H

#include <stddef.h>

/*
 * Wire protocol (newline-terminated text over an AF_UNIX stream socket):
 *
 *   client -> daemon
 *     STATUS
 *     ADD <seconds>
 *     SET <seconds> [display] [lid]   (reset the timer to <seconds> from now)
 *     INDEF [display] [lid]           (run until explicitly stopped)
 *     LID <0|1>                       (set lid-mode intent without resetting)
 *     STOP
 *
 *   daemon -> client
 *     R <indefinite> <remaining> <elapsed> <display> <lid>
 *         indefinite: 0/1, remaining/elapsed: seconds (remaining=-1 if indefinite)
 *     BYE                    (acknowledges STOP; daemon then exits)
 *     ERR <message>
 */

/* Fill buf with the per-user socket path. Returns 0 on success, -1 if too long. */
int ipc_socket_path(char *buf, size_t n);

/* Fill buf with the per-user lid-guard pidfile path. Returns 0 / -1. */
int ipc_lidfile_path(char *buf, size_t n);

/* Connect to a running daemon. Returns a socket fd, or -1 if none is reachable. */
int ipc_connect(void);

/* Connect to a daemon at an explicit socket path (used by the root lid guard,
 * whose getuid() differs from the launching user's). Returns fd, or -1. */
int ipc_connect_path(const char *path);

/* Write a line (a trailing '\n' is added). Returns 0 on success, -1 on error. */
int ipc_send_line(int fd, const char *line);

/* Read one '\n'-terminated line into buf (newline stripped). Returns 0 / -1. */
int ipc_recv_line(int fd, char *buf, size_t n);

#endif /* TANGI_IPC_H */
