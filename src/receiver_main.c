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
// https://www.geeksforgeeks.org/udp-server-client-implementation-c/

#define FLOW_WINDOW_SIZE 32

struct sockaddr_in si_me, si_other;
int s, slen;
char buffer[FLOW_WINDOW_SIZE][PACKET_SIZE]; // https://stackoverflow.com/questions/1088622/how-do-i-create-an-array-of-strings-in-c/1095006
header_t * header; // handshake header, also the last header sent
header_t * header_recv; // the header received from sender
int init_seq_no = 0; // sequence number: 20000000/1400 = 14285.71428571 packets in max, seq number max 
int last_ack = -1;
int buffer_head = 0;
int pkts_received = 0;

void diep(char *s) {
    perror(s);
    exit(1);
}

int out_of_window(int rec_seq){
    if (rec_seq < last_ack || rec_seq > last_ack + FLOW_WINDOW_SIZE){
        return 1;
    }else{
        return 0;
    }
}

void send_header(header_t * header, int sockfd, const struct sockaddr_in dest_addr){
    socklen_t len = sizeof(dest_addr);
    ssize_t bytes_sent = sendto(sockfd, header, sizeof(header_t), 0, (const struct sockaddr *)&dest_addr, len);
    if (bytes_sent == -1){
        diep("Send second-way handshake");
    }
    fprintf(stderr, "sequence no of the packet sent: %d, ack sent:%d\n", header->seq, header->ack);

}


void second_handshake(int sockfd, const struct sockaddr_in dest_addr){
    socklen_t len = sizeof(dest_addr);
    int bytes_recv = recvfrom(sockfd, header_recv, sizeof(header_t), MSG_WAITALL, ( struct sockaddr *) &dest_addr, &len);
    if (bytes_recv == -1){
        diep("Recieve first-way handshake");
    }
    fprintf(stderr, "received handshake from sender with seq no: %d\n", header_recv->seq);
    header = calloc(1, sizeof(header_t));
    header->syn = 1;
    header->fin = 0;
    header->seq = init_seq_no; // https://stackoverflow.com/questions/822323/how-to-generate-a-random-int-in-c
    header->ack = header_recv->seq; // I use the sequence number from client
    last_ack = header->ack;
    // ssize_t bytes_sent = sendto(sockfd, header, sizeof(header), 0, (const struct sockaddr *)&dest_addr, len);
    // if (bytes_sent == -1){
    //     diep("Send second-way handshake");
    // }
    // fprintf(stderr, "finished second handshake, current sync number: %d\n", header->seq);
    send_header(header, sockfd, dest_addr);
}



