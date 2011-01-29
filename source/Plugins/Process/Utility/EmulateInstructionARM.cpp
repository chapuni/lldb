//===-- EmulateInstructionARM.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "EmulateInstructionARM.h"
#include "ARMDefines.h"
#include "ARMUtils.h"

using namespace lldb;
using namespace lldb_private;

// ARM constants used during decoding
#define REG_RD          0
#define LDM_REGLIST     1
#define PC_REG          15
#define PC_REGLIST_BIT  0x8000

#define ARMv4     (1u << 0)
#define ARMv4T    (1u << 1)
#define ARMv5T    (1u << 2)
#define ARMv5TE   (1u << 3)
#define ARMv5TEJ  (1u << 4)
#define ARMv6     (1u << 5)
#define ARMv6K    (1u << 6)
#define ARMv6T2   (1u << 7)
#define ARMv7     (1u << 8)
#define ARMv8     (1u << 9)
#define ARMvAll   (0xffffffffu)

typedef enum
{
    eEncodingA1,
    eEncodingA2,
    eEncodingA3,
    eEncodingA4,
    eEncodingA5,
    eEncodingT1,
    eEncodingT2,
    eEncodingT3,
    eEncodingT4,
    eEncodingT5,
} ARMEncoding;

typedef enum
{
    eSize16,
    eSize32
} ARMInstrSize;

// Typedef for the callback function used during the emulation.
// Pass along (ARMEncoding)encoding as the callback data.
typedef bool (*EmulateCallback) (EmulateInstructionARM *emulator, ARMEncoding encoding);
    
typedef struct
{
    uint32_t mask;
    uint32_t value;
    uint32_t variants;
    ARMEncoding encoding;
    ARMInstrSize size;
    EmulateCallback callback;
    const char *name;
}  ARMOpcode;

static bool 
emulate_push (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); 
        NullCheckIfThumbEE(13); 
        address = SP - 4*BitCount(registers);

        for (i = 0 to 14)
        {
            if (registers<i> == ’1’)
            {
                if i == 13 && i != LowestSetBit(registers) // Only possible for encoding A1 
                    MemA[address,4] = bits(32) UNKNOWN;
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
            }
        }

        if (registers<15> == ’1’) // Only possible for encoding A1 or A2 
            MemA[address,4] = PCStoreValue();
        
        SP = SP - 4*BitCount(registers);
    }
#endif

    bool success = false;
    const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (emulator->ConditionPassed())
    {
        const uint32_t addr_byte_size = emulator->GetAddressByteSize();
        const addr_t sp = emulator->ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, 0, &success);
        if (!success)
            return false;
        uint32_t registers = 0;
        uint32_t Rt; // the source register
        switch (encoding) {
        case eEncodingT1:
            registers = Bits32(opcode, 7, 0);
            // The M bit represents LR.
            if (Bits32(opcode, 8, 8))
                registers |= 0x000eu;
            // if BitCount(registers) < 1 then UNPREDICTABLE;
            if (BitCount(registers) < 1)
                return false;
            break;
        case eEncodingT2:
            // Ignore bits 15 & 13.
            registers = Bits32(opcode, 15, 0) & ~0xa000;
            // if BitCount(registers) < 2 then UNPREDICTABLE;
            if (BitCount(registers) < 2)
                return false;
            break;
        case eEncodingT3:
            Rt = Bits32(opcode, 15, 12);
            // if BadReg(t) then UNPREDICTABLE;
            if (BadReg(Rt))
                return false;
            registers = (1u << Rt);
            break;
        case eEncodingA1:
            registers = Bits32(opcode, 15, 0);
            // Instead of return false, let's handle the following case as well,
            // which amounts to pushing one reg onto the full descending stacks.
            // if BitCount(register_list) < 2 then SEE STMDB / STMFD;
            break;
        case eEncodingA2:
            Rt = Bits32(opcode, 15, 12);
            // if t == 13 then UNPREDICTABLE;
            if (Rt == dwarf_sp)
                return false;
            registers = (1u << Rt);
            break;
        default:
            return false;
        }
        addr_t sp_offset = addr_byte_size * BitCount (registers);
        addr_t addr = sp - sp_offset;
        uint32_t i;
        
        EmulateInstruction::Context context = { EmulateInstruction::eContextPushRegisterOnStack, eRegisterKindDWARF, 0, 0 };
        for (i=0; i<15; ++i)
        {
            if (BitIsSet (registers, 1u << i))
            {
                context.arg1 = dwarf_r0 + i;    // arg1 in the context is the DWARF register number
                context.arg2 = addr - sp;       // arg2 in the context is the stack pointer offset
                uint32_t reg_value = emulator->ReadRegisterUnsigned(eRegisterKindDWARF, context.arg1, 0, &success);
                if (!success)
                    return false;
                if (!emulator->WriteMemoryUnsigned (context, addr, reg_value, addr_byte_size))
                    return false;
                addr += addr_byte_size;
            }
        }
        
        if (BitIsSet (registers, 1u << 15))
        {
            context.arg1 = dwarf_pc;    // arg1 in the context is the DWARF register number
            context.arg2 = addr - sp;   // arg2 in the context is the stack pointer offset
            const uint32_t pc = emulator->ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, 0, &success);
            if (!success)
                return false;
            if (!emulator->WriteMemoryUnsigned (context, addr, pc + 8, addr_byte_size))
                return false;
        }
        
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.arg0 = eRegisterKindGeneric;
        context.arg1 = LLDB_REGNUM_GENERIC_SP;
        context.arg2 = -sp_offset;
    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, sp - sp_offset))
            return false;
    }
    return true;
}

