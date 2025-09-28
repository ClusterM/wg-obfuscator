#ifndef _MASKING_STUN_H_
#define _MASKING_STUN_H_

#include <stdint.h>
#include <netinet/in.h>
#include "wg-obfuscator.h"

static const uint8_t COOKIE_BE[4] = {0x21,0x12,0xA4,0x42};
#define STUN_TYPE_DATA_IND      0x0115
#define STUN_BINDING_REQ        0x0001
#define STUN_BINDING_RESP       0x0101
#define STUN_ATTR_XORMAPPED     0x0020
#define STUN_ATTR_SOFTWARE      0x8022
#define STUN_ATTR_FINGERPR      0x8028
#define STUN_ATTR_DATA          0x0013

extern masking_handler_t stun_masking_handler;

#endif // _MASKING_STUN_H_