void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
    
    slen = sizeof (si_other);


    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_me, 0, sizeof (si_me));
    memset(&si_other, 0, sizeof(si_other));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1)
        diep("bind");

	/* Now receive data and send acknowledgements */  
    char * temp_buffer = calloc(1, PACKET_SIZE); // we first store the received pkt here and then cpy to buffer if necessary
    header_recv = (header_t *)temp_buffer;
    second_handshake(s, si_other);

    
    // open destination file
    FILE* file = fopen(destinationFile, "w");
    if (file == NULL) {
        printf("Could not open file to save.");
        exit(1);
    }

    // receive file
    char* data;
    int data_len = PACKET_SIZE - sizeof(header_t);
    socklen_t len = sizeof(si_other);
    int bytes_recv;
    bytes_recv = recvfrom(s, temp_buffer, PACKET_SIZE, MSG_WAITALL, ( struct sockaddr *) &si_other, &len);
    // handle handshake error
    
    while (header_recv->fin != 1){
        fprintf(stderr, "===========================\n new packet received, seq:%d, ack:%d,fin:'%d', bytes_recv:%d\n", header_recv->seq, header_recv->ack, header_recv->fin, bytes_recv);
        if (bytes_recv == -1){
            diep("Recieve data");
        }else if (bytes_recv == sizeof(header_t)){ // still received the first handshake header
            // bytes_sent = sendto(sockfd, header, sizeof(header), 0, (const struct sockaddr *)&dest_addr, len);
            // if (bytes_sent == -1){
            //     diep("Send second-way handshake");
            // }
            // fprintf(stderr, "finished second handshake, current sync number: %d\n", header->seq);
            send_header(header, s, si_other);
        }else if (bytes_recv > 0){ // receive packet of data
            fprintf(stderr, "sequence no of the sender packet received: '%d', fin:'%d'\n", header_recv->seq, header_recv->fin);
            if (!out_of_window(header_recv->seq)){// see if it is in the window, if not, ignore
                int buffer_dest = (header_recv->seq)%FLOW_WINDOW_SIZE; // the index in the buffer this packet should be placed 
                if (header_recv->seq == last_ack){
                    fprintf(stderr, "- Same sequnce no as the last ack\n");
                    send_header(header, s, si_other);
                }else if (header_recv->seq == last_ack + 1){ // receive the base
                    fprintf(stderr, "- Receive the base\n");
                    memcpy(buffer[buffer_dest], temp_buffer, bytes_recv); // TODO: if sender send the last packet, it should send size based on data size
                    buffer[buffer_dest][bytes_recv] = 0;
                    for (int i = 0; i < FLOW_WINDOW_SIZE; ++i)
                    {
                        /* code */
                        buffer_dest %= FLOW_WINDOW_SIZE;
                        if (strlen(buffer[buffer_dest] + sizeof(header_t)) != 0){
                            
                            // save to file
                            data = buffer[buffer_dest] + sizeof(header_t);
                            // fprintf(stderr, "saved data to file: '%s'\n", data);
                            fwrite(data, strlen(data), 1, file);
                            header->syn = 0;
                            // header->fin = 0;
                            header->seq++; // https://stackoverflow.com/questions/822323/how-to-generate-a-random-int-in-c
                            header->ack = ((header_t *)(buffer[buffer_dest]))->seq; // I use the sequence number from client
                            strcpy(buffer[buffer_dest]+ sizeof(header_t), ""); // empty
                            buffer_dest++;
                            last_ack = header->ack;
                        }else{
                            break;
                        }
                    }
                    send_header(header, s, si_other);
                }else{ // receive not the base in the window
                    fprintf(stderr, "- Receive packet in the window other than the base\n");
                    if (strlen(buffer[buffer_dest]) == 0){
                        memcpy(buffer[buffer_dest], temp_buffer, bytes_recv);
                        buffer[buffer_dest][bytes_recv] = 0;
                    }
                    // send previous header.
                    // bytes_sent = sendto(sockfd, header, sizeof(header), 0, (const struct sockaddr *)&dest_addr, len);
                    // if (bytes_sent == -1){
                    //     diep("Send ACK");
                    // }
                    send_header(header, s, si_other);
                    
                }
                
            }else{
                fprintf(stderr, "- Out of window, cur window base:%d, tail:%d\n", last_ack+1,last_ack + FLOW_WINDOW_SIZE);
            }
        }
        bytes_recv = recvfrom(s, temp_buffer, PACKET_SIZE, MSG_WAITALL, ( struct sockaddr *) &si_other, &len);
    }

    // TODO: send end packet
    fprintf(stderr, "after receive fin: syn:%d, seq:%d, ack:%d,fin:'%d', bytes_recv:%d\n", header_recv->syn, header_recv->seq, header_recv->ack, header_recv->fin, bytes_recv);
    header->seq++;
    header->syn = 0;
    header->fin = 0;
    header->ack = header_recv->seq;
    fclose(file);
    send_header(header, s, si_other); // ack sender fin

    header->seq++;
    header->fin = 1;

    send_header(header, s, si_other); // send fin
    bytes_recv = recvfrom(s, temp_buffer, PACKET_SIZE, MSG_WAITALL, ( struct sockaddr *) &si_other, &len);// receive fin ack
    while (header_recv->ack != header->seq){
        // TODO: timeout
        fprintf(stderr, "header_recv->ack:%d, header->seq:%d\n", header_recv->ack, header->seq);
        send_header(header, s, si_other);
        bytes_recv = recvfrom(s, temp_buffer, PACKET_SIZE, MSG_WAITALL, ( struct sockaddr *) &si_other, &len);
    }

    // TODO: another new line in receive file
    
    
    // fprintf(stderr, "Bytes received: %d,Received data: %s\n", bytes_recv, data);
    // while (header_recv->fin != 1){
    //     fprintf(stderr, "Received data: %s\n", data);
    // }
    
    
    close(s);
	printf("%s received. int len:%lu\n", destinationFile, sizeof(int));
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

