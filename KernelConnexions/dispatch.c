//
//  dispatch.c
//  KernelConnexions
//
//  Created by Alex Nichol on 11/21/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#include "dispatch.h"

static lck_grp_t * queueGroup = NULL;
static lck_mtx_t * queueMutex = NULL;
static KCDispatchCB * dispatches = NULL;
static uint32_t dispatchesCount = 0;
static uint32_t dispatchesAlloc = 0;
static uint32_t queueStatus = 0; // 1 = stopping, 2 = stopped

static thread_t backgroundThread = NULL;
static void dispatch_queue_main();

__private_extern__
kern_return_t dispatch_initialize() {
    // TODO: free stuff on failure
    dispatches = (KCDispatchCB *)OSMalloc(sizeof(KCDispatchCB) * 2, general_malloc_tag());
    if (!dispatches) {
        return KERN_FAILURE;
    }
    dispatchesAlloc = 2;
    
    queueGroup = lck_grp_alloc_init("queue", LCK_GRP_ATTR_NULL);
    if (!queueGroup) {
        OSFree(dispatches, sizeof(KCDispatchCB) * dispatchesAlloc, general_malloc_tag());
        return KERN_FAILURE;
    }
    queueMutex = lck_mtx_alloc_init(queueGroup, LCK_ATTR_NULL);
    if (!queueMutex) {
        OSFree(dispatches, sizeof(KCDispatchCB) * dispatchesAlloc, general_malloc_tag());
        lck_grp_free(queueGroup);
        return KERN_FAILURE;
    }
    
    if (kernel_thread_start(dispatch_queue_main, NULL, &backgroundThread) != KERN_SUCCESS) {
        OSFree(dispatches, sizeof(KCDispatchCB) * dispatchesAlloc, general_malloc_tag());
        lck_mtx_free(queueMutex, queueGroup);
        lck_grp_free(queueGroup);
        return KERN_FAILURE;
    }
    
    return KERN_SUCCESS;
}

__private_extern__
void dispatch_finalize() {
    // wait for the background thread to die
    lck_mtx_lock(queueMutex);
    queueStatus = 1;
    lck_mtx_unlock(queueMutex);
    while (true) {
        lck_mtx_lock(queueMutex);
        if (queueStatus == 2) {
            lck_mtx_unlock(queueMutex);
            break;
        }
        lck_mtx_unlock(queueMutex);
        IOSleep(10);
    }
    OSFree(dispatches, sizeof(KCDispatchCB) * dispatchesAlloc, general_malloc_tag());
    lck_mtx_free(queueMutex, queueGroup);
    lck_grp_free(queueGroup);
    thread_deallocate(backgroundThread);
}

__private_extern__
errno_t dispatch_push(void (*call)(void * data), void * data) {
    KCDispatchCB callback;
    callback.call = call;
    callback.data = data;
    lck_mtx_lock(queueMutex);
    if (dispatchesCount == dispatchesAlloc) {
        KCDispatchCB * newDispatches = (KCDispatchCB *)OSMalloc(sizeof(KCDispatchCB) * (dispatchesAlloc + 2), general_malloc_tag());
        if (!newDispatches) return ENOMEM;
        for (uint32_t i = 0; i < dispatchesCount; i++) {
            newDispatches[i] = dispatches[i];
        }
        OSFree(dispatches, sizeof(KCDispatchCB) * dispatchesAlloc, general_malloc_tag());
        dispatches = newDispatches;
        dispatchesAlloc += 2;
    }
    dispatches[dispatchesCount++] = callback;
    lck_mtx_unlock(queueMutex);
    return 0;
}

#pragma mark - Private -

static void dispatch_queue_main(void * nothing, wait_result_t waitResult) {
    while (true) {
        lck_mtx_lock(queueMutex);
        if (queueStatus != 0) {
            queueStatus = 2;
            lck_mtx_unlock(queueMutex);
            return;
        }
        
        if (dispatchesCount > 0) {
            KCDispatchCB callMe = dispatches[0];
            for (uint32_t i = 0; i + 1 < dispatchesCount; i++) {
                dispatches[i] = dispatches[i + 1];
            }
            dispatchesCount--;
            lck_mtx_unlock(queueMutex);
            callMe.call(callMe.data);
        } else {
            lck_mtx_unlock(queueMutex);
            IOSleep(10);
        }
    }
}
