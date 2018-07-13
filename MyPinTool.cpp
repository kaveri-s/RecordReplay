/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2017 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
//
// This tool counts the number of times a routine is executed and 
// the number of instructions executed in a routine
//

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string.h>
#include "pin.H"
#include <cassert>
#include "../Utils/regvalue_utils.h"


#define BEFORE 0
#define AFTER 1

ofstream outFile;

// Holds instruction count for a single procedure
typedef struct RtnCount
{
    string _name;
    string _image;
    ADDRINT _address;
    RTN _rtn;
    UINT64 _rtnCount;
    UINT64 _icount;
    UINT64 _memacc;
    struct RtnCount * _next;
} RTN_COUNT;

// Linked list of instruction counts for each routine
RTN_COUNT * RtnList = 0;

//for register Deltas
ADDRINT regval[24];

// This function is called before every instruction is executed
VOID docount(UINT64 * counter)
{
    (*counter)++;
}

/* ===================================================================== */
// Command line switches
/* ===================================================================== */


KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "proccount.out", "specify output file name");

KNOB<BOOL>   KnobCount(KNOB_MODE_WRITEONCE,  "pintool",
    "count", "1", "count instructions, basic blocks and threads in the application");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

VOID AddRegValToArr(int reg, ADDRINT val) {
    if(reg<16)
        regval[reg-3] = val;
    else
        regval[reg-55+16] = val;
}

ADDRINT GetRegValFromArr(int reg) {
    if(reg<16)
        return regval[reg-3];
    else
        return regval[reg-55+16];
}

const char * StripPath(const char * path)
{
    const char * file = strrchr(path,'/');
    if (file)
        return file+1;
    else
        return path;
}


/////////////////////
// ANALYSIS FUNCTIONS
/////////////////////


FILE * trace;

// Print a memory read record
static void RecordMemRead(VOID * ip, VOID * addr)
{
    outFile << ip << ": R" << addr << endl;
    // fprintf(trace,"%p: R %p\n", ip, addr);
}

// Print a memory write record
static void RecordMemWrite(VOID * ip, VOID * addr)
{
    outFile << ip << ": R" << addr << endl;
    // fprintf(trace,"%p: W %p\n", ip, addr);
}


static void PrintRegisters(const CONTEXT * ctxt, int Order)
{
    // static const UINT stRegSize = REG_Size(REG_ST_BASE);
    for (int reg = (int)REG_GR_BASE; reg <= (int)REG_GR_LAST; ++reg)
    {
        // For the integer registers, it is safe to use ADDRINT. But make sure to pass a pointer to it.
        ADDRINT val;
        PIN_GetContextRegval(ctxt, (REG)reg, reinterpret_cast<UINT8*>(&val));
        // OutFile << reg << "..." << REG_StringShort((REG)reg) << ": 0x" << hex << val << endl;
        if(Order==BEFORE) {
            // outFile << REG_StringShort((REG)reg) << "\t0x" << setw(16) << left << hex << val << endl;
            AddRegValToArr(reg, val);
        }
        else {
            ADDRINT oldval = GetRegValFromArr(reg);
            if(oldval!=val)
                outFile << REG_StringShort((REG)reg) << "\t0x" << setw(16) << left << hex << oldval << "\t" << "0x" << hex << val << endl;
        }


    }
    for (int reg = (int)REG_XMM_BASE; reg < (int)REG_XMM7; ++reg)
    {
        // For the x87 FPU stack registers, using PIN_REGISTER ensures a large enough buffer.
        PIN_REGISTER val;
        PIN_GetContextRegval(ctxt, (REG)reg, reinterpret_cast<UINT8*>(&val));
        // if(Order==BEFORE)
        //     AddRegValToArr(reg, val);
        // else {
        //     ADDRINT oldval = GetRegValFromArr(reg);
        //     if(oldval!=val)
        //         OutFile << REG_StringShort((REG)reg) << "\t0x" << setw(16) << left << hex << oldval << "\t" << "0x" << hex << val << endl;
        // }
        // OutFile << REG_StringShort((REG)reg) << ": " << to_string(stRegSize) << endl;
    }
}