// Set r7 or ip to point to saved value residing within the stack.
// ADD (SP plus immediate)
static bool
emulate_add_rd_sp_imm (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, imm32, ‘0’);
        if d == 15 then
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (emulator->ConditionPassed())
    {
        const addr_t sp = emulator->ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, 0, &success);
        if (!success)
            return false;
        uint32_t Rd; // the destination register
        uint32_t imm32;
        switch (encoding) {
        case eEncodingT1:
            Rd = 7;
            imm32 = Bits32(opcode, 7, 0) << 2; // imm32 = ZeroExtend(imm8:'00', 32)
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
            break;
        default:
            return false;
        }
        addr_t sp_offset = imm32;
        addr_t addr = sp + sp_offset; // a pointer to the stack area
        
        EmulateInstruction::Context context = { EmulateInstruction::eContextRegisterPlusOffset,
                                                eRegisterKindGeneric,
                                                LLDB_REGNUM_GENERIC_SP,
                                                sp_offset };
    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + Rd, addr))
            return false;
    }
    return true;
}

// Set r7 or ip to the current stack pointer.
// MOV (register)
static bool
emulate_mov_rd_sp (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        result = R[m];
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                // APSR.C unchanged
                // APSR.V unchanged
    }
#endif

    bool success = false;
    //const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    //if (!success)
    //    return false;

    if (emulator->ConditionPassed())
    {
        const addr_t sp = emulator->ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, 0, &success);
        if (!success)
            return false;
        uint32_t Rd; // the destination register
        switch (encoding) {
        case eEncodingT1:
            Rd = 7;
            break;
        case eEncodingA1:
            Rd = 12;
            break;
        default:
            return false;
        }
        EmulateInstruction::Context context = { EmulateInstruction::eContextRegisterPlusOffset,
                                                eRegisterKindGeneric,
                                                LLDB_REGNUM_GENERIC_SP,
                                                0 };
    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + Rd, sp))
            return false;
    }
    return true;
}

// Move from high register (r8-r15) to low register (r0-r7).
// MOV (register)
static bool
emulate_mov_low_high (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        result = R[m];
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                // APSR.C unchanged
                // APSR.V unchanged
    }
#endif

    bool success = false;
    const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (emulator->ConditionPassed())
    {
        uint32_t Rm; // the source register
        uint32_t Rd; // the destination register
        switch (encoding) {
        case eEncodingT1:
            Rm = Bits32(opcode, 6, 3);
            Rd = Bits32(opcode, 2, 1); // bits(7) == 0
            break;
        default:
            return false;
        }
        int32_t reg_value = emulator->ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + Rm, 0, &success);
        if (!success)
            return false;
        
        // The context specifies that Rm is to be moved into Rd.
        EmulateInstruction::Context context = { EmulateInstruction::eContextRegisterPlusOffset,
                                                eRegisterKindDWARF,
                                                dwarf_r0 + Rm,
                                                0 };
    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + Rd, reg_value))
            return false;
    }
    return true;
}

// PC relative immediate load into register, possibly followed by ADD (SP plus register).
// LDR (literal)
static bool
emulate_ldr_rd_pc_rel (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); NullCheckIfThumbEE(15);
        base = Align(PC,4);
        address = if add then (base + imm32) else (base - imm32);
        data = MemU[address,4];
        if t == 15 then
            if address<1:0> == ‘00’ then LoadWritePC(data); else UNPREDICTABLE;
        elsif UnalignedSupport() || address<1:0> = ‘00’ then
            R[t] = data;
        else // Can only apply before ARMv7
            if CurrentInstrSet() == InstrSet_ARM then
                R[t] = ROR(data, 8*UInt(address<1:0>));
            else
                R[t] = bits(32) UNKNOWN;
    }
#endif

    bool success = false;
    const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (emulator->ConditionPassed())
    {
        const uint32_t pc = emulator->ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, 0, &success);
        if (!success)
            return false;

        // PC relative immediate load context
        EmulateInstruction::Context context = {EmulateInstruction::eContextRegisterPlusOffset,
                                               eRegisterKindGeneric,
                                               LLDB_REGNUM_GENERIC_PC,
                                               0};
        uint32_t Rd; // the destination register
        uint32_t imm32; // immediate offset from the PC
        addr_t addr;    // the PC relative address
        uint32_t data;  // the literal data value from the PC relative load
        switch (encoding) {
        case eEncodingT1:
            Rd = Bits32(opcode, 10, 8);
            imm32 = Bits32(opcode, 7, 0) << 2; // imm32 = ZeroExtend(imm8:'00', 32);
            addr = pc + 4 + imm32;
            context.arg2 = 4 + imm32;
            break;
        default:
            return false;
        }
        data = emulator->ReadMemoryUnsigned(context, addr, 4, 0, &success);
        if (!success)
            return false;    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + Rd, data))
            return false;
    }
    return true;
}

// An add operation to adjust the SP.
// ADD (SP plus register)
static bool
emulate_add_sp_rm (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(SP, shifted, ‘0’);
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (emulator->ConditionPassed())
    {
        const addr_t sp = emulator->ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, 0, &success);
        if (!success)
            return false;
        uint32_t Rm; // the second operand
        switch (encoding) {
        case eEncodingT2:
            Rm = Bits32(opcode, 6, 3);
            break;
        default:
            return false;
        }
        int32_t reg_value = emulator->ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + Rm, 0, &success);
        if (!success)
            return false;

        addr_t addr = (int32_t)sp + reg_value; // the adjusted stack pointer value
        
        EmulateInstruction::Context context = { EmulateInstruction::eContextAdjustStackPointer,
                                                eRegisterKindGeneric,
                                                LLDB_REGNUM_GENERIC_SP,
                                                reg_value };
    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, addr))
            return false;
    }
    return true;
}

// Set r7 to point to some ip offset.
// SUB (immediate)
static bool
emulate_sub_r7_ip_imm (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, NOT(imm32), ‘1’);
        if d == 15 then // Can only occur for ARM encoding
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (emulator->ConditionPassed())
    {
        const addr_t ip = emulator->ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r12, 0, &success);
        if (!success)
            return false;
        uint32_t imm32;
        switch (encoding) {
        case eEncodingA1:
            imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
            break;
        default:
            return false;
        }
        addr_t ip_offset = imm32;
        addr_t addr = ip - ip_offset; // the adjusted ip value
        
        EmulateInstruction::Context context = { EmulateInstruction::eContextRegisterPlusOffset,
                                                eRegisterKindDWARF,
                                                dwarf_r12,
                                                -ip_offset };
    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r7, addr))
            return false;
    }
    return true;
}

