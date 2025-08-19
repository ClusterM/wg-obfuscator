/* mini_argp.h
 * Tiny callback-based CLI parser */

#ifndef _MINI_ARGP_H_
#define _MINI_ARGP_H_
#include <stdio.h>
#include <string.h>

typedef int (*mini_argp_cb)(const char *lname, char sname,
                            const char *val, void *ctx);

typedef struct {
    const char  *long_name;   /* "--target"  or NULL */
    char         short_name;  /* 't' or 0            */
    int          has_arg;     /* 0 flag, 1 needs val */
    mini_argp_cb cb;          /* your handler        */
} mini_argp_opt;

/* internal: find desc by name */
static const mini_argp_opt *
margp_find(const mini_argp_opt *o, char *lname, char sname)
{
    for (; o->long_name || o->short_name; o++)
        if ((lname && o->long_name && !strcmp(lname,o->long_name)) ||
            (sname && o->short_name==sname))
            return o;
    return NULL;
}

static int mini_argp_parse(int argc, char **argv,
                           const mini_argp_opt *opts,
                           void *ctx,
                           mini_argp_cb cb)
{
    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];

        /* long option: --foo or --foo=bar */
        if (!strncmp(arg, "--", 2)) {
            char *name = arg + 2, *val = NULL;
            if ((val = strchr(name, '='))) { *val = 0; ++val; }

            const mini_argp_opt *o = margp_find(opts, name, 0);
            if (!o) { fprintf(stderr,"unknown --%s\n", name); return -1; }
            if (o->has_arg && !val) {
                if (++i == argc) { fprintf(stderr,"--%s needs value\n",name);return -1; }
                val = argv[i];
            }
            if (!o->has_arg) val = NULL;
            mini_argp_cb to_call = o->cb ? o->cb : cb;
            if (to_call(o->long_name, o->short_name, val, ctx)) return -1;
        }

        /* short(s):  -a, -fVAL, -xzvf */
        else if (arg[0]=='-' && arg[1]) {
            for (size_t pos=1; arg[pos]; ++pos) {
                char c = arg[pos];
                const mini_argp_opt *o = margp_find(opts,NULL,c);
                if (!o){ fprintf(stderr,"unknown -%c\n",c); return -1;}
                const char *val = NULL;

                if (o->has_arg) {
                    if (arg[pos+1])          val = &arg[pos+1];    /* -fVAL */
                    else if (++i < argc)     val = argv[i];        /* -f VAL */
                    else { fprintf(stderr,"-%c needs value\n",c); return -1;}
                    /* Stop further char-scanning: the rest of the arg is the value */
                    pos = strlen(arg)-1;
                }
                mini_argp_cb to_call = o->cb ? o->cb : cb;
                if (to_call(o->long_name, o->short_name, val, ctx)) return -1;
            }
        }

        /* positional not allowed */
        else {
            fprintf(stderr,"unexpected arg: %s\n",arg); return -1;
        }
    }
    return 0;
}

#endif/* _MINI_ARGP_H_ */
