/*******************************************************************************
# Linux-UVC streaming input-plugin for MJPG-streamer                           #
#                                                                              #
# This package work with the Logitech UVC based webcams with the mjpeg feature #
#                                                                              #
#   Orginally Copyright (C) 2005 2006 Laurent Pinchart &&  Michel Xhaard       #
#   Modifications Copyright (C) 2006  Gabriel A. Devenyi                       #
#   Modifications Copyright (C) 2007  Tom Stöveken                             #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; either version 2 of the License, or            #
# (at your option) any later version.                                          #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/

#include <stdio.h>
#include <jpeglib.h>
#include <stdlib.h>
#include <string.h>

#include "jpeg_utils.h"
#include "../../mjpg_streamer.h"

#define OUTPUT_BUF_SIZE  4096

typedef struct {
    struct jpeg_destination_mgr pub; /* public fields */

    JOCTET * buffer;    /* start of buffer */

    unsigned char *outbuffer;
    int outbuffer_size;
    unsigned char *outbuffer_cursor;
    int *written;

} mjpg_destination_mgr;

typedef mjpg_destination_mgr * mjpg_dest_ptr;

/******************************************************************************
Description.:
Input Value.:
Return Value:
******************************************************************************/
METHODDEF(void) init_destination(j_compress_ptr cinfo)
{
    mjpg_dest_ptr dest = (mjpg_dest_ptr) cinfo->dest;

    /* Allocate the output buffer --- it will be released when done with image */
    dest->buffer = (JOCTET *)(*cinfo->mem->alloc_small)((j_common_ptr) cinfo, JPOOL_IMAGE, OUTPUT_BUF_SIZE * sizeof(JOCTET));

    *(dest->written) = 0;

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;
}

/******************************************************************************
Description.: called whenever local jpeg buffer fills up
Input Value.:
Return Value:
******************************************************************************/
METHODDEF(boolean) empty_output_buffer(j_compress_ptr cinfo)
{
    mjpg_dest_ptr dest = (mjpg_dest_ptr) cinfo->dest;

    memcpy(dest->outbuffer_cursor, dest->buffer, OUTPUT_BUF_SIZE);
    dest->outbuffer_cursor += OUTPUT_BUF_SIZE;
    *(dest->written) += OUTPUT_BUF_SIZE;

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;

    return TRUE;
}

/******************************************************************************
Description.: called by jpeg_finish_compress after all data has been written.
              Usually needs to flush buffer.
Input Value.:
Return Value:
******************************************************************************/
METHODDEF(void) term_destination(j_compress_ptr cinfo)
{
    mjpg_dest_ptr dest = (mjpg_dest_ptr) cinfo->dest;
    size_t datacount = OUTPUT_BUF_SIZE - dest->pub.free_in_buffer;

    /* Write any data remaining in the buffer */
    memcpy(dest->outbuffer_cursor, dest->buffer, datacount);
    dest->outbuffer_cursor += datacount;
    *(dest->written) += datacount;
}

