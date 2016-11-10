//
// Book:      OpenGL(R) ES 2.0 Programming Guide
// Authors:   Aaftab Munshi, Dan Ginsburg, Dave Shreiner
// ISBN-10:   0321502795
// ISBN-13:   9780321502797
// Publisher: Addison-Wesley Professional
// URLs:      http://safari.informit.com/9780321563835
//            http://www.opengles-book.com
//

//
/// \file ESUtil.h
/// \brief A utility library for OpenGL ES.  This library provides a
///        basic common framework for the example applications in the
///        OpenGL ES 2.0 Programming Guide.
//
#ifndef ESUTIL_H
#define ESUTIL_H

///
//  Includes
//
#include <stdint.h>

#ifdef __cplusplus

extern "C" {
#endif

///
//  Macros
//
#define ESUTIL_API
#define ESCALLBACK

//
/// \brief Log a message to the debug output for the platform
/// \param formatStr Format string for error log.  
//
void ESUTIL_API esLogMessage ( const char *formatStr, ... );

char* ESUTIL_API esLoadTGA ( char *fileName, int *width, int *height );

/* return current time (in seconds) */
double ESUTIL_API esNow(void);

#ifdef __cplusplus
}
#endif

#endif // ESUTIL_H
