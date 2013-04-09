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
/* Implemented in C++ because I don't have the time to implement
 * all the data structures in C.
 */
#include "flightrecorder.h"
#include "interval.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

//#define DEBUG
#ifdef DEBUG
#include <stdio.h>
#endif

#include <vector>
#include <string>

const uint32_t FDR_MAGIC = 0x8e1aaa8f;
const uint32_t FDR_VERSION = 1;

typedef enum {
    RTYPE_RANGE_DATA = 0,
    RTYPE_RANGE_TEMP_DATA = 1,
    RTYPE_ADD_UPDATED_RANGE = 2,
    RTYPE_REMOVE_UPDATED_RANGE = 3,
    RTYPE_EVENT = 4,
    RTYPE_COMMENT = 5
} fdr_record_type;

#define ID_LEN (255+1)
typedef struct {
    char name[ID_LEN]; /* short id of max 255 bytes */
    size_t value;
} fdr_parameters;

/* can use a standard set/map, as long as we promise not to overlap intervals,
 * otherwise a structure such as an interval tree would be suitable
 */
typedef Interval<size_t> MemInterval;
typedef std::set<MemInterval> MemIntervalSet;
typedef std::map<MemInterval, void*> FlightRecStorage;
typedef std::vector<fdr_parameters> ParameterList;

inline void write_record_type(int fd, uint8_t record_type)
{
    (void)write(fd, &record_type, sizeof(uint8_t));
}

class Event {
    bool valid;
public:
    char event_type[ID_LEN];
    /* temporary ranges */
    MemIntervalSet temp;
    ParameterList parameters;

    Event(const char *event_type): valid(true) {
        strncpy(this->event_type, event_type, ID_LEN);
        this->event_type[ID_LEN-1] = 0;
    }
    ~Event() {}

    bool is_valid() const { return valid; }

    flightrec_status add_parameter(const char *name, size_t value)
    {
        fdr_parameters params = {};
        strncpy(params.name, name, ID_LEN);
        params.name[ID_LEN-1] = 0;
        params.value = value;
        parameters.push_back(params);
        return FDR_OK;
    }
    flightrec_status add_oneshot_range(size_t addr_start, size_t addr_end)
    {
        MemInterval r(addr_start, addr_end);
        flightrec_status ret = FDR_OK;

        std::pair<MemIntervalSet::iterator, bool> rv = temp.insert(MemInterval(addr_start, addr_end));
        if(!rv.second)
        {
            /* overlaps with already existing temporary range; bounce it */
            ret = FDR_OVERLAP;
        }
        return ret;
    }
};

class FlightRecorder {
    /* mutex protecting data structure */
    pthread_mutex_t mutex;
    /* stored data */
    FlightRecStorage stored;
    /* persistent ranges */
    MemIntervalSet persistent;
    /* file descriptor for output */
    int fd;
    bool valid;

    static const size_t diff_granularity = 256;

    /* check all monitored ranges, and log changes */
    void write_mem_range(size_t start, size_t end)
    {
        write_record_type(fd, RTYPE_RANGE_DATA);
        (void)write(fd, &start, sizeof(size_t));
        (void)write(fd, &end, sizeof(size_t));
        (void)write(fd, (void*)start, end - start);
    }

    /* Binary diff between two memory areas.
     * Log all found differences to output file. 
     */
    void bdiff(size_t from, size_t to, size_t size, size_t granularity)
    {
        size_t mismatch_start = 0;
        size_t mismatch_end = 0;
        bool in_difference = false;
#ifdef DEBUG
        //printf("bdiff %08x %08x %08x %08x\n", (uint32_t)from, (uint32_t)to, (uint32_t)size, (uint32_t)granularity);
#endif
        for(size_t ptr = 0; ptr < size; ptr += granularity)
        {
            size_t compsize = std::min(size-ptr, granularity);
            if(memcmp((void*)(from + ptr), (void*)(to + ptr), compsize) != 0)
            {
                //printf("mismatch %08x %08x %08x\n", (uint32_t)(from + ptr), (uint32_t)(to + ptr), (uint32_t)compsize);
                if(!in_difference) /* start new difference? */
                {
                    mismatch_start = ptr;
                    in_difference = true;
                }
                mismatch_end = ptr + compsize; /* extend end of differing area */
            }
            else /* Block matches */
            {
                if(in_difference) /* write previous difference to log */
                {
                    write_mem_range(to + mismatch_start, to + mismatch_end);
                    in_difference = false;
                }
            }
        }
        if(in_difference)
        {
            /* Write last block mismatch */
            write_mem_range(to + mismatch_start, to + mismatch_end);
        }
    }

