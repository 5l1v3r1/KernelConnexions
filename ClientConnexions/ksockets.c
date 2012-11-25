//
//  ksockets.c
//  KernelConnexions
//
//  Created by Alex Nichol on 11/25/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#include "ksockets.h"

static int ksocket_read_ensure(int fd, void * buff, int len);
static int ksocket_write_ensure(int fd, const void * buff, int len);
static int ksocket_wait_response(int fd);

int ksocket_init() {
    struct sockaddr_ctl addr;
    struct ctl_info info;
    bzero(&addr, sizeof(addr));
    bzero(&info, sizeof(info));
    
    addr.sc_len = sizeof(struct sockaddr_ctl);
    addr.sc_family = AF_SYSTEM;
    addr.ss_sysaddr = AF_SYS_CONTROL;
    
    strncpy(info.ctl_name, "com.aqnichol.KernelConnexions", sizeof(info.ctl_name));
    
    int fd = socket(PF_SYSTEM, SOCK_STREAM, SYSPROTO_CONTROL);
    if (fd < 0) return fd;
    
    if (ioctl(fd, CTLIOCGINFO, &info)) {
        return -1;
    }
    
    addr.sc_id = info.ctl_id;
    addr.sc_unit = 0;
    
    int result;
    if ((result = connect(fd, (struct sockaddr *)&addr, sizeof(addr)))) {
        return result;
    }
    
    return fd;
}

int ksocket_close(int socket) {
    close(socket);
    return 0;
}

// protocol
int ksocket_connect_ipv4(int socket, const void * addr, uint16_t port) {
    ksocket_header_t header;
    header.type = CONTROL_PACKET_CONNECT;
    header.len = htons(6);
    if (ksocket_write_ensure(socket, &header, 3) != 0) return -1;
    uint16_t portBig = htons(port);
    if (ksocket_write_ensure(socket, &portBig, 2) != 0) return -1;
    if (ksocket_write_ensure(socket, addr, 4) != 0) return -1;
    errno = 0;
    return ksocket_wait_response(socket);
}

int ksocket_connect_ipv6(int socket, const void * addr, uint16_t port) {
    ksocket_header_t header;
    header.type = CONTROL_PACKET_CONNECT;
    header.len = htons(18);
    if (ksocket_write_ensure(socket, &header, 3) != 0) return -1;
    uint16_t portBig = htons(port);
    if (ksocket_write_ensure(socket, &portBig, 2) != 0) return -1;
    if (ksocket_write_ensure(socket, addr, 16) != 0) return -1;
    errno = 0;
    return ksocket_wait_response(socket);
}

int ksocket_disconnect(int socket) {
    ksocket_header_t header;
    header.type = CONTROL_PACKET_CLOSE;
    header.len = 0;
    return ksocket_write_ensure(socket, &header, 3);
}

/**
 * Read data from the socket
 * @param socket The socket
 * @param buff Output buffer
 * @param size Maximum output size
 * @return If 0, then the connection was closed but the ksocket remains open.
 *   If -1, then the ksocket itself has died.
 *   If positive, the number of bytes read.
 */
int ksocket_read(int socket, void ** buffOut) {
    ksocket_header_t header;
    if (ksocket_read_ensure(socket, &header, 3) != 0) return -1;
    if (header.len == 0) {
        if (header.type == CONTROL_PACKET_HUNGUP) return 0;
        else return -1;
    }
    char * buff = (char *)malloc(htons(header.len));
    if (ksocket_read_ensure(socket, buff, htons(header.len)) != 0) {
        free(buff);
        return -1;
    }
    if (header.type != CONTROL_PACKET_DATA) {
        free(buff);
        return -1;
    }
    *buffOut = buff;
    return htons(header.len);
}

int ksocket_send(int socket, const void * buff, int len) {
    int offset = 0;
    while (offset < len) {
        int nextSize = len - offset > 0xffff ? 0xffff : len - offset;
        ksocket_header_t header;
        header.type = CONTROL_PACKET_SEND;
        header.len = htons(nextSize);
        if (ksocket_write_ensure(socket, &header, 3) != 0) return -1;
        if (ksocket_write_ensure(socket, &buff[offset], nextSize) != 0) return -1;
        offset += nextSize;
    }
    return 0;
}

#pragma mark - Private -

static int ksocket_read_ensure(int fd, void * buff, int len) {
    int off = 0;
    char * cBuf = (char *)buff;
    while (off < len) {
        errno = 0;
        ssize_t res = read(fd, &cBuf[off], len - off);
        if (res < 0) {
            if (errno == EINTR) continue;
            else return -1;
        }
        off += res;
    }
    return 0;
}

static int ksocket_write_ensure(int fd, const void * buff, int len) {
    int off = 0;
    const char * cBuf = (const char *)buff;
    while (off < len) {
        errno = 0;
        ssize_t res = write(fd, &cBuf[off], len - off);
        if (res < 0) {
            if (errno == EINTR) continue;
            else return -1;
        }
        off += res;
    }
    return 0;
}

static int ksocket_wait_response(int fd) {
    ksocket_header_t header;
    if (ksocket_read_ensure(fd, &header, 3) != 0) return -1;
    if (header.len == 0) {
        if (header.type == CONTROL_PACKET_CONNECTED) return 0;
        else return -1;
    }
    char * buff = (char *)malloc(htons(header.len));
    if (ksocket_read_ensure(fd, buff, htons(header.len)) != 0) {
        free(buff);
        return -1;
    }
    if (header.type == CONTROL_PACKET_CONNECTED) {
        free(buff);
        return 0;
    } else if (header.type == CONTROL_PACKET_ERROR) {
        uint32_t origError = *((uint32_t *)buff);
        uint32_t error = htonl(origError);
        errno = error;
    }
    free(buff);
    return -1;
}
