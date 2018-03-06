
#include <stdio.h>
#include <sys/types.h> //types for socket.h and netinet/in.h
#include <sys/socket.h>  // structures for sockets
#include <netinet/in.h>  // structures for sockaddr_in
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define BUFSIZE 2048
#define HSIZE 4 //header size
#define ACK 1
#define SYN 2
#define FIN 4
#define FOF 8

//show greater detail if debug flag is on
int debug = 0; 

union header
{
  char bytes[4];
  struct fields {
    short seq;        //seq number
    short flags_size; //flag (4bit) + size of payload & header (10bit) << 4                                           
  } fields;
};

int reportError(char* msg, int errorCode)
//report error according to msg and exit with errorcode
//1: general technical difficulty (i.e wrong argc, allocation error)
//2: network connectivity
//3: synchronization error (i.e. ack num mismatch)
{
  fprintf(stderr,"Error: %s\n", msg);
  exit(errorCode);
}

int parseMsg(char* msg, char*payload, int* flags, int* seq)
//given headed msg, parse msg (find size from header)
//return payload size (if only header is sent, return 0)
{
  union header h; int size;
  memcpy(h.bytes, msg, HSIZE);

  unsigned int flags_size = h.fields.flags_size;
  size = (flags_size >> 4) - HSIZE; //payload size
  *flags = flags_size & 15;
  *seq = h.fields.seq;
  memcpy(payload, msg + HSIZE, size);

  return size;
}

int main(int argc, char * argv[])
{
  //address structures
  struct sockaddr_in modelA;
  struct sockaddr_storage clientA; //note I didn't use sockaddr_in!

  //argument handler
  if (argc < 2) reportError("argument failure: ./server portNumber", 1);
  if (argc > 2) debug = 1;
  int port = atoi(argv[1]);
  if (debug) fprintf(stderr, "Server Port: %d\n", port);
  
  //create and bind socket
  int sockfd = socket(PF_INET, SOCK_DGRAM, 0); //ipv4, UDP
  if (sockfd == -1) reportError("socket creation failed", 2);
  
  modelA.sin_family = AF_INET; //communication domain
  modelA.sin_addr.s_addr = INADDR_ANY; // ip (accepts any internet address)
  modelA.sin_port = htons(port); //server port

  if (bind(sockfd, (struct sockaddr *) &modelA, sizeof(modelA)) < 0)
    reportError("socket binding failed",2);

  //no listening since no SYNC!
  //hence can start recv/send msg!

  while(1) 
  {
    //receive client msg
    char msg[BUFSIZE]; char payload[BUFSIZE-HSIZE]; int flags; int seq; 
    socklen_t clientA_len; //stores clientA length
    if (debug) fprintf(stderr, ">waiting for msg\n");
    int n = recvfrom(sockfd, msg, BUFSIZE, 0,(struct sockaddr *) &clientA, &clientA_len);

    
    /*
    //print client ip & port 
    if (debug) { 
      unsigned char *ip = (unsigned char *)&(clientA.sin_addr.s_addr); 
      fprintf(stderr, "Found client %d.%d.%d.%d:%d\n"
	      ,clientA.sin_addr.s_addr, ip[1], ip[2], ip[3]
	      ,ntohs(((struct sockaddr_in)clientA).sin_port));
    }
    */

    //check for error & parse msg
    if(n >= BUFSIZE) reportError("Buffer overflow", 1);
    if(n < 0) reportError("recvfrom error", 2);
    n = parseMsg(msg, payload, &flags, &seq);


    //read paylaod
    if(n > 0) {
      if (debug) fprintf(stderr, "Message from client:\n");
      payload[n]=0;
      //printf("%s",buffer);
      //printf("okay %d msg is here: [%s] \n", recvlen, buffer);
      printf("%d, %s\n\n", n, payload);
    }

    //send msg
    if (debug) fprintf(stderr,"sending msg: %s\n", payload);
    while(sendto(sockfd, msg, n+4, 0,(struct sockaddr *)&clientA, clientA_len)<0);
    if (debug) fprintf(stderr,"sent\n");
  }


}


