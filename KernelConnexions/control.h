//
//  control.h
//  KernelConnexions
//
//  Created by Alex Nichol on 11/20/12.
//  Copyright (c) 2012 Alex Nichol. All rights reserved.
//

#ifndef KernelConnexions_control_h
#define KernelConnexions_control_h

#include <mach/mach_types.h>
#include <sys/types.h>
#include <sys/kern_control.h> // kernel control API
#include <sys/proc.h>
#include "general.h"
#include "debug.h"
#include "connection.h"
#include "dispatch.h"

// client packets
#define CONTROL_PACKET_CONNECT 0x1
#define CONTROL_PACKET_CLOSE 0x3
#define CONTROL_PACKET_SEND 0x5

// control packets
#define CONTROL_PACKET_CONNECTED 0x2
#define CONTROL_PACKET_ERROR 0x4
#define CONTROL_PACKET_DATA 0x6
#define CONTROL_PACKET_HUNGUP 0x8

typedef struct {
    uint32_t connection;
    char * buffer;
    uint32_t bufferSize;
    lck_mtx_t * lock;
    uint32_t identifier;
    uint32_t unit;
} KCControl;

typedef struct {
    char packetType;
    uint16_t length;
    char * data;
} KCControlPacket;

kern_return_t control_register();
kern_ctl_ref control_get();
kern_return_t control_unregister();

uint32_t kc_control_create(uint32_t unit);
errno_t kc_control_destroy(uint32_t identifier);

errno_t kc_control_append_data(uint32_t identifier, mbuf_t buffer);
errno_t kc_control_read_packet(uint32_t identifier, KCControlPacket ** packet); // maybe ENODATA
uint32_t kc_control_get_connection(uint32_t identifier);
uint32_t kc_control_get_unit(uint32_t identifier);

KCControlPacket * kc_control_packet_allocate(uint16_t length);
void kc_control_packet_free(KCControlPacket * packet);

#endif
