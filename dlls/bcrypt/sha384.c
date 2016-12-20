/*
 * Copyright 2016 Michael Müller
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#include "bcrypt_internal.h"

void sha384_init(SHA512_CTX *ctx)
{
    ctx->len = 0;
    ctx->h[0] = 0xcbbb9d5dc1059ed8ULL;
    ctx->h[1] = 0x629a292a367cd507ULL;
    ctx->h[2] = 0x9159015a3070dd17ULL;
    ctx->h[3] = 0x152fecd8f70e5939ULL;
    ctx->h[4] = 0x67332667ffc00b31ULL;
    ctx->h[5] = 0x8eb44a8768581511ULL;
    ctx->h[6] = 0xdb0c2e0d64f98fa7ULL;
    ctx->h[7] = 0x47b5481dbefa4fa4ULL;
}

void sha384_finalize(SHA512_CTX *ctx, UCHAR *buffer)
{
    UCHAR buffer512[64];

    sha512_finalize(ctx, buffer512);
    memcpy(buffer, buffer512, 48);
}
