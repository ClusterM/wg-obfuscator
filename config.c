#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include "config.h"
#include "wg-obfuscator.h"
#include "mini_argp.h"
#include "masking.h"

// Executable name
static const char *arg0;

/* The options we understand. */
static const mini_argp_opt options[] = {
    { "help", '?', 0 },
    { "config", 'c', 1 },
    { "source-if", 'i', 1 },
    { "source", 's', 1 },
    { "source-lport", 'p', 1 },
    { "target-if", 'o', 1 },
    { "target", 't', 1 },
    { "target-lport", 'r', 1 },
    { "key", 'k', 1 },
    { "masking", 'a', 1 },
    { "static-bindings", 'b', 1 },
    { "max-clients" , 'm', 1 },
    { "idle-timeout", 'l', 1 },
    { "in-timeout", 'n', 1 },
    { "max-dummy", 'd', 1 },
    { "fwmark", 'f', 1 },
    { "allow-clean", 'e', 0 },
    { "verbose", 'v', 1 },
    { "log-file", 'L', 1 },
    { "log-timestamps", 'T', 1 },
    { "resolve-interval", 'R', 1 },
    { 0 }
};

static void show_usage(void)
{
    printf("Usage: %s [options]\n%s", arg0,
        "  -?, --help                 Give this help list\n"
        "\n"
        "Main settings:\n"
        "  -c, --config=<config_file> Read configuration from file\n"
        "                             (can be used instead of the rest arguments)\n"
        "  -i, --source-if=<ip>       Source interface to listen on\n"
        "                             (optional, default - 0.0.0.0, e.g. all)\n"
        "  -p, --source-lport=<port>  Source port to listen\n"
        "  -t, --target=<ip>:<port>   Target IP and port\n"
        "  -k, --key=<key>            Obfuscation key \n"
        "                             (required, must be 1-255 characters long)\n"
        "  -a, --masking=<type>       Masking type (optional, default - AUTO)\n"
        "                             Supported values: STUN, AUTO, NONE\n"
        "  -b, --static-bindings=<ip>:<port>:<port>,...\n"
        "                             Comma-separated static bindings for two-way mode\n"
        "                             as <client_ip>:<client_port>:<forward_port>\n"
        "                             You can also repeat this option.\n"
        "  -f, --fwmark=<mark>        Firewall mark to set on all packets\n"
        "                             (optional, default - 0, e.g. disabled)\n"
        "  -v, --verbose=<level>      Verbosity level (optional, default - INFO)\n"
        "                             ERRORS (critical errors only)\n"
        "                             WARNINGS (important messages)\n"
        "                             2 - INFO (informational messages: status messages,\n"
        "                                      connection established, etc.)\n"
        "                             3 - DEBUG (detailed debug messages)\n"
        "                             4 - TRACE (very detailed debug messages, including\n"
        "                                       packet dumps)\n"
        "  -L, --log-file=<path>      Write the log to this file instead of stderr\n"
        "                             (optional, the file is reopened on SIGHUP)\n"
        "  -T, --log-timestamps=<val> Prefix log lines with a timestamp\n"
        "                             (optional, default - AUTO, e.g. only when\n"
        "                             writing to a log file)\n"
        "                             Supported values: AUTO, TRUE, FALSE\n"
        "\n"
        "Additional options:\n"
        "  -m, --max-clients=<number> Maximum number of clients (default: 1024)\n"
        "  -l, --idle-timeout=<sec>   Idle timeout in seconds (default: 300)\n"
        "  -n, --in-timeout=<sec>     Incoming timeout in seconds (default: 0 - disabled)\n"
        "  -d, --max-dummy=<bytes>    Maximum length of dummy bytes for data packets\n" 
        "                             (default: 4)\n"
        "  -e, --allow-clean          For servers, allow non-obfuscated incoming connections\n"
        "  -R, --resolve-interval=<sec>\n"
        "                             Re-resolve target and static-binding hostnames\n"
        "                             every N seconds (default: 0 - disabled).\n"
        "                             SIGHUP always triggers a refresh.\n"
        "                             If non-zero, a failed resolve at startup is retried\n"
        "                             instead of exiting.\n");
}

