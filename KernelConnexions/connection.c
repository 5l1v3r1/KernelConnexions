//
//  connection.c
//  KernelConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#include "connection.h"

static lck_grp_t * mutexGroup = NULL;
static lck_mtx_t * listMutex = NULL;
static KCConnection ** connections = NULL;
static size_t connectionsCount = 0;
static size_t connectionsAlloc = 0;
static uint32_t identifierIncrement = 1;

static KCConnection * kc_connection_lock(uint32_t identifier);
static void kc_connection_unlock(KCConnection * conn);

static void kc_upcall(socket_t so, void * cookie, int waitf);
static void kc_upcall_dispatched(void * cookie);
static void kc_upcall_check_data_return(KCConnection * connection);

__private_extern__
kern_return_t connection_initialize() {
    mutexGroup = lck_grp_alloc_init("connection", LCK_GRP_ATTR_NULL);
    if (!mutexGroup) return KERN_FAILURE;
    listMutex = lck_mtx_alloc_init(mutexGroup, LCK_ATTR_NULL);
    if (!listMutex) return KERN_FAILURE;
    connections = (KCConnection **)OSMalloc(sizeof(KCConnection *) * 2, general_malloc_tag());
    if (!connections) return KERN_FAILURE;
    connectionsAlloc = 2;
    return KERN_SUCCESS;
}

__private_extern__
void connection_finalize() {
    lck_mtx_free(listMutex, mutexGroup);
    lck_grp_free(mutexGroup);
    OSFree(connections, (uint32_t)connectionsAlloc * (uint32_t)sizeof(KCConnection), general_malloc_tag());
}

__private_extern__
uint32_t kc_connection_create(KCConnectionCallbacks callbacks, void * userData) {
    OSMallocTag tag = general_malloc_tag();
    KCConnection * newConnection = OSMalloc(sizeof(KCConnection), tag);
    if (!newConnection) return NULL;
    bzero(newConnection, sizeof(KCConnection));
    newConnection->lock = lck_mtx_alloc_init(mutexGroup, LCK_ATTR_NULL);
    
    newConnection->newdata_cb = callbacks.newdata;
    newConnection->closed_cb = callbacks.closed;
    newConnection->opened_cb = callbacks.opened;
    newConnection->failed_cb = callbacks.failed;
    newConnection->userData = userData;
    
    lck_mtx_lock(listMutex);
    newConnection->identifier = identifierIncrement++; // this is within the lock for a reason
    if (connectionsCount == connectionsAlloc) {
        uint32_t oldSize = (uint32_t)connectionsAlloc * (uint32_t)sizeof(KCConnection *);
        connectionsAlloc += 2;
        KCConnection ** newBuffer = (KCConnection **)OSMalloc((uint32_t)sizeof(KCConnection *) * (uint32_t)connectionsAlloc, tag);
        if (!newBuffer) {
            lck_mtx_free(newConnection->lock, mutexGroup);
            OSFree(newConnection, sizeof(KCConnection), tag);
            return NULL;
        }
        for (size_t i = 0; i < connectionsCount; i++) {
            newBuffer[i] = connections[i];
        }
        OSFree(connections, oldSize, tag);
        connections = newBuffer;
    }
    connections[connectionsCount++] = newConnection;
    lck_mtx_unlock(listMutex);
    
    return newConnection->identifier;
}

__private_extern__
void kc_connection_destroy(uint32_t identifier) {
    KCConnection * connection = NULL;
    
    lck_mtx_lock(listMutex);
    // remove the connection from the list
    for (size_t i = 0; i < connectionsCount; i++) {
        if (connection) {
            connections[i - 1] = connections[i];
        } else {
            if (connections[i]->identifier == identifier) {
                connection = connections[i];
            }
        }
    }
    if (!connection) return; // it has already been removed
    connectionsCount -= 1;
    lck_mtx_unlock(listMutex);
    
    OSMallocTag tag = general_malloc_tag();
    lck_mtx_lock(connection->lock);
    kc_connection_lock(connection, FALSE);
    if (connection->socket) {
        sock_close(connection->socket);
        connection->socket = NULL;
    }
    lck_mtx_t * mtx = connection->lock;
    lck_mtx_free(mtx, mutexGroup);
    OSFree(connection, sizeof(KCConnection), tag);
    // TODO: figure out if I need to unlock before free
}

