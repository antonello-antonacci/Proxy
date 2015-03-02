#define _BSD_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "utilProxy.h"
#include "utilProxy.c"
#include <error.h>
#include <pthread.h>
#define ERRORE402 "402\n\n"
#define ERRORE403 "403\n\n"
#define ERRORE404 "404\n\n"
#define ERRORE405 "405\n\n"
#define PARAMETRI "IP proxy-server: 127.0.0.1	porta proxy: 55554"
#define GETMHTTP "GET mhttp://"
#define MAXLENPATH 2048
#define MAXLENREQ ((MAXLENPATH)+1024)
#define MAXLENDATA 5000
pthread_cond_t cond= PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex= PTHREAD_MUTEX_INITIALIZER;
int nconn[100];//int i=0;
int matrix[100][(MAXLENDATA/10)+3];
int turno[100];
int tfila=0;
int arraycache[20];

struct Life{
	char nome[20];
	char buf[5500];
	int expire;	
	long int birth;
	};
struct Life cache[20];
struct Range {
	int first; /* -1 means not specified */
	int last;
};

struct Request {
	char buf[MAXLENREQ];
	char inf[MAXLENREQ];/*	buffer per richiesta INF	*/
	int lenreq;
	uint16_t port;	/*	porta server	*/
	struct in_addr serveraddr; /*	ip server	sockaddr_in*/
	char path[MAXLENPATH];
	};

struct ResponseInf {
	char buf[MAXLENREQ];
	int Len;
	int expire;
};

struct RequestGet {
	char buf[MAXLENREQ];
	int maxconnect;
	int temp;//usato per i thread indica il proprio numero
	struct Range range;
	};
	
struct ResponseGet{
	char buf[MAXLENDATA];
	int fd;//usato per la invio200, qui viene salvato il fd del client
	int expire;
	char dati[MAXLENDATA];
	struct Range range;	
	};

struct RequestWrapper {
	struct Request request;
	struct RequestGet request_get;
	struct ResponseGet response_get;
	struct ResponseInf response_inf;
	int posizione;	
};
void *thread_func(void* args);

static void initRequest(struct RequestGet *request_get) {
	memset( request_get, 0 , sizeof(struct RequestGet) );
	/* lenreq settato a zero indica nuova richiesta */
	request_get->range.first=-1; /* -1 means not specified */
	request_get->range.last=-1;
}

