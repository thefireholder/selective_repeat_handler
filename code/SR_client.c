#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h> //types for socket.h and netinet/in.h
#include <sys/socket.h>  // structures for sockets
#include <strings.h> //bzero bcopy
#include <stdlib.h>
#include <unistd.h>

#define BUFSIZE 2048
#define HSIZE 4 //header size
#define ARGMSG "argument failure: ./client hostname port# file"
#define ACK 1
#define SYN 2
#define FIN 4
#define FOF 8

int debug = 0; //when on, prints more detail to stderr msg.

union header
{
  char bytes[4];
  struct fields {
    short seq;        //seq number
    short flags_size; //flag (4bit) + size of payload & header (10bit) << 4 
  } fields;
};

int reportError(char* msg, int errorCode)
//print error msg  and exit with error code
//1: general error (buffer overflow)
//2: network connection error
//3: protocol error
{
  fprintf(stderr,"Error: %s\n", msg);
  exit(errorCode);
}

int formatMsg(char* n_msg, char* o_msg, int o_msgSize, int seq, int flags)
//format new message with header according to o_msg and flags (Extra 4 byte)
//if o_msgSize = -1, determines o_msg size with strlen
//flags are 4 bit: ACK+SYN+FIN+404(FOF)
//returns the size of n_msg
{
    //find o_msg Length
    if(o_msgSize == -1) o_msgSize = strlen(o_msg);
    
    //set header field
    union header h;
    h.fields.seq = seq;
    h.fields.flags_size = flags + ((o_msgSize+4)<<4);
    
    //copy header + payload into n_msg
    memcpy(n_msg, h.bytes, 4);
    memcpy(n_msg+4, o_msg, o_msgSize);
    
    
    return o_msgSize+4;
}
//only takes BUFSIZE n_msg buffer
//o_msgSize < BUFSIZE-4
//no alias


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


int main(int argc, char *argv[])
{
  struct sockaddr_in serv_addr;

  //argument check
  if (argc < 4) reportError(ARGMSG,1);
  if (argc > 4) debug = 1;

  //get host name and port
  int port = atoi(argv[2]);
  struct hostent *host = gethostbyname(argv[1]);
  if (host==NULL) reportError("no such host",2);

  //initialize socket
  int sockfd = socket(PF_INET, SOCK_DGRAM, 0);
  if (sockfd == -1) reportError("socket creation failed", 2);

  //initialize serv_addr
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy((char *)host->h_addr, (char *)&serv_addr.sin_addr.s_addr, host->h_length);
  serv_addr.sin_port = htons(port);

  //UDP needs no connection, transmit msg from here
  char msg[BUFSIZE-4];
  char fmsg[BUFSIZE];

  //send request msg (format & send until make)
  int n = sprintf(msg, "%s:%s","REQUEST", argv[3]); //REQUEST:fileName
  n = formatMsg(fmsg, msg, n, 0, SYN+FIN); //fmsg = header(4)+payload(msg)
  while(sendto(sockfd, fmsg, n, 0,(struct sockaddr *)&serv_addr,sizeof(serv_addr))<0);

  //receive msg & send ACK
  char payload[BUFSIZE-HSIZE]; int flags; int seq; 
  socklen_t servA_len; //stores clientA length
  while(1) {
    //receive msg
    if (debug) fprintf(stderr, ">waiting for msg\n");
    int n = recvfrom(sockfd, msg, BUFSIZE, 0,(struct sockaddr *) &serv_addr, &servA_len);

    //check for error & parse msg
    if(n >= BUFSIZE) reportError("Buffer overflow", 1);
    if(n < 0) reportError("recvfrom error", 2);
    n = parseMsg(msg, payload, &flags, &seq);

    //read paylaod                                                                             
    if(n > 0) {
      if (debug) fprintf(stderr, "Message from server:\n");
      payload[n]=0;
      printf("%d, %s\n\n", n, payload);
    }

    //send ack

  }

  //end terminal
    
}
