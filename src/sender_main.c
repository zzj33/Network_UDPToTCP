/* 
 * File:   sender_main.c
 * Author: 
 *
 * Created on 
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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
#include <time.h>
#include <sys/queue.h>
#include "header.h"


#define HEADER_FLAG 0 //1-0001:SYN, 2-0010:ACK, 4-0100:FIN
#define HEADER_SEQ 1 //1-4 for seq#
#define HEADER_ACK 5 //5-8 for ack#
#define HEADER_DATA 9 //4- for data fragment
#define INITIAL_TIMEOUT 1000 // in ms
#define INITIAL_SST 64
#define MAX_BUF_SIZE 1024

/*
Invariants:
- sequence number increase by packet
*/

struct sockaddr_in si_other;
int s, slen;
char * buffer; // heap memory, store the whole packet: including header and actual data. Same in receiver
header_t * header;
header_t * header_recv; // the header received from recieiver
int seqNum = 0; //total packet number, start from 0
int dataSize = PACKET_SIZE - sizeof(header_t);
int read_start = 0; //fp's index
char send_buf[MAX_BUF_SIZE][PACKET_SIZE]; //pointer to packets

float cw_size = 1; // congestion window size
float sst = INITIAL_SST;
int base = 0; // the first index in the congestion window (packet)
int tail = 0; // the last index in the congestion window (packet)
int preTail = -1; // tail before recv the new ack; to find new package needed to send
int dupack = 0;
int bytes_to_send; // global var, total bytes to be send
int bytes_rem; //bytes remaining to send
time_que *timeQ; //each packet's timestamp
struct timespec ts;
_Bool time_flag = false; //whether timeout
_Bool sendFin = false; //
long timeout = 500000; //timeout bound
int last_ack = -1;

typedef struct time_que                             //queue
{
    STAILQ_ENTRY(time_que) field;
    long nsec;
} time_que;

typedef STAILQ_HEAD(que_head, time_que) que_head;    //queue head



void diep(char *s) {
    perror(s);
    exit(1);
}

void first_handshake(int sockfd, const struct sockaddr_in dest_addr){
    header = calloc(1, sizeof(header_t));
    header->syn = 1;
    header->fin = 0;
    header->seq = seqNum; // https://stackoverflow.com/questions/822323/how-to-generate-a-random-int-in-c
    header->ack = 0;
    fprintf(stderr, "ready to send with syn number: %d\n", header->seq);
    ssize_t bytes_sent = sendto(sockfd, header, sizeof(header_t), 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
    // ssize_t bytes_sent = sendto(sockfd, header, sizeof(header), 0, NULL, 0);
    if (bytes_sent == -1){
        diep("Send first-way Handshake");
    }

}

//reload the buffer when cw tail reach the send_buf's tail
//(move the whole cw back to the start of buffer)
void load_buffer(FILE* fp) {
    read_start += base * dataSize; //move the file index
    seqNum = read_start / dataSize; //set seqNum back to first load packet
    preTail -= base;
    tail -= base; //re-set the base and tail, with the same cw size
    base = 0;
    fseek(fp, read_start, SEEK_SET);
    int i = 0;
    //packet data into buffer
    for (; i < MAX_BUF_SIZE & (seqNum+1) * dataSize < bytes_to_send; i++) {
        header = (header_t *)send_buf[i];
        header->syn = 0;
        header->fin = 0;
        header->seq = seqNum;
        char* data = send_buf[i] + sizeof(header_t);
        fread(data, dataSize, 1, fp);
        seqNum++;
    }
    //load the last packet
    if ((seqNum+1) * dataSize >= bytes_to_send & i < MAX_BUF_SIZE) {
        header = (header_t *)send_buf[i];
        header->syn = 0;
        header->fin = 0;
        header->seq = seqNum;
        char* data = send_buf[i] + sizeof(header_t);
        fread(data, bytes_to_send - (seqNum * dataSize), 1, fp);
    }
}


void slow_start(int sockfd, const struct sockaddr_in dest_addr, FILE* fp){
    socklen_t len = sizeof(dest_addr);

    //the first buffer 
    // time_flag = false;
    // sst = cw_size / 2; //the first time, will it be zero? So I think maybe it is better put it under outside
    // tail = base;
    // cw_size = 1;
    // dupack = 0;
    time_que *elm;
    //send data if recv ack, and check timeout, not finished
    while (!time_flag && dupack < 3 && cw_size < sst & bytes_rem > 0){
        while (preTail < tail) {
            //record the timestamp and send the packet
            clock_gettime(CLOCK_REALTIME, &ts);
            elm = malloc(sizeof(time_que));
            elm -> nsec = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            STAILQ_INSERT_TAIL(timeQ, elm, field);
            sendto(sockfd, send_buf[preTail+1], PACKET_SIZE, 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
            preTail++;
        }
        //check time out and recv ack
        clock_gettime(CLOCK_REALTIME, &ts);
        long curTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        if ((curTime - STAILQ_FIRST(timeQ)->nsec) <= timeout) {
            int bytes_recv = recvfrom(sockfd, header_recv, sizeof(header_t), MSG_WAITALL, ( struct sockaddr *) &dest_addr, &len);
            if (bytes_recv != 0) {
                if (header_recv->ack == last_ack){
                    dupack++;
                }else if (header_recv->ack >= base && header_recv->ack <= tail) {  // TODO: tail included?
                    dupack = 0
                    cw += header_recv->ack - base + 1;
                    base = header_recv->ack + 1;
                    int old_tail = tail;
                    tail = base + cw - 1;
                    if (tail >= MAX_BUF_SIZE)
                        load_buffer(fp);
                    for (int i = old_tail+1; i <= tail; ++i)
                    {
                        // send new packets
                    }
                }
            }
        } else {
            time_flag = true;
        }
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
    printf("%llu", bytesToTransfer);
    

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


	// /* Send data and receive acknowledgements on s*/
    // buffer = calloc(1, PACKET_SIZE);
    // header = (header_t *)buffer;

    first_handshake(s, si_other);

    timeQ = malloc(sizeof(time_que));
    STAILQ_INIT(timeQ);
    

    if (seqNum == 0) load_buffer(fp);

    slow_start(s, si_other, fp);

    while (bytes_to_send > 0){ // change all the parameters here
        if (time_flag){
            time_flag = false;
            sst = cw_size / 2;
            tail = base;
            cw_size = 1;
            dupack = 0;
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


