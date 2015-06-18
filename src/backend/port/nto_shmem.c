/*-------------------------------------------------------------------------
 *
 * nto_shmem.c
 *	  Implement shared memory using QNX Neutrino facilities
 *
 * These routines represent a fairly thin layer on top of NTO shared
 * memory functionality.
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/port/nto_shmem.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <sys/file.h>
#include <sys/mman.h>
#ifdef HAVE_SYS_SHM_H
#include <sys/shm.h>
#endif

#include "miscadmin.h"
#include "portability/mem.h"
#include "storage/ipc.h"
#include "storage/pg_shmem.h"

void *UsedShmemSegAddr = NULL;
int UsedShmemSegSize = 0;

static void PGSharedMemoryDelete(int status, Datum dname);

static int
errcode_for_dynamic_shared_memory()
{
	if (errno == EFBIG || errno == ENOMEM)
		return errcode(ERRCODE_OUT_OF_MEMORY);
	else
		return errcode_for_file_access();
}

static void
GenerateSHMName(char *name)
{
	int chars = 0;
	char *dd;

	strcpy(name, "/PostgreSQL.");
	if (name == NULL)
		elog(FATAL, "could not allocate memory for shared memory name");

	for (dd = DataDir; *dd; ++dd) {
		if (*dd == '/')
			chars = 0;
		else if (chars++ < 2)
			strncat(name, dd, 1);
	}
}

/*
 * PGSharedMemoryIsInUse
 *
 * Is a previously-existing shmem segment still existing and in use?

 * The point of this exercise is to detect the case where a prior postmaster
 * crashed, but it left child backends that are still running.  Therefore
 * we only care about shmem segments that are associated with the intended
 * DataDir.  This is an important consideration since accidental matches of
 * shmem segment IDs are reasonably common.
 */
bool
PGSharedMemoryIsInUse(unsigned long id1, unsigned long id2)
{
	int fd;
	char name[64];
	GenerateSHMName(name);

	fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd == -1)
		return true;

	close(fd);
	shm_unlink(name);

	return false;
}

/*
 * PGSharedMemoryCreate
 *
 * Create a shared memory segment of the given size and initialize its
 * standard header.  Also, register an on_shmem_exit callback to release
 * the storage.
 *
 * Dead Postgres segments are recycled if found, but we do not fail upon
 * collision with non-Postgres shmem segments.  The idea here is to detect and
 * re-use keys that may have been assigned by a crashed postmaster or backend.
 *
 * makePrivate means to always create a new segment, rather than attach to
 * or recycle any existing segment.
 */
PGShmemHeader *
PGSharedMemoryCreate(Size size, bool makePrivate, int port, PGShmemHeader **shim)
{
	char name[64];
	void *address;
	PGShmemHeader *hdr;
	int fd;
	struct stat st;

#if defined(EXEC_BACKEND) || !defined(MAP_HUGETLB)
	if (huge_pages == HUGE_PAGES_ON)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("huge pages not supported on this platform")));
#endif

	/* Room for a header? */
	Assert(size > MAXALIGN(sizeof(PGShmemHeader)));

	GenerateSHMName(name);

	UsedShmemSegAddr = NULL;

	fd = shm_open(name, O_RDWR | O_CREAT, 0600);
	if (fd == -1) {
		ereport(FATAL,
		    (errmsg("could not create shared memory segment: error code %d", errno)));
	}

	/* try to get segment size */
	if (fstat(fd, &st) != 0) {
		close(fd);
		ereport(FATAL, (errcode_for_dynamic_shared_memory(),
		    errmsg("could not stat shared memory segment \"%s\": %m", name)));
	}

	/* resize if needed */
	if (size != st.st_size && -1 == ftruncate(fd, size)) {
		close(fd);
		ereport(FATAL,
		        (errmsg("could not resize shared memory segment \"%s\": %m", name)));
	}

	address = mmap(NULL, size,
	    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_HASSEMAPHORE | MAP_NOSYNC,
	    fd, 0);

	if (address == MAP_FAILED) {
		close(fd);
		ereport(FATAL, (errcode_for_dynamic_shared_memory(),
		    errmsg("could not map shared memory segment \"%s\": %m", name)));
	}

	hdr = (PGShmemHeader *) address;
	hdr->magic = PGShmemMagic;
	hdr->creatorPID = getpid();
	hdr->totalsize = size;
	hdr->freeoffset = MAXALIGN(sizeof(PGShmemHeader));
	hdr->dsm_control = 0;

	UsedShmemSegAddr = address;
	UsedShmemSegSize = size;

	close(fd);

	on_shmem_exit(PGSharedMemoryDelete, PointerGetDatum(strdup(name)));

	return *shim = hdr;
}

/*
 * PGSharedMemoryDetach
 *
 * Detach from the shared memory segment, if still attached.  This is not
 * intended for use by the process that originally created the segment
 * (it will have an on_shmem_exit callback registered to do that).  Rather,
 * this is for subprocesses that have inherited an attachment and want to
 * get rid of it.
 */
void
PGSharedMemoryDetach(void)
{
	char name[64];

	GenerateSHMName(name);

	if (UsedShmemSegAddr != NULL &&
	    -1 == munmap(UsedShmemSegAddr, UsedShmemSegSize))
	{
		ereport(FATAL, (errcode_for_dynamic_shared_memory(),
		        errmsg("could not unmap shared memory segment \"%s\": %m", name)));
	}

	UsedShmemSegAddr = NULL;
	UsedShmemSegSize = NULL;
}

static void
PGSharedMemoryDelete(int status, Datum dname)
{
	char *name = DatumGetPointer(dname);

	PGSharedMemoryDetach();
	if (-1 == shm_unlink(name)) {
		ereport(FATAL, (errcode_for_dynamic_shared_memory(),
		        errmsg("could not remove shared memory segment \"%s\": %m", name)));
	}

	free(name);
}

