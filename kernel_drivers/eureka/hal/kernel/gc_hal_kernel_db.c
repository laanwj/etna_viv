/****************************************************************************
*
*    Copyright (C) 2005 - 2012 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/




#include "gc_hal_kernel_precomp.h"

#define _GC_OBJ_ZONE    gcvZONE_DATABASE

/*******************************************************************************
***** Private fuctions ********************************************************/

/*******************************************************************************
**  gckKERNEL_NewDatabase
**
**  Create a new database structure and insert it to the head of the hash list.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gctUINT32 ProcessID
**          ProcessID that identifies the database.
**
**  OUTPUT:
**
**      gcsDATABASE_PTR * Database
**          Pointer to a variable receiving the database structure pointer on
**          success.
*/
static gceSTATUS
gckKERNEL_NewDatabase(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID,
    OUT gcsDATABASE_PTR * Database
    )
{
    gceSTATUS status;
    gcsDATABASE_PTR database;
    gctBOOL acquired = gcvFALSE;
    gctSIZE_T slot;

    gcmkHEADER_ARG("Kernel=0x%x ProcessID=%d", Kernel, ProcessID);

    /* Acquire the database mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Kernel->os, Kernel->db->dbMutex, gcvINFINITE));
    acquired = gcvTRUE;

    if (Kernel->db->freeDatabase != gcvNULL)
    {
        /* Allocate a database from the free list. */
        database             = Kernel->db->freeDatabase;
        Kernel->db->freeDatabase = database->next;
    }
    else
    {
        gctPOINTER pointer = gcvNULL;

        /* Allocate a new database from the heap. */
        gcmkONERROR(gckOS_Allocate(Kernel->os,
                                   gcmSIZEOF(gcsDATABASE),
                                   &pointer));

        database = pointer;
    }

    /* Compute the hash for the database. */
    slot = ProcessID % gcmCOUNTOF(Kernel->db->db);

    /* Insert the database into the hash. */
    database->next   = Kernel->db->db[slot];
    Kernel->db->db[slot] = database;

    /* Save the hash slot. */
    database->slot = slot;

    /* Release the database mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));

    /* Return the database. */
    *Database = database;

    /* Success. */
    gcmkFOOTER_ARG("*Database=0x%x", *Database);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the database mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_FindDatabase
**
**  Find a database identified by a process ID and move it to the head of the
**  hash list.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gctUINT32 ProcessID
**          ProcessID that identifies the database.
**
**      gctBOOL LastProcessID
**          gcvTRUE if searching for the last known process ID.  gcvFALSE if
**          we need to search for the process ID specified by the ProcessID
**          argument.
**
**  OUTPUT:
**
**      gcsDATABASE_PTR * Database
**          Pointer to a variable receiving the database structure pointer on
**          success.
*/
static gceSTATUS
gckKERNEL_FindDatabase(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID,
    IN gctBOOL LastProcessID,
    OUT gcsDATABASE_PTR * Database
    )
{
    gceSTATUS status;
    gcsDATABASE_PTR database, previous;
    gctSIZE_T slot;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Kernel=0x%x ProcessID=%d LastProcessID=%d",
                   Kernel, ProcessID, LastProcessID);

    /* Compute the hash for the database. */
    slot = ProcessID % gcmCOUNTOF(Kernel->db->db);

    /* Acquire the database mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Kernel->os, Kernel->db->dbMutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Check whether we are getting the last known database. */
    if (LastProcessID)
    {
        /* Use last database. */
        database = Kernel->db->lastDatabase;

        if (database == gcvNULL)
        {
            /* Database not found. */
            gcmkONERROR(gcvSTATUS_INVALID_DATA);
        }
    }
    else
    {
        /* Walk the hash list. */
        for (previous = gcvNULL, database = Kernel->db->db[slot];
             database != gcvNULL;
             database = database->next)
        {
            if (database->processID == ProcessID)
            {
                /* Found it! */
                break;
            }

            previous = database;
        }

        if (database == gcvNULL)
        {
            /* Database not found. */
            gcmkONERROR(gcvSTATUS_INVALID_DATA);
        }

        if (previous != gcvNULL)
        {
            /* Move database to the head of the hash list. */
            previous->next   = database->next;
            database->next   = Kernel->db->db[slot];
            Kernel->db->db[slot] = database;
        }
    }

    /* Release the database mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));

    /* Return the database. */
    *Database = database;

    /* Success. */
    gcmkFOOTER_ARG("*Database=0x%x", *Database);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the database mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_DeleteDatabase