__private_extern__
errno_t kc_connection_connect(uint32_t identifier, const void * host, uint16_t port, boolean_t isIpv6) {
    errno_t error;
    KCConnection * connection;
    if (!(connection = kc_connection_lock(identifier))) return ENOENT;
    if (isIpv6) {
        struct sockaddr_in6 addr;
        bzero(&addr, sizeof(addr));
        memcpy(&addr.sin6_addr, host, sizeof(addr.sin6_addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_len = sizeof(addr);
        addr.sin6_port = port;
        error = sock_socket(AF_INET6, SOCK_STREAM, 0,
                            kc_upcall, connection, &connection->socket);
        if (error) {
            debugf("sock_socket(ipv6): error %d", error);
            kc_connection_unlock(connection);
            return error;
        }
        error = sock_connect(connection->socket, (const struct sockaddr *)&addr, MSG_DONTWAIT);
        if (error != EINPROGRESS && error) {
            debugf("sock_connect(ipv6): error %d", error);
            kc_connection_unlock(connection);
            return error;
        }
        if (!error) {
            // call callback immediately
            kc_connection_opened cb = (kc_connection_opened)connection->opened_cb;
            kc_connection_unlock(connection);
            cb(identifier);
        } else {
            kc_connection_unlock(connection);
        }
    } else {
        struct sockaddr_in addr;
        bzero(&addr, sizeof(addr));
        memcpy(&addr.sin_addr, host, sizeof(addr.sin_addr));
        addr.sin_family = AF_INET;
        addr.sin_len = sizeof(addr);
        addr.sin_port = port;
        error = sock_socket(AF_INET, SOCK_STREAM, 0,
                            kc_upcall, connection, &connection->socket);
        if (error) {
            debugf("sock_socket(ipv4): error %d", error);
            kc_connection_unlock(connection);
            return error;
        }
        error = sock_connect(connection->socket, (const struct sockaddr *)&addr, MSG_DONTWAIT);
        if (error != EINPROGRESS && error) {
            debugf("sock_connect(ipv4): error %d", error);
            kc_connection_unlock(connection);
            return error;
        }
        if (!error) {
            // call callback immediately
            kc_connection_opened cb = (kc_connection_opened)connection->opened_cb;
            kc_connection_unlock(connection);
            cb(identifier);
        } else {
            kc_connection_unlock(connection);
        }
    }
    return error;
}

__private_extern__
errno_t kc_connection_write(uint32_t identifier, const void * buffer, size_t length) {
    KCConnection * connection;
    if (!(connection = kc_connection_lock(identifier))) return ENOENT;
    
    mbuf_t bodyPacket;
    size_t offset = 0;
    
    while (offset < length) {
        if (mbuf_allocpacket(MBUF_WAITOK, length - offset, NULL, &bodyPacket)) {
            return ENOMEM;
        }
        
        size_t sentCount = 0;
        mbuf_copyback(bodyPacket, 0, length - offset, &((char *)buffer)[offset], MBUF_WAITOK);
        mbuf_settype(bodyPacket, MBUF_TYPE_DATA);
        mbuf_pkthdr_setlen(bodyPacket, length);
        errno_t error = sock_sendmbuf(connection->socket, NULL, bodyPacket, 0, &sentCount);
        if (error) {
            debugf("sock_sendmbuf error: %d", error);
            kc_connection_unlock(connection);
            return error;
        }
        offset += sentCount;
        // note: our mbuf is automatically freed by sock_sendmbuf
    }
    
    kc_connection_unlock(connection);
    return 0;
}

#pragma mark - Private -

static KCConnection * kc_connection_lock(uint32_t identifier) {
    KCConnection * conn = NULL;
    lck_mtx_lock(listMutex);
    boolean_t hasFound = FALSE;
    for (size_t i = 0; i < connectionsCount; i++) {
        if (connections[i]->identifier == identifier) {
            hasFound = TRUE;
            conn = connections[i];
            break;
        }
    }
    if (!hasFound) {
        return NULL;
    }
    lck_mtx_lock(conn->lock);
    lck_mtx_unlock(listMutex);
    return conn;
}

static void kc_connection_unlock(KCConnection * conn) {
    lck_mtx_unlock(conn->lock);
}

static void kc_upcall(socket_t so, void * cookie, int waitf) {
    dispatch_push(kc_upcall_dispatched, cookie);
}

static void kc_upcall_dispatched(void * cookie) {
    uint32_t identifier = (uint32_t)cookie;
    KCConnection * connection;
    if (!(connection = kc_connection_lock(identifier))) return;
    socket_t so = connection->socket;
    if (!connection->isConnected) {
        void * cb;
        if (sock_isconnected(so)) {
            connection->isConnected = TRUE;
            void * cb = connection->opened_cb;
            kc_connection_unlock(connection);
            ((kc_connection_opened)cb)(identifier);
        } else {
            sock_close(connection->socket);
            connection->socket = NULL;
            cb = connection->failed_cb;
            kc_connection_unlock(connection);
            ((kc_connection_failed)cb)(identifier, 0);
        }
    } else {
        kc_upcall_check_data_return(connection);
    }

}

static void kc_upcall_check_data_return(KCConnection * connection) {
    void * cb = NULL;
    socket_t so = connection->socket;
    uint32_t identifier = connection->identifier;
    if (!sock_isconnected(so)) {
        sock_close(connection->socket);
        connection->isConnected = FALSE;
        connection->socket = NULL;
        cb = connection->closed_cb;
        kc_connection_unlock(connection);
        ((kc_connection_closed)cb)(identifier);
    } else {
        mbuf_t buffer;
        size_t recvLen = 0xFFFFFFFF; // I just hope this will always be enough; maybe in 2032 it won't be
        errno_t error = sock_receivembuf(so, NULL, &buffer, 0, &recvLen);
        if (error) {
            debugf("sock_receivembuf: error %d", error);
            connection->isConnected = FALSE;
            sock_close(connection->socket);
            connection->socket = NULL;
            cb = connection->failed_cb;
            kc_connection_unlock(connection);
            ((kc_connection_failed)cb)(identifier, error);
            return;
        }
        uint32_t len = (uint32_t)mbuf_len(buffer);
        char * rawData = OSMalloc(len, general_malloc_tag());
        if (!rawData) {
            debugf("fatal: failed to allocate received data!");
            cb = connection->failed_cb;
            kc_connection_unlock(connection);
            ((kc_connection_failed)cb)(identifier, ENOMEM);
            return;
        }
        mbuf_copydata(buffer, 0, len, rawData);
        mbuf_free(buffer);
        cb = connection->newdata_cb;
        kc_connection_unlock(connection);
        ((kc_connection_newdata)cb)(identifier, rawData, len);
        OSFree(rawData, len, general_malloc_tag());
    }
}
