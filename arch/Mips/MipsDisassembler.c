//===- MipsDisassembler.cpp - Disassembler for Mips -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is part of the Mips Disassembler.
//
//===----------------------------------------------------------------------===//

/* Capstone Disassembly Engine */
/* By Nguyen Anh Quynh <aquynh@gmail.com>, 2013-2015 */

#ifdef CAPSTONE_HAS_MIPS

#include <stdio.h>
#include <string.h>

#include "capstone/platform.h"

#include "MipsDisassembler.h"

#include "../../utils.h"

#include "../../MCRegisterInfo.h"
#include "../../SStream.h"

#include "../../MathExtras.h"

//#include "Mips.h"
//#include "MipsRegisterInfo.h"
//#include "MipsSubtarget.h"
#include "../../MCFixedLenDisassembler.h"
#include "../../MCInst.h"
//#include "llvm/MC/MCSubtargetInfo.h"
#include "../../MCRegisterInfo.h"
#include "../../MCDisassembler.h"

#define GET_SUBTARGETINFO_ENUM
#define UNIT ((uint64_t)1)

static unsigned getReg(const MCRegisterInfo *MRI, unsigned RC, unsigned RegNo);
// static uint64_t getFeatureBits(int mode);
static inline unsigned checkFeatureRequired(unsigned Bits, unsigned Feature,
                                            bool Require);
#define GET_REGINFO_ENUM
#define GET_REGINFO_MC_DESC
#define GET_INSTRINFO_ENUM
#define MIPS_GET_DISASSEMBLER
#include "CapstoneMipsModule.h"

/// Extract 'not' into Require, Require being '0' or 'false' means returns true
/// when the feature is not available Also we're not using bits to represent
/// feature anymore (for obvious reason)
static inline unsigned checkFeatureRequired(unsigned Bits, unsigned Feature,
                                            bool Require) {
  //    if(Feature == Mips_FeatureFP64Bit) return true; // enables all fp
  //    instructions (32/64)
  switch (Feature) {
  default:
    return true; // For arbitrary checks we always declare it true - enables all
                 // checks
  case Mips_FeatureMips1: // Disabled features
  case Mips_FeatureMicroMips:
    return getbool(Bits & CS_MODE_MICRO) == Require;
  case Mips_FeatureMips4_32r2:
  case Mips_FeatureMips2:
    return getbool(Bits & CS_MODE_MICRO) != Require; // these two are disabled
  case Mips_FeatureSoftFloat: // Soft float represents no instruction
    return !Require;
  case Mips_FeatureMips16:
    return getbool(Bits & CS_MODE_16) == Require;
  case Mips_FeatureMips32r6:
    return getbool(Bits & CS_MODE_MIPS32R6) == Require;
  case Mips_FeatureMips64r6:
    return getbool(Bits & (CS_MODE_16 | CS_MODE_32 | CS_MODE_MIPS32R6 |
                           CS_MODE_64)) != Require;
  case Mips_FeatureFP64Bit:
    return true; // enable this feature if required
  case Mips_FeatureMips64r2:
    return getbool(Bits & CS_MODE_64) == Require;
  }
  return false; // unreachable
}


void Mips_init(MCRegisterInfo *MRI)
{
	// InitMCRegisterInfo(MipsRegDesc, 394, RA, PC,
	// 		MipsMCRegisterClasses, 62,
	// 		MipsRegUnitRoots,
	// 		273,
	// 		MipsRegDiffLists,
	// 		MipsLaneMaskLists,
	// 		MipsRegStrings,
	// 		MipsRegClassStrings,
	// 		MipsSubRegIdxLists,
	// 		12,
	// 		MipsSubRegIdxRanges,
	// 		MipsRegEncodingTable);


	MCRegisterInfo_InitMCRegisterInfo(
		MRI, MipsRegDesc, ARR_SIZE(MipsRegDesc), 0, 0, MipsMCRegisterClasses,
		ARR_SIZE(MipsMCRegisterClasses), 0, 0, MipsRegDiffLists, 0,
		MipsSubRegIdxLists, ARR_SIZE(MipsSubRegIdxLists), 0);
}

/// Read two bytes from the ArrayRef and return 16 bit halfword sorted
/// according to the given endianess.
static void readInstruction16(unsigned char *code, uint32_t *insn,
		bool isBigEndian)
{
	// We want to read exactly 2 Bytes of data.
	if (isBigEndian)
		*insn = (code[0] << 8) | code[1];
	else
		*insn = (code[1] << 8) | code[0];
}

