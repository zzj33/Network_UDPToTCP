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

int cw_size = 1; // congestion window size
int cw_cnt = 0;  // fraction part of cw
float sst = INITIAL_SST;
int base = 0; // the first index in the congestion window (packet)
int tail = 0; // the last index in the congestion window (packet)
int preTail = -1; // tail before recv the new ack; to find new package needed to send
int dupack = 0;
int bytes_to_send; // global var, total bytes to be send
int bytes_rem; //bytes remaining to send
struct timespec ts;

_Bool time_flag = false; //whether timeout
long timeout = 500; //timeout bound, in ms
int last_ack = -1;

typedef struct time_que                             //queue
{
    STAILQ_ENTRY(time_que) field;
    long nsec;
} time_que;

typedef STAILQ_HEAD(que_head, time_que) que_head;    //queue head

que_head *timeQ; //each packet's timestamp

void diep(char *s) {
    perror(s);
    exit(1);
}

void uni_send(int sockfd, const struct sockaddr_in dest_addr){
    _Bool TO_flag = false;
    _Bool finish = false;
    long curTime;
    long sendTime;
    socklen_t len = sizeof(dest_addr);
    while (!finish) {
        clock_gettime(CLOCK_REALTIME, &ts);
        sendTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        ssize_t bytes_sent = sendto(sockfd, header, sizeof(header_t), 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
        // ssize_t bytes_sent = sendto(sockfd, header, sizeof(header), 0, NULL, 0);
        if (bytes_sent == -1){
            diep("Send error");
        }
        while (!TO_flag) {
            clock_gettime(CLOCK_REALTIME, &ts);
            curTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            //fprintf(stderr, "time diff:%ld in uni_send\n", curTime - sendTime);
            if (curTime - sendTime < timeout) {
                int bytes_recv = recvfrom(sockfd, header_recv, sizeof(header_t), MSG_WAITALL, ( struct sockaddr *) &dest_addr, &len);
                //fprintf(stderr, "bytes_recv:%d in uni_send\n", bytes_recv);
                if (bytes_recv == -1){
                    diep("recvfrom error in uni_send()");
                }
                if (bytes_recv > 0 && header_recv -> ack == seqNum) {
                    finish = true;
                    break;
                }
            } else {
                TO_flag = true;
            }
        }
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
    for (; i < MAX_BUF_SIZE && (seqNum+1) * dataSize < bytes_to_send; i++) {
        header = (header_t *)send_buf[i];
        header->syn = 0;
        header->fin = 0;
        header->seq = seqNum;
        char* data = send_buf[i] + sizeof(header_t);
        fread(data, dataSize, 1, fp);
        seqNum++;
    }
    //load the last packet
    if ((seqNum+1) * dataSize >= bytes_to_send && i < MAX_BUF_SIZE) {
        header = (header_t *)send_buf[i];
        header->syn = 0;
        header->fin = 0;
        header->seq = seqNum;
        char* data = send_buf[i] + sizeof(header_t);
        fread(data, bytes_to_send - (seqNum * dataSize), 1, fp);
        printf("last packet size: %d", bytes_to_send - (seqNum * dataSize));
    }
}

void recv_new_ack(int cur_ack, FILE* fp){
    dupack = 0;
    last_ack = cur_ack;
    for (int i = base; i <= last_ack && !STAILQ_EMPTY(timeQ); i++) {
        STAILQ_REMOVE_HEAD(timeQ, field);
    }
    //congestion avoidance
    for (int i = base; i <= last_ack; i++) {
        if (cw_size >= sst) {
            cw_cnt++;
            if (cw_cnt == cw_size) {
                cw_size++;
                cw_cnt = 0;
            }
        } else {
            cw_size++;
        }
        bytes_rem -= dataSize; //new ack means packet received successfully
    }
    base = last_ack + 1;
    preTail = tail;
    tail = base + cw_size - 1;
    if (tail >= MAX_BUF_SIZE)
        load_buffer(fp);
}

void slow_start(int sockfd, const struct sockaddr_in dest_addr, FILE* fp){
    socklen_t len = sizeof(dest_addr);
    time_que *elm;
    //send data if recv ack, and check timeout
    while (!time_flag && dupack < 3 && bytes_rem > 0){
        while (preTail < tail) {
            //record the timestamp and send the packet
            clock_gettime(CLOCK_REALTIME, &ts);
            elm = malloc(sizeof(time_que));
            elm -> nsec = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            STAILQ_INSERT_TAIL(timeQ, elm, field);
            int bytes_send = sendto(sockfd, send_buf[preTail+1], sizeof(send_buf[preTail+1]), 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
            printf("send: %d\n", bytes_send);
            preTail++;

        }
        //check time out and recv ack
        clock_gettime(CLOCK_REALTIME, &ts);
        long curTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        if ((curTime - STAILQ_FIRST(timeQ)->nsec) <= timeout) {
            int bytes_recv = recvfrom(sockfd, header_recv, sizeof(header_t), MSG_WAITALL, ( struct sockaddr *) &dest_addr, &len);
            if (bytes_recv != 0) {
                int cur_ack = header_recv->ack - (read_start * dataSize);
                if (cur_ack == last_ack){
                    dupack++;
                } else if (cur_ack >= base) {  // TODO: tail included?
                    recv_new_ack(cur_ack, fp);
                }
            }
        } else {
            time_flag = true;
        }
    }

}

void fast_recovery(int sockfd, const struct sockaddr_in dest_addr, FILE* fp) {
    socklen_t len = sizeof(dest_addr);
    sendto(sockfd, send_buf[base], PACKET_SIZE, 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
    time_que *elm;
    while (!time_flag && dupack >= 3 && bytes_rem > 0){
        while (preTail < tail) {
            //record the timestamp and send the packet
            clock_gettime(CLOCK_REALTIME, &ts);
            elm = malloc(sizeof(time_que));
            elm -> nsec = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            STAILQ_INSERT_TAIL(timeQ, elm, field);
            ssize_t bytes_sent = sendto(sockfd, send_buf[preTail+1], sizeof(send_buf[preTail+1]), 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (bytes_sent == -1){
                diep("Send error");
            }
            preTail++;
        }
        //check time out and recv ack
        clock_gettime(CLOCK_REALTIME, &ts);
        long curTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        if ((curTime - STAILQ_FIRST(timeQ)->nsec) <= timeout) { 
            int bytes_recv = recvfrom(sockfd, header_recv, sizeof(header_t), MSG_WAITALL, ( struct sockaddr *) &dest_addr, &len);
            if (bytes_recv != 0) {
                int cur_ack = header_recv->ack - (read_start * dataSize);
                if (cur_ack >= base) {
                    recv_new_ack(cur_ack, fp);
                } else if (cur_ack == last_ack) {
                    dupack++;
                    cw_size++;
                    preTail = tail;
                    tail = base + cw_size - 1;
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
    bytes_rem = bytes_to_send;
    printf("filesize: %d\n", bytes_to_send);
    

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

	header = calloc(1, sizeof(header_t));
    header_recv = calloc(1, sizeof(header_t));

    //first hand shake
    header->syn = 1;
    header->seq = seqNum;
    header->fin = 0;
    uni_send(s, si_other);
    fprintf(stderr, "ready to send with syn number: %d\n", header->seq);


    //time queue, load buffer, and start send
    timeQ = malloc(sizeof(time_que));
    STAILQ_INIT(timeQ);
    
    load_buffer(fp);

    while (bytes_rem > 0){ // change all the parameters here
        if (time_flag){
            time_flag = false;
            sst = cw_size / 2;
            tail = base;
            preTail = tail - 1;
            cw_size = 1;
            dupack = 0;
            STAILQ_INIT(timeQ);
            slow_start(s, si_other, fp);
        } else if (dupack == 3){
            sst = cw_size / 2;
            cw_size = sst + 3;
            tail = base + cw_size - 1;
            fast_recovery(s, si_other, fp);
        } else {
            slow_start(s, si_other, fp);
        }
    }

    //fareware
    header->syn = 0;
    header->seq = seqNum;
    header->fin = 1;
    uni_send(s, si_other);

    int bytes_recv;
    socklen_t len = sizeof(si_other);
    seqNum++;
    while(true) {
        bytes_recv = recvfrom(s, header_recv, sizeof(header_t), MSG_WAITALL, ( struct sockaddr *) &si_other, &len);
        if (bytes_recv > 0 && header_recv -> fin == 1) {
                header->syn = 0;
                header->seq = header_recv -> seq;
                header->ack = header_recv -> seq;
                header->fin = 0;
                sendto(s, header, sizeof(header_t), 0, (const struct sockaddr *)&si_other, sizeof(si_other));
                break;
        }
    }

    printf("Closing the socket\n");
    close(s);
    fclose(fp);
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