// Set ip to point to some stack offset.
// SUB (SP minus immediate)
static bool
emulate_sub_ip_sp_imm (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, NOT(imm32), ‘1’);
        if d == 15 then // Can only occur for ARM encoding
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (emulator->ConditionPassed())
    {
        const addr_t sp = emulator->ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, 0, &success);
        if (!success)
            return false;
        uint32_t imm32;
        switch (encoding) {
        case eEncodingA1:
            imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
            break;
        default:
            return false;
        }
        addr_t sp_offset = imm32;
        addr_t addr = sp - sp_offset; // the adjusted stack pointer value
        
        EmulateInstruction::Context context = { EmulateInstruction::eContextRegisterPlusOffset,
                                                eRegisterKindGeneric,
                                                LLDB_REGNUM_GENERIC_SP,
                                                -sp_offset };
    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r12, addr))
            return false;
    }
    return true;
}

// A sub operation to adjust the SP -- allocate space for local storage.
static bool
emulate_sub_sp_imm (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, NOT(imm32), ‘1’);
        if d == 15 then // Can only occur for ARM encoding
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (emulator->ConditionPassed())
    {
        const addr_t sp = emulator->ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, 0, &success);
        if (!success)
            return false;
        uint32_t imm32;
        switch (encoding) {
        case eEncodingT1:
            imm32 = ThumbImmScaled(opcode); // imm32 = ZeroExtend(imm7:'00', 32)
        case eEncodingT2:
            imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
            break;
        case eEncodingT3:
            imm32 = ThumbImm12(opcode); // imm32 = ZeroExtend(i:imm3:imm8, 32)
            break;
        case eEncodingA1:
            imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
            break;
        default:
            return false;
        }
        addr_t sp_offset = imm32;
        addr_t addr = sp - sp_offset; // the adjusted stack pointer value
        
        EmulateInstruction::Context context = { EmulateInstruction::eContextAdjustStackPointer,
                                                eRegisterKindGeneric,
                                                LLDB_REGNUM_GENERIC_SP,
                                                -sp_offset };
    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, addr))
            return false;
    }
    return true;
}

// A store operation to the stacks that also updates the SP.
static bool
emulate_str_rt_sp (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
        address = if index then offset_addr else R[n];
        MemU[address,4] = if t == 15 then PCStoreValue() else R[t];
        if wback then R[n] = offset_addr;
    }
#endif

    bool success = false;
    const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (emulator->ConditionPassed())
    {
        const uint32_t addr_byte_size = emulator->GetAddressByteSize();
        const addr_t sp = emulator->ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, 0, &success);
        if (!success)
            return false;
        uint32_t Rt; // the source register
        uint32_t imm12;
        switch (encoding) {
        case eEncodingA1:
            Rt = Bits32(opcode, 15, 12);
            imm12 = Bits32(opcode, 11, 0);
            break;
        default:
            return false;
        }
        addr_t sp_offset = imm12;
        addr_t addr = sp - sp_offset;
        
        EmulateInstruction::Context context = { EmulateInstruction::eContextPushRegisterOnStack, eRegisterKindDWARF, 0, 0 };
        if (Rt != 15)
        {
            context.arg1 = dwarf_r0 + Rt;    // arg1 in the context is the DWARF register number
            context.arg2 = addr - sp;        // arg2 in the context is the stack pointer offset
            uint32_t reg_value = emulator->ReadRegisterUnsigned(eRegisterKindDWARF, context.arg1, 0, &success);
            if (!success)
                return false;
            if (!emulator->WriteMemoryUnsigned (context, addr, reg_value, addr_byte_size))
                return false;
        }
        else
        {
            context.arg1 = dwarf_pc;    // arg1 in the context is the DWARF register number
            context.arg2 = addr - sp;   // arg2 in the context is the stack pointer offset
            const uint32_t pc = emulator->ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, 0, &success);
            if (!success)
                return false;
            if (!emulator->WriteMemoryUnsigned (context, addr, pc + 8, addr_byte_size))
                return false;
        }
        
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.arg0 = eRegisterKindGeneric;
        context.arg1 = LLDB_REGNUM_GENERIC_SP;
        context.arg2 = -sp_offset;
    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, sp - sp_offset))
            return false;
    }
    return true;
}

