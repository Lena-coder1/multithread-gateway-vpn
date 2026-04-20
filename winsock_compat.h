// winsock_compat.h - Windows socket compatibility for Unix socket code
#ifndef WINSOCK_COMPAT_H
#define WINSOCK_COMPAT_H

// Windows socket headers
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// Link with Winsock library
#pragma comment(lib, "ws2_32.lib")

// POSIX thread header on Windows
#include <pthread.h>

// Emulate Unix close() with Windows closesocket()
#define close(socket) closesocket(socket)

// Emulate Unix read() with Windows recv()
#define read(socket, buffer, length) recv(socket, buffer, length, 0)

// Emulate Unix write() with Windows send()
#define write(socket, buffer, length) send(socket, buffer, length, 0)

// Helper function to initialize Winsock (call ONCE at program start)
static inline int winsock_init(void) {
    WSADATA wsa_data;
    return WSAStartup(MAKEWORD(2, 2), &wsa_data);
}

// Helper function to cleanup Winsock (call at program end)
static inline void winsock_cleanup(void) {
    WSACleanup();
}

#endif