#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include "uftp.h"

int main(int argc, char *argv[]){
  FILE *fp; // input file
  struct hostent *host;
  struct sockaddr_in remote_sin, local_sin;
  socklen_t addr_len;
  unsigned int my_port, remote_port; // port number to communicate with server
  char* hostname;
  char* new_filename;
  int s;
  struct frame send_frame;
  int readres;
  int recvlen;
  /* test some helper functions */
  int offset_result;
  int ok_result;
  pthread_t send_thread;
  pthread_t ack_thread;
  int i;

  /* Set port numbers for the local and remote host. */
  my_port = atoi(argv[4]);
  remote_port = atoi(argv[5]);

  /* Allocate the parameters struct and initialize its values*/
  struct send_file_args *state;
  state = (struct send_file_args*) malloc(sizeof(struct send_file_args));

  /* Populate the send state struct */
  state->file_name = (char*) malloc(sizeof(char) * (strlen(argv[1]) + 1)); // name of local file
  bcopy(argv[1], state->file_name, strlen(argv[1]) + 1);
  state->file_name[strlen(argv[1])] = '\0';
  
  /* Initialize has_ack to 0 in all frame slots.*/
  for(i = 0; i < BUF_SIZE; i++){
    state->frame_buf[i].has_ack = 0;
  }

  /* Initialize other parameters */
  state->lar = 0;
  state->lfs = 0;
  state->sws = 4;
  state->seq_max = 8;
  
  state->new_filename = (char*) malloc(sizeof (char) * strlen(argv[2])+1);
  bcopy(argv[2], state->new_filename, strlen(argv[2]) + 1);
  state->new_filename[strlen(argv[2])] = '\0';

  

  /* Set the length of an address */
  addr_len = sizeof(state->local_sin);
  
  /* Configure address of client */
  bzero((char*) &(state->local_sin), sizeof(state->local_sin));
  state->local_sin.sin_family = AF_INET;
  state->local_sin.sin_addr.s_addr = INADDR_ANY;
  state->local_sin.sin_port = htons(my_port);

  /* Resolve the hostname into a host struct */
  hostname = argv[3];
  if(!(host = gethostbyname(hostname))){
    fprintf(stderr, "Couldn't resolve IP from hostname");
    exit(1);
  }

  /* Configure address of server */
  bzero((char*) &(state->remote_sin), sizeof(state->remote_sin));
  state->remote_sin.sin_family = AF_INET;
  bcopy(host->h_addr, (char*) &(state->remote_sin.sin_addr), host->h_length); 
  state->remote_sin.sin_port = htons(remote_port);

  /* Allocate the socket Id for the client to send over. */
  if ((state->s = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
    perror( "couldn't open a socket");
    exit(1);
  }

  /* Bind to client socket to receive messages */
  if ((bind(state->s, (struct sockaddr *) &(state->local_sin), sizeof(state->local_sin))) < 0) {
      perror("couldn't bind local socket");
      exit(1);
  }

  /* send file contents */
  printf("Transferring file...\n");
  pthread_create(&send_thread, NULL, send_file, (void*) state);  
  pthread_create(&ack_thread, NULL, ack_listen, (void*) state);  
  pthread_join(send_thread, NULL);
  pthread_join(ack_thread, NULL);
		   
  return 0;
}