**
**  Remove a database from the hash list and delete its structure.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gcsDATABASE_PTR Database
**          Pointer to the database structure to remove.
**
**  OUTPUT:
**
**      Nothing.
*/
static gceSTATUS
gckKERNEL_DeleteDatabase(
    IN gckKERNEL Kernel,
    IN gcsDATABASE_PTR Database
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gcsDATABASE_PTR database;

    gcmkHEADER_ARG("Kernel=0x%x Database=0x%x", Kernel, Database);

    /* Acquire the database mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Kernel->os, Kernel->db->dbMutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Check slot value. */
    gcmkVERIFY_ARGUMENT(Database->slot < gcmCOUNTOF(Kernel->db->db));

    if (Database->slot < gcmCOUNTOF(Kernel->db->db))
    {
        /* Check if database if the head of the hash list. */
        if (Kernel->db->db[Database->slot] == Database)
        {
            /* Remove the database from the hash list. */
            Kernel->db->db[Database->slot] = Database->next;
        }
        else
        {
            /* Walk the has list to find the database. */
            for (database = Kernel->db->db[Database->slot];
                 database != gcvNULL;
                 database = database->next
            )
            {
                /* Check if the next list entry is this database. */
                if (database->next == Database)
                {
                    /* Remove the database from the hash list. */
                    database->next = Database->next;
                    break;
                }
            }

            if (database == gcvNULL)
            {
                /* Ouch!  Something got corrupted. */
                gcmkONERROR(gcvSTATUS_INVALID_DATA);
            }
        }
    }

    if (Kernel->db->lastDatabase != gcvNULL)
    {
        /* Insert database to the free list. */
        Kernel->db->lastDatabase->next = Kernel->db->freeDatabase;
        Kernel->db->freeDatabase       = Kernel->db->lastDatabase;
    }

    /* Keep database as the last database. */
    Kernel->db->lastDatabase = Database;

    /* Release the database mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the database mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_NewRecord
**
**  Create a new database record structure and insert it to the head of the
**  database.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gcsDATABASE_PTR Database
**          Pointer to a database structure.
**
**  OUTPUT:
**
**      gcsDATABASE_RECORD_PTR * Record
**          Pointer to a variable receiving the database record structure
**          pointer on success.
*/
static gceSTATUS
gckKERNEL_NewRecord(
    IN gckKERNEL Kernel,
    IN gcsDATABASE_PTR Database,
    OUT gcsDATABASE_RECORD_PTR * Record
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gcsDATABASE_RECORD_PTR record = gcvNULL;

    gcmkHEADER_ARG("Kernel=0x%x Database=0x%x", Kernel, Database);

    /* Acquire the database mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Kernel->os, Kernel->db->dbMutex, gcvINFINITE));
    acquired = gcvTRUE;

    if (Kernel->db->freeRecord != gcvNULL)
    {
        /* Allocate the record from the free list. */
        record             = Kernel->db->freeRecord;
        Kernel->db->freeRecord = record->next;
    }
    else
    {
        gctPOINTER pointer = gcvNULL;

        /* Allocate the record from the heap. */
        gcmkONERROR(gckOS_Allocate(Kernel->os,
                                   gcmSIZEOF(gcsDATABASE_RECORD),
                                   &pointer));

        record = pointer;
    }

    /* Insert the record in the database. */
    record->next   = Database->list;
    Database->list = record;

    /* Release the database mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));

    /* Return the record. */
    *Record = record;

    /* Success. */
    gcmkFOOTER_ARG("*Record=0x%x", *Record);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the database mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));
    }
    if (record != gcvNULL)
    {
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Kernel->os, record));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_DeleteRecord
**
**  Remove a database record from the database and delete its structure.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gcsDATABASE_PTR Database
**          Pointer to a database structure.
**
**      gceDATABASE_TYPE Type
**          Type of the record to remove.
**
**      gctPOINTER Data
**          Data of the record to remove.
**
**  OUTPUT:
**
**      gctSIZE_T_PTR Bytes
**          Pointer to a variable that receives the size of the record deleted.
**          Can be gcvNULL if the size is not required.
*/
static gceSTATUS
gckKERNEL_DeleteRecord(
    IN gckKERNEL Kernel,
    IN gcsDATABASE_PTR Database,
    IN gceDATABASE_TYPE Type,
    IN gctPOINTER Data,
    OUT gctSIZE_T_PTR Bytes OPTIONAL
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gcsDATABASE_RECORD_PTR record, previous;

    gcmkHEADER_ARG("Kernel=0x%x Database=0x%x Type=%d Data=0x%x",
                   Kernel, Database, Type, Data);

    /* Acquire the database mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Kernel->os, Kernel->db->dbMutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Scan the database for this record. */
    for (record = Database->list, previous = gcvNULL;
         record != gcvNULL;
         record = record->next
    )
    {
        if ((record->type == Type)
        &&  (record->data == Data)
        )
        {
            /* Found it! */
            break;
        }

        previous = record;
    }

    if (record == gcvNULL)
    {
        /* Ouch!  This record is not found? */
        gcmkONERROR(gcvSTATUS_INVALID_DATA);
    }

    if (Bytes != gcvNULL)
    {
        /* Return size of record. */
        *Bytes = record->bytes;
    }

    /* Remove record from database. */
    if (previous == gcvNULL)
    {
        Database->list = record->next;
    }
    else
    {
        previous->next = record->next;
    }

    /* Insert record in free list. */
    record->next       = Kernel->db->freeRecord;
    Kernel->db->freeRecord = record;

    /* Release the database mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu", gcmOPT_VALUE(Bytes));
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the database mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_FindRecord
**
**  Find a database record from the database.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gcsDATABASE_PTR Database
**          Pointer to a database structure.
**
**      gceDATABASE_TYPE Type
**          Type of the record to remove.
**
**      gctPOINTER Data
**          Data of the record to remove.
**
**  OUTPUT:
**
**      gctSIZE_T_PTR Bytes
**          Pointer to a variable that receives the size of the record deleted.
**          Can be gcvNULL if the size is not required.
*/
static gceSTATUS
gckKERNEL_FindRecord(
    IN gckKERNEL Kernel,
    IN gcsDATABASE_PTR Database,
    IN gceDATABASE_TYPE Type,
    IN gctPOINTER Data,
    OUT gcsDATABASE_RECORD_PTR Record
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gcsDATABASE_RECORD_PTR record;

    gcmkHEADER_ARG("Kernel=0x%x Database=0x%x Type=%d Data=0x%x",
                   Kernel, Database, Type, Data);

    /* Acquire the database mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(Kernel->os, Kernel->db->dbMutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Scan the database for this record. */
    for (record = Database->list;
         record != gcvNULL;
         record = record->next
    )
    {
        if ((record->type == Type)
        &&  (record->data == Data)
        )
        {
            /* Found it! */
            break;
        }
    }

    if (record == gcvNULL)
    {
        /* Ouch!  This record is not found? */
        gcmkONERROR(gcvSTATUS_INVALID_DATA);
    }

    if (Record != gcvNULL)
    {
        /* Return information of record. */
        gcmkONERROR(
            gckOS_MemCopy(Record, record, sizeof(gcsDATABASE_RECORD)));
    }

    /* Release the database mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));

    /* Success. */
    gcmkFOOTER_ARG("Record=0x%x", Record);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the database mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Kernel->os, Kernel->db->dbMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}


/*******************************************************************************
***** Public API **************************************************************/

/*******************************************************************************
**  gckKERNEL_CreateProcessDB
**
**  Create a new process database.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gctUINT32 ProcessID
**          Process ID used to identify the database.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_CreateProcessDB(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID
    )
{
    gceSTATUS status;
    gcsDATABASE_PTR database = gcvNULL;

    gcmkHEADER_ARG("Kernel=0x%x ProcessID=%d", Kernel, ProcessID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Create a new database. */
    gcmkONERROR(gckKERNEL_NewDatabase(Kernel, ProcessID, &database));

    /* Initialize the database. */
    database->processID             = ProcessID;
    database->vidMem.bytes          = 0;
    database->vidMem.maxBytes       = 0;
    database->vidMem.totalBytes     = 0;
    database->nonPaged.bytes        = 0;
    database->nonPaged.maxBytes     = 0;
    database->nonPaged.totalBytes   = 0;
    database->contiguous.bytes      = 0;
    database->contiguous.maxBytes   = 0;
    database->contiguous.totalBytes = 0;
    database->mapMemory.bytes          = 0;
    database->mapMemory.maxBytes       = 0;
    database->mapMemory.totalBytes     = 0;
    database->mapUserMemory.bytes      = 0;
    database->mapUserMemory.maxBytes   = 0;
    database->mapUserMemory.totalBytes = 0;
    database->list                  = gcvNULL;

#if gcdSECURE_USER
    {
        gctINT slot;
        gcskSECURE_CACHE * cache = &database->cache;

        /* Setup the linked list of cache nodes. */
        for (slot = 1; slot <= gcdSECURE_CACHE_SLOTS; ++slot)
        {
            cache->cache[slot].logical = gcvNULL;

#if gcdSECURE_CACHE_METHOD != gcdSECURE_CACHE_TABLE
            cache->cache[slot].prev = &cache->cache[slot - 1];
            cache->cache[slot].next = &cache->cache[slot + 1];
#   endif
#if gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_HASH
            cache->cache[slot].nextHash = gcvNULL;
            cache->cache[slot].prevHash = gcvNULL;
#   endif
        }

#if gcdSECURE_CACHE_METHOD != gcdSECURE_CACHE_TABLE
        /* Setup the head and tail of the cache. */
        cache->cache[0].next    = &cache->cache[1];
        cache->cache[0].prev    = &cache->cache[gcdSECURE_CACHE_SLOTS];
        cache->cache[0].logical = gcvNULL;

        /* Fix up the head and tail pointers. */
        cache->cache[0].next->prev = &cache->cache[0];
        cache->cache[0].prev->next = &cache->cache[0];
#   endif

#if gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_HASH
        /* Zero out the hash table. */
        for (slot = 0; slot < gcmCOUNTOF(cache->hash); ++slot)
        {
            cache->hash[slot].logical  = gcvNULL;
            cache->hash[slot].nextHash = gcvNULL;
        }
#   endif

        /* Initialize cache index. */
        cache->cacheIndex = gcvNULL;
        cache->cacheFree  = 1;
        cache->cacheStamp = 0;
    }
#endif

    /* Reset idle timer. */
    Kernel->db->lastIdle = 0;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_AddProcessDB
**
**  Add a record to a process database.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gctUINT32 ProcessID
**          Process ID used to identify the database.
**
**      gceDATABASE_TYPE TYPE
**          Type of the record to add.
**
**      gctPOINTER Pointer
**          Data of the record to add.
**
**      gctPHYS_ADDR Physical
**          Physical address of the record to add.
**
**      gctSIZE_T Size
**          Size of the record to add.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_AddProcessDB(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID,
    IN gceDATABASE_TYPE Type,
    IN gctPOINTER Pointer,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Size
    )
{
    gceSTATUS status;
    gcsDATABASE_PTR database;
    gcsDATABASE_RECORD_PTR record = gcvNULL;
    gcsDATABASE_COUNTERS * count;

    gcmkHEADER_ARG("Kernel=0x%x ProcessID=%d Type=%d Pointer=0x%x "
                   "Physical=0x%x Size=%lu",
                   Kernel, ProcessID, Type, Pointer, Physical, Size);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Special case the idle record. */
    if (Type == gcvDB_IDLE)
    {
        gctUINT64 time;

        /* Get the current profile time. */
        gcmkONERROR(gckOS_GetProfileTick(&time));

        if ((ProcessID == 0) && (Kernel->db->lastIdle != 0))
        {
            /* Out of idle, adjust time it was idle. */
            Kernel->db->idleTime += time - Kernel->db->lastIdle;
            Kernel->db->lastIdle  = 0;
        }
        else if (ProcessID == 1)
        {
            /* Save current idle time. */
            Kernel->db->lastIdle = time;
        }

#if gcdDYNAMIC_SPEED
        {
            /* Test for first call. */
            if (Kernel->db->lastSlowdown == 0)
            {
                /* Save milliseconds. */
                Kernel->db->lastSlowdown     = time;
                Kernel->db->lastSlowdownIdle = Kernel->db->idleTime;
            }
            else
            {
                /* Compute ellapsed time in milliseconds. */
                gctUINT delta = gckOS_ProfileToMS(time - Kernel->db->lastSlowdown);

                /* Test for end of period. */
                if (delta >= gcdDYNAMIC_SPEED)
                {
                    /* Compute number of idle milliseconds. */
                    gctUINT idle = gckOS_ProfileToMS(
                        Kernel->db->idleTime  - Kernel->db->lastSlowdownIdle);

                    /* Broadcast to slow down the GPU. */
                    gcmkONERROR(gckOS_BroadcastCalibrateSpeed(Kernel->os,
                                                              Kernel->hardware,
                                                              idle,
                                                              delta));

                    /* Save current time. */
                    Kernel->db->lastSlowdown     = time;
                    Kernel->db->lastSlowdownIdle = Kernel->db->idleTime;
                }
            }
        }
#endif

        /* Success. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);

    /* Find the database. */
    gcmkONERROR(gckKERNEL_FindDatabase(Kernel, ProcessID, gcvFALSE, &database));

    /* Create a new record in the database. */
    gcmkONERROR(gckKERNEL_NewRecord(Kernel, database, &record));

    /* Initialize the record. */
    record->kernel   = Kernel;
    record->type     = Type;
    record->data     = Pointer;
    record->physical = Physical;
    record->bytes    = Size;

    /* Get pointer to counters. */
    switch (Type)
    {
    case gcvDB_VIDEO_MEMORY:
        count = &database->vidMem;
        break;

    case gcvDB_NON_PAGED:
        count = &database->nonPaged;
        break;

    case gcvDB_CONTIGUOUS:
        count = &database->contiguous;
        break;

    case gcvDB_MAP_MEMORY:
        count = &database->mapMemory;
        break;

    case gcvDB_MAP_USER_MEMORY:
        count = &database->mapUserMemory;
        break;

    default:
        count = gcvNULL;
        break;
    }

    if (count != gcvNULL)
    {
        /* Adjust counters. */
        count->totalBytes += Size;
        count->bytes      += Size;

        if (count->bytes > count->maxBytes)
        {
            count->maxBytes = count->bytes;
        }
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_RemoveProcessDB
**
**  Remove a record from a process database.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gctUINT32 ProcessID
**          Process ID used to identify the database.
**
**      gceDATABASE_TYPE TYPE
**          Type of the record to remove.
**
**      gctPOINTER Pointer
**          Data of the record to remove.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_RemoveProcessDB(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID,
    IN gceDATABASE_TYPE Type,
    IN gctPOINTER Pointer
    )
{
    gceSTATUS status;
    gcsDATABASE_PTR database;
    gctSIZE_T bytes = 0;

    gcmkHEADER_ARG("Kernel=0x%x ProcessID=%d Type=%d Pointer=0x%x",
                   Kernel, ProcessID, Type, Pointer);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);

    /* Find the database. */
    gcmkONERROR(gckKERNEL_FindDatabase(Kernel, ProcessID, gcvFALSE, &database));

    /* Delete the record. */
    gcmkONERROR(
        gckKERNEL_DeleteRecord(Kernel, database, Type, Pointer, &bytes));

    /* Update counters. */
    switch (Type)
    {
    case gcvDB_VIDEO_MEMORY:
        database->vidMem.bytes -= bytes;
        break;

    case gcvDB_NON_PAGED:
        database->nonPaged.bytes -= bytes;
        break;

    case gcvDB_CONTIGUOUS:
        database->contiguous.bytes -= bytes;
        break;

    case gcvDB_MAP_MEMORY:
        database->mapMemory.bytes -= bytes;
        break;

    case gcvDB_MAP_USER_MEMORY:
        database->mapUserMemory.bytes -= bytes;
        break;

    default:
        break;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_FindProcessDB
**
**  Find a record from a process database.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gctUINT32 ProcessID
**          Process ID used to identify the database.
**
**      gceDATABASE_TYPE TYPE
**          Type of the record to remove.
**
**      gctPOINTER Pointer
**          Data of the record to remove.
**
**  OUTPUT:
**
**      gcsDATABASE_RECORD_PTR Record
**          Copy of record.
*/
gceSTATUS
gckKERNEL_FindProcessDB(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID,
    IN gctUINT32 ThreadID,
    IN gceDATABASE_TYPE Type,
    IN gctPOINTER Pointer,
    OUT gcsDATABASE_RECORD_PTR Record
    )
{
    gceSTATUS status;
    gcsDATABASE_PTR database;

    gcmkHEADER_ARG("Kernel=0x%x ProcessID=%d Type=%d Pointer=0x%x",
                   Kernel, ProcessID, ThreadID, Type, Pointer);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);

    /* Find the database. */
    gcmkONERROR(gckKERNEL_FindDatabase(Kernel, ProcessID, gcvFALSE, &database));

    /* Find the record. */
    gcmkONERROR(
        gckKERNEL_FindRecord(Kernel, database, Type, Pointer, Record));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_DestroyProcessDB
**
**  Destroy a process database.  If the database contains any records, the data
**  inside those records will be deleted as well.  This aids in the cleanup if
**  a process has died unexpectedly or has memory leaks.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gctUINT32 ProcessID
**          Process ID used to identify the database.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_DestroyProcessDB(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID
    )
{
    gceSTATUS status;
    gcsDATABASE_PTR database;
    gcsDATABASE_RECORD_PTR record, next;
    gctBOOL asynchronous;

    gcmkHEADER_ARG("Kernel=0x%x ProcessID=%d", Kernel, ProcessID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Find the database. */
    gcmkONERROR(gckKERNEL_FindDatabase(Kernel, ProcessID, gcvFALSE, &database));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DATABASE,
                   "DB(%d): VidMem: total=%lu max=%lu",
                   ProcessID, database->vidMem.totalBytes,
                   database->vidMem.maxBytes);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DATABASE,
                   "DB(%d): NonPaged: total=%lu max=%lu",
                   ProcessID, database->nonPaged.totalBytes,
                   database->nonPaged.maxBytes);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DATABASE,
                   "DB(%d): Contiguous: total=%lu max=%lu",
                   ProcessID, database->contiguous.totalBytes,
                   database->contiguous.maxBytes);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DATABASE,
                   "DB(%d): Idle time=%llu",
                   ProcessID, Kernel->db->idleTime);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DATABASE,
                   "DB(%d): Map: total=%lu max=%lu",
                   ProcessID, database->mapMemory.totalBytes,
                   database->mapMemory.maxBytes);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DATABASE,
                   "DB(%d): Map: total=%lu max=%lu",
                   ProcessID, database->mapUserMemory.totalBytes,
                   database->mapUserMemory.maxBytes);

    if (database->list != gcvNULL)
    {
        gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DATABASE,
                       "Process %d has entries in its database:",
                       ProcessID);
    }

    /* Walk all records. */
    for (record = database->list; record != gcvNULL; record = next)
    {
        /* Next next record. */
        next = record->next;

        /* Dispatch on record type. */
        switch (record->type)
        {
        case gcvDB_VIDEO_MEMORY:
            /* Free the video memory. */
//####modified for marvell-bg2
#if USE_GALOIS_SHM
            status = gckVIDMEM_PreFree(record->data);
#endif
//####end for marvell-bg2
            status = gckVIDMEM_Free(record->data);

            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DATABASE,
                           "DB: VIDEO_MEMORY 0x%x (status=%d)",
                           record->data, status);
            break;

        case gcvDB_NON_PAGED:
            /* Free the non paged memory. */
            status = gckOS_FreeNonPagedMemory(Kernel->os,
                                              record->bytes,
                                              record->physical,
                                              record->data);

            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DATABASE,
                           "DB: NON_PAGED 0x%x, bytes=%lu (status=%d)",
                           record->data, record->bytes, status);
            break;

        case gcvDB_CONTIGUOUS:
            /* Free the contiguous memory. */
            status = gckOS_FreeContiguous(Kernel->os,
                                          record->physical,
                                          record->data,
                                          record->bytes);

            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DATABASE,
                           "DB: CONTIGUOUS 0x%x bytes=%lu (status=%d)",
                           record->data, record->bytes, status);
            break;

        case gcvDB_SIGNAL:
