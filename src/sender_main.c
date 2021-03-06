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

#define INITIAL_TIMEOUT 200 // in ms
#define INITIAL_SST 64
#define MAX_BUF_SIZE 200

/*
Invariants:
- sequence number increase by packet
*/

/*read file*/
struct sockaddr_in si_other;
int s, slen;
header_t * header;  //the header of the send package
header_t * header_recv; // the header received from recieiver
int dataSize = PACKET_SIZE - sizeof(header_t);
int headerSize = sizeof(header_t);
unsigned long long bytes_to_send; // global var, total bytes to be send
unsigned long long int read_start = 0; //fp's index
long long bytes_rem; //bytes remaining to send. this allow to be negative, so don't use unsigned

/*sender buffer*/
int seqNum = 0; //total packet number, start from 0
char send_buf[MAX_BUF_SIZE][PACKET_SIZE]; //pointer to packets
int lastSeqNum = 0; //judege when to load the last package
_Bool last_loaded = false; //whether loaded the last package
int lastTail = 0;   //last package idx in buffer, if have loaded the last package
int lastPckSize = 0;  //last package's size, if have loaded the last package

/*congestion window*/
int cw_size = 1; // congestion window size
int cw_cnt = 0;  // fraction part of cw
int sst = INITIAL_SST;
int base = 0; // the first index in the congestion window (packet)
int tail = 0; // the last index in the congestion window (packet)
int dupack = 0;
int last_send = -1; // point the last sended package in  cw
int last_ack = -1; //the last ack seqNum

/*time out*/
_Bool time_flag = false; //whether timeout
struct timespec ts;
long timeout = INITIAL_TIMEOUT; //timeout bound, in ms
long rt = INITIAL_TIMEOUT;
int dt = 50;
int measure_cnt = 0; //measure RTT every cw_size ACK

int sizeQ = 0;

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

