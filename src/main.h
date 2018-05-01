#ifndef __MAIN_H__
#define __MAIN_H__

//#define  _GNU_SOURCE
//#include <sched.h>
#include <getopt.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/prctl.h>

#ifdef PROFILER
#include <gperftools/profiler.h>
#endif

#include "common.h"
#include "etcd.h"
#include "util.h"
#include "ae.h"
#include "anet.h"

#endif
