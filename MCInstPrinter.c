//
// Created by Phosphorus15 on 2021/5/14.
//
#include "MCInstPrinter.h"
#include "MCInst.h"

static bool MCInstPrinter_matchAliasCondition(
    const MCInst *MI, unsigned *OpIdx, const PatternsForOpcode *OpToPatterns,
    const AliasPattern *Patterns, const AliasPatternCond *Conds,
    const AliasPatternCond *Cond, bool *OrPredicateResult) {
    // FIXME so here's on problem we ought to detect feature bits here
    if (Cond->Kind == AliasPatternCond_K_Feature ||
        Cond->Kind == AliasPatternCond_K_NegFeature)
        return true; // STI->getFeatureBits().test(C.Value);
    // For feature tests where just one feature is required in a list, set the
    // predicate result bit to whether the expression will return true, and only
    // return the real result at the end of list marker.
    if (Cond->Kind == AliasPatternCond_K_OrFeature) {
        //        *OrPredicateResult |= STI->getFeatureBits().test(C.Value);
        return true;
    }
    if (Cond->Kind == AliasPatternCond_K_OrNegFeature) {
        //        *OrPredicateResult |= !(STI->getFeatureBits().test(C.Value));
        return true;
    }
    if (Cond->Kind == AliasPatternCond_K_EndOrFeatures) {
        bool Res = *OrPredicateResult;
        *OrPredicateResult = false;
        return Res;
    }

    MCOperand *Opnd = MCInst_getOperand((MCInst *)MI, *OpIdx);
    *OpIdx = *OpIdx + 1;
    switch (Cond->Kind) {
    case AliasPatternCond_K_Imm:
        // Operand must be a specific immediate.
        return MCOperand_isImm(Opnd) && MCOperand_getImm(Opnd) == Cond->Value;
    case AliasPatternCond_K_Reg:
        // Operand must be a specific register.
        return MCOperand_isReg(Opnd) && MCOperand_getReg(Opnd) == Cond->Value;
    case AliasPatternCond_K_TiedReg:
        // Operand must match the register of another operand.
        return MCOperand_isReg(Opnd) &&
            MCOperand_getReg(Opnd) ==
                MCOperand_getReg(MCInst_getOperand((MCInst *)MI, Cond->Value));
    case AliasPatternCond_K_RegClass:
        // Operand must be a register in this class. Value is a register class id.
        return MCOperand_isReg(Opnd) &&
            MCRegisterClass_contains(
                MCRegisterInfo_getRegClass(MRI, Cond->Value),
                MCOperand_getReg(Opnd));
    case AliasPatternCond_K_Custom:
        // Operand must match some custom criteria.
        // TODO might affect something            return M.ValidateMCOperand(Opnd,
        // *STI, C.Value);
        return false;
    case AliasPatternCond_K_Ignore:
        // Operand can be anything.
        return true;
    case AliasPatternCond_K_Feature:
    case AliasPatternCond_K_NegFeature:
    case AliasPatternCond_K_OrFeature:
    case AliasPatternCond_K_OrNegFeature:
    case AliasPatternCond_K_EndOrFeatures:
    default:
        return false;
        // TODO: Impossible to reach
        // llvm_unreachable("handled earlier");
    }
}

const char *MCInstPrinter_matchAliasPatterns(
    const MCInst *MI, const PatternsForOpcode *OpToPatterns,
    const AliasPattern *Patterns, const AliasPatternCond *Conds,
    const char *AsmStrings[], unsigned len) {

    // Binary search by opcode. Return false if there are no aliases for this
    // opcode.
    PatternsForOpcode *It =
        Binary_Search(OpToPatterns, MCInst_getOpcode(MI), len);
    if (It == NULL || It->Opcode != MCInst_getOpcode(MI))
        return NULL;

    // Try all patterns for this opcode.
    uint32_t AsmStrOffset = ~0U;

    for (unsigned i = It->PatternStart; i < It->PatternStart + It->NumPatterns;
        i++) {
        const AliasPattern Pattern = Patterns[i];
        if (MCInst_getNumOperands(MI) != Pattern.NumOperands)
        return NULL;
        unsigned OpIdx = 0;
        bool OrPredicateResult = false;
        bool fallThrough = true;
        for (unsigned j = Pattern.AliasCondStart;
            j < Pattern.AliasCondStart + Pattern.NumConds; j++) {
        fallThrough &= MCInstPrinter_matchAliasCondition(
            MI, &OpIdx, OpToPatterns, Patterns, Conds, &Conds[j],
            &OrPredicateResult);
        if (!fallThrough)
            break;
        }
        if (fallThrough) {
        AsmStrOffset = Pattern.AsmStrOffset;
        break;
        }
    }

    // If no alias matched, don't print an alias.
    if (AsmStrOffset == ~0U)
        return NULL;

    return (const char *)((*AsmStrings) + AsmStrOffset);
}
