/* Client.c  */

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

/* #define DEBUG */

#include "Util.h"

extern void srandom(unsigned int seed);
extern long int random(void);

/* valori da esplicitare come specifiche di progetto 
 * dire anche che nei file scaricati 
 * non deve essere presente il carattere di valore 0
 */
#define MAXNUMEMBEDDEDOBJECT 10
#define MAXNUMLINK 10
#define MAXLENPATH 2048
#define MAXLENREQ ((MAXLENPATH)+1024)
#define MAXLENDATA 5000
#define MAXLENRESP ((MAXLENREQ)+MAXLENDATA)
/* fine valori da esplicitare come specifiche di progetto */


int accettaInputTastiera=0;
int printed=0;

struct LINKS {
	int numlinks;
	int idxs[MAXNUMLINK];
	char links[MAXNUMLINK][MAXLENPATH];
	/* char linkedbufs[MAXNUMLINK][MAXLENDATA]; */
};

typedef struct MappaDaVisualizzare {
	char buf[MAXLENDATA*(1+MAXNUMLINK+MAXNUMEMBEDDEDOBJECT)];
	int len;
	struct LINKS L;
} MDV;


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



static int RecvWithDelay(int fd, const void *buf, size_t n) {
	
	int ris, myerrno;

	ris=recv(fd,(void*)buf,n,0);
	myerrno=errno;
	if(ris>0) {
		/* inserisco una attesa proporzionale al numero di bytes */
		attesa(ris/3);
		errno=myerrno;
		return(ris);
	}
	else
		return(ris);
}