/// readInstruction - read four bytes from the MemoryObject
/// and return 32 bit word sorted according to the given endianess
static void readInstruction32(unsigned char *code, uint32_t *insn, bool isBigEndian, bool isMicroMips)
{
	// High 16 bits of a 32-bit microMIPS instruction (where the opcode is)
	// always precede the low 16 bits in the instruction stream (that is, they
	// are placed at lower addresses in the instruction stream).
	//
	// microMIPS byte ordering:
	//   Big-endian:    0 | 1 | 2 | 3
	//   Little-endian: 1 | 0 | 3 | 2

	// We want to read exactly 4 Bytes of data.
	if (isBigEndian) {
		// Encoded as a big-endian 32-bit word in the stream.
		*insn =
			(code[3] << 0) | (code[2] << 8) | (code[1] << 16) | ((uint32_t) code[0] << 24);
	} else {
		if (isMicroMips) {
			*insn = (code[2] << 0) | (code[3] << 8) | (code[0] << 16) |
				((uint32_t) code[1] << 24);
		} else {
			*insn = (code[0] << 0) | (code[1] << 8) | (code[2] << 16) |
				((uint32_t) code[3] << 24);
		}
	}
}

static DecodeStatus MipsDisassembler_getInstruction(int mode, MCInst *instr,
		const uint8_t *code, size_t code_len,
		uint16_t *Size,
		uint64_t Address, bool isBigEndian, MCRegisterInfo *MRI)
{
	uint32_t Insn;
	DecodeStatus Result;

	instr->MRI = MRI;

	if (instr->flat_insn->detail) {
		memset(instr->flat_insn->detail, 0, offsetof(cs_detail, mips)+sizeof(cs_mips));
	}

	if (mode & CS_MODE_MICRO) {
		if (code_len < 2)
			// not enough data
			return MCDisassembler_Fail;

		readInstruction16((unsigned char*)code, &Insn, isBigEndian);

		// Calling the auto-generated decoder function.
		Result = decodeInstruction(DecoderTableMicroMips16, instr, Insn, Address, MRI, mode);
		if (Result != MCDisassembler_Fail) {
			*Size = 2;
			return Result;
		}

		if (code_len < 4)
			// not enough data
			return MCDisassembler_Fail;

		readInstruction32((unsigned char*)code, &Insn, isBigEndian, true);

		//DEBUG(dbgs() << "Trying MicroMips32 table (32-bit instructions):\n");
		// Calling the auto-generated decoder function.
		Result = decodeInstruction(DecoderTableMicroMips32, instr, Insn, Address, MRI, mode);
		if (Result != MCDisassembler_Fail) {
			*Size = 4;
			return Result;
		}
		return MCDisassembler_Fail;
	}

	if (code_len < 4)
		// not enough data
		return MCDisassembler_Fail;

	readInstruction32((unsigned char*)code, &Insn, isBigEndian, false);

	if ((mode & CS_MODE_MIPS2) && ((mode & CS_MODE_MIPS3) == 0)) {
		// DEBUG(dbgs() << "Trying COP3_ table (32-bit opcodes):\n");
		Result = decodeInstruction(DecoderTableCOP3_32, instr, Insn, Address, MRI, mode);
		if (Result != MCDisassembler_Fail) {
			*Size = 4;
			return Result;
		}
	}

	if ((mode & CS_MODE_MIPS32R6) && (mode & CS_MODE_MIPS64)) {
		// DEBUG(dbgs() << "Trying Mips32r6_64r6 (GPR64) table (32-bit opcodes):\n");
		Result = decodeInstruction(DecoderTableMips32r6_64r6_GP6432, instr, Insn,
				Address, MRI, mode);
		if (Result != MCDisassembler_Fail) {
			*Size = 4;
			return Result;
		}
	}

	if (mode & CS_MODE_MIPS32R6) {
		// DEBUG(dbgs() << "Trying Mips32r6_64r6 table (32-bit opcodes):\n");
		Result = decodeInstruction(DecoderTableMips32r6_64r632, instr, Insn,
				Address, MRI, mode);
		if (Result != MCDisassembler_Fail) {
			*Size = 4;
			return Result;
		}
	}

	if (mode & CS_MODE_MIPS64) {
		// DEBUG(dbgs() << "Trying Mips64 (GPR64) table (32-bit opcodes):\n");
		Result = decodeInstruction(DecoderTableMips6432, instr, Insn,
				Address, MRI, mode);
		if (Result != MCDisassembler_Fail) {
			*Size = 4;
			return Result;
		}
	}

	// DEBUG(dbgs() << "Trying Mips table (32-bit opcodes):\n");
	// Calling the auto-generated decoder function.
	Result = decodeInstruction(DecoderTableMips32, instr, Insn, Address, MRI, mode);
	if (Result != MCDisassembler_Fail) {
		*Size = 4;
		return Result;
	}

	return MCDisassembler_Fail;
}

bool Mips_getInstruction(csh ud, const uint8_t *code, size_t code_len, MCInst *instr,
		uint16_t *size, uint64_t address, void *info)
{
	cs_struct *handle = (cs_struct *)(uintptr_t)ud;

	DecodeStatus status = MipsDisassembler_getInstruction(handle->mode, instr,
			code, code_len,
			size,
			address, MODE_IS_BIG_ENDIAN(handle->mode), (MCRegisterInfo *)info);

	return status == MCDisassembler_Success;
}

static unsigned getReg(const MCRegisterInfo *MRI, unsigned RC, unsigned RegNo)
{
	const MCRegisterClass *rc = MCRegisterInfo_getRegClass(MRI, RC);
	return rc->RegsBegin[RegNo];
}


#endif
