//
//  ksockets.h
//  KernelConnexions
//
//  Created by Alex Nichol on 11/25/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#ifndef KernelConnexions_ksockets_h
#define KernelConnexions_ksockets_h

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

// client packets
#define CONTROL_PACKET_CONNECT 0x1
#define CONTROL_PACKET_CLOSE 0x3
#define CONTROL_PACKET_SEND 0x5

// control packets
#define CONTROL_PACKET_CONNECTED 0x2
#define CONTROL_PACKET_ERROR 0x4
#define CONTROL_PACKET_DATA 0x6
#define CONTROL_PACKET_HUNGUP 0x8

struct ksocket_header {
    uint8_t type;
    uint16_t len;
} __attribute__((__packed__));

typedef struct ksocket_header ksocket_header_t;

int ksocket_init();
int ksocket_close(int socket);

// protocol
int ksocket_connect_ipv4(int socket, const void * addr, uint16_t port);
int ksocket_connect_ipv6(int socket, const void * addr, uint16_t port);
int ksocket_disconnect(int socket);

/**
 * Read data from the socket
 * @param socket The socket
 * @param buff Output buffer (you must free)
 * @return If 0, then the connection was closed but the ksocket remains open.
 *   If -1, then the ksocket itself has died.
 *   If positive, the number of bytes read.
 */
int ksocket_read(int socket, void ** buff);

int ksocket_send(int socket, const void * buff, int len);

#endif