static int parse_opt(const char *lname, char sname, const char *val, void *ctx);

/**
 * @brief Resets the configuration structure to its default values.
 *
 * This function clears the configuration structure by setting all fields to zero
 * and resetting the verbosity level to the default value.
 *
 * @param config Pointer to the obfuscator_config structure to be reset.
 */
static void reset_config(obfuscator_config_t *config)
{
    if (config->static_bindings) {
        free(config->static_bindings);
    }
    memset(config, 0, sizeof(*config));
    config->max_clients = MAX_CLIENTS_DEFAULT;
    config->idle_timeout = IDLE_TIMEOUT_DEFAULT;
    config->in_timeout = IN_TIMEOUT_DEFAULT;
    config->max_dummy_length_data = MAX_DUMMY_LENGTH_DATA_DEFAULT;
    config->log_timestamps = -1; // auto
    verbose = LL_DEFAULT;
}

/**
 * Checks if the given string represents a valid integer.
 *
 * @param str Pointer to the null-terminated string to check.
 * @return Non-zero value if the string is a valid integer, 0 otherwise.
 */
static uint8_t is_integer(const char *str)
{
    if (!str || !*str) {
        return 0; // Empty string is not an integer
    }
    while (*str) {
        if (!isdigit((unsigned char)*str)) {
            return 0; // Non-digit character found
        }
        str++;
    }
    return 1; // All characters are digits
}

/**
 * Parses a boolean value from a string.
 *
 * @param str Pointer to the null-terminated string to parse.
 * @return 1 for true values ("true", "yes", "on", "1"),
 *         0 for false values ("false", "no", "off", "0"),
 *         -1 if the string is not a valid boolean value.
 */
static int parse_bool(const char *str)
{
    char lower[16];
    if (!str || !*str || strlen(str) >= sizeof(lower)) {
        return -1;
    }
    strcpy(lower, str);
    for (char *p = lower; *p; ++p) *p = tolower((unsigned char)*p);
    if (!strcmp(lower, "true") || !strcmp(lower, "yes") || !strcmp(lower, "on") || !strcmp(lower, "1")) {
        return 1;
    }
    if (!strcmp(lower, "false") || !strcmp(lower, "no") || !strcmp(lower, "off") || !strcmp(lower, "0")) {
        return 0;
    }
    return -1;
}

/**
 * @brief Reads and processes the configuration file.
 *
 * This function opens the specified configuration file and parses its contents
 * to initialize or update the application's configuration settings.
 *
 * @param filename The path to the configuration file to be read.
 * @param config Pointer to the obfuscator_config structure where the parsed settings will be stored.
 */
