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


#include <stddef.h>
#include <string.h>
#include <i86.h>
#define INCL_DOSEXCEPTIONS
#define INCL_DOSPROCESS
#define INCL_DOSMISC
#define INCL_DOSERRORS
#define INCL_DOSSESMGR
#define INCL_DOSMODULEMGR
#include <os2.h>
#include "dosdebug.h"
#include "trpimp.h"
#include "os2trap.h"
#include "os2v2acc.h"
#include "bsexcpt.h"
#include "wdpmhelp.h"
#include "softmode.h"
#include "madregs.h"
#define ERROR_DEFINE_CONSTS
#include "os2err.h"
#include "exedos.h"
#include "exeos2.h"
#include "exeflat.h"

dos_debug               Buff;
static BOOL             stopOnSecond;
USHORT                  TaskFS;
extern char             SetHardMode(char);
extern VOID             InitDebugThread(VOID);

extern unsigned         X86CPUType();

#ifdef DEBUG_OUT

void Out(char *str)
{
    USHORT      written;

    DosWrite(1, str, strlen(str), &written);
}

#define NSIZE 20
void OutNum(ULONG i)
{
    char numbuff[NSIZE];
    char *ptr;

    ptr = numbuff+NSIZE;
    *--ptr = '\0';
    if (i == 0) {
        *--ptr = '0';
    }
    while (i != 0) {
        *--ptr = "0123456789abcdef"[i & 0x0f];
        i >>= 4;
    }
    Out(ptr);
}
#endif

#define EXE_MZ  0x5a4d
#define EXE_NE  0x454e
#define EXE_LE  0x454c
#define EXE_LX  0x584c

#define OBJECT_IS_CODE  0x0004L
#define OBJECT_IS_BIG   0x2000L

#define EXE_IS_FULLSCREEN       0x0100
#define EXE_IS_PMC              0x0200
#define EXE_IS_PM               0x0300

static ULONG    ExceptLinear;
static UCHAR    TypeProcess;
static BOOL     Is32Bit;
static watch    WatchPoints[MAX_WP];
static short    WatchCount = 0;
static short    DebugRegsNeeded = 0;
static unsigned_16      lastCS;
static unsigned_16      lastSS;
static unsigned_32      lastEIP;
static unsigned_32      lastESP;


extern void bp(void);
bool    ExpectingAFault;
char    OS2ExtList[] = {".exe\0"};

#pragma aux bp = 0xcc;

static bool Is32BitSeg(unsigned seg)
{
    if (IsFlatSeg(seg))
        return TRUE;
    if (IsUnknownGDTSeg(seg))
        return(TRUE);
    return FALSE;
}

/*
 * RecordModHandle - save module handle for later reference
 */
void RecordModHandle(ULONG value)
{
    if (ModHandles == NULL) {
        DosAllocMem(&ModHandles, sizeof(ULONG) * 512, PAG_COMMIT | PAG_READ | PAG_WRITE);
        }
    ModHandles[NumModHandles] = value;
    ++NumModHandles;
}


/*
 * SeekRead - seek to a file position, and read the data
 */
BOOL SeekRead(HFILE handle, ULONG newpos, void *ptr, ULONG size)
{
    ULONG       read;
    ULONG       pos;

    if (DosSetFilePtr(handle, newpos, 0, &pos) != 0) {
        return FALSE;
    }
    if (DosRead(handle, ptr, size, &read) != 0) {
        return FALSE;
    }
    if (read != size) {
        return FALSE;
    }
    return TRUE;

} /* SeekRead */


/*
 * FindNewHeader - get a pointer to the new exe header
 */
static BOOL FindNewHeader(char *name, HFILE *hdl,
                          ULONG *new_head, USHORT *id)
{
    long        open_rc;
    HFILE       h;
    BOOL        rc;
    USHORT      data; /* MUST be 16-bit! */

    open_rc = OpenFile(name, 0, OPEN_PRIVATE);
    if (open_rc < 0) {
        return FALSE;
    }
    h = open_rc;
    rc = FALSE;
    while (1) {
        if (!SeekRead(h, 0x00, &data, sizeof(data))) {
            break;
        }
        if (data != EXE_MZ) break;   /* MZ */

        if (!SeekRead(h, 0x18, &data, sizeof(data))) {
            break;
        }
        if (data < 0x40)       /* offset of relocation header */
            break;

        if (!SeekRead(h, 0x3c, new_head, sizeof(ULONG))) {
            break;
        }

        if (!SeekRead(h, *new_head, id, sizeof(USHORT))) {
            break;
        }
        rc = TRUE;
        break;
    }
    if (!rc) {
        DosClose(h);
    }
    *hdl = h;
    return rc;

} /* FindNewHeader */

#define MAX_OBJECTS     128

static ULONG            LastMTE;
static unsigned         NumObjects;
static object_record    ObjInfo[MAX_OBJECTS];

static void GetObjectInfo(ULONG mte)
{
    HFILE               hdl;
    ULONG               new_head;
    USHORT              type;
    unsigned_32         objoff;
    unsigned_32         numobjs;

    char                buff[CCHMAXPATH];

    if (mte == LastMTE) {
        return;
    }
    memset(ObjInfo, 0, sizeof(ObjInfo));
    DosQueryModuleName(mte, sizeof(buff), buff);
    NumObjects = 0;
    if (!FindNewHeader(buff, &hdl, &new_head, &type)) {
        return;
    }
    if (type != EXE_LE && type != EXE_LX) {
        DosClose(hdl);
        return;
    }
    SeekRead(hdl, new_head + 0x40, &objoff, sizeof(objoff));
    SeekRead(hdl, new_head + 0x44, &numobjs, sizeof(numobjs));
    if (numobjs <= MAX_OBJECTS) {
        SeekRead(hdl, new_head + objoff, ObjInfo, numobjs * sizeof(ObjInfo[0]));
        NumObjects = numobjs;
    }
    LastMTE = mte;
    DosClose(hdl);
}


bool DebugExecute(dos_debug *buff, ULONG cmd, bool stop_on_module_load)
{
    EXCEPTIONREPORTRECORD       ex;
    ULONG                       value;
    ULONG                       stopvalue;
    ULONG                       notify = 0;
    BOOL                        got_second_notification;
    ULONG                       fcp;
    CONTEXTRECORD               fcr;

    buff->Cmd = cmd;
    value = buff->Value;
    if (cmd == DBG_C_Go) {
        value = 0;
    }
    stopvalue = XCPT_CONTINUE_EXECUTION;
    got_second_notification = FALSE;
    if (cmd == DBG_C_Stop) {
        stopvalue = XCPT_CONTINUE_STOP;
    }

    for ( ; ; ) {

        buff->Value = value;
        buff->Cmd = cmd;
        CallDosDebug(buff);

        value = stopvalue;
        cmd = DBG_C_Continue;

        /*
         * handle the preemptive notifications
         */
        switch (buff->Cmd) {
        case DBG_N_ModuleLoad:
            RecordModHandle(buff->Value);
            if (stop_on_module_load)
                return(TRUE);
            break;
        case DBG_N_ModuleFree:
            break;
        case DBG_N_NewProc:
            break;
        case DBG_N_ProcTerm:
            value = XCPT_CONTINUE_STOP;         /* halt us */
            notify = DBG_N_ProcTerm;
            break;
        case DBG_N_ThreadCreate:
            break;
        case DBG_N_ThreadTerm:
            break;
        case DBG_N_AliasFree:
            break;
        case DBG_N_Exception:
            ExceptLinear = buff->Addr;
            if (buff->Value == DBG_X_STACK_INVALID) {
                value = XCPT_CONTINUE_SEARCH;
                break;
            }
            fcp = buff->Len;
            if (buff->Value == DBG_X_PRE_FIRST_CHANCE) {
                ExceptNum = buff->Buffer;
                if (ExceptNum == XCPT_BREAKPOINT) {
                    notify = DBG_N_Breakpoint;
                    value = XCPT_CONTINUE_STOP;
                    break;
                } else if (ExceptNum == XCPT_SINGLE_STEP) {
                    notify = DBG_N_SStep;
                    value = XCPT_CONTINUE_STOP;
                    break;
                }
            }
            //
            // NOTE: Going to second chance causes OS/2 to report the
            //       exception in the debugee.  However, if you report
            //       the fault at the first chance notification, the
            //       debugee's own fault handlers will not get invoked!
            //
            if (buff->Value == DBG_X_FIRST_CHANCE && !ExpectingAFault) {
                if (stopOnSecond && !got_second_notification) {
                    value = XCPT_CONTINUE_SEARCH;
                    break;
                }
            }
            notify = DBG_N_Exception;
            value = XCPT_CONTINUE_STOP;

            /*
             * Buffer contains the ptr to the exception block
             */
            buff->Cmd = DBG_C_ReadMemBuf;
            buff->Addr = buff->Buffer;
            buff->Buffer = (ULONG)&ex;
            buff->Len = sizeof(ex);
            CallDosDebug(buff);
            ExceptNum = ex.ExceptionNum;
            if (ExceptNum == XCPT_PROCESS_TERMINATE ||
                ExceptNum == XCPT_ASYNC_PROCESS_TERMINATE ||
                ExceptNum == XCPT_GUARD_PAGE_VIOLATION ||
                (ExceptNum & XCPT_CUSTOMER_CODE)) {
                value = XCPT_CONTINUE_SEARCH;
                break;
            }

            /*
             * get the context record
             */
            buff->Cmd = DBG_C_ReadMemBuf;
            buff->Addr = fcp;
            buff->Buffer = (ULONG)&fcr;
            buff->Len = sizeof(fcr);
            CallDosDebug(buff);
            buff->EAX = fcr.ctx_RegEax;
            buff->EBX = fcr.ctx_RegEbx;
            buff->ECX = fcr.ctx_RegEcx;
            buff->EDX = fcr.ctx_RegEdx;
            buff->ESI = fcr.ctx_RegEsi;
            buff->EDI = fcr.ctx_RegEdi;
            buff->ESP = fcr.ctx_RegEsp;
            buff->EBP = fcr.ctx_RegEbp;
            buff->DS = fcr.ctx_SegDs;
            buff->CS = fcr.ctx_SegCs;
            buff->ES = fcr.ctx_SegEs;
            buff->FS = fcr.ctx_SegFs;
            buff->GS = fcr.ctx_SegGs;
            buff->SS = fcr.ctx_SegSs;
            buff->EIP = fcr.ctx_RegEip;
            buff->EFlags = fcr.ctx_EFlags;
            WriteRegs(buff);

            if (ExpectingAFault || got_second_notification) {
                break;
            }
            if (stopOnSecond) {
                value = XCPT_CONTINUE_EXECUTION;
                got_second_notification = TRUE;
            }
            break;
        default:
            if (notify != 0) {
                buff->Cmd = notify;
            }
            return (FALSE);
        }
    }
//    return( FALSE );
}


void WriteRegs(dos_debug *buff)
{
    buff->Cmd = DBG_C_WriteReg;
    CallDosDebug(buff);
}

void ReadRegs(dos_debug *buff)
{

    buff->Cmd = DBG_C_ReadReg;
    CallDosDebug(buff);
}

void ReadLinear(char *data, ULONG lin, USHORT size)
{
    Buff.Cmd = DBG_C_ReadMemBuf;
    Buff.Addr = lin;
    Buff.Buffer = (ULONG)data;
    Buff.Len = size;
    CallDosDebug(&Buff);
}

void WriteLinear(char *data, ULONG lin, USHORT size)
{
    Buff.Cmd = DBG_C_WriteMemBuf;
    Buff.Addr = lin;
    Buff.Buffer = (ULONG)data;
    Buff.Len = size;
    CallDosDebug(&Buff);
}

USHORT WriteBuffer(char *data, USHORT segv, ULONG offv, USHORT size)
{
    USHORT      length;
    bool        iugs;
    USHORT      resdata;
    ULONG       flat;

    if (segv < 4) {
        return 0;
    }

    length = size;
    if (Pid != 0) {
        iugs = IsUnknownGDTSeg(segv);
        if (!iugs) {
            flat = MakeItFlatNumberOne(segv, offv);
            WriteLinear(data, flat, size);
            if (Buff.Cmd == DBG_N_Success) {
                return size;
            }
        }
        while (length != 0) {
            Buff.Cmd = DBG_C_WriteMem_D;
            if (length == 1) {
                if (iugs) {
                    if (!TaskReadWord(segv, offv, &resdata)) {
                        break;
                    }
                    resdata &= 0xff00;
                    resdata |= *data;
                    if (!TaskWriteWord(segv, offv, resdata)) {
                        break;
                    }
                } else {
                    Buff.Cmd = DBG_C_ReadMem_D;
                    Buff.Addr = MakeItFlatNumberOne(segv, offv);
                    CallDosDebug(&Buff);
                    Buff.Cmd = DBG_C_WriteMem_D;
                    Buff.Addr = MakeItFlatNumberOne(segv, offv);
                    Buff.Value &= 0xff00;
                    Buff.Value |= *data;
                    CallDosDebug(&Buff);
                    if (Buff.Cmd != DBG_N_Success) {
                        break;
                    }
                }
                data++;
                length--;
                offv++;
            } else {
                resdata = *data;
                data++;
                resdata |= *data << 8;
                data++;
                if (iugs) {
                    if (!TaskWriteWord(segv, offv, resdata)) {
                        break;
                    }
                } else {
                    Buff.Value = resdata;
                    Buff.Addr = MakeItFlatNumberOne(segv, offv);
                    CallDosDebug(&Buff);
                    if (Buff.Cmd != DBG_N_Success) {
                        break;
                    }
                }
                length -= 2;
                offv += 2;
            }
        }
    }
    return size - length; /* return amount written */
}


static USHORT ReadBuffer(char *data, USHORT segv, ULONG offv, USHORT size)
{
    USHORT      length;
    bool        iugs;
    USHORT      resdata;
    ULONG       flat;

    if (segv < 4) {
        return 0;
    }
    length = size;
    if (Pid != 0) {
        iugs = IsUnknownGDTSeg(segv);
        if (!iugs) {
            flat = MakeItFlatNumberOne(segv, offv);
            ReadLinear(data, flat, size);
            if (Buff.Cmd == DBG_N_Success) {
                return size;
            }
        }
        while (length != 0) {
            if (iugs) {
                if (!TaskReadWord(segv, offv, &resdata)) {
                    break;
                }
            } else {
                Buff.Cmd = DBG_C_ReadMem_D;
                Buff.Addr = MakeItFlatNumberOne(segv, offv);
                CallDosDebug(&Buff);
                if (Buff.Cmd != DBG_N_Success) {
                    break;
                }
                resdata = Buff.Value;
            }
            *data = resdata & 0xff;
            data++;
            offv++;
            length--;
            if (length != 0) {
                *data = resdata >> 8;
                data++;
                offv++;
                length--;
            }
        }
    }
    return size - length;
}


void DoWritePgmScrn(char *buff, USHORT len)
{
    ULONG   written;

    DosWrite(2, buff, len, &written);
    BreakPoint(0);
}

unsigned ReqGet_sys_config()
{
    ULONG         version[2];
    dos_debug     buff;
    char          tmp[DBG_CO_SIZE];
    get_sys_config_ret  *ret;

    ret = GetOutPtr(0);
    ret->sys.os = OS_OS2;
    DosQuerySysInfo(QSV_VERSION_MAJOR, QSV_VERSION_MINOR, &version, sizeof(version));
    ret->sys.osminor = version[1];
    ret->sys.osmajor = version[0];
    ret->sys.cpu = X86CPUType();
    ret->sys.fpu = X86_387; /* OS/2 2.0 auto-emulates */
    WriteRegs(&Buff);

    buff.Cmd = DBG_C_ReadCoRegs;
    buff.Buffer = (ULONG)tmp;
    buff.Tid = 1;
    buff.Pid = Pid;
    buff.Value = DBG_CO_387;    /* for 2.0: DBG_CO_387 */
    buff.Len = DBG_CO_SIZE;     /* for 2.0: size of register state */
    buff.Index = 0;             /* for 2.0: must be 0 */
    CallDosDebug(&buff);
    if (buff.Cmd != DBG_N_Success) {
        ret->sys.fpu = X86_NO;
    }
    /* Was:
     *  DosGetHugeShift(&shift);
     *  ret->sys.huge_shift = shift;
     *  I don't think the shift value is needed at all and it should be 3
     *  in case it is. MN
     */
    ret->sys.huge_shift = 3;
    ret->sys.mad = MAD_X86;
    return sizeof(*ret);
}


unsigned ReqMap_addr(void)
{
    USHORT              seg;
    ULONG               flags;
    map_addr_req        *acc;
    map_addr_ret        *ret;
    unsigned            i;
    addr_off            off;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);
    ret->lo_bound = 0;
    ret->hi_bound = ~(addr48_off)0;
    if (Pid == 0) {
        ret->out_addr = acc->in_addr;
        return sizeof(*ret);
    }

    GetObjectInfo(ModHandles[acc->handle]);

    seg = acc->in_addr.segment;
    off = acc->in_addr.offset;
    switch (seg) {
        case MAP_FLAT_CODE_SELECTOR:
        case MAP_FLAT_DATA_SELECTOR:
            seg = 1;
            off += ObjInfo[0].addr;
            for (i = 0; i < NumObjects; ++i) {
                if (ObjInfo[i].addr <= off
                        && (ObjInfo[i].addr + ObjInfo[i].size) > off) {
                    seg = i + 1;
                    off -= ObjInfo[i].addr;
                    ret->lo_bound = ObjInfo[i].addr - ObjInfo[0].addr;
                    ret->hi_bound = ret->lo_bound + ObjInfo[i].size - 1;
                    break;
                }
            }
            break;
    }

    Buff.MTE = ModHandles[acc->handle];
    Buff.Cmd = DBG_C_NumToAddr;
    Buff.Value = seg;
    CallDosDebug(&Buff);
    Buff.MTE = ModHandles[0];
    flags = ObjInfo[seg - 1].flags;
    if (flags & OBJECT_IS_BIG) {
        ret->out_addr.segment = (flags & OBJECT_IS_CODE) ? FlatCS : FlatDS;
        ret->out_addr.offset = Buff.Addr + off;
    } else {
        Buff.Cmd = DBG_C_LinToSel;
        CallDosDebug(&Buff);
        ret->out_addr.segment = Buff.Value;
        ret->out_addr.offset = Buff.Index + off;
    }
    return sizeof(*ret);
}

unsigned ReqAddr_info(void)
{
    addr_info_req       *acc;
    addr_info_ret       *ret;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);
    ret->is_32 = Is32BitSeg(acc->in_addr.segment);
    return sizeof(*ret);
}

unsigned ReqMachine_data()
{
    machine_data_req    *acc;
    machine_data_ret    *ret;
    unsigned_8          *data;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);
    data = GetOutPtr(sizeof(*ret));
    ret->cache_start = 0;
    ret->cache_end = ~(addr_off)0;
    *data = 0;
    if (Is32BitSeg(acc->addr.segment))
        *data |= X86AC_BIG;
    return sizeof(*ret) + sizeof(*data);
}

unsigned ReqChecksum_mem(void)
{
    ULONG         offset;
    USHORT        length;
    ULONG         sum;
    checksum_mem_req    *acc;
    checksum_mem_ret    *ret;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);
    length = acc->len;
    sum = 0;
    if (Pid != 0) {
        offset = acc->in_addr.offset;
        while (length != 0) {
            Buff.Cmd = DBG_C_ReadMem_D;
            Buff.Addr = MakeItFlatNumberOne(acc->in_addr.segment, offset);
            CallDosDebug(&Buff);
            if (Buff.Cmd != DBG_N_Success) {
                break;
            }
            sum += Buff.Value & 0xff;
            offset++;
            length--;
            if (length != 0) {
                sum += Buff.Value >> 8;
                offset++;
                length--;
            }
        }
    }
    ret->result = sum;
    return sizeof(*ret);
}


unsigned ReqRead_mem(void)
{
    read_mem_req        *acc;
    void                *ret;
    unsigned            len;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);
    len = ReadBuffer(ret, acc->mem_addr.segment, acc->mem_addr.offset, acc->len);
    return len;
}


unsigned ReqWrite_mem(void)
{
    write_mem_req       *acc;
    write_mem_ret       *ret;
    unsigned            len;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);

    len = GetTotalSize() - sizeof(*acc);

    ret->len = WriteBuffer(GetInPtr(sizeof(*acc)),
                           acc->mem_addr.segment, acc->mem_addr.offset, len);
    return sizeof(*ret);
}

static void ReadCPU(struct x86_cpu *r)
{
    r->eax = Buff.EAX;
    r->ebx = Buff.EBX;
    r->ecx = Buff.ECX;
    r->edx = Buff.EDX;
    r->esi = Buff.ESI;
    r->edi = Buff.EDI;
    r->esp = Buff.ESP;
    r->ebp = Buff.EBP;
    r->ds = Buff.DS;
    r->cs = Buff.CS;
    r->es = Buff.ES;
    r->ss = Buff.SS;
    r->fs = Buff.FS;
    r->gs = Buff.GS;
    r->eip = Buff.EIP;
    r->efl = Buff.EFlags;
    if (!Is32BitSeg(Buff.CS)) {
        r->eip &= 0xffff;
    }
    if (!Is32BitSeg(Buff.SS)) {
        r->esp &= 0xffff;
        r->ebp &= 0xffff;
    }
}

static void WriteCPU(struct x86_cpu *r)
{
    Buff.EAX = r->eax ;
    Buff.EBX = r->ebx ;
    Buff.ECX = r->ecx ;
    Buff.EDX = r->edx ;
    Buff.ESI = r->esi ;
    Buff.EDI = r->edi ;
    Buff.ESP = r->esp ;
    Buff.EBP = r->ebp ;
    Buff.DS = r->ds ;
    Buff.CS = r->cs ;
    Buff.ES = r->es ;
    Buff.SS = r->ss ;
    Buff.FS = r->fs ;
    Buff.GS = r->gs ;
    Buff.EIP = r->eip ;
    Buff.EFlags = r->efl ;
    lastSS = Buff.SS;
    lastESP = Buff.ESP;
    lastCS = Buff.CS;
    lastEIP = Buff.EIP;
}

unsigned ReqRead_cpu(void)
{
    trap_cpu_regs *regs;

    regs = GetOutPtr(0);
    memset(regs, 0, sizeof(trap_cpu_regs));
    if (Pid != 0) {
        ReadRegs(&Buff);
        ReadCPU((struct x86_cpu *)regs);
    }
    return sizeof(*regs);
}

unsigned ReqRead_fpu(void)
{
    Buff.Cmd = DBG_C_ReadCoRegs;
    Buff.Buffer = (ULONG)GetOutPtr(0);
    Buff.Value = DBG_CO_387;        /* for 2.0: DBG_CO_387 */
    Buff.Len = DBG_CO_SIZE;         /* for 2.0: size of register state */
    Buff.Index = 0;                 /* for 2.0: must be 0 */
    CallDosDebug(&Buff);
    if (Buff.Cmd == DBG_N_CoError) {
        return(0);
    } else {
        return(DBG_CO_SIZE);
    }
}

unsigned ReqWrite_cpu( void )
{
    trap_cpu_regs       *regs;

    regs = GetInPtr(sizeof(write_cpu_req));
    if (Pid != 0) {
        WriteCPU((struct x86_cpu *)regs);
        WriteRegs(&Buff);
    }
    return 0;
}

unsigned ReqWrite_fpu(void)
{
    Buff.Cmd = DBG_C_WriteCoRegs;
    Buff.Buffer = (ULONG)GetInPtr(sizeof(write_fpu_req));
    Buff.Value = DBG_CO_387;        /* for 2.0: DBG_CO_387 */
    Buff.Len = DBG_CO_SIZE;         /* for 2.0: buffer size */
    Buff.Index = 0;                 /* for 2.0: must be zero */
    CallDosDebug(&Buff);
    return 0;
}

unsigned ReqRead_regs(void)
{
    mad_registers       *mr;

    mr = GetOutPtr(0);
    memset(mr, 0, sizeof(mr->x86));
    if (Pid != 0) {
        ReadRegs(&Buff);
        ReadCPU(&mr->x86.cpu);
        Buff.Cmd = DBG_C_ReadCoRegs;
        Buff.Buffer = (ULONG)&mr->x86.fpu;
        Buff.Value = DBG_CO_387;        /* for 2.0: DBG_CO_387 */
        Buff.Len = DBG_CO_SIZE;         /* for 2.0: size of register state */
        Buff.Index = 0;                 /* for 2.0: must be 0 */
        CallDosDebug(&Buff);
    }
    return sizeof(mr->x86);
}

unsigned ReqWrite_regs(void)
{
    mad_registers       *mr;

    mr = GetInPtr(sizeof(write_regs_req));
    if (Pid != 0) {
        WriteCPU(&mr->x86.cpu);
        WriteRegs(&Buff);
        Buff.Cmd = DBG_C_WriteCoRegs;
        Buff.Buffer = (ULONG)&mr->x86.fpu;
        Buff.Value = DBG_CO_387;        /* for 2.0: DBG_CO_387 */
        Buff.Len = DBG_CO_SIZE;         /* for 2.0: buffer size */
        Buff.Index = 0;                 /* for 2.0: must be zero */
        CallDosDebug(&Buff);
    }
    return 0;
}

unsigned ReqGet_lib_name(void)
{
    get_lib_name_req    *acc;
    get_lib_name_ret    *ret;
    char                *name;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);
    if (acc->handle != 0) {
        CurrModHandle = acc->handle + 1;
    }
    if (CurrModHandle >= NumModHandles) {
        ret->handle = 0;
        return sizeof(*ret);
    }
    name = GetOutPtr(sizeof(*ret));
    Buff.Value = ModHandles[CurrModHandle];
    DosQueryModuleName(ModHandles[CurrModHandle], 128, name);
    ret->handle = CurrModHandle;
    return sizeof(*ret) + strlen(name) + 1;
}

static USHORT GetEXEFlags(char *name)
{
    HFILE       hdl;
    USHORT      type;
    ULONG       new_head;
    USHORT      exeflags;

    if (!FindNewHeader(name, &hdl, &new_head, &type)) {
        return 0;
    }
    if (type == EXE_NE) {
        SeekRead(hdl, new_head + 0x0c, &exeflags, sizeof(exeflags));
    } else if (type == EXE_LE || type == EXE_LX) {
        SeekRead(hdl, new_head + 0x10, &exeflags, sizeof(exeflags));
    } else {
        exeflags = 0;
    }
    DosClose(hdl);
    exeflags &= 0x0700;
    return exeflags;

} /* GetEXEFlags */

static bool FindLinearStartAddress(ULONG *pLin, char *name)
{
    bool        rc;
    HFILE       hdl;
    USHORT      type;
    ULONG       new_head;
    ULONG       objnum;
    USHORT      sobjn;
    ULONG       eip;
    USHORT      ip;

    if (!FindNewHeader(name, &hdl, &new_head, &type)) {
        return FALSE;
    }
    for ( ; ; ) {
        rc = FALSE;
        if (type == EXE_NE) {

            if (!SeekRead(hdl, new_head + 0x14, &ip, sizeof(ip))) {
                break;
            }
            eip = ip;
            if (!SeekRead(hdl, new_head + 0x16, &sobjn, sizeof(sobjn))) {
                break;
            }
            objnum = sobjn;

            Is32Bit = FALSE;

        } else if (type == EXE_LE || type == EXE_LX) {

            if (!SeekRead(hdl, new_head + 0x1c, &eip, sizeof(eip))) {
                break;
            }
            if (!SeekRead(hdl, new_head + 0x18, &objnum, sizeof(objnum))) {
                break;
            }

            Is32Bit = TRUE;

        } else {
            break;
        }

        Buff.Cmd = DBG_C_NumToAddr;
        Buff.Value = objnum;
        Buff.MTE = ModHandles[0];
        CallDosDebug(&Buff);
        if (Buff.Cmd != DBG_N_Success) {
            break;
        }
        *pLin = eip + Buff.Addr;

        Buff.MTE = ModHandles[0];

        rc = TRUE;
        break;
    }
    DosClose(hdl);
    return rc;

} /* FindLinearStartAddress */

static BOOL ExecuteUntilLinearAddressHit(ULONG lin)
{
    byte        saved;
    byte        breakpnt = 0xCC;
    BOOL        rc = TRUE;

    ReadLinear(&saved, lin, sizeof(byte));
    WriteLinear(&breakpnt, lin, sizeof(byte));
    do {
        ExceptNum = 0;
        DebugExecute(&Buff, DBG_C_Go, TRUE);
        if (ExceptNum == 0) {
            rc = TRUE; // dll loaded
            break;
        }
        if (ExceptNum != XCPT_BREAKPOINT) {
            rc = FALSE;
            break;
        }
    } while (ExceptLinear != lin);
    WriteLinear(&saved, lin, sizeof(byte));
    return rc;
}

void AppSession()
{
    if (!IsPMDebugger())
        DosSelectSession(SID);
}

void DebugSession()
{
    if (!IsPMDebugger())
        DosSelectSession(0);
}

unsigned ReqProg_load( void )
{
    NEWSTARTDATA        start;
    char                *parms;
    char                *end;
    char                *prog;
    char                exe_name[CCHMAXPATH];
    char                appname[CCHMAXPATH];
    ULONG               startLinear;
    prog_load_ret       *ret;

    LastMTE = 0;
    ExceptNum = -1;
    ret = GetOutPtr(0);
    AtEnd = FALSE;
    TaskFS = 0;
    prog = GetInPtr(sizeof(prog_load_req));
    if (FindFilePath(prog, exe_name, OS2ExtList) != 0) {
        exe_name[0] = '\0';
    }
    parms = AddDriveAndPath(exe_name, UtilBuff);
    while (*prog != '\0')
        ++prog;
    ++prog;
    end = (char *)GetInPtr(GetTotalSize() - 1) + 1;
    MergeArgvArray(prog, parms, end - prog);

    start.Length = offsetof(NEWSTARTDATA, IconFile); /* default for the rest */
    start.Related = 1;
    start.FgBg = !Remote;
    start.TraceOpt = 1;
    strcpy(appname, TRP_The_WATCOM_Debugger);
    strcat(appname, ": ");
    strcat(appname, exe_name);
    start.PgmTitle = (PSZ) appname;
    start.PgmName = UtilBuff;
    start.PgmInputs = parms;
    start.TermQ = 0;
    start.Environment = NULL;
    start.InheritOpt = 1;
    ret->err = 0;
    if (GetEXEFlags(UtilBuff) == EXE_IS_PM) {
        if (TypeProcess == SSF_TYPE_WINDOWABLEVIO) {
            ret->err = ERROR_NOT_IN_WINDOW;
        } else {
            start.SessionType = SSF_TYPE_PM;
            if (!IsPMDebugger())
                StartPMHelp();
        }
    } else if (TypeProcess == _PT_WINDOWABLEVIO || TypeProcess == _PT_PM) {
        start.SessionType = SSF_TYPE_WINDOWABLEVIO;
    } else if (TypeProcess == _PT_FULLSCREEN) {
        start.SessionType = SSF_TYPE_FULLSCREEN;
    }
    if (ret->err == 0) {
        ret->err = DosStartSession((void *) &start, &SID, &Pid);
    }
    if (ret->err == ERROR_SMG_START_IN_BACKGROUND) {
        ret->err = 0;
    }
    if (ret->err != 0) {
        Pid = 0;
        /* may need to do this
        ret->task_id = Pid;
        ret->thread = 1;
        ret->flags = LD_FLAG_IS_PROT;
        */
    } else {
        ret->task_id = Pid;
        ret->flags = LD_FLAG_IS_PROT;
        Buff.Pid = Pid;
        Buff.Tid = 0;
        Buff.Cmd = DBG_C_Connect;
        Buff.Value = DBG_L_386;
        CallDosDebug(&Buff);

        Buff.Pid = Pid;
        Buff.Tid = 1;
        DebugExecute(&Buff, DBG_C_Stop, FALSE);
        if (Buff.Cmd != DBG_N_Success) {
            ret->err = 14; /* can't load */
            return(sizeof(*ret));
        }
        ReadRegs(&Buff);
        CanExecTask = FALSE;
        if (FindLinearStartAddress(&startLinear, UtilBuff)) {
            if (Is32Bit) {
                ret->flags |= LD_FLAG_IS_32;
            }
            CanExecTask = ExecuteUntilLinearAddressHit(startLinear);
            ReadRegs(&Buff);
        }

        // Removed - was causing major problems on newer OS/2 versions. MN
#if 0
        if (CanExecTask) {
            dos_debug   save;

            save.Pid = Pid;
            save.Tid = 1;
            ReadRegs(&save);
            if (!CausePgmToLoadThisDLL(startLinear)) {
                CanExecTask = FALSE;
            }
            WriteRegs(&save);
        }
#endif
        Buff.Pid = Pid;
        Buff.Tid = 1;
        ReadRegs(&Buff);
        TaskFS = Buff.FS;
    }
    ret->flags |= LD_FLAG_HAVE_RUNTIME_DLLS;
    ret->mod_handle = 0;
    CurrModHandle = 1;
    return sizeof(*ret);
}

unsigned ReqProg_kill(void)
{
    prog_kill_ret       *ret;

    ret = GetOutPtr(0);
    SaveStdIn = NIL_DOS_HANDLE;
    SaveStdOut = NIL_DOS_HANDLE;
    if (Pid != 0) {
        Buff.Cmd = DBG_C_Term;
        Buff.Pid = Pid;
        CallDosDebug(&Buff);
    }
    NumModHandles = 0;
    CurrModHandle = 1;
    Pid = 0;
    ret->err = 0;
    DosSleep(500);   // Without this, it seems that restarts happen too fast
                     // and we end up running a 2nd instance of a dead task
                     // or some such sillyness.  I don't really know, but
                     // this DosSleep avoids problems when restarting a PM app
                     // ( ... Yes, this is a Hail Mary ... )
    StopPMHelp();
    return sizeof(*ret);
}

unsigned ReqSet_break(void)
{
    byte                ch;
    set_break_req       *acc;
    set_break_ret       *ret;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);
    ReadBuffer(&ch, acc->break_addr.segment, acc->break_addr.offset, sizeof(byte));
    ret->old = ch;
    ch = 0xCC;
    WriteBuffer(&ch, acc->break_addr.segment, acc->break_addr.offset, sizeof(byte));
    return sizeof(*ret);
}

unsigned ReqClear_break(void)
{
    clear_break_req     *acc;
    byte                ch;

    acc = GetInPtr(0);
    ch = acc->old;
    WriteBuffer(&ch, acc->break_addr.segment, acc->break_addr.offset, sizeof(byte));
    return 0;
}

unsigned ReqSet_watch(void)
{
    set_watch_req       *acc;
    set_watch_ret       *ret;
    dword               buff;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);
    ret->err = 1;
    if (WatchCount < MAX_WP) { // nyi - artificial limit (32 should be lots)
        WatchPoints[WatchCount].addr.segment = acc->watch_addr.segment;
        WatchPoints[WatchCount].addr.offset = acc->watch_addr.offset;
        WatchPoints[WatchCount].len = acc->size;
        ReadBuffer((char *)&buff, acc->watch_addr.segment,
                    acc->watch_addr.offset, sizeof(dword));
        WatchPoints[WatchCount].value = buff;
        DebugRegsNeeded += (acc->watch_addr.offset & (acc->size - 1)) ? 2 : 1;
        ret->err = 0;
        ++WatchCount;
    }
    ret->multiplier = 50000;
    if (ret->err == 0 && DebugRegsNeeded <= 4) {
        ret->multiplier |= USING_DEBUG_REG;
    }
    return sizeof(*ret);
}

unsigned ReqClear_watch(void)
{
    clear_watch_req  *acc;
    watch            *dst;
    watch            *src;
    int              i;


    acc = GetInPtr(0);
    dst = src = WatchPoints;
    for (i = 0; i < WatchCount; ++i) {
        if (src->addr.segment != acc->watch_addr.segment ||
                src->addr.offset != acc->watch_addr.offset) {
            *dst = *src;
            ++dst;
        } else {
            DebugExecute(&Buff, DBG_C_Stop, FALSE);
            Buff.Cmd = DBG_C_ClearWatch;
            Buff.Index = 0; // src->id;
            CallDosDebug( &Buff );
        }
        ++src;
    }
    DebugRegsNeeded -= (acc->watch_addr.offset & (acc->size - 1)) ? 2 : 1;
    --WatchCount;
    return 0;
}

static volatile bool     BrkPending;

void SetBrkPending()
{
    BrkPending = TRUE;
}

static unsigned MapReturn(unsigned conditions)
{
    if (BrkPending) {
        /* Get CS:EIP & SS:ESP correct */
        ReadRegs(&Buff);
        return conditions | COND_USER;
    }
//    Out( "Map Return - " );
//    OutNum( Buff.Cmd );
//    Out( "\r\n" );
    switch (Buff.Cmd) {
    case DBG_N_Success:
        return conditions;
    case DBG_N_AsyncStop:
        return conditions | COND_USER;
//    case DBG_N_Signal:
//        return( TRAP_USER );
    case DBG_N_SStep:
        return conditions | COND_TRACE;
    case DBG_N_Breakpoint:
        return conditions | COND_BREAK;
    case DBG_N_Exception:
        switch (ExceptNum) {
        case XCPT_PROCESS_TERMINATE:
        case XCPT_ASYNC_PROCESS_TERMINATE:
            return conditions | COND_TERMINATE;
        default:
            return conditions | COND_EXCEPTION;
        }
    case DBG_N_Watchpoint:
        return conditions | COND_WATCH;
    case DBG_N_ModuleLoad:
        return conditions | COND_LIBRARIES;
    case DBG_N_ThreadTerm:
        return conditions;
    case DBG_N_Error: // must terminate application - system semaphore locked
        Buff.Cmd = DBG_C_Term;
        Buff.Pid = Pid;
        CallDosDebug(&Buff);
    default:
        AtEnd = TRUE;
        CanExecTask = FALSE;
        return conditions | COND_TERMINATE;
    }
}

static bool setDebugRegs(void)
{
    int                 needed;
    int                 i;

    needed = 0;
    for (i = 0; i < WatchCount; ++i) {
        needed += WatchPoints[i].addr.offset & (WatchPoints[i].len - 1) ? 2 : 1;
        if (needed > 4) {
            return FALSE;
        }
    }
    for (i = 0; i < WatchCount; ++i) {
        Buff.Cmd = DBG_C_SetWatch;
        Buff.Addr = MakeItFlatNumberOne(WatchPoints[i].addr.segment,
                                        WatchPoints[i].addr.offset & ~(WatchPoints[i].len - 1));
        Buff.Len = WatchPoints[i].len;
        Buff.Index = 0;
        Buff.Value = DBG_W_Write | DBG_W_Local;
        CallDosDebug(&Buff);
        if (WatchPoints[i].addr.offset & (WatchPoints[i].len - 1)) {
            Buff.Cmd = DBG_C_SetWatch;
            Buff.Addr += WatchPoints[i].len;
            Buff.Index = 0;
            CallDosDebug(&Buff);
        }
    }
    return TRUE;
}

static void watchSingleStep(void)
{
    dos_debug           save;
    dword               memval;
    int                 i;

    DebugExecute(&Buff, DBG_C_SStep, TRUE);
    while (Buff.Cmd == DBG_N_SStep) {
        for (i = 0; i < WatchCount; ++i) {
            ReadRegs(&save);
            ReadBuffer((char *)&memval, WatchPoints[i].addr.segment,
                       WatchPoints[i].addr.offset, sizeof(memval));
            WriteRegs(&save);
            if( WatchPoints[i].value != memval ) {
                Buff.Cmd = DBG_N_Watchpoint;
                return;
            }
        }
        DebugExecute(&Buff, DBG_C_SStep, TRUE);
    }
}

/* This break handler ensures that upon hitting Ctrl-C or Ctrl-Break when
   the debugger has focus, the debuggee will be actually interrupted instead
   of killing the debugger. We do NOT handle XCPT_SIGNAL_KILLPROC, hence
   the debugger can still be killed by another app (a Good Thing).
   Note: Ctrl-C doesn't seem to be sending signals for some reason, but
   Ctrl-Break is working so it doesn't really matter.
*/
ULONG _System BreakHandler(PEXCEPTIONREPORTRECORD       pExRec,
                           PEXCEPTIONREGISTRATIONRECORD pRegRec,
                           PCONTEXTRECORD               pCtxRec,
                           PVOID                        args)
{
    switch (pExRec->ExceptionNum) {
        case XCPT_SIGNAL: {
            switch (pExRec->ExceptionInfo[0]) {
               case XCPT_SIGNAL_INTR:
               case XCPT_SIGNAL_BREAK:
                   SetBrkPending();
                   return XCPT_CONTINUE_EXECUTION;
            }
        }
    }
    /* Pass everything else further down */
    return XCPT_CONTINUE_SEARCH;
}

static unsigned progRun(bool step)
{
    prog_go_ret                 *ret;
    ULONG                       ulTimes;
    EXCEPTIONREGISTRATIONRECORD RegRec = {0}; // must be on stack!

    RegRec.ExceptionHandler = BreakHandler;

    ret = GetOutPtr(0);
    if (NumModHandles > CurrModHandle) {
        ret->conditions = COND_LIBRARIES;
        ret->stack_pointer.segment = lastSS;
        ret->stack_pointer.offset = lastESP;
        ret->program_counter.segment = lastCS;
        ret->program_counter.offset = lastEIP;
        return sizeof(*ret);
    }
    BrkPending = FALSE;

    // Set exception handler and don't forget to set signal focus!
    DosSetExceptionHandler(&RegRec);
    DosSetSignalExceptionFocus(SIG_SETFOCUS, &ulTimes);

    if (AtEnd) {
        Buff.Cmd = DBG_N_ProcTerm;
    } else if (step) {
        DebugExecute (&Buff, DBG_C_SStep, TRUE);
    } else if (!setDebugRegs()) {
        watchSingleStep();
    } else {
        DebugExecute(&Buff, DBG_C_Go, TRUE);
        if (Buff.Cmd == DBG_N_Success) {
            Buff.Cmd = DBG_N_ProcTerm;
        }
    }

    DosSetSignalExceptionFocus(SIG_UNSETFOCUS, &ulTimes);
    DosUnsetExceptionHandler(&RegRec);

    ret->conditions = (COND_CONFIG | COND_THREAD);
    if (NumModHandles > CurrModHandle) {
        ret->conditions |= COND_LIBRARIES;
    }
    ret->conditions = MapReturn(ret->conditions);
    lastSS = ret->stack_pointer.segment = Buff.SS;
    lastESP = ret->stack_pointer.offset = Buff.ESP;
    lastCS = ret->program_counter.segment = Buff.CS;
    lastEIP = ret->program_counter.offset = Buff.EIP;
    //runret->thread = Buff.Tid;
    //if (runret->returnvalue == TRAP_TERMINATE) {
    //    AtEnd = TRUE;
    //    CanExecTask = FALSE;
    //}
    return sizeof(*ret);
}

unsigned ReqProg_go(void)
{
    unsigned    rc;

    PMUnLock();
    rc = progRun(FALSE);
    PMLock(Buff.Pid, Buff.Tid);
    return rc;
}

unsigned ReqProg_step( void )
{
    unsigned    rc;

    PMUnLock();
    rc = progRun(TRUE);
    PMLock(Buff.Pid, Buff.Tid);
    return rc;
}

unsigned ReqFile_write_console()
{
    ULONG        len;
    ULONG        written_len;
    char         *ptr;
    file_write_console_ret      *ret;

    ptr = GetInPtr(sizeof(file_write_console_req));
    len = GetTotalSize() - sizeof(file_write_console_req);
    ret = GetOutPtr(0);
    if (CanExecTask) {
        /* print/program request */
        ret->len = len;
        ret->err = 0;
        TaskPrint(ptr, len);
    } else {
        ret->err = DosWrite(2, ptr, len, &written_len);
        ret->len = written_len;
    }
    return sizeof(*ret);
}

struct thd_state {
    unsigned char   dbg_state;
    unsigned char   thread_state;
    unsigned short  priority;
};

static int ValidThread(TID thread)
{
    struct  thd_state  state; /* TStat_t with IBM headers */
    TID      save;

    if (thread == 0)
        return 0;
    save = Buff.Tid;
    Buff.Tid = thread;
    Buff.Cmd = DBG_C_ThrdStat;
    Buff.Buffer = (ULONG)&state;
    Buff.Len = 4;
    CallDosDebug(&Buff);
    Buff.Tid = save;
    return Buff.Cmd == DBG_N_Success;
}

unsigned ReqThread_get_next(void)
{
    thread_get_next_req *acc;
    thread_get_next_ret *ret;
    TID                 thread;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);

    if (Pid != 0) {
        thread = acc->thread;
        while (++thread <= 256) {
            if (ValidThread(thread)) {
                ret->thread = thread;
                //NYI:Assume all threads can be run
                ret->state = THREAD_THAWED;
                return sizeof(*ret);
            }
        }
    }
    ret->thread = (acc->thread == 0) ? 1 : 0;
    return sizeof(*ret);
}

unsigned ReqThread_set(void)
{
    thread_set_req      *acc;
    thread_set_ret      *ret;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);
    ret->err = 0;
    ret->old_thread = Buff.Tid;
    if (ValidThread(acc->thread)) {
        Buff.Pid = Pid;
        Buff.Tid = acc->thread;
        Buff.Cmd = DBG_C_ReadReg;
        CallDosDebug(&Buff);
    } else if (acc->thread != 0) {
        ret->err = 1;
    }
    return sizeof(*ret);
}

typedef enum {                  /* values for .cmd field */
        PT_CMD_READ_MEM_I = 1,  /* read I-space */
        PT_CMD_READ_MEM_D,      /* read D-space */
        PT_CMD_READ_REGS,       /* read registers */
        PT_WRITE_CMD_MEM_I,     /* write I-space */
        PT_CMD_WRITE_MEM_D,     /* write D-space */
        PT_CMD_WRITE_REGS,      /* write registers */
        PT_CMD_GO,              /* go (with signal) */
        PT_CMD_TERMINATE,       /* terminate child process */
        PT_CMD_SINGLE_STEP,     /* single step */
        PT_CMD_STOP,            /* stop child process */
        PT_CMD_FREEZE,          /* freeze child process */
        PT_CMD_RESUME,          /* resume child process */
        PT_CMD_SEG_TO_SEL,      /* resume child process */
        PT_CMD_READ_8087,       /* read npx */
        PT_CMD_WRITE_8087,      /* write npx */
        PT_CMD_GET_LIB_NAME,    /* get library module name */
        PT_CMD_THREAD_STAT,     /* get thread status */
#if 0   /* depends on which documentation you believe */
        PT_CMD_READ_MEM_B,      /* read memory block */
        PT_CMD_WRITE_MEM_B,     /* write memory block */
#else
        PT_CMD_MAP_RO_ALIAS,    /* create a read only segment alias */
        PT_CMD_MAP_WR_ALIAS,    /* create a read/write segment alias */
        PT_CMD_UNMAP_ALIAS      /* unmap a segment alias */
#endif
} trace_codes;

static unsigned DoThread(trace_codes code)
{
    TID                 save;
    thread_thaw_req     *acc;
    thread_thaw_ret     *ret;

    acc = GetInPtr(0);
    ret = GetOutPtr(0);
    if (ValidThread(acc->thread)) {
        save = Buff.Tid;
        Buff.Pid = Pid;
        Buff.Tid = acc->thread;
        Buff.Cmd = code;
        CallDosDebug(&Buff);
        Buff.Tid = save;
        ret->err = 0;
    } else {
        ret->err = 1;   // failed
    }
    return sizeof(*ret);
}

unsigned ReqThread_freeze(void)
{
    return DoThread(DBG_C_Freeze);
}

unsigned ReqThread_thaw(void)
{
    return DoThread(DBG_C_Resume);
}

unsigned ReqGet_message_text()
{
    get_message_text_ret        *ret;
    char                        *err_txt;

    ret = GetOutPtr(0);
    err_txt = GetOutPtr(sizeof(*ret));
    if (ExceptNum == -1) {
        err_txt[0] = '\0';
    } else {
        strcpy(err_txt, GetExceptionText());
    }
    ExceptNum = -1;
    ret->flags = MSG_NEWLINE | MSG_ERROR;
    return sizeof(*ret) + strlen(err_txt) + 1;
}

unsigned ReqGet_next_alias()
{
    get_next_alias_req  *acc;
    get_next_alias_ret  *ret;

    ret = GetOutPtr(0);
    ret->seg = 0;
    ret->alias = 0;
    acc = GetInPtr(0);
    if (Is32Bit && acc->seg == 0) {
        ret->seg = FlatCS;
        ret->alias = FlatDS;
    }
    return sizeof(*ret);
}

void TRAPENTRY TellHandles(HAB hab, HWND hwnd)
{
    TellSoftModeHandles(hab, hwnd);
}

char TRAPENTRY TellHardMode(char hard)
{
    return SetHardMode(hard);
}

trap_version TRAPENTRY TrapInit(char *parm, char *err, bool remote)
{
    trap_version        ver;
    PTIB                ptib;
    PPIB                ppib;

    parm = parm;
    Remote = remote;
    err[0] = '\0';
    ver.major = TRAP_MAJOR_VERSION;
    ver.minor = TRAP_MINOR_VERSION;
    ver.remote = FALSE;
    SaveStdIn = NIL_DOS_HANDLE;
    SaveStdOut = NIL_DOS_HANDLE;
    Screen = DEBUG_SCREEN;
    if (parm[0] == '2') {
        stopOnSecond = TRUE;
    }

    DosGetInfoBlocks(&ptib, &ppib);
    TypeProcess = ppib->pib_ultype;

    InitSoftDebug();
    InitDebugThread();

    return ver;
}

void TRAPENTRY TrapFini(void)
{
}

/* An empty DLL initialization/termination funcion... just to prevent
 * possible misunderstandings with the linker/loader - and in case we
 * need it later.
 */
ULONG _System _DLL_InitTerm (ULONG modhandle, ULONG flag)
{
    /* If flag is zero then the DLL is being loaded so initialization  */
    /* should be performed.  If flag is 1 then the DLL is being freed  */
    /* so termination should be performed.                             */

    switch (flag)
       {
       case 0:
          break;

       case 1:
          break;
       }

    /* A nonzero value must be returned to indicate success. */
    return 1;
}

