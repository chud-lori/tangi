#ifndef TANGI_DAEMON_H
#define TANGI_DAEMON_H

/*
 * Run the tangi daemon. Takes ownership of an already bound+listening socket.
 *
 *   listen_fd     bound AF_UNIX listening socket
 *   handshake_fd  write end of a pipe; the daemon reports readiness on it
 *                 ("O\n" on success, "E<msg>\n" on failure) then closes it
 *   initial_secs  timer length in seconds (ignored if indefinite)
 *   indefinite    run until explicitly stopped
 *   display       also keep the display awake
 *
 * Never returns; calls _exit() when the timer ends or STOP is received.
 */
void daemon_run(int listen_fd, int handshake_fd, long initial_secs,
                int indefinite, int display);

#endif /* TANGI_DAEMON_H */