//resend the package in timeout sec, until receive the ack
void uni_send(int sockfd, const struct sockaddr_in dest_addr){
    _Bool finish = false;
    long curTime;
    long sendTime;
    socklen_t len = sizeof(dest_addr);
    while (!finish) {
        _Bool timeout_flag = false;
        clock_gettime(CLOCK_REALTIME, &ts);
        sendTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        ssize_t bytes_sent = sendto(sockfd, header, sizeof(header_t), 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (bytes_sent == -1){
            diep("Send error");
        }
        while (!timeout_flag) {
            clock_gettime(CLOCK_REALTIME, &ts);
            curTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            if (curTime - sendTime <= timeout) {
                int bytes_recv = recvfrom(sockfd, header_recv, sizeof(header_t), MSG_DONTWAIT, ( struct sockaddr *) &dest_addr, &len);
                if (bytes_recv > 0 && header_recv -> ack == seqNum) {
                    seqNum = header_recv -> ack + 1;
                    finish = true;
                    break;
                }
            } else {
                timeout_flag = true;
            }
        }
    }
}

//reload the buffer when cw tail reach the send_buf's tail
//(move the whole cw back to the start of buffer); also can be implemented by queue
void load_buffer(FILE* fp) {
    printf("-----------reload packet\n");
    read_start += base * dataSize; //move the file index
    seqNum = read_start / dataSize + 1; //set seqNum back to first load packet
    last_send -= base;
    tail -= base; //re-set the base and tail, with the same cw size
    base = 0;
    fseek(fp, read_start, SEEK_SET);
    int i = 0;
    //packet data into buffer
    for (; i < MAX_BUF_SIZE && (unsigned long long int)seqNum * dataSize < bytes_to_send; i++) {
        header = (header_t *)send_buf[i];
        header->syn = 0;
        header->fin = 0;
        header->seq = seqNum;
        header->ack = 0;
        char* data = send_buf[i] + sizeof(header_t);
        fread(data, dataSize, 1, fp);
        seqNum++;
    }
    //load the last packet
    if (seqNum >= lastSeqNum && i < MAX_BUF_SIZE) {
        header = (header_t *)send_buf[i];
        header->syn = 0;
        header->fin = 0;
        header->seq = seqNum;
        header->ack = 0;
        char* data = send_buf[i] + sizeof(header_t);
        fread(data, lastPckSize, 1, fp);
        last_loaded = true;   //need to know the last packet's num to control the tail(cw size) and don't reload anymore
        lastTail = i;
    }
}

int min(int a, int b) {
    if (a < b) 
        return a;
    else 
        return b;
}

int max(int a, int b) {
    if (a > b) 
        return a;
    else 
        return b;
}

void resize_timeQ() {
    printf("resizing...\n");
    que_head *timeQ_new;
    timeQ_new = malloc(sizeof(time_que));
    STAILQ_INIT(timeQ_new);
    int i = base;
    printf("Q now is : %d\n", sizeQ);
    printf("Q should be : %d\n", tail - base + 1);
    sizeQ = 0;
    time_que *elm;
    elm = malloc(sizeof(time_que));
    for (; i <= tail; i++) {
        printf("step1\n");
        if (STAILQ_EMPTY(timeQ))
            printf("why??????\n");
        elm -> nsec = STAILQ_FIRST(timeQ)->nsec;
        printf("step2\n");
        STAILQ_INSERT_TAIL(timeQ_new, elm, field);
        printf("step3\n");
        STAILQ_REMOVE_HEAD(timeQ, field);
        sizeQ++;
    }
    printf("Q now is : %d\n", sizeQ);
    timeQ = timeQ_new;
}

void recv_new_ack(int cur_ack, FILE* fp, _Bool fast_reco){
    dupack = 0;
    last_ack = cur_ack;
    printf("---new ACK----: %d\n", cur_ack);
    if (measure_cnt >= 10 && !STAILQ_EMPTY(timeQ)) {
        clock_gettime(CLOCK_REALTIME, &ts);
        long curTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        int mt = curTime - STAILQ_FIRST(timeQ)->nsec;
        printf("mt: %d\n", mt);
        printf("rt: %ld\n", rt);
        printf("dt: %d\n", dt);
        rt = 0.8 * rt + 0.2 * mt;
        dt = 0.8 * dt + 0.2 * (rt - mt);
        timeout = rt + 4 * dt;
        printf("------timeOUT----: %ld\n", timeout);
        measure_cnt = 0;
    } else {
        printf("---measure_cnt----: %d\n", measure_cnt);
    }
    measure_cnt += (last_ack - base + 1);
    int i = base;
    for (i = base; i <= last_ack && !STAILQ_EMPTY(timeQ); i++) {
        sizeQ--;
        STAILQ_REMOVE_HEAD(timeQ, field);
    }

    if (fast_reco) { //whether in fast recovery
        cw_size = sst;
    } else {
        //congestion avoidance
        for (i = base; i <= last_ack; i++) {
            if (cw_size - sst > 0) {
                cw_cnt++;
                if (cw_cnt >= cw_size) {
                    cw_size++;
                    cw_cnt = 0;
                }
            } else {
                cw_size++;
            }
        }
    }

    bytes_rem = bytes_rem - dataSize * (last_ack - base + 1); //new ack means packet received successfully
    base = last_ack + 1;
    tail = max(base, base + cw_size - 1);

    if (last_loaded) {
        tail = min(tail, lastTail);
    }

    //if cw shrink, resize the timeout queue
    if (last_send > tail) {
        resize_timeQ();
    }

    last_send = max(last_send, base - 1);   //if recv ack > base
    last_send = min(last_send, tail);       //if new ack in fast recovery makes the cw smaller
    if (tail >= MAX_BUF_SIZE && !last_loaded) {
        load_buffer(fp);
    }
}

void slow_start(int sockfd, const struct sockaddr_in dest_addr, FILE* fp){
    printf("--------slow_start\n");
    socklen_t len = sizeof(dest_addr);
    time_que *elm;
    //send data if recv ack, and check timeout
    while (!time_flag && dupack < 3 && bytes_rem > 0){
        if (last_send < tail) {
            //record the timestamp and send the packet
            clock_gettime(CLOCK_REALTIME, &ts);
            elm = malloc(sizeof(time_que));
            elm -> nsec = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            STAILQ_INSERT_TAIL(timeQ, elm, field);
            sizeQ++;
            printf("Q++: %d\n", sizeQ);
            //check if the last package
            int bytesToSend = PACKET_SIZE;
            header_t * temp = (header_t *) send_buf[last_send+1];
            if (temp -> seq == lastSeqNum)
                bytesToSend = lastPckSize + sizeof(header_t);
            //send package
            sendto(sockfd, send_buf[last_send+1], bytesToSend, 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
            printf("send packet seqNum: %d\n", temp -> seq);
            last_send++;
        }
        //check time out and recv ack
        clock_gettime(CLOCK_REALTIME, &ts);
        long curTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

        if (STAILQ_EMPTY(timeQ)) {
            printf("EMPTY: %d\n", last_send);
            printf("BASE: %d\n", base);
            printf("TAIL: %d\n", tail);
        }
        printf("%ld\n", curTime - STAILQ_FIRST(timeQ)->nsec);
        printf("%ld\n", timeout);
        if ((curTime - STAILQ_FIRST(timeQ)->nsec) <= timeout) {
            int bytes_recv = recvfrom(sockfd, header_recv, sizeof(header_t), MSG_DONTWAIT, ( struct sockaddr *) &dest_addr, &len);
            printf("OK1\n");
            if (bytes_recv > 0) {
                printf("OK2\n");
                int cur_ack = header_recv->ack - (read_start / dataSize) - 1;
                if (cur_ack == last_ack){
                    printf("OK3\n");
                    dupack++;
                } else if (cur_ack >= base) {
                    printf("OK4\n");
                    recv_new_ack(cur_ack, fp, false);
                }
            }
        } else {
            time_flag = true;
        }
    }

}

void fast_recovery(int sockfd, const struct sockaddr_in dest_addr, FILE* fp) {
    printf("--------fast_recovery\n");
    socklen_t len = sizeof(dest_addr);
    int bytesToSend = PACKET_SIZE;
    header_t * temp = (header_t *) send_buf[base];
    if (temp -> seq == lastSeqNum)
        bytesToSend = lastPckSize + sizeof(header_t);
    sendto(sockfd, send_buf[base], bytesToSend, 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
    printf("send packet seqNum: %d\n", temp -> seq);
    time_que *elm;
    while (!time_flag && dupack >= 3 && bytes_rem > 0){
        if (last_send < tail) {
            //record the timestamp and send the packet
            clock_gettime(CLOCK_REALTIME, &ts);
            elm = malloc(sizeof(time_que));
            elm -> nsec = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            STAILQ_INSERT_TAIL(timeQ, elm, field);
            sizeQ++;
            printf("Q++: %d\n", sizeQ);
            //check if the last package
            bytesToSend = PACKET_SIZE;
            temp = (header_t *) send_buf[last_send+1];
            if (temp -> seq == lastSeqNum)
                bytesToSend = lastPckSize + sizeof(header_t);
            sendto(sockfd, send_buf[last_send+1], bytesToSend, 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
            printf("send packet seqNum: %d\n", temp -> seq);
            last_send++;
        }
        //check time out and recv ack
        clock_gettime(CLOCK_REALTIME, &ts);
        long curTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        if ((curTime - STAILQ_FIRST(timeQ)->nsec) <= timeout) { 
            int bytes_recv = recvfrom(sockfd, header_recv, sizeof(header_t), MSG_DONTWAIT, ( struct sockaddr *) &dest_addr, &len);
            if (bytes_recv > 0) {
                int cur_ack = header_recv->ack - (read_start / dataSize) - 1;
                if (cur_ack >= base) {
                    recv_new_ack(cur_ack, fp, true);
                } else if (cur_ack == last_ack) {
                    dupack++;
                    cw_size++;
                    tail = base + cw_size - 1;
                    if (tail >= MAX_BUF_SIZE && !last_loaded) {
                        load_buffer(fp);
                    }
                    if (last_loaded) {
                        tail = min(tail, lastTail);
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
    // fseek(fp, 0, SEEK_END);
    // bytes_to_send = ftell(fp);
    // rewind(fp);
    // if (bytesToTransfer < bytes_to_send){
    //     bytes_to_send = bytesToTransfer;
    // }
    bytes_to_send = bytesToTransfer;
    if (bytes_to_send == 0) return;
    bytes_rem = bytes_to_send;
    lastSeqNum = (bytes_to_send - 1) / dataSize + 1;
    lastPckSize = bytes_to_send - ((unsigned long long int)(lastSeqNum - 1) * dataSize);
    printf("filesize: %llu\n", bytes_to_send);
    printf("laseSeq: %d\n", lastSeqNum);
    

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
            printf("--------timeout\n");
            time_flag = false;
            sst = cw_size / 2;
            tail = base;
            last_send = tail - 1;
            cw_size = 1;
            dupack = 0;
            STAILQ_INIT(timeQ);
            sizeQ = 0;
            cw_cnt = 0;
            measure_cnt = 0;
            slow_start(s, si_other, fp);
        } else if (dupack == 3){
            sst = cw_size / 2;
            cw_size = sst + 3;
            tail = base + cw_size - 1;
            if (last_send > tail) {
                resize_timeQ();
            }
            last_send = min(tail, last_send);
            cw_cnt = 0;
            fast_recovery(s, si_other, fp);
        } else {
            cw_cnt = 0;
            slow_start(s, si_other, fp);
        }
    }
    printf("finish tran\n");

    //fareware
    seqNum++;
    header->syn = 0;
    header->seq = seqNum;
    header->fin = 1;
    uni_send(s, si_other);

    int bytes_recv;
    socklen_t len = sizeof(si_other);
    //TODO timeout after no msg
    long curTime;
    long sendTime;
    clock_gettime(CLOCK_REALTIME, &ts);
    sendTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    curTime = sendTime;
    while(curTime - sendTime <= timeout) {
        clock_gettime(CLOCK_REALTIME, &ts);
        curTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        int bytes_recv = recvfrom(s, header_recv, sizeof(header_t), MSG_DONTWAIT, ( struct sockaddr *) &si_other, &len);
        if (bytes_recv > 0 && header_recv -> fin == 1) {
                header->syn = 0;
                header->seq = header_recv -> seq + 1;
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


