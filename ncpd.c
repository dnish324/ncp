/*
 * Copyright (c) 2019 Daisuke Nishimura <dnish324@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#define BUFFSIZE 1024

static char err_prefix[32];
#define err_msg(msg)	\
	snprintf(err_prefix, sizeof(err_prefix), "%s:%d %s error", __FILE__, __LINE__, msg);	\
	perror(err_prefix);

static void
usage(char *message)
{
	if (message)
		printf("%s\n", message);
	fprintf(stderr, "Usage: ncpd <port number>\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	unsigned port;
	int sk1, sk2;
	struct sockaddr_in self_addr, remote_addr;
	int len;
	char *pw1, *pw2, *pw;
	char recv_buf[BUFFSIZE];
	char send_buf[BUFFSIZE];

	if (argc != 2)
		usage(NULL);

	port = atoi(argv[1]);
	if (port == 0)
		usage("Invalid port number.");

	system("stty -echo >/dev/null 2>&1");
	printf("Password: ");
	scanf("%ms%*c", &pw1);
	printf("\n");
	system("stty -echo >/dev/null 2>&1");
	printf("Verify password: ");
	scanf("%ms%*c", &pw2);
	printf("\n");
	system("stty echo >/dev/null 2>&1");
	if (strcmp(pw1, pw2) != 0) {
		fprintf(stderr, "Entered passwords doesn't match.\n");
		exit(1);
	}
	pw = strdup(pw1);
	if (pw == NULL) {
		err_msg("strdup()");
		exit(1);
	}
	free(pw1);
	free(pw2);

	sk1 = socket(AF_INET, SOCK_STREAM, 0);
	if (sk1 < 0) {
		err_msg("socket()");
		exit(1);
	}

	memset(&self_addr, 0, sizeof(self_addr));
	self_addr.sin_family = AF_INET;
	self_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	self_addr.sin_port = htons(port);

	if (bind(sk1, (struct sockaddr *) &self_addr, sizeof(self_addr)) == -1) {
		err_msg("bind()");
		exit(1);
	}

	listen(sk1, 16);

	while (true) {
		int len;
		int fd;
		struct stat sbuf;
		struct timespec times[2];
		ssize_t size;

		len = sizeof(remote_addr);
		sk2 = accept(sk1, (struct sockaddr *)&remote_addr, (socklen_t *)&len);
		if (sk2 < 0) {
			err_msg("accept()");
			exit(1);
		}
		printf("Connected from %s\n", inet_ntoa(remote_addr.sin_addr));

		/* check password */
		if (recv(sk2, recv_buf, BUFFSIZE, 0) < 0) {
			err_msg("recv()");
			exit(1);
		}
		if (strcmp(pw, recv_buf) != 0) {
			snprintf(send_buf, BUFFSIZE, "%d", -EINVAL);
			if (send(sk2, send_buf, BUFFSIZE, 0) < 0) {
				err_msg("send()");
				exit(1);
			}
			close(sk2);
			continue;
		} else {
			snprintf(send_buf, BUFFSIZE, "%d", 0);
			if (send(sk2, send_buf, BUFFSIZE, 0) < 0) {
				err_msg("send()");
				exit(1);
			}
		}

		/* recieve file */
		if ((size = recv(sk2, recv_buf, BUFFSIZE, 0)) < 0) {
			err_msg("recv()");
			exit(1);
		}

		if ((fd = open(recv_buf, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)) < 0) {
			if (errno != EEXIST) {
				err_msg("open()");
				exit(1);
			}
			snprintf(send_buf, BUFFSIZE, "%d", -EEXIST);
			if (send(sk2, send_buf, BUFFSIZE, 0) < 0) {
				err_msg("send()");
				exit(1);
			}
			close(sk2);
			continue;
		}
		snprintf(send_buf, BUFFSIZE, "%d", 0);
		if (send(sk2, send_buf, BUFFSIZE, 0) < 0) {
			err_msg("send()");
			exit(1);
		}

		if (recv(sk2, &sbuf, sizeof(sbuf), 0) < 0) {
			err_msg("recv()");
			exit(1);
		}

		printf("Writing to %s\n", recv_buf);
		lseek(fd, 0, SEEK_SET);
		/* FIXME: handle recv error */
		while ((size = recv(sk2, recv_buf, BUFFSIZE, 0)) > 0) {
			if (write(fd, recv_buf, size) < 0) {
				err_msg("write()");
				exit(1);
			}
		}

		/* FIXME: handle errors */
		fchmod(fd, sbuf.st_mode & ~S_IFMT);
		times[0] = sbuf.st_atim;
		times[1] = sbuf.st_mtim;
		futimens(fd, times);

		close(fd);

		printf("Closing connection from %s\n", inet_ntoa(remote_addr.sin_addr));
		close(sk2);
	}

	free(pw);
	return 0;
}