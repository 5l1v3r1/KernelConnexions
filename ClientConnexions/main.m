//
//  main.m
//  ClientConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#import <Foundation/Foundation.h>

#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <sys/socket.h>

int openConnection(NSString * bundleID);

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        int connection = openConnection(@"com.aqnichol.KernelConnexions");
        if (connection < 0) {
            fprintf(stderr, "failed to open socket.\n");
            return 1;
        }
        sleep(1);
        close(connection);
    }
    return 0;
}

int openConnection(NSString * bundleID) {
    const char * addrStr = [bundleID UTF8String];
    
    struct sockaddr_ctl addr;
    struct ctl_info info;
    bzero(&addr, sizeof(addr));
    bzero(&info, sizeof(info));
    
    addr.sc_len = sizeof(struct sockaddr_ctl);
    addr.sc_family = AF_SYSTEM;
    addr.ss_sysaddr = AF_SYS_CONTROL;
    
    strncpy(info.ctl_name, addrStr, sizeof(info.ctl_name));
    
    int fd = socket(PF_SYSTEM, SOCK_STREAM, SYSPROTO_CONTROL);
    if (fd < 0) return fd;
    
    if (ioctl(fd, CTLIOCGINFO, &info)) {
        return ENOENT;
    }
    
    addr.sc_id = info.ctl_id;
    addr.sc_unit = 0;
    
    int result;
    if ((result = connect(fd, (struct sockaddr *)&addr, sizeof(addr)))) {
        return result;
    }
    
    return fd;
}

