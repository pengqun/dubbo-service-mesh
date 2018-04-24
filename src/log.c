/*
 * Provide log facilty for iCare.
 */

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include "log.h"

/*
 * Log output destination. 
 */
#define LOG_TO_STDOUT      0
#define LOG_TO_STDERR      1
#define LOG_TO_LOGFILE     2
#define LOG_TO_DIRMAX      3

/*
 * Log option flags.
 */
#define LOG_OPT_TIME       1
#define LOG_OPT_POS        2
#define LOG_OPT_BAR        4

struct log_config {
    uint16_t log_flag; /* output switch */
    uint16_t log_opt;
};

static char *log_type_str[] = {
    "FATAL",
    "ERROR",
    "INFO", 
    "DEBUG",
    "WARN", 
};

static FILE * log_output_fps[LOG_TO_DIRMAX] = {NULL,NULL,NULL};

static struct log_config log_conf[LOG_LEVEL_MAX][LOG_TO_DIRMAX] = 
{
    /* FATAL*/
    {
        { 0, 0 },                                         // stdout
        { 1, LOG_OPT_TIME | LOG_OPT_POS | LOG_OPT_BAR },  // stderr
        { 1, LOG_OPT_TIME | LOG_OPT_POS | LOG_OPT_BAR },  // file
    }, 
    /* ERR*/
    {
        { 0, 0 },
        { 1, LOG_OPT_TIME | LOG_OPT_POS | LOG_OPT_BAR },
        { 1, LOG_OPT_TIME | LOG_OPT_POS | LOG_OPT_BAR },
    },
    /*INFO*/
    { 
        { 1, LOG_OPT_TIME },
        { 0, 0 },
        { 1, LOG_OPT_TIME },
    },
    /*DEBUG*/
    {
        { 1, LOG_OPT_TIME | LOG_OPT_BAR |LOG_OPT_POS },
        { 0, 0 },
        { 0, 0 },
    },
    /* WARN */
    {
        { 0, 0 },
        { 1, LOG_OPT_TIME | LOG_OPT_POS | LOG_OPT_BAR },
        { 1, LOG_OPT_TIME | LOG_OPT_POS | LOG_OPT_BAR },
    },
};

void init_log(char *logfile_name)
{
#if 0 /* use log rotate to manage log file instead */

    /* backup previous log file:  icare.log*/
    char command[1024];
    sprintf(command, "[ -f %s ] && cat %s >> %s.bak; > %s", logfile_name, 
            logfile_name, logfile_name, logfile_name);
    system(command);
#endif

    /*open the log file*/
    FILE *logfile = fopen(logfile_name, "a+");
    if (!logfile) {
        perror("open log file failed");
        exit(errno);
    }
    log_output_fps[LOG_TO_STDOUT]   =   stdout;
    log_output_fps[LOG_TO_STDERR]   =   stderr;
    log_output_fps[LOG_TO_LOGFILE]  =   logfile;

#if 0
    /* set log_start_time */
    struct tm local_time;
    time_t log_start_time = time(NULL);
    localtime_r(&log_start_time, &local_time);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%F %T", &local_time);
#endif

    fprintf(logfile, "\n****************************************");
    fprintf(logfile, "****************************************\n\n");
    fflush(logfile);
}

static inline void log_with_option(int32_t type, int32_t fpno, char *time, 
        const char *pos, const char *msg) 
{
    /* To avoid interleaving output from multiple threads. */
    flockfile(log_output_fps[fpno]);
    
    if (log_conf[type][fpno].log_opt & LOG_OPT_TIME) 
        fprintf(log_output_fps[fpno], "%s  ", time); 

    if (log_conf[type][fpno].log_opt & LOG_OPT_BAR)
        fprintf(log_output_fps[fpno], "[%s] ", log_type_str[type]);

    fprintf(log_output_fps[fpno], "%s ", msg); 

    if (log_conf[type][fpno].log_opt & LOG_OPT_POS) 
        fprintf(log_output_fps[fpno], "(%s)", pos); 

    fprintf(log_output_fps[fpno], "\n"); 

    fflush(log_output_fps[fpno]);

    funlockfile(log_output_fps[fpno]);

    return;
}

static inline void log_to_file(int32_t type, char *time, const char *pos,
        const char *msg) 
{
    if (log_conf[type][LOG_TO_STDOUT].log_flag)  
        log_with_option(type, LOG_TO_STDOUT, time, pos, msg); 

    if (log_conf[type][LOG_TO_STDERR].log_flag) 
        log_with_option(type, LOG_TO_STDERR, time, pos, msg); 

    if (log_conf[type][LOG_TO_LOGFILE].log_flag)
        log_with_option(type, LOG_TO_LOGFILE, time, pos, msg); 
}

void _log_msg(int32_t type, const char *file, int32_t  line, const char *format, ...)
{
    /* get time string */
    struct tm local_time;
    char time_str[32]   = {0};
    time_t now = time(NULL);
    localtime_r(&now, &local_time);
    strftime(time_str, sizeof(time_str), "%e %b %T", &local_time);

    /* get position string */
    char pos_str[32]  = {0};
    char line_str[16] = {0};
    const int32_t pos_max_size = 30;

    sprintf(line_str, "%i", line);
    int32_t diff = strlen(file) + strlen(line_str) + 1 - pos_max_size;
    if (diff > 0) 
        sprintf(pos_str, "%s:%i", file + diff, line);
    else 
        sprintf(pos_str, "%s:%i", file, line);

    /* put va_list into msg buffer */
    char log_msg[1024] = {0};
    va_list args;
    va_start(args, format);
    vsnprintf(log_msg, sizeof(log_msg), format, args);
    va_end(args);

    log_to_file(type, time_str, pos_str, log_msg);

    if (type == FATAL) {
        exit(errno);
    }
}
