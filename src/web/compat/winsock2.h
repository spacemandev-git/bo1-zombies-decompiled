// winsock2.h - Winsock types over the musl socket headers (web build). The engine's own UDP traffic does not use
// sockets on the web (src/web/web_net.cpp exchanges packets with the page through shared-memory rings); these
// declarations exist for code that only names the types (DemonWare's bdAddr: in_addr, inet_addr).
#pragma once
#ifndef BO1_WEB_WINSOCK2_H
#define BO1_WEB_WINSOCK2_H
#include "windows.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

typedef UINT_PTR SOCKET;
#define INVALID_SOCKET ((SOCKET)(~0))
#define SOCKET_ERROR (-1)
#define WSADESCRIPTION_LEN 256
#define WSASYS_STATUS_LEN 128
typedef struct WSAData {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[WSADESCRIPTION_LEN + 1];
    char szSystemStatus[WSASYS_STATUS_LEN + 1];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
} WSADATA, *LPWSADATA;
typedef struct sockaddr SOCKADDR, *PSOCKADDR, *LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN, *PSOCKADDR_IN, *LPSOCKADDR_IN;
typedef struct in_addr IN_ADDR, *PIN_ADDR;
typedef struct hostent HOSTENT, *PHOSTENT, *LPHOSTENT;
#define WSAEWOULDBLOCK 10035L
#define WSAEINPROGRESS 10036L
#define WSAEALREADY 10037L
#define WSAEMSGSIZE 10040L
#define WSAEADDRINUSE 10048L
#define WSAEADDRNOTAVAIL 10049L
#define WSAENETDOWN 10050L
#define WSAENETUNREACH 10051L
#define WSAECONNRESET 10054L
#define WSAENOBUFS 10055L
#define WSAEISCONN 10056L
#define WSAENOTCONN 10057L
#define WSAETIMEDOUT 10060L
#define WSAECONNREFUSED 10061L
#define WSAEHOSTUNREACH 10065L
#define WSANOTINITIALISED 10093L
#define WSAEAFNOSUPPORT 10047L
#define WSAEINVAL 10022L
#define WSAEACCES 10013L
#define WSAEBADF 10009L
#define WSAEINTR 10004L
#define WSAEFAULT 10014L
#define WSAEMFILE 10024L
#define WSAENOTSOCK 10038L
#define WSAEDESTADDRREQ 10039L
#define WSAEPROTOTYPE 10041L
#define WSAENOPROTOOPT 10042L
#define WSAEPROTONOSUPPORT 10043L
#define WSAESOCKTNOSUPPORT 10044L
#define WSAEOPNOTSUPP 10045L
#define WSAEPFNOSUPPORT 10046L
#define WSAENETRESET 10052L
#define WSAECONNABORTED 10053L
#define WSAESHUTDOWN 10058L
#define WSAETOOMANYREFS 10059L
#define WSAELOOP 10062L
#define WSAENAMETOOLONG 10063L
#define WSAEHOSTDOWN 10064L
#define WSASYSNOTREADY 10091L
#define WSAVERNOTSUPPORTED 10092L
#define WSAHOST_NOT_FOUND 11001L
#define WSATRY_AGAIN 11002L
#define WSANO_RECOVERY 11003L
#define WSANO_DATA 11004L
#define FIONBIO_WIN 0x8004667EL
#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

BO1_EXTERN_C_BEGIN
// web: no sockets are opened through these (see the header comment); they fail with WSAENETDOWN
int WSAStartup(WORD version, LPWSADATA data);
int WSACleanup(void);
int WSAGetLastError(void);
void WSASetLastError(int err);
int closesocket(SOCKET s);
int ioctlsocket(SOCKET s, long cmd, unsigned long *arg);
BO1_EXTERN_C_END
#endif
