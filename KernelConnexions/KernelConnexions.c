//
//  KernelConnexions.c
//  KernelConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#include <mach/mach_types.h>

kern_return_t KernelConnexions_start(kmod_info_t * ki, void *d);
kern_return_t KernelConnexions_stop(kmod_info_t *ki, void *d);

kern_return_t KernelConnexions_start(kmod_info_t * ki, void *d)
{
    return KERN_SUCCESS;
}

kern_return_t KernelConnexions_stop(kmod_info_t *ki, void *d)
{
    return KERN_SUCCESS;
}
