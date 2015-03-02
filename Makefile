GCCFLAGS= -Wall -Wunused -ansi -pedantic -ggdb 
LINKERFLAGS=-lpthread -lm

all: Server2.exe Client2.exe proxy

Client2.exe: Client2.o Util.o
	gcc -o Client2.exe ${GCCFLAGS} Client2.o Util.o ${LINKERFLAGS} 

Client2.o: Client.c Util.h 
	gcc -c ${GCCFLAGS} -DCLIENT2 -o Client2.o Client.c

Server2.exe: Server2.o Util.o
	gcc -o Server2.exe ${GCCFLAGS} Server2.o Util.o ${LINKERFLAGS} 

Server2.o: Server.c Util.h 
	gcc -c ${GCCFLAGS} -DSOLORANGE -o Server2.o Server.c

proxy: proxy.o utilProxy.o
	gcc -o proxy proxy.o -lpthread

proxy.o: proxy.c utilProxy.h 
	gcc -c -Wall proxy.c

Util.o: Util.c Util.h 
	gcc -c ${GCCFLAGS} Util.c

utilProxy.o: utilProxy.c utilProxy.h 
	gcc -c ${GCCFLAGS} utilProxy.c


clean:	
	rm -f core* *.stackdump
	rm -f *.exe
	rm -f Server2.o Server2.exe
	rm -f Client2.o Client2.exe
	rm -f proxy.o proxy

	rm -f Util.o
	rm -f utilProxy.o

cleanall:	clean
	rm -f *~

