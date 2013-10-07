#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "interval.h"

typedef std::set<Interval<size_t> > IntervalSet;

void print_set(const IntervalSet &s)
{
    for(IntervalSet::const_iterator i=s.begin(); i!=s.end(); ++i)
    {
        printf("%i-%i ", (int)i->start, (int)i->end);
    }
    printf("\n");
}

int main()
{
    IntervalSet s;
    s.insert(Interval<size_t>(1,10));
    s.insert(Interval<size_t>(10,20));
    s.insert(Interval<size_t>(20,30));
    s.insert(Interval<size_t>(30,40));

    print_set(s);

    printf("Matching intervals:\n");
    std::pair<IntervalSet::const_iterator,IntervalSet::const_iterator> ir = intersecting_intervals(s, Interval<size_t>(10,25));
    for(IntervalSet::const_iterator i=ir.first; i!=ir.second; ++i)
    {
        printf("%i-%i ", (int)i->start, (int)i->end);
    }
    printf("\n");

    intervalset_merge(s, Interval<size_t>(25, 50));
    print_set(s);
}

