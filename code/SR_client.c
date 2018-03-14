#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h> //types for socket.h and netinet/in.h
#include <sys/socket.h>  // structures for sockets
#include <strings.h> //bzero bcopy
#include <stdlib.h>
#include <unistd.h>

#include <errno.h>

#define BUFSIZE 1024
#define HSIZE 4 //header size
#define WNDSIZE 5 //receive window size 
#define WFILE "received.data"
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
    unsigned short seq;        //seq number
    unsigned short flags_size; //flag (4bit) + size of payload & header (10bit) << 4 
  } fields;
};

struct seq_msg {
  int seq; //if entries is empty, seq = -1
  int size;
  char msg[BUFSIZE];
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


int duplicate_check(int ** seq_d, int seq)
//check if duplicate, if so return 1
//else store the value, return 0 AND
//if it is storing in the first of its 3 section, erase the farthest section
//0: 0~10239 .. 1:10240~20479 .. 2:20480~30719
{
  int sec; //find section to use
  if (seq < 10240) sec = 0;
  else if (seq < 20480) sec = 1;
  else sec = 2;

  int i;
  for(i = 0; i < 11; i++)
  {
    if(seq_d[sec][i] == seq) return 1; //if duplicate, return
    if(seq_d[sec][i] == -1) break; //if first empty encountered
  }

  //store in that empty
  seq_d[sec][i] = seq;
  if (i==0) memset(seq_d[(sec+2)%3], -1, 11);
  return 0;
}

int insert_w(struct seq_msg wnd[], char*payload, int seq, int size)
//insert msg into seq window (size refer to payload size)
//0 on succes (if impossible return -1)
{
  for(int i = 0; i < WNDSIZE; i++)
  {
    if (wnd[i].seq == -1) //empty
    {
      wnd[i].seq = seq;
      wnd[i].size = size;
      memcpy(payload, wnd[i].msg, size);
      return 0;
    }
  }
  return -1;
}
//wnd must have n = WNDSIZE entries

int main(int argc, char *argv[])
{
  struct sockaddr_in serverA;

  //argument check
  if (argc < 4) reportError(ARGMSG,1);
  if (argc > 4) debug = 1;

  //get host name and port
  int port = atoi(argv[2]);
  struct hostent *host = gethostbyname(argv[1]);
  if (host==NULL) reportError("no such host",2);

  //initialize socket
  int sockfd = socket(PF_INET, SOCK_DGRAM, 0);
  int timebomb = socket(PF_INET, SOCK_DGRAM, 0);

  //timebomb
  struct timeval tv;
  tv.tv_sec = 2; tv.tv_usec = 0;
  int soerror = setsockopt(timebomb, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
  
  if (soerror == -1) reportError("setsockopt failed", 2);
  if (sockfd == -1) reportError("socket creation failed", 2);
  if (timebomb == -1) reportError("socket creation failed", 2);

  //initialize serverA
  bzero((char *) &serverA, sizeof(serverA));
  serverA.sin_family = AF_INET;
  bcopy((char *)host->h_addr, (char *)&serverA.sin_addr.s_addr, host->h_length);
  serverA.sin_port = htons(port);

  //UDP needs no connection, transmit msg from here
  socklen_t servA_len; //stores clientA length
  char payload[BUFSIZE-HSIZE]; int flags; int seq; int n; //size
  char fmsg[BUFSIZE];
  FILE *fp;

  //send request msg (format & send until make)
  do {
    n = sprintf(payload, "%s", argv[3]); //REQUEST:fileName
    n = formatMsg(fmsg, payload, n, 0, ACK); //fmsg = header(4)+payload(msg)
    while(sendto(timebomb, fmsg, n, 0,(struct sockaddr *)&serverA,sizeof(serverA))<=0);
    if (debug) fprintf(stderr, ">request sent\n");
    n = recvfrom(timebomb, fmsg, BUFSIZE, 0,(struct sockaddr *) &serverA, &servA_len);
    if (debug) fprintf(stderr, ">received %d\n", n);
  }
  while (n < HSIZE || (n-4) != parseMsg(fmsg, payload, &flags, &seq) || !(flags & ACK)); //file mismatch or no ack
  
  //received response (404)
  if(flags & FOF) {printf("404 not found error\n"); exit(0);}
  else fp = fopen(WFILE,"wr+");
  

  //send sync msg

  do {
    if (debug) fprintf(stderr, "Sending packet SYN\n");
    n = formatMsg(fmsg, payload, 0, 0, SYN); //fmsg = header(4)+payload(msg)
    while(sendto(sockfd, fmsg, n, 0,(struct sockaddr *)&serverA,sizeof(serverA))<=0);
    n = recvfrom(sockfd, fmsg, BUFSIZE, 0,(struct sockaddr *) &serverA, &servA_len);
    if (debug) fprintf(stderr, ">received %d\n", n);
  }
  while (n < HSIZE || (n-4) != parseMsg(fmsg, payload, &flags, &seq) || !(flags & SYN)); // received msg

  
  //seq and file transfer
  int seq_d[3][11]; //sequence number stored for duplicate checking
  //0: 0~10239 .. 1:10240~20479 .. 2:20480~30719
  int recv_base;
  struct seq_msg r_window[WNDSIZE];


  //deal with syn
  seq_d[0][0] = seq; //first seq
  recv_base = seq + n; //base



  //receive msg & send ACK
  while(1) {

    //receive msg
    if (debug) fprintf(stderr, ">waiting for msg\n");
    n = recvfrom(sockfd, fmsg, BUFSIZE, 0,(struct sockaddr *) &serverA, &servA_len);

    //check for error & parse msg
    if(n > BUFSIZE) reportError("Buffer overflow", 1);
    if(n < HSIZE) reportError("recvfrom error (less than 4 byte)", 2);
    n = parseMsg(fmsg, payload, &flags, &seq);

    //duplicate check
    

    //insert msg into window
    //if (insert_w(r_window, payload, seq, n) == -1) reportError("not enough space in r_window",3);
    

    //read paylaod                                                                             
    if(n > 0) {
      if (debug) fprintf(stderr, "Message from server:\n");
      payload[n]=0;
      printf("%d, %s\n\n", n, payload);
    }

    //send ack
    printf("Sending packet %d",seq);
    n = formatMsg(fmsg, payload, n, seq, ACK);
    while(sendto(sockfd, fmsg, n, 0,(struct sockaddr *)&serverA,sizeof(serverA))<0);
  }

  //end terminal
    
}
