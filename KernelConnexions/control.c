//
//  control.c
//  KernelConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#include "control.h"

static KCControl * kc_control_lock(uint32_t identifier);
static void kc_control_unlock(KCControl * control);
static void kc_process_packet(void * unitInfo);
static void kc_process_packet_connect(uint32_t identifier, KCControlPacket * packet);
static void kc_process_packet_send(uint32_t identifier, KCControlPacket * packet);
static void kc_process_packet_close(uint32_t identifier, KCControlPacket * packet);

static errno_t control_handle_connect(kern_ctl_ref kctlref, struct sockaddr_ctl * sac, void ** unitinfo);
static errno_t control_handle_disconnect(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo);
static errno_t control_handle_getopt(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, int opt, void * data, size_t * len);
static errno_t control_handle_send(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, mbuf_t m, int flags);
static errno_t control_handle_setopt(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, int opt, void * data, size_t len);

static void kc_connection_opened_callback(uint32_t connection);
static void kc_connection_closed_callback(uint32_t connection);
static void kc_connection_failed_callback(uint32_t connection, errno_t error);
static void kc_connection_newdata_callback(uint32_t connection, char * buffer, size_t size);

static struct kern_ctl_reg ConnexionsControlRegistration = {
    kBundleID,
    0,
    0,
    CTL_FLAG_PRIVILEGED | CTL_FLAG_REG_SOCK_STREAM, // you can remove CTL_FLAG_PRIVILEGED to give permission to *anybody*
    0,
    0,
    control_handle_connect,
    control_handle_disconnect,
    control_handle_send,
    control_handle_setopt,
    control_handle_getopt
};

static KCConnectionCallbacks ConnectionCallbacks = {
    kc_connection_opened_callback,
    kc_connection_closed_callback,
    kc_connection_failed_callback,
    kc_connection_newdata_callback
};

static kern_ctl_ref clientControl = NULL;

static lck_grp_t * mutexGroup = NULL;
static lck_mtx_t * listMutex = NULL;
static KCControl ** controls = NULL;
static uint32_t controlsCount = 0;
static uint32_t controlsAlloc = 0;
static uint32_t controlIdentifier = 1;

__private_extern__
kern_return_t control_register() {
    controls = OSMalloc(sizeof(KCControl *) * 2, general_malloc_tag());
    if (!controls) return KERN_FAILURE;
    controlsAlloc = 2;
    
    mutexGroup = lck_grp_alloc_init("control", LCK_GRP_ATTR_NULL);
    if (!mutexGroup) {
        OSFree(controls, (uint32_t)sizeof(KCControl *) * controlsAlloc, general_malloc_tag());
        return KERN_FAILURE;
    }
    
    listMutex = lck_mtx_alloc_init(mutexGroup, LCK_ATTR_NULL);
    if (!listMutex) {
        lck_grp_free(mutexGroup);
        OSFree(controls, (uint32_t)sizeof(KCControl *) * controlsAlloc, general_malloc_tag());
        return KERN_FAILURE;
    }
    
    errno_t error = ctl_register(&ConnexionsControlRegistration, &clientControl);
    if (error != 0) {
        debugf("fatal: failed to register control: %lu", error);
        lck_mtx_free(listMutex, mutexGroup);
        lck_grp_free(mutexGroup);
        OSFree(controls, (uint32_t)sizeof(KCControl *) * controlsAlloc, general_malloc_tag());
        return KERN_FAILURE;
    }
    
    return error;
}

__private_extern__
kern_ctl_ref control_get() {
    return clientControl;
}

__private_extern__
kern_return_t control_unregister() {
    if (clientControl) {
        if (ctl_deregister(clientControl) != 0) {
            debugf("failed unloading because of connected controls");
            return KERN_FAILURE;
        } else {
            clientControl = NULL;
        }
    }
    
    lck_mtx_free(listMutex, mutexGroup);
    lck_grp_free(mutexGroup);
    OSFree(controls, controlsAlloc * (uint32_t)sizeof(KCControl *), general_malloc_tag());
    
    return KERN_SUCCESS;
}

#pragma mark - Data Structures -

