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


#define ADJUST_HFILE 'A'

typedef struct {
    char                command;
    unsigned long       pid;
    unsigned long       tid;
} pmhelp_packet;

enum {
    PMHELP_LOCK,
    PMHELP_UNLOCK,
    PMHELP_EXIT,
    PMHELP_SWITCHBACK,
};

#define MSGBOXID    1001

#define ID_WINDOW   256
#define ID_WINDOW2  356

#define ID_OPTIONS  257
#define ID_LOCK     258
#define ID_UNLOCK   259
#define ID_EXITPROG 260
#define ID_SWITCH   261
#define ID_BUTTON   262

extern void StartPMHelp(void);
extern void StopPMHelp(void);

extern void PMLock(unsigned long,unsigned long);
extern void PMUnLock(void);
extern int PMFlip(void);