static void read_config_file(const char *filename, obfuscator_config_t *config)
{
    // Read configuration from the file
    uint8_t first_section = 1; // Flag to indicate if this is the first section being processed
    char line[10 * 1024]; // Buffer to hold each line from the config file

    FILE *config_file = fopen(filename, "r");
    if (config_file == NULL) {
        perror("Can't open config file");
        exit(EXIT_FAILURE);
    }

    while (fgets(line, sizeof(line), config_file)) {
        // Remove trailing newlines, carriage returns, spaces and tabs
        while (strlen(line) && (line[strlen(line) - 1] == '\n' || line[strlen(line) - 1] == '\r' 
            || line[strlen(line) - 1] == ' ' || line[strlen(line) - 1] == '\t')) {
            line[strlen(line) - 1] = 0;
        }
        // Remove leading spaces and tabs
        while (strlen(line) && (line[0] == ' ' || line[0] == '\t')) {
            memmove(line, line + 1, strlen(line));
        }
        // Ignore comments
        char *comment_index = strstr(line, "#");
        if (comment_index != NULL) {
            *comment_index = 0;
        }
        // Skip empty lines or with spaces only
        if (strspn(line, " \t\r\n") == strlen(line)) {
            continue;
        }

        // It can be new section
        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            if (!first_section) {
                // new config, need to fork the process
                pid_t pid = fork();
                if (pid < 0) {
                    log(LL_ERROR, "Can't fork a new instance - %s (%d)", strerror(errno), errno);
                    exit(EXIT_FAILURE);
                }
                if (pid == 0) {
                    // Close in the child process
                    fclose(config_file);
                    // Stop config file processing for this instance
                    return;
                }
                // Remember the instance to be able to forward signals to it
                register_child_instance(pid);
            }
            size_t len = strlen(line) - 2;
            if (len > sizeof(section_name) - 1) {
                len = sizeof(section_name) - 1;
            }
            strncpy(section_name, line + 1, len);
            section_name[len] = 0;

            // Reset all the parameters
            reset_config(config);

            first_section = 0; // We have processed the first section
            continue;
        }

        // Parse key-value pairs
        char *key = strtok(line, "=");
        key = trim(key);
        while (strlen(key) && (key[strlen(key) - 1] == ' ' || key[strlen(key) - 1] == '\t' || key[strlen(key) - 1] == '\r' || key[strlen(key) - 1] == '\n')) {
            key[strlen(key) - 1] = 0;
        }
        char *value = strtok(NULL, "=");
        if (value == NULL) {
            log(LL_ERROR, "Invalid configuration line: %s", line);
            exit(EXIT_FAILURE);
        }
        value = trim(value);
        if (!*value) {
            log(LL_ERROR, "Invalid configuration line: %s", line);
            exit(EXIT_FAILURE);
        }
        const mini_argp_opt *o = margp_find(options, key, 0);
        if (o == NULL) {
            log(LL_ERROR, "Unknown configuration key: %s", key);
            exit(EXIT_FAILURE);
        }
        if (!o->has_arg) {
            // Flag options are represented as booleans in the config file
            int b = parse_bool(value);
            if (b < 0) {
                log(LL_ERROR, "Configuration key '%s' accepts only boolean values (true/yes/on/1 or false/no/off/0)", key);
                exit(EXIT_FAILURE);
            }
            if (!b) {
                continue;
            }
            value = NULL;
        }
        parse_opt(o->long_name, o->short_name, value, config);
    }
    fclose(config_file);
}