__private_extern__
uint32_t kc_control_create(uint32_t unit) {
    KCControl * control = (KCControl *)OSMalloc(sizeof(KCControl), general_malloc_tag());
    if (!control) return 0;
    bzero(control, sizeof(KCControl));
    control->unit = unit;
    control->lock = lck_mtx_alloc_init(mutexGroup, LCK_ATTR_NULL);
    
    lck_mtx_lock(listMutex);
    control->identifier = controlIdentifier++;
    control->connection = kc_connection_create(ConnectionCallbacks, number_to_pointer(control->identifier));
    if (control->connection == 0) {
        lck_mtx_unlock(listMutex);
        lck_mtx_free(control->lock, mutexGroup);
        OSFree(control, sizeof(KCControl), general_malloc_tag());
        return 0;
    }
    
    if (controlsAlloc == controlsCount) {
        uint32_t newSize = (controlsAlloc + 2) * (uint32_t)sizeof(KCControl *);
        KCControl ** newControls = (KCControl **)OSMalloc(newSize, general_malloc_tag());
        if (!newControls) {
            lck_mtx_free(control->lock, mutexGroup);
            kc_connection_destroy(control->connection);
            OSFree(control, sizeof(KCControl), general_malloc_tag());
            lck_mtx_unlock(listMutex);
            return 0;
        }
        memcpy(newControls, controls, sizeof(KCControl *) * controlsCount);
        OSFree(controls, (uint32_t)sizeof(KCControl *) * controlsAlloc, general_malloc_tag());
        controls = newControls;
        controlsAlloc += 2;
    }
    
    controls[controlsCount++] = control;
    
    uint32_t id = control->identifier;
    lck_mtx_unlock(listMutex);
    
    return id;
}

__private_extern__
errno_t kc_control_destroy(uint32_t identifier) {
    lck_mtx_lock(listMutex);
    
    KCControl * control = NULL;
    for (uint32_t i = 0; i < controlsCount; i++) {
        if (control) {
            controls[i - 1] = controls[i];
        } else if (controls[i]->identifier == identifier) {
            control = controls[i];
        }
    }
    if (!control) {
        lck_mtx_unlock(listMutex);
        return ENOENT;
    }
    controlsCount -= 1;
    lck_mtx_unlock(listMutex);
    
    lck_mtx_lock(control->lock);
    kc_connection_destroy(control->connection);
    if (control->buffer) {
        OSFree(control->buffer, control->bufferSize, general_malloc_tag());
    }
    lck_mtx_unlock(control->lock);
    lck_mtx_free(control->lock, mutexGroup);
    OSFree(control, sizeof(KCControl), general_malloc_tag());
    
    return 0;
}

__private_extern__
errno_t kc_control_append_data(uint32_t identifier, mbuf_t buffer) {
    KCControl * control;
    if (!(control = kc_control_lock(identifier))) return ENOENT;
    
    if (!control->buffer) {
        char * buff = OSMalloc((uint32_t)mbuf_len(buffer), general_malloc_tag());
        if (!buff) {
            kc_control_unlock(control);
            return ENOMEM;
        }
        control->buffer = buff;
        control->bufferSize = (uint32_t)mbuf_len(buffer);
    } else {
        char * newBuff = OSMalloc((uint32_t)mbuf_len(buffer) + control->bufferSize, general_malloc_tag());
        if (!newBuff) {
            kc_control_unlock(control);
            return ENOMEM;
        }
        memcpy(newBuff, control->buffer, control->bufferSize);
        OSFree(control->buffer, control->bufferSize, general_malloc_tag());
        control->buffer = newBuff;
        control->bufferSize += (uint32_t)mbuf_len(buffer);
    }
    uint32_t offset = control->bufferSize - (uint32_t)mbuf_len(buffer);
    mbuf_copydata(buffer, 0, mbuf_len(buffer), &control->buffer[offset]);
    
    kc_control_unlock(control);
    return 0;
}

__private_extern__
errno_t kc_control_read_packet(uint32_t identifier, KCControlPacket ** packet) {
    KCControl * control;
    if (!(control = kc_control_lock(identifier))) return ENOENT;
    
    if (!control->buffer) {
        kc_control_unlock(control);
        return ENODATA;
    }
    
    if (control->bufferSize > 3) {
        uint16_t sizeField = htons(*(uint16_t *)(&control->buffer[1]));
        if (control->bufferSize >= sizeField + 3) {
            KCControlPacket * readPacket = kc_control_packet_allocate(sizeField);
            readPacket->packetType = control->buffer[0];
            memcpy(readPacket->data, &control->buffer[3], sizeField);
            
            if (sizeField + 3 == control->bufferSize) {
                OSFree(control->buffer, control->bufferSize, general_malloc_tag());
                control->buffer = NULL;
                control->bufferSize = 0;
            } else {
                uint32_t newSize = control->bufferSize - sizeField - 3;
                char * cutdownBuff = OSMalloc(newSize, general_malloc_tag());
                if (!cutdownBuff) {
                    kc_control_packet_free(readPacket);
                    kc_control_unlock(control);
                    return ENOMEM;
                }
                memcpy(cutdownBuff, &control->buffer[sizeField + 3], newSize);
                OSFree(control->buffer, control->bufferSize, general_malloc_tag());
                control->buffer = cutdownBuff;
                control->bufferSize = newSize;
            }
            *packet = readPacket;
            kc_control_unlock(control);
            return 0;
        }
    }
    
    kc_control_unlock(control);
    return ENODATA;
}

