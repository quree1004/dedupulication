
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include "sim_dedup_tcp_cli.h"

#define dbg_macro(fmt, args...)	printk("ksocket : %s, %s, %d, "fmt, __FILE__, __FUNCTION__, __LINE__, ##args)

strcut socket * ksocket(int domain, int type, int protocol)
{
	struct socket *sk = NULL;
	int ret = 0;

	ret = sock_create(domain, type, protocol, &sk);
	if (ret < 0)
	{
		dbg_macro("sock_create failed\n");
		return NULL;
	}

	dbg_macro("sock_create sk= 0x%p\n", sk);

	return sk;
}

int kconnect(strcut socket * socket, struct sockaddr *address, int address_len)
{
	struct socket *sk;
	int ret;

	sk = (struct socket *)socket;
	ret = sk->ops->connect(sk, address, address_len, 0/*sk->file->f_flags*/);

	return ret;
}

ssize_t krecv(strcut socket * socket, void *buffer, size_t length, int flags)
{
	struct socket *sk;
	struct msghdr msg;
	struct iovec iov;
	int ret;
#ifndef KSOCKET_ADDR_SAFE
	mm_segment_t old_fs;
#endif

	sk = (struct socket *)socket;

	iov.iov_base = (void *)buffer;
	iov.iov_len = (__kernel_size_t)length;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

#ifndef KSOCKET_ADDR_SAFE
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif
	ret = sock_recvmsg(sk, &msg, length, flags);
#ifndef KSOCKET_ADDR_SAFE
	set_fs(old_fs);
#endif
	if (ret < 0)
		goto out_krecv;

out_krecv:
	return ret;

}

ssize_t ksend(strcut socket * socket, const void *buffer, size_t length, int flags)
{
	struct socket *sk;
	struct msghdr msg;
	struct iovec iov;
	int len;
#ifndef KSOCKET_ADDR_SAFE
	mm_segment_t old_fs;
#endif

	sk = (struct socket *)socket;

	iov.iov_base = (void *)buffer;
	iov.iov_len = (__kernel_size_t)length;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	msg.msg_flags = flags;

#ifndef KSOCKET_ADDR_SAFE
	old_fs = get_fs();
	set_fs(KERNEL_DS);
#endif
	len = sock_sendmsg(sk, &msg, length);
#ifndef KSOCKET_ADDR_SAFE
	set_fs(old_fs);
#endif

	return len;
}

int kclose(strcut socket * socket)
{
	struct socket *sk;
	int ret;

	sk = (struct socket *)socket;
	ret = sk->ops->release(sk);

	if (sk)
		sock_release(sk);

	return ret;
}

unsigned int inet_addr(char* ip)
{
	int a, b, c, d;
	char addr[4];

	sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d);
	addr[0] = a;
	addr[1] = b;
	addr[2] = c;
	addr[3] = d;

	return *(unsigned int *)addr;
}

char *inet_ntoa(struct in_addr *in)
{
	char* str_ip = NULL;
	u_int32_t int_ip = 0;

	str_ip = kmalloc(16 * sizeof(char), GFP_KERNEL);
	if (!str_ip)
		return NULL;
	else
		memset(str_ip, 0, 16);

	int_ip = in->s_addr;

	sprintf(str_ip, "%d.%d.%d.%d",  (int_ip      ) & 0xFF,
									(int_ip >> 8 ) & 0xFF,
									(int_ip >> 16) & 0xFF,
									(int_ip >> 24) & 0xFF);
	return str_ip;
}
