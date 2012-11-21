//
//  dispatch.h
//  KernelConnexions
//
//  Created by Alex Nichol on 11/21/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#ifndef KernelConnexions_dispatch_h
#define KernelConnexions_dispatch_h

#include <mach/mach_types.h>
#include <IOKit/IOLib.h>
#include "general.h"
#include "debug.h"

typedef struct {
    void (*call)(void * data);
    void * data;
} KCDispatchCB;

kern_return_t dispatch_initialize();
void dispatch_finalize();

errno_t dispatch_push(void (*call)(void * data), void * data);

#endif