/******************************************************************************
Description.: Prepare for output to a stdio stream.
Input Value.: buffer is the already allocated buffer memory that will hold
              the compressed picture. "size" is the size in bytes.
Return Value: -
******************************************************************************/
GLOBAL(void) dest_buffer(j_compress_ptr cinfo, unsigned char *buffer, int size, int *written)
{
    mjpg_dest_ptr dest;

    if(cinfo->dest == NULL) {
        cinfo->dest = (struct jpeg_destination_mgr *)(*cinfo->mem->alloc_small)((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(mjpg_destination_mgr));
    }

    dest = (mjpg_dest_ptr) cinfo->dest;
    dest->pub.init_destination = init_destination;
    dest->pub.empty_output_buffer = empty_output_buffer;
    dest->pub.term_destination = term_destination;
    dest->outbuffer = buffer;
    dest->outbuffer_size = size;
    dest->outbuffer_cursor = buffer;
    dest->written = written;
}


void create_compress(struct jpeg_compress_struct *cinfo, struct jpeg_error_mgr *jerr) {
    cinfo->err = jpeg_std_error(jerr);
    jpeg_create_compress(cinfo);
}

void destroy_compress(struct jpeg_compress_struct *cinfo) {
    jpeg_destroy_compress(cinfo);
}

/******************************************************************************
Description.: yuv2jpeg function is based on compress_yuyv_to_jpeg written by
              Gabriel A. Devenyi.
              It uses the destination manager implemented above to compress
              YUYV data to JPEG. Most other implementations use the
              "jpeg_stdio_dest" from libjpeg, which can not store compressed
              pictures to memory instead of a file.
Input Value.: image structure from own .h, destination buffer and buffersize
              the buffer must be large enough, no error/size checking is done!
Return Value: the buffer will contain the compressed data
******************************************************************************/
int compress_yuyv_to_jpeg(image_config_t *img, unsigned char *buffer, int size, int quality,
        struct jpeg_compress_struct *cinfo)
{
//    struct jpeg_compress_struct cinfo;
//    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    unsigned char *line_buffer, *yuyv;
    int z;
    /*static int written;*/
    int written;

//    DBG("compress_yuyv_to_jpeg 1\n");

    line_buffer = calloc(img->width * 3, 1);
    yuyv = img->data;

//    DBG("compress_yuyv_to_jpeg 2\n");

//    cinfo.err = jpeg_std_error(&jerr);
//    jpeg_create_compress(&cinfo);
    /* jpeg_stdio_dest (&cinfo, file); */
    dest_buffer(cinfo, buffer, size, &written);

//    DBG("compress_yuyv_to_jpeg 3\n");

    cinfo->image_width = img->width;
    cinfo->image_height = img->height;
    cinfo->input_components = 3;
    cinfo->in_color_space = JCS_RGB;

//    DBG("compress_yuyv_to_jpeg 4\n");

    jpeg_set_defaults(cinfo);
    jpeg_set_quality(cinfo, quality, TRUE);

//    DBG("compress_yuyv_to_jpeg 5\n");

    jpeg_start_compress(cinfo, TRUE);

//    DBG("compress_yuyv_to_jpeg 6\n");

//    DBG("compress_yuyv_to_jpeg size: %d, %d\n", im->width, im->height);

    z = 0;
    while(cinfo->next_scanline < img->height) {
        int x;
        unsigned char *ptr = line_buffer;

//        DBG("compress_yuyv_to_jpeg 6.1: %d\n", cinfo.next_scanline);

        for(x = 0; x < img->width; x++) {
            int r, g, b;
            int y, u, v;

            if(!z)
                y = yuyv[0] << 8;
            else
                y = yuyv[2] << 8;
            u = yuyv[1] - 128;
            v = yuyv[3] - 128;

            r = (y + (359 * v)) >> 8;
            g = (y - (88 * u) - (183 * v)) >> 8;
            b = (y + (454 * u)) >> 8;

            *(ptr++) = (r > 255) ? 255 : ((r < 0) ? 0 : r);
            *(ptr++) = (g > 255) ? 255 : ((g < 0) ? 0 : g);
            *(ptr++) = (b > 255) ? 255 : ((b < 0) ? 0 : b);

            if(z++) {
                z = 0;
                yuyv += 4;
            }
        }

//        DBG("compress_yuyv_to_jpeg 6.2\n");

        row_pointer[0] = line_buffer;
        jpeg_write_scanlines(cinfo, row_pointer, 1);

//        DBG("compress_yuyv_to_jpeg 6.3\n");
    }

//    DBG("compress_yuyv_to_jpeg 7\n");

    jpeg_finish_compress(cinfo);
//    jpeg_destroy_compress(&cinfo);

//    DBG("compress_yuyv_to_jpeg 8\n");

    free(line_buffer);

//    DBG("compress_yuyv_to_jpeg 9\n");

    return (written);
}

/******************************************************************************
Description.: rgb82jpeg function is based on compress_yuyv_to_jpeg written by
              Gabriel A. Devenyi.
              It uses the destination manager implemented above to compress
              RGB8 data to JPEG. Most other implementations use the
              "jpeg_stdio_dest" from libjpeg, which can not store compressed
              pictures to memory instead of a file.
Input Value.: image structure from own .h, destination buffer and buffersize
              the buffer must be large enough, no error/size checking is done!
Return Value: the buffer will contain the compressed data
******************************************************************************/
int compress_rgb8_to_jpeg(image_config_t *img, unsigned char *buffer, int size, int quality,
        struct jpeg_compress_struct *cinfo)
{
    JSAMPROW row_pointer[1];
    int written;
    int row_stride;

    row_stride = img->width * 3;

    dest_buffer(cinfo, buffer, size, &written);

    cinfo->image_width = img->width;
    cinfo->image_height = img->height;
    cinfo->input_components = 3;
    cinfo->in_color_space = JCS_RGB;

    jpeg_set_defaults(cinfo);
    jpeg_set_quality(cinfo, quality, TRUE);

    jpeg_start_compress(cinfo, TRUE);

    while(cinfo->next_scanline < cinfo->image_height) {
        row_pointer[0] = &img->data[cinfo->next_scanline * row_stride];
        jpeg_write_scanlines(cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(cinfo);

    return (written);
}
