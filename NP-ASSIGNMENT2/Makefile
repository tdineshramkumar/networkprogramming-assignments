all: dns_client echoserver blockingio nonblockingio twoprocesses twothreads fileserver multiplethreads nonblockingconnect echoserver

dns_client: dns_client.c
	gcc dns_client.c -o dns_client 

echoserver: echoserver.c
	gcc echoserver.c -o echoserver 
blockingio: blockingio.c
	gcc blockingio.c -o blockingio 

nonblockingio: nonblockingio.c
	gcc nonblockingio.c -o nonblockingio 

twothreads: twothreads.c
	gcc twothreads.c -o twothreads -lpthread 

twoprocesses: twoprocesses.c
	gcc twoprocesses.c -o twoprocesses 

fileserver: fileserver.c
	gcc fileserver.c -o fileserver 

multiplethreads: multiplethreads.c
	gcc multiplethreads.c -o multiplethreads -lpthread 

nonblockingconnect: nonblockingconnect.c
	gcc nonblockingconnect.c -o nonblockingconnect 

clean:
	rm dns_client nonblockingio blockingio twoprocesses twothreads fileserver multiplethreads nonblockingconnect echoserver
