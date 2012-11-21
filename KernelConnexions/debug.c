//
//  debug.c
//  KernelConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#include "debug.h"

__private_extern__
void debugf(const char * str, ...) {
    va_list list;
    va_start(list, str);
    char buff[128];
    vsnprintf(buff, 128, str, list);
    printf("["kBundleID"]: %s\n", buff);
    va_end(list);
}
