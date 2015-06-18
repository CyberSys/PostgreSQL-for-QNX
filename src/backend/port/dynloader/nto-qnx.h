/*-------------------------------------------------------------------------
 *
 * nto-qnx.h
 * port-specific prototypes for QNX Neutrino
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/backend/port/dynloader/nto-qnx.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PORT_PROTOS_H
#define PORT_PROTOS_H

#include <sys/types.h>
#include <nlist.h>
#include <sys/link.h>
#include <dlfcn.h>

#include "utils/dynamic_loader.h" /* pgrminclude ignore */

/*
 * In some older systems, the RTLD_NOW flag isn't defined and the mode
 * argument to dlopen must always be 1.  The RTLD_GLOBAL flag is wanted
 * if available, but it doesn't exist everywhere.
 * If it doesn't exist, set it to 0 so it has no effect.
 */
#ifndef RTLD_NOW
#define RTLD_NOW 1
#endif
#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0
#endif

#define pg_dlopen(f) NTO_derived_dlopen((f), RTLD_NOW | RTLD_GLOBAL)
#define pg_dlsym     NTO_derived_dlsym
#define pg_dlclose   NTO_derived_dlclose
#define pg_dlerror   NTO_derived_dlerror

char * NTO_derived_dlerror(void);
void * NTO_derived_dlopen(const char *filename, int num);
void * NTO_derived_dlsym(void *handle, const char *name);
void NTO_derived_dlclose(void *handle);

#endif   /* PORT_PROTOS_H */
