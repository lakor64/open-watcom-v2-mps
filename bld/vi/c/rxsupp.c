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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "vi.h"
#include "rxsupp.h"

extern char near META[];

/*
 * CurrentRegComp - compile current regular expression
 */
int CurrentRegComp( char *str )
{
    MemFree( CurrentRegularExpression );
    CurrentRegularExpression = RegComp( str );
    return( RegExpError );

} /* CurrentRegComp */

/*
 * GetCurrRegExpColumn
 */
int GetCurrRegExpColumn( char *data )
{
    int cl;

    cl = (int) (CurrentRegularExpression->startp[0] - data);
    return( cl );

} /* GetCurrRegExpColumn */

/*
 * GetCurrRegExpLength
 */
int GetCurrRegExpLength( void )
{
    int len;

    len = (int) (CurrentRegularExpression->endp[0] - CurrentRegularExpression->startp[0] );
    return( len );

} /* GetCurrRegExpLength */

/*
 * SetMajickString - set up the Majick string
 */
void SetMajickString( char *str )
{

    if( str == NULL ) {
        if( Majick != NULL ) {
            return;
        }
        str = "()~@";
    }
    AddString2( &Majick, str );

} /* SetMajickString */

/*
 * MakeExpressionNonRegular - escape out all magical chars
 */
void MakeExpressionNonRegular( char *str )
{
    int         i,j=0,k;
    char        *foo;

    k = strlen( str );
    foo = StaticAlloc();
    for( i=0;i<k;i++ ) {
        if( str[i] == '/' ) {
            foo[j++] = '\\';
        } else if( strchr( META, str[i] ) != NULL ) {
            foo[j++] = '\\';
            if( !EditFlags.Magic && Majick != NULL ) {
                if( strchr( Majick, str[i] ) != NULL ) {
                    j--;
                }
            }
        }
        foo[j++] = str[i];
    }
    foo[j] = 0;
    strcpy( str, foo );
    StaticFree( foo );

} /* MakeExpressionNonRegular */
