/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
 * Use is subject to license terms.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *      
 */
#include <stdio.h>

/*-
 * Copyright (c) 2001-2003 Allan Saddi <allan@saddi.com>
 * Copyright (c) 2012 Moinak Ghosh moinakg <at1> gm0il <dot> com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Define WORDS_BIGENDIAN if compiling on a big-endian architecture.
 */

/*
 * Added PreFinal and updateSimple -- David G. Andersen, Jan 2014
 */
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include <pthread.h>
#include <string.h>
#include "sha512.h"

//LITTLE ENDIAN ONLY

#if defined(__MINGW32__) || defined(__MINGW64__)

static __inline unsigned short
bswap_16 (unsigned short __x)
{
  return (__x >> 8) | (__x << 8);
}

static __inline unsigned int
bswap_32 (unsigned int __x)
{
  return (bswap_16 (__x & 0xffff) << 16) | (bswap_16 (__x >> 16));
}

static __inline unsigned long long
bswap_64 (unsigned long long __x)
{
  return (((unsigned long long) bswap_32 (__x & 0xffffffffull)) << 32) | (bswap_32 (__x >> 32));
}

#define BYTESWAP(x) bswap_32(x)
#define BYTESWAP64(x) bswap_64(x)

#elif defined(__APPLE__)

#include <libkern/OSByteOrder.h>

#define BYTESWAP(x) OSSwapBigToHostInt32(x)
#define BYTESWAP64(x) OSSwapBigToHostInt64(x)

#else

#include <endian.h> //glibc

#define BYTESWAP(x) be32toh(x)
#define BYTESWAP64(x) be64toh(x)

#endif /* defined(__MINGW32__) || defined(__MINGW64__) */


static const uint8_t padding[SHA512_BLOCK_SIZE] = {
  0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint64_t iv512[SHA512_HASH_WORDS] = {
  0x6a09e667f3bcc908LL,
  0xbb67ae8584caa73bLL,
  0x3c6ef372fe94f82bLL,
  0xa54ff53a5f1d36f1LL,
  0x510e527fade682d1LL,
  0x9b05688c2b3e6c1fLL,
  0x1f83d9abfb41bd6bLL,
  0x5be0cd19137e2179LL
};

static const uint64_t iv256[SHA512_HASH_WORDS] = {
  0x22312194fc2bf72cLL,
  0x9f555fa3c84c64c2LL,
  0x2393b86b6f53b151LL,
  0x963877195940eabdLL,
  0x96283ee2a88effe3LL,
  0xbe5e1e2553863992LL,
  0x2b0199fc2c85b8aaLL,
  0x0eb72ddc81c52ca2LL
};

static void
_init (SHA512_Context *sc, const uint64_t iv[SHA512_HASH_WORDS])
{
	int i;

	sc->totalLength[0] = 0LL;
	sc->totalLength[1] = 0LL;
	for (i = 0; i < SHA512_HASH_WORDS; i++)
		sc->hash[i] = iv[i];
	sc->bufferLength = 0L;
}

void
SHA512_Init (SHA512_Context *sc)
{
	_init (sc, iv512);
}

static const uint8_t finalpad[92] = {
0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  /* 64 */
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  /* 72 */
0x0, 0x0, 0x0, 0x0, /* 76 */
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  /* 84 */
0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x20 /* 92 */
};

void
SHA512_Update_Simple (SHA512_Context *sc, const void *vdata, size_t len)
{
        const uint8_t *data = (const uint8_t *)vdata;
        uint32_t bufferBytesLeft;
        size_t bytesToCopy;

        bufferBytesLeft = SHA512_BLOCK_SIZE - sc->bufferLength;
        bytesToCopy = bufferBytesLeft;
        if (bytesToCopy > len) {
          // btc = 92, len = 76 for first call
          bytesToCopy = len;
        }
        
        memcpy (&sc->buffer.bytes[sc->bufferLength], data, bytesToCopy);
        sc->totalLength[1] += bytesToCopy * 8L;
        
        sc->bufferLength += bytesToCopy;
        data += bytesToCopy;
        len -= bytesToCopy;
}

void SHA512_PreFinal (SHA512_Context *sc)
{
        SHA512_Update_Simple (sc, finalpad, 92);

        sc->blocks = 1;
}
