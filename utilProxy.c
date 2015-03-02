/* UtilProxy.c */

#define _BSD_SOURCE   /* per inet_aton */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

extern void srandom(unsigned int seed);
extern long int random(void);

int	SetsockoptReuseAddr(int s)
{
	int OptVal, ris, myerrno;

	/* avoid EADDRINUSE error on bind() */
	OptVal = 1;
	ris = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&OptVal, sizeof(OptVal));
	if (ris != 0 )  {
		myerrno=errno;
		printf ("setsockopt() SO_REUSEADDR failed, Err: %d \"%s\"\n", errno,strerror(errno));
		errno=myerrno;
		return(0);
	}
	else
		return(1);
}

int	SetsockoptSndBuf(int s, int numbytes)
{
	int ris;

	ris = setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&numbytes, sizeof(int));
	if (ris != 0 )  {
		printf ("setsockopt() SO_SNDBUF failed, Err: %d \"%s\"\n", errno,strerror(errno));
		return(0);
	}
	else
		return(1);
}

int	SetsockoptRcvBuf(int s, int numbytes)
{
	int ris;

	ris = setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)&numbytes, sizeof(int));
	if (ris != 0 )  {
		printf ("setsockopt() SO_RCVBUF failed, Err: %d \"%s\"\n", errno,strerror(errno));
		return(0);
	}
	else
		return(1);
}


int TCP_setup_connection(int *pserverfd, char *string_IP_remote_address, int port_number_remote)
{
	struct sockaddr_in Local, Serv;
	int ris;

	*pserverfd = socket(AF_INET, SOCK_STREAM, 0);
	if (*pserverfd<0) {
		printf ("socket() failed, Err: %d \"%s\"\n", errno,strerror(errno));
		fflush(stdout);
		return(0);
	}

	ris =	SetsockoptReuseAddr(*pserverfd);
	if (ris==0)  {
	    printf ("SetsockoptReuseAddr() failed, Err: %d \"%s\"\n",errno,strerror(errno));
		return(0);
	}

	/* name the socket */
	memset ( &Local, 0, sizeof(Local) );
	Local.sin_family		=	AF_INET;
	Local.sin_addr.s_addr	=	htonl(INADDR_ANY);         /* wildcard */
	Local.sin_port			=	htons(0);

	ris = bind(*pserverfd, (struct sockaddr*) &Local, sizeof(Local));
	if (ris<0)  {
	    printf ("bind() failed, Err: %d \"%s\"\n",errno,strerror(errno));
		return(0);
	}

	/* assign our destination address */
	memset ( &Serv, 0, sizeof(Serv) );
	Serv.sin_family	 =	AF_INET;
	Serv.sin_addr.s_addr  =	inet_addr(string_IP_remote_address);
	Serv.sin_port		 =	htons(port_number_remote);
#ifdef DEBUG
	printf ("connecting to %s %d\n", string_IP_remote_address, port_number_remote);
	fflush(stdout);
#endif
	do {
		ris = connect(*pserverfd, (struct sockaddr*) &Serv, sizeof(Serv));
	} while( (ris<0)&&(errno==EINTR));

	if (ris<0)  {
		printf ("Connect() failed, Err: %d \"%s\"\n",errno,strerror(errno));
		fflush(stdout);
		return(0);
	}
#ifdef DEBUG
	printf ("connected to %s %d\n", string_IP_remote_address, port_number_remote);
	fflush(stdout);
#endif
	return(1);

}

int TCP_setup_socket_listening(int *plistenfd, int numero_porta_locale)
{
	int ris;
	struct sockaddr_in Local;
	
	*plistenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (*plistenfd < 0)
	{	
		printf ("socket() failed, Err: %d \"%s\"\n", errno,strerror(errno));
		return(0);
	}
	
	ris =	SetsockoptReuseAddr(*plistenfd);
	if (ris==0)  {
	    printf ("SetsockoptReuseAddr() failed, Err: %d \"%s\"\n",errno,strerror(errno));
		return(0);
}
	/*name the socket */
	memset ( &Local, 0, sizeof(Local) );
	Local.sin_family= AF_INET;
	/* specifico l'indirizzo IP attraverso cui voglio ricevere la connessione */
	Local.sin_addr.s_addr = htonl(INADDR_ANY);
	Local.sin_port	      = htons(numero_porta_locale);

	ris = bind(*plistenfd, (struct sockaddr*) &Local, sizeof(Local));
	if (ris<0)
	{	
		printf ("bind() failed, Err: %d \"%s\"\n",errno,strerror(errno));
		fflush(stderr);
		return(0);
	}

	/* enable accepting of connection  */
	ris = listen(*plistenfd, 100 );
	if (ris<0)
	{	
		printf ("listen() failed, Err: %d \"%s\"\n",errno,strerror(errno));
		exit(1);
	}
	return(1);
}



ssize_t  Writen (int fd, const void *buf, size_t n)
{   
	size_t	nleft;     
	ssize_t  nwritten;  
	char	*ptr;

	ptr = (void *)buf;
	nleft = n;
	while (nleft > 0) 
	{
		if ( (nwritten = write(fd, ptr, nleft)) < 0) {
			if (errno == EINTR)	nwritten = 0;   /* and call write() again*/
			else			return(-1);       /* error */
		}
		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n);
}

