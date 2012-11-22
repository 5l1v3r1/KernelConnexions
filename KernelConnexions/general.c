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

__private_extern__
void * number_to_pointer(uint32_t num) {
    uint32_t ptr[4];
    bzero(ptr, sizeof(uint32_t) * 4);
    ptr[0] = num;
    return *((void **)ptr);
}

uint32_t pointer_to_number(void * pointer) {
    uint32_t * nums = (uint32_t *)&pointer;
    return nums[0];
}
