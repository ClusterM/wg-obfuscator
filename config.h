#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "wg-obfuscator.h"

char *trim(char *s);
int parse_config(int argc, char **argv, obfuscator_config_t *config);

#endif // _CONFIG_H_