static int getPunto(struct Request *request, int *plen) {
	/* cerco un . */
	if( (request->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( request->buf[*plen] != '.' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getInt(struct Request *request, int *plen, int *pint) {
	/* cerco un intero */
	if( (request->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		int ris; char str[128];

		ris=sscanf(request->buf + (*plen), "%i", pint);
		if(ris!=1)	return(-1); /* errato */

		if (*pint<0)  return(-1); /* errato */

		sprintf(str,"%i", *pint );
		*plen += strlen(str);
		return(1); /* found */
	}
}

static int getDuePunti(struct Request *request, int *plen) {
	/* cerco un : */
	if( (request->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( request->buf[*plen] != ':' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getSlash(struct Request *request, int *plen) {
	/* cerco un / */
	if( (request->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( request->buf[*plen] != '/' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getIP(struct Request *request, int *plen) {

	int n, ris, newlen=*plen;
	char strIP[32];
	struct in_addr ina;

	/* cerco primo byte */
	ris=getInt(request,&newlen, &n);
	if(ris!=1){printf("errore %d\n",ris); return(ris);}
	ris=getPunto(request,&newlen);
	if(ris!=1) {printf("errore\n"); return(ris);}
	/* cerco secondo byte */
	ris=getInt(request,&newlen, &n);
	if(ris!=1) {printf("errore\n"); return(ris);}
	ris=getPunto(request,&newlen);
	if(ris!=1) {printf("errore\n"); return(ris);}
	/* cerco terzo byte */
	ris=getInt(request,&newlen, &n);
	if(ris!=1) {printf("errore\n"); return(ris);}
	ris=getPunto(request,&newlen);
	if(ris!=1) return(ris);
	/* cerco quarto byte */
	ris=getInt(request,&newlen, &n);
	if(ris!=1) return(ris);
	/* ok, c'e' un indirizzo IP */
	memcpy(strIP,request->buf+(*plen), newlen-(*plen) );
	/*strcpy(request->serverIP, strIP);*/
	strIP[newlen-(*plen)]=0;
	ris=inet_aton(strIP,&ina);
	if(ris==0) {
		perror("inet_aton failed :");
		return(-1); /* no IP */
	}
	
	memcpy(&(request->serveraddr),&ina,sizeof(struct in_addr));
	*plen=newlen;
	return(1);
}

static int getPort(struct Request *request, int *plen) {
	/* cerco un intero */
	int port;
	if( (request->lenreq - *plen) < 1 ) return(0); /* incompleto */
	else {
		int ris; char str[128];

		ris=sscanf(request->buf + (*plen), "%i", &port );
		if(ris!=1)	return(-1); /* errato */

		if( (port<0) || (port>65535) ) return(-1); /* errato */
		request->port=(uint16_t)port;

		sprintf(str,"%i", port );
		*plen += strlen(str);
		printf("SONO NELLA GETPORT E VALE %i\n",port);
		return(1); /* found */
	}
}

static int getPathAndEOL(struct Request *request, int *plen) {

	int n=0;
	while(*plen+n<request->lenreq) {
		if(request->buf[*plen+n] == '\n' ) {
			request->path[n] = 0;
			*plen += n+1;
			return(1);
		}
		else {
			request->path[n] = request->buf[*plen+n];
			n++;
		}
	}
	/* not found \n means incompleto */
	return(0);
}
/*	questa funzione ha il compito di individuare il primo posto libero
 *  nella cache*/
int posto_libero(){
	int i=0;
	while((strlen(cache[i].nome)!=0)&& i<20){
		i++;
		if(i >= 20){
			return(-1);
			}
		}
	return(i);
}
void *impresa_pulizie(){
int i;
struct timeval tv;

	for(;;){
		i=0;
		for(i=0;i<20;i++){
			if(strlen(cache[i].nome)!=0){
				gettimeofday(&tv,NULL);
				if(tv.tv_sec >= cache[i].expire+cache[i].birth){//se scaduto
					if(arraycache[i]!=1){//Se non è stato bloccato dal proxy
						memset(&cache[i],0,sizeof(struct Life));//lo elimino
						}
				}
			}
		}
	}
}

static int Get2Inf(struct Request *request){
		strcpy(request->inf,request->buf);
		request->inf[0]='I';
		request->inf[1]='N';
		request->inf[2]='F';
		return(1);
}
/* la checkLen, tramite una serie di if, dovrà restituire il valore di n
* che indica in quante parti si può dividere il file richiesto	*/
int checkLen(struct ResponseInf *pinf){
	int n;
	
	if(pinf->Len%10==0){
		n=pinf->Len/10;
		return(n);
	}
	else{
		n=pinf->Len/10+1;
		return(n);
		}
}


static int Get200(char *buf){
		
		buf[0]='2';
		buf[1]='0';
		buf[2]='0';
		return(1);
}

static int check402( char *buf,int *plen) {
	
	if(*plen>=1){
		if(buf[0]!='4')
			return(-1);
		}
	if(*plen>=2){
		if(buf[1]!='0')
			return(-1);
		}
	if(*plen>=3){
		if(buf[2]!='2')
			return(-1);
		}
	if(*plen>=4){
		if(buf[3]!='\n')
			return(-1);
		}
	if(*plen>=5){
		if(buf[4]!='\n')
			return(-1);
		}
	return(1);
}
static int check403( char *buf,int *plen) {
	
	if(*plen>=1){
		if(buf[0]!='4')
			return(-1);
		}
	if(*plen>=2){
		if(buf[1]!='0')
			return(-1);
		}
	if(*plen>=3){
		if(buf[2]!='3')
			return(-1);
		}
	if(*plen>=4){
		if(buf[3]!='\n')
			return(-1);
		}
	if(*plen>=5){
		if(buf[4]!='\n')
			return(-1);
		}
	return(1);
}

static int check404( char *buf,int *plen) {
	if(*plen>=1){
		if(buf[0]!='4')
			return(-1);
		}
	if(*plen>=2){
		if(buf[1]!='0')
			return(-1);
		}
	if(*plen>=3){
		if(buf[2]!='4')
			return(-1);
		}
	if(*plen>=4){
		if(buf[3]!='\n')
			return(-1);
		}
	if(*plen>=5){
		if(buf[4]!='\n')
			return(-1);
		}
	return(1);
}

static int check405( char *buf, int *plen) {
	if(*plen>=1){
		if(buf[0]!='4')
			return(-1);
		}
	if(*plen>=2){
		if(buf[1]!='0')
			return(-1);
		}
	if(*plen>=3){
		if(buf[2]!='5')
			return(-1);
		}
	if(*plen>=4){
		if(buf[3]!='\n')
			return(-1);
		}
	if(*plen>=5){
		if(buf[4]!='\n')
			return(-1);
		}
	return(1);
}

static int check201(struct ResponseGet *pris, int *plen){
	
	
	
	if(*plen<1) return(0); /* leggere ancora perchè SIMULO ERRORE */
	if(*plen>=1) 
		if(pris->buf[0]!='2') 
			return(-1);
	if(*plen>=2)
		if(pris->buf[1]!='0')
			return(-1);
	if(*plen>=3) 
		if(pris->buf[2]!='1')
			return(-1);
	if(*plen>=4)
		if(pris->buf[3]!='\n')
			return(-1);
	if(*plen>=5) 
		if(pris->buf[4]!='R')
			return(-1);
	if(*plen>=6) 
		if(pris->buf[5]!='a')
			return(-1);
	if(*plen>=7)
		if(pris->buf[6]!='n')
			return(-1);
	if(*plen>=8)
		if(pris->buf[7]!='g')
			return(-1);
	if(*plen>=9)
		if(pris->buf[8]!='e')
			return(-1);
	if(*plen>=10)
		if(pris->buf[9]!=' ')
			return(-1);
	
	if(*plen>=11) {
		int ris, len2, len3, len4, valuerange;
		char str[1024];
		
		ris=sscanf(pris->buf+10, "%i", &len2);/*	cerco il valore di inizio del range	*/
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if( len2<0 ) return(-1); /* errato, len negativo */
		pris->range.first=len2;/*	metto il valore di inizio del range	*/
		valuerange=len2;
		sprintf(str, "%i", len2);
		len2=11+strlen(str);
		if(*plen>=len2){
			if(pris->buf[len2-1]!='-')
				return(-1);
			}
		ris=sscanf(pris->buf+len2+1-1, "%i", &len3);/*	cerco il valore di fine del range	*/
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if( len3<0 ) return(-1); /* errato, len negativo */
		pris->range.last=len3;/*	metto il valore di fine del range	*/
		
		valuerange=(pris->range.last-pris->range.first)+1;/*	+1 perchè estremi compresi	*/
		sprintf(str, "%i", len3);
		len3=len2+strlen(str)+1;
		
		if(*plen>=len3){
			if(pris->buf[len3-1]!='\n')
				return(-1);
			}
		
		/* cerco se c'e' Expire xxxx\n  */
		if(*plen>=len3+1)
			if(pris->buf[len3+1-1]!='E')
				return(-1);		
		if(*plen>=len3+2)
			if(pris->buf[len3+2-1]!='x')
				return(-1);		
		if(*plen>=len3+3)
			if(pris->buf[len3+3-1]!='p')
				return(-1);		
		if(*plen>=len3+4)
			if(pris->buf[len3+4-1]!='i')
				return(-1);		
		if(*plen>=len3+5)
			if(pris->buf[len3+5-1]!='r')
				return(-1);		
		if(*plen>=len3+6)
			if(pris->buf[len3+6-1]!='e')
				return(-1);		
		if(*plen>=len3+7)
			if(pris->buf[len3+7-1]!=' ')
				return(-1);
		if(*plen<len3+7+1)
			return(0);
		
		ris=sscanf(pris->buf+len3+7, "%i", &len4);
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if(len4<0) return(-1); /* errato, len negativo */
		pris->expire=len4;
		
		sprintf(str,"%i", len4);
		/* cerco il carattere successivo alla fine del numero trovato */
		len3= len3 + 8 + strlen(str);
		/* controllo se c'e' l' EOL */
		if(*plen>=len3)
			if(pris->buf[len3-1]!='\n')
				return(-1);	
		/* cerco la fine del messaggio */
		if(*plen>=(len3+1))
			if(pris->buf[len3+1-1]!='\n')
				return(-1);
		
		/*	vedo se il n di bytes ricevuti è minore di quello che mi aspettavo in totale	*/
		if(*plen<(len3+1+valuerange))
			return(0);
		
		bzero(pris->dati,strlen(pris->dati));
		memcpy(pris->dati,pris->buf+len3+1,valuerange);
				
		}
	return(1);	
	
	}
static int check202( struct ResponseInf *prisInf,int *plen) {
	
	if(*plen<1) return(0); /* leggere ancora  perchè SIMULO ERRORE*/
	if(*plen>=1) 
		if(prisInf->buf[0]!='2') 
			return(-1);
	if(*plen>=2)
		if(prisInf->buf[1]!='0')
			return(-1);
	if(*plen>=3) 
		if(prisInf->buf[2]!='2')
			return(-1);
	if(*plen>=4)
		if(prisInf->buf[3]!='\n')
			return(-1);
	/* cerco se c'e' Len xxxx\n  */
	if(*plen>=5) 
		if(prisInf->buf[4]!='L')
			return(-1);
	if(*plen>=6) 
		if(prisInf->buf[5]!='e')
			return(-1);
	if(*plen>=7)
		if(prisInf->buf[6]!='n')
			return(-1);
	if(*plen>=8)
		if(prisInf->buf[7]!=' ')
			return(-1);
	if(*plen>=9) {
		int ris, len2, len3;
		char str[1024];
		
		ris=sscanf(prisInf->buf+8, "%i", &len2);
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if( len2<0 ) return(-1); /* errato, len negativo */
		prisInf->Len=len2;
		
		sprintf(str, "%i", len2);
		len2=9+strlen(str);
		if(*plen>=len2){
			if(prisInf->buf[len2-1]!='\n')
				return(-1);
			}
		/* cerco se c'e' Expire xxxx\n  */
		if(*plen>=len2+1)
			if(prisInf->buf[len2+1-1]!='E')
				return(-1);		
		if(*plen>=len2+2)
			if(prisInf->buf[len2+2-1]!='x')
				return(-1);		
		if(*plen>=len2+3)
			if(prisInf->buf[len2+3-1]!='p')
				return(-1);		
		if(*plen>=len2+4)
			if(prisInf->buf[len2+4-1]!='i')
				return(-1);		
		if(*plen>=len2+5)
			if(prisInf->buf[len2+5-1]!='r')
				return(-1);		
		if(*plen>=len2+6)
			if(prisInf->buf[len2+6-1]!='e')
				return(-1);		
		if(*plen>=len2+7)
			if(prisInf->buf[len2+7-1]!=' ')
				return(-1);
		if(*plen<len2+7+1)
			return(0);

		ris=sscanf(prisInf->buf+len2+7, "%i", &len3);
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if(len3<0) return(-1); /* errato, len negativo */
		prisInf->expire=len3;
		sprintf(str,"%i", len3 );
		/* cerco il carattere successivo alla fine del numero trovato */
		len2= len2 + 8 + strlen(str);
		/* controllo se c'e' l' EOL */
		if(*plen>=len2)
			if(prisInf->buf[len2-1]!='\n')
				return(-1);	
		/* cerco la fine del messaggio */
		if(*plen>=(len2+1))
			if(prisInf->buf[len2+1-1]!='\n')
				return(-1);
		if(*plen<(len2+1+(prisInf->Len)) )
			return(1);
		
		}
	return(1);
}

int invio200(struct ResponseGet *rToClient, int filen){
	char buffer[6000];
	sprintf(buffer,"200\nLen %d\nExpire %d\n\n%s",filen,rToClient->expire,rToClient->dati);
	Sendn(rToClient->fd,buffer,strlen(buffer));
	return(1);
	}



/*	Funzione usata per:
 * - creare socket di comunicazione con il server
 * - inviare richiesta di tipo INF
 * - ricevere risposta dal server di tipo INF 202
 * - controllare la presenza di errori 404 oppure 402	*/
int handlerResponseInf(struct RequestWrapper *wrapper){
	int ris, serverfd,porta_server, temp=0;
	char *IPserver;/*	ip del server */
				
	IPserver=inet_ntoa(wrapper->request.serveraddr);
	porta_server=wrapper->request.port;
	/*	questo while viene usato per reinviare le richieste in caso di 
	 * SIMULO ERRORE del server o BLOCCO	*/
	while(temp<=0){
		/*	creazione nuovo socket per comunicazione con server	*/
		serverfd=TCP_setup_socket_server(IPserver,porta_server);
		
		/*	invio richiesta INF al server*/
		ris=Sendn(serverfd,wrapper->request.inf,strlen(wrapper->request.inf));
		ris=0;
		while(1){/* ricezione contenuto risposta INF dal server	*/
			ris=recv(serverfd,wrapper->response_inf.buf+ris,sizeof(wrapper->response_inf.buf),0);
			if (ris==0){break;}/*	ha finito di ricevere dati	*/
			if(ris<0){ /*	timeout_inactivity_seconds scaduto	*/
				perror("recv"); printf("\n\nBLOCCO DEL SERVER oppure Server non attivo \n\n");
				close(serverfd);//chiudo l'attuale connessione con il server
				break;//provo una nuova connessione con il server
				}
			}
		printf("ricevuti dati dal server: %s",wrapper->response_inf.buf);
		int lenrecv=strlen(wrapper->response_inf.buf);
		temp=check202(&wrapper->response_inf,&lenrecv);
		if(temp==0){  /*	SIMULO ERRORE	*/
			/*	bisogna ripetere la richiesta	*/
			printf("Errore ricezione risposta INF\nRICONNESSIONE IN CORSO....\n");
			}
		if(temp<0){	/*	per come è impostato il progetto ci sará 404	*/
			//printf("controllo il 404\n");
			if((ris=check404(wrapper->response_inf.buf,&lenrecv))==1){
				Sendn(wrapper->response_get.fd,ERRORE404,strlen(ERRORE404));
				close(wrapper->response_get.fd);
				close(serverfd);
				//pthread_exit(NULL);
				return(-1);
				}
			else {/*	se arrivo qui ho ricevuto un 402	*/
				//printf("controllo per un 402\n");
				if((ris=check402(wrapper->response_inf.buf,&lenrecv))==1){
				Sendn(wrapper->response_get.fd,ERRORE402,strlen(ERRORE402));
				close(wrapper->response_get.fd);
				close(serverfd);
				pthread_exit(NULL);
				}
			}
		}
		wrapper->response_get.expire = wrapper->response_inf.expire;//copio il valore expire TOTALE del file richiesto
		close(serverfd);/*	chiudo il socket tanto non è più utilizzabile	*/
	}/*	chiusura while(temp<=0)	se arrivo qui allora ho ricevuto un 202*/
	return(1);
}

/*	Funzione usata per:
 * - creare socket di comunicazione con il server
 * - inviare richiesta di tipo GET
 * - ricevere risposta dal server di tipo GET 201
 * - controllare la presenza di errori 405 oppure 403 oppure 402	*/

int handlerResponseGet(struct RequestWrapper *wr){
	int serverfd,porta_server, temp=0,ris=0;
	char *IPserver;
			
	IPserver=inet_ntoa(wr->request.serveraddr);
	porta_server=wr->request.port;
	/*	questo while viene usato per reinviare le richieste in caso di 
	* SIMULO ERRORE del server	*/
	while(temp<=0){
		/*	creazione nuovo socket per comunicazione con server	*/
		serverfd=TCP_setup_socket_server(IPserver,porta_server);
		
		/*	invio richiesta GET al server*/
		ris=Sendn(serverfd,wr->request_get.buf,strlen(wr->request_get.buf));
		ris=0;
		/*	svuoto il buffer per un eventuale errore*/
		bzero(wr->response_get.buf,strlen(wr->response_get.buf));
		while(1){/* ricezione contenuto risposta GET dal server	*/
			//printf("\nfaccio una recv dentro la handlerresponse get\n");
			ris=recv(serverfd,wr->response_get.buf+ris,sizeof(wr->response_get.buf),0);
			//printf("\n%s fine e ris vale %d\n",qrisGet->buf,ris);
			
			if (ris==0){break;}/*	ha finito di ricevere dati	*/
			if (ris<0){ /*	timeout_inactivity_seconds scaduto	*/
				perror("recv"); printf("\n\nBLOCCO DEL SERVER oppure Server non attivo \n\n");
				close(serverfd);//chiudo l'attuale connessione con il server
				break;//provo ad instaurare una nuova connessione con il server
				}
		}
	printf("ricevuti dati dal server: %s",wr->response_get.buf);
	int lenrecv=strlen(wr->response_get.buf);
	temp=check201(&wr->response_get,&lenrecv);
	//printf("\n\nil valore di temp subito dopo la check201 è:%d\n\n",temp);
		if(temp==0){  /*	SIMULO ERRORE	*/
			/*	bisogna ripetere la richiesta	*/
			printf("Errore ricezione risposta GET con RANGE\nRICONNESSIONE IN CORSO....\n");
			}
		if(temp<0){	/*	per come è impostato il progetto ci sará 405	*/
			//printf("controllo il 405\n");
			if((ris=check405(wr->response_get.buf,&lenrecv))==1){
				Sendn(wr->response_get.fd,ERRORE405,strlen(ERRORE405));
				close(wr->response_get.fd);
				close(serverfd);
				pthread_exit(NULL);
				}
			else {/*	se arrivo qui ho ricevuto un 402	*/
				//printf("controllo per un 402\n");
				if((ris=check402(wr->response_get.buf,&lenrecv))==1){
				Sendn(wr->response_get.fd,ERRORE402,strlen(ERRORE402));
				close(wr->response_get.fd);
				close(serverfd);
				pthread_exit(NULL);
					}
				else{
					//printf("controllo per un 403\n");
					if((ris=check403(wr->response_get.buf,&lenrecv))==1){
						Sendn(wr->response_get.fd,ERRORE403,strlen(ERRORE403));
						close(wr->response_get.fd);
						close(serverfd);
						pthread_exit(NULL);
						}
				}
			}
			/*	chiudo il socket tanto non è più utilizzabile	*/
			close(serverfd);
		}
	}/*	chiusura while(temp<=0)	se arrivo qui allora ho ricevuto un 201*/
	return(1);
}/*fine handlerresponseget*/


void *thread_func(void* args){
	
	struct RequestWrapper *wr = args;
	
	int r = wr->request_get.temp;
	int tmp;
	if(r < wr->request_get.maxconnect-1){
		wr->request_get.range.first=(10*r)+1;
		wr->request_get.range.last=(10*r)+10;
		/*	strutturazione richiesta GET con RANGE limitato	*/
		bzero(wr->request_get.buf,strlen(wr->request_get.buf));//azzero il buffer per "scorie"
		strncpy(wr->request_get.buf,wr->request.buf,strlen(wr->request.buf)-1);//copio solo la prima riga della richiesta
		sprintf(wr->request_get.buf,"%sRange %d-%d\n\n",wr->request_get.buf,wr->request_get.range.first,wr->request_get.range.last);
						if((tmp=handlerResponseGet(wr))!=1){
							//return(-1);
							pthread_exit(NULL);
							}
						}
					else{
						wr->request_get.range.first=(10*r)+1;
						wr->request_get.range.last=-1;
						/*	strutturazione richiesta GET con RANGE senza limite	*/
						bzero(wr->request_get.buf,strlen(wr->request_get.buf));
						strncpy(wr->request_get.buf,wr->request.buf,strlen(wr->request.buf)-1);
						sprintf(wr->request_get.buf,"%sRange %d-\n\n",wr->request_get.buf,wr->request_get.range.first);
						if((tmp=handlerResponseGet(wr))!=1){
							pthread_exit(NULL);
							}
						}
					nconn[wr->posizione]--;
					pthread_mutex_lock(&mutex);
					while(matrix[wr->posizione][r]!=1){/* se non e' il mio turno*/
						printf("il thread %d si blocca\n",r);
						pthread_cond_wait(&cond, &mutex);
						}
						if(wr->request_get.range.first==1){
						//printf("\n**********il thread %d fa la INVIO200****\n",r);
						invio200(&wr->response_get,wr->response_inf.Len);
						}
						else{
							//invio200bis(wr->response_get);
							Sendn(wr->response_get.fd,wr->response_get.dati,strlen(wr->response_get.dati));
						//	printf("\n*****il thread %d INVIA******%s****\n",r,wr->response_get.dati);
						}
					printf("sblocco il thread %d\n",r+1);
					matrix[wr->posizione][r+1]=1;
					pthread_mutex_unlock(&mutex);
					pthread_cond_broadcast(&cond);
					pthread_exit(NULL);					
	}



int inviaricevi(struct RequestWrapper *wrapper){
	int i=0;
	int pth;
	pthread_t id[wrapper->request_get.maxconnect];
	struct RequestWrapper cpy_local[wrapper->request_get.maxconnect];
	matrix[wrapper->posizione][0]=1;
	
	/*	copio la prima riga meno un EOL	*/
	while(1){
		while(i < wrapper->request_get.maxconnect && nconn[wrapper->posizione] < 5){
			cpy_local[i] = *wrapper;
			//memcpy(&cpy_local[i],wrapper,sizeof(cpy_local));
			cpy_local[i].request_get.temp = i;
			if((pth=pthread_create(&id[i],NULL,thread_func,&cpy_local[i]))!=0){
					printf("creazione thread %d fallita\n",i);
					exit(-1);
					}
				else{
					printf("creato thread %d\n",i);
					}
			i++;nconn[wrapper->posizione]++;
			//sleep(1);
			}
			if(i>=wrapper->request_get.maxconnect){//se ho richiesto tutte le parti
				for(i=0;i<wrapper->request_get.maxconnect;i++) {
					printf("aspetto che il thread %d termini\n",i);
					pthread_join( id[i],NULL);
					}
				/*copio l'intero messaggio nella struttura originale
				mi serve per la cache*/
				for(i=0;i<wrapper->request_get.maxconnect;i++){
					strcat(wrapper->response_get.dati,cpy_local[i].response_get.dati);
					}
				printf("terminati tutti i thread\n");
				return(0);
				}
		}
	}/*chiusura invia&ricevi*/

int *funzione_nuova(void* args){
	int coop=tfila++;
	struct RequestWrapper *struct1 = args;
	struct timeval tv;
	struct stat mod;
	int ris,mj,tmp;
	int len2=strlen(GETMHTTP);
	int lock = 0;
	/*	ricezione richiesta da parte del client	*/
		ris=recv(struct1->response_get.fd,struct1->request.buf,sizeof(struct1->request.buf),0);
		gettimeofday(&tv, NULL);
		//printf("dal client ho ricevuto questo:%s\n\n\n\n\n\n",struct1->request.buf);
		struct1->request.lenreq+=ris;
		ris=getIP(&struct1->request,&len2);
		if(ris!=1){/*{IPserver=req.serverIP;}*/	
			Sendn(struct1->response_get.fd,ERRORE403,strlen(ERRORE403));
			close(struct1->response_get.fd);
			pthread_exit(NULL);}
		ris=getDuePunti(&struct1->request,&len2);
		if(ris!=1){
			Sendn(struct1->response_get.fd,ERRORE403,strlen(ERRORE403));
			close(struct1->response_get.fd);
			pthread_exit(NULL);}		
		if((ris=getPort(&struct1->request,&len2))!=1){
			Sendn(struct1->response_get.fd,ERRORE403,strlen(ERRORE403));
			close(struct1->response_get.fd);
			pthread_exit(NULL);}
		ris=getSlash(&struct1->request,&len2);
		if(ris!=1) {
			Sendn(struct1->response_get.fd,ERRORE403,strlen(ERRORE403));
			close(struct1->response_get.fd);
			pthread_exit(NULL);}
		ris=getPathAndEOL(&struct1->request,&len2);
		if(ris!=1) {
			Sendn(struct1->response_get.fd,ERRORE403,strlen(ERRORE403));
			close(struct1->response_get.fd);
			pthread_exit(NULL);}
		//printf("PATH DEL FILE:%s\n",wrapper->request.path);
		ris=Get2Inf(&struct1->request);
		//printf("INF:%s\n",wrapper->request.inf);
		/*********************	INIZIO CACHE	*********/
		while((strlen(cache[lock].nome)!=0)&& lock < 20){
			if(strcmp(struct1->request.path,cache[lock].nome)!=0){
				lock++;
				if(lock >= 20){
					lock = -1;
					break;
					}
				//inc=lock;
				}
			else{/*	se l'ho trovato nella cache	*/
				arraycache[lock]=1;
				//inc=lock;
				if(tv.tv_sec < cache[lock].expire+cache[lock].birth){/*	se è ancora valido*/
					/*invio al client*/
					stat(struct1->request.path,&mod);
					if(mod.st_mtime < cache[lock].birth){//modificato da quando è nella cache?
						Sendn(struct1->response_get.fd,cache[lock].buf,strlen(cache[lock].buf));
						close(struct1->response_get.fd);//chiudo fd
						gettimeofday(&tv, NULL);
						cache[lock].birth=tv.tv_sec;//aggiorno birth del file
						arraycache[lock]=0;
						pthread_exit(NULL);
						}
					}
				/*	se arrivo qui o è scaduto oppure è stato modificato
					dopo essere stato messo nella cache	*/
					
					memset(&cache[lock],0,sizeof(struct Life));
					arraycache[lock]=0;					
					break;
					
				}
			}//fine while strlen.....

		/********************	FINE CACHE	***********/
		if((ris=handlerResponseInf(struct1))==-1){//potenziale 404,allora devo toglierlo subito dalla cache
			memset(&cache[lock],0,sizeof(struct Life));
			arraycache[lock]=0;
			pthread_exit(NULL);
		}
		if((struct1->request_get.maxconnect=checkLen(&struct1->response_inf))==0){//eventuale file Vuoto.mhtml
			Get200(struct1->response_inf.buf);//leggera modifica al codice msg 202->200
			Sendn(struct1->response_get.fd,&struct1->response_inf,strlen(struct1->response_inf.buf));
			close(struct1->response_get.fd);
			turno[coop]=0;
			tfila--;
			//bzero(array,sizeof(array));
			for(mj=0;mj<(MAXLENDATA/10)+3;mj++){
				matrix[struct1->posizione][mj] = 0;
			}
			pthread_exit(NULL);
			}
		printf("il numero di connessioni da fare è=%d\n",struct1->request_get.maxconnect);			
		inviaricevi(struct1);
		gettimeofday(&tv,NULL);	//tempo di inserimento nella cache
		close(struct1->response_get.fd);
		
		/*azzero la posizione-sima della matrice che ho appena finito di utilizzare*/
		for(mj=0;mj<(MAXLENDATA/10)+3;mj++){
			matrix[struct1->posizione][mj] = 0;
		}
		/************* INSERIMENTO NELLA CACHE ***********/
		/*se inizialmente non c'erano posti liberi nella cache*/
		if(lock == -1){
			if((tmp=posto_libero())==-1){//nemmeno adesso, quindi il file non può essere messo in cache
				pthread_exit(NULL);//esco direttamente
			}//altrimenti
			else lock = tmp;//inserisco con indice lock-esimo il file nella cache
		}
		sprintf(cache[lock].buf,"200\nLen %d\nExpire %d\n\n%s",struct1->response_inf.Len,struct1->response_get.expire,struct1->response_get.dati);
		strcpy(cache[lock].nome,struct1->request.path);
		cache[lock].expire=struct1->response_inf.expire;
		cache[lock].birth=tv.tv_sec;
		/**************** FINE INSERIMENTO NELLA CACHE ********/
		memset(struct1,0,sizeof(struct RequestWrapper));
		/*rendo di nuovo disponibile il posto*/
		turno[coop]=0;
		tfila--;
		//printf("%s\n",struct1->response_get.dati);
		pthread_exit(NULL);
	}/*chiusura funzione_nuova*/


static void usage(void){
	printf("usage: ./proxy PORTA_PROXY	IP_PROXY_SERVER\n");
	}
	
int main(int argc, char *argv[]){
	
   	struct sockaddr_in Client;
	struct RequestWrapper wrapper[100];
	short int porta_proxy;
	int listenfd, clientfd, ris=0;
	int ticket;
	socklen_t len;
	char localIP[32];/*	ip del proxy di default 127.0.0.1*/
	pthread_t tid[100];
	pthread_t cid;
	
	if(argc==1) { 
		printf ("\nuso i parametri di default\n%s\n", PARAMETRI);
		porta_proxy=55554;
		strcpy(localIP,"127.0.0.1");		
	}
	else if(argc>3){
		printf("Errore: accettati al massimo due parametri\n");
		usage();
		exit(1);
		}
		else { /*	leggo i parametri da linea di comando	*/
		porta_proxy=atoi(argv[1]);
		strcpy(localIP,argv[2]);
		}

	ris=TCP_setup_socket_listening(&listenfd,porta_proxy);
	if(ris==0) return(0);	
	ris=SetsockoptReuseAddr(listenfd);
	if(ris==0) return(0);
	for(ris=0;ris<100;ris++){
		initRequest(&wrapper[ris].request_get);
		}
	//creo un thread che controlla continuamente la validità dei file nella cache
	pthread_create(&cid,NULL,(void *)impresa_pulizie,NULL);
	while(1){		/*	lato server	*/

		//printf("\nsono nel while(1)\n");
		if((clientfd= accept(listenfd,(struct sockaddr*)&Client,&len))<0){
			perror("errore nella accept\n");
			exit(-1);
		}
		//printf("\naccept completata\n");
		/*ricerca posto libero nell'array di strutture requestwarpper*/
		ticket=0;
		while(turno[ticket]!=0){
				ticket++;}
		turno[ticket]=1;//occupo il posto
		wrapper[ticket].posizione = ticket;//assegno indice di riconoscimento
		wrapper[ticket].response_get.fd=clientfd;
		pthread_create(&tid[ticket],NULL,(void *)funzione_nuova,&wrapper[ticket]);
	      
		}/*chiusura loop d'ascolto del proxy*/
}//chiusura main


	





