//
//  general.c
//  KernelConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#include "general.h"

static OSMallocTag mallocTag = NULL;

__private_extern__
void general_initialize() {
    mallocTag = OSMalloc_Tagalloc(kBundleID, OSMT_DEFAULT);
}

__private_extern__
void general_finalize() {
    OSMalloc_Tagfree(mallocTag);
}

__private_extern__
OSMallocTag general_malloc_tag() {
    return mallocTag;
}