static bool 
emulate_vpush (EmulateInstructionARM *emulator, ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); CheckVFPEnabled(TRUE); NullCheckIfThumbEE(13);
        address = SP - imm32;
        SP = SP - imm32;
        if single_regs then
            for r = 0 to regs-1
                MemA[address,4] = S[d+r]; address = address+4;
        else
            for r = 0 to regs-1
                // Store as two word-aligned words in the correct order for current endianness.
                MemA[address,4] = if BigEndian() then D[d+r]<63:32> else D[d+r]<31:0>;
                MemA[address+4,4] = if BigEndian() then D[d+r]<31:0> else D[d+r]<63:32>;
                address = address+8;
    }
#endif

    bool success = false;
    const uint32_t opcode = emulator->OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (emulator->ConditionPassed())
    {
        const uint32_t addr_byte_size = emulator->GetAddressByteSize();
        const addr_t sp = emulator->ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, 0, &success);
        if (!success)
            return false;
        bool single_regs;
        uint32_t d;     // UInt(Vd:D) starting register
        uint32_t imm32; // stack offset
        uint32_t regs;  // number of registers
        switch (encoding) {
        case eEncodingT1:
        case eEncodingA1:
            single_regs = false;
            d = Bits32(opcode, 15, 12) << 1 | Bits32(opcode, 22, 22);
            imm32 = Bits32(opcode, 7, 0) * addr_byte_size;
            // If UInt(imm8) is odd, see "FSTMX".
            regs = Bits32(opcode, 7, 0) / 2;
            // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
            if (regs == 0 || regs > 16 || (d + regs) > 32)
                return false;
            break;
        case eEncodingT2:
        case eEncodingA2:
            single_regs = true;
            d = Bits32(opcode, 15, 12) << 1 | Bits32(opcode, 22, 22);
            imm32 = Bits32(opcode, 7, 0) * addr_byte_size;
            regs = Bits32(opcode, 7, 0);
            // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
            if (regs == 0 || regs > 16 || (d + regs) > 32)
                return false;
            break;
        default:
            return false;
        }
        uint32_t start_reg = single_regs ? dwarf_s0 : dwarf_d0;
        uint32_t reg_byte_size = single_regs ? addr_byte_size : addr_byte_size * 2;
        addr_t sp_offset = imm32;
        addr_t addr = sp - sp_offset;
        uint32_t i;
        
        EmulateInstruction::Context context = { EmulateInstruction::eContextPushRegisterOnStack, eRegisterKindDWARF, 0, 0 };
        for (i=d; i<regs; ++i)
        {
            context.arg1 = start_reg + i;    // arg1 in the context is the DWARF register number
            context.arg2 = addr - sp;        // arg2 in the context is the stack pointer offset
            // uint64_t to accommodate 64-bit registers.
            uint64_t reg_value = emulator->ReadRegisterUnsigned(eRegisterKindDWARF, context.arg1, 0, &success);
            if (!success)
                return false;
            if (!emulator->WriteMemoryUnsigned (context, addr, reg_value, reg_byte_size))
                return false;
            addr += reg_byte_size;
        }
        
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.arg0 = eRegisterKindGeneric;
        context.arg1 = LLDB_REGNUM_GENERIC_SP;
        context.arg2 = -sp_offset;
    
        if (!emulator->WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, sp - sp_offset))
            return false;
    }
    return true;
}