#if USE_NEW_LINUX_SIGNAL
            status = gcvSTATUS_NOT_SUPPORTED;
#else
            /* Free the user signal. */
            status = gckOS_DestroyUserSignal(Kernel->os,
                                             gcmPTR2INT(record->data));
#endif /* USE_NEW_LINUX_SIGNAL */

            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DATABASE,
                           "DB: SIGNAL %d (status=%d)",
                           (gctINT) record->data, status);
            break;

        case gcvDB_VIDEO_MEMORY_LOCKED:
            /* Unlock what we still locked */
            status = gckVIDMEM_Unlock(record->kernel,
                                      record->data,
                                      gcvSURF_TYPE_UNKNOWN,
                                      &asynchronous);

            if (gcmIS_SUCCESS(status) && (gcvTRUE == asynchronous))
            {
                /* TODO: we maybe need to schedule a event here */
                status = gckVIDMEM_Unlock(record->kernel,
                                          record->data,
                                          gcvSURF_TYPE_UNKNOWN,
                                          gcvNULL);
            }

            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DATABASE,
                           "DB: VIDEO_MEMORY_LOCKED 0x%x (status=%d)",
                           record->data, status);
            break;

        case gcvDB_CONTEXT:
            /* TODO: Free the context */
            status = gckCOMMAND_Detach(Kernel->command, record->data);

            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DATABASE,
                           "DB: CONTEXT 0x%x (status=%d)",
                           record->data, status);
            break;

        case gcvDB_MAP_MEMORY:
            /* Unmap memory. */
            status = gckKERNEL_UnmapMemory(Kernel,
                                           record->physical,
                                           record->bytes,
                                           record->data);

            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DATABASE,
                           "DB: MAP MEMORY %d (status=%d)",
                           gcmPTR2INT(record->data), status);
            break;

        case gcvDB_MAP_USER_MEMORY:
            /* TODO: Unmap user memory. */
            status = gckOS_UnmapUserMemoryEx(Kernel->os,
                                             Kernel->core,
                                             record->data,
                                             record->bytes,
                                             record->physical,
                                             0);

            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DATABASE,
                           "DB: MAP USER MEMORY %d (status=%d)",
                           gcmPTR2INT(record->data), status);
            break;

                    case gcvDB_SHARED_INFO:
                        status = gckOS_FreeMemory(Kernel->os, record->physical);
                        break;

        default:
            gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DATABASE,
                           "DB: Correcupted record=0x%08x type=%d",
                           record, record->type);
            break;
        }

        /* Delete the record. */
        gcmkONERROR(gckKERNEL_DeleteRecord(Kernel,
                                           database,
                                           record->type,
                                           record->data,
                                           gcvNULL));
    }

    /* Delete the database. */
    gcmkONERROR(gckKERNEL_DeleteDatabase(Kernel, database));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_QueryProcessDB