__private_extern__
uint32_t kc_control_get_connection(uint32_t identifier) {
    KCControl * control;
    if (!(control = kc_control_lock(identifier))) return 0;
    
    uint32_t conn = control->connection;
    
    kc_control_unlock(control);
    return conn;
}

uint32_t kc_control_get_unit(uint32_t identifier) {
    KCControl * control;
    if (!(control = kc_control_lock(identifier))) return 0;
    uint32_t unit = control->unit;
    kc_control_unlock(control);
    return unit;
}

#pragma mark - Control Packets -

__private_extern__
KCControlPacket * kc_control_packet_allocate(uint16_t length) {
    uint32_t allocLen = length + (uint32_t)sizeof(KCControlPacket);
    KCControlPacket * packet = OSMalloc(allocLen, general_malloc_tag());
    if (!packet) return NULL;
    packet->packetType = 0;
    packet->length = length;
    packet->data = &((char *)packet)[sizeof(KCControlPacket)];
    bzero(packet->data, length);
    return packet;
}

__private_extern__
void kc_control_packet_free(KCControlPacket * packet) {
    uint32_t allocLen = packet->length + (uint32_t)sizeof(KCControlPacket);
    OSFree(packet, allocLen, general_malloc_tag());
}

#pragma mark - Private -

static KCControl * kc_control_lock(uint32_t identifier) {
    lck_mtx_lock(listMutex);
    KCControl * control = NULL;
    for (uint32_t i = 0; i < controlsCount; i++) {
        if (controls[i]->identifier == identifier) {
            control = controls[i];
            break;
        }
    }
    if (!control) {
        lck_mtx_unlock(listMutex);
        return NULL;
    }
    lck_mtx_lock(control->lock);
    lck_mtx_unlock(listMutex);
    return control;
}

static void kc_control_unlock(KCControl * control) {
    lck_mtx_unlock(control->lock);
}

#pragma mark - Processing -

static void kc_process_packet(void * unitInfo) {
    uint32_t identifier = pointer_to_number(unitInfo);
    KCControlPacket * packet = NULL;
    errno_t error = kc_control_read_packet(identifier, &packet);
    if (!error) {
        debugf("kc_process_packet of type: %d", (int)packet->packetType);
        if (packet->packetType == CONTROL_PACKET_CONNECT) {
            kc_process_packet_connect(identifier, packet);
        } else if (packet->packetType == CONTROL_PACKET_SEND) {
            kc_process_packet_send(identifier, packet);
        } else if (packet->packetType == CONTROL_PACKET_CLOSE) {
            kc_process_packet_close(identifier, packet);
        }
        kc_control_packet_free(packet);
    } else if (error == ENODATA) {
        debugf("kc_process_packet ENODATA");
    } else {
        debugf("kc_process_packet error: %d", error);
    }
}

static void kc_process_packet_connect(uint32_t identifier, KCControlPacket * packet) {
    debugf("kc_process_packet_connect: entry");
    boolean_t isIpv6 = FALSE;
    if (packet->length == 6) {
        debugf("kc_process_packet_connect: got ipv4 connect");
    } else if (packet->length == 18) {
        debugf("kc_process_packet_connect: got ipv6 connect");
        isIpv6 = TRUE;
    } else {
        debugf("kc_process_packet_connect: unknown ip version packet size");
        return;
    }
    uint16_t port = *(uint16_t *)packet->data;
    void * addr = &packet->data[2];
    uint32_t conn = kc_control_get_connection(identifier);
    if (conn) {
        kc_connection_connect(conn, addr, port, isIpv6);
    }
}

static void kc_process_packet_send(uint32_t identifier, KCControlPacket * packet) {
    if (packet->length > 0) {
        uint32_t conn = kc_control_get_connection(identifier);
        if (conn) {
            kc_connection_write(conn, packet->data, packet->length);
        }
    }
}

