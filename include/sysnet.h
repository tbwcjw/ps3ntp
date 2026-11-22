#ifndef __SYSNET_H__
#define __SYSNET_H__

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif
	extern int32_t sys_net_9C056962(int32_t family, int32_t type, int32_t protocol); // socket()

	extern struct hostent *sys_net_71F4C717(const char *name); // gethostbyname()
	
	extern int32_t sys_net_88F03575(int32_t s, int32_t level, int32_t optname, const void *optval, socklen_t optlen); // setsockopt()

	extern ssize_t sys_net_9647570B(int32_t s, const void *buf, size_t len, int32_t flags, const struct sockaddr *addr, socklen_t addrlen); // sendto()

	extern ssize_t sys_net_FBA04F37(int32_t s, void *buf, size_t len, int32_t flags); // recv()

#ifdef __cplusplus
}
#endif

#define gethostbyname sys_net_71F4C717

#define socket sys_net_9C056962

#define setsockopt sys_net_88F03575

#define sendto sys_net_9647570B

#define recv sys_net_FBA04F37

#endif
