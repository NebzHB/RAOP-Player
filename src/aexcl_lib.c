/*****************************************************************************
 * socket interface library
 * Copyright (C) 2004 Shiro Ninomiya <shiron@snino.com>
 *  (c) Philippe, philippe_44@outlook.com: AirPlay V2 + simple library
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/
#include <stdarg.h>
#include <fcntl.h>
#include <signal.h>

#include "platform.h"
#include "aexcl_lib.h"

extern log_level	util_loglevel;
static log_level	*loglevel = &util_loglevel;

static void set_nonblock(int s) {
#if WIN
	u_long iMode = 1;
	ioctlsocket(s, FIONBIO, &iMode);
#else
	int flags = fcntl(s, F_GETFL,0);
	fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

/*
 * open tcp port
 */
int open_tcp_socket(struct in_addr host, unsigned short *port)
{
	int sd;
	int optval = 1;

	/* socket creation */
	sd = socket(AF_INET, SOCK_STREAM, 0);
	//set_nonblock(sd):

	if (sd < 0) {
		LOG_ERROR("cannot create tcp socket %x", host);
		return -1;
	}

	setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE, (void*) &optval, sizeof(optval));
#if 0 //only Linux supports this
	optval = 120;
	optval = setsockopt(sd, SOL_TCP, TCP_KEEPIDLE, &optval, sizeof(optval));
	optval = 60;
	optval = setsockopt(sd, SOL_TCP, TCP_KEEPINTVL, &optval, sizeof(optval));
	optval = 10;
	optval = setsockopt(sd, SOL_TCP, TCP_KEEPCNT, &optval, sizeof(optval));
#endif

	if (!bind_host(sd, host, port)) {
		closesocket(sd);
		return -1;
	}

	return sd;
}

int open_udp_socket(struct in_addr host, unsigned short *port, bool blocking)
{
	int sd;

	/*socket creation*/
	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (!blocking) set_nonblock(sd);

	if (sd < 0) {
		LOG_ERROR("cannot create udp socket %x", host);
		return -1;
	}
	if (!bind_host(sd, host, port)) {
		closesocket(sd);
		return -1;
	}
	return sd;
}

/*
 * create tcp connection
 * as long as the socket is not non-blocking, this can block the process
 * nsport is network byte order
 */
bool get_tcp_connect(int sd, struct sockaddr_in peer)
{
	for (size_t count = 0; count < 2; count++) {
		if (!connect(sd, (struct sockaddr*) &peer, sizeof(struct sockaddr))) {
			return true;
		}
		usleep(100*1000);
	}

	LOG_ERROR("cannot connect addr=%s, port=%d", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
	return false;
}

bool get_tcp_connect_by_host(int sd, struct in_addr peer, unsigned short port)
{
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = peer.s_addr;
	addr.sin_port = htons(port);

	return get_tcp_connect(sd, addr);
}

/* bind an opened socket to specified host and port.
 * if port is NULL or *port=0, use dynamically assigned port
 */
bool bind_host(int sd, struct in_addr host, unsigned short *port)
{
	struct sockaddr_in addr;
	socklen_t nlen=sizeof(struct sockaddr);

	memset(&addr, 0, sizeof(addr));

	/* use specified hostname */
	addr.sin_addr.s_addr = host.s_addr;
	addr.sin_family = AF_INET;

	/* bind a specified port */
	addr.sin_port = port ? htons(*port) : 0;

	if (bind(sd, (struct sockaddr *) &addr, sizeof(addr)) < 0){
		LOG_ERROR("cannot bind: %s", strerror(errno));
		return false;
	}

	if (port && *port == 0) {
		getsockname(sd, (struct sockaddr *) &addr, &nlen);
		*port = ntohs(addr.sin_port);
	}

	return true;
}

/*
 * read one line from the file descriptor
 * timeout: msec unit, -1 for infinite
 * if CR comes then following LF is expected
 * returned string in line is always null terminated, maxlen-1 is maximum string length
 */
int read_line(int fd, char *line, int maxlen, int timeout, int no_poll)
{
	int i,rval;
	int count=0;
	struct pollfd pfds;
	char ch;

	*line=0;
	pfds.fd=fd;
	pfds.events =POLLIN;
	for(i=0;i<maxlen;i++){
		if(no_poll || poll(&pfds, 1, timeout))
			rval=recv(fd,&ch,1,0);
		else return 0;

		if(rval==-1){
			if(errno==EAGAIN) return 0;
			LOG_ERROR("fd: %d read error: %s", fd, strerror(errno));
			return -1;
		}
		if(rval==0){
			LOG_INFO("disconnected on the other end %u", fd);
			return -1;
		}
		if(ch=='\n'){
			*line=0;
			return count;
		}
		if(ch=='\r') continue;
		*line++=ch;
		count++;
		if(count>=maxlen-1) break;
	}
	*line=0;
	return count;
}

/*
 * key_data type data look up
 */
char *kd_lookup(key_data_t *kd, char *key)
{
	int i = 0;
	while (kd && kd[i].key){
		if (!strcmp(kd[i].key, key)) return kd[i].data;
		i++;
	}
	return NULL;
}

void free_kd(key_data_t *kd)
{
	int i = 0;
	while (kd && kd[i].key){
		free(kd[i].key);
		if (kd[i].data) free(kd[i].data);
		i++;
	}

	kd[0].key = NULL;
}

/*
 * remove one character from a string
 * return the number of deleted characters
 */
int remove_char_from_string(char *str, char rc)
{
	int i=0,j=0,len;
	int num=0;
	len=strlen(str);
	while(i<len){
		if(str[i]==rc){
			for(j=i;j<len;j++) str[j]=str[j+1];
			len--;
			num++;
		}else{
			i++;
		}
	}
	return num;
}

/*
 * transform an hex string (into an array or bytes)
 * return the number of deleted characters
 */
int hex2bytes(char *hex, uint8_t **bytes) {
	size_t i, len = strlen(hex) / 2;

	if (!*bytes && (*bytes = malloc(len)) == NULL) return 0;

	for (i = 0; i < len; i++) {
		sscanf(hex + i*2, "%2hhx", *bytes + i);
	}

	return len;
}