**
**  Query a process database for the current usage of a particular record type.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gctUINT32 ProcessID
**          Process ID used to identify the database.
**
**      gctBOOL LastProcessID
**          gcvTRUE if searching for the last known process ID.  gcvFALSE if
**          we need to search for the process ID specified by the ProcessID
**          argument.
**
**      gceDATABASE_TYPE Type
**          Type of the record to query.
**
**  OUTPUT:
**
**      gcuDATABASE_INFO * Info
**          Pointer to a variable that receives the requested information.
*/
gceSTATUS
gckKERNEL_QueryProcessDB(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID,
    IN gctBOOL LastProcessID,
    IN gceDATABASE_TYPE Type,
    OUT gcuDATABASE_INFO * Info
    )
{
    gceSTATUS status;
    gcsDATABASE_PTR database;

    gcmkHEADER_ARG("Kernel=0x%x ProcessID=%d Type=%d Info=0x%x",
                   Kernel, ProcessID, Type, Info);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Info != gcvNULL);

    /* Find the database. */
    gcmkONERROR(
        gckKERNEL_FindDatabase(Kernel, ProcessID, LastProcessID, &database));

    /* Get pointer to counters. */
    switch (Type)
    {
    case gcvDB_VIDEO_MEMORY:
        gcmkONERROR(gckOS_MemCopy(&Info->counters,
                                  &database->vidMem,
                                  gcmSIZEOF(database->vidMem)));
        break;

    case gcvDB_NON_PAGED:
        gcmkONERROR(gckOS_MemCopy(&Info->counters,
                                  &database->nonPaged,
                                  gcmSIZEOF(database->vidMem)));
        break;

    case gcvDB_CONTIGUOUS:
        gcmkONERROR(gckOS_MemCopy(&Info->counters,
                                  &database->contiguous,
                                  gcmSIZEOF(database->vidMem)));
        break;

    case gcvDB_IDLE:
        Info->time           = Kernel->db->idleTime;
        Kernel->db->idleTime = 0;
        break;

    case gcvDB_MAP_MEMORY:
        gcmkONERROR(gckOS_MemCopy(&Info->counters,
                                  &database->mapMemory,
                                  gcmSIZEOF(database->mapMemory)));
        break;

    case gcvDB_MAP_USER_MEMORY:
        gcmkONERROR(gckOS_MemCopy(&Info->counters,
                                  &database->mapUserMemory,
                                  gcmSIZEOF(database->mapUserMemory)));
        break;

    default:
        break;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdSECURE_USER
/*******************************************************************************
**  gckKERNEL_GetProcessDBCache
**
**  Get teh secure cache from a process database.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to a gckKERNEL object.
**
**      gctUINT32 ProcessID
**          Process ID used to identify the database.
**
**  OUTPUT:
**
**      gcskSECURE_CACHE_PTR * Cache
**          Pointer to a variable that receives the secure cache pointer.
*/
gceSTATUS
gckKERNEL_GetProcessDBCache(
    IN gckKERNEL Kernel,
    IN gctUINT32 ProcessID,
    OUT gcskSECURE_CACHE_PTR * Cache
    )
{
    gceSTATUS status;
    gcsDATABASE_PTR database;

    gcmkHEADER_ARG("Kernel=0x%x ProcessID=%d", Kernel, ProcessID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Cache != gcvNULL);

    /* Find the database. */
    gcmkONERROR(gckKERNEL_FindDatabase(Kernel, ProcessID, gcvFALSE, &database));

    /* Return the pointer to the cache. */
    *Cache = &database->cache;

    /* Success. */
    gcmkFOOTER_ARG("*Cache=0x%x", *Cache);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif
