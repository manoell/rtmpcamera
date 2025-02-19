#include "rtmp_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

static FILE* log_file = NULL;
static const char* log_level_strings[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

void rtmp_log_init(const char* log_filepath) {
    log_file = fopen(log_filepath, "a");
    if (!log_file) {
        printf("Erro ao abrir arquivo de log: %s\n", log_filepath);
        return;
    }
    log_rtmp("=== Início da sessão de log ===");
}

void rtmp_log_cleanup(void) {
    if (log_file) {
        log_rtmp("=== Fim da sessão de log ===");
        fclose(log_file);
        log_file = NULL;
    }
}

void log_rtmp(const char* format, ...) {
    va_list args;
    time_t now;
    struct tm* timeinfo;
    char timestamp[26];

    time(&now);
    timeinfo = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    // Log para arquivo
    if (log_file) {
        fprintf(log_file, "[%s] ", timestamp);
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        fprintf(log_file, "\n");
        fflush(log_file);
    }

    // Log para console
    printf("[%s] ", timestamp);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

void log_rtmp_level(rtmp_log_level_t level, const char* format, ...) {
    va_list args;
    time_t now;
    struct tm* timeinfo;
    char timestamp[26];

    time(&now);
    timeinfo = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    // Log para arquivo
    if (log_file) {
        fprintf(log_file, "[%s][%s] ", timestamp, log_level_strings[level]);
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        fprintf(log_file, "\n");
        fflush(log_file);
    }

    // Log para console
    printf("[%s][%s] ", timestamp, log_level_strings[level]);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}