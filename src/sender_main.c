/* 
 * File:   sender_main.c
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
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include "header.h"


#define HEADER_FLAG 0 //1-0001:SYN, 2-0010:ACK, 4-0100:FIN
#define HEADER_SEQ 1 //1-4 for seq#
#define HEADER_ACK 5 //5-8 for ack#
#define HEADER_DATA 9 //4- for data fragment
#define PCK_SIZE 4096 // TODO: 1472 payload
#define INITIAL_TIMEOUT 1000 // in ms
#define INITIAL_SST 64

/*
Invariants:
- sequence number increase by bytes
- window size measured by bytes
*/

struct sockaddr_in si_other;
int s, slen;
header_t * header;


void diep(char *s) {
    perror(s);
    exit(1);
}

void first_handshake(int sockfd){
    header = calloc(1, sizeof(header_t));
    header->syn = 1;
    header->fin = 0;
    header->seq = rand()%256; // https://stackoverflow.com/questions/822323/how-to-generate-a-random-int-in-c
    header->ack = 0;
    fprintf(stderr, "ready to send with syn number: %d\n", header->seq);
    ssize_t bytes_sent = sendto(sockfd, header, sizeof(header), 0, NULL, 0);
    if (bytes_sent == -1){
        diep("Send first-way Handshake");
    }

}

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Could not open file to send.");
        exit(1);
    }
    printf("%llu", bytesToTransfer);
    // unsigned long int num_pck = (bytesToTransfer - 1) / (PCK_SIZE - HEADER_DATA) + 1; //num of packets need to send
    
    
    




	/* Determine how many bytes to transfer */

    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }


	/* Send data and receive acknowledgements on s*/
    first_handshake(s);

    printf("Closing the socket\n");
    close(s);
    return;

}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);



    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}


