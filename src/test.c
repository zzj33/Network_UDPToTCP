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
// int main(){
// 	unsigned int a = 3234567890; 
//     printf("%u\n", sizeof(a));
// 	unsigned char pBuf[4];
// 	int b=0;
 
// 	//int2byte
//     pBuf[0] =(unsigned char)(a & 0xff);
// 	pBuf[1] = (unsigned char)((a >> 8) & 0xff);
//     pBuf[2] = (unsigned char)((a >> 16) & 0xff);
//     pBuf[3] = (unsigned char)((a >> 24) & 0xff);
 
// 	printf("bruce int2byte  ==>> pBuf[0]=%d,pBuf[1]=%d\n",pBuf[0],pBuf[1]);
 
//     //byte2int
// 	b =(unsigned int)( pBuf[0] | (pBuf[1] << 8) | (pBuf[2] << 16) | (pBuf[3] << 24));
// 	printf("bruce byte2int  ==>> b=%u\n",b);
 
 
// 	return 0;
// }

void swap(int *x, int *y);
int main() {  
    int a = 10, b = 20;
  
    swap(&a, &b);
    printf("a=%d\nb=%d\n", a, b);
 
    return 0;
 }
 void swap(int *x, int *y) {
    int t;
 
    t = x;
    x = y;
    y = t;
}