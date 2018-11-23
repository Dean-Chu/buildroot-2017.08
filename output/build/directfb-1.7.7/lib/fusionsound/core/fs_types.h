/*
   (c) Copyright 2012-2013  DirectFB integrated media GmbH
   (c) Copyright 2001-2013  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Shimokawa <andi@directfb.org>,
              Marek Pikarski <mass@directfb.org>,
              Sven Neumann <neo@directfb.org>,
              Ville Syrjälä <syrjala@sci.fi> and
              Claudio Ciccani <klan@users.sf.net>.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/


#ifndef __FS_TYPES_H__
#define __FS_TYPES_H__

#include <dfb_types.h>

/*
 * __fsf (FusionSound Float): used by internal mixing routines.
 */

#ifdef FS_USE_IEEE_FLOATS

typedef float __fsf;

#define FSF_ONE   1.0f

#define FSF_MAX   0.999999880791f

#define FSF_MIN  -1.0f


#define fsf_add( a, b )              ((a) + (b))

#define fsf_sub( a, b )              ((a) - (b))

#define fsf_shr( a, b )              ((a) / (__fsf)(1 << (b)))

#define fsf_shl( a, b )              ((a) * (__fsf)(1 << (b)))

#define fsf_mul( a, b )              ((a) * (b))

#define fsf_div( a, b )              ((a) / (b))

#define fsf_clip( x )                (((x) > FSF_MAX) ? FSF_MAX : \
                                      (((x) < FSF_MIN) ? FSF_MIN : (x)))


#define fsf_from_int( x )            (__fsf)(x)
#define fsf_from_int_scaled( x, s )  (__fsf)(x) * (1.0f/(1<<(s)))
#define fsf_to_int( x )              (int)(x)

#define fsf_from_float( x )          (__fsf)(x)
#define fsf_to_float( x )            (__fsf)(x)

#define fsf_from_u8( x )             ((__fsf)((x)-128)/128.0f)
#define fsf_to_u8( x )               (((x)*128.0f)+128)

#define fsf_from_s16( x )            ((__fsf)(x)/32768.0f)
#define fsf_to_s16( x )              ((x)*32768.0f)

#define fsf_from_s24( x )            ((__fsf)(x)/8388608.0f)
#define fsf_to_s24( x )              ((x)*8388608.0f)

#define fsf_from_s32( x )            ((__fsf)(x)/2147483648.0f)
#define fsf_to_s32( x )              ((x)*2147483648.0f)

/*
 * Triangular Dithering.
 */
#define fsf_dither_profiles( p, n ) \
     struct { unsigned int r; } p[n] = { [0 ... (n-1)] = { 0 } }

#define fsf_dither( s, b, p ) ( \
 __extension__({ \
     register int _r; \
     _r    = -((p).r >> (b)); \
     (p).r = (p).r * 196314165 + 907633515; \
     _r   += (p).r >> (b); \
     (s) + (float)_r / 2147483648.0f; \
 }) \
)

#else /* !FS_USE_IEEE_FLOATS (Fixed Point) */

typedef signed int __fsf;

#define FSF_DECIBITS   23 /* Number of bits for decimal part.
                           * This is 23 by default because we need at least
                           * 7 bits for integer part (mixing limitations) 
                           * and 1 bit for sign, another bit is reserved to
                           * avoid overlaps during sums.
                           */

#define FSF_ONE        (1 << FSF_DECIBITS)

#define FSF_MAX        (FSF_ONE - 1)

#define FSF_MIN       -FSF_ONE


#define fsf_add( a, b )              ((a) + (b))

#define fsf_sub( a, b )              ((a) - (b))

#define fsf_shr( a, b )              ((a) >> (b))

#define fsf_shl( a, b )              ((a) << (b))          

#if (SIZEOF_LONG == 8) || defined(FS_ENABLE_PRECISION)
# define fsf_mul( a, b )             (((long long)(a) * (long long)(b)) >> FSF_DECIBITS)
#else
# define fsf_mul( a, b )             (((a) >> (FSF_DECIBITS-15)) * ((b) >> 15))
#endif

#define fsf_div( a, b )              ((a) / (b))

