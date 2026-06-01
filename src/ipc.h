#ifndef TANGI_IPC_H
#define TANGI_IPC_H

#include <stddef.h>

/*
 * Wire protocol (newline-terminated text over an AF_UNIX stream socket):
 *
 *   client -> daemon
 *     STATUS
 *     ADD <seconds>
 *     SET <seconds>          (reset the timer to <seconds> from now)
 *     INDEF                  (run until explicitly stopped)
 *     STOP
 *
 *   daemon -> client
 *     R <indefinite> <remaining> <elapsed> <display>
 *         indefinite: 0/1, remaining/elapsed: seconds (remaining=-1 if indefinite)
 *     BYE                    (acknowledges STOP; daemon then exits)
 *     ERR <message>
 */

/* Fill buf with the per-user socket path. Returns 0 on success, -1 if too long. */
int ipc_socket_path(char *buf, size_t n);

/* Connect to a running daemon. Returns a socket fd, or -1 if none is reachable. */
int ipc_connect(void);

/* Write a line (a trailing '\n' is added). Returns 0 on success, -1 on error. */
int ipc_send_line(int fd, const char *line);

/* Read one '\n'-terminated line into buf (newline stripped). Returns 0 / -1. */
int ipc_recv_line(int fd, char *buf, size_t n);

#endif /* TANGI_IPC_H */
