#include <sys/resource.h>
#include <sys/prctl.h>
#include <execinfo.h>
#include "common.h"
#include "log.h"

/*
 * Dump the current stack trace using glibc's backtrace().
 * Output memory addresses can be translated by 'addr2line'.
 */
void dump_stack()
{
    int i, nptrs;
    void *buffer[100];
    char **strings;

    nptrs = backtrace(buffer, 100);

    strings = backtrace_symbols(buffer, nptrs);
    if (strings == NULL) {
        log_msg(ERR, "backtrace_symbols error");
        exit(-1);
    }

    log_msg(INFO, "Backtrace for process");

    for (i = 0; i < nptrs; i++) {
        log_msg(INFO, "  %s", strings[i]);
    }

    free(strings);
}

/*
 * Set the coredump size of current process.
 */
void core_dump_config()
{
    char *dump_size_str;
    int unlimited = 1;
    rlim_t max_dump = 0;

    /* Linux specific core dump configuration; set dumpable flag if needed. */
    int dumpable = prctl(PR_GET_DUMPABLE, 0, 0, 0, 0);
    if (dumpable == -1) {
        log_msg(WARN, "can't get core dump configuration.");
    } else if (unlimited || max_dump > 0) {
        /* Try to enbale core dump for this process. */
        if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1) {
            log_msg(ERR, "unable to make this process dumpable.");
            return;
        }
        log_msg(DEBUG, "process is dumpable.");
    }

    struct rlimit lim, new_lim;

    if (getrlimit(RLIMIT_CORE, &lim) == -1) {
        log_msg(WARN, "can't read core dump limit.");
        return;
    }

    if (lim.rlim_max == RLIM_INFINITY && lim.rlim_cur == RLIM_INFINITY) {
        /* already unlimited */
        log_msg(INFO, "Core dump size is unlimited.");
        return;
    } else { /* try set to unlimited */
        new_lim.rlim_max = RLIM_INFINITY;
        new_lim.rlim_cur = RLIM_INFINITY;
        if (setrlimit(RLIMIT_CORE, &new_lim) == 0) {
            log_msg(INFO, "Core dump size set to unlimited.");
            return;
        }
        if (errno == EPERM) { /* try increasing the soft limit to the hard limit instead. */
            if (lim.rlim_cur < lim.rlim_max) {
                log_msg(INFO, "lim.rlim_cur < lim.rlim_max");
                return;
            }
            new_lim.rlim_cur = lim.rlim_max;
            if (setrlimit(RLIMIT_CORE, &new_lim) == 0) {
                log_msg(INFO, "Could not set core dump size to unlimited;"
                              "set to the hard limit instead.");
                return;
            }
        }
        log_msg(ERR, "could not set core dump size to unlimited or hard limit.");
        return;
    }
}

/*
 * Init debug environment.
 */
void init_debug()
{
    //atomic_set(&dump_cnt, 1);
    core_dump_config();
}
