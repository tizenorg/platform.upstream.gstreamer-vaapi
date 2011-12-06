/*
 *  gstvaapidebug.h - VA-API debugging utilities
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_DEBUG_H
#define GST_VAAPI_DEBUG_H

#include <gst/gstinfo.h>

#if DEBUG
GST_DEBUG_CATEGORY_EXTERN(gst_debug_vaapi);
#define GST_CAT_DEFAULT gst_debug_vaapi
#endif
/* FPS Calculation for DEBUG */
#define FPS_CALCULATION(objname)                     \
    do{                                              \
        static guint num_frame = 0;                  \
        static struct timeval last_sys_time;         \
        static struct timeval first_sys_time;        \
        static int b_last_sys_time_init = FALSE;     \
        if (!b_last_sys_time_init) {                 \
          gettimeofday (&last_sys_time, NULL);       \
          gettimeofday (&first_sys_time, NULL);      \
          b_last_sys_time_init = TRUE;               \
        } else {                                     \
          if ((num_frame%50)==0) {                   \
            double total, current;                   \
            struct timeval cur_sys_time;             \
            gettimeofday (&cur_sys_time, NULL);      \
            total = (cur_sys_time.tv_sec - first_sys_time.tv_sec)*1.0f +       \
                   (cur_sys_time.tv_usec - first_sys_time.tv_usec)/1000000.0f; \
            current = (cur_sys_time.tv_sec - last_sys_time.tv_sec)*1.0f +      \
                    (cur_sys_time.tv_usec - last_sys_time.tv_usec)/1000000.0f; \
            printf("%s Current fps: %.2f, Total avg fps: %.2f\n",              \
                    #objname, (float)50.0f/current, (float)num_frame/total);   \
            last_sys_time = cur_sys_time;            \
          }                                          \
        }                                            \
        ++num_frame;                                 \
    }while(0)


#endif /* GST_VAAPI_DEBUG_H */
