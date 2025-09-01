#ifndef _MASKING_HANDLERS_H_
#define _MASKING_HANDLERS_H_

#include "masking.h"

/* List of available masking handlers */
#include "masking_stun.h"

static masking_handler_t * const masking_handlers[] = {
    &stun_masking_handler,
    NULL
};

#endif