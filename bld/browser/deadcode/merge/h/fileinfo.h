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


#ifndef _LINE_SECTION_FILE_NAME_INCLUDED
#define _LINE_SECTION_FILE_NAME_INCLUDED

// System includes --------------------------------------------------------

#include <wstd.h>
#include <string.h>
#include <wcvector.h>

// Project includes -------------------------------------------------------

#include "dbgobj.h"
#include "memstrm.h"

class FileInfo : public DebugObject {
friend class LinePrologue;
friend class MBRFile;

public:
                                FileInfo( char * filename = NULL,
                                          uint_32 dirIndex = 0,
                                          uint_32 lastModified = 0,
                                          uint_32 length = 0,
                                          uint_32 newDirIndex = 0 );
    void                        encode( MemoryStream & mem );

    void                        set( char * filename = NULL,
                                     uint_32 dirIndex = 0,
                                     uint_32 lastModified = 0,
                                     uint_32 length = 0,
                                     uint_32 newDirIndex = 0 );
    uint_32                     oldDirIndex() { return _oldDirIndex; }
    uint_32                     lastModified() { return _lastModified; }
    uint_32                     length() { return _length; }
    uint_32                     newDirIndex() { return _newDirIndex; }

private:

    uint_32                     _oldDirIndex;
    uint_32                     _lastModified;
    uint_32                     _length;
    uint_32                     _newDirIndex;

};

typedef WCValOrderedVector<FileInfo *> FileNameVector;

#endif