    /* Perform binary difference on all monitored ranges */
    void check_monitored_ranges()
    {
        /* incremental update */
        for(MemIntervalSet::const_iterator i=persistent.begin(); i!=persistent.end(); ++i)
        {
            /* Check whether range is already stored */
            FlightRecStorage::iterator j = stored.find(*i);
            if(j == stored.end())
            {
                /* Not in storage at all, write whole block. */
                //printf("Not in storage at all, write whole block.\n");
                write_mem_range(i->start, i->end);
                /* Copy memory block to newly allocated storage,
                 * so that we can compare against it later. */
                void *block = malloc(i->end - i->start);
                memcpy(block, (void*)i->start, i->end - i->start);
#ifdef DEBUG
                printf("storing %p: %p %p\n", block, i->start, i->end);
#endif
                stored.insert(std::make_pair(*i, block));
            } else {
                if(j->first.start != i->start || j->first.end != i->end)
                {
                    /* Huh? Not exactly the same range, could do an intersection here but
                     * currently this is not supported, write the entire range */
                    //printf("Warning: stored block mismatch start1=%p start2=%p end1=%p end2=%p\n", j->first.start, i->start, j->first.end, i->end);
                    write_mem_range(i->start, i->end);
                } else {
                    /* compare ranges and write differences */
                    /* copy new values to storage */
                    bdiff((size_t)j->second, i->start, i->end - i->start, diff_granularity);
                    memcpy(j->second, (void*)i->start, i->end - i->start);
                }
            }
        }
    }
public:
    FlightRecorder(const char *name):
        fd(-1), valid(false)
    {
        if(pthread_mutex_init(&mutex, NULL))
            return;

        fd = open(name, O_CREAT|O_WRONLY|O_TRUNC, 0777);
        if(fd == -1)
        {
            pthread_mutex_destroy(&mutex);
            return;
        }
        if(write(fd, &FDR_MAGIC, sizeof(FDR_MAGIC)) < 0 ||
           write(fd, &FDR_VERSION, sizeof(FDR_VERSION)) < 0)
        {
            pthread_mutex_destroy(&mutex);
            close(fd);
            return;
        }

        valid = true;
        // Postcondition is either: fully valid object, or invalid object
        // with no initialized fields AT ALL.
    }

    ~FlightRecorder()
    {
        if(valid)
        {
            close(fd);
            pthread_mutex_destroy(&mutex);
            // Free comparison blocks in stored
            for(FlightRecStorage::iterator i=stored.begin(); i!=stored.end(); ++i)
            {
                free(i->second);
            }
        }
    }

    bool is_valid() const { return valid; }

    flightrec_status add_monitored_range(size_t addr_start, size_t addr_end)
    {
        pthread_mutex_lock(&mutex);
#ifdef DEBUG
        printf("-> add_monitored_range %p %p\n", addr_start, addr_end);
#endif
        std::pair<MemIntervalSet::iterator, bool> rv = persistent.insert(MemInterval(addr_start, addr_end));
        if(rv.second)
        {
            write_record_type(fd, RTYPE_ADD_UPDATED_RANGE);
            (void)write(fd, &addr_start, sizeof(size_t));
            (void)write(fd, &addr_end, sizeof(size_t));
        }
        pthread_mutex_unlock(&mutex);
        return rv.second ? FDR_OK : FDR_OVERLAP;
    }

    flightrec_status remove_monitored_range(size_t addr_start, size_t addr_end)
    {
        pthread_mutex_lock(&mutex);
        // XXX currently assumes a complete match, partial munmaps are not supported
        size_t ret = persistent.erase(MemInterval(addr_start, addr_end));
        if(ret)
        {
            write_record_type(fd, RTYPE_REMOVE_UPDATED_RANGE);
            (void)write(fd, &addr_start, sizeof(size_t));
            (void)write(fd, &addr_end, sizeof(size_t));
        }
        pthread_mutex_unlock(&mutex);
        return ret ? FDR_OK : FDR_NOT_FOUND;
    }

