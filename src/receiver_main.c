/* 
 * File:   receiver_main.c
 * Author: 
 *
 * Created on
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include "header.h"



struct sockaddr_in si_me, si_other;
int s, slen;


void diep(char *s) {
    perror(s);
    exit(1);
}


void first_handshake(int sockfd){
    header = calloc(1, sizeof(header_t));
    header->syn = 1;
    header->seq = rand()%256; // https://stackoverflow.com/questions/822323/how-to-generate-a-random-int-in-c
    header->ack = 0;
    ssize_t bytes_sent = sendto(sockfd, header, sizeof(header), 0, NULL, 0);
    if (bytes_sent == -1){
        diep("First Handshake");
    }
}



void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
    
    slen = sizeof (si_other);


    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1)
        diep("bind");

	/* Now receive data and send acknowledgements */    

    close(s);
	printf("%s received.", destinationFile);
    return;
}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}

