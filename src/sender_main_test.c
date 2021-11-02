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
- sequence number increase by packet
*/

struct sockaddr_in si_other;
int s, slen;
char * buffer; // heap memory, store the whole packet: including header and actual data. Same in receiver
header_t * header;
float cw_size = 1; // congestion window size
int file_position = 0; // should times the data size to get the byte position
int base = 0; // the first index in the congestion window
int tail = 0; // the last index in the congestion window
int dupack = 0;
int sst = INITIAL_SST;
int last_ack = -1;
int timeout = 1;
int bytes_to_send; // global var, bytes remain to be send


void diep(char *s) {
    perror(s);
    exit(1);
}

void send_header(header_t * header, int sockfd, const struct sockaddr_in dest_addr){
    socklen_t len = sizeof(dest_addr);
    ssize_t bytes_sent = sendto(sockfd, header, sizeof(header_t), 0, (const struct sockaddr *)&dest_addr, len);
    fprintf(stderr, "syn:%d, sequence no: %d, ack:%d, fin:%d, bytes sent:%zd\n", header->syn, header->seq, header->ack, header->fin, bytes_sent);
    if (bytes_sent == -1){
        diep("Send second-way handshake");
    }
}

void first_handshake(int sockfd, const struct sockaddr_in dest_addr){
    // header = calloc(1, sizeof(header_t));
    header->syn = 1;
    header->fin = 0;
    header->seq = 0; // https://stackoverflow.com/questions/822323/how-to-generate-a-random-int-in-c
    header->ack = 0;
    fprintf(stderr, "ready to send with syn number: %d\n", header->seq);
    ssize_t bytes_sent = sendto(sockfd, header, sizeof(header_t), 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
    // ssize_t bytes_sent = sendto(sockfd, header, sizeof(header), 0, NULL, 0);
    if (bytes_sent == -1){
        diep("Send first-way Handshake");
    }
    header->seq++;
    fprintf(stderr, "after handshake sequence number: %d\n", header->seq);

}

void slow_start(int sockfd, const struct sockaddr_in dest_addr, FILE* fp){
    
    
    while (!timeout && dupack <3 && cw_size < sst & bytes_to_send > 0){

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
    fseek(fp, 0, SEEK_END);
    int bytes_to_send = ftell(fp);
    rewind(fp);
    if (bytesToTransfer < bytes_to_send){
        bytes_to_send = bytesToTransfer;
    } 
    printf("bytes_to_send: %d\n", bytes_to_send);
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


    buffer = calloc(1, PACKET_SIZE);
    header = (header_t *)buffer;
    /* Send data and receive acknowledgements on s*/
    first_handshake(s, si_other);

    
    int data_size = PACKET_SIZE - sizeof(header_t);
    fprintf(stderr, "data size:%d\n===================\n", data_size);
    char* data = buffer + sizeof(header_t);
    int data_sent = 0;
    while (bytes_to_send){
        if (bytes_to_send > data_size){
            fread(data, data_size, 1, fp);
            data_sent = data_size;
        }else{
            fread(data, bytes_to_send, 1, fp);
            memset(data+bytes_to_send, 0, data_size - bytes_to_send);
            data_sent = bytes_to_send;
        }
        bytes_to_send -= data_sent;
        
        fprintf(stderr, "header->seq:%d\ndata to be sent:'%s'\n", header->seq, data);
        ssize_t bytes_sent = sendto(s, buffer, sizeof(header_t) + data_sent, 0, (const struct sockaddr *)&si_other, sizeof(si_other));
        // ssize_t bytes_sent = sendto(sockfd, header, sizeof(header), 0, NULL, 0);
        if (bytes_sent == -1){
            diep("send data");
        }
        header->seq++;

    }
    fclose(fp);

    // send fin packet
    header->fin = 1;
    header->ack = 150;
    header->syn = 100;
    send_header(header, s, si_other);

    /*
    while (bytes_to_send > 0){
        if (timeout){
            slow_start(s, si_other, fp);
        }else if (dupack == 3){
            // TODO: fast_recovery()
        }else{
            // TODO: congestion avoidance
        }
    }

    char* data = buffer + sizeof(header_t);
    strcpy(data, "I can!");
    fprintf(stderr, "buffer size%lu\n", PACKET_SIZE);

    // start sending file
    ssize_t bytes_sent = sendto(s, buffer, PACKET_SIZE, 0, (const struct sockaddr *)&si_other, sizeof(si_other));
    // ssize_t bytes_sent = sendto(sockfd, header, sizeof(header), 0, NULL, 0);
    if (bytes_sent == -1){
        diep("Send first-way Handshake");
    }
    */

    printf("Closing the socket, int len:%lu\n", sizeof(int));
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