static ARMOpcode g_arm_opcodes[] =
{
    // push register(s)
    { 0x0fff0000, 0x092d0000, ARMvAll,       eEncodingA1, eSize32, emulate_push, "push <registers>" },
    { 0x0fff0fff, 0x052d0004, ARMvAll,       eEncodingA2, eSize32, emulate_push, "push <register>" },

    // set r7 to point to a stack offset
    { 0x0ffff000, 0x028d7000, ARMvAll,       eEncodingA1, eSize32, emulate_add_rd_sp_imm, "add r7, sp, #<const>" },
    { 0x0ffff000, 0x024c7000, ARMvAll,       eEncodingA1, eSize32, emulate_sub_r7_ip_imm, "sub r7, ip, #<const>"},
    // set ip to point to a stack offset
    { 0x0fffffff, 0x01a0c00d, ARMvAll,       eEncodingA1, eSize32, emulate_mov_rd_sp, "mov ip, sp" },
    { 0x0ffff000, 0x028dc000, ARMvAll,       eEncodingA1, eSize32, emulate_add_rd_sp_imm, "add ip, sp, #<const>" },
    { 0x0ffff000, 0x024dc000, ARMvAll,       eEncodingA1, eSize32, emulate_sub_ip_sp_imm, "sub ip, sp, #<const>"},

    // adjust the stack pointer
    { 0x0ffff000, 0x024dd000, ARMvAll,       eEncodingA1, eSize32, emulate_sub_sp_imm, "sub sp, sp, #<const>"},

    // push one register
    // if Rn == '1101' && imm12 == '000000000100' then SEE PUSH;
    { 0x0fff0000, 0x052d0000, ARMvAll,       eEncodingA1, eSize32, emulate_str_rt_sp, "str Rt, [sp, #-imm12]!" },

    // vector push consecutive extension register(s)
    { 0x0fbf0f00, 0x0d2d0b00, ARMv6T2|ARMv7, eEncodingA1, eSize32, emulate_vpush, "vpush.64 <list>"},
    { 0x0fbf0f00, 0x0d2d0a00, ARMv6T2|ARMv7, eEncodingA2, eSize32, emulate_vpush, "vpush.32 <list>"}
};

static ARMOpcode g_thumb_opcodes[] =
{
    // push register(s)
    { 0xfffffe00, 0x0000b400, ARMvAll,       eEncodingT1, eSize16, emulate_push, "push <registers>" },
    { 0xffff0000, 0xe92d0000, ARMv6T2|ARMv7, eEncodingT2, eSize32, emulate_push, "push.w <registers>" },
    { 0xffff0fff, 0xf84d0d04, ARMv6T2|ARMv7, eEncodingT3, eSize32, emulate_push, "push.w <register>" },
    // move from high register to low register
    { 0xffffffc0, 0x00004640, ARMvAll,        eEncodingT1, eSize16, emulate_mov_low_high, "mov r0-r7, r8-r15" },

    // set r7 to point to a stack offset
    { 0xffffff00, 0x0000af00, ARMvAll,        eEncodingT1, eSize16, emulate_add_rd_sp_imm, "add r7, sp, #imm" },
    { 0xffffffff, 0x0000466f, ARMvAll,        eEncodingT1, eSize16, emulate_mov_rd_sp, "mov r7, sp" },

    // PC relative load into register (see also emulate_add_sp_rm)
    { 0xfffff800, 0x00004800, ARMvAll,       eEncodingT1, eSize16, emulate_ldr_rd_pc_rel, "ldr <Rd>, [PC, #imm]"},

    // adjust the stack pointer
    { 0xffffff87, 0x00004485, ARMvAll,       eEncodingT2, eSize16, emulate_add_sp_rm, "add sp, <Rm>"},
    { 0xffffff80, 0x0000b080, ARMvAll,       eEncodingT1, eSize16, emulate_sub_sp_imm, "add sp, sp, #imm"},
    { 0xfbef8f00, 0xf1ad0d00, ARMv6T2|ARMv7, eEncodingT2, eSize32, emulate_sub_sp_imm, "sub.w sp, sp, #<const>"},
    { 0xfbff8f00, 0xf2ad0d00, ARMv6T2|ARMv7, eEncodingT3, eSize32, emulate_sub_sp_imm, "subw sp, sp, #imm12"},

    // vector push consecutive extension register(s)
    { 0xffbf0f00, 0xed2d0b00, ARMv6T2|ARMv7, eEncodingT1, eSize32, emulate_vpush, "vpush.64 <list>"},
    { 0xffbf0f00, 0xed2d0a00, ARMv6T2|ARMv7, eEncodingT2, eSize32, emulate_vpush, "vpush.32 <list>"}
};

