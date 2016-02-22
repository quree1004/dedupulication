#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

void error_handling(char *message);

int main(int argc, char **argv)
{
    int serv_sock;
    int clnt_sock;
    struct sockaddr_in serv_addr;
    struct sockaddr_in clnt_addr;
    int clnt_addr_size;

    char buf[1024];

    serv_sock=socket(PF_INET, SOCK_STREAM, 0); /* 서버 소켓 생성 */
    if(serv_sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family=AF_INET;
    serv_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    serv_addr.sin_port=htons(5555);

    if( bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr))==-1 ) /* 소켓에 주소 할당 */
        error_handling("bind() error");

    if( listen(serv_sock, 5)==-1 )  /* 연결 요청 대기 상태로 진입 */
        error_handling("listen() error");

    clnt_addr_size=sizeof(clnt_addr);

    clnt_sock=accept(serv_sock, (struct sockaddr*)&clnt_addr,&clnt_addr_size); /* 연결 요청 수락 */
    if(clnt_sock==-1)
        error_handling("accept() error");

    while(1)
    {
        read(clnt_sock, buf, sizeof(buf));
        printf("KERNEL MSG : %s\n", buf);

        if(!strcmp(buf, "End of Dedup"))
            break;
<<<<<<< HEAD
		strcpy(buf, "Hello Ksocket\n");
        write(clnt_sock, buf, sizeof(buf)); /* 데이터 전송 */
=======
		//strcpy(buf, "Hello Ksocket\n");
        //write(clnt_sock, buf, sizeof(buf)); /* 데이터 전송 */
>>>>>>> 3ad11c0565c5cfeec047165131f6815c0fb67db8
    }

    close(clnt_sock); /* 연결 종료 */

     return 0;
}

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