#define fsf_clip( x )                (((x) > FSF_MAX) ? FSF_MAX : \
                                      (((x) < FSF_MIN) ? FSF_MIN : (x)))


#define fsf_from_int( x )            ((__fsf)(x) << FSF_DECIBITS)
#define fsf_from_int_scaled( x, s )  ((__fsf)(x) << (FSF_DECIBITS-(s)))
#define fsf_to_int( x )              (int)((x) >> FSF_DECIBITS)

#define fsf_from_float( x )          ((__fsf)((x) * (float)FSF_ONE))
#define fsf_to_float( x )            ((float)(x) / (float)FSF_ONE)

#if FSF_DECIBITS >= 7
# define fsf_from_u8( x )            ((__fsf)((x)-128) << (FSF_DECIBITS - 7))
# define fsf_to_u8( x )              (((x) >> (FSF_DECIBITS - 7))+128)
#else
# define fsf_from_u8( x )            ((__fsf)((x)-128) >> (7 - FSF_DECIBITS))
# define fsf_to_u8( x )              (((x) << (7 - FS_DECIBITS))+128)
#endif

#if FSF_DECIBITS >= 15
# define fsf_from_s16( x )           ((__fsf)(x) << (FSF_DECIBITS - 15))
# define fsf_to_s16( x )             ((x) >> (FSF_DECIBITS - 15))
#else
# define fsf_from_s16( x )           ((__fsf)(x) >> (15 - FSF_DECIBITS))
# define fsf_to_s16( x )             ((x) << (15 - FSF_DECIBITS))
#endif

#if FSF_DECIBITS >= 23
# define fsf_from_s24( x )           ((__fsf)(x) << (FSF_DECIBITS - 23))
# define fsf_to_s24( x )             ((x) >> (FSF_DECIBITS - 23))
#else
# define fsf_from_s24( x )           ((__fsf)(x) >> (23 - FSF_DECIBITS))
# define fsf_to_s24( x )             ((x) << (23 - FSF_DECIBITS))
#endif

#if FSF_DECIBITS >= 31
# define fsf_from_s32( x )           ((__fsf)(x) << (FSF_DECIBITS - 31))
# define fsf_to_s32( x )             ((x) >> (FSF_DECIBITS - 31))
#else
# define fsf_from_s32( x )           ((__fsf)(x) >> (31 - FSF_DECIBITS))
# define fsf_to_s32( x )             ((x) << (31 - FSF_DECIBITS))
#endif

/* 
 * Noise Shaped Dithering.
 *
 * Rough adaption of the following 5th order FIR filter:
 *        { 2.033, -2.165, 1.959, -1.590, 0.6149 }
 * Reference: 
 *   Stanley P. Lipshitz, John Vanderkooy, and Robert A. Wannamaker,
 *   "Minimally Audible Noise Shaping", J. Audio Eng. Soc., Vol.39, No.11, 1991.
 */
#define fsf_dither_profiles( p, n ) \
     struct { int e[5]; unsigned int r; } p[n] = { [0 ... (n-1)] = { {0, 0, 0, 0, 0}, 0 } }

#define fsf_dither( s, b, p ) ( \
 __extension__({ \
     const int      _m = (1 << (FSF_DECIBITS+1-(b))) - 1; \
     register __fsf _s, _o; \
     _s       = (s) + (p).e[0] - (p).e[1] + (p).e[2] - (p).e[3] + (p).e[4]; \
     _o       = _s + (1 << (FSF_DECIBITS-(b))) - ((p).r & _m); \
     (p).r    = (p).r * 196314165 + 907633515; \
     _o      += (p).r & _m; \
     (p).e[4] = ((p).e[3] >> 1) - ((p).e[3] >> 3); \
     (p).e[3] = (p).e[2] - ((p).e[2] >> 2); \
     (p).e[2] = (p).e[1] - ((p).e[1] >> 4); \
     (p).e[1] = (p).e[0] + ((p).e[0] >> 4); \
     (p).e[0] = _s - (_o & ~_m); \
     (p).e[0] = ((p).e[0] << 1) + ((p).e[0] >> 5); \
     _o; \
   }) \
)

#endif /* FS_USE_IEEE_FLOATS */


#endif /* __FS_TYPES_H__ */
