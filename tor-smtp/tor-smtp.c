/*
 * tor-smtp
 *
 * Copyright (c) 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * The bidicopy() routine is from OpenBSD's netcat rewrite, plus patches
 * from the fedora project.
 *
 * Copyright (c) 2001 Eric Jackson <ericj@monkey.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <syslog.h>
/* for BSD: htons is in there: */
#include <arpa/inet.h>
#include "atomicio.h"

//#define DEBUG(str,args...) fprintf(stderr, str, ##args);
#define DEBUG(str,args...) do {} while(0)

int read_bytes_timeout(int fd, char *buf, int bytes, int timeout)
{
# define DEBUG_RETURN(str,args...) do { DEBUG(str, ##args); return 0; } while (0)
	struct pollfd pollfd = { .fd = fd, .events = POLLIN };
	struct timeval tv_start, tv_now;
	int got = 0, tmp;
	
	gettimeofday(&tv_start, NULL);
	
	while (got < bytes && timeout >= 0) {
		if (poll(&pollfd, 1, timeout*1000) <= 0)
			DEBUG_RETURN("poll failed: %d (got %d)\n", errno, got);
		gettimeofday(&tv_now, NULL);
		timeout -= tv_now.tv_sec - tv_start.tv_sec;
		tv_start = tv_now;

		tmp = read(fd, buf+got, bytes-got);
		if (tmp <= 0)
			DEBUG_RETURN("read failed: %d\n", errno);
		got += tmp;
	}
	return bytes;
#undef DEBUG_RETURN
}

int connect_via_tor(const char *tor_server, const char *tor_port,
		    const char *remote_server, short remote_port)
{
	int fd;
	struct addrinfo *res = NULL;
	char socks_init[] = { 5, 1, 0 };
	char buf[255+7]; /* must be less than 255+7+1 bytes long */
	int remote_server_len = strlen(remote_server);

	if (remote_server_len > sizeof(buf)-7/*5 leading bytes and port */)
		return -1;

	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		DEBUG("failed to create socket\n");
		return -1;
	}

#define DEBUG_OUT(str, args...) do { DEBUG(str,##args); goto out_close; } while (0)

	if (getaddrinfo(tor_server, tor_port, NULL, &res))
		DEBUG_OUT("getaddrinfo\n");
	if (!res)
		DEBUG_OUT("getaddrinfo returned empty set\n");
		
	if (connect(fd, res->ai_addr, res->ai_addrlen))
		DEBUG_OUT("failed to connect\n");

	/* now that we are connected, set up SOCKS */
	if (write(fd, &socks_init, sizeof(socks_init)) != sizeof(socks_init))
		goto out_close;
	if (!read_bytes_timeout(fd, &buf[0], 2, 120))
		goto out_close;
	if (buf[0] != 5 || buf[1] != 0)
		goto out_close;

	buf[0] = 5;
	buf[1] = 1;
	buf[2] = 0;
	buf[3] = 3;
	buf[4] = remote_server_len;
	strcpy(buf+5, remote_server);
	*(short*) (buf+5+remote_server_len) = htons(remote_port);
	if (write(fd, buf, 5+remote_server_len+2) != 5+remote_server_len+2)
		goto out_close;
	if (!read_bytes_timeout(fd, buf, 4, 120))
		goto out_close;
	if (buf[0] != 5 || buf[1] != 0)
		goto out_close;
	/* read rest of the response */
	switch(buf[3]) {
	case 1:
		if (!read_bytes_timeout(fd, buf, 6, 120))
			goto out_close;
		break;
	/* TODO handle other responses */
	}
	freeaddrinfo(res);

	return fd;
 out_close:
 	close(fd);
 	return -1;
}

/* read at most buflen characters into buffer,
 * until linefeed is encountered.
 * NB: if more characters are received they are discarded!
 */
int readline(int fd, char *buf, int buflen, int timeout)
{
# define DEBUG_RETURN(str,args...) do { DEBUG(str, ##args); return 0; } while (0)
	struct pollfd pollfd = { .fd = fd, .events = POLLIN };
	struct timeval tv_start, tv_now;
	int got = 0, tmp;
	char *match;
	
	gettimeofday(&tv_start, NULL);
	
	while (got < buflen && timeout >= 0) {
		if (poll(&pollfd, 1, timeout*1000) <= 0)
			DEBUG_RETURN("poll failed: %d (got %d)\n", errno, got);
		gettimeofday(&tv_now, NULL);
		timeout -= tv_now.tv_sec - tv_start.tv_sec;
		tv_start = tv_now;

		tmp = read(fd, buf+got, buflen-got);
		if (tmp <= 0)
			DEBUG_RETURN("read failed: %d\n", errno);
		match = memchr(buf+got, '\n', tmp);
		if (match) {
			if (*(match-1) == '\r')
				match--;
			*match = '\0';
			return match-buf;
		}
		got += tmp;
	}
	return -1;
#undef DEBUG_RETURN
}

