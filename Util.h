#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>


extern int	SetsockoptReuseAddr(int s);
extern int	SetsockoptSndBuf(int s, int numbytes);
extern int	SetsockoptRcvBuf(int s, int numbytes);
extern int	TCP_setup_connection(int *pserverfd, char *string_IP_remote_address, int port_number_remote);
extern int	TCP_setup_socket_listening(int *plistenfd, int numero_porta_locale);
extern ssize_t  Writen (int fd, const void *buf, size_t n);
extern int	Readn(int fd, char *ptr, int nbytes);
extern int	Sendn (int fd, const void *buf, size_t n);
extern void init_random(int seed);
extern float genera_0_1(void);
extern struct timeval differenza(struct timeval dopo,struct timeval prima);
extern double sec_differenza(struct timeval dopo,struct timeval prima);
extern int attesa(int msec);

#define CLEARSCREEN "\033[2J\033[0;0f"

#define DEFAULTCOLOR "\033[0m"
#define ROSSO  "\033[22;31m"
#define VERDE  "\033[22;32m"
#define MARRONE  "\033[22;31m"
#define GIALLO "\033[01;33m"
#define VIOLA "\033[33;35;2m"
#define ROSA "\033[33;35;1m"
#define BLU "\033[34;34;3m"
#define AZZURRO "\033[34;34;1m"
#define VERDECHIARO "\033[22;33m"

#define GREEN "\033[0;0;32m"
#define WHITE   "\033[0m"
#define RED "\033[0;0;31m"
#define BLUE "\033[0;0;34m"
#define ORANGE "\033[0;0;33m"

#endif   /*  __UTIL_H__  */ 