/* Parse a single option. */
static int parse_opt(const char *lname, char sname, const char *val, void *ctx)
{
    obfuscator_config_t *config = (obfuscator_config_t *)ctx;
    char val_lower[16];

    switch (sname)
    {
        case '?':
            // Show usage and exit
            show_usage();
            exit(EXIT_SUCCESS);
        case 'c':
            read_config_file(val, config);
            break;
        case 'i':
            strncpy(config->client_interface, val, sizeof(config->client_interface) - 1);
            config->client_interface[sizeof(config->client_interface) - 1] = 0; // Ensure null-termination
            config->client_interface_set = 1;
            break;
        case 'p':
            if (!is_integer(val)) {
                log(LL_ERROR, "Invalid source port: %s (must be an integer)", val);
                exit(EXIT_FAILURE);
            }
            config->listen_port = atoi(val);
            if (config->listen_port <= 0 || config->listen_port > 65535) {
                log(LL_ERROR, "Invalid listen port: %s (must be between 1 and 65535)", val);
                exit(EXIT_FAILURE);
            }
            config->listen_port_set = 1;
            break;
        case 't':
            strncpy(config->forward_host_port, val, sizeof(config->forward_host_port) - 1);
            config->forward_host_port[sizeof(config->forward_host_port) - 1] = 0; // Ensure null-termination
            config->forward_host_port_set = 1;
            break;
        case 'b':
            {
                char *new_bindings;
                if (!config->static_bindings) {
                    new_bindings = strdup(val);
                } else {
                    size_t old_len = strlen(config->static_bindings);
                    new_bindings = realloc(config->static_bindings, old_len + strlen(val) + 2);
                    if (new_bindings) {
                        new_bindings[old_len] = ',';
                        strcpy(new_bindings + old_len + 1, val);
                    }
                }
                if (!new_bindings) {
                    log(LL_ERROR, "Out of memory while parsing static-bindings");
                    exit(EXIT_FAILURE);
                }
                config->static_bindings = new_bindings;
                config->static_bindings_set = 1;
            }
            break;
        case 'k':
            strncpy(config->xor_key, val, sizeof(config->xor_key));
            config->xor_key[sizeof(config->xor_key) - 1] = 0; // Ensure null-termination
            if (strlen(config->xor_key) == 0) {
                log(LL_ERROR, "XOR key cannot be empty");
                exit(EXIT_FAILURE);
            }
            config->xor_key_set = 1;
            break;
        case 'm':
            if (!is_integer(val)) {
                log(LL_ERROR, "Invalid maximum number of clients: %s (must be an integer)", val);
                exit(EXIT_FAILURE);
            }
            config->max_clients = atoi(val);
            if (config->max_clients <= 0) {
                log(LL_ERROR, "Invalid maximum number of clients: %s (must be greater than 0)", val);
                exit(EXIT_FAILURE);
            }
            break;
        case 'l':
            if (!is_integer(val)) {
                log(LL_ERROR, "Invalid idle timeout: %s (must be an integer)", val);
                exit(EXIT_FAILURE);
            }
            config->idle_timeout = atol(val);
            if (config->idle_timeout <= 0) {
                log(LL_ERROR, "Invalid idle timeout: %s (must be greater than 0)", val);
                exit(EXIT_FAILURE);
            }
            config->idle_timeout *= 1000; // Convert to milliseconds
            break;
        case 'n':
            if (!is_integer(val)) {
                log(LL_ERROR, "Invalid incoming timeout: %s (must be an integer)", val);
                exit(EXIT_FAILURE);
            }
            config->in_timeout = atol(val);
            if (config->in_timeout <= 0) {
                log(LL_ERROR, "Invalid incoming timeout: %s (must be greater than 0)", val);
                exit(EXIT_FAILURE);
            }
            config->in_timeout *= 1000; // Convert to milliseconds
            break;
        case 'd':
            if (!is_integer(val)) {
                log(LL_ERROR, "Invalid maximum dummy length for data packets: %s (must be an integer)", val);
                exit(EXIT_FAILURE);
            }
            config->max_dummy_length_data = atoi(val);
            if (config->max_dummy_length_data < 0 || config->max_dummy_length_data > MAX_DUMMY_LENGTH_TOTAL) {
                log(LL_ERROR, "Invalid maximum dummy length for data packets: %s (must be between 0 and %d)", val, MAX_DUMMY_LENGTH_TOTAL);
                exit(EXIT_FAILURE);
            }
            break;
        case 'e':
            config->allow_clean = 1;
            break;
        case 'f':
            // parse string with decimal and hexadecimal support
#ifdef __linux__
            {
                long int v = strtol(val, NULL, 0);
                if (v <= 0 || v > UINT16_MAX) {
                    log(LL_ERROR, "Invalid firewall mark: %s", val);
                    exit(EXIT_FAILURE);
                }
                config->fwmark = (uint16_t)v;
            }
#else
            log(LL_WARN, "Firewall mark is not supported on this platform");
#endif
            break;
        case 'a':
            {
                strncpy(val_lower, val, sizeof(val_lower) - 1);
                val_lower[sizeof(val_lower) - 1] = 0;
                for (char *p = val_lower; *p; ++p) *p = tolower((unsigned char)*p);
                if (strcmp(val_lower, "none") == 0) {
                    config->masking_handler = NULL;
                    config->masking_handler_set = 1;
                    break;
                }
                if (strcmp(val_lower, "auto") == 0) {
                    config->masking_handler = NULL;
                    config->masking_handler_set = 0;
                    break;
                }
                masking_handler_t *handler = get_masking_handler_by_name(val_lower);
                if (handler == NULL) {
                    log(LL_ERROR, "Unknown masking type: %s", val);
                    exit(EXIT_FAILURE);
                }
                config->masking_handler = handler;
                config->masking_handler_set = 1;
            }
            break;
        case 'v':
            strncpy(val_lower, val, sizeof(val_lower) - 1);
            val_lower[sizeof(val_lower) - 1] = 0;
            for (char *p = val_lower; *p; ++p) *p = tolower((unsigned char)*p);
            if (strcmp(val_lower, "error") == 0) {
                verbose = LL_ERROR;
            } else if (strcmp(val_lower, "warn") == 0) {
                verbose = LL_WARN;
            } else if (strcmp(val_lower, "info") == 0) {
                verbose = LL_INFO;
            } else if (strcmp(val_lower, "debug") == 0) {
                verbose = LL_DEBUG;
            } else if (strcmp(val_lower, "trace") == 0) {
                verbose = LL_TRACE;
            } else {
                // check if it's a number
                if (is_integer(val)) {
                    verbose = atoi(val);
                    if (verbose < 0 || verbose > 4) {
                        log(LL_ERROR, "Invalid verbosity level: %s (must be one of 'ERROR', 'WARN', 'INFO', 'DEBUG', 'TRACE')", val);
                        exit(EXIT_FAILURE);
                    }
                } else {
                    log(LL_ERROR, "Invalid verbosity level: %s (must be one of 'ERROR', 'WARN', 'INFO', 'DEBUG', 'TRACE')", val);
                    exit(EXIT_FAILURE);
                }            
            }
            break;
        case 'L':
            strncpy(config->log_file, val, sizeof(config->log_file) - 1);
            config->log_file[sizeof(config->log_file) - 1] = 0; // Ensure null-termination
            if (strlen(config->log_file) == 0) {
                log(LL_ERROR, "Log file path cannot be empty");
                exit(EXIT_FAILURE);
            }
            config->log_file_set = 1;
            break;
        case 'T':
            {
                strncpy(val_lower, val, sizeof(val_lower) - 1);
                val_lower[sizeof(val_lower) - 1] = 0;
                for (char *p = val_lower; *p; ++p) *p = tolower((unsigned char)*p);
                if (strcmp(val_lower, "auto") == 0) {
                    config->log_timestamps = -1;
                    break;
                }
                int b = parse_bool(val_lower);
                if (b < 0) {
                    log(LL_ERROR, "Invalid log timestamps mode: %s (must be one of 'AUTO', 'TRUE', 'FALSE')", val);
                    exit(EXIT_FAILURE);
                }
                config->log_timestamps = b;
            }
            break;
        case 'R':
            if (!is_integer(val)) {
                log(LL_ERROR, "Invalid resolve interval: %s (must be an integer)", val);
                exit(EXIT_FAILURE);
            }
            config->resolve_interval = atol(val);
            if (config->resolve_interval < 0) {
                log(LL_ERROR, "Invalid resolve interval: %s (must be 0 or greater)", val);
                exit(EXIT_FAILURE);
            }
            config->resolve_interval *= 1000; // Convert to milliseconds
            break;
        default:
            // should never happen
            return -1;
    }
    return 0;
}

int parse_config(int argc, char **argv, obfuscator_config_t *config)
{
    /* Parse command line arguments */
    reset_config(config);
    arg0 = argv[0]; // Save the executable name
    if (argc == 1) {
        fprintf(stderr, "No arguments provided, use \"%s --help\" command for usage information\n", argv[0]);
        return -1;
    }
    if (mini_argp_parse(argc, argv, options, config, parse_opt) != 0) {
        fprintf(stderr, "Failed to parse command line arguments\n");
        return -1;
    }

    return 0;
}

/**
 * @brief Removes leading and trailing whitespace characters from the input string.
 *
 * This function modifies the input string in place by trimming any whitespace
 * characters (such as spaces, tabs, or newlines) from both the beginning and end.
 *
 * @param s Pointer to the null-terminated string to be trimmed.
 * @return Pointer to the trimmed string.
 */
char *trim(char *s) {
    char *end;
    // Trim leading spaces, tabs, carriage returns and newlines
    while (*s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
    if (!*s) return s;
    // Trim trailing spaces, tabs, carriage returns and newlines
    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) *end-- = 0;
    return s;
}
