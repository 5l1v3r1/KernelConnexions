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
static uint32_t connectionsCount = 0;
static uint32_t connectionsAlloc = 0;
static uint32_t identifierIncrement = 1;

static KCConnection * kc_connection_lock(uint32_t identifier);
static void kc_connection_unlock(KCConnection * conn);

static errno_t kc_socket_set_nonblocking(socket_t so);
static void kc_upcall(socket_t so, void * cookie, int waitf);
static void kc_upcall_dispatched(void * cookie);
static void kc_upcall_check_data_return(KCConnection * connection);
static errno_t kc_upcall_data_iteration(KCConnection * connection);
static errno_t kc_upcall_write_iteration(KCConnection * connection);

__private_extern__
kern_return_t connection_initialize() {
    mutexGroup = lck_grp_alloc_init("connection", LCK_GRP_ATTR_NULL);
    if (!mutexGroup) return KERN_FAILURE;
    listMutex = lck_mtx_alloc_init(mutexGroup, LCK_ATTR_NULL);
    if (!listMutex) {
        lck_grp_free(mutexGroup);
        return KERN_FAILURE;
    }
    connections = (KCConnection **)OSMalloc(sizeof(KCConnection *) * 2, general_malloc_tag());
    if (!connections) {
        lck_mtx_free(listMutex, mutexGroup);
        lck_grp_free(mutexGroup);
        return KERN_FAILURE;
    }
    connectionsAlloc = 2;
    return KERN_SUCCESS;
}

__private_extern__
void connection_finalize() {
    lck_mtx_free(listMutex, mutexGroup);
    lck_grp_free(mutexGroup);
    OSFree(connections, connectionsAlloc * (uint32_t)sizeof(KCConnection *), general_malloc_tag());
}

__private_extern__
uint32_t kc_connection_create(KCConnectionCallbacks callbacks, void * userData) {
    OSMallocTag tag = general_malloc_tag();
    KCConnection * newConnection = OSMalloc(sizeof(KCConnection), tag);
    if (!newConnection) return 0;
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
        uint32_t oldSize = connectionsAlloc * (uint32_t)sizeof(KCConnection *);
        connectionsAlloc += 2;
        KCConnection ** newBuffer = (KCConnection **)OSMalloc((uint32_t)sizeof(KCConnection *) * connectionsAlloc, tag);
        if (!newBuffer) {
            lck_mtx_free(newConnection->lock, mutexGroup);
            OSFree(newConnection, sizeof(KCConnection), tag);
            lck_mtx_unlock(listMutex);
            return 0;
        }
        memcpy(newBuffer, connections, oldSize);
        OSFree(connections, oldSize, tag);
        connections = newBuffer;
    }
    connections[connectionsCount++] = newConnection;
    
    uint32_t id = newConnection->identifier;
    lck_mtx_unlock(listMutex);
    
    return id;
}

__private_extern__
void kc_connection_destroy(uint32_t identifier) {
    KCConnection * connection = NULL;
    
    lck_mtx_lock(listMutex);
    // remove the connection from the list
    for (size_t i = 0; i < connectionsCount; i++) {
        if (connection) {
            connections[i - 1] = connections[i];
        } else if (connections[i]->identifier == identifier) {
            connection = connections[i];
        }
    }
    if (!connection) {
        lck_mtx_unlock(listMutex);
        return;
    }
    connectionsCount -= 1;
    lck_mtx_unlock(listMutex);
    
    OSMallocTag tag = general_malloc_tag();
    lck_mtx_lock(connection->lock);
    if (connection->socket) {
        sock_close(connection->socket);
        connection->socket = NULL;
    }
    if (connection->writeBuffer) {
        mbuf_free(connection->writeBuffer);
        connection->writeBuffer = NULL;
    }
    lck_mtx_t * mtx = connection->lock;
    lck_mtx_unlock(connection->lock);
    lck_mtx_free(mtx, mutexGroup);
    OSFree(connection, sizeof(KCConnection), tag);
}

