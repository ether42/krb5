/*
 * Copyright 1993 by OpenVision Technologies, Inc.
 * 
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 * 
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "gssapiP_krb5.h"
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif

/*
 * $Id$
 */

static unsigned char zeros[16] = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};

krb5_error_code
kg_make_seed(context, key, seed)
     krb5_context context;
     krb5_keyblock *key;
     unsigned char *seed;
{
   krb5_error_code code;
   krb5_keyblock *tmpkey;
   int i;

   if (code = krb5_copy_keyblock(context, key, &tmpkey))
      return(code);

   /* reverse the key bytes, as per spec */

   for (i=0; i<tmpkey->length; i++)
      tmpkey->contents[i] = key->contents[key->length - 1 - i];

   code = kg_encrypt(context, tmpkey, NULL, zeros, seed, 16);

   krb5_free_keyblock(context, tmpkey);

   return(code);
}
