import re

COLOR_RE = re.compile('(\x1b\[(?:[0-9]+\;)*[0-9]+m)')
def pad_right(s, length, ch=' '):
    '''Pad text with possibly color escape sequences,
    will cut off the text if it is longer than the allowed length.'''
    y = COLOR_RE.split(s)
    ptr,idx,rv = (0,0,[])
    while ptr < length and idx < len(y):
        if idx > 0:
            rv.append(y[idx-1])
        rv.append(y[idx][0:length - ptr])
        ptr += len(rv[-1])
        idx += 2
    rv.append(ch * (length - ptr))
    return ''.join(rv)


