//
//  KernelConnexions.c
//  KernelConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#include <mach/mach_types.h>

#include <libkern/OSMalloc.h> // gives allocation stuff
#include <stdarg.h> // gives va_list
#include <sys/proc.h> // gives proc_selfpid()

#include "const.h"
#include "general.h"
#include "connection.h"
#include "control.h"
#include "dispatch.h"

kern_return_t KernelConnexions_start(kmod_info_t * ki, void * d);
kern_return_t KernelConnexions_stop(kmod_info_t * ki, void * d);

kern_return_t KernelConnexions_start(kmod_info_t * ki, void * d) {
    general_initialize();
    errno_t error;
    
    error = dispatch_initialize();
    if (error != KERN_SUCCESS) {
        general_finalize();
    }
    
    error = connection_initialize();
    if (error != KERN_SUCCESS) {
        dispatch_finalize();
        general_finalize();
    }
    
    error = control_register();
    if (error != KERN_SUCCESS) {
        connection_finalize();
        dispatch_finalize();
        general_finalize();
        return error;
    }
    
    return KERN_SUCCESS;
}

kern_return_t KernelConnexions_stop(kmod_info_t * ki, void * d) {
    control_unregister();
    connection_finalize();
    general_finalize();
    return KERN_SUCCESS;
}

#pragma mark - Debugging -