__private_extern__
errno_t kc_connection_connect(uint32_t identifier, const void * host, uint16_t port, boolean_t isIpv6) {
    errno_t error;
    KCConnection * connection;
    if (!(connection = kc_connection_lock(identifier))) return ENOENT;
    if (connection->socket) {
        kc_connection_unlock(connection);
        return EALREADY;
    }
    if (isIpv6) {
        struct sockaddr_in6 addr;
        bzero(&addr, sizeof(addr));
        memcpy(&addr.sin6_addr, host, sizeof(addr.sin6_addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_len = sizeof(addr);
        addr.sin6_port = port;
        error = sock_socket(AF_INET6, SOCK_STREAM, 0,
                            kc_upcall, number_to_pointer(identifier), &connection->socket);
        if (error) {
            debugf("sock_socket(ipv6): error %d", error);
            kc_connection_unlock(connection);
            return error;
        }
        if ((error = kc_socket_set_nonblocking(connection->socket))) {
            debugf("kc_socket_set_nonblocking(ipv6): error %d", error);
            sock_close(connection->socket);
            connection->socket = NULL;
            kc_connection_unlock(connection);
            return error;
        }
        error = sock_connect(connection->socket, (const struct sockaddr *)&addr, MSG_DONTWAIT);
        if (error != EINPROGRESS && error) {
            debugf("sock_connect(ipv6): error %d", error);
            sock_close(connection->socket);
            connection->socket = NULL;
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
                            kc_upcall, number_to_pointer(identifier), &connection->socket);
        if (error) {
            debugf("sock_socket(ipv4): error %d", error);
            kc_connection_unlock(connection);
            return error;
        }
        if ((error = kc_socket_set_nonblocking(connection->socket))) {
            debugf("kc_socket_set_nonblocking(ipv4): error %d", error);
            sock_close(connection->socket);
            connection->socket = NULL;
            kc_connection_unlock(connection);
            return error;
        }
        error = sock_connect(connection->socket, (const struct sockaddr *)&addr, MSG_DONTWAIT);
        if (error != EINPROGRESS && error) {
            debugf("sock_connect(ipv4): error %d", error);
            sock_close(connection->socket);
            connection->socket = NULL;
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
        debugf("sock_connect(ipv4): final result: %d", error);
    }
    return error;
}

__private_extern__
errno_t kc_connection_write(uint32_t identifier, const void * buffer, size_t length) {
    KCConnection * connection;
    if (!(connection = kc_connection_lock(identifier))) return ENOENT;
    
    mbuf_t bodyPacket;
    if (mbuf_allocpacket(MBUF_WAITOK, length, NULL, &bodyPacket)) {
        kc_connection_unlock(connection);
        return ENOMEM;
    }
    mbuf_copyback(bodyPacket, 0, length, buffer, MBUF_WAITOK);
    mbuf_settype(bodyPacket, MBUF_TYPE_DATA);
    mbuf_setlen(bodyPacket, length);
    
    if (connection->writeBuffer) {
        connection->writeBuffer = mbuf_concatenate(connection->writeBuffer, bodyPacket);
    } else {
        connection->writeBuffer = bodyPacket;
    }
        
    errno_t error;
    while (!(error = kc_upcall_write_iteration(connection))) {
    }
    if (error == EWOULDBLOCK) error = EINPROGRESS;
    else if (error == ENODATA) error = 0;
    
    kc_connection_unlock(connection);
    return error;
}

__private_extern__
errno_t kc_connection_close(uint32_t identifier) {
    KCConnection * connection;
    if (!(connection = kc_connection_lock(identifier))) return ENOENT;
    if (connection->socket) {
        sock_close(connection->socket);
        connection->socket = NULL;
        connection->isConnected = FALSE;
    }
    kc_connection_unlock(connection);
    return 0;
}

__private_extern__
void * kc_connection_get_user_data(uint32_t identifier) {
    KCConnection * connection;
    if (!(connection = kc_connection_lock(identifier))) return NULL;
    void * buff = connection->userData;
    kc_connection_unlock(connection);
    return buff;
}

#pragma mark - Private -

static KCConnection * kc_connection_lock(uint32_t identifier) {
    KCConnection * conn = NULL;
    lck_mtx_lock(listMutex);
    for (size_t i = 0; i < connectionsCount; i++) {
        if (connections[i]->identifier == identifier) {
            conn = connections[i];
            break;
        }
    }
    if (!conn) {
        lck_mtx_unlock(listMutex);
        return NULL;
    }
    lck_mtx_lock(conn->lock);
    lck_mtx_unlock(listMutex);
    return conn;
}

static void kc_connection_unlock(KCConnection * conn) {
    lck_mtx_unlock(conn->lock);
}

static errno_t kc_socket_set_nonblocking(socket_t so) {
    errno_t err;
    int val = 1;
    
    err = sock_ioctl(so, FIONBIO, &val);
    return err;
}

static void kc_upcall(socket_t so, void * cookie, int waitf) {
    dispatch_push(kc_upcall_dispatched, cookie);
}

static void kc_upcall_dispatched(void * cookie) {
    uint32_t identifier = pointer_to_number(cookie);
    KCConnection * connection;
    if (!(connection = kc_connection_lock(identifier))) return;
    socket_t so = connection->socket;
    if (!so) {
        kc_connection_unlock(connection);
        return;
    }
    if (!connection->isConnected) {
        debugf("not already connected");
        void * cb;
        if (sock_isconnected(so)) {
            connection->isConnected = TRUE;
            void * cb = connection->opened_cb;
            kc_connection_unlock(connection);
            ((kc_connection_opened)cb)(identifier);
        } else {
            sock_close(connection->socket);
            connection->socket = NULL;
            connection->isConnected = FALSE;
            cb = connection->failed_cb;
            kc_connection_unlock(connection);
            ((kc_connection_failed)cb)(identifier, 0);
        }
    } else {
        kc_upcall_check_data_return(connection);
    }

}

static void kc_upcall_check_data_return(KCConnection * connection) {
    debugf("check data");
    void * cb = NULL;
    socket_t so = connection->socket;
    uint32_t identifier = connection->identifier;
    if (!sock_isconnected(so)) {
        debugf("not connected");
        if (connection->socket) {
            sock_close(connection->socket);
            connection->socket = NULL;
        }
        connection->isConnected = FALSE;
        cb = connection->closed_cb;
        kc_connection_unlock(connection);
        ((kc_connection_closed)cb)(identifier);
    } else {
        debugf("doing data iterations");
        errno_t error = 0;
        while (!(error = kc_upcall_data_iteration(connection))) {
            if (!kc_connection_lock(identifier)) return;
        }
        if (error == ESHUTDOWN) {
            sock_close(connection->socket);
            connection->socket = NULL;
            connection->isConnected = FALSE;
            cb = connection->closed_cb;
            kc_connection_unlock(connection);
            ((kc_connection_closed)cb)(identifier);
            return;
        } else if (error == EJUSTRETURN) {
            kc_connection_unlock(connection);
            return;
        } else if (error != EWOULDBLOCK && error) {
            sock_close(connection->socket);
            connection->socket = NULL;
            connection->isConnected = FALSE;
            cb = connection->failed_cb;
            kc_connection_unlock(connection);
            ((kc_connection_failed)cb)(identifier, error);
            return;
        }
        while (!(error = kc_upcall_write_iteration(connection))) {
        }
        if (error != EWOULDBLOCK && error != ENODATA) {
            connection->isConnected = FALSE;
            sock_close(connection->socket);
            connection->socket = NULL;
            cb = connection->failed_cb;
            kc_connection_unlock(connection);
            ((kc_connection_failed)cb)(identifier, error);
            return;
        }
        kc_connection_unlock(connection);
    }
}

static errno_t kc_upcall_data_iteration(KCConnection * connection) {
    if (!connection->socket) return EJUSTRETURN;
    mbuf_t buffer;
    size_t recvLen = 0xFFFF;
    errno_t error = sock_receivembuf(connection->socket, NULL, &buffer, MSG_DONTWAIT, &recvLen);
    if (error) {
        return error;
    } else {
        if (!buffer && !recvLen) {
            return ESHUTDOWN;
        }
    }

    uint32_t len = (uint32_t)mbuf_len(buffer);
    char * rawData = OSMalloc(len, general_malloc_tag());
    if (!rawData) {
        return ENOMEM;
    }
    mbuf_copydata(buffer, 0, len, rawData);
    mbuf_free(buffer);
    void * cb = connection->newdata_cb;
    uint32_t identifier = connection->identifier;
    kc_connection_unlock(connection);
    ((kc_connection_newdata)cb)(identifier, rawData, len);
    return 0;
}

static errno_t kc_upcall_write_iteration(KCConnection * connection) {
    if (!connection->writeBuffer) return ENODATA;
    size_t sentCount = 0;
    mbuf_t packetCopy;
    mbuf_dup(connection->writeBuffer, MBUF_WAITOK, &packetCopy);
    // note: our mbuf is automatically freed by sock_sendmbuf
    errno_t error = sock_sendmbuf(connection->socket, NULL, packetCopy, MSG_DONTWAIT, &sentCount);
    if (error) return error;
    if (sentCount == mbuf_len(connection->writeBuffer)) {
        mbuf_free(connection->writeBuffer);
        connection->writeBuffer = NULL;
    } else {
        // remove the first sentCount bytes
        mbuf_adj(connection->writeBuffer, (int)sentCount);
    }
    return 0;
}
