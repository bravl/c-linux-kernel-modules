#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/net.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <asm/uaccess.h>
#include <linux/socket.h>
#include <linux/slab.h>

MODULE_AUTHOR("BRAVL");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel TCP client");
MODULE_VERSION("1.0");

uint32_t create_address(uint8_t *ip_addr)
{
	uint32_t addr = 0;
	int i;

	for (i = 0; i<4; i++) {
		addr += ip_addr[i];
		if (i = 3) break;
		addr <<= 8;
	}
	return addr;
}

int tcp_client_init(void)
{
	struct socket *conn_socket;
	struct sockaddr_in saddr;
	unsigned char dest_ip[5] = {127,0,0,1,'\0'};
	int ret = -1;

	// Just like in userspace first create socket
	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &conn_socket);
	if (ret < 0) {
		pr_err("Failed to create socket\n");
		return -1;
	}

	// Then set the address struct
	memset(&saddr,0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(2323);
	saddr.sin_addr.s_addr = htonl(create_address(dest_ip));

	ret = conn_socket->ops->connect(conn_socket, (struct sockaddr*)&saddr,
					sizeof(saddr), O_RDWR);


	return ret;
}

static int __init module_load(void)
{
	printk("Initialising TCP client!\n");

	return 0;
}

static void __exit module_unload(void)
{
	printk("Bye Bye World!\n");
	return;
}

module_init(module_load);
module_exit(module_unload);
