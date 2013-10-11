/**
 * systemd-echo - An ECHO server for systemd.
 * Copyright (C) 2013 Changli Gao <xiaosuo@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <ctype.h>

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#include <netinet/in.h>

#include <systemd/sd-daemon.h>

#define CONF_PATH SYSCONFDIR "/" PACKAGE_NAME ".conf"

#define pr_err(fmt, ...) \
	fprintf(stderr, SD_ERR fmt, ##__VA_ARGS__)

struct iobuf {
	char	*head;
	char	*data;
	size_t	len;
};

static struct iobuf *iob;
static size_t iob_size;

static int backlog = 10;

static char *str_lstrip(char *str)
{
	while (isspace(*str))
		str++;

	return str;
}

static char *str_rstrip(char *str)
{
	char *end = str + strlen(str) - 1;

	while (end >= str && isspace(*end))
		end--;

	return str;
}

static char *str_strip(char *str)
{
	return str_rstrip(str_lstrip(str));
}

static int load_conf(void)
{
	FILE *fh = fopen(CONF_PATH, "r");
	char buf[LINE_MAX];
	char *ptr, *name, *value;

	if (!fh)
		goto err;
	while (fgets(buf, sizeof(buf), fh)) {
		name = str_strip(buf);
		/* Skip empty lines */
		if (!*name)
			continue;
		ptr = strchr(name, '=');
		if (!ptr)
			goto err2;
		*ptr++ = '\0';
		name = str_rstrip(name);
		value = str_lstrip(ptr);
		if (strcmp(name, "Backlog") == 0) {
			backlog = atoi(value);
			if (backlog <= 0)
				goto err2;
		} else {
			goto err2;
		}
	}
	if (ferror(fh) || !feof(fh))
		goto err2;
	fclose(fh);

	return 0;
err2:
	fclose(fh);
err:
	return -1;
}

