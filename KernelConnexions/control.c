//
//  control.c
//  KernelConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#include "control.h"

static errno_t control_handle_connect(kern_ctl_ref kctlref, struct sockaddr_ctl * sac, void ** unitinfo);
static errno_t control_handle_disconnect(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo);
static errno_t control_handle_getopt(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, int opt, void * data, size_t * len);
static errno_t control_handle_send(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, mbuf_t m, int flags);
static errno_t control_handle_setopt(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, int opt, void * data, size_t len);

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

static kern_ctl_ref clientControl = NULL;

static lck_grp_t * mutexGroup = NULL;
static lck_mtx_t * listMutex = NULL;
static KCControl ** controlList = NULL;
static uint32_t controlCount = 0;
static uint32_t controlAlloc = 0;

__private_extern__
kern_return_t control_register() {
    controlList = OSMalloc(sizeof(KCControl) * 2, general_malloc_tag());
    if (!controlList) return KERN_FAILURE;
    controlAlloc = 2;
    
    errno_t error = ctl_register(&ConnexionsControlRegistration, &clientControl);
    if (error != 0) {
        debugf("fatal: failed to register control: %lu", error);
    }
    mutexGroup = lck_grp_alloc_init("control", LCK_GRP_ATTR_NULL);
    listMutex = lck_mtx_alloc_init(mutexGroup, LCK_ATTR_NULL);
    
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
            return EBUSY;
        } else {
            clientControl = NULL;
        }
    }
    return KERN_SUCCESS;
}

#pragma mark - Data Structures -

__private_extern__
KCControl * kc_control_create() {
    KCControl * control = OSMalloc(sizeof(KCControl), general_malloc_tag());
    if (!control) return NULL;
    lck_mtx_lock(listMutex);
    if (controlCount == controlAlloc) {
        KCControl ** newList = (KCControl **)OSMalloc((uint32_t)sizeof(KCControl *) * (controlAlloc + 2), general_malloc_tag());
        if (!newList) {
            OSFree(control, sizeof(KCControl), general_malloc_tag());
            return NULL;
        }
        for (uint32_t i = 0; i < controlCount; i++) {
            newList[i] = controlList[i];
        }
        OSFree(controlList, (uint32_t)sizeof(KCControl *) * (controlAlloc + 2), general_malloc_tag());
        controlList = newList;
        controlAlloc += 2;
    }
    controlList[controlCount++] = control;
    lck_mtx_unlock(listMutex);
    return control;
}

__private_extern__
errno_t kc_control_release(KCControl * control) {
    lck_mtx_lock(listMutex);
    // remove the connection from the list
    boolean_t hasFound = FALSE;
    for (uint32_t i = 0; i < controlCount; i++) {
        if (hasFound) {
            controlList[i - 1] = controlList[i];
        } else {
            if (controlList[i] == control) {
                hasFound = TRUE;
            }
        }
    }
    if (!hasFound) {
        lck_mtx_unlock(listMutex);
        return ENOENT; // it has already been removed
    }
    controlCount -= 1;
    
    if (control->currentPacketData) {
        OSFree(control->currentPacketData, control->currentPacketSize, general_malloc_tag());
    }
    OSFree(control, sizeof(KCControl), general_malloc_tag());
    
    lck_mtx_unlock(listMutex);
    
    return 0;
}

__private_extern__
errno_t kc_control_append_data(KCControl * control, mbuf_t buffer) {
    // assumes lock
    if (control->currentPacketData) {
        char * newBuffer = OSMalloc(control->currentPacketSize + (uint32_t)mbuf_len(buffer), general_malloc_tag());
        // TODO: i'm just too tired to code anymore
    } else {
        
    }
}

__private_extern__
errno_t kc_control_read_packet(KCControl * control, KCControlPacket ** packet) {
    
}

__private_extern__
KCControlPacket * kc_control_packet_allocate(uint16_t length) {
    
}

__private_extern__
void kc_control_packet_free(KCControlPacket * packet) {
    
}

#pragma mark - Control Private -

static errno_t control_handle_connect(kern_ctl_ref kctlref, struct sockaddr_ctl * sac, void ** unitinfo) {
    debugf("connected by PID %d", proc_selfpid());
    
    return 0;
}

static errno_t control_handle_disconnect(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo) {
    
    return 0;
}

static errno_t control_handle_getopt(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, int opt, void * data, size_t * len) {
    return 0;
}

static errno_t control_handle_send(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, mbuf_t m, int flags) {
    return 0;
}

static errno_t control_handle_setopt(kern_ctl_ref kctlref, u_int32_t unit, void * unitinfo, int opt, void * data, size_t len) {
    return 0;
}