/*
 * readwrite()
 * Loop that polls on the network file descriptor and stdin.
 */
void
bidicopy(int nfd)
{
	struct pollfd pfd[2];
	unsigned char buf[8192];
	int n, wfd = fileno(stdin);
	int lfd = fileno(stdout);
	int plen;

	plen = 1024;

	/* Setup Network FD */
	pfd[0].fd = nfd;
	pfd[0].events = POLLIN;

	/* Set up STDIN FD. */
	pfd[1].fd = wfd;
	pfd[1].events = POLLIN;

	while (pfd[0].fd != -1) {
		if ((n = poll(pfd, 2, -1)) < 0) {
			close(nfd);
			return;
		}

		if (n == 0)
			return;

		if (pfd[0].revents & POLLIN) {
			if ((n = read(nfd, buf, plen)) < 0)
				return;
			else if (n == 0) {
				goto shutdown_rd;
			} else {
				if (atomicio(vwrite, lfd, buf, n) != n)
					return;
			}
		}
		else if (pfd[0].revents & POLLHUP) {
		shutdown_rd:
			shutdown(nfd, SHUT_RD);
			pfd[0].fd = -1;
			pfd[0].events = 0;
		}

		if (1) {
		    if(pfd[1].revents & POLLIN) {
			if ((n = read(wfd, buf, plen)) < 0)
				return;
			else if (n == 0) {
				goto shutdown_wr;
			} else {
				if (atomicio(vwrite, nfd, buf, n) != n)
					return;
			}
		    }
		    else if (pfd[1].revents & POLLHUP) {
		    shutdown_wr:
			shutdown(nfd, SHUT_WR);
			pfd[1].fd = -1;
			pfd[1].events = 0;
		    }
		}
	}
}

int main(int argc, char **argv)
{
	char *tor_server = "localhost";
	char *tor_port = "9050";
	char *remote_server = NULL;
	char *tmp;
	char local_onion[255];
	char buf[255], buf2[255];
	int linelen;
	int fd;

	if (argc >= 2)
		tor_server = argv[1];
	if (argc >= 3)
		tor_port = argv[2];
	if (argc > 3 || (tor_server && (!strcasecmp(tor_server, "--help") || !strcasecmp(tor_server, "-h")))) {
		fprintf(stderr, "tor-smtp [tor-server] [tor-port]\n");
		fprintf(stderr, "     (default localhost 9050)\n");
		return 2;
	}

	openlog("tor-smtp", LOG_PID, LOG_MAIL);

	printf("220 Welcome\r\n");
	fflush(stdout);
	
 command:
	if ((linelen = readline(0, buf, sizeof(buf), 30)) < 0) {
		printf("421 connection timed out\r\n");
		fflush(stdout);
		return 0;
	}
	if (linelen < 4) {
		goto invalid;
	}
	if (!strncasecmp("QUIT", buf, 4)) {
		printf("221 closing connection\r\n");
		fflush(stdout);
		return 0;
	}
	if (!strncasecmp("EHLO ", buf, 5) || !strncasecmp("HELO ", buf, 5))
		goto helo_continue;
 invalid:
 	printf("500 invalid command\r\n");
 	fflush(stdout);
 	goto command;
 	
 helo_continue:
 	tmp = strtok(buf+5, " ");
 	if (!tmp) {
 		printf("500 invalid EHLO/HELO: missing local name\n");
 		fflush(stdout);
 		goto command;
 	}
 	remote_server = strtok(NULL, " ");
 	if (!remote_server) {
 		printf("500 invalid EHLO/HELO: missing remote name\n");
 		goto command;
 	}
 	strncpy(local_onion, tmp, sizeof(local_onion));
	fd = connect_via_tor(tor_server, tor_port, remote_server, 25);
	if (fd < 0) {
		printf("421 could not connect\r\n");
		fflush(stdout);
		syslog(LOG_WARNING, "tor connection to %s failed",
		       remote_server);
		return 0;
	}
	
	linelen = readline(fd, buf2, sizeof(buf2), 120);
	if (linelen < 0) {
		printf("421 could not connect\r\n");
		fflush(stdout);
		syslog(LOG_WARNING, "no welcome message from %s", remote_server);
		return 0;
	}
	if (strncmp("220 ", buf2, 4)) {
		printf("421 server seems to not like us\r\n");
		DEBUG("answer was: %s\n", buf2);
		fflush(stdout);
		return 0;
	}
	
	strncpy(buf+5, local_onion, sizeof(buf)-5);
	linelen = strlen(buf);
	*(buf+linelen) = '\r';
	*(buf+linelen+1) = '\n';
	write(fd, buf, linelen+2);
	
	close(2); /* get rid of stderr */
	/* now "all" we have to do is do a bidirectional copy of fd and stdio */
	bidicopy(fd);
	return 0;
}
