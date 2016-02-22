#ifndef _ksocket_h_
#define _ksocket_h_

struct socket * ksocket(int domain, int type, int protocol);

int kclose(struct socket * socket);

int kconnect(struct socket * socket, struct sockaddr *address, int address_len)

ssize_t krecv(struct socket * socket, void *buffer, size_t length, int flags);
ssize_t ksend(struct socket * socket, const void *buffer, size_t length, int flags);

unsigned int inet_addr(char* ip);
char *inet_ntoa(struct in_addr *in);

#endif
