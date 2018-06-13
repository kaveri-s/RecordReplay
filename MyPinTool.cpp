/*BEGIN_LEGAL
Intel Open Source License
Copyright (c) 2002-2015 Intel Corporation. All rights reserved.

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
// This tool demonstrates the use of the PIN_GetContextRegval API for various types of registers.
// It is used with the regval_app application.

#include <fstream>
#include <string>
#include <iostream>
#include <cassert>
#include <iomanip>
#include "pin.H"
#include "../Utils/regvalue_utils.h"

#define BEFORE 0
#define AFTER 1

using std::ofstream;


/////////////////////
// GLOBAL VARIABLES
/////////////////////

// A knob for defining the output file name

// ofstream object for handling the output
ofstream OutFile;

//For basic blocks
UINT64 insCount = 0;        //number of dynamically executed instructions
UINT64 bblCount = 0;        //number of dynamically executed basic blocks
std::ostream * out = &cerr;

//for register Deltas
ADDRINT regval[16];

/* ===================================================================== */
// Command line switches
/* ===================================================================== */


KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "regval.out", "specify output file name");

KNOB<BOOL>   KnobCount(KNOB_MODE_WRITEONCE,  "pintool",
    "count", "1", "count instructions, basic blocks and threads in the application");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    cerr << "This tool prints out the number of dynamically executed " << endl <<
            "instructions, basic blocks and threads in the application." << endl << endl;

    cerr << KNOB_BASE::StringKnobSummary() << endl;

    return -1;
}

VOID AddRegValToArr(int reg, ADDRINT val) {
    regval[reg-3]=val;
}

ADDRINT GetRegValFromArr(int reg) {
    return regval[reg-3];
}

/////////////////////
// ANALYSIS FUNCTIONS
/////////////////////

static void PrintRegisters(const CONTEXT * ctxt, int Order)
{
    // static const UINT stRegSize = REG_Size(REG_ST_BASE);
    for (int reg = (int)REG_GR_BASE; reg <= (int)REG_GR_LAST; ++reg)
    {
        // For the integer registers, it is safe to use ADDRINT. But make sure to pass a pointer to it.
        ADDRINT val;
        PIN_GetContextRegval(ctxt, (REG)reg, reinterpret_cast<UINT8*>(&val));
        // OutFile << reg << "..." << REG_StringShort((REG)reg) << ": 0x" << hex << val << endl;
        if(Order==BEFORE)
            AddRegValToArr(reg, val);
        else {
            ADDRINT oldval = GetRegValFromArr(reg);
            if(oldval!=val)
                OutFile << REG_StringShort((REG)reg) << "\t0x" << setw(16) << left << hex << oldval << "\t" << "0x" << hex << val << endl;
        }


    }
    for (int reg = (int)REG_ST_BASE; reg <= (int)REG_ST_LAST; ++reg)
    {
        // For the x87 FPU stack registers, using PIN_REGISTER ensures a large enough buffer.
        PIN_REGISTER val;
        PIN_GetContextRegval(ctxt, (REG)reg, reinterpret_cast<UINT8*>(&val));
        // OutFile << REG_StringShort((REG)reg) << ": " << to_string(stRegSize) << endl;
    }
}


VOID BeforeBbl(const CONTEXT * ctxt, UINT32 numInstInBbl, VOID *ip)
{
    OutFile <<  "===============================================" << endl;
    OutFile << "This is the basic block at Address: " << static_cast<std::string *>(ip) <<endl;
    // OutFile <<  "-----------------------------------------------" << endl;
    // OutFile << "Before Basic Block" << endl;
    PrintRegisters(ctxt, BEFORE);
    bblCount++;
    insCount += numInstInBbl;
}

VOID AfterBbl(const CONTEXT * ctxt, VOID *ip)
{
    OutFile <<  "-----------------------------------------------" << endl;
    OutFile << "After Basic Block at Address: " << static_cast<std::string *>(ip) << endl;
    OutFile << "Reg\t" << "Old Val\t\t\t" << "New Val"<< endl;
    PrintRegisters(ctxt, AFTER);
    OutFile <<  "===============================================" << endl;
}


/////////////////////
// INSTRUMENTATION FUNCTIONS
/////////////////////

static VOID Trace(TRACE trace, VOID *v)
{
    // Visit every basic block in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        INS ins = BBL_InsTail(bbl);
        // Insert a call to CountBbl() before every basic bloc, passing the number of instructions
        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)BeforeBbl, IARG_CONST_CONTEXT, IARG_UINT32, BBL_NumIns(bbl), IARG_INST_PTR, IARG_END);

        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)AfterBbl, IARG_CONST_CONTEXT, IARG_INST_PTR, IARG_END);
    }
}


static VOID Fini(INT32 code, VOID *v)
{
    *out <<  "===============================================" << endl;
    *out <<  "MyPinTool analysis results: " << endl;
    *out <<  "Number of instructions: " << insCount  << endl;
    *out <<  "Number of basic blocks: " << bblCount  << endl;
    *out <<  "===============================================" << endl;
    OutFile.close();
}


/////////////////////
// MAIN FUNCTION
/////////////////////

int main(int argc, char * argv[])
{
    // Initialize Pin
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    // Open the output file
    OutFile.open(KnobOutputFile.Value().c_str());

    // Add instrumentation
    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Start running the application
    PIN_StartProgram(); // never returns

    return 0;
}