int  Sendn (int fd, const void *buf, size_t n)
{   
	size_t	nleft;     
	ssize_t  nwritten;  
	char	*ptr;
	

	ptr = (void *)buf;
	nleft = n;
	while (nleft > 0) 
	{
		if ( (nwritten = send(fd, ptr, nleft, MSG_NOSIGNAL)) < 0) {
			if (errno == EINTR)	nwritten = 0;   /* and call send() again */
			else			return(-1);       /* error */
		}
		
		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n);
}

int Readn(int fd, char *ptr, int nbytes)
{
	int nleft,nread;

	nleft=nbytes;
	while(nleft>0)
	{
		do {
			nread=read(fd,ptr,nleft);
		} while ( (nread<0) && (errno==EINTR) );
		if(nread<0)
		{	/* errore   */
			char msg[2000];
			sprintf(msg,"readn: errore in lettura [result %d] :",nread);
			perror(msg);
			return(-1);
		}
		else
		{
			if(nread==0) {
				return(0);
				break;
			}
		}

		nleft -= nread;
		ptr   += nread;
	}
return(nbytes);
}


void init_random(int seed)
{
	if(seed==0) /* seme casuale */
		srandom( (unsigned int)getpid() );
	else
		srandom( seed );
}

float genera_0_1(void) {
	double f;
	/* genero un numero compreso tra 0 e RAND_MAX */
	long int n=random();
	/* normalizzo tra 0 e 1 */
	f=((double)n)/((double)RAND_MAX);
	return( (float)f );
}

#define SEC_IN_MCSEC 1000000L
int normalizza( struct timeval *t )
{
	if(t->tv_usec>=SEC_IN_MCSEC)
	{
		t->tv_sec  += ( t->tv_usec/SEC_IN_MCSEC );
		t->tv_usec =  ( t->tv_usec%SEC_IN_MCSEC );
	}
	return(1);
}

int somma(struct timeval tmr,struct timeval ist,struct timeval *delay)
{
	normalizza( &tmr );
	normalizza( &ist );
	
	(*delay).tv_sec = ist.tv_sec + tmr.tv_sec;
	(*delay).tv_usec = ist.tv_usec + tmr.tv_usec;
	normalizza( delay );
	return(1);
}

struct timeval differenza(struct timeval dopo,struct timeval prima)
{
	struct timeval diff;
	normalizza( &prima);
	normalizza( &dopo);
	
	if ( dopo.tv_sec < prima.tv_sec){
		diff.tv_sec = 0;
		diff.tv_usec = 0;
	}
	else {
		diff.tv_sec = dopo.tv_sec - prima.tv_sec;	/* Sottraggo i secondi tra di loro */
		if  (dopo.tv_usec < prima.tv_usec){
			if (diff.tv_sec > 0)
			{
				/*
				Devo scalare di uno i secondi e sottrarli ai micro secondi 
				ossia aggiungo 1000000 all'ultima espressione
				*/
				diff.tv_sec =diff.tv_sec - 1;
				diff.tv_usec =(dopo.tv_usec) - prima.tv_usec + SEC_IN_MCSEC;
			}
			else{
				diff.tv_usec = 0;
			}
		}
		else {
			diff.tv_usec = dopo.tv_usec - prima.tv_usec;
		}
	}
	return(diff);
}


double sec_differenza(struct timeval dopo,struct timeval prima) {
	struct timeval diff=differenza(dopo,prima);
	return( ((double)diff.tv_sec)+(((double)diff.tv_usec)/1000000.0) );
}

int attesa(int msec) {
	struct timeval t;
	int ris;

	t.tv_usec=msec*1000;
	t.tv_sec=0;
	ris=select(1,NULL,NULL,NULL,&t);
	return(ris);
}


int TCP_setup_socket_server(char *IP, int numero_porta_server){
	int fd;
	struct sockaddr_in server_proxy;
	struct timeval tv; /* struttura definita per INACTIVITY_TIMEOUT_SECONDS	*/
	fd=socket(AF_INET, SOCK_STREAM, 0);
	if (fd<0){
		perror("\n creazione socket di comunicazione con il server fallita\n");
		exit(-1);
	}
	printf("creazione socket comunicazione server completata\n");
	/*printf("%s \n",localIP);*/
	memset ( &server_proxy, 0, sizeof(server_proxy) );
	server_proxy.sin_family= AF_INET;
	/* specifico l'indirizzo IP attraverso cui voglio ricevere la connessione */
	/* Local.sin_addr.s_addr = htonl(INADDR_ANY); */
	server_proxy.sin_addr.s_addr = inet_addr(IP);
	server_proxy.sin_port = htons(numero_porta_server);
		
	/*if(bind(fd,(struct sockaddr*)&server_proxy,sizeof(server_proxy))<0){
		perror("bind");
		exit(-1);
	}
	else{
		printf("bind completata\n\n");
		}*/
	/*	lato client	*/
	
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,  sizeof tv))
    {
     perror("setsockopt");
     return -1;
     }

	if (connect(fd,(struct sockaddr*)&server_proxy, sizeof(server_proxy))!=0){
		perror("connect error"); 
		exit(-1); 
	}
	else{
		printf("connessione server proxy completata\n\n\n");
		}
	return(fd);
}		