static const size_t k_num_arm_opcodes = sizeof(g_arm_opcodes)/sizeof(ARMOpcode);
static const size_t k_num_thumb_opcodes = sizeof(g_thumb_opcodes)/sizeof(ARMOpcode);

bool 
EmulateInstructionARM::ReadInstruction ()
{
    bool success = false;
    m_inst_cpsr = ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_FLAGS, 0, &success);
    if (success)
    {
        addr_t pc = ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_ADDRESS, &success);
        if (success)
        {
            Context read_inst_context = {eContextReadOpcode, 0, 0};
            if (m_inst_cpsr & MASK_CPSR_T)
            {
                m_inst_mode = eModeThumb;
                uint32_t thumb_opcode = ReadMemoryUnsigned(read_inst_context, pc, 2, 0, &success);
                
                if (success)
                {
                    if ((m_inst.opcode.inst16 & 0xe000) != 0xe000 || ((m_inst.opcode.inst16 & 0x1800u) == 0))
                    {
                        m_inst.opcode_type = eOpcode16;
                        m_inst.opcode.inst16 = thumb_opcode;
                    }
                    else
                    {
                        m_inst.opcode_type = eOpcode32;
                        m_inst.opcode.inst32 = (thumb_opcode << 16) | ReadMemoryUnsigned(read_inst_context, pc + 2, 2, 0, &success);
                    }
                }
            }
            else
            {
                m_inst_mode = eModeARM;
                m_inst.opcode_type = eOpcode32;
                m_inst.opcode.inst32 = ReadMemoryUnsigned(read_inst_context, pc, 4, 0, &success);
            }
        }
    }
    if (!success)
    {
        m_inst_mode = eModeInvalid;
        m_inst_pc = LLDB_INVALID_ADDRESS;
    }
    return success;
}

uint32_t
EmulateInstructionARM::CurrentCond ()
{
    switch (m_inst_mode)
    {
    default:
    case eModeInvalid:
        break;

    case eModeARM:
        return UnsignedBits(m_inst.opcode.inst32, 31, 28);
    
    case eModeThumb:
        return 0x0000000Eu; // Return always for now, we need to handl IT instructions later
    }
    return UINT32_MAX;  // Return invalid value
}
bool
EmulateInstructionARM::ConditionPassed ()
{
    if (m_inst_cpsr == 0)
        return false;

    const uint32_t cond = CurrentCond ();
    
    if (cond == UINT32_MAX)
        return false;
    
    bool result = false;
    switch (UnsignedBits(cond, 3, 1))
    {
    case 0: result = (m_inst_cpsr & MASK_CPSR_Z) != 0; break;
    case 1: result = (m_inst_cpsr & MASK_CPSR_C) != 0; break;
    case 2: result = (m_inst_cpsr & MASK_CPSR_N) != 0; break;
    case 3: result = (m_inst_cpsr & MASK_CPSR_V) != 0; break;
    case 4: result = ((m_inst_cpsr & MASK_CPSR_C) != 0) && ((m_inst_cpsr & MASK_CPSR_Z) == 0); break;
    case 5: 
        {
            bool n = (m_inst_cpsr & MASK_CPSR_N);
            bool v = (m_inst_cpsr & MASK_CPSR_V);
            result = n == v;
        }
        break;
    case 6: 
        {
            bool n = (m_inst_cpsr & MASK_CPSR_N);
            bool v = (m_inst_cpsr & MASK_CPSR_V);
            result = n == v && ((m_inst_cpsr & MASK_CPSR_Z) == 0);
        }
        break;
    case 7: 
        result = true; 
        break;
    }

    if (cond & 1)
        result = !result;
    return result;
}


bool
EmulateInstructionARM::EvaluateInstruction ()
{
    return false;
}
