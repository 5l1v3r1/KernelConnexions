//
//  main.m
//  ClientConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "ANKSocket.h"

void logMessage(NSData * str);

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        /*int connection = openConnection(@"com.aqnichol.KernelConnexions");
        if (connection < 0) {
            fprintf(stderr, "failed to open socket.\n");
            return 1;
        }
        char buff[512];
        printf("now connected to the kernel; press enter to send packet\n");
        fgets(buff, 512, stdin);
        char data[] = {1, 0, 6, 0x55, 0x46, 198, 74, 59, 95};
        write(connection, data, 9);
        printf("now sent data; press enter to send super data\n");
        char superdata[] = {0x5, 0, 3, 'h', 'i', '\n'};
        fgets(buff, 512, stdin);
        write(connection, superdata, 6);
        printf("now sent superduper data. press enter to close\n");
        fgets(buff, 512, stdin);
        close(connection);*/
        
        ANKSocket * socket = [[ANKSocket alloc] initWithHost:@"198.74.59.95" port:0x5546];
        if (!socket) {
            NSLog(@"failed to connect.");
            return 1;
        }
        logMessage([socket read:512]);
        if (![socket write:[@"hello, there\n" dataUsingEncoding:NSASCIIStringEncoding]]) {
            NSLog(@"failed to write data.");
            return 1;
        }
        logMessage([socket read:512]);
        [socket close];
    }
    return 0;
}

void logMessage(NSData * str) {
    NSString * s = [[NSString alloc] initWithData:str encoding:NSASCIIStringEncoding];
    NSLog(@"got message: %@", s);
}
