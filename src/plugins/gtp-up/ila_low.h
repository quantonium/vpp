/*
 * ila_low.h - GTP to ILA interface.
 *
 * Copyright (c) 2018, Quantonium Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Quantonium nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL QUANTONIUM BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __ILA_LOW_H__
#define __ILA_LOW_H__

/* These are some defintions taken from the ila kernel include file
 * net/ipv6/ila/ila.h. These defintions should really be in the uapi
 * ila.h. Once they are there, this include file can be removed and
 * that one used.
 */

/* Temporary hack. Assume running on little endian (x86) */
#define __LITTLE_ENDIAN_BITFIELD

struct ila_locator {
	union {
		__u8            v8[8];
		__be16          v16[4];
		__be32          v32[2];
		__be64		v64;
	};
};

struct ila_identifier {
	union {
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			u8 __space:4;
			u8 csum_neutral:1;
			u8 type:3;
#elif defined(__BIG_ENDIAN_BITFIELD)
			u8 type:3;
			u8 csum_neutral:1;
			u8 __space:4;
#else
#error  "Adjust your <asm/byteorder.h> defines"
#endif
			u8 __space2[7];
		};
		__u8            v8[8];
		__be16          v16[4];
		__be32          v32[2];
		__be64		v64;
	};
};

#define CSUM_NEUTRAL_FLAG	htonl(0x10000000)

struct ila_addr {
	union {
		struct in6_addr addr;
		struct {
			struct ila_locator loc;
			struct ila_identifier ident;
		};
	};
};

static inline struct ila_addr *ila_a2i(struct in6_addr *addr)
{
	return (struct ila_addr *)addr;
}

static inline bool ila_addr_is_ila(struct ila_addr *iaddr)
{
	return (iaddr->ident.type != ILA_ATYPE_IID);
}

static inline bool ila_csum_neutral_set(struct ila_identifier ident)
{
	return !!(ident.csum_neutral);
}

#endif /* __ILA_LOW_H__ */