static void kc_process_packet_close(uint32_t identifier, KCControlPacket * packet) {
    uint32_t conn = kc_control_get_connection(identifier);
    if (conn) {
        kc_connection_close(conn);
    }
}

#pragma mark - Control Private -

static errno_t control_handle_connect(kern_ctl_ref kctlref, struct sockaddr_ctl * sac, void ** unitinfo) {
    debugf("connected by PID %d", proc_selfpid());
    uint32_t info = kc_control_create(sac->sc_unit);
    if (!info) return ENOMEM;
    *unitinfo = number_to_pointer(info); // this is ugly but screw it
    return 0;
}

static errno_t control_handle_disconnect(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo) {
    uint32_t identifier = pointer_to_number(unitinfo);
    kc_control_destroy(identifier);
    return 0;
}

static errno_t control_handle_getopt(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, int opt, void * data, size_t * len) {
    return 0;
}

static errno_t control_handle_send(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, mbuf_t m, int flags) {
    uint32_t identifier = pointer_to_number(unitinfo);
    errno_t error;
    if ((error = kc_control_append_data(identifier, m))) {
        return error;
    }
    return dispatch_push(kc_process_packet, number_to_pointer(identifier));
}

static errno_t control_handle_setopt(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, int opt, void * data, size_t len) {
    return 0;
}

#pragma mark - Connection Callbacks -

static void kc_connection_opened_callback(uint32_t connection) {
    void * userInfo = kc_connection_get_user_data(connection);
    uint32_t identifier = pointer_to_number(userInfo);
    if (!identifier) return;
    
    char data[] = {CONTROL_PACKET_CONNECTED, 0, 0};
    uint32_t unit = kc_control_get_unit(identifier);
    if (ctl_enqueuedata(clientControl, unit, data, 3, 0)) {
        debugf("error calling ctl_enqueuedata");
    }
}

static void kc_connection_closed_callback(uint32_t connection) {
    void * userInfo = kc_connection_get_user_data(connection);
    uint32_t identifier = pointer_to_number(userInfo);
    if (!identifier) return;
    
    char data[] = {CONTROL_PACKET_HUNGUP, 0, 0};
    uint32_t unit = kc_control_get_unit(identifier);
    if (ctl_enqueuedata(clientControl, unit, data, 3, 0)) {
        debugf("error calling ctl_enqueuedata");
    }
}

static void kc_connection_failed_callback(uint32_t connection, errno_t error) {
    void * userInfo = kc_connection_get_user_data(connection);
    uint32_t identifier = pointer_to_number(userInfo);
    if (!identifier) return;
    
    char data[7] = {CONTROL_PACKET_HUNGUP, 0, 4};
    uint32_t errorBig = htonl(error);
    memcpy(&data[3], &errorBig, 4);
    
    uint32_t unit = kc_control_get_unit(identifier);
    if (ctl_enqueuedata(clientControl, unit, data, 7, 0)) {
        debugf("error calling ctl_enqueuedata");
    }
}

static void kc_connection_newdata_callback(uint32_t connection, char * buffer, size_t size) {
    void * userInfo = kc_connection_get_user_data(connection);
    uint32_t identifier = pointer_to_number(userInfo);
    if (!identifier) {
        OSFree(buffer, (uint32_t)size, general_malloc_tag());
        return;
    }
    uint32_t unit = kc_control_get_unit(identifier);
        
    const char * subBuffer = buffer;
    uint32_t subSize = (uint32_t)size;
    while (subSize > 0) {
        uint32_t useSize = subSize < 65536 ? subSize : 65535;
        char * data = (char *)OSMalloc(useSize + 3, general_malloc_tag());
        if (!data) {
            debugf("%s: failed to allocate", __FUNCTION__);
            OSFree(buffer, (uint32_t)size, general_malloc_tag());
            return;
        }
        data[0] = CONTROL_PACKET_DATA;
        uint16_t sizeBig = htons(useSize);
        memcpy(&data[1], &sizeBig, 2);
        memcpy(&data[3], subBuffer, useSize);
        subBuffer = &subBuffer[useSize];
        subSize -= useSize;
        if (ctl_enqueuedata(clientControl, unit, data, useSize + 3, 0)) {
            debugf("%s: failed to enqueue data", __FUNCTION__);
            OSFree(data, useSize + 3, general_malloc_tag());
            OSFree(buffer, (uint32_t)size, general_malloc_tag());
            return;
        }
        OSFree(data, useSize + 3, general_malloc_tag());
    }
    OSFree(buffer, (uint32_t)size, general_malloc_tag());
}
