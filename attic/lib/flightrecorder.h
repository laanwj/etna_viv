/*
 * Copyright (c) 2012-2013 Wladimir J. van der Laan
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef H_FLIGHTRECORDER
#define H_FLIGHTRECORDER
/* In-process "flight recorder" 

The idea is to log discrete events with their context.

- A list of memory ranges to monitor. At every event, these ranges
  will be checked for differences, and all differences will be logged.
  Memory ranges to monitor can be added and removed at any point. 

  There are two kinds of ranges:
  - Temporary: specific to this event, these will be stored (if modified) only for
    this event.
  - Persistent: checked for every event, for example mmapped areas

- The event itself consists of a list of ranges that are "up to date",
  a list of pointers to the root ranges (such as the command structure), and
  possibly metadata such as a sequence id and the current time.

Underlying data structure needs to be a representation of the entire memory range, over time,
initialized as "empty" (undefined).

Ie, start with empty range 0x00000000-0xFFFFFFFF
0x00004000 - 0x00008000 is updated for event 1
0x00006000 - 0x00008000 is updated for event 2
...

Need to quickly answer queries of the form "what are the last known values of range X-Y".
Then write ranges that changed.

Need a certain granularity to prevent overkill (lower than page-level, but higher than byte, maybe 16 bytes)

In-memory representation: 
    list of non-overlapping ranges [(A1-B1,ptr1),(A2-B2,ptr2)] where ptr1,ptr2 are pointers into the file.
    Ranges may need to be split when the middle is updated.

    Could just keep the current state of the memory in memory, that will make implementation easier (and faster)
    but would be kind of a memory leak over time (at least with no way to forget old ranges, maybe
      forget current memory contents on REMOVE_UPDATED_RANGE immediately?).

    Merge consecutive and overlapping ranges. Split ranges if the middle is removed.

    As the ranges are non-overlapping a binary search tree can be used.
    
Changed ranges are simply written to the end of the file.
  RANGE_DATA Addr_begin Addr_end <data>
  ADD_UPDATED_RANGE <A-B> (add range to ranges that are kept up-to-date for every event)
  REMOVE_UPDATED_RANGE <A-B> (remove range from ranges that are kept up-to-date for every event)
  EVENT <metadata>
    ROOT <metadata> <addr>
    ...
  COMMENT <data>   (allow inserting arbitrary markers into stream)

For metadata, just use a 64-bit tag for now containing the event type etc.
   event type: (mmap, munmap, open, close, ioctl)

API is thread-safe.

On-disk format is simple:

HEADER:
  magic        uint32  0x8e1aaa8f
  version      uint32  1

REPEATED:
  record_type  uint8   One of RTYPE_* (see flightrecorder.cpp)
  <data for record_type>

RTYPE_RANGE_DATA: Change in memory range
  start        size_t  Start address of range
  end          size_t  End address of range
  data         uint8[end-start]   Data of memory range

RTYPE_RANGE_TEMP_DATA: Temporary memory range (only for this event)
  start        size_t  Start address of range
  end          size_t  End address of range
  data         uint8[end-start]   Data of memory range

RTYPE_ADD_UPDATED_RANGE: Add persistent monitored range
  start        size_t  Start address of range
  end          size_t  End address of range

RTYPE_REMOVE_UPDATED_RANGE: Remove persistent monitored range
  start        size_t  Start address of range
  end          size_t  End address of range

RTYPE_EVENT:  Event
  event_type   short_string   Event type id
  num_parameters  uint32_t    Number of parameters
  REPEATED:
     name      short_string    Event parameter id
     value     size_t          Event parameter value

RTYPE_COMMENT:
  size         size_t  Size of comment
  comment      uint8[size]   Comment data

*/
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FDR_OK,
    FDR_ERROR,
    FDR_OVERLAP,
    FDR_NOT_FOUND
} flightrec_status;

typedef void * flightrec_t;
typedef void * flightrec_event_t;

flightrec_t fdr_open(const char *filename);
/**
 * Add a memory range to monitor. Changes to this range will be detected and logged
 * for every event.
 */
flightrec_status fdr_add_monitored_range(flightrec_t self, void *addr_start, size_t size);
/**
 * Remove a memory range previously added with fdr_add_monitored_range.
 */
flightrec_status fdr_remove_monitored_range(flightrec_t self, void *addr_start, size_t size);

/**
 * Start a new event.
 * @param[in] event_type  A short identifier of the event, of max 255 bytes
 * @returns  Event context, use this to add temporary memory ranges.
 */
flightrec_event_t fdr_new_event(flightrec_t self, const char *event_type);

/**
 * Add a parameter to the next event.
 */
flightrec_status fdr_event_add_parameter(flightrec_event_t self, const char *name, size_t value);

/**
 * Add a memory range just for the duration of the the next event.
 */
flightrec_status fdr_event_add_oneshot_range(flightrec_event_t self, void *addr_start, size_t size);

/**
 * Main entry point for logging an event.
 * Takes ownership of passed event context.
 */
flightrec_status fdr_log_event(flightrec_t self, flightrec_event_t context);

/**
 * Log a comment. Contents can be arbitrary binary data.
 */
flightrec_status fdr_log_comment(flightrec_t self, const char *data, size_t size);
/**
 * Close FDR stream.
 */
flightrec_status fdr_close(flightrec_t self);

#ifdef __cplusplus
}
#endif

#endif

