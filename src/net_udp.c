// Small non-blocking UDP wrapper for online play (see net_udp.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net_udp.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
static void sock_nonblock(sock_t s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
static void sock_close(sock_t s) { closesocket(s); }
static void net_init(void) {
  static int done;
  if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = 1; }
}
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
static void sock_nonblock(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
static void sock_close(sock_t s) { close(s); }
static void net_init(void) {}
#endif

struct NetSock { sock_t fd; };

NetSock* net_open(int port) {
  net_init();
  sock_t fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd == BAD_SOCK) return NULL;
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((uint16_t)port);
  if (bind(fd, (struct sockaddr*)&a, sizeof a) != 0) { sock_close(fd); return NULL; }
  sock_nonblock(fd);
  NetSock* s = malloc(sizeof *s);
  s->fd = fd;
  return s;
}
void net_close(NetSock* s) { if (s) { sock_close(s->fd); free(s); } }

bool net_resolve(const char* text, int default_port, NetAddr* out) {
  net_init();
  char host[256]; snprintf(host, sizeof host, "%s", text);
  int port = default_port;
  char* colon = strrchr(host, ':');
  if (colon) { port = atoi(colon + 1); *colon = 0; }
  while (*host == ' ') memmove(host, host + 1, strlen(host));
  // "192.168.001.023" (the menu's fixed-width form) has leading zeros that inet_pton rejects
  unsigned b[4];
  if (sscanf(host, "%u.%u.%u.%u", &b[0], &b[1], &b[2], &b[3]) == 4 && b[0] < 256 && b[1] < 256 && b[2] < 256 && b[3] < 256) {
    out->ip = b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    out->port = (uint16_t)port;
    return port > 0 && port < 65536;
  }
  struct addrinfo hints, *res = NULL; memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
  out->ip = ntohl(((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
  out->port = (uint16_t)port;
  freeaddrinfo(res);
  return true;
}

int net_recv(NetSock* s, uint8_t* buf, int max, NetAddr* from) {
  struct sockaddr_in a; socklen_t al = sizeof a;
  int n = (int)recvfrom(s->fd, (char*)buf, max, 0, (struct sockaddr*)&a, &al);
  if (n <= 0) return 0;
  from->ip = ntohl(a.sin_addr.s_addr); from->port = ntohs(a.sin_port);
  return n;
}
void net_send(NetSock* s, const NetAddr* to, const uint8_t* buf, int len) {
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(to->ip); a.sin_port = htons(to->port);
  sendto(s->fd, (const char*)buf, len, 0, (struct sockaddr*)&a, sizeof a);
}

const char* net_local_ip(void) {
  // "connect" a UDP socket towards a public address (nothing is sent) and see which local address
  // the system picked: that's the one on the default route
  static char out[32];
  net_init();
  out[0] = 0;
  sock_t fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd == BAD_SOCK) return out;
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(53); a.sin_addr.s_addr = htonl(0x08080808);
  if (connect(fd, (struct sockaddr*)&a, sizeof a) == 0) {
    struct sockaddr_in me; socklen_t ml = sizeof me;
    if (getsockname(fd, (struct sockaddr*)&me, &ml) == 0) {
      uint32_t ip = ntohl(me.sin_addr.s_addr);
      if (ip) snprintf(out, sizeof out, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    }
  }
  sock_close(fd);
  return out;
}
void net_addr_text(const NetAddr* a, char* out, int n) {
  snprintf(out, n, "%u.%u.%u.%u:%u", a->ip >> 24, (a->ip >> 16) & 255, (a->ip >> 8) & 255, a->ip & 255, a->port);
}
