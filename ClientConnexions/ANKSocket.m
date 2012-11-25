//
//  ANKSocket.m
//  KernelConnexions
//
//  Created by Alex Nichol on 11/25/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#import "ANKSocket.h"

@implementation ANKSocket

- (id)initWithHost:(NSString *)host port:(uint16_t)port {
    if ((self = [super init])) {
        struct hostent * ent = gethostbyname([host UTF8String]);
        if (!ent) return nil;
        fd = ksocket_init();
        if (fd < 0) return nil;
        if (ent->h_addrtype == AF_INET) {
            if (ksocket_connect_ipv4(fd, ent->h_addr_list[0], port) != 0) {
                close(fd);
                return nil;
            }
        } else if (ent->h_addrtype == AF_INET6) {
            if (ksocket_connect_ipv6(fd, ent->h_addr_list[0], port) != 0) {
                close(fd);
                return nil;
            }
        } else {
            close(fd);
            return nil;
        }
        backlog = [[NSMutableData alloc] init];
    }
    return self;
}

- (NSData *)read:(NSUInteger)max {
    if ([backlog length] > 0) {
        if ([backlog length] > max) {
            NSData * subData = [NSData dataWithBytes:[backlog bytes] length:max];
            [backlog replaceBytesInRange:NSMakeRange(0, max) withBytes:NULL length:0];
            return subData;
        } else {
            NSData * d = backlog;
            backlog = [NSMutableData data];
            return d;
        }
    }
    void * buff = 0;
    int len = ksocket_read(fd, &buff);
    if (len <= 0) {
        close(fd);
        return nil;
    }
    [backlog appendBytes:buff length:len];
    free(buff);
    return [self read:max];
}

- (BOOL)write:(NSData *)data {
    if (ksocket_send(fd, [data bytes], (int)[data length])) {
        close(fd);
        return NO;
    }
    return YES;
}

- (void)close {
    ksocket_close(fd);
}

- (void)dealloc {
    ksocket_close(fd);
}

@end
