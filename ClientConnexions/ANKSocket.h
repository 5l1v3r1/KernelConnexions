//
//  ANKSocket.h
//  KernelConnexions
//
//  Created by Alex Nichol on 11/25/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#import <Foundation/Foundation.h>
#include <netdb.h>
#include <netinet/in.h>
#include "ksockets.h"

@interface ANKSocket : NSObject {
    int fd;
    NSMutableData * backlog;
}

- (id)initWithHost:(NSString *)host port:(uint16_t)port;
- (NSData *)read:(NSUInteger)max;
- (BOOL)write:(NSData *)data;
- (void)close;

@end