int main(int argc, char *argv[])
{
	int epfd, n_fds, signal_fd, listen_fd = -1, dgram_fd = -1,
	    timer_fd = -1, fd;
	sigset_t mask;
	struct epoll_event ev;
	char buf[4096];

	if (getppid() != 1) {
		pr_err("This program should be invoked by init only\n");
		goto err;
	}

	if (load_conf()) {
		pr_err("Failed to load the configuration file\n");
		goto err;
	}

	if (argc != 1) {
		pr_err("No argument is supported\n");
		goto err;
	}

	epfd = epoll_create1(EPOLL_CLOEXEC);
	if (epfd < 0) {
		pr_err("Failed to create the epoll fd\n");
		goto err;
	}

	n_fds = sd_listen_fds(true);
	if (n_fds < 0) {
		pr_err("Failed to read listening file descriptors from "
		       "environment: %s\n", strerror(errno));
		goto err;
	}
	if (n_fds > 2) {
		pr_err("Only 2 fds are allowed\n");
		goto err;
	}
	for (fd = SD_LISTEN_FDS_START; fd < SD_LISTEN_FDS_START + n_fds; fd++) {
		int type = SOCK_STREAM;
		int retval = sd_is_socket_inet(fd, AF_INET6, type, 1, 7);

		if (!retval) {
			type = SOCK_DGRAM;
			retval = sd_is_socket_inet(fd, AF_INET6, type, -1, 7);
			if (!retval) {
				pr_err("The passed in socket is wrong\n");
				goto err;
			}
		}

		if (retval < 0) {
			pr_err("Failed to determine the socket type: %s\n",
			       strerror(-retval));
			goto err;
		} else if (type == SOCK_STREAM) {
			if (listen_fd >= 0) {
				pr_err("Only one stream socket is allowed\n");
				goto err;
			}
			listen_fd = fd;
		} else {
			if (dgram_fd >= 0) {
				pr_err("Only one dgram socket is allowed\n");
				goto err;
			}
			dgram_fd = fd;
		}
	}

	if (listen_fd < 0) {
		struct sockaddr_in6 addr;
		int reuse = 1;

		listen_fd = socket(PF_INET6,
				SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
		if (listen_fd < 0) {
			pr_err("Failed to create the listen socket\n");
			goto err;
		}
		if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
					sizeof(reuse))) {
			pr_err("Failed to set the reuseaddr option to the "
			       "listen socket\n");
			goto err;
		}
		memset(&addr, 0, sizeof(addr));
		addr.sin6_family = AF_INET6;
		addr.sin6_port = htons(7);
		if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr))) {
			pr_err("Failed to bind the listen socket\n");
			goto err;
		}
		if (listen(listen_fd, backlog)) {
			pr_err("Failed to listen on the listen socket\n");
			goto err;
		}
	}
	ev.events = EPOLLIN;
	ev.data.fd = listen_fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev)) {
		pr_err("Failed to monitor the listen socket\n");
		goto err;
	}

	if (dgram_fd < 0) {
		struct sockaddr_in6 addr;

		dgram_fd = socket(PF_INET6,
				SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
		if (dgram_fd < 0) {
			pr_err("Failed to create the dgram socket\n");
			goto err;
		}
		memset(&addr, 0, sizeof(addr));
		addr.sin6_family = AF_INET6;
		addr.sin6_port = htons(7);
		if (bind(dgram_fd, (struct sockaddr *)&addr, sizeof(addr))) {
			pr_err("Failed to bind the dgram socket\n");
			goto err;
		}
	}
	ev.events = EPOLLIN;
	ev.data.fd = dgram_fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, dgram_fd, &ev)) {
		pr_err("Failed to monitor the dgram socket\n");
		goto err;
	}

	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM); /* terminate the program */
	sigaddset(&mask, SIGHUP); /* reload the configuration */
	sigprocmask(SIG_BLOCK, &mask, NULL);
	signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
	if (signal_fd < 0) {
		pr_err("Failed to create the signal fd.\n");
		goto err;
	}
	ev.events = EPOLLIN;
	ev.data.fd = signal_fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, signal_fd, &ev)) {
		pr_err("Failed to monitor the signal fd.\n");
		goto err;
	}

	if (getenv("WATCHDOG_USEC")) {
		struct itimerspec its;
		unsigned long long usec;

		timer_fd = timerfd_create(CLOCK_MONOTONIC,
				TFD_NONBLOCK | TFD_CLOEXEC);
		if (timer_fd < 0) {
			pr_err("Failed to create the watchdog timer fd.\n");
			goto err;
		}
		usec = strtoull(getenv("WATCHDOG_USEC"), NULL, 10) / 2;
		its.it_interval.tv_sec = usec / 1000000;
		its.it_interval.tv_nsec = (usec % 1000000) * 1000;
		its.it_value = its.it_interval;
		if (timerfd_settime(timer_fd, 0, &its, NULL)) {
			pr_err("Failed to arms the watchdog timer.\n");
			goto err;
		}
		ev.events = EPOLLIN;
		ev.data.fd = timer_fd;
		if (epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev)) {
			pr_err("Failed to monitor the watchdog timer fd.\n");
			goto err;
		}
	}

	sd_notify((timer_fd >= 0) ? 0 : 1, "READY=1");

	/* Main loop. */
	while (1) {
		n_fds = epoll_wait(epfd, &ev, 1, -1);
		if (n_fds <= 0)
			continue;

		if (ev.data.fd == signal_fd) {
			struct signalfd_siginfo ssi;

			if (read(signal_fd, &ssi, sizeof(ssi)) != sizeof(ssi)) {
				pr_err("Failed to read sth. from the signal "
				       "fd\n");
				goto err;
			}
			if (ssi.ssi_signo == SIGTERM) {
				goto out;
			} else if (ssi.ssi_signo == SIGHUP) {
				if (load_conf()) {
					pr_err("Failed to reload the "
					       "configuration file\n");
					goto err;
				}
				if (listen(listen_fd, backlog)) {
					pr_err("Failed to update the backlog "
					       "of the listen socket to %d\n",
					       backlog);
					goto err;
				}
			} else {
				pr_err("Received the unexpected signal: %d\n",
				       ssi.ssi_signo);
				goto err;
			}
		} else if (ev.data.fd == timer_fd) {
			uint64_t exp;

			if (read(timer_fd, &exp, sizeof(exp)) != sizeof(exp)) {
				pr_err("Failed to read sth. from the timer "
				       "fd\n");
				goto err;
			}
			sd_notify(0, "WATCHDOG=1");
		} else if (ev.data.fd == listen_fd) {
			fd = accept(listen_fd, NULL, NULL);
			if (fd < 0)
				continue;
			ev.events = EPOLLIN;
			ev.data.fd = fd;
			if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)) {
				pr_err("Failed to monitor the connected "
				       "socket: %d\n", fd);
				goto err;
			}
		} else if (ev.data.fd == dgram_fd) {
			union {
				struct sockaddr sa;
				struct sockaddr_in in;
				struct sockaddr_in6 in6;
			} sa;
			socklen_t len = sizeof(sa);
			int retval;

			retval = recvfrom(dgram_fd, buf, sizeof(buf), 0, &sa.sa,
					&len);
			if (retval<= 0)
				continue;
			sendto(dgram_fd, buf, retval, 0, &sa.sa, len);
		} else if (ev.events == EPOLLIN) {
			int retval, len;

			len = read(ev.data.fd, buf, sizeof(buf));
			if (len < 0) {
				continue;
			} else if (len == 0) {
				close(ev.data.fd);
				continue;
			}
			retval = write(ev.data.fd, buf, len);
			if (retval < 0) {
				if (errno == EAGAIN || errno == EINTR) {
					retval = 0;
				} else {
					close(ev.data.fd);
					continue;
				}
			}
			len -= retval;
			if (len <= 0)
				continue;

			if (ev.data.fd >= iob_size) {
				struct iobuf *niob;
				size_t niob_size = ev.data.fd + 1;

				niob = realloc(iob, sizeof(*iob) * niob_size);
				if (!niob) {
					close(ev.data.fd);
					continue;
				}
				memset(niob + iob_size, 0,
				       sizeof(*iob) * (niob_size - iob_size));
				iob = niob;
				iob_size = niob_size;
			}
			iob[ev.data.fd].head = malloc(len);
			if (!iob[ev.data.fd].head) {
				close(ev.data.fd);
				continue;
			}
			memcpy(iob[ev.data.fd].head, buf + retval, len);
			iob[ev.data.fd].data = iob[ev.data.fd].head;
			iob[ev.data.fd].len = len;
			ev.events = EPOLLOUT;
			if (epoll_ctl(epfd, EPOLL_CTL_MOD, ev.data.fd, &ev)) {
				free(iob[ev.data.fd].head);
				close(ev.data.fd);
			}
		} else if (ev.events == EPOLLOUT) {
			int retval;

			retval = write(ev.data.fd, iob[ev.data.fd].data,
					iob[ev.data.fd].len);
			if (retval < 0) {
				if (errno == EAGAIN || errno == EINTR) {
					retval = 0;
				} else {
					free(iob[ev.data.fd].head);
					close(ev.data.fd);
					continue;
				}
			}
			iob[ev.data.fd].len -= retval;
			if (iob[ev.data.fd].len == 0) {
				free(iob[ev.data.fd].head);
				ev.events = EPOLLIN;
				if (epoll_ctl(epfd, EPOLL_CTL_MOD, ev.data.fd,
						&ev)) {
					close(ev.data.fd);
					continue;
				}
				continue;
			}
			iob[ev.data.fd].data += retval;
		}
	}
out:
	return EXIT_SUCCESS;
err:
	return EXIT_FAILURE;
}
