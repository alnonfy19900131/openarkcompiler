/*
 * Copyright (c) [2019] Huawei Technologies Co.,Ltd.All rights reserved.
 *
 * OpenArkCompiler is licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *
 *     http://license.coscl.org.cn/MulanPSL
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR
 * FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v1 for more details.
 */
#include "me_rc_lowering.h"
#include <cstring>

/*
 * RCLowering phase generate RC intrinsic for reference assignment
 * based on previous analyze results. RC intrinsic will later be lowered
 * in Code Generation
 */
namespace {
}

namespace maple {
static inline bool CheckOp(const MeStmt *stmt, const Opcode op) {
  return stmt != nullptr && stmt->GetOp() == op;
}

static inline void CheckRemove(MeStmt *stmt, const Opcode op) {
  if (CheckOp(stmt, op)) {
    stmt->GetBB()->RemoveMeStmt(stmt);
  }
}

void RCLowering::Prepare() {
  MIRFunction *mirFunction = func.GetMirFunc();
  ASSERT(mirFunction->GetModule()->CurFunction() == mirFunction, "unexpected CurFunction");
  if (enabledDebug) {
    LogInfo::MapleLogger() << "Handling function " << mirFunction->GetName() << '\n';
  }
}

void RCLowering::PreRCLower() {
  // preparation steps before going through basicblocks
  MarkLocalRefVar();

  MarkAllRefOpnds();
  CreateCleanupIntrinsics();
}

void RCLowering::MarkLocalRefVar() {
  MIRFunction *mirFunction = func.GetMirFunc();
  size_t bsize = mirFunction->GetSymTab()->GetSymbolTableSize();
  for (size_t i = 0; i < bsize; ++i) {
    MIRSymbol *sym = mirFunction->GetSymTab()->GetSymbolFromStIdx(i);
    if (sym != nullptr && sym->GetStorageClass() == kScAuto && !sym->IgnoreRC()) {
      sym->SetLocalRefVar();
    }
  }
}

void RCLowering::MarkAllRefOpnds() {
  auto eIt = func.valid_end();
  for (auto bIt = func.valid_begin(); bIt != eIt; ++bIt) {
    for (auto &stmt : (*bIt)->GetMeStmts()) {
      MeExpr *lhsRef = stmt.GetLHSRef(ssaTab, false);
      if (lhsRef == nullptr) {
        continue;
      }
      if (lhsRef->GetMeOp() == kMeOpVar) {
        auto *var = static_cast<VarMeExpr*>(lhsRef);
        cleanUpVars[var->GetOStIdx()] = var;
        ssaTab.UpdateVarOstMap(var->GetOStIdx(), varOStMap);
      }
      stmt.EnableNeedDecref();
      MeExpr *rhs = stmt.GetRHS();
      if (rhs == nullptr) {
        continue;
      }
      if (rhs->PointsToSomethingThatNeedsIncRef()) {
        stmt.EnableNeedIncref();
      } else {
        stmt.DisableNeedIncref();
      }
    }
  }
}

void RCLowering::CreateCleanupIntrinsics() {
  for (BB *bb : func.GetCommonExitBB()->GetPred()) {
    MeStmt *lastMeStmt = to_ptr(bb->GetMeStmts().rbegin());
    if (!CheckOp(lastMeStmt, OP_return)) {
      continue;
    }
    std::vector<MeExpr*> opnds;
    for (const auto &item : cleanUpVars) {
      if (!varOStMap[item.first]->IsLocal() || varOStMap[item.first]->IsFormal()) {
        continue;
      }
      opnds.push_back(item.second);
    }
    IntrinsiccallMeStmt *intrn = irMap.CreateIntrinsicCallMeStmt(INTRN_MPL_CLEANUP_LOCALREFVARS, opnds);
    bb->InsertMeStmtBefore(to_ptr(bb->GetMeStmts().rbegin()), intrn);
  }
}

// move to MeFunction::CreateVarMeExprFromSym as func has ssaTab and irMap
VarMeExpr *RCLowering::CreateVarMeExprFromSym(MIRSymbol &sym) const {
  OriginalSt *ost = ssaTab.FindOrCreateSymbolOriginalSt(sym, func.GetMirFunc()->GetPuidx(), 0);
  return irMap.GetOrCreateZeroVersionVarMeExpr(*ost);
}

// note that RCInstrinsic creation will check the ref assignment and reuse lhs if possible
IntrinsiccallMeStmt *RCLowering::CreateRCIntrinsic(const MIRIntrinsicID intrnID, const MeStmt &stmt,
                                                   std::vector<MeExpr*> &opnds, bool assigned) {
  IntrinsiccallMeStmt *intrn = nullptr;
  if (assigned) {
    MeExpr *ret = stmt.GetOp() == OP_regassign ? stmt.GetLHS() : irMap.CreateRegMeExpr(PTY_ref);
    intrn = irMap.CreateIntrinsicCallAssignedMeStmt(intrnID, opnds, ret);
  } else {
    intrn = irMap.CreateIntrinsicCallMeStmt(intrnID, opnds);
  }
  intrn->SetSrcPos(stmt.GetSrcPosition());
  return intrn;
}

MIRIntrinsicID RCLowering::PrepareVolatileCall(const MeStmt &stmt, const MIRIntrinsicID intrnId) {
  bool isLoad = (intrnId == INTRN_MCCLoadRefSVol || intrnId == INTRN_MCCLoadWeakVol || intrnId == INTRN_MCCLoadRefVol);
  if (isLoad) {
    CheckRemove(stmt.GetNext(), OP_membaracquire);
  } else {  // volatile store case
    CheckRemove(stmt.GetPrev(), OP_membarrelease);
    CheckRemove(stmt.GetNext(), OP_membarstoreload);
  }
  return intrnId;
}

IntrinsiccallMeStmt *RCLowering::GetVarRHSHandleStmt(const MeStmt &stmt) {
  auto *var = static_cast<VarMeExpr*>(stmt.GetRHS());
  const MIRSymbol *sym = ssaTab.GetMIRSymbolFromID(var->GetOStIdx());
  if (!sym->IsGlobal() || sym->IsFinal()) {
    return nullptr;
  }
  // load global into temp and update rhs to temp
  std::vector<MeExpr*> opnds;
  bool isVolatile = var->IsVolatile(ssaTab);
  MIRIntrinsicID rcCallID = isVolatile ? PrepareVolatileCall(stmt, INTRN_MCCLoadRefSVol) : INTRN_MCCLoadRefS;
  opnds.push_back(irMap.CreateAddrofMeExpr(*var));
  // rhs is not special, skip
  if (rcCallID == INTRN_UNDEFINED) {
    return nullptr;
  }
  // rhs is special, need handle
  return CreateRCIntrinsic(rcCallID, stmt, opnds, true);
}

IntrinsiccallMeStmt *RCLowering::GetIvarRHSHandleStmt(const MeStmt &stmt) {
  auto *ivar = static_cast<IvarMeExpr*>(stmt.GetRHS());
  if (ivar->IsFinal()) {
    return nullptr;
  }
  // load global into temp and update rhs to temp
  std::vector<MeExpr*> opnds;
  MIRIntrinsicID rcCallId = ivar->IsVolatile() ? PrepareVolatileCall(stmt, INTRN_MCCLoadRefVol) : INTRN_MCCLoadRef;
  opnds.push_back(&ivar->GetBase()->GetAddrExprBase());
  opnds.push_back(irMap.CreateAddrofMeExpr(*ivar));
  // rhs is not special, skip
  if (rcCallId == INTRN_UNDEFINED) {
    return nullptr;
  }
  // rhs is special, need handle
  return CreateRCIntrinsic(rcCallId, stmt, opnds, true);
}

// this function returns true if we generated new MRT calls and replaced rhs
void RCLowering::HandleAssignMeStmtRHS(MeStmt &stmt) {
  if (!stmt.NeedIncref()) {
    return;
  }
  MeExpr *rhs = stmt.GetRHS();
  CHECK_FATAL(rhs != nullptr, "rhs is nullptr in RCLowering::HandleAssignMeStmtRHS");
  IntrinsiccallMeStmt *loadCall = nullptr;
  std::vector<MeExpr*> opnds;
  if (rhs->GetMeOp() == kMeOpVar) {
    loadCall = GetVarRHSHandleStmt(stmt);
  } else if (rhs->GetMeOp() == kMeOpIvar) {
    loadCall = GetIvarRHSHandleStmt(stmt);
  }
  // rhs is not special, skip
  if (loadCall == nullptr) {
    return;
  }
  if (stmt.GetOp() == OP_regassign) {
    stmt.GetBB()->ReplaceMeStmt(&stmt, loadCall);
    if (rhs->IsVolatile(ssaTab)) {
      stmt.SetOpnd(1, loadCall->GetMustDefList()->front().GetLHS());
    }
  } else {
    stmt.GetBB()->InsertMeStmtBefore(&stmt, loadCall);
    stmt.SetOpnd(1, loadCall->GetMustDefList()->front().GetLHS());
  }
  stmt.DisableNeedIncref();
}

void RCLowering::HandleCallAssignedMeStmt(MeStmt &stmt, MeExpr *pendingDec) {
  MapleVector<MustDefMeNode> *mustDefs = stmt.GetMustDefList();
  ASSERT(mustDefs != nullptr, "null ptr check");

  BB *bb = stmt.GetBB();
  CHECK_FATAL(bb != nullptr, "bb null ptr check");
  if (mustDefs->empty()) {
    // introduce a ret and decref on it as callee has +1 return ref
    IntroduceRegRetIntoCallAssigned(stmt);
    return;
  }

  // decref for old value
  MeExpr *lhs = mustDefs->front().GetLHS();
  if (lhs->GetMeOp() != kMeOpVar) {
    return;
  }
  auto *ost = ssaTab.GetOriginalStFromID(static_cast<VarMeExpr*>(lhs)->GetOStIdx());
  if (!ost->IsSymbolOst()) {
    return;
  }
  MIRSymbol *retSym = ost->GetMIRSymbol();
  if (retSym == nullptr) {
    return;
  }
  assignedPtrSym.insert(retSym);
  if (!stmt.NeedDecref()) {
    return;
  }

  CHECK_FATAL(pendingDec != nullptr, "pendingDec null ptr check");
  HandleRetOfCallAssignedMeStmt(stmt, *pendingDec);
}

void RCLowering::IntroduceRegRetIntoCallAssigned(MeStmt &stmt) {
  RegMeExpr *curTmp = irMap.CreateRegMeExpr(PTY_ref);
  stmt.GetMustDefList()->push_back(MustDefMeNode(curTmp, &stmt));
  std::vector<MeExpr*> opnds = { curTmp };
  IntrinsiccallMeStmt *decrefCall = CreateRCIntrinsic(INTRN_MCCDecRef, stmt, opnds);
  stmt.GetBB()->InsertMeStmtAfter(&stmt, decrefCall);
}

void RCLowering::HandleRetOfCallAssignedMeStmt(MeStmt &stmt, MeExpr &pendingDec) {
  BB *bb = stmt.GetBB();
  CHECK_FATAL(bb != nullptr, "bb null ptr check");
  RegassignMeStmt *backup = irMap.CreateRegassignMeStmt(*irMap.CreateRegMeExpr(PTY_ref), pendingDec, *bb);
  std::vector<MeExpr*> opnds = { backup->GetRegLHS() };
  IntrinsiccallMeStmt *decrefCall = CreateRCIntrinsic(INTRN_MCCDecRef, stmt, opnds);
  if (!dynamic_cast<CallMeStmt*>(&stmt)) {
    bb->InsertMeStmtBefore(&stmt, backup);
    bb->InsertMeStmtAfter(&stmt, decrefCall);
  } else {
    /*
     * simple optimization for callassign
     * instead of change callassign {dassign} to backup; callassign {dassign}; decref
     * callassign {regassign}; backup; dassign (regread); decref
     */
    RegMeExpr *curTmp = irMap.CreateRegMeExpr(PTY_ref);
    MeStmt *regToVar = irMap.CreateDassignMeStmt(*stmt.GetAssignedLHS(), *curTmp, *bb);
    stmt.GetMustDefList()->front().UpdateLHS(*curTmp);
    bb->InsertMeStmtAfter(&stmt, decrefCall);
    bb->InsertMeStmtAfter(&stmt, regToVar);
    bb->InsertMeStmtAfter(&stmt, backup);
  }
}

bool RCLowering::RCFirst(MeExpr &rhs) {
  // null, local var/reg read
  if (rhs.GetMeOp() == kMeOpConst) {
    return static_cast<ConstMeExpr&>(rhs).IsZero();
  } else if (rhs.GetMeOp() == kMeOpVar) {
    auto &rhsVar = static_cast<VarMeExpr&>(rhs);
    const MIRSymbol *sym = ssaTab.GetMIRSymbolFromID(rhsVar.GetOStIdx());
    return sym->IsLocal();
  }
  return rhs.GetMeOp() == kMeOpReg;
}

void RCLowering::PreprocessAssignMeStmt(MeStmt &stmt) {
  BB *bb = stmt.GetBB();
  MeExpr *lhs = stmt.GetLHS();
  CHECK_FATAL(lhs != nullptr, "null ptr check");
  MeExpr *rhs = stmt.GetRHS();
  CHECK_FATAL(rhs != nullptr, "null ptr check");
  // Check whether there is any statement exist whose opnd is the throw value in catch BB
  if (needSpecialHandleException && rhs->GetMeOp() == kMeOpReg &&
      static_cast<RegMeExpr*>(rhs)->GetRegIdx() == -kSregThrownval) {
    needSpecialHandleException = false;
  }
  if (lhs->GetMeOp() != kMeOpVar) {
    return;
  }
  MIRSymbol *lsym = ssaTab.GetMIRSymbolFromID(stmt.GetVarLHS()->GetOStIdx());
  if (stmt.GetOp() == OP_dassign && (lsym->GetStorageClass() == kScAuto || lsym->GetStorageClass() == kScFormal)) {
    assignedPtrSym.insert(lsym);
  }
  if (rhs->GetMeOp() != kMeOpGcmalloc) {
    return;
  }
  gcMallocObjects.insert(lhs);
  if (lsym->GetAttr(ATTR_rcunowned)) {
    /*
     * if new obj is assigned to unowned refvar, we need a localrefvar
     * to decref at exit
     * introduce new localrefvar = lhs after current stmt
     */
    MeStmt *backup = irMap.CreateDassignMeStmt(*CreateNewTmpVarMeExpr(true), *lhs, *bb);
    // backup will not have any incref/decref
    bb->InsertMeStmtAfter(&stmt, backup);
  }
}

void RCLowering::HandleAssignMeStmtRegLHS(MeStmt &stmt) {
  if (!stmt.NeedIncref()) {
    return;
  }
  std::vector<MeExpr*> opnds = { stmt.GetLHS() };
  IntrinsiccallMeStmt *incCall = CreateRCIntrinsic(INTRN_MCCIncRef, stmt, opnds);
  stmt.GetBB()->InsertMeStmtAfter(&stmt, incCall);
}

void RCLowering::HandleAssignMeStmtVarLHS(MeStmt &stmt, MeExpr *pendingDec) {
  MIRSymbol *lsym = ssaTab.GetMIRSymbolFromID(stmt.GetVarLHS()->GetOStIdx());
  if (lsym->IsGlobal()) {
    // decref could be optimized away after if null check
    HandleAssignToGlobalVar(stmt);
  } else {
    // local assign, backup old value and insert Inc and Dec after
    HandleAssignToLocalVar(stmt, pendingDec);
  }
  assignedPtrSym.insert(lsym);
}

void RCLowering::HandleAssignToGlobalVar(MeStmt &stmt) {
  MeExpr *lhs = stmt.GetLHS();
  CHECK_FATAL(lhs != nullptr, "null ptr check");
  MeExpr *rhs = stmt.GetRHS();
  CHECK_FATAL(rhs != nullptr, "null ptr check");
  BB *bb = stmt.GetBB();
  CHECK_FATAL(bb != nullptr, "bb null ptr check");

  std::vector<MeExpr*> opnds = { irMap.CreateAddrofMeExpr(*lhs), rhs };
  IntrinsiccallMeStmt *writeRefCall = CreateRCIntrinsic(SelectWriteBarrier(stmt), stmt, opnds);
  bb->ReplaceMeStmt(&stmt, writeRefCall);
}

void RCLowering::HandleAssignToLocalVar(MeStmt &stmt, MeExpr *pendingDec) {
  MeExpr *lhs = stmt.GetLHS();
  CHECK_FATAL(lhs != nullptr, "null ptr check");
  MeExpr *rhs = stmt.GetRHS();
  CHECK_FATAL(rhs != nullptr, "null ptr check");
  BB *bb = stmt.GetBB();
  CHECK_FATAL(bb != nullptr, "bb null ptr check");
  bool incWithLHS = stmt.NeedIncref();

  if (stmt.NeedDecref()) {
    if (RCFirst(*rhs)) {
      // temp is not needed
      if (incWithLHS) {
        std::vector<MeExpr*> opnds = { rhs };
        IntrinsiccallMeStmt *incCall = CreateRCIntrinsic(INTRN_MCCIncRef, stmt, opnds);
        bb->InsertMeStmtBefore(&stmt, incCall);
        incWithLHS = false;
      }
      std::vector<MeExpr*> opnds = { pendingDec };
      IntrinsiccallMeStmt *decCall = CreateRCIntrinsic(INTRN_MCCDecRef, stmt, opnds);
      bb->InsertMeStmtBefore(&stmt, decCall);
    } else {
      RegassignMeStmt *backup = irMap.CreateRegassignMeStmt(*irMap.CreateRegMeExpr(PTY_ref), *pendingDec, *bb);
      bb->InsertMeStmtBefore(&stmt, backup);
      std::vector<MeExpr*> opnds = { backup->GetLHS() };
      IntrinsiccallMeStmt *decCall = CreateRCIntrinsic(INTRN_MCCDecRef, stmt, opnds);
      bb->InsertMeStmtAfter(&stmt, decCall);
    }
  }
  if (incWithLHS) {
    std::vector<MeExpr*> opnds = { lhs };
    IntrinsiccallMeStmt *incCall = CreateRCIntrinsic(INTRN_MCCIncRef, stmt, opnds);
    bb->InsertMeStmtAfter(&stmt, incCall);
  }
}

bool RCLowering::IsInitialized(IvarMeExpr &ivar) {
  MeExpr *base = ivar.GetBase();
  if (base->GetMeOp() != kMeOpVar) {
    return true;
  }
  if (gcMallocObjects.find(base) == gcMallocObjects.end()) {
    return true;
  }
  FieldID fieldID = ivar.GetFieldID();
  MapleSet<FieldID> *fieldSet = initializedFields[base];
  if (fieldSet != nullptr && (fieldSet->count(fieldID) > 0 || fieldSet->count(0) > 0)) {
    return true;
  }
  MIRType *baseType = GlobalTables::GetTypeTable().GetTypeFromTyIdx(ivar.GetTyIdx());
  ASSERT(dynamic_cast<MIRPtrType*>(baseType) != nullptr, "unexpected type");
  auto *ptype = static_cast<MIRPtrType*>(baseType)->GetPointedType();
  auto *classType = dynamic_cast<MIRClassType*>(ptype);
  return classType == nullptr || !classType->IsOwnField(fieldID);
}

void RCLowering::HandleAssignMeStmtIvarLHS(MeStmt &stmt) {
  auto &iassign = static_cast<IassignMeStmt&>(stmt);
  IvarMeExpr *lhsInner = iassign.GetLHSVal();
  FieldID fieldID = lhsInner->GetFieldID();
  MIRType *baseType = GlobalTables::GetTypeTable().GetTypeFromTyIdx(lhsInner->GetTyIdx());
  ASSERT(dynamic_cast<MIRPtrType*>(baseType) != nullptr, "unexpected type");
  auto *ptype = static_cast<MIRPtrType*>(baseType)->GetPointedType();
  auto *classType = dynamic_cast<MIRClassType*>(ptype);
  // skip RC operation if the field is unowned
  if (classType != nullptr && classType->IsFieldRCUnownedRef(fieldID)) {
    return;
  }
  if (!IsInitialized(*lhsInner)) {
    stmt.DisableNeedDecref();
    MapleSet<FieldID> *fieldSet = initializedFields[lhsInner->GetBase()];
    if (fieldSet == nullptr) {
      fieldSet =
          mirModule.GetMemPool()->New<MapleSet<FieldID>>(std::less<FieldID>(), mirModule.GetMPAllocator().Adapter());
    }
    fieldSet->insert(fieldID);
  }
  MeExpr *rhsInner = stmt.GetRHS();
  MIRIntrinsicID intrinsicID = SelectWriteBarrier(stmt);
  std::vector<MeExpr*> opnds =
      { &lhsInner->GetBase()->GetAddrExprBase(), irMap.CreateAddrofMeExpr(*lhsInner), rhsInner };
  IntrinsiccallMeStmt *writeRefCall = CreateRCIntrinsic(intrinsicID, stmt, opnds);
  stmt.GetBB()->ReplaceMeStmt(&stmt, writeRefCall);
}

void RCLowering::HandleAssignMeStmt(MeStmt &stmt, MeExpr *pendingDec) {
  PreprocessAssignMeStmt(stmt);
  if (!stmt.NeedIncref() && !stmt.NeedDecref()) {
    return;
  }
  HandleAssignMeStmtRHS(stmt);
  MeExpr *lhs = stmt.GetLHS();
  CHECK_FATAL(lhs != nullptr, "null ptr check");
  if (lhs->GetMeOp() == kMeOpReg) {
    HandleAssignMeStmtRegLHS(stmt);
  } else if (lhs->GetMeOp() == kMeOpVar) {
    HandleAssignMeStmtVarLHS(stmt, pendingDec);
  } else if (lhs->GetMeOp() == kMeOpIvar) {
    HandleAssignMeStmtIvarLHS(stmt);
  }
}

/*
 * align with order in rcinsertion, otherwise missing weak volatile
 * note that we are generating INTRN_MCCWriteNoRC so write_barrier is supported,
 * otherwise iassign would be enough.
 */
MIRIntrinsicID RCLowering::SelectWriteBarrier(const MeStmt &stmt) {
  bool incWithLHS = stmt.NeedIncref();
  bool decWithLHS = stmt.NeedDecref();
  MeExpr *lhs = stmt.GetLHS();
  CHECK_FATAL(lhs != nullptr, "null ptr check");
  MeExprOp meOp = lhs->GetMeOp();
  CHECK_FATAL((meOp == kMeOpVar || meOp == kMeOpIvar), "Not Expected meOp");
  if (lhs->IsVolatile(ssaTab)) {
    if (meOp == kMeOpVar) {
      return PrepareVolatileCall(stmt, incWithLHS ? (decWithLHS ? INTRN_MCCWriteSVol : INTRN_MCCWriteSVolNoDec)
                                                  : (decWithLHS ? INTRN_MCCWriteSVolNoInc : INTRN_MCCWriteSVolNoRC));
    } else {
      if (static_cast<IvarMeExpr*>(lhs)->IsRCWeak()) {
        return PrepareVolatileCall(stmt, INTRN_MCCWriteVolWeak);
      }
      return PrepareVolatileCall(stmt, incWithLHS ? INTRN_MCCWriteVol : INTRN_MCCWriteVolNoInc);
    }
  } else {
    if (meOp == kMeOpVar) {
      return incWithLHS ? (decWithLHS ? INTRN_MCCWriteS : INTRN_MCCWriteSNoDec)
                        : (decWithLHS ? INTRN_MCCWriteSNoInc : INTRN_MCCWriteSNoRC);
    } else {
      if (static_cast<IvarMeExpr*>(lhs)->IsRCWeak()) {
        return INTRN_MCCWriteWeak;
      }
      return incWithLHS ? (decWithLHS ? INTRN_MCCWrite : INTRN_MCCWriteNoDec)
                        : (decWithLHS ? INTRN_MCCWriteNoInc : INTRN_MCCWriteNoRC);
    }
  }
}

void RCLowering::RCLower() {
  auto eIt = func.valid_end();
  for (auto bIt = func.valid_begin(); bIt != eIt; ++bIt) {
    if (bIt == func.common_entry() || bIt == func.common_exit()) {
      continue;
    }
    BBLower(**bIt);
  }
}

/*
 * if a var me expr is initialized by constructor, record it's initialized map
 * if a field id is not in initialized map, means the field has not been assigned a value
 * dec ref is not necessary in it's first assignment.
 */
void RCLowering::InitializedObjectFields(MeStmt &stmt) {
  if (stmt.GetOp() != OP_callassigned) {
    return;
  }
  auto &call = static_cast<CallMeStmt&>(stmt);
  MIRFunction *callee = GlobalTables::GetFunctionTable().GetFunctionFromPuidx(call.GetPUIdx());
  if (call.NumMeStmtOpnds() == 0 || call.GetOpnd(0)->GetMeOp() != kMeOpVar) {
    return;
  }
  MeExpr *firstOpnd = call.GetOpnd(0);
  bool isNew = gcMallocObjects.find(firstOpnd) != gcMallocObjects.end();
  bool hasNotInitialized = initializedFields.find(firstOpnd) == initializedFields.end();
  bool inInitializedMap =
      mirModule.GetPuIdxFieldInitializedMap().find(call.GetPUIdx()) != mirModule.GetPuIdxFieldInitializedMap().end();
  if (callee->IsConstructor() && isNew && hasNotInitialized && inInitializedMap) {
    initializedFields[firstOpnd] = mirModule.GetPUIdxFieldInitializedMapItem(call.GetPUIdx());
  } else {
    for (auto iter : call.GetOpnds()) {
      gcMallocObjects.erase(iter);
    }
  }
}

void RCLowering::BBLower(BB &bb) {
  MeExpr *pendingDec = nullptr;
  gcMallocObjects.clear();
  initializedFields.clear();
  needSpecialHandleException = bb.GetAttributes(kBBAttrIsCatch);
  for (auto &stmt : bb.GetMeStmts()) {
    pendingDec = stmt.GetLHSRef(ssaTab, false);
    Opcode opcode = stmt.GetOp();
    if (opcode == OP_return) {
      rets.push_back(&stmt);
    } else if (kOpcodeInfo.IsCallAssigned(opcode)) {
      MIRType *retType = stmt.GetReturnType();
      if (retType != nullptr && retType->GetPrimType() == PTY_ref) {
        HandleCallAssignedMeStmt(stmt, pendingDec);
      }
      InitializedObjectFields(stmt);
    } else if (stmt.IsAssign()) {
      HandleAssignMeStmt(stmt, pendingDec);
    } else {
      // handling is not necessary
    }
  }
  // there is no any statement exist whose opnd is the throw value, handle it
  if (needSpecialHandleException) {
    // create throw value
    OriginalSt *ost =
        ssaTab.GetOriginalStTable().FindOrCreatePregOriginalSt(-kSregThrownval, func.GetMirFunc()->GetPuidx());
    ost->SetTyIdx(GlobalTables::GetTypeTable().GetPrimType(PTY_ref)->GetTypeIndex());
    RegMeExpr *regReadExpr = irMap.CreateRegMeExprVersion(*ost);
    regReadExpr->SetPtyp(PTY_ref);
    MeStmt *firstMeStmt = to_ptr(bb.GetMeStmts().begin());
    std::vector<MeExpr*> opnds = { regReadExpr };
    IntrinsiccallMeStmt *decRefcall = CreateRCIntrinsic(INTRN_MCCDecRef, *firstMeStmt, opnds);
    bb.InsertMeStmtAfter(firstMeStmt, decRefcall);
  }
}

IntrinsiccallMeStmt *FindCleanupIntrinsic(const MeStmt &ret) {
  auto &meStmts = ret.GetBB()->GetMeStmts();
  for (auto iter = meStmts.rbegin(); iter != meStmts.rend(); ++iter) {
    if (CheckOp(to_ptr(iter), OP_intrinsiccall)) {
      auto *intrinsicCall = static_cast<IntrinsiccallMeStmt*>(to_ptr(iter));
      if (intrinsicCall->GetIntrinsic() == INTRN_MPL_CLEANUP_LOCALREFVARS) {
        return intrinsicCall;
      }
    }
  }
  return nullptr;
}

void RCLowering::HandleReturnVar(RetMeStmt &ret) {
  auto *retVar = static_cast<VarMeExpr*>(ret.GetOpnd(0));
  CHECK_FATAL(retVar != nullptr, "retVal null ptr check");
  MIRSymbol *sym = ssaTab.GetMIRSymbolFromID(retVar->GetOStIdx());
  if (sym != nullptr && sym->IgnoreRC()) {
    return;
  }
  if (sym != nullptr && sym->IsGlobal() && !sym->IsFinal()) {
    HandleReturnGlobal(ret);
  } else if (assignedPtrSym.count(sym) > 0 && sym->GetStorageClass() == kScAuto && sym->GetAttr(ATTR_localrefvar)) {
    /*
     * must be regreadAtReturn
     * checking localrefvar because some objects are meta
     */
    HandleReturnRegread(ret);
  } else {
    // if returning formal, incref unless placementRC is used and formal is NOT reassigned
    HandleReturnFormal(ret);
  }
}

void RCLowering::HandleReturnGlobal(RetMeStmt &ret) {
  BB *bb = ret.GetBB();
  CHECK_FATAL(bb != nullptr, "bb null ptr check");
  auto *retVar = static_cast<VarMeExpr*>(ret.GetOpnd(0));
  CHECK_FATAL(retVar != nullptr, "retVal null ptr check");
  std::vector<MeExpr*> opnds = { irMap.CreateAddrofMeExpr(*retVar) };
  IntrinsiccallMeStmt *loadCall = CreateRCIntrinsic(INTRN_MCCLoadRefS, ret, opnds, true);
  bb->InsertMeStmtBefore(&ret, loadCall);
  ret.SetOpnd(0, loadCall->GetMustDefList()->front().GetLHS());
}

void RCLowering::HandleReturnRegread(RetMeStmt &ret) {
  BB *bb = ret.GetBB();
  CHECK_FATAL(bb != nullptr, "bb null ptr check");
  auto *retVar = static_cast<VarMeExpr*>(ret.GetOpnd(0));
  CHECK_FATAL(retVar != nullptr, "retVal null ptr check");
  IntrinsiccallMeStmt *cleanup = FindCleanupIntrinsic(ret);
  if (cleanup == nullptr) {
    std::vector<MeExpr*> opnds = { retVar };
    IntrinsiccallMeStmt *incCall = CreateRCIntrinsic(INTRN_MCCIncRef, ret, opnds);
    bb->InsertMeStmtBefore(&ret, incCall);
  } else {
    // remove argument from intrinsiccall MPL_CLEANUP_LOCALREFVARS (dread ref %Reg1_R5678, ...
    MapleVector<MeExpr*> *opnds = &cleanup->GetOpnds();
    for (auto iter = opnds->begin(); iter != opnds->end(); iter++) {
      if (*iter == retVar || static_cast<VarMeExpr*>(*iter)->GetOStIdx() == retVar->GetOStIdx()) {
        opnds->erase(iter);
        opnds->push_back(retVar);  // pin it to end of std::vector
        cleanup->SetIntrinsic(INTRN_MPL_CLEANUP_LOCALREFVARS_SKIP);
        break;
      }
    }
  }
}

void RCLowering::HandleReturnFormal(RetMeStmt &ret) {
  BB *bb = ret.GetBB();
  CHECK_FATAL(bb != nullptr, "bb null ptr check");
  auto *retVar = static_cast<VarMeExpr*>(ret.GetOpnd(0));
  CHECK_FATAL(retVar != nullptr, "retVal null ptr check");
  std::vector<MeExpr*> opnds = { retVar };
  IntrinsiccallMeStmt *increfStmt = CreateRCIntrinsic(INTRN_MCCIncRef, ret, opnds, true);
  ret.SetOpnd(0, increfStmt->GetMustDefList()->front().GetLHS());
  IntrinsiccallMeStmt *cleanup = FindCleanupIntrinsic(ret);
  if (cleanup == nullptr) {
    bb->InsertMeStmtBefore(&ret, increfStmt);
  } else {
    bb->InsertMeStmtAfter(cleanup, increfStmt);
  }
}

void RCLowering::HandleReturnIvar(RetMeStmt &ret) {
  // insert calls
  auto *retIvar = static_cast<IvarMeExpr*>(ret.GetOpnd(0));
  if (retIvar->IsVolatile()) {
    std::vector<MeExpr*> opnds = { &retIvar->GetBase()->GetAddrExprBase(), irMap.CreateAddrofMeExpr(*retIvar) };
    IntrinsiccallMeStmt *loadCall = CreateRCIntrinsic(PrepareVolatileCall(ret, INTRN_MCCLoadRefVol), ret, opnds, true);
    ret.GetBB()->InsertMeStmtBefore(&ret, loadCall);
    ret.SetOpnd(0, loadCall->GetMustDefList()->front().GetLHS());
  } else {
    std::vector<MeExpr*> opnds = { &retIvar->GetBase()->GetAddrExprBase(), irMap.CreateAddrofMeExpr(*retIvar) };
    MeStmt *loadCall = CreateRCIntrinsic(INTRN_MCCLoadRef, ret, opnds, true);
    ret.GetBB()->InsertMeStmtBefore(&ret, loadCall);
    ret.SetOpnd(0, loadCall->GetMustDefList()->front().GetLHS());
  }
}

void RCLowering::HandleReturnReg(RetMeStmt &ret) {
  /*
   * if the register with ref value is defined by callassigned or gcmalloc
   * return incref should have been delegated and not needed.
   */
  auto *regRet = static_cast<RegMeExpr*>(ret.GetOpnd(0));
  if (regRet->GetDefBy() == kDefByMustDef) {
    return;
  }
  if (regRet->GetDefBy() == kDefByStmt && regRet->GetDefStmt()->GetOp() == OP_regassign) {
    MeExpr *rhs = regRet->GetDefStmt()->GetRHS();
    ASSERT(rhs != nullptr, "null ptr check");
    if (rhs->GetOp() == OP_gcmalloc || rhs->GetOp() == OP_gcmallocjarray) {
      return;
    }
  }
  std::vector<MeExpr*> opnds = { regRet };
  IntrinsiccallMeStmt *incCall = CreateRCIntrinsic(INTRN_MCCIncRef, ret, opnds, true);
  ret.SetOpnd(0, incCall->GetMustDefList()->front().GetLHS());
  ret.GetBB()->InsertMeStmtBefore(&ret, incCall);
}

void RCLowering::HandleReturnWithCleanup() {
  for (auto *stmt : rets) {
    auto *ret = static_cast<RetMeStmt*>(stmt);
    IntrinsiccallMeStmt *cleanup = FindCleanupIntrinsic(*ret);
    if (cleanup != nullptr && !tmpLocalRefVars.empty()) {  // new localrefvar introduced in this phase
      for (auto tmpVar : tmpLocalRefVars) {
        cleanup->GetOpnds().push_back(tmpVar);
      }
    }
    if (ret->GetOpnds().empty()) {
      continue;
    }
    MeExpr *retVal = ret->GetOpnd(0);
    // handle nullptr return first
    if (retVal->GetMeOp() == kMeOpConst && static_cast<ConstMeExpr*>(retVal)->IsZero()) {
      continue;
    }
    if (retVal->GetMeOp() == kMeOpVar) {
      HandleReturnVar(*ret);
    } else if (retVal->GetMeOp() == kMeOpIvar) {
      HandleReturnIvar(*ret);
    } else if (retVal->GetMeOp() == kMeOpReg) {
      HandleReturnReg(*ret);
    } else {
      // incref by default
      RegMeExpr *tmpReg = irMap.CreateRegMeExpr(PTY_ref);
      RegassignMeStmt *temp = irMap.CreateRegassignMeStmt(*tmpReg, *retVal, *stmt->GetBB());
      stmt->GetBB()->InsertMeStmtBefore(stmt, temp);
      std::vector<MeExpr*> opnds = { tmpReg };
      IntrinsiccallMeStmt *incCall = CreateRCIntrinsic(INTRN_MCCIncRef, *stmt, opnds);
      stmt->GetBB()->InsertMeStmtBefore(stmt, incCall);
      ret->SetOpnd(0, tmpReg);
    }
  }
}

void RCLowering::HandleReturnNeedBackup() {
  /*
   * any return value expression containing ivar has to be saved in a
   * temp with the temp being returned
   */
  for (auto *stmt : rets) {
    auto *ret = static_cast<RetMeStmt*>(stmt);
    if (ret->NumMeStmtOpnds() == 0) {
      continue;
    }
    MeExpr *retVal = ret->GetOpnd(0);
    if (!retVal->HasIvar()) {
      continue;
    }
    RegMeExpr *curTmp = irMap.CreateRegMeExpr(retVal->GetPrimType());
    MeStmt *regAssign = irMap.CreateRegassignMeStmt(*curTmp, *retVal, *ret->GetBB());
    ret->GetBB()->InsertMeStmtBefore(ret, regAssign);
    ret->SetOpnd(0, curTmp);
  }
}

void RCLowering::HandleReturnStmt() {
  bool returnRef = (func.GetMirFunc()->GetReturnType()->GetPrimType() == PTY_ref);
  bool updateCleanup = !tmpLocalRefVars.empty();
  if (returnRef || updateCleanup) {
    HandleReturnWithCleanup();
  } else {
    HandleReturnNeedBackup();
  }
}

void RCLowering::HandleArguments() {
  /*
   * handle arguments, if the formal gets modified
   * insert incref at entry and decref before all returns
   */
  MIRFunction *mirFunc = func.GetMirFunc();
  BB *firstBB = func.GetFirstBB();
  MeStmt *firstMeStmt = to_ptr(firstBB->GetMeStmts().begin());
  for (size_t i = (mirFunc->IsStatic() ? 0 : 1); i < mirFunc->GetFormalCount(); ++i) {
    MIRSymbol *sym = mirFunc->GetFormal(i);
    if (sym == nullptr || sym->IgnoreRC() || (!sym->IsGlobal() && assignedPtrSym.count(sym) == 0)) {
      continue;
    }
    VarMeExpr *argVar = CreateVarMeExprFromSym(*sym);
    CHECK_FATAL(argVar != nullptr, "null ptr check");
    IntrinsiccallMeStmt *incCall = nullptr;
    if (firstMeStmt != nullptr) {
      std::vector<MeExpr*> opnds = { argVar };
      incCall = CreateRCIntrinsic(INTRN_MCCIncRef, *firstMeStmt, opnds);
      firstBB->InsertMeStmtBefore(firstMeStmt, incCall);
    }
    TypeAttrs typeAttr = mirFunc->GetNthParamAttr(i);
    typeAttr.SetAttr(ATTR_localrefvar);
    mirFunc->SetNthParamAttr(i, typeAttr);
    sym->SetLocalRefVar();

    for (auto *stmt : rets) {
      std::vector<MeExpr*> opnds = { argVar };
      IntrinsiccallMeStmt *decrefCall = CreateRCIntrinsic(INTRN_MCCDecRef, *stmt, opnds);
      stmt->GetBB()->InsertMeStmtBefore(stmt, decrefCall);
    }
  }
}

void RCLowering::PostRCLower() {
  HandleArguments();
  // handle ret stmts
  HandleReturnStmt();
}

void RCLowering::Finish() {
  if (enabledDebug) {
    LogInfo::MapleLogger() << "\n============== After RC LOWERING =============" << '\n';
    func.Dump(false);
  }
}

OriginalSt *RCLowering::RetrieveOSt(const std::string &name, bool isLocalRefVar) const {
  MIRSymbol *backupSym = mirModule.GetMIRBuilder()->GetOrCreateLocalDecl(
      name, *GlobalTables::GetTypeTable().GetTypeFromTyIdx(TyIdx(PTY_ptr)));  // use PTY_ptr for temp
  backupSym->SetIsTmp(true);
  if (isLocalRefVar) {
    backupSym->SetLocalRefVar();
  }
  OriginalSt *ost = ssaTab.FindOrCreateSymbolOriginalSt(*backupSym, func.GetMirFunc()->GetPuidx(), 0);
  return ost;
}

// function for creating short-lived temp
VarMeExpr *RCLowering::CreateNewTmpVarMeExpr(bool isLocalRefVar) {
  std::string name = std::string("__RCTemp__").append(std::to_string(++tmpCount));
  OriginalSt *ost = RetrieveOSt(name, isLocalRefVar);
  if (ost->GetZeroVersionIndex() == 0) {
    ost->SetZeroVersionIndex(irMap.GetVerst2MeExprTableSize());
    irMap.PushBackVerst2MeExprTable(nullptr);
    ost->PushbackVersionIndex(ost->GetZeroVersionIndex());
  }
  VarMeExpr *varMeExpr = irMap.CreateNewVarMeExpr(*ost, PTY_ref, 0);
  if (isLocalRefVar) {
    tmpLocalRefVars.insert(varMeExpr);
  }
  return varMeExpr;
}

AnalysisResult *MeDoRCLowering::Run(MeFunction *func, MeFuncResultMgr *funcResMgr, ModuleResultMgr *moduleResMgr) {
  auto *kh = static_cast<KlassHierarchy*>(moduleResMgr->GetAnalysisResult(MoPhase_CHA, &func->GetMIRModule()));
  ASSERT(kh != nullptr, "KlassHierarchy has problem");
  if (func->GetIRMap() == nullptr) {
    auto *hmap = static_cast<MeIRMap*>(funcResMgr->GetAnalysisResult(MeFuncPhase_IRMAP, func));
    CHECK_FATAL(hmap != nullptr, "hssamap has problem");
    func->SetIRMap(hmap);
  }
  CHECK_FATAL(func->GetMeSSATab() != nullptr, "ssatab has problem");
  RCLowering rcLowering(*func, *kh, DEBUGFUNC(func));

  rcLowering.Prepare();
  rcLowering.PreRCLower();
  rcLowering.RCLower();
  // handle all the extra RC work
  rcLowering.PostRCLower();
  rcLowering.Finish();
  return nullptr;
}
}  // namespace maple
