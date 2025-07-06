#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "config.h"
#include "wg-obfuscator.h"
#include "mini_argp.h"

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
    { "static-bindings", 'b', 1 },
    { "verbose", 'v', 1 },
    { 0 }
};

static void show_usage(void)
{
    printf("Usage: %s [options]\n%s", arg0,
        "  -c, --config=<config_file> Read configuration from file (can be used instead\n"
        "                             of the rest arguments\n"
        "  -i, --source-if=<ip>       Source interface to listen on (optional, default -\n"
        "                             0.0.0.0, e.g. all\n"
        "  -p, --source-lport=<port>  Source port to listen\n"
        "  -t, --target=<ip>:<port>   Target IP and port\n"
        "  -k, --key=<key>            Obfuscation key (required, must be 1-255\n"
        "                             characters long)\n"
        "  -b, --static-bindings=<ip>:<port>:<port>,...\n"
        "                             Comma-separated static bindings for two-way mode\n"
        "                             as <client_ip>:<client_port>:<forward_port>\n"
        "  -v, --verbose=<0-4>        Verbosity level (optional, default - 2)\n"
        "                             0 - ERRORS (critical errors only)\n"
        "                             1 - WARNINGS (important messages: startup and\n"
        "                             shutdown messages)\n"
        "                             2 - INFO (informational messages: status messages,\n"
        "                             connection established, etc.)\n"
        "                             3 - DEBUG (detailed debug messages)\n"
        "                             4 - TRACE (very detailed debug messages, including\n"
        "                             packet dumps)\n"
        "  -?, --help                 Give this help list\n");
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
static void reset_config(struct obfuscator_config *config)
{
    memset(config, 0, sizeof(*config));
    verbose = LL_DEFAULT;
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
static void read_config_file(const char *filename, struct obfuscator_config *config)
{
    // Read configuration from the file
    uint8_t first_section = 1; // Flag to indicate if this is the first section being processed
    char line[256];

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
                if (fork() == 0) {
                    // Close in the child process
                    fclose(config_file);
                    // Stop config file processing for this instance
                    return;
                }
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
            log(LL_ERROR, "Configuration key '%s' does not accept a value", key);
            exit(EXIT_FAILURE);
        }
        parse_opt(o->long_name, o->short_name, value, config);
    }
    fclose(config_file);
}

/* Parse a single option. */
static int parse_opt(const char *lname, char sname, const char *val, void *ctx)
{
    struct obfuscator_config *config = (struct obfuscator_config *)ctx;

    switch (sname)
    {
        case '?':
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
            strncpy(config->static_bindings, val, sizeof(config->static_bindings) - 1);
            config->static_bindings[sizeof(config->static_bindings) - 1] = 0; // Ensure null-termination
            config->static_bindings_set = 1;
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
        case 'v':
            // TODO: parse verbosity level from string
            verbose = atoi(val);
            if (verbose < 0 || verbose > 4) {
                log(LL_ERROR, "Invalid verbosity level: %s (must be between 0 and 4)", val);
                exit(EXIT_FAILURE);
            }            
            break;
        default:
            // should never happen
            return -1;
    }
    return 0;
}

int parse_config(int argc, char **argv, struct obfuscator_config *config)
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
