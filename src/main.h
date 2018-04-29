#ifndef __MAIN_H__
#define __MAIN_H__

#include <getopt.h>
#include <signal.h>

#ifdef MICRO_HTTP
#include <microhttpd.h>
#endif

#ifdef PROFILER
#include <gperftools/profiler.h>
#endif

#include "common.h"
#include "etcd.h"
#include "util.h"
#include "ae.h"
#include "anet.h"

#endif
