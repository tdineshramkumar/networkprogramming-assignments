/*
	T Dinesh Ram Kumar
	2014A3A70302P
	DNS Client in C ... 
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define try(expr,msg) 	if((expr)==-1){\
							perror(msg);\
							exit(EXIT_FAILURE);\
						}
//#define LOCAL
#ifndef LOCAL
#define SERVERADDR  "8.8.8.8"
#else
#define SERVERADDR  "172.24.2.71"
#endif 

#define FG_BLACK "\033[30m"
#define FG_RED "\033[31m"
#define FG_GREEN "\033[32m"
#define FG_YELLOW "\033[33m"
#define FG_BLUE "\033[34m"
#define FG_MAGENTA "\033[35m"
#define FG_CYAN "\033[36m"
#define FG_WHITE "\033[37m"
#define FG_DEFAULT "\033[39m"

#define BG_WHITE "\033[47m"
#define RESET "\033[0m"

#pragma pack(1)
struct _header{
	uint16_t ID; 

	uint8_t RD:1 ;
	uint8_t TC:1 ;
	uint8_t AA:1 ;
	uint8_t OPCODE:4 ;
	uint8_t QR:1 ;

	uint8_t RCODE:4 ;
	uint8_t Z:3 ;
	uint8_t RA:1 ;

	uint16_t QDCOUNT ;
	uint16_t ANCOUNT ;
	uint16_t NSCOUNT ;
	uint16_t ARCOUNT ;
};
struct _question {
	// Note name not included4
	// char * QNAME ; // Not included in final ..
	uint16_t QTYPE ;
	uint16_t QCLASS ;
};
struct _resoucerecord {
	// Note name not included
	// char * NAME ; 
	uint16_t TYPE ;
	uint16_t CLASS ;
	uint32_t TTL ;
	uint16_t RDLENGTH ;
	uint8_t RDATA[] ; // Length from RDLENGTH
};
#pragma pack()

typedef struct _header header ;
typedef struct _header * Header;
typedef struct _question question;
typedef struct _question * Question ;
typedef struct _resoucerecord resoucerecord;
typedef struct _resoucerecord * Resoucerecord;

#define PRINT(count,msg,...) for (int i=0;i <count;i ++)printf("\t"); printf(msg , ##__VA_ARGS__);
// This function prints the dns header 
void printrcode(int errorcode) {
	#define CASE(id,msg) case id: printf("(%s)\n",msg); break;
	switch (errorcode) {
		CASE(0,"no error.");
		CASE(1,"format error.");
		CASE(2,"Problem at name server.");
		CASE(3,"Domain Reference Problem");
		CASE(4,"Query type not supported");
		CASE(5,"Administratively prohibited");
		default: printf("Reserved\n");
	}
}
// THis function prints the DNS header 
void printDNSHeader(Header h) {
	PRINT(1,"Transaction ID: %d\n",h->ID);
	PRINT(1,"Flags:\n");
	PRINT(2,"QR (Query?): %d\n", h->QR);
	PRINT(2,"OPCODE: %d\n", h->OPCODE);
	PRINT(2,"AA (Authoritative Answer?): %d\n", h->AA);
	PRINT(2,"TC (TrunCation): %d\n", h->TC);
	PRINT(2,"RD (Recursion Desired): %d\n", h->RD);
	PRINT(2,"RA (Recursion Available): %d\n", h->RA);
	PRINT(2,"RCODE (Response Code): %d ", h->RCODE); 
	printrcode(h->RCODE);
	PRINT(1,"QDCOUNT (Question Count): %d\n", ntohs(h->QDCOUNT) );
	PRINT(1,"ANCOUNT (Answer Count): %d\n", ntohs(h->ANCOUNT) ); 
	PRINT(1,"NSCOUNT (Nameserver Count): %d\n", ntohs(h->NSCOUNT));
	PRINT(1,"ARCOUNT (Addition Section Count): %d\n", ntohs(h->ARCOUNT));
}
// Note: This does not print name ..
void printQuestion(Question q) {
	switch(ntohs(q->QTYPE)) {
		case 1: PRINT(1,"QTYPE: %d (A)\n",ntohs(q->QTYPE)); break;
		case 2: PRINT(1,"QTYPE: %d (NS)\n",ntohs(q->QTYPE)); break;
		case 5: PRINT(1,"QTYPE: %d (CNAME)\n",ntohs(q->QTYPE)); break;
		case 6: PRINT(1,"QTYPE: %d (SOA)\n",ntohs(q->QTYPE)); break;
		case 12: PRINT(1,"QTYPE: %d (PTR)\n",ntohs(q->QTYPE)); break;
		case 15: PRINT(1,"QTYPE: %d (MX)\n",ntohs(q->QTYPE)); break;
		case 28: PRINT(1,"QTYPE: %d (AAAA)\n",ntohs(q->QTYPE)); break;
		default: PRINT(1,"QTYPE: %d\n",ntohs(q->QTYPE));
	}
	switch ( ntohs(q->QCLASS) ) {
		case 1: PRINT(1,"QCLASS: %d (IN)\n",ntohs(q->QCLASS)); break;
		default: PRINT(1,"QCLASS: %d\n",ntohs(q->QCLASS)); break;
	}
}
// This function prints the DNS formatted string 
int printdnsname(char msg[], char *name) {
// This function is causing problem 
//	printf("PRINT DNS NAME\n");
//	printf("SIZE: %d\n", name[0]);
	uint16_t size = name[0];
	if ( size & 0x80 && size & 0x40 ) {
		// Then pointer (go to that place and print)
		// <-------------------- PROBLEM IS HERE 
		uint8_t tmp = size ^ 0xc0 ;
		//if ( tmp == 0 ) printf("ZERO\n");
		//printf("NAME{1} %d\n",name[1] );
		uint16_t addr = tmp * 256u + ( (uint8_t) name[1]) ; 
//		printf("ADDR: %d (%d)\n",addr,tmp );
		printdnsname(msg,&(msg[addr]));
		//printf("NAME{1} %d\n",name[1] );
		return 2;
	}
	else if ( size == 0 ) return 1; // End of string 
	else {
		for (int i = 1; i <= size ; i++)  printf("%c", name[i]);
		printf("."); 
		return size + 1 + printdnsname(msg,name + size + 1);
	}
}
void printResourceRecord(char msg[], Resoucerecord r) {
	char buf[256], *chrptr;
	uint16_t *tmp ;
	uint32_t *tmp2 ;
	int pos ;
	switch(ntohs(r->TYPE)) {
		case 1: printf("\tTYPE: %d (A)\n",ntohs(r->TYPE)); break;
		case 2: printf("\tTYPE: %d (NS)\n",ntohs(r->TYPE)); break;
		case 5: printf("\tTYPE: %d (CNAME)\n",ntohs(r->TYPE)); break;
		case 6: printf("\tTYPE: %d (SOA)\n",ntohs(r->TYPE)); break;
		case 12: printf("\tTYPE: %d (PTR)\n",ntohs(r->TYPE)); break;
		case 15: printf("\tTYPE: %d (MX)\n",ntohs(r->TYPE)); break;
		case 28: printf("\tTYPE: %d (AAAA)\n",ntohs(r->TYPE)); break;
		default: printf("\tTYPE: %d\n",ntohs(r->TYPE));
	}
	switch ( ntohs(r->CLASS) ) {
		case 1: PRINT(1,"CLASS: %d (IN)\n",ntohs(r->CLASS)); break;
		default: PRINT(1,"CLASS: %d\n",ntohs(r->CLASS)); break;
	}
	printf("\tTTL: %u\n",ntohl(r->TTL)) ;
	printf("\tRDLENGTH: %d\n", ntohs(r->RDLENGTH));
	printf("\tRDATA:\n");
	switch(ntohs(r->TYPE)) {
		case 1: printf("\t\tIP ADDRESS: %s\n",inet_ntop(AF_INET,r->RDATA,buf,256)); break;
		case 2: case 5:	case 12: 
			printf("\t\tName Server: "); 
			printdnsname(msg,r->RDATA); printf("\n"); break ;
		case 6: // SOA
			printf("\t\tPrimary NS: ");
			pos = printdnsname(msg,r->RDATA);
			printf("\n\t\tAdmin MailBox: ");
			pos += printdnsname(msg,r->RDATA + pos);
			printf("\n");
			#define REPEAT_STMT(msg) ({\
			tmp2 = (uint32_t *) (r->RDATA + pos) ;\
			printf("\t\t%s: %d\n",msg,ntohl(*tmp2));\
			pos += sizeof(uint32_t); })
			REPEAT_STMT("Serial Number");
			REPEAT_STMT("Refresh Interval");
			REPEAT_STMT("Retry Interval");
			REPEAT_STMT("Expiration Limit");
			REPEAT_STMT("Minimum TTL");
			break ;
		case 15: 
			tmp = (uint16_t *) r->RDATA ;
			printf("\t\tPREFERENCE: %d\n", ntohs(*tmp));
			PRINT(2,"MAIL Exchanger: ");
			chrptr = r->RDATA + 2 ;
			printdnsname(msg, chrptr); printf("\n");
			break ;
		case 28: printf("\t\tIP ADDRESS: %s\n",inet_ntop(AF_INET6,r->RDATA,buf,256)); break ;
		default:
			for ( int i = 0 ; i < ntohs(r->RDLENGTH) ; i ++ ) {
				if ( i % 16 == 0 ) printf("\n");
				printf("%02x", r->RDATA[i]);
			}
			printf("\n");
	}
}

void printMessageContents(char msg[], char len) {
	char *tmp = msg ;
	int nsize, pos ;
	Header h = (Header) tmp;
	printf(FG_CYAN);
	PRINT(0,"DNS Message Header:\n");
	printDNSHeader(h);
	printf("\n");
	pos = sizeof(header);
	printf(FG_RED);
	// Now print the Question Section ...
	if ( ntohs(h->QDCOUNT) > 0 ) {
		PRINT(0,"DNS Question Section:\n");
		for ( int i = 0 ; i < ntohs(h->QDCOUNT) ; i ++ ) {
			PRINT(1,"QNAME: ");
			nsize = printdnsname(msg,msg+pos); 
			printf("\n");
			pos += nsize ;
			Question q = (Question) ( msg + pos );
			printQuestion(q);
			pos += sizeof(question);
			printf("\n");
		}
	}
	printf(FG_BLUE);
	if ( ntohs(h->ANCOUNT) > 0 ) {
		PRINT(0,"DNS Answer Section:\n");
		for ( int i = 0 ; i < ntohs(h->ANCOUNT) ; i ++ ) {
			printf("\r" BG_WHITE);
			printf(RESET);
			printf( FG_BLUE);
			PRINT(1,"NAME: ");
			nsize = printdnsname(msg,msg+pos); 
			printf("\n");
			pos += nsize ;
			Resoucerecord r = (Resoucerecord) ( msg + pos );
			printResourceRecord(msg,r);
			printf(RESET);
			pos += sizeof(resoucerecord);
			pos += htons(r->RDLENGTH)  ; // Needed to skip
			printf("\n");
			printf("\n");
		}	
	}
	printf(FG_GREEN);
	if ( ntohs(h->NSCOUNT) > 0 ) {
		PRINT(0,"Domain Authority Section:\n");
		for ( int i = 0 ; i < ntohs(h->NSCOUNT) ; i ++ ) {
			printf("\r" BG_WHITE);
			printf(RESET);
			printf( FG_GREEN);
			PRINT(1,"NAME: ");
			nsize = printdnsname(msg,msg+pos); 
			printf("\n");
			pos += nsize ;
			Resoucerecord r = (Resoucerecord) ( msg + pos );
			printResourceRecord(msg,r);
			printf(RESET);
			pos += sizeof(resoucerecord);
			pos += htons(r->RDLENGTH) ; // Needed to skip 
			printf("\n");
			printf("\n");
		}	
	}
	printf(FG_MAGENTA);
	if ( ntohs(h->ARCOUNT) > 0 ) {
		PRINT(0,"Additional Information Section:\n");
		for ( int i = 0 ; i < ntohs(h->ARCOUNT) ; i ++ ) {
			printf("\r" BG_WHITE);
			printf(RESET);
			printf( FG_MAGENTA);
			PRINT(1,"NAME: ");
			nsize = printdnsname(msg,msg+pos); 
			printf("\n");
			pos += nsize ;
			Resoucerecord r = (Resoucerecord) ( msg + pos );
			printResourceRecord(msg,r);
			printf(RESET);
			pos += sizeof(resoucerecord);
			pos += htons(r->RDLENGTH) ; // Needed to skip 
			printf("\n");
			printf("\n");
		}	
	}
	printf(RESET);
}

int dnsformatdnamestr(char *dnamein,char *out) {
	char in[1024];
	bzero(in,1024);
	struct in_addr addr ;
	strcpy(in,dnamein);	
	if (inet_aton(in , &addr) == 1) {
		// USE WITH PTR
		// If an ip address ...
		addr.s_addr = htonl(addr.s_addr);
		strcpy(in,inet_ntoa(addr));
		strcpy(in+strlen(in),".in-addr.arpa");
	}

	char *tmp = in ,*otmp = out;
	int len = strlen(in);
	for ( int i = 0,j = 0 ; i < len; i ++)
	{
		if ( in[i] == '.' ) { 
			in[i] = 0 ;
		//	printf("%d%s",strlen(tmp),tmp);
			*otmp++ = strlen(tmp);
			strcpy(otmp,tmp);
			otmp += strlen(tmp) ;
			tmp = in + i + 1  ;
		}
		else if ( i == len -1  ){ // if last then ..
		//	printf("%d%s",strlen(tmp),tmp);
			*otmp++ = strlen(tmp);
			strcpy(otmp,tmp);
		}
	}
	return len + 2 ;
} 
// WARNING: QUERY SIZE IS ONLY 512 Bytes And NO CHECKS PERFORMED
// Note: First Argument contains the DNS Server
int main(int argc, char *argv[]) {

	// Declarations
	int fd , namelen, querylen, responselen ;
	struct sockaddr_in serveraddr ;
	char query[512], response[512];
	Header h ;
	int serveraddrlen = sizeof(serveraddr);
	// Initializations
	bzero(&serveraddr,sizeof(serveraddr));
	serveraddr.sin_family = AF_INET ;
	serveraddr.sin_port = htons(53) ;
	serveraddr.sin_addr.s_addr = inet_addr(SERVERADDR);
	bzero(query,512);
	h = (Header) query ;
	h->ID = htons(getpid());
	h->RD = 1 ;

#define SET_QTYPE(q,qtype) ({\
	if ( strcmp(qtype,"A") == 0 ) q->QTYPE = htons( 1 );\
	else if ( strcmp(qtype,"NS") == 0 ) q->QTYPE = htons( 2 );\
	else if ( strcmp(qtype,"CNAME") == 0 ) q->QTYPE = htons( 5 );\
	else if ( strcmp(qtype,"SOA") == 0 ) q->QTYPE = htons( 6 );\
	else if ( strcmp(qtype,"WKS") == 0 ) q->QTYPE = htons( 11 );\
	else if ( strcmp(qtype,"PTR") == 0 ) q->QTYPE = htons( 12 );\
	else if ( strcmp(qtype,"MX") == 0 ) q->QTYPE = htons( 15 );\
	else if ( strcmp(qtype,"SRV") == 0 ) q->QTYPE = htons( 33 );\
	else if ( strcmp(qtype,"AAAA") == 0 ) q->QTYPE = htons( 28 );\
	else q->QTYPE = htons( 255 );\
})
	if (argc < 3) {
		printf(FG_RED "FORMAT: exe DOMAIN TYPE.\n" RESET);
		exit(EXIT_FAILURE);
	}

	h->QDCOUNT = htons(1); // Set the number of queries.
	namelen = dnsformatdnamestr(argv[1],&(query[sizeof(header)]));
	Question q = (Question) (query + sizeof(header) + namelen);
	q->QCLASS = htons(1); // Internet
	SET_QTYPE(q,argv[2]);

	querylen = sizeof(header)+sizeof(question)+namelen;

	printf( FG_RED BG_WHITE "\tQUERY (size: %d)" RESET "\n", querylen);
	printMessageContents(query,querylen);


	try ( fd = socket(PF_INET, SOCK_DGRAM, 0), "socket creation failed.\n");
 	try ( sendto(fd, query, querylen, 0, (struct sockaddr *) &serveraddr, serveraddrlen) ,"sendto failed.");
 	try ( responselen = recvfrom(fd, response, 512, 0,(struct sockaddr *) &serveraddr, &serveraddrlen) , "recvfrom failed.");
 	
 	printf( FG_RED BG_WHITE "\tRESPONSE (size: %d)" RESET "\n" , responselen);
 	printMessageContents(response, responselen);
	try ( close(fd), "close failed.\n" );
	return 0;
}
