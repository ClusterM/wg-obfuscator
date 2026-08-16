#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "wg-obfuscator.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

// Log files may contain client addresses, so they are not world-readable
#define LOG_FILE_MODE 0640

// File descriptor of the log file, -1 when logging to stderr
static int log_fd = -1;
// Path of the log file, kept to be able to reopen it on SIGHUP
static char *log_path = NULL;
// 1 if every line must be prefixed with a timestamp
static int log_timestamps = 0;

/**
 * @brief Returns the single character representing a logging level.
 */
static char level_char(int level)
{
    switch (level) {
        case LL_ERROR: return 'E';
        case LL_WARN:  return 'W';
        case LL_INFO:  return 'I';
        case LL_DEBUG: return 'D';
        case LL_TRACE: return 'T';
        default:       return '?';
    }
}

/**
 * @brief Writes the whole buffer to the descriptor, retrying on partial writes.
 *
 * @param fd Descriptor to write to.
 * @param buf Data to write.
 * @param len Number of bytes to write.
 */
static void write_all(int fd, const char *buf, size_t len)
{
    while (len) {
        ssize_t written = write(fd, buf, len);
        if (written <= 0) {
            if (written < 0 && errno == EINTR) {
                continue;
            }
            // Nothing sensible can be done about a broken log, drop the rest of the line
            return;
        }
        buf += written;
        len -= (size_t)written;
    }
}

/**
 * @brief Appends formatted text to the buffer without ever overflowing it.
 *
 * @param buf Buffer to append to.
 * @param size Usable size of the buffer, including the terminating zero.
 * @param len Current length of the text in the buffer.
 * @param fmt Format string.
 * @param args Arguments for the format string.
 * @return New length of the text in the buffer.
 */
static size_t append_vfmt(char *buf, size_t size, size_t len, const char *fmt, va_list args)
{
    int n;

    if (len + 1 >= size) {
        return len;
    }
    n = vsnprintf(buf + len, size - len, fmt, args);
    if (n < 0) {
        return len;
    }
    if ((size_t)n >= size - len) {
        return size - 1; // truncated
    }
    return len + (size_t)n;
}

/**
 * @brief Appends formatted text to the buffer without ever overflowing it.
 *
 * @param buf Buffer to append to.
 * @param size Usable size of the buffer, including the terminating zero.
 * @param len Current length of the text in the buffer.
 * @param fmt Format string.
 * @return New length of the text in the buffer.
 */
static size_t append_fmt(char *buf, size_t size, size_t len, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    len = append_vfmt(buf, size, len, fmt, args);
    va_end(args);

    return len;
}

/**
 * @brief Writes the common line prefix (timestamp and instance name) to the buffer.
 *
 * @param buf Buffer to write to.
 * @param size Usable size of the buffer, including the terminating zero.
 * @param level Logging level of the line.
 * @return Length of the prefix.
 */
static size_t log_prefix(char *buf, size_t size, int level)
{
    size_t len = 0;

    if (log_timestamps) {
        struct timespec ts;
        struct tm *tm;
        if (clock_gettime(CLOCK_REALTIME, &ts) == 0 && (tm = localtime(&ts.tv_sec)) != NULL) {
            len = strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm);
            len = append_fmt(buf, size, len, ".%03ld ", (long)(ts.tv_nsec / 1000000));
        }
    }

    return append_fmt(buf, size, len, "[%s][%c] ", section_name, level_char(level));
}

/**
 * @brief Terminates the line and writes it out with a single write() call.
 *
 * A single write() to a descriptor opened with O_APPEND keeps lines of different
 * instances from interleaving when they share the same log file.
 *
 * @param buf Buffer holding the line, must have one spare byte for the newline.
 * @param len Length of the line.
 */
static void log_emit(char *buf, size_t len)
{
    buf[len++] = '\n';
    write_all(log_fd >= 0 ? log_fd : STDERR_FILENO, buf, len);
}

/**
 * @brief Writes a single log line.
 *
 * This is the implementation behind the log() macro, which is the one that
 * checks the verbosity level, so the line written here is unconditional.
 *
 * @param level Logging level of the line.
 * @param fmt Format string.
 */
void log_printf(int level, const char *fmt, ...)
{
    char buf[LOG_LINE_MAX];
    va_list args;
    size_t len = log_prefix(buf, sizeof(buf) - 1, level);

    va_start(args, fmt);
    len = append_vfmt(buf, sizeof(buf) - 1, len, fmt, args);
    va_end(args);

    log_emit(buf, len);
}

/**
 * @brief Writes a hexadecimal dump of a buffer, one or more prefixed lines.
 *
 * @param level Logging level of the dump.
 * @param prefix Text to put right after the line prefix, e.g. a direction marker.
 * @param data Data to dump.
 * @param length Number of bytes to dump.
 */
void log_hexdump(int level, const char *prefix, const uint8_t *data, int length)
{
    char buf[LOG_LINE_MAX];
    int i = 0;

    if (verbose < level) {
        return;
    }

    // Long dumps are split into several lines, each one with its own prefix
    do {
        size_t len = log_prefix(buf, sizeof(buf) - 1, level);
        len = append_fmt(buf, sizeof(buf) - 1, len, "%s", prefix);
        while ((i < length) && (len + 3 < sizeof(buf) - 1)) {
            len = append_fmt(buf, sizeof(buf) - 1, len, "%02X ", data[i++]);
        }
        log_emit(buf, len);
    } while (i < length);
}

/**
 * @brief Initializes logging.
 *
 * @param path Path of the log file or NULL to keep logging to stderr.
 * @param timestamps_mode 1 to force timestamps on, 0 to force them off,
 *                        negative to enable them only when logging to a file.
 */
void log_init(const char *path, int8_t timestamps_mode)
{
    log_timestamps = (timestamps_mode >= 0) ? (timestamps_mode != 0) : (path != NULL);

    if (!path) {
        return;
    }

    log_path = strdup(path);
    if (!log_path) {
        fprintf(stderr, "Out of memory while initializing the log file\n");
        exit(EXIT_FAILURE);
    }

    log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_BINARY, LOG_FILE_MODE);
    if (log_fd < 0) {
        // Logging into nowhere is worse than not starting at all
        fprintf(stderr, "Can't open log file '%s' - %s (%d)\n", path, strerror(errno), errno);
        exit(EXIT_FAILURE);
    }

    // Make every log file start with the version, no matter the verbosity level
    log_printf(LL_INFO, "Starting %s", version_string());
}

/**
 * @brief Reopens the log file, to be called after the log has been rotated.
 */
void log_reopen(void)
{
    int fd;

    if (!log_path) {
        return;
    }

    fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_BINARY, LOG_FILE_MODE);
    if (fd < 0) {
        // Keep the old descriptor, losing the log completely would be worse
        fprintf(stderr, "Can't reopen log file '%s' - %s (%d)\n", log_path, strerror(errno), errno);
        return;
    }
    if (log_fd >= 0) {
        close(log_fd);
    }
    log_fd = fd;

    log_printf(LL_INFO, "Log file reopened, %s", version_string());
}