VOID BeforeRoutine(const CONTEXT * ctxt, RTN rtn)
{
    outFile <<  "===============================================" << endl;
    outFile << "This is the Routine at Address: \n" << RTN_Address(rtn) << dec <<endl;
    outFile << "-----------------------------------------------" << endl;
    outFile << "Memory Accesses:" << endl;
    // OutFile <<  "-----------------------------------------------" << endl;
    // OutFile << "Before Basic Block" << endl;
    PrintRegisters(ctxt, BEFORE);
}

VOID AfterRoutine(const CONTEXT * ctxt)
{
    outFile <<  "-----------------------------------------------" << endl;
    outFile << "After Routine" << endl;
    outFile << "Reg\t" << "Old Val\t\t\t" << "New Val"<< endl;
    PrintRegisters(ctxt, AFTER);
    outFile <<  "===============================================" << endl;
}

// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID *v)
{
    
    // Allocate a counter for this routine
    RTN_COUNT * rc = new RTN_COUNT;

    // The RTN goes away when the image is unloaded, so save it now
    // because we need it in the fini
    rc->_name = RTN_Name(rtn);
    rc->_image = StripPath(IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str());
    rc->_address = RTN_Address(rtn);
    rc->_icount = 0;
    rc->_rtnCount = 0;
    rc->_memacc = 0;

    // Add to list of routines
    rc->_next = RtnList;
    RtnList = rc;

    RTN_Open(rtn);

    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_rtnCount), IARG_END);
    // Insert a call at the entry point of a routine to increment the call count
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)BeforeRoutine, IARG_CONST_CONTEXT, IARG_UINT32, rtn, IARG_END);
    
    INS ins = RTN_InsHead(rtn);
    INS prev = ins;

    

    // For each instruction of the routine
    for (; INS_Valid(ins); ins = INS_Next(ins))
    {
        UINT32 memOperands = INS_MemoryOperandCount(ins);

        // Iterate over each memory operand of the instruction.
        for (UINT32 memOp = 0; memOp < memOperands; memOp++)
        {
            if (INS_MemoryOperandIsRead(ins, memOp))
            {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                    IARG_INST_PTR,
                    IARG_MEMORYOP_EA, memOp,
                    IARG_END);
                INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_memacc), IARG_END);

            }
            // Note that in some architectures a single memory operand can be 
            // both read and written (for instance incl (%eax) on IA-32)
            // In that case we instrument it once for read and once for write.
            if (INS_MemoryOperandIsWritten(ins, memOp))
            {
                INS_InsertPredicatedCall(
                    ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                    IARG_INST_PTR,
                    IARG_MEMORYOP_EA, memOp,
                    IARG_END);
                INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_memacc), IARG_END);
            }
        }
        // Insert a call to docount to increment the instruction counter for this rtn
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_PTR, &(rc->_icount), IARG_END);
        prev=ins;
    }

    INS_InsertCall(prev, IPOINT_BEFORE, (AFUNPTR)AfterRoutine, IARG_CONST_CONTEXT, IARG_END);

    RTN_Close(rtn);
}

// This function is called when the application exits
// It prints the name and count for each procedure
VOID Fini(INT32 code, VOID *v)
{
    outFile << setw(18) << "Address" << " "
          << setw(12) << "Calls" << " "
          << setw(12) << "Instructions" << " "
          << setw(12) << "Memory Accesses" << " "
          << setw(23) << "Procedure" << " "
          << setw(15) << "Image" << " " << endl;

    for (RTN_COUNT * rc = RtnList; rc; rc = rc->_next)
    {
        if (rc->_icount > 0)
            outFile << setw(18) << hex << rc->_address << dec << " "
                  << setw(12) << rc->_rtnCount << " "
                  << setw(12) << rc->_icount << " "
                  << setw(12) << rc->_memacc << " "
                  << setw(23) << rc->_name << " "
                  << setw(15) << rc->_image << endl;
    }

    // fprintf(trace, "#eof\n");
    // fclose(trace);
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This Pintool counts the number of times a routine is executed" << endl;
    cerr << "and the number of instructions executed in a routine" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    // trace = fopen("pinatrace.out", "w");
    // Initialize symbol table code, needed for rtn instrumentation
    PIN_InitSymbols();

    outFile.open("mypintool.out");

    // Initialize pin
    if (PIN_Init(argc, argv)) return Usage();

    // Register Routine to be called to instrument rtn
    RTN_AddInstrumentFunction(Routine, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}