    flightrec_status log_event(Event *context)
    {
        /* count parameters */
        uint32_t num_parameters = context->parameters.size();
        
        pthread_mutex_lock(&mutex);
        /* log updates to monitored ranges */
        check_monitored_ranges();

        /* log temp ranges */
        for(MemIntervalSet::const_iterator i=context->temp.begin(); i!=context->temp.end(); ++i)
        {
            write_record_type(fd, RTYPE_RANGE_TEMP_DATA);
            (void)write(fd, &i->start, sizeof(size_t));
            (void)write(fd, &i->end, sizeof(size_t));
            (void)write(fd, (void*)i->start, i->end - i->start);
        }

        write_record_type(fd, RTYPE_EVENT);
        uint8_t name_len = strlen(context->event_type);
        (void)write(fd, &name_len, sizeof(uint8_t));
        (void)write(fd, context->event_type, name_len);

        /* log parameters */
        (void)write(fd, &num_parameters, sizeof(uint32_t));
        for(size_t i=0; i<num_parameters; ++i)
        {
            uint8_t name_len = strlen(context->parameters[i].name);
            (void)write(fd, &name_len, sizeof(uint8_t));
            (void)write(fd, context->parameters[i].name, name_len);
            (void)write(fd, &context->parameters[i].value, sizeof(size_t));
        }
        delete context;
        pthread_mutex_unlock(&mutex);
        return FDR_OK;
    }

    flightrec_status log_comment(const char *data, size_t size)
    {
        pthread_mutex_lock(&mutex);
        write_record_type(fd, RTYPE_COMMENT);
        (void)write(fd, &size, sizeof(size_t));
        (void)write(fd, data, size);
        pthread_mutex_unlock(&mutex);
        return FDR_OK;
    }
};

extern "C" {

flightrec_t fdr_open(const char *filename)
{
    FlightRecorder *rv = new FlightRecorder(filename);
    if(rv->is_valid()) {
        return (flightrec_t) rv;
    } else {
        delete rv;
        return NULL;
    }
}

flightrec_status fdr_add_monitored_range(flightrec_t self, void *addr_start, size_t size)
{
    if(self == NULL)
        return FDR_ERROR;
    return ((FlightRecorder*)self)->add_monitored_range((size_t)addr_start, ((size_t)addr_start)+size);
}

flightrec_status fdr_remove_monitored_range(flightrec_t self, void *addr_start, size_t size)
{
    if(self == NULL)
        return FDR_ERROR;
    return ((FlightRecorder*)self)->remove_monitored_range((size_t)addr_start, ((size_t)addr_start)+size);
}

flightrec_event_t fdr_new_event(flightrec_t self, const char *event_type)
{
    if(self == NULL)
        return NULL;
    Event *rv = new Event(event_type);
    if(rv->is_valid()) {
        return (flightrec_event_t) rv;
    } else {
        delete rv;
        return NULL;
    }
}

flightrec_status fdr_event_add_parameter(flightrec_event_t self, const char *name, size_t value)
{
    if(self == NULL)
        return FDR_ERROR;
    return ((Event*)self)->add_parameter(name, value);
}

flightrec_status fdr_event_add_oneshot_range(flightrec_event_t self, void *addr_start, size_t size)
{
    if(self == NULL)
        return FDR_ERROR;
    return ((Event*)self)->add_oneshot_range((size_t)addr_start, ((size_t)addr_start)+size);
}

flightrec_status fdr_log_event(flightrec_t self, flightrec_event_t context)
{
    if(self == NULL)
        return FDR_ERROR;
    return ((FlightRecorder*)self)->log_event((Event*)context);
}

flightrec_status fdr_log_comment(flightrec_t self, const char *data, size_t size)
{
    if(self == NULL)
        return FDR_ERROR;
    return ((FlightRecorder*)self)->log_comment(data, size);
}

flightrec_status fdr_close(flightrec_t self)
{
    delete (FlightRecorder*)self;
    return FDR_OK;
}

}

