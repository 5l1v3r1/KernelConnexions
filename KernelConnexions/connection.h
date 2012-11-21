//
//  connection.h
//  KernelConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#ifndef KernelConnexions_connection_h
#define KernelConnexions_connection_h

#include "general.h"
#include "debug.h"
#include "dispatch.h"
#include <sys/kpi_socket.h>
#include <netinet/in.h>
#include <sys/mbuf.h>
#include <kern/locks.h>

typedef struct {
    socket_t socket;
    lck_mtx_t * lock;
    void * userData;
    void * opened_cb;
    void * closed_cb;
    void * failed_cb;
    void * newdata_cb;
    boolean_t isConnected;
    uint32_t identifier;
} KCConnection;

typedef void (*kc_connection_opened)(uint32_t identifier);
typedef void (*kc_connection_closed)(uint32_t identifier);
typedef void (*kc_connection_failed)(uint32_t identifier, errno_t error);
typedef void (*kc_connection_newdata)(uint32_t identifier, const char * buffer, size_t length);

typedef struct {
    kc_connection_opened opened;
    kc_connection_closed closed;
    kc_connection_failed failed;
    kc_connection_newdata newdata;
} KCConnectionCallbacks;

kern_return_t connection_initialize();
void connection_finalize();

uint32_t kc_connection_create(KCConnectionCallbacks callbacks, void * userData);
void kc_connection_destroy(uint32_t connection);
errno_t kc_connection_connect(uint32_t connection, const void * host, uint16_t port, boolean_t isIpv6);
errno_t kc_connection_write(uint32_t connection, const void * buffer, size_t length);

#endif
