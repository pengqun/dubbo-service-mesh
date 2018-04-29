#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
//#include <curl/curl.h>

#ifdef MICRO_HTTP
#include <microhttpd.h>
#endif

#ifdef PROFILER
#include <gperftools/profiler.h>
#endif

//#define DEBUG_THIS_FILE
#include "log.h"

#include "etcd.h"
#include "util.h"
#include "ae.h"
#include "anet.h"

#endif
