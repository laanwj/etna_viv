/*
 * Copyright (c) 2013 Wladimir J. van der Laan
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
#ifndef H_INTERVAL

#include <map>
#include <set>

template <typename T> struct Interval {
    /* [start,end) */
    T start;
    T end;

    Interval(T start, T end):
        start(start), end(end)
    {
    }

    bool operator<(const Interval &b) const
    {
        return start < b.start && end < b.end;
    }
    bool operator==(const Interval &b) const
    {
        // Ranges overlap
        return end > b.start && b.end > start;
    }
    bool empty() const
    {
        return start == end;
    }

    // Return merged (bounding) interval
    Interval<T> merge(const Interval<T> &o)
    {
        return Interval<T>(std::min(start, o.start), std::max(end, o.end));
    }
};

template <typename T,typename U> std::pair<typename T::iterator,typename T::iterator> intersecting_intervals(
        T &s, const Interval<U> &r)
{
    if(r.empty())
        return make_pair(s.end(), s.end());
    typename T::iterator i = s.lower_bound(Interval<U>(r.start,r.start+1));
    typename T::iterator j = s.upper_bound(Interval<U>(r.end-1,r.end));
    return make_pair(i,j);
}

template <typename T,typename U> std::pair<typename T::const_iterator,typename T::const_iterator> intersecting_intervals(
        const T &s, const Interval<U> &r)
{
    if(r.empty())
        return make_pair(s.end(), s.end());
    typename T::const_iterator i = s.lower_bound(Interval<U>(r.start,r.start+1));
    typename T::const_iterator j = s.upper_bound(Interval<U>(r.end-1,r.end));
    return make_pair(i,j);
}

template <typename T, typename U> void intervalset_merge(
        T &s, const Interval<U> &r)
{
    if(s.find(r) != s.end())
    {
        /* overlaps with at least one current interval */
        /* get intervals that overlap with r */
        std::pair<typename T::iterator, typename T::iterator> overlap = intersecting_intervals(s, r);
        Interval<U> span(overlap.first->start, overlap.second->end);
        s.erase(overlap.first, overlap.second);
        s.insert(span.merge(r));
    }
    else
    {
        /* completely new interval, just add it */
        s.insert(r);
    }
}

template <typename T, typename U> void intervalset_remove(
        T &s, const Interval<U> &r)
{
    if(s.find(r) != s.end())
    {
        /* overlaps with at least one current interval */
        /* remove intervals that are completely contained within the removed interval */
        /* may need to split up an interval if r divides it */

        /* get intervals that overlap with r */
        std::pair<typename T::iterator, typename T::iterator> overlap = intersecting_intervals(s, r);
        Interval<U> span(overlap.first->start, overlap.second->end);
        s.erase(overlap.first, overlap.second);
        s.insert(span.merge(r));
    }
    else
    {
        /* completely new interval nothing to do */
    }
}

#endif

