/* Server.c  */

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
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <math.h>

#define DEBUG
/* viene definito dal Makefile
 * quando definito ordina al server di
 * accettare solo richieste INF e RANGE con Range minore di MAXLENRANGE=10
 #define SOLORANGE
 */
#define MAXLENRANGE 10

#include "Util.h"


#define MAXLENPATH 2048
#define MAXLENREQ ((MAXLENPATH)+1024)
#define MAXLENDATA 5000
#define MAXLENRESP ((MAXLENREQ)+MAXLENDATA)
#define GET 1
#define INF 2
#define MAXNUMTHREADWORKING 5

char pathbase[MAXLENPATH];
char localIP[MAXLENPATH];
int numero_porta_locale;
int msecdelay;
float proberrore, probfrozen;
int printed=0;

pthread_mutex_t mutex;
int numthread=0;
int numthreadfrozen=0;


struct Range {
	int first; /* -1 means not specified */
	int last;
};

struct Request {
	char buf[MAXLENREQ];
	int lenreq;
	int reqType;
	uint16_t port;
	struct in_addr inaddr; /* network endianess */
	char path[MAXLENPATH];
	struct Range range;
};

struct Response {
	char buf[MAXLENRESP];
	int lenresp;
};

struct ThreadParam {
	int fd;
};

static void sig_print(int signo)
{
	if(printed==0)
	{
		printed=1;
		if(signo==SIGINT)		printf("SIGINT\n");
		else if(signo==SIGHUP)	printf("SIGHUP\n");
		else if(signo==SIGTERM)	printf("SIGTERM\n");
		else					printf("other signo\n");
		printf("\n");
		fflush(stdout);
	}
	exit(0);
	return;
}

static void Exit(int errcode)
{
	sig_print(0);
}


static int SendnWithError(int fd, const void *buf, size_t n) {
	int ris,i,numpezzi,size,nsent=0;
	float proberrorepezzo, probfrozenpezzo, prob;

	if(msecdelay>0) {
		/* aspetto un pochetto prima di spedire */
		attesa(msecdelay);
	}
#define NUMPEZZI 10
	if(n<=80) {
		numpezzi=2;
		size=n/numpezzi;
		proberrorepezzo=proberrore/numpezzi;
		probfrozenpezzo=probfrozen/numpezzi;
	}
	else { /* n>80 */
		numpezzi=n/40;
		size=n/numpezzi;
		proberrorepezzo=(1.5*proberrore)/numpezzi;
		probfrozenpezzo=(1.5*probfrozen)/numpezzi;
	}
	/*
	if(n>100) numpezzi=10;
	else			numpezzi=2;
	size=n/numpezzi;
	proberrorepezzo=proberrore/numpezzi;
	probfrozenpezzo=probfrozen/numpezzi;
	*/
	

	for(i=0;i<numpezzi;i++) {
		int tobesent=size, myerrno;

		prob=genera_0_1();
		if(prob<proberrorepezzo) {
			/* simulo errore esplicito */
			printf("SIMULO ERRORE\n");
			fflush(stdout);
			errno=EPIPE;
			return(-1);
		}
		prob=genera_0_1();
		if(prob<probfrozenpezzo) {
			/* congelo, blocco per sempre senza chiudere connessione */
			pthread_mutex_lock(&mutex);
			numthreadfrozen++;
			pthread_mutex_unlock(&mutex);
			printf("BLOCCO\n");
			fflush(stdout);
			while(1) { sleep(9999); } /* attesa infinita */
		}
		if(i==numpezzi-1) { /* ultimo pezzo, spedisco i byte residui */
			tobesent=n-nsent;
		}
		else
			tobesent=size;
		ris=Sendn(fd,((char*)buf)+nsent,tobesent);
		myerrno=errno;

		if(msecdelay>0) {
			/* aspetto un pochetto prima di continuare */
			attesa(msecdelay);
		}

		if(ris!=tobesent) { /* errore */
			errno=myerrno;
			return(-1);
		}
		else {
			nsent+=ris;
		}
	}
	return(nsent);
}


