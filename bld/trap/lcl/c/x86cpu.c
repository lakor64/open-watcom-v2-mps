/****************************************************************************
*
*                            Open Watcom Project
*
*    Portions Copyright (c) 1983-2002 Sybase, Inc. All Rights Reserved.
*
*  ========================================================================
*
*    This file contains Original Code and/or Modifications of Original
*    Code as defined in and that are subject to the Sybase Open Watcom
*    Public License version 1.0 (the 'License'). You may not use this file
*    except in compliance with the License. BY USING THIS FILE YOU AGREE TO
*    ALL TERMS AND CONDITIONS OF THE LICENSE. A copy of the License is
*    provided with the Original Code and Modifications, and is also
*    available at www.sybase.com/developer/opensource.
*
*    The Original Code and all software distributed under the License are
*    distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
*    EXPRESS OR IMPLIED, AND SYBASE AND ALL CONTRIBUTORS HEREBY DISCLAIM
*    ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR
*    NON-INFRINGEMENT. Please see the License for the specific language
*    governing rights and limitations under the License.
*
*  ========================================================================
*
* Description:  WHEN YOU FIGURE OUT WHAT THIS FILE DOES, PLEASE
*               DESCRIBE IT HERE!
*
****************************************************************************/


#include "mad.h"
#include "madx86.h"

#if !defined(__386__)
    #define     Is8086()        (!IsNot8086())
    extern unsigned short IsNot8086();
    /* shr ax,33 will produce 0 on 8086, 0xefff on everything else */
    #pragma aux IsNot8086 =     \
            "mov        ax,0xffff"      \
            "mov        cl,33"          \
            "shr        ax,cl"          \
            value [ax] modify [cl]

    extern unsigned short Is186();
    /* push sp on a 86/186 pushes value after increment */
    #pragma aux Is186 =         \
            "push       sp"             \
            "pop        ax"             \
            "sub        ax,sp"          \
            value [ax]

    #define     Is286()         (!IsNot286())
    extern unsigned short IsNot286();
    /* 286 won't let you turn on top bits in flags regs */
    /* NOTE: this doesn't work in protect mode */
    #pragma aux IsNot286 =              \
            "pushf"                     \
            "mov        ax,0xf000"      \
            "push       ax"             \
            "popf"                      \
            "pushf"                     \
            "pop        ax"             \
            "popf"                      \
            "and        ax,0xf000"      \
            value [ax]
#endif

#if defined(__386__)
    #define AX  eax
    #define BX  ebx
    #define CX  ecx
    #define DX  edx
#else
    #define AX  ax
    #define BX  bx
    #define CX  cx
    #define DX  dx
#endif

extern unsigned Is386();
/* Try and flip the AC bit in EFlags */
#pragma aux Is386 =             \
        ".586"                  \
        "mov    dx,sp"          \
        "and    sp,0xfffc"      \
        "pushfd"                \
        "pushfd"                \
        "pop    eax"            \
        "xor    eax,0x40000"    \
        "push   eax"            \
        "popfd"                 \
        "pushfd"                \
        "pop    ebx"            \
        "popfd"                 \
        "xor    eax,ebx"        \
        "shr    eax,18"         \
        "and    eax,1"          \
        "mov    sp,dx"          \
        value [AX] modify [BX DX]

extern unsigned Is486();
/* Try and flip the ID bit in EFlags */
#pragma aux Is486 =             \
        ".586"                  \
        "pushfd"                \
        "pushfd"                \
        "pop    eax"            \
        "xor    eax,0x200000"   \
        "push   eax"            \
        "popfd"                 \
        "pushfd"                \
        "pop    ebx"            \
        "popfd"                 \
        "xor    eax,ebx"        \
        "shr    eax,21"         \
        "and    eax,1"          \
        value [AX] modify [BX]

extern unsigned CPUId();
#pragma aux CPUId =             \
        ".586"                  \
        "mov    eax,1"          \
        "cpuid"                 \
        "shr    eax,8"          \
        "and    eax,0xf"        \
        value [AX] modify [BX CX DX]

unsigned X86CPUType()
{
    #if !defined(__386__)
        if( Is8086() ) return( X86_86 );
        if( Is186() ) return( X86_186 );
        if( Is286() ) return( X86_286 );
    #endif
    if( Is386() ) return( X86_386 );
    if( Is486() ) return( X86_486 );
    return( CPUId() );
}

#ifdef TEST
main()
{
    printf( "CPUType => %d\n", X86CPUType() );
}
#endif
