// Small non-blocking UDP wrapper for online play (Linux and Windows), IPv4.
#ifndef NET_UDP_H
#define NET_UDP_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint32_t ip; uint16_t port; } NetAddr;   // both in host byte order
typedef struct NetSock NetSock;

NetSock* net_open(int port);   // 0 = any free port; NULL on failure
void net_close(NetSock* s);
// "1.2.3.4", "1.2.3.4:7845" or a host name; default_port if none is given
bool net_resolve(const char* text, int default_port, NetAddr* out);
// one waiting datagram, or 0 if none (never blocks)
int net_recv(NetSock* s, uint8_t* buf, int max, NetAddr* from);
void net_send(NetSock* s, const NetAddr* to, const uint8_t* buf, int len);
// the address other machines on the network most likely reach this one at ("" if unknown)
const char* net_local_ip(void);
void net_addr_text(const NetAddr* a, char* out, int n);
#endif