static int getDuePunti(char *buf, int len, int *plen) {
	/* cerco un : */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( buf[*plen] != ':' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getSlash(char *buf, int len, int *plen) {
	/* cerco un / */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( buf[*plen] != '/' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

#if 0
static int getEOL(char *buf, int len, int *plen) {
	/* cerco un \n */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( buf[*plen] != '\n' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}
#endif

static int getPunto(char *buf, int len, int *plen) {
	/* cerco un . */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( buf[*plen] != '.' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getPuntoEVirgola(char *buf, int len, int *plen) {
	/* cerco un . */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		if( buf[*plen] != ';' ) return(-1); /* errato */
		else {
			(*plen)++; /* found, len punta al successivo carattere */
			return(1); /* found */
		}
	}
}

static int getInt(char *buf, int len, int *plen, int *pint) {
	/* cerco un intero */
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		int ris; char str[128];

		ris=sscanf(buf + (*plen), "%i", pint);
		if(ris!=1)	return(-1); /* errato */

		if( (*pint<0) || (*pint>255) ) return(-1); /* errato */

		sprintf(str,"%i", *pint );
		*plen += strlen(str);
		return(1); /* found */
	}
}


static int getIP(char *buf, int len, int *plen, struct in_addr *pinaddr) {

	int n, ris, newlen=*plen;
	char strIP[32];
	struct in_addr ina;

	/* cerco primo byte */
	ris=getInt(buf,len,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto(buf,len,&newlen);
	if(ris!=1) return(ris);
	/* cerco secondo byte */
	ris=getInt(buf,len,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto(buf,len,&newlen);
	if(ris!=1) return(ris);
	/* cerco terzo byte */
	ris=getInt(buf,len,&newlen, &n);
	if(ris!=1) return(ris);
	ris=getPunto(buf,len,&newlen);
	if(ris!=1) return(ris);
	/* cerco quarto byte */
	ris=getInt(buf,len,&newlen, &n);
	if(ris!=1) return(ris);
	/* ok, c'e' un indirizzo IP */
	memcpy(strIP,buf+(*plen), newlen-(*plen) );
	strIP[newlen-(*plen)]=0;
	ris=inet_aton(strIP,&ina);
	if(ris==0) {
		perror("inet_aton failed :");
		return(-1); /* no IP */
	}
	memcpy(pinaddr,&ina,sizeof(struct in_addr));
	*plen=newlen;
	return(1);
}

static int getPort(char *buf, int len, int *plen, int *pport) {
	/* cerco un intero */
	
	if( (len - *plen) < 1 ) return(0); /* incompleto */
	else {
		int ris; char str[128];

		ris=sscanf(buf + (*plen), "%i", pport );
		if(ris!=1)	return(-1); /* errato */

		if( (*pport<0) || (*pport>65535) ) return(-1); /* errato */

		sprintf(str,"%i", *pport );
		*plen += strlen(str);
		return(1); /* found */
	}
}



static int getPathAndMAGGIORE(char *buf, int len, int *plen, char *path) {

	int n=0;
	while(*plen+n<len) {
		if(buf[*plen+n] == '>' ) {
			path[n] = 0;
			*plen += n+1;
			return(1);
		}
		else {
			path[n] = buf[*plen+n];
			n++;
		}
	}
	/* not found \n means incompleto */
	return(0);
}


static void initMDV(MDV *pmdv ) {
	pmdv->len=0;
	pmdv->buf[0]=0;
	pmdv->L.numlinks=0;
}


static int check200OK( char *buf,int len, char *bufdst, int *plendst) {
	
	if(len<1) return(0); /* leggere ancora */

	if(len>=1) 
		if(buf[0]!='2') 
			return(-1);

	if(len>=2)
		if(buf[1]!='0')
			return(-1);
		
	if(len>=3) 
		if(buf[2]!='0')
			return(-1);
		
	if(len>=4)
		if(buf[3]!='\n')
			return(-1);

	/* cerco se c'e' Len xxxx\n  */
	if(len>=5) 
		if(buf[4]!='L')
			return(-1);
		
	if(len>=6) 
		if(buf[5]!='e')
			return(-1);
		
	if(len>=7)
		if(buf[6]!='n')
			return(-1);

	if(len>=8)
		if(buf[7]!=' ')
			return(-1);
		
	
	if(len>=9) {

		int ris, len2, len3;
		char str[1024];

		ris=sscanf(buf+8, "%i", &len2);
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if( len2<0 ) return(-1); /* errato, len negativo */

		/* salvo la lunghezza del file scaricato */
		*plendst=len2;

		sprintf(str,"%i", len2 );
		/* cerco il carattere successivo alla fine del numero trovato */
		len2= 9+strlen(str);

		if(len>=len2)
			if(buf[len2-1]!='\n')
				return(-1);
		
		/* cerco se c'e' Expire xxxx\n  */
		if(len>=len2+1)
			if(buf[len2+1-1]!='E')
				return(-1);
		
		if(len>=len2+2)
			if(buf[len2+2-1]!='x')
				return(-1);
		
		if(len>=len2+3)
			if(buf[len2+3-1]!='p')
				return(-1);
		
		if(len>=len2+4)
			if(buf[len2+4-1]!='i')
				return(-1);
		
		if(len>=len2+5)
			if(buf[len2+5-1]!='r')
				return(-1);
		
		if(len>=len2+6)
			if(buf[len2+6-1]!='e')
				return(-1);
		
		if(len>=len2+7)
			if(buf[len2+7-1]!=' ')
				return(-1);

		if(len<len2+7+1)
			return(0);

		ris=sscanf(buf+len2+7, "%i", &len3);
		if(ris!=1)	return(-1); /* errato, ci sono caratteri ma non e' numero */
		if( len2<0 ) return(-1); /* errato, len negativo */

		/* me ne frego dell' expire */
		sprintf(str,"%i", len3 );
		/* cerco il carattere successivo alla fine del numero trovato */
		len2= len2 + 8 + strlen(str);

		/* controllo se c'e' l' EOL */
		if(len>=len2)
			if(buf[len2-1]!='\n')
				return(-1);
	
		/* cerco la fine del messaggio */
		if(len>=(len2+1))
			if(buf[len2+1-1]!='\n')
				return(-1);
		
		/* se arrivo qui, 
		 * len2+1 e' l'indice dell'ultimo byte del messaggio mhttp
		 *
		 * ma dovrebbero esserci ancora *plendst byte
		 * di cui il primo char dovrebbe avere indice len2+2
		 *
		 */

		if( len < (len2+1+(*plendst)) )
			return(0);

		/* copio nel buffer destinazione i dati allegati nel messaggio 200 */
		memcpy(bufdst, buf+len2+1, *plendst);

		return(1); /* messaggio 200 corretto e completo, copiato in bufdst */
									
	}
	return(0); /* leggere ancora */
}


static int Get(char *proxyIP, int proxyport, char *URL, char *buf, int *plen) {
	char buf1[MAXLENRESP];
	int ris, fd, len1;
	char request[MAXLENREQ];

	ris=TCP_setup_connection(&fd,proxyIP,proxyport);
	if(ris==0) return(0); /* errore */

	/* send GET request */
	sprintf(request,"GET %s\n\n",URL);
	ris=Sendn(fd,request,strlen(request));
	if(ris!=strlen(request)) return(0);

	/* receive response */
	len1=0;
	while(1) {
		/* ris=recv(fd,buf1+len1,MAXLENRESP-len1,0); */
		ris=RecvWithDelay(fd,buf1+len1,MAXLENRESP-len1);
		if(ris<0) {
			if(errno==EINTR) ; /* do nothing, repeat */
			else return(0);
		}
		else if(ris==0) /* connection closed by peer */
			return(0);
		else { /* ris>0 */
			len1+=ris;
			buf1[len1]=0; /* un bel terminatore non guasta mai */

			ris=check200OK(buf1,len1,buf,plen);
#ifdef DEBUG
			printf("check200OK \"%s\"\nris %d\n",buf1,ris);
#endif
			if(ris==0) ;  /* msg incompleto, read again */
			else if(ris==1) { /* msg 200 ok, gia' copiato msg in buf */
				return(200);
			}
			else { /* -1 formato msg errato oppure NON 200 ok */
				return(0);
			}
		}
	}
	return(0);
}

static void aggiungiaMDV(char *buf1, int len1, MDV *pmdv) {
	memcpy(pmdv->buf+pmdv->len, buf1, len1);
	pmdv->len += len1;
	pmdv->buf[pmdv->len]=0; /* un terminatore in piu' non guasta mai */
}



/* cerca  <REF=mhttp://130.136.2.34:9876/piri/picchio> */
static int isREF(char *buf, int i, int *plenparsed, int len, char *objectURL){

	int ris;
	char temppath[MAXLENREQ];
#define REFMHTTP "REF=mhttp://"

	if( ((strlen(REFMHTTP)) +1 /*nomefile almeno 1 char*/ +1 /* il > */ ) > (len-i) ) {
		/* formato errato */
		return(-1);
	}
	if( strncmp(REFMHTTP,buf+i,strlen(REFMHTTP))==0 ) {

		int port;
		struct in_addr inaddr;

		int len1=i+strlen(REFMHTTP);
		ris=getIP(buf,len,&len1,&inaddr);
		if(ris!=1) return(ris);
		ris=getDuePunti(buf,len,&len1);
		if(ris!=1) return(ris);
		ris=getPort(buf,len,&len1,&port);
		if(ris!=1) return(ris);
		ris=getSlash(buf,len,&len1);
		if(ris!=1) return(ris);
		ris=getPathAndMAGGIORE(buf,len,&len1,temppath);
		if(ris!=1) return(ris);

		sprintf(objectURL,"mhttp://%s:%i/%s",inet_ntoa(inaddr),port,temppath);
		*plenparsed=len1;
		return(1);
	}
	else {
		/* NON e' un REF */
		return(-1);
	}

}

/* cerca  <IDX=1034;REF=mhttp://130.136.2.34:9876/piri/picchio> */
static int isIDXREF(char *buf, int i, int *plenparsed, int len, MDV *pmdv, char *objectURL){

	int ris;
#define IDX "IDX="

	if( (strlen(IDX) +1 /* numero */ +1 /* ; */ + (strlen(REFMHTTP)) +1 /*nomefile*/ +1 /* il > */ ) > (len-i) ) {
		/* formato errato */
		return(-1);
	}

	if( strncmp(IDX,buf+i,strlen(IDX))==0 ) {
		int idx;
		int len1=i+strlen(IDX);
		/* in realta' non cerco una porta ma un numero intero */
		ris=getPort(buf,len,&len1,&idx);
		if(ris!=1) return(ris);
		ris=getPuntoEVirgola(buf,len,&len1);
		if(ris!=1) return(ris);

		/* ora cerco il REF= */
		if( isREF(buf,len1,plenparsed,len,objectURL)==1 ) {

			/* ora *plenparsed e' l'indice del carattere 
			 * successivo al carattere > che termina REF 
			 */
			char straux[1024];

			/* copio l'indice e il percorso nel vettore dei link */
			strcpy(pmdv->L.links[pmdv->L.numlinks],objectURL);
			pmdv->L.idxs[pmdv->L.numlinks]=idx;
			pmdv->L.numlinks++;
			

			/* copio nella mappa i caratteri "<idx>" 
			 * e incremento la len della mappa
			 */
			sprintf(straux,"<%i>", idx);
			strcpy( pmdv->buf + pmdv->len, straux );
			pmdv->len += strlen(straux);

			return(1);
		}
		else {
			/* NON e' un REF */
			return(-1);
		}
	}
	else {
		/* NON e' un REF */
		return(-1);
	}

}


/* scandisce il file ottenuto, 
 * se trova <IDX=NUM;REF=...> lo mette nella mappa, lascia solo <NUM>
 * se trova <REF=...> chiede di caricarlo e sostituirlo al riferimento ret 1
 * se durante un download capita un problema, fine ret -1
 *
 * man mano che aggiunge bytes alla mappa incrementa len della mappa
 *
 * quando ha finito ret 0
 */
static int parse(char *buf,int *plenparsed,int len,MDV *pmdv,char *objectURL) {

	int i;

	/* NO VIENE FATTO FUORI pmdv->len=0; 
		 pmdv->len e plenparsed vengono inizializzati fuori, io devo modificarli
	 */

	for( i=*plenparsed; i<len; i++ ) {

		if(buf[i]=='<') {
		
			if(i+1<len) {
			
				/* passo al carattere successivo al < */
				++*plenparsed;
				i++;

				if( isREF(buf,i,plenparsed,len,objectURL)==1 ) {
					/* ora *plenparsed e' l'indice del carattere 
					 * successivo al carattere > che termina REF 
					 */
					return(1);
				}
				else if( isIDXREF(buf,i,plenparsed,len,pmdv,objectURL)==1 ) {

					/* ora *plenparsed indica il carattere > che termina IDXREF
					 * assegno ad i il valore di *plenparsed,
					 * MA LO DEVO DECREMENTARE DI 1 PERCHE' POI
					 * il for fa i++ e lo fa puntare al carattere successivo al >
					 */
					i=*plenparsed; 
					i--;
					/* continuo il parsing del carattere successivo al > */
				}
				else {
					/* errore */
					return(-1);
				}	
			}
			else { /* i+1 >= len  =>  errore, stringa finita prematuramente */
				return(-1);
			}


		}
		else {
			pmdv->buf[pmdv->len]=buf[i];
			pmdv->len++;
			/* (*plenparsed)++; */
			/* *plenparsed; */
			++*plenparsed;
		}
	}
	pmdv->buf[pmdv->len]=0; /* un terminatore in piu' non fa mai male */
	return(0); /* finito, ok */
}




static int GetAll(char *proxyIP, int proxyport, char *URL, MDV *pmdv ) {
	int ris, len;
	char buf[MAXLENRESP];

	initMDV(pmdv);
	/* scarica l'oggetto specificato dall' URL, 
	 * se ce la fa toglie l'intestazione mhttp 
	 * e lascia solo l'oggetto in buf
	 */
	ris=Get(proxyIP,proxyport,URL,buf,&len);

	if(ris==200) {
		int lenparsed=0;

		while(1) {
			char objectURL[MAXLENPATH];
			/* scandisce il file ottenuto, 
			 * se trova <IDX=NUM;REF=...> lo mette nella mappa, lascia solo <NUM>
			 * se trova <REF=...> chiede di caricarlo e sostituirlo al riferimento
			 * se durante un download capita un problema, fine
			 *
			 * man mano che aggiunge bytes alla mappa incrementa len della mappa
			 */
			ris=parse(buf,&lenparsed,len,pmdv,objectURL);
			if(ris==0) { /* fine parsing, tutto ok */
				return(1);
			}
			else if(ris==1) { /* c'e' oggetto embedded da caricare */
				int len1;
				char buf1[MAXLENRESP];

				ris=Get(proxyIP,proxyport,objectURL,buf1,&len1);
				if(ris==200) {
					aggiungiaMDV(buf1,len1,pmdv);
					/* e continuo il parsing nel while */

				}
				else {
					return(0);
				}
			}
			else { /* ris==-1 errore */
				return(0);
			}
		}
	}
	else {
		return(0); /* errore */
	}
}

static void clrscr(void) {
#ifndef DEBUG
	printf(CLEARSCREEN);
#endif
}

static void VisualizzaErrore( char *URL ) {
	clrscr();
	printf("%s%s%s\n", GIALLO, URL, WHITE ); 
	printf("%sImpossibile reperire completamente la risorsa%s\n",ROSA,WHITE); 
	fflush(stdout);
}

static char accettaInputRandom(MDV *pmdv) {
	int i;
	if(pmdv->L.numlinks<=0) return('q');
	/* estraggo un numero random tra 1 e numlinks
	 * e seleziono l'idx che trovo nella nel vettore idxs
	 * nella posizione specificata dal numero estratto
	 */
	i=(int)(random()%((long int)(pmdv->L.numlinks)));
	return(pmdv->L.idxs[i]);
}

static char accettaInputDaTastiera(MDV *pmdv) {
	int i, idx, ris;
	char str[1024], *ptr;
	while(1) {

		printf("\navailable q ");
		for(i=0;i<pmdv->L.numlinks;i++) 
			printf("%i ",pmdv->L.idxs[i]);
		printf("press key and ENTER ..... ");
		fflush(stdout);

		ptr=fgets(str,1024,stdin);
		if(ptr!=NULL) {
			if(str[0]=='q') return('q');
			else {
				ris=sscanf(str,"%i", &idx);
				if(ris==1) {
					/* cerco se c'e' un idx i */
					for(i=0;i<pmdv->L.numlinks;i++) {
						if(pmdv->L.idxs[i]==idx) {
							return( (char)idx);
						}
					}
				}
			}
		}
	}
	return('q'); /* unreacheable */
}

static void Visualizza( char *URL, MDV *pmdv) {
	int i;

	clrscr();
	printf("%s%s%s\n", GIALLO, URL, WHITE ); 

	for( i=0; i<pmdv->len; i++ ) {
		if(pmdv->buf[i]=='<')
			printf("%s<", RED);
		else if(pmdv->buf[i]=='>')
			printf(">%s", WHITE);
		else
			printf("%c", pmdv->buf[i]);
	}

	printf("\n"); 
	fflush(stdout);
}

static int navigaFromRisorsa(char *proxyIP, int proxyport, char *currentURL, int *pnumerrori, int *pnumfiles)
{
	char ch; int ris;

	do {
		/* download e visualizzazione di ciascuna risorsa */
		MDV mdv;
		++*pnumfiles;

		clrscr();
		printf("%s Fetching %s%s\n", GIALLO, currentURL, WHITE ); 

		ris=GetAll(proxyIP,proxyport,currentURL,&mdv);

		if(ris!=1) { 
			++*pnumerrori; 
			VisualizzaErrore(currentURL); 
			if(accettaInputTastiera==1) { /* user input, ammesso solo q */
				/* se input non accettabile ripeto */
				ch=accettaInputDaTastiera(&mdv);
			}
			else { /* termina uso di questa risorsa */
				sleep(2); 
				ch='q';
			}
		}
		else { 
			Visualizza(currentURL,&mdv);
			sleep(2); /* in ogni caso aspetta un secondo */

			if(accettaInputTastiera==1) { /* user input */
				/* se input non accettabile ripeto */
				ch=accettaInputDaTastiera(&mdv);
			}
			else if(accettaInputTastiera==2) { /* random input */
				/* se input non accettabile esco restituendo 'q' */
				ch=accettaInputRandom(&mdv);
			}
			else { /* termina uso di questa risorsa */
				ch='q';
			}
			if(ch!='q') {
				int j;
				for(j=0;j<mdv.L.numlinks;j++) {
					if(mdv.L.idxs[j]==((int)ch)) {
						strcpy(currentURL,mdv.L.links[j]);
						break;
					}
				}
			}
		}
	} while(ch!='q');
	return(1);	
}

#ifdef CLIENT2
#define PARAMETRIDEFAULT "1 2 127.0.0.1 55554 mhttp://127.0.0.1:55555/1.mhtml mhttp://127.0.0.1:55555/2bis.mhtml mhttp://127.0.0.1:55555/3.mhtml mhttp://127.0.0.1:55555/4.mhtml "
static void usage(void) 
{ printf("usage: ./Client2.exe SEME TIPOINPUT PROXY_IP PROXY_PORT URL1 URL2 ... URLn\n");
	printf("           con TIPOINPUT 1=DaTastiera 2=Random 0=NoInput\n");
	printf("\n");
	printf("   es  ./Client2.exe 1 2 127.0.0.1 55554 "
									"mhttp://127.0.0.1:55555/4000.mhtml "
									"mhttp://127.0.0.1:55555/2bis.mhtml "
									"mhttp://127.0.0.1:55555/1.mhtml "
									"mhttp://127.0.0.1:55555/1000.mhtml "
									"mhttp://127.0.0.1:55555/2.mhtml "
									"mhttp://127.0.0.1:55555/3.mhtml "
									"mhttp://127.0.0.1:55555/4.mhtml "
									"mhttp://127.0.0.1:55555/2.mhtml "
									"\n");
}
#else 
#define PARAMETRIDEFAULT "1 2 127.0.0.1 55554 mhttp://127.0.0.1:55555/1.mhtml mhttp://127.0.0.1:55555/2bis.mhtml mhttp://127.0.0.1:55555/3.mhtml mhttp://127.0.0.1:55555/4.mhtml "
static void usage(void) 
{ printf("usage: ./Client1.exe SEME TIPOINPUT PROXY_IP PROXY_PORT URL1 URL2 ... URLn\n");
	printf("           con TIPOINPUT 1=DaTastiera 2=Random 0=NoInput\n");
	printf("\n");
	printf("   es  ./Client1.exe 1 2 127.0.0.1 55554 "
									"mhttp://127.0.0.1:55555/1.mhtml "
									"mhttp://127.0.0.1:55555/2bis.mhtml "
									"mhttp://127.0.0.1:55555/2.mhtml "
									"mhttp://127.0.0.1:55555/3.mhtml "
									"mhttp://127.0.0.1:55555/4.mhtml "
									"mhttp://127.0.0.1:55555/2.mhtml "
									"mhttp://127.0.0.1:55555/1000.mhtml "
									"\n");
}
#endif

int main(int argc, char *argv[])
{
	int  numrisorse, seed, i, numfiles, numerrori;
	char URLs[20][1024];
	char proxyIP[32];
	int  proxyport;
	struct timeval start, end;

	if(argc==1) { 
		printf ("uso i parametri di default \n%s\n", PARAMETRIDEFAULT);
		seed=1;
		/* accettaInputTastiera=1; */
		accettaInputTastiera=2; /* random */

		strcpy(proxyIP,"127.0.0.1"); /* da modificare */
		proxyport=55554; /* da modificare */

#ifdef CLIENT2
		numrisorse=8; /* 5; */
		/* strcpy(URLs[0],"mhttp://127.0.0.1:55555/1.mhtml"); */
		strcpy(URLs[0],"mhttp://127.0.0.1:55555/4000.mhtml");
		strcpy(URLs[1],"mhttp://127.0.0.1:55555/2bis.mhtml"); /* non esiste */
		strcpy(URLs[2],"mhttp://127.0.0.1:55555/1.mhtml");
		strcpy(URLs[3],"mhttp://127.0.0.1:55555/1000.mhtml");
		strcpy(URLs[4],"mhttp://127.0.0.1:55555/2.mhtml");
		strcpy(URLs[5],"mhttp://127.0.0.1:55555/3.mhtml");
		strcpy(URLs[6],"mhttp://127.0.0.1:55555/4.mhtml");
		strcpy(URLs[7],"mhttp://127.0.0.1:55555/2.mhtml");
#else  /* per Client1 */
		numrisorse=7; /* 5; */
		strcpy(URLs[0],"mhttp://127.0.0.1:55555/1.mhtml");
		strcpy(URLs[1],"mhttp://127.0.0.1:55555/2bis.mhtml"); /* non esiste */
		strcpy(URLs[2],"mhttp://127.0.0.1:55555/2.mhtml");
		strcpy(URLs[3],"mhttp://127.0.0.1:55555/3.mhtml");
		strcpy(URLs[4],"mhttp://127.0.0.1:55555/4.mhtml");
		strcpy(URLs[5],"mhttp://127.0.0.1:55555/2.mhtml");
		strcpy(URLs[6],"mhttp://127.0.0.1:55555/1000.mhtml");
#endif
	}
	else if(argc<6) { 
		printf ("necessari almeno 5 parametri\n"); usage(); Exit(1);
	}
	else { /* leggo parametri da linea di comando */
		seed=atoi(argv[1]);
		accettaInputTastiera=atoi(argv[2]);;
		strcpy(proxyIP,argv[3]); 
		proxyport=atoi(argv[4]);
		for(i=5,numrisorse=0; i<argc; i++,numrisorse++) {
			strcpy(URLs[numrisorse],argv[i]);
		}
	}
	init_random(seed);


	if ((signal (SIGHUP, sig_print)) == SIG_ERR) { perror("signal (SIGHUP) failed: "); Exit(2); }
	if ((signal (SIGINT, sig_print)) == SIG_ERR) { perror("signal (SIGINT) failed: "); Exit(2); }
	if ((signal (SIGTERM, sig_print)) == SIG_ERR) { perror("signal (SIGTERM) failed: "); Exit(2); }

	gettimeofday(&start,NULL);
	numerrori=0;
	numfiles=0;
	for(i=0;i<numrisorse;i++) {

		char currentURL[MAXLENPATH];
		strcpy(currentURL,URLs[i]);

		navigaFromRisorsa(proxyIP,proxyport,currentURL,&numerrori,&numfiles);
	}

	gettimeofday(&end,NULL);
	printf("numfilesOK %i numerrori %i tempo %f secs\n", numfiles,numerrori, sec_differenza(end,start) );

	return(0);
}


