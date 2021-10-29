#define PACKET_SIZE 1472

typedef struct _header_t{
    int syn;
    int seq;
    int ack;
    int fin;
} header_t;