static int getDuePunti(struct Request *preq, int *plen) {
	/* cerco un : */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( preq->buf[*plen] != ':' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getSlash(struct Request *preq, int *plen) {
	/* cerco un / */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( preq->buf[*plen] != '/' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getEOL(struct Request *preq, int *plen) {
	/* cerco un \n */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( preq->buf[*plen] != '\n' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getPunto(struct Request *preq, int *plen) {
	/* cerco un . */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( preq->buf[*plen] != '.' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getMeno(struct Request *preq, int *plen) {
	/* cerco un . */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( preq->buf[*plen] != '-' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getInt(struct Request *preq, int *plen, int *pint) {
	/* cerco un intero */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		int ris; char str[128];

		ris=sscanf(preq->buf + (*plen), "%i", pint);
		if(ris!=1)	return(-1); /* errato */

		if( (*pint<0) || (*pint>255) ) return(-1); /* errato */

		sprintf(str,"%i", *pint );
		*plen += strlen(str);
		return(1); /* found */
	}
}


static int getLongInt(struct Request *preq, int *plen, int *pint) {
	/* cerco un intero */
	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		int ris; char str[128];

		ris=sscanf(preq->buf + (*plen), "%i", pint);
		if(ris!=1)	return(-1); /* errato */

		if( *pint<0 ) return(-1); /* errato */

		sprintf(str,"%i", *pint );
		*plen += strlen(str);
		return(1); /* found */
	}
}


static int getIP(struct Request *preq, int *plen) {

	int n, ris, newlen=*plen;
	char strIP[32];
	struct in_addr ina;

	/* cerco primo byte */
	ris=getInt(preq,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto(preq,&newlen);
	if(ris!=1) return(ris);
	/* cerco secondo byte */
	ris=getInt(preq,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto(preq,&newlen);
	if(ris!=1) return(ris);
	/* cerco terzo byte */
	ris=getInt(preq,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto(preq,&newlen);
	if(ris!=1) return(ris);
	/* cerco quarto byte */
	ris=getInt(preq,&newlen, &n);
	if(ris!=1) return(ris);
	/* ok, c'e' un indirizzo IP */
	memcpy(strIP,preq->buf+(*plen), newlen-(*plen) );
	strIP[newlen-(*plen)]=0;
	ris=inet_aton(strIP,&ina);
	if(ris==0) {
		perror("inet_aton failed :");
		return(-1); /* no IP */
	}
	/* controllo che l'indirizzo IP sia quello del mio server 
	 * quello specificato a riga di comando */
	if( strcmp(strIP,localIP) != 0 ) {
		return(-1); /* not suitable IP address */
	}
	memcpy(&(preq->inaddr),&ina,sizeof(struct in_addr));
	*plen=newlen;
	return(1);
}

static int getPort(struct Request *preq, int *plen) {
	/* cerco un intero */
	
	int port;

	if( (preq->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		int ris; char str[128];

		ris=sscanf(preq->buf + (*plen), "%i", &port );
		if(ris!=1)	return(-1); /* errato */

		if( (port<0) || (port>65535) ) return(-1); /* errato */

		/* controllo che la porta sia quella del mio server 
	 	 * quella specificata a riga di comando */
		if( ((int)port) != numero_porta_locale ) {
			return(-1); /* not suitable port number */
		}
		preq->port=(uint16_t)port;

		sprintf(str,"%i", port );
		*plen += strlen(str);
		return(1); /* found */
	}
}



static int getPathAndEOL(struct Request *preq, int *plen) {

	int n=0;
	while(*plen+n<preq->lenreq) {
		if(preq->buf[*plen+n] == '\n' ) {
			preq->path[n] = 0;
			*plen += n+1;
			return(1);
		}
		else {
			preq->path[n] = preq->buf[*plen+n];
			n++;
		}
	}
	/* not found \n means incompleto */
	return(0);
}


static int getRangeAndEOL(struct Request *preq, int *plen) {

#define RANGE  "Range "
	int ris, first, last;

	if( (preq->lenreq - *plen) <= strlen(RANGE)) {
		return(0); /* mancano caratteri */
	}
	if(strncmp(RANGE,preq->buf+(*plen),strlen(RANGE))==0) { /* found "Range " */
		/* RANGE */
		*plen += strlen(RANGE);

		ris=getLongInt(preq,plen, &first);
		if(ris!=1) return(ris); /* manca intero: o c'e' altro o e' finito buffer */

		preq->range.first=first;
		ris=getMeno(preq,plen);
		if(ris!=1) return(ris); /* manca -  : o c'e' altro o e' finito buffer */

		/* se c'e' EOL allora last e' non definito 
		 * quindi prima cerco un EOL 
		 */
		ris=getEOL(preq,plen);
		if( ris==0 ) { /* buffer finito */
			return(0);
		}
		else if( ris==1 ) { /* trovato EOL, last non definito */
			preq->range.last=-1;
			return(1); /* found Range con last non definito */
		}
		/* else ris==-1 trovato qualcosa di diverso da EOL, forse e' il last ? */
		ris=getLongInt(preq,plen, &last);
		if(ris!=1) return(ris); /* manca intero: o c'e' altro o e' finito buffer */
		preq->range.last=last;

		ris=getEOL(preq,plen);
		if(ris!=1) return(ris); /* se ris==0 fine buffer  se ris==-1 e' robaccia */

		return(1); /* found Range completo */
	}

	return(0); /* non dovrei arrivarci mai */
}

/* occhio, devono avere stessa lunghezza */
#define GETMHTTP "GET mhttp://"
#define INFMHTTP "INF mhttp://"
static int checkRequest(struct Request *preq) {
	/*	restituisce 0 se lettura incompleta, 
	 *	-1 se formato errato, 1 se formato corretto e completo
	 */

	int ris, len;

	/* per prima cosa aggiungo uno zero terminatore 
	 * in fondo ai bytes letti, che non fa mai male
	 */
	preq->buf[preq->lenreq]=0;

	if(preq->lenreq<=strlen(GETMHTTP)) { /* non ho letto abbastanza bytes */

		if(strcmp(preq->buf,GETMHTTP)==0) {/* found parte di GET */
			return(0); /* lettura incompleta, read again */
		}
		if(strcmp(preq->buf,INFMHTTP)==0) { /* found parte di INF */
			return(0); /* lettura incompleta, read again */
		}

		/* se arrivo qui ho bytes diversi da GET e INF, formato errato */
		return(-1);
	}

	if(preq->lenreq>strlen(GETMHTTP)) {

		if(strncmp(GETMHTTP,preq->buf,strlen(GETMHTTP))==0) { /* found GET */
			/* GET */
			preq->reqType=GET;
			len=strlen(GETMHTTP);
			ris=getIP(preq,&len);
			if(ris!=1) return(ris);
			ris=getDuePunti(preq,&len);
			if(ris!=1) return(ris);
			ris=getPort(preq,&len);
			if(ris!=1) return(ris);
			ris=getSlash(preq,&len);
			if(ris!=1) return(ris);
			ris=getPathAndEOL(preq,&len);
			if(ris!=1) return(ris);
			/* il campo Range puo' esserci o no, quindi prima cerco un EOL */
			ris=getEOL(preq,&len);
#ifdef SOLORANGE
			if( ris==0 ) {
				return(ris); /* e' gia' finito il buffer -> return */
			}
			else if( ris==1 ) {
				return(-1); /* se c'e' un eol => fine msg => manca Range => error */
			}
#else
			if( (ris==0) || (ris==1) ) {
				return(ris); /* se c'e' un eol o e' gia' finito il buffer -> return */
			}
#endif
			else { /* ris == -1 */
			/* else ci sono caratteri ma non c'e' l' EOL
			 * quindi cerco se c'c' un Range
			 */
				ris=getRangeAndEOL(preq,&len);
				if(ris!=1) return(ris);

#if 0 /* NOOOOOOO!! lascio chiedere anche Range grandi */
#ifdef SOLORANGE
				/* se arrivo qui ho trovato Range, 
				 * contro Range sia definito e che chieda meno di 10 byte */
				if( !
						(
							(preq->range.first != -1)	&&
							(preq->range.last != -1)	&&
							(preq->range.last >= preq->range.last)	&&
							( (preq->range.last - preq->range.first) <= MAXLENRANGE )
						)
					) {
					return(-1);
				}
#endif
#endif /* fine #if 0 */
 

				ris=getEOL(preq,&len);
				/* restituisce 0 se buffer corto, 1 se trova \n, -1 se non trova */
				return(ris);

			}
		}
		else if(strncmp(INFMHTTP,preq->buf,strlen(INFMHTTP))==0) { /*found INF*/
			/* INF */
			preq->reqType=INF;
			len=strlen(INFMHTTP);
			ris=getIP(preq,&len);
			if(ris!=1) return(ris);
			ris=getDuePunti(preq,&len);
			if(ris!=1) return(ris);
			ris=getPort(preq,&len);
			if(ris!=1) return(ris);
			ris=getSlash(preq,&len);
			if(ris!=1) return(ris);
			ris=getPathAndEOL(preq,&len);
			if(ris!=1) return(ris);
			ris=getEOL(preq,&len);
			/* restituisce 0 se buffer corto, 1 se trova \n, -1 se non trova */
			return(ris);
		}
		else {
			/* c'erano abbastanza caratteri, ma non ho trovato ne GET ne INF */
			return(-1);
		}
	}
	return(0); /* default - lettura incompleta */
}

static int getRequest(int fd, struct Request *preq) {
	int ris;
	while(1) {
		ris=recv(fd,preq->buf+preq->lenreq, MAXLENREQ - preq->lenreq,MSG_NOSIGNAL);
		if(ris<0) {
			if(errno==EINTR) ; /* do again */
			else return(-2); /* read error */
		}
		else if(ris==0) { /* peer closed connection */
			return(-1); /* formato richiesta sbagliato */
		}
		else { /* letto qualcosa, controllo */
			preq->lenreq+=ris;
			ris=checkRequest(preq); /* modifica campi Request */
			if(ris==0) { /* richiesta incompleta, read again */
				;
			}
			else if(ris==-1) {
				return(-1); /* formato errato, return -1 */
			}
			else { /* ris==1 */
				return(1);
			}
		}
	}
	return(-1); 
}

static int ResponseUnknownError(int fd) {
	char response[48]="402\n\n";
	int ris;
	ris=SendnWithError(fd, response, strlen(response) );
	return(ris);
}

static int ResponseWrongRequest(int fd) {
	char response[48]="403\n\n";
	int ris;
	ris=SendnWithError(fd, response, strlen(response) );
	return(ris);
}

static int ResponseFileNotFound(int fd) {
	char response[48]="404\n\n";
	int ris;
	ris=SendnWithError(fd, response, strlen(response) );
	return(ris);
}

static int ResponseIntervalNotFound(int fd) {
	char response[48]="405\n\n";
	int ris;
	ris=SendnWithError(fd, response, strlen(response) );
	return(ris);
}

static void initRequest(struct Request *preq) {
	memset( preq, 0 , sizeof(struct Request) );
	/* lenreq settato a zero indica nuova richiesta */
	preq->range.first=-1; /* -1 means not specified */
	preq->range.last=-1;
}

struct FileInfo {
	int error; /* 1 ok, */
	int expire;
	long int len;
};


static int getFileInfo(char *filename, struct FileInfo *pfi) {

	FILE *f;
	int ris, ret;
	long inizio, fine;

	f=fopen(filename,"rb");
	if(f==NULL) return(-3); /* file not found */

	ret=-5;
	/* vado all'inizio e prendo la posizione corrente */
	ris=fseek(f,0L,SEEK_SET);
	if(ris<0) ret=-5; /* unknown error */
	else {
		inizio=ftell(f);
		if(inizio<0) ret=-5; /* unknown error */
		else {
			/* vado alla fine e prendo la posizione corrente */
			ris=fseek(f,0L,SEEK_END);
			if(ris<0) ret=-5; /* unknown error */
			else {

				fine=ftell(f);
				if(fine<0) ret=-5; /* unknown error */

				else {

					pfi->error=0;
					pfi->expire=30; /* 30 secs */
					pfi->len= (long int)(fine-inizio);
					ret=1;
				}
			}
		}
	}
	fclose(f);

	return( ret );
}

static int prepareInfResponse(struct Request *preq, struct Response *presp) {
	int ris;
	struct FileInfo fi;
	char filename[9182];

	strcpy(filename,pathbase);
	strcat(filename,preq->path);

	ris=getFileInfo(filename,&fi);
	if( 		(ris==-5) /* unknown error */
			||	(ris==-3)	/* file not found */
		)
		return(ris);

	else {	/* ris==1 */
		sprintf(presp->buf,"202\nLen %li\nExpire %i\n\n", fi.len, fi.expire);
		presp->lenresp=strlen(presp->buf);
		return(1);
	}
}


static int AppendBytesFromFile(char *filename, struct Request *preq, struct Response *presp) {
	
	FILE *f;
	int ris, ret;

	f=fopen(filename,"rb");
	if(f==NULL) return(-3); /* file not found */

	ret=-5;
	/* vado all'inizio del range richiesto MA ATTENZIONE
	 * NEI FILE L'INDICE DEI CARATTERI COMINCIA DA 0
	 * MENTRE NEI RANGE IL PRIMO BYTE HA INDICE 1
	 * DA CUI IL -1 messo qui sotto
	 */
	ris=fseek(f, preq->range.first -1 ,SEEK_SET);
	if(ris<0) ret=-5; /* unknown error */
	else {
		/* copio i dati richiesti */
		ris=fread( 	presp->buf + presp->lenresp, 
								1, preq->range.last - preq->range.first +1, f);
		if( ris != (preq->range.last - preq->range.first +1) ) {
			ret=-5; /* unknown error */
		}
		else {
			presp->lenresp += (preq->range.last - preq->range.first +1); 
			/* un terminatore di stringhe in piu' non fa mai male */
			presp->buf[presp->lenresp]=0; 
			ret=1;
		}
	}
	fclose(f);

	return( ret );
}


static int prepareGetResponse(struct Request *preq, struct Response *presp) {

	int ris;
	struct FileInfo fi;
	char filename[9182];

	strcpy(filename,pathbase);
	strcat(filename,preq->path);

	ris=getFileInfo(filename,&fi);
	if( 		(ris==-5) /* unknown error */
			||	(ris==-3)	/* file not found */
		)
		return(ris);

	else {	/* ris==1 */

		if(preq->range.first != -1 ) { /* e' una richiesta range */

			if( preq->range.first > fi.len ) { 
				return(-4); /* range fuori dal file */
			}
			if( preq->range.last > fi.len ) { 
				return(-4); /* range fuori dal file */
			}
			if( (preq->range.last != -1) /* fine range e' definito */
					&& 
					(preq->range.last < preq->range.first) ) { 
				return(-4); /* range fuori dal file */
			}
			if( preq->range.last == -1 ) /* fine range e' fine file */
				preq->range.last = fi.len;

			/* compongo la risposta Range */
			sprintf(presp->buf,"201\nRange %i-%i\nExpire %i\n\n", 
							preq->range.first, preq->range.last, fi.expire);
			presp->lenresp=strlen(presp->buf);
			/* copio il range e incremento lenresp */
			ris=AppendBytesFromFile(filename,preq,presp);
			if(ris!=1) {
				return(ris); /* che minchia succede? */
			}
			return(1);

		}

		else { /* e' una richiesta di tutto il file */

			if( preq->range.last != -1) {
				return(-4); /* range errato */
			}

			/* configuro il range */
			preq->range.first = 1;
			preq->range.last = fi.len;
			/* compongo la risposta GET */
			sprintf(presp->buf,"200\nLen %i\nExpire %i\n\n", 
							preq->range.last - preq->range.first +1, fi.expire);
			presp->lenresp=strlen(presp->buf);
			/* copio il range e incremento lenresp */
			ris=AppendBytesFromFile(filename,preq,presp);
			if(ris!=1) {
				return(ris); /* che minchia succede? */
			}
			return(1);

		}
	}
	return(-5); /* unknown error */
}


static int prepareResponse(struct Request *preq, struct Response *presp) {

	switch( preq->reqType) {

		case GET:
		return( prepareGetResponse(preq,presp) );
		break;

		case INF:
		return( prepareInfResponse(preq,presp) );
		break;

		default:
		return(-5); /* unknown error */
		break;
	}
	return(-5); /* unknown error */
}


static int SendResponse(int fd, struct Response *presp) {
	
	return( SendnWithError(fd, presp->buf, presp->lenresp)   );
}

static void* RecvAndSend( void *p) {
	int fd, repeat, ris, ris1;
	struct Request req;
	struct Response resp;

	pthread_mutex_lock(&mutex);
	/* quanti sono i thread funzionanti? */
	ris=numthread-numthreadfrozen;
	if(ris<MAXNUMTHREADWORKING)
		numthread++; /* aggiungo questo thread solo se non lo voglio killare */
	pthread_mutex_unlock(&mutex);

	if(ris>=MAXNUMTHREADWORKING) {
		/* thread eccedente, crepa bastardo */
		pthread_exit(NULL);
		return(NULL);
	}

	fd=((struct ThreadParam*)p)->fd;
	free(p);
	p=NULL;

	initRequest(&req);

	repeat=1;
	while(repeat) {
		ris=getRequest(fd,&req);
#ifdef DEBUG
		printf("\"%s\"\n",req.buf );
#endif

		switch(ris) {
			case -2: /* read error, faro' terminare il thread */
			repeat=0;
			break;

			case -1: /* lettura richiesta terminata, formato richiesta sbagliato */
			repeat=0;
			ResponseWrongRequest(fd);
			break;

			case 0: /* lettura richiesta parziale, continua lettura */
			break;

			case 1: /* lettura richiesta terminata, richiesta corretta */
			repeat=0;
			ris1=prepareResponse(&req,&resp);
			switch(ris1) {
				case 1:
				SendResponse(fd,&resp);
				break;

				case -3: /* file not found */
				ResponseFileNotFound(fd);
				break;

				case -4: /* interval not found */
				ResponseIntervalNotFound(fd);
				break;

				case -5: /* unknown error */
				default:
				ResponseUnknownError(fd);
				break;
			}
			break;

			default:
			repeat=0;
			/* 
			unknown error, faro' terminare il thread 
			chiudendo il socket senza mandare alcuna risposta
			*/
			ResponseUnknownError(fd);
			break; /* unknown error, faro' terminare il thread chiudendo il socket */

		}
	}
	close(fd);
	pthread_mutex_lock(&mutex);
	numthread--;
	pthread_mutex_unlock(&mutex);
	pthread_exit(NULL);
	return(NULL);
}


#define PARAMETRIDEFAULT "./ 127.0.0.1 55555 200 0.2 0.2"
static void usage(void) 
{ printf ("usage: ./Server.exe PATHBASE LOCAL_IP LOCALPORT MSECDELAY PROBERRORE PROBFROZEN\n");
	printf ("   es  ./Server.exe ./ 127.0.0.1 55555 200 0.2 0.2\n");
}

int main(int argc, char *argv[])
{
	int listenfd;
	struct sockaddr_in Cli, Local;
	unsigned int len;
	int ris;
	pthread_attr_t attr;

	if(argc==1) { 
		printf ("uso i parametri di default \n%s\n", PARAMETRIDEFAULT);
		strcpy(pathbase,"./");
		strcpy(localIP,"127.0.0.1");
		numero_porta_locale=55555;
		msecdelay=200;
		proberrore=0.2;
		probfrozen=0.2;
	}
	else if(argc!=7) { 
		printf ("necessari 0 o 6  parametri\n"); usage(); Exit(1);
	}
	else { /* leggo parametri da linea di comando */
		strcpy(pathbase,argv[1]);
		strcpy(localIP,argv[2]);
		numero_porta_locale=atoi(argv[3]);
		if(numero_porta_locale==0) {
			printf ("parametro numero 3 %s sballato\n",argv[3]); usage(); Exit(1);
		}
		ris=sscanf(argv[4],"%i",&msecdelay);
		if(ris!=1) {
			printf ("parametro numero 4 msecdelay %s sballato\n",argv[4]);
			usage(); Exit(1);
		}
		ris=sscanf(argv[5],"%f",&proberrore);
		if(ris!=1) {
			printf ("parametro numero 5 proberrore %s sballato\n",argv[5]);
			usage(); Exit(1);
		}
		ris=sscanf(argv[6],"%f",&probfrozen);
		if(ris!=1) {
			printf ("parametro numero 6 probfrozen %s sballato\n",argv[6]);
			usage(); Exit(1);
		}
	}
	init_random(0);


	if ((signal (SIGHUP, sig_print)) == SIG_ERR) { perror("signal (SIGHUP) failed: "); Exit(2); }
	if ((signal (SIGINT, sig_print)) == SIG_ERR) { perror("signal (SIGINT) failed: "); Exit(2); }
	if ((signal (SIGTERM, sig_print)) == SIG_ERR) { perror("signal (SIGTERM) failed: "); Exit(2); }

	pthread_mutex_init(&mutex,NULL);
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0)
	{	
		printf ("socket() failed, Err: %d \"%s\"\n", errno,strerror(errno));
		return(1);
	}

	/* avoid EADDRINUSE error on bind() */
	ris = SetsockoptReuseAddr(listenfd);
	if (ris==0)
	{
		printf ("SetsockoptReuseAddr() failed, Err: %d \"%s\"\n", errno,strerror(errno));
		close(listenfd);
		return(1);
	}

	/*name the socket */
	memset ( &Local, 0, sizeof(Local) );
	Local.sin_family= AF_INET;
	/* specifico l'indirizzo IP attraverso cui voglio ricevere la connessione */
	/* Local.sin_addr.s_addr = htonl(INADDR_ANY); */
	Local.sin_addr.s_addr = inet_addr(localIP);
	Local.sin_port	      = htons(numero_porta_locale);

	ris = bind(listenfd, (struct sockaddr*) &Local, sizeof(Local));
	if (ris<0)
	{	
		printf ("bind() failed, Err: %d \"%s\"\n",errno,strerror(errno));
		fflush(stderr);
		close(listenfd);
		return(1);
	}

	/* enable accepting of connection  */
	ris = listen(listenfd, 100 );
	if (ris<0)
	{	
		printf ("listen() failed, Err: %d \"%s\"\n",errno,strerror(errno));
		Exit(1);
	}

	while(1) {
		memset ( &Cli, 0, sizeof(Cli) );
		len=sizeof(Cli);

		ris=accept(listenfd,(struct sockaddr *)&Cli, &len);
		if(ris<0){
			if(errno==EINTR) {
				;  /* do again */
			}
			else {
				printf ("accept failed, Err: %d \"%s\"\n",errno,strerror(errno));
				Exit(1);
			}
		}
		else {
			/* nuovo client */
			struct ThreadParam *p;

			p=malloc(sizeof(struct ThreadParam));
			if(p==NULL) {
				printf ("malloc failed, Err: %d \"%s\"\n",errno,strerror(errno));
				Exit(1);
			}
			else {
				pthread_t th;

				p->fd=ris;

				ris=pthread_create(&th,&attr,RecvAndSend, (void*)p );
				if(ris!=0) {
					printf ("pthread_create failed, Err: %d \"%s\"\n",errno,strerror(errno));
					Exit(1);
				}
				p=NULL;
			}
		}
	}
}


