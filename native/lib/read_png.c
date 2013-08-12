/* Based on a gist by Morten Nobel
 * https://gist.github.com/mortennobel/5299151 
 */
#include "read_png.h"

#include <stdlib.h>
#include <stdio.h>
#include <png.h>

bool read_png(char *name, int row_align, int *outStride, int *outWidth, int *outHeight, uint32_t **outData) 
{
    png_structp png_ptr;
    png_infop info_ptr;
    unsigned int sig_read = 0;
    int color_type, interlace_type;
    FILE *fp;
    
    if ((fp = fopen(name, "rb")) == NULL)
        return false;
    
    /* Create and initialize the png_struct
     * with the desired error handler
     * functions.  If you want to use the
     * default stderr and longjump method,
     * you can supply NULL for the last
     * three parameters.  We also supply the
     * the compiler header file version, so
     * that we know if the application
     * was compiled with a compatible version
     * of the library.  REQUIRED
     */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                     NULL, NULL, NULL);
    
    if (png_ptr == NULL) {
        fclose(fp);
        return false;
    }
    
    /* Allocate/initialize the memory
     * for image information.  REQUIRED. */
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        fclose(fp);
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return false;
    }
    
    /* Set error handling if you are
     * using the setjmp/longjmp method
     * (this is the normal method of
     * doing things with libpng).
     * REQUIRED unless you  set up
     * your own error handlers in
     * the png_create_read_struct()
     * earlier.
     */
    if (setjmp(png_jmpbuf(png_ptr))) {
        /* Free all of the memory associated
         * with the png_ptr and info_ptr */
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        /* If we get here, we had a
         * problem reading the file */
        return false;
    }
    
    /* Set up the output control if
     * you are using standard C streams */
    png_init_io(png_ptr, fp);
    
    /* If we have already
     * read some of the signature */
    png_set_sig_bytes(png_ptr, sig_read);
    
    /*
     * If you have enough memory to read
     * in the entire image at once, and
     * you need to specify only
     * transforms that can be controlled
     * with one of the PNG_TRANSFORM_*
     * bits (this presently excludes
     * dithering, filling, setting
     * background, and doing gamma
     * adjustment), then you can read the
     * entire image (including pixels)
     * into the info structure with this
     * call
     *
     * PNG_TRANSFORM_STRIP_16 |
     * PNG_TRANSFORM_PACKING  forces 8 bit
     * PNG_TRANSFORM_EXPAND forces to
     *  expand a palette into RGB
     */
    png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_PACKING | PNG_TRANSFORM_EXPAND, NULL);
    
    png_uint_32 width, height, stride;
    int bit_depth;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
                 &interlace_type, NULL, NULL);
    
    unsigned int channels = png_get_channels(png_ptr, info_ptr);
    uint32_t *data = NULL;
    
    png_bytepp row_pointers = png_get_rows(png_ptr, info_ptr);

    stride = (width * 4 + row_align - 1) & ~(row_align-1);
    data = malloc(stride * height);
  
    switch(channels)
    {
    case 3: /* Convert bytewise RGB to A8R8G8B8 */
        for (int i = 0; i < height; i++) 
        {
            for(int j = 0; j < width; j++) 
            {
                data[(stride/4) * i + j] = 
                    0xff000000 |
                    (row_pointers[i][j*3 + 0] << 16) |
                    (row_pointers[i][j*3 + 1] << 8) |
                    (row_pointers[i][j*3 + 2]);
            }
        }
        break;
    case 4: /* Convert bytewise RGBA to A8R8G8B8 */
        for (int i = 0; i < height; i++) 
        {
            for(int j = 0; j < width; j++) 
            {
                data[(stride/4) * i + j] = 
                    (row_pointers[i][j*3 + 3] << 24) |
                    (row_pointers[i][j*3 + 0] << 16) |
                    (row_pointers[i][j*3 + 1] << 8) |
                    (row_pointers[i][j*3 + 2]);
            }
        }
        break;
    default: /* Invalid number of channels */
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        free(data);
        return false;
    }
    
    /* Clean up after the read,
     * and free any memory allocated */
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    
    /* Close the file */
    fclose(fp);
    
    /* That's it */
    *outWidth = width;
    *outHeight = height;
    *outStride = stride;
    *outData = data;
    return true;
}

