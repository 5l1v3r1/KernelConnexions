//
//  general.h
//  KernelConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#ifndef KernelConnexions_general_h
#define KernelConnexions_general_h

#include <libkern/OSMalloc.h>
#include <sys/param.h> // gives NULL
#include "const.h"

void general_initialize();
void general_finalize();
OSMallocTag general_malloc_tag();

#endif
