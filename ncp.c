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

static char err_prefix[32];
#define err_msg(msg)	\
	snprintf(err_prefix, sizeof(err_prefix), "%s:%d %s error", __FILE__, __LINE__, msg);	\
	perror(err_prefix);

static void
usage(char *message)
{
	if (message)
		printf("%s\n", message);
	fprintf(stderr, "Usage: ncp <IP address>:<port number> <files to copy>\n");
	exit(1);
}

#define BUFFSIZE 1024
int
main(int argc, char *argv[])
{
	char *dst_ip_str;
	in_addr_t dst_ip;
	unsigned port;
	int sk;
	struct sockaddr_in remote_addr;
	char *pw = NULL;
	char recv_buf[BUFFSIZE];
	char send_buf[BUFFSIZE];
	int i;
	int ret;

	if (argc < 3) {
		usage(NULL);
		exit(1);
	}

	if (sscanf(argv[1], "%m[0-9.]:%u", &dst_ip_str, &port) != 2) {
		usage(NULL);
		exit(1);
	}
	if ((dst_ip = inet_addr(dst_ip_str)) == INADDR_NONE) {
		usage("Invalid IP address");
		exit(1);
	}

	memset(&remote_addr, 0, sizeof(remote_addr));
	remote_addr.sin_family = AF_INET;
	remote_addr.sin_addr.s_addr = dst_ip;
	remote_addr.sin_port = htons(port);

	/* send files */
	for (i = 2; i < argc; i++) {
		int fd;
		struct stat sbuf;
		ssize_t size;

		if (stat(argv[i], &sbuf) < 0) {
			err_msg("stat()");
			exit(1);
		} else if ((sbuf.st_mode & S_IFMT) != S_IFREG) {
			fprintf(stderr, "Invalid type: skip copying %s...\n", argv[i]);
			continue;
		} else if (access(argv[i], R_OK) != 0) {
			fprintf(stderr, "Don't have read permission: skip copying %s...\n", argv[i]);
			continue;
		}

		sk = socket(AF_INET, SOCK_STREAM, 0);
		if (sk < 0) {
			err_msg("socket()");
			exit(1);
		}

		if (connect(sk, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
			err_msg("connect()");
			exit(1);
		}
		printf("Connected to %s\n", inet_ntoa(remote_addr.sin_addr));

		/* handle password */
		if (pw == NULL) {
			system("stty -echo >/dev/null 2>&1");
			printf("Enter password: ");
			scanf("%ms%*c", &pw);
			printf("\n");
			system("stty echo >/dev/null 2>&1");
		}
		strncpy(send_buf, pw, BUFFSIZE);
		if (send(sk, send_buf, BUFFSIZE, 0) < 0) {
			err_msg("send()");
			exit(1);
		}
		if (recv(sk, recv_buf, BUFFSIZE, 0) < 0) {
			err_msg("recv()");
			exit(1);
		}
		if ((ret = atoi(recv_buf)) != 0) {
			fprintf(stderr, "Invalid Password\n");
			exit(1);
		}

		strncpy(send_buf, argv[i], BUFFSIZE);
		if (send(sk, send_buf, BUFFSIZE, 0) < 0) {
			err_msg("send()");
			exit(1);
		}
		if (recv(sk, recv_buf, BUFFSIZE, 0) < 0) {
			err_msg("recv()");
			exit(1);
		}
		if ((ret = atoi(recv_buf)) != 0) {
			fprintf(stderr, "%s: skip copying %s...\n", strerror(-ret), argv[i]);
			continue;
		}

		if((fd = open(argv[i], O_RDONLY)) < 0) {
			err_msg("open()");
			exit(1);
		}

		printf("Sending %s\n", argv[i]);
		lseek(fd, 0, SEEK_SET);
		/* FIXME: handle read error */
		while((size = read(fd, send_buf, BUFFSIZE)) > 0) {
			if (send(sk, send_buf, size, 0) < 0) {
				err_msg("send()");
				exit(1);
			}
		}

		close(fd);
		close(sk);
	}

	free(pw);
	return 0;
}