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
#ifndef MAPLE_IR_INCLUDE_MIR_NODES_H
#define MAPLE_IR_INCLUDE_MIR_NODES_H
#include <sstream>
#include <utility>
#include "opcodes.h"
#include "opcode_info.h"
#include "mir_type.h"
#include "mir_module.h"
#include "mir_const.h"
#include "maple_string.h"
#include "ptr_list_ref.h"

namespace maple {
class MIRPregTable;  // circular dependency exists, no other choice
class TypeTable;  // circular dependency exists, no other choice

struct RegFieldPair {
 public:
  RegFieldPair() : fieldID(0), pregIdx(0) {}

  RegFieldPair(FieldID fidx, PregIdx16 pidx) : fieldID(fidx), pregIdx(pidx) {}

  bool IsReg() const {
    return pregIdx > 0;
  }

  FieldID GetFieldID() const {
    return fieldID;
  }

  PregIdx16 GetPregIdx() const {
    return pregIdx;
  }

  void SetFieldID(FieldID fld) {
    fieldID = fld;
  }

  void SetPregIdx(PregIdx16 idx) {
    pregIdx = idx;
  }

 private:
  FieldID fieldID;
  PregIdx16 pregIdx;
};

using CallReturnPair = std::pair<StIdx, RegFieldPair>;
using CallReturnVector = MapleVector<CallReturnPair>;
// Made public so that other modules (such as maplebe) can print intrinsic names
// in debug information or comments in assembly files.
const char *GetIntrinsicName(MIRIntrinsicID intrn);
class BaseNode {
 public:
  explicit BaseNode(Opcode o) {
    op = o;
    ptyp = kPtyInvalid;
    typeFlag = 0;
    numOpnds = 0;
  }

  BaseNode(Opcode o, uint8 numOpr) {
    op = o;
    ptyp = kPtyInvalid;
    typeFlag = 0;
    numOpnds = numOpr;
  }

  BaseNode(const Opcode o, const PrimType typ, uint8 numOpr) {
    op = o;
    ptyp = typ;
    typeFlag = 0;
    numOpnds = numOpr;
  }

  virtual ~BaseNode() = default;

  virtual BaseNode *CloneTree(MapleAllocator &allocator) const {
    return allocator.GetMemPool()->New<BaseNode>(*this);
  }

  virtual void DumpBase(const MIRModule &mod, int32 indent) const;

  virtual void Dump(const MIRModule &mod, int32 indent) const {
    DumpBase(mod, indent);
  }

  void Dump(const MIRModule &mod) const {
    Dump(mod, 0);
    LogInfo::MapleLogger() << std::endl;
  }

  virtual uint8 SizeOfInstr() const {
    return kOpcodeInfo.GetTableItemAt(GetOpCode()).instrucSize;
  }

  const char *GetOpName() const;
  bool MayThrowException(void);
  virtual uint8 NumOpnds(void) const {
    return numOpnds;
  }

  uint8 GetNumOpnds() const {
    return numOpnds;
  }

  void SetNumOpnds(uint8 num) {
    numOpnds = num;
  }

  Opcode GetOpCode() const {
    return op;
  }

  void SetOpCode(Opcode o) {
    op = o;
  }

  PrimType GetPrimType() const {
    return ptyp;
  }

  void SetPrimType(PrimType type) {
    ptyp = type;
  }

  virtual BaseNode *Opnd(size_t i = 0) const {
    ASSERT(0, "override needed");
    return nullptr;
  }

  virtual void SetOpnd(BaseNode *node, size_t i = 0) {
    ASSERT(0, "This should not happen");
  }

  virtual bool IsLeaf() const {
    return true;
  }

  virtual CallReturnVector *GetCallReturnVector() {
    return nullptr;
  }

  virtual MIRType *GetCallReturnType() {
    return nullptr;
  }

  virtual bool IsUnaryNode() const {
    return false;
  }

  virtual bool IsBinaryNode() const {
    return false;
  }

  virtual bool IsTernaryNode() const {
    return false;
  }

  virtual bool IsNaryNode() const {
    return false;
  }

  bool IsCondBr() const {
    return kOpcodeInfo.IsCondBr(GetOpCode());
  }

  virtual bool Verify() const {
    return true;
  }

 protected:
  Opcode op;
  PrimType ptyp;
  uint8 typeFlag;  // a flag to speed up type related operations in the VM
  uint8 numOpnds;  // only used for N-ary operators, switch and rangegoto
                   // operands immediately before each node
};

class UnaryNode : public BaseNode {
 public:
  explicit UnaryNode(Opcode o) : BaseNode(o, 1), uOpnd(nullptr) {}

  UnaryNode(Opcode o, PrimType typ) : BaseNode(o, typ, 1), uOpnd(nullptr) {}

  UnaryNode(Opcode o, PrimType typ, BaseNode *expr) : BaseNode(o, typ, 1), uOpnd(expr) {}

  virtual ~UnaryNode() = default;

  void DumpOpnd(const MIRModule &mod, int32 indent) const;
  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  UnaryNode *CloneTree(MapleAllocator &allocator) const {
    UnaryNode *nd = allocator.GetMemPool()->New<UnaryNode>(*this);
    nd->SetOpnd(uOpnd->CloneTree(allocator), 0);
    return nd;
  }

  BaseNode *Opnd(size_t i = 0) const {
    ASSERT(i == 0, "Invalid operand idx in UnaryNode");
    return uOpnd;
  }

  uint8 NumOpnds() const {
    return 1;
  }

  void SetOpnd(BaseNode *node, size_t i = 0) {
    uOpnd = node;
  }

  bool IsLeaf() const {
    return false;
  }

  bool IsUnaryNode() const {
    return true;
  }

 private:
  BaseNode *uOpnd;
};

class TypeCvtNode : public UnaryNode {
 public:
  explicit TypeCvtNode(Opcode o) : UnaryNode(o), fromPrimType(kPtyInvalid) {}

  TypeCvtNode(Opcode o, PrimType typ) : UnaryNode(o, typ), fromPrimType(kPtyInvalid) {}

  TypeCvtNode(Opcode o, PrimType typ, PrimType fromtyp, BaseNode *expr)
      : UnaryNode(o, typ, expr), fromPrimType(fromtyp) {}

  ~TypeCvtNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  TypeCvtNode *CloneTree(MapleAllocator &allocator) const {
    TypeCvtNode *nd = allocator.GetMemPool()->New<TypeCvtNode>(*this);
    nd->SetOpnd(Opnd(0)->CloneTree(allocator), 0);
    return nd;
  }

  PrimType FromType(void) const {
    return fromPrimType;
  }

  void SetFromType(PrimType from) {
    fromPrimType = from;
  }

 private:
  PrimType fromPrimType;
};

// used for retype
class RetypeNode : public TypeCvtNode {
 public:
  RetypeNode() : TypeCvtNode(OP_retype), tyIdx(0) {}

  explicit RetypeNode(PrimType typ) : TypeCvtNode(OP_retype, typ), tyIdx(0) {}

  RetypeNode(PrimType typ, PrimType fromtyp, TyIdx idx, BaseNode *expr)
      : TypeCvtNode(OP_retype, typ, fromtyp, expr), tyIdx(idx) {}

  ~RetypeNode() = default;
  void Dump(const MIRModule &mod, int32 indent) const;

  RetypeNode *CloneTree(MapleAllocator &allocator) const {
    RetypeNode *nd = allocator.GetMemPool()->New<RetypeNode>(*this);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    return nd;
  }

  const TyIdx &GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(const TyIdx tyIdxVal) {
    tyIdx = tyIdxVal;
  }

 private:
  TyIdx tyIdx;
};

// used for extractbits, sext, zext
class ExtractbitsNode : public UnaryNode {
 public:
  explicit ExtractbitsNode(Opcode o) : UnaryNode(o), bitsOffset(0), bitsSize(0) {}

  ExtractbitsNode(Opcode o, PrimType typ) : UnaryNode(o, typ), bitsOffset(0), bitsSize(0) {}

  ExtractbitsNode(Opcode o, PrimType typ, uint8 offset, uint8 size)
      : UnaryNode(o, typ), bitsOffset(offset), bitsSize(size) {}

  ExtractbitsNode(Opcode o, PrimType typ, uint8 offset, uint8 size, BaseNode *expr)
      : UnaryNode(o, typ, expr), bitsOffset(offset), bitsSize(size) {}

  ~ExtractbitsNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  ExtractbitsNode *CloneTree(MapleAllocator &allocator) const {
    ExtractbitsNode *nd = allocator.GetMemPool()->New<ExtractbitsNode>(*this);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    return nd;
  }

  uint8 GetBitsOffset() const {
    return bitsOffset;
  }

  void SetBitsOffset(uint8 offset) {
    bitsOffset = offset;
  }

  uint8 GetBitsSize() const {
    return bitsSize;
  }

  void SetBitsSize(uint8 size) {
    bitsSize = size;
  }

 private:
  uint8 bitsOffset;
  uint8 bitsSize;
};

class GCMallocNode : public BaseNode {
 public:
  explicit GCMallocNode(Opcode o) : BaseNode(o), tyIdx(0), origPrimType(kPtyInvalid) {}

  GCMallocNode(Opcode o, PrimType typ, TyIdx tIdx) : BaseNode(o, typ, 0), tyIdx(tIdx), origPrimType(kPtyInvalid) {}

  ~GCMallocNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  GCMallocNode *CloneTree(MapleAllocator &allocator) const {
    GCMallocNode *nd = allocator.GetMemPool()->New<GCMallocNode>(*this);
    return nd;
  }

  TyIdx GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(TyIdx idx) {
    tyIdx = idx;
  }

  void SetOrigPType(PrimType type) {
    origPrimType = type;
  }

 private:
  TyIdx tyIdx;
  PrimType origPrimType;
};

class JarrayMallocNode : public UnaryNode {
 public:
  explicit JarrayMallocNode(Opcode o) : UnaryNode(o), tyIdx(0) {}

  JarrayMallocNode(Opcode o, PrimType typ) : UnaryNode(o, typ), tyIdx(0) {}

  JarrayMallocNode(Opcode o, PrimType typ, TyIdx typeIdx) : UnaryNode(o, typ), tyIdx(typeIdx) {}

  JarrayMallocNode(Opcode o, PrimType typ, TyIdx typeIdx, BaseNode *opnd) : UnaryNode(o, typ, opnd), tyIdx(typeIdx) {}

  ~JarrayMallocNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  JarrayMallocNode *CloneTree(MapleAllocator &allocator) const {
    JarrayMallocNode *nd = allocator.GetMemPool()->New<JarrayMallocNode>(*this);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    return nd;
  }

  TyIdx GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(TyIdx idx) {
    tyIdx = idx;
  }

 private:
  TyIdx tyIdx;
};

// iaddrof also use this node
class IreadNode : public UnaryNode {
 public:
  explicit IreadNode(Opcode o) : UnaryNode(o), tyIdx(0), fieldID(0) {}

  IreadNode(Opcode o, PrimType typ) : UnaryNode(o, typ), tyIdx(0), fieldID(0) {}

  IreadNode(Opcode o, PrimType typ, TyIdx typeIdx, FieldID fid) : UnaryNode(o, typ), tyIdx(typeIdx), fieldID(fid) {}

  IreadNode(Opcode o, PrimType typ, TyIdx typeIdx, FieldID fid, BaseNode *expr)
      : UnaryNode(o, typ, expr), tyIdx(typeIdx), fieldID(fid) {}

  ~IreadNode() = default;
  virtual void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  IreadNode *CloneTree(MapleAllocator &allocator) const {
    IreadNode *nd = allocator.GetMemPool()->New<IreadNode>(*this);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    return nd;
  }

  const TyIdx &GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(const TyIdx tyIdxVal) {
    tyIdx = tyIdxVal;
  }

  FieldID GetFieldID() const {
    return fieldID;
  }

  void SetFieldID(FieldID fieldIDVal) {
    fieldID = fieldIDVal;
  }

  // the base of an address expr is either a leaf or an iread
  BaseNode &GetAddrExprBase() const {
    BaseNode *base = Opnd();
    while (base->NumOpnds() != 0 && base->GetOpCode() != OP_iread) {
      base = base->Opnd(0);
    }
    return *base;
  }

 protected:
  TyIdx tyIdx;
  FieldID fieldID;
};

// IaddrofNode has the same member fields and member methods as IreadNode
using IaddrofNode = IreadNode;

class IreadoffNode : public UnaryNode {
 public:
  IreadoffNode() : UnaryNode(OP_ireadoff), offset(0) {}

  IreadoffNode(PrimType ptyp, int32 ofst) : UnaryNode(OP_ireadoff, ptyp), offset(ofst) {}

  IreadoffNode(PrimType ptyp, BaseNode *opnd, int32 ofst) : UnaryNode(OP_ireadoff, ptyp, opnd), offset(ofst) {}

  ~IreadoffNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  IreadoffNode *CloneTree(MapleAllocator &allocator) const {
    IreadoffNode *nd = allocator.GetMemPool()->New<IreadoffNode>(*this);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    return nd;
  }

  int32 GetOffset() const {
    return offset;
  }

  void SetOffset(int32 offsetValue) {
    offset = offsetValue;
  }

 private:
  int32 offset;
};

class IreadFPoffNode : public BaseNode {
 public:
  IreadFPoffNode() : BaseNode(OP_ireadfpoff), offset(0) {}

  IreadFPoffNode(PrimType ptyp, int32 ofst) : BaseNode(OP_ireadfpoff, ptyp), offset(ofst) {}

  ~IreadFPoffNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  IreadFPoffNode *CloneTree(MapleAllocator &allocator) const {
    IreadFPoffNode *nd = allocator.GetMemPool()->New<IreadFPoffNode>(*this);
    return nd;
  }

  int32 GetOffset() const {
    return offset;
  }

  void SetOffset(int32 offsetValue) {
    offset = offsetValue;
  }

 private:
  int32 offset;
};

class BinaryOpnds {
 public:
  virtual ~BinaryOpnds() = default;

  virtual void Dump(const MIRModule &mod, int32 indent) const;

  BaseNode *GetBOpnd(int32 i) const {
    CHECK_FATAL(i >= 0 && i < kOperandNumBinary, "Invalid operand idx in BinaryOpnds");
    return bOpnd[i];
  }

  void SetBOpnd(BaseNode *node, int32 i) {
    CHECK_FATAL(i >= 0 && i < kOperandNumBinary, "Invalid operand idx in BinaryOpnds");
    bOpnd[i] = node;
  }

 private:
  BaseNode *bOpnd[kOperandNumBinary];
};

class BinaryNode : public BaseNode, public BinaryOpnds {
 public:
  explicit BinaryNode(Opcode o) : BaseNode(o, kOperandNumBinary) {}

  BinaryNode(Opcode o, PrimType typ) : BaseNode(o, typ, kOperandNumBinary) {}

  BinaryNode(Opcode o, PrimType typ, BaseNode *l, BaseNode *r) : BaseNode(o, typ, kOperandNumBinary) {
    SetBOpnd(l, 0);
    SetBOpnd(r, 1);
  }

  ~BinaryNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  void Dump(const MIRModule &mod) const;
  bool Verify() const;

  BinaryNode *CloneTree(MapleAllocator &allocator) const {
    BinaryNode *nd = allocator.GetMemPool()->New<BinaryNode>(*this);
    nd->SetBOpnd(GetBOpnd(0)->CloneTree(allocator), 0);
    nd->SetBOpnd(GetBOpnd(1)->CloneTree(allocator), 1);
    return nd;
  }

  bool IsCommutative() const {
    switch (GetOpCode()) {
      case OP_add:
      case OP_mul:
      case OP_band:
      case OP_bior:
      case OP_bxor:
      case OP_land:
      case OP_lior:
        return true;
      default:
        return false;
    }
  }

  BaseNode *Opnd(size_t i) const {
    ASSERT(i < kOperandNumBinary, "invalid operand idx in BinaryNode");
    ASSERT(i >= 0, "invalid operand idx in BinaryNode");
    return GetBOpnd(i);
  }

  uint8 NumOpnds() const {
    return kOperandNumBinary;
  }

  void SetOpnd(BaseNode *node, size_t i = 0) {
    SetBOpnd(node, i);
  }

  bool IsLeaf() const {
    return false;
  }

  bool IsBinaryNode() const {
    return true;
  }
};

class CompareNode : public BinaryNode {
 public:
  explicit CompareNode(Opcode o) : BinaryNode(o), opndType(kPtyInvalid) {}

  CompareNode(Opcode o, PrimType typ) : BinaryNode(o, typ), opndType(kPtyInvalid) {}

  CompareNode(Opcode o, PrimType typ, PrimType otype, BaseNode *l, BaseNode *r)
      : BinaryNode(o, typ, l, r), opndType(otype) {}

  ~CompareNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  void Dump(const MIRModule &mod) const;
  bool Verify() const;

  CompareNode *CloneTree(MapleAllocator &allocator) const {
    CompareNode *nd = allocator.GetMemPool()->New<CompareNode>(*this);
    nd->SetBOpnd(GetBOpnd(0)->CloneTree(allocator), 0);
    nd->SetBOpnd(GetBOpnd(1)->CloneTree(allocator), 1);
    return nd;
  }

  PrimType GetOpndType() const {
    return opndType;
  }

  void SetOpndType(PrimType type) {
    opndType = type;
  }

 private:
  PrimType opndType;  // type of operands.
};

class DepositbitsNode : public BinaryNode {
 public:
  DepositbitsNode() : BinaryNode(OP_depositbits), bitsOffset(0), bitsSize(0) {}

  DepositbitsNode(Opcode o, PrimType typ) : BinaryNode(o, typ), bitsOffset(0), bitsSize(0) {}

  DepositbitsNode(Opcode o, PrimType typ, uint8 offset, uint8 size, BaseNode *l, BaseNode *r)
      : BinaryNode(o, typ, l, r), bitsOffset(offset), bitsSize(size) {}

  ~DepositbitsNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  DepositbitsNode *CloneTree(MapleAllocator &allocator) const {
    DepositbitsNode *nd = allocator.GetMemPool()->New<DepositbitsNode>(*this);
    nd->SetBOpnd(GetBOpnd(0)->CloneTree(allocator), 0);
    nd->SetBOpnd(GetBOpnd(1)->CloneTree(allocator), 1);
    return nd;
  }

  uint8 GetBitsOffset() const {
    return bitsOffset;
  }

  void SetBitsOffset(uint8 offset) {
    bitsOffset = offset;
  }

  uint8 GetBitsSize() const {
    return bitsSize;
  }

  void SetBitsSize(uint8 size) {
    bitsSize = size;
  }

 private:
  uint8 bitsOffset;
  uint8 bitsSize;
};

// used for resolveinterfacefunc, resolvevirtualfunc
// bOpnd[0] stores base vtab/itab address
// bOpnd[1] stores offset
class ResolveFuncNode : public BinaryNode {
 public:
  explicit ResolveFuncNode(Opcode o) : BinaryNode(o), puIdx(0) {}

  ResolveFuncNode(Opcode o, PrimType typ) : BinaryNode(o, typ), puIdx(0) {}

  ResolveFuncNode(Opcode o, PrimType typ, PUIdx idx) : BinaryNode(o, typ), puIdx(idx) {}

  ResolveFuncNode(Opcode o, PrimType typ, PUIdx pIdx, BaseNode *opnd0, BaseNode *opnd1)
      : BinaryNode(o, typ, opnd0, opnd1), puIdx(pIdx) {}

  ~ResolveFuncNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  ResolveFuncNode *CloneTree(MapleAllocator &allocator) const {
    ResolveFuncNode *nd = allocator.GetMemPool()->New<ResolveFuncNode>(*this);
    nd->SetBOpnd(GetBOpnd(0)->CloneTree(allocator), 0);
    nd->SetBOpnd(GetBOpnd(1)->CloneTree(allocator), 1);
    return nd;
  }

  BaseNode *GetTabBaseAddr() const {
    return GetBOpnd(0);
  }

  BaseNode *GetOffset() const {
    return GetBOpnd(1);
  }

  PUIdx GetPuIdx() const {
    return puIdx;
  }

  void SetPuIdx(PUIdx idx) {
    puIdx = idx;
  }

 private:
  PUIdx puIdx;
};

class TernaryNode : public BaseNode {
 public:
  explicit TernaryNode(Opcode o) : BaseNode(o, kOperandNumTernary) {
    topnd[0] = nullptr;
    topnd[1] = nullptr;
    topnd[2] = nullptr;
  }

  TernaryNode(Opcode o, PrimType typ) : BaseNode(o, typ, kOperandNumTernary) {
    topnd[0] = nullptr;
    topnd[1] = nullptr;
    topnd[2] = nullptr;
  }

  TernaryNode(Opcode o, PrimType typ, BaseNode *e0, BaseNode *e1, BaseNode *e2) : BaseNode(o, typ, kOperandNumTernary) {
    topnd[0] = e0;
    topnd[1] = e1;
    topnd[2] = e2;
  }

  ~TernaryNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const override;
  bool Verify() const override;

  TernaryNode *CloneTree(MapleAllocator &allocator) const override {
    TernaryNode *nd = allocator.GetMemPool()->New<TernaryNode>(*this);
    nd->topnd[0] = topnd[0]->CloneTree(allocator);
    nd->topnd[1] = topnd[1]->CloneTree(allocator);
    nd->topnd[2] = topnd[2]->CloneTree(allocator);
    return nd;
  }

  BaseNode *Opnd(size_t i) const override {
    CHECK_FATAL(i < kOperandNumTernary, "array index out of range");
    return topnd[i];
  }

  uint8 NumOpnds() const override {
    return kOperandNumTernary;
  }

  void SetOpnd(BaseNode *node, size_t i = 0) override {
    CHECK_FATAL(i < kOperandNumTernary, "array index out of range");
    topnd[i] = node;
  }

  bool IsLeaf() const override {
    return false;
  }

  bool IsTernaryNode() const override {
    return true;
  }

 private:
  BaseNode *topnd[kOperandNumTernary];
};

class NaryOpnds {
 public:
  explicit NaryOpnds(MapleAllocator &mpallocter) : nOpnd(mpallocter.Adapter()) {}

  virtual ~NaryOpnds() = default;

  virtual void Dump(const MIRModule &mod, int32 indent) const;
  bool VerifyOpnds() const;

  const MapleVector<BaseNode*> &GetNopnd() const {
    return nOpnd;
  }

  MapleVector<BaseNode*> &GetNopnd() {
    return nOpnd;
  }

  size_t GetNopndSize() const {
    return nOpnd.size();
  }

  BaseNode *GetNopndAt(size_t i) const {
    CHECK_FATAL(i < nOpnd.size(), "array index out of range");
    return nOpnd[i];
  }

  void SetNOpndAt(size_t i, BaseNode *opnd) {
    CHECK_FATAL(i < nOpnd.size(), "array index out of range");
    nOpnd[i] = opnd;
  }

  void SetNOpnd(const MapleVector<BaseNode*> &val) {
    nOpnd = val;
  }

 private:
  MapleVector<BaseNode*> nOpnd;
};

class NaryNode : public BaseNode, public NaryOpnds {
 public:
  NaryNode(MapleAllocator &allocator, Opcode o) : BaseNode(o), NaryOpnds(allocator) {}

  NaryNode(const MIRModule &mod, Opcode o) : NaryNode(mod.GetCurFuncCodeMPAllocator(), o) {}

  NaryNode(MapleAllocator &allocator, Opcode o, PrimType typ) : BaseNode(o, typ, 0), NaryOpnds(allocator) {}

  NaryNode(const MIRModule &mod, Opcode o, PrimType typ) : NaryNode(mod.GetCurFuncCodeMPAllocator(), o, typ) {}

  NaryNode(MapleAllocator &allocator, const NaryNode &node)
      : BaseNode(node.GetOpCode(), node.GetPrimType(), node.numOpnds), NaryOpnds(allocator) {}

  NaryNode(const MIRModule &mod, const NaryNode &node) : NaryNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  NaryNode(NaryNode &node) = delete;
  NaryNode &operator=(const NaryNode &node) = delete;
  ~NaryNode() = default;

  virtual void Dump(const MIRModule &mod, int32 indent) const override;

  NaryNode *CloneTree(MapleAllocator &allocator) const override {
    NaryNode *nd = allocator.GetMemPool()->New<NaryNode>(allocator, *this);
    for (size_t i = 0; i < GetNopndSize(); i++) {
      nd->GetNopnd().push_back(GetNopndAt(i)->CloneTree(allocator));
    }
    return nd;
  }

  BaseNode *Opnd(size_t i) const override {
    return GetNopndAt(i);
  }

  uint8 NumOpnds() const override {
    ASSERT(numOpnds == GetNopndSize(), "NaryNode has wrong numOpnds field");
    return GetNopndSize();
  }

  void SetOpnd(BaseNode *node, size_t i = 0) override {
    ASSERT(i < GetNopnd().size(), "array index out of range");
    SetNOpndAt(i, node);
  }

  bool IsLeaf() const override {
    return false;
  }

  bool Verify() const override {
    return true;
  }

  bool IsNaryNode() const override {
    return true;
  }
};

class IntrinsicopNode : public NaryNode {
 public:
  IntrinsicopNode(MapleAllocator &allocator, Opcode o, TyIdx typeIdx = TyIdx())
      : NaryNode(allocator, o), intrinsic(INTRN_UNDEFINED), tyIdx(typeIdx) {}

  IntrinsicopNode(const MIRModule &mod, Opcode o, TyIdx typeIdx = TyIdx())
      : IntrinsicopNode(mod.GetCurFuncCodeMPAllocator(), o, typeIdx) {}

  IntrinsicopNode(MapleAllocator &allocator, Opcode o, PrimType typ, TyIdx typeIdx = TyIdx())
      : NaryNode(allocator, o, typ), intrinsic(INTRN_UNDEFINED), tyIdx(typeIdx) {}

  IntrinsicopNode(const MIRModule &mod, Opcode o, PrimType typ, TyIdx typeIdx = TyIdx())
      : IntrinsicopNode(mod.GetCurFuncCodeMPAllocator(), o, typ, typeIdx) {}

  IntrinsicopNode(MapleAllocator &allocator, const IntrinsicopNode &node)
      : NaryNode(allocator, node), intrinsic(node.GetIntrinsic()), tyIdx(node.GetTyIdx()) {}

  IntrinsicopNode(const MIRModule &mod, const IntrinsicopNode &node)
      : IntrinsicopNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  IntrinsicopNode(IntrinsicopNode &node) = delete;
  IntrinsicopNode &operator=(const IntrinsicopNode &node) = delete;
  ~IntrinsicopNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  IntrinsicopNode *CloneTree(MapleAllocator &allocator) const {
    IntrinsicopNode *nd = allocator.GetMemPool()->New<IntrinsicopNode>(allocator, *this);
    for (size_t i = 0; i < GetNopndSize(); i++) {
      nd->GetNopnd().push_back(GetNopndAt(i)->CloneTree(allocator));
    }
    nd->SetNumOpnds(GetNopndSize());
    return nd;
  }

  MIRIntrinsicID GetIntrinsic() const {
    return intrinsic;
  }

  void SetIntrinsic(MIRIntrinsicID intrinsicID) {
    intrinsic = intrinsicID;
  }

  TyIdx GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(TyIdx idx) {
    tyIdx = idx;
  }

 private:
  MIRIntrinsicID intrinsic;
  TyIdx tyIdx;
};

class ConstvalNode : public BaseNode {
 public:
  ConstvalNode() : BaseNode(OP_constval), constVal(nullptr) {}

  explicit ConstvalNode(PrimType typ) : BaseNode(OP_constval, typ, 0), constVal(nullptr) {}

  explicit ConstvalNode(MIRConst *constv) : BaseNode(OP_constval), constVal(constv) {}

  ConstvalNode(PrimType typ, MIRConst *constv) : BaseNode(OP_constval, typ, 0), constVal(constv) {}
  ~ConstvalNode() = default;
  void Dump(const MIRModule &mod, int32 indent) const;

  ConstvalNode *CloneTree(MapleAllocator &allocator) const {
    return allocator.GetMemPool()->New<ConstvalNode>(*this);
  }

  const MIRConst *GetConstVal() const {
    return constVal;
  }

  MIRConst *GetConstVal() {
    return constVal;
  }

  void SetConstVal(MIRConst *val) {
    constVal = val;
  }

 private:
  MIRConst *constVal;
};

class ConststrNode : public BaseNode {
 public:
  ConststrNode() : BaseNode(OP_conststr), strIdx(0) {}

  explicit ConststrNode(UStrIdx i) : BaseNode(OP_conststr), strIdx(i) {}

  ConststrNode(PrimType typ, UStrIdx i) : BaseNode(OP_conststr, typ, 0), strIdx(i) {}

  ~ConststrNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  ConststrNode *CloneTree(MapleAllocator &allocator) const {
    return allocator.GetMemPool()->New<ConststrNode>(*this);
  }

  UStrIdx GetStrIdx() const {
    return strIdx;
  }

  void SetStrIdx(UStrIdx idx) {
    strIdx = idx;
  }

 private:
  UStrIdx strIdx;
};

class Conststr16Node : public BaseNode {
 public:
  Conststr16Node() : BaseNode(OP_conststr16), strIdx(0) {}

  explicit Conststr16Node(U16StrIdx i) : BaseNode(OP_conststr16), strIdx(i) {}

  Conststr16Node(PrimType typ, U16StrIdx i) : BaseNode(OP_conststr16, typ, 0), strIdx(i) {}

  ~Conststr16Node() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  Conststr16Node *CloneTree(MapleAllocator &allocator) const {
    return allocator.GetMemPool()->New<Conststr16Node>(*this);
  }

  U16StrIdx GetStrIdx() const {
    return strIdx;
  }

  void SetStrIdx(U16StrIdx idx) {
    strIdx = idx;
  }

 private:
  U16StrIdx strIdx;
};

class SizeoftypeNode : public BaseNode {
 public:
  SizeoftypeNode() : BaseNode(OP_sizeoftype), tyIdx(0) {}

  explicit SizeoftypeNode(TyIdx t) : BaseNode(OP_sizeoftype), tyIdx(t) {}

  SizeoftypeNode(PrimType type, TyIdx t) : BaseNode(OP_sizeoftype, type, 0), tyIdx(t) {}

  ~SizeoftypeNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  SizeoftypeNode *CloneTree(MapleAllocator &allocator) const {
    return allocator.GetMemPool()->New<SizeoftypeNode>(*this);
  }

  TyIdx GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(TyIdx idx) {
    tyIdx = idx;
  }

 private:
  TyIdx tyIdx;
};

class FieldsDistNode : public BaseNode {
 public:
  FieldsDistNode() : BaseNode(OP_fieldsdist), tyIdx(0), fieldID1(0), fieldID2(0) {}

  FieldsDistNode(TyIdx t, FieldID f1, FieldID f2) : BaseNode(OP_fieldsdist), tyIdx(t), fieldID1(f1), fieldID2(f2) {}

  FieldsDistNode(PrimType typ, TyIdx t, FieldID f1, FieldID f2)
      : BaseNode(OP_fieldsdist, typ, 0), tyIdx(t), fieldID1(f1), fieldID2(f2) {}

  ~FieldsDistNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  FieldsDistNode *CloneTree(MapleAllocator &allocator) const {
    return allocator.GetMemPool()->New<FieldsDistNode>(*this);
  }

  TyIdx GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(TyIdx idx) {
    tyIdx = idx;
  }

  FieldID GetFiledID1() const {
    return fieldID1;
  }

  void SetFiledID1(FieldID id) {
    fieldID1 = id;
  }

  FieldID GetFiledID2() const {
    return fieldID2;
  }

  void SetFiledID2(FieldID id) {
    fieldID2 = id;
  }

 private:
  TyIdx tyIdx;
  FieldID fieldID1;
  FieldID fieldID2;
};

class ArrayNode : public NaryNode {
 public:
  ArrayNode(MapleAllocator &allocator) : NaryNode(allocator, OP_array), boundsCheck(true) {}

  explicit ArrayNode(const MIRModule &mod) : ArrayNode(mod.GetCurFuncCodeMPAllocator()) {}

  ArrayNode(MapleAllocator &allocator, PrimType typ, TyIdx idx)
      : NaryNode(allocator, OP_array, typ), tyIdx(idx), boundsCheck(true) {}

  ArrayNode(const MIRModule &mod, PrimType typ, TyIdx idx) : ArrayNode(mod.GetCurFuncCodeMPAllocator(), typ, idx) {}

  ArrayNode(MapleAllocator &allocator, PrimType typ, TyIdx idx, bool bcheck)
      : NaryNode(allocator, OP_array, typ), tyIdx(idx), boundsCheck(bcheck) {}

  ArrayNode(const MIRModule &mod, PrimType typ, TyIdx idx, bool bcheck)
      : ArrayNode(mod.GetCurFuncCodeMPAllocator(), typ, idx, bcheck) {}

  ArrayNode(MapleAllocator &allocator, const ArrayNode &node)
      : NaryNode(allocator, node), tyIdx(node.tyIdx), boundsCheck(node.boundsCheck) {}

  ArrayNode(const MIRModule &mod, const ArrayNode &node) : ArrayNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  ArrayNode(ArrayNode &node) = delete;
  ArrayNode &operator=(const ArrayNode &node) = delete;
  ~ArrayNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;
  bool IsSameBase(ArrayNode*);

  uint8 NumOpnds(void) const {
    ASSERT(numOpnds == GetNopndSize(), "ArrayNode has wrong numOpnds field");
    return GetNopndSize();
  }

  ArrayNode *CloneTree(MapleAllocator &allocator) const {
    ArrayNode *nd = allocator.GetMemPool()->New<ArrayNode>(allocator, *this);
    for (size_t i = 0; i < GetNopndSize(); i++) {
      nd->GetNopnd().push_back(GetNopndAt(i)->CloneTree(allocator));
    }
    nd->boundsCheck = boundsCheck;
    nd->SetNumOpnds(GetNopndSize());
    return nd;
  }

  MIRType *GetArrayType(const TypeTable &tt) const;

  BaseNode *GetIndex(size_t i) {
    return Opnd(i + 1);
  }

  BaseNode *GetDim(const MIRModule &mod, const TypeTable &tt, int i) const;
  BaseNode *GetBase() {
    return Opnd(0);
  }

  TyIdx GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(TyIdx idx) {
    tyIdx = idx;
  }

  bool GetBoundsCheck() const {
    return boundsCheck;
  }

  void SetBoundsCheck(bool check) {
    boundsCheck = check;
  }

 private:
  TyIdx tyIdx;
  bool boundsCheck;
};

class AddrofNode : public BaseNode {
 public:
  explicit AddrofNode(Opcode o) : BaseNode(o), stIdx(), fieldID(0) {}

  AddrofNode(Opcode o, PrimType typ) : AddrofNode(o, typ, StIdx(), 0) {}

  AddrofNode(Opcode o, PrimType typ, StIdx sIdx, FieldID fid) : BaseNode(o, typ, 0), stIdx(sIdx), fieldID(fid) {}

  ~AddrofNode() = default;

  virtual void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;
  bool CheckNode(const MIRModule &mod) const;

  AddrofNode *CloneTree(MapleAllocator &allocator) const {
    return allocator.GetMemPool()->New<AddrofNode>(*this);
  }

  StIdx GetStIdx() const {
    return stIdx;
  }

  void SetStIdx(StIdx idx) {
    stIdx = idx;
  }

  void SetStFullIdx(uint32 idx) {
    stIdx.SetFullIdx(idx);
  }

  FieldID GetFieldID() const {
    return fieldID;
  }

  void SetFieldID(FieldID fieldIDVal) {
    fieldID = fieldIDVal;
  }

 private:
  StIdx stIdx;
  FieldID fieldID;
};

// DreadNode has the same member fields and member methods as AddrofNode
using DreadNode = AddrofNode;

class RegreadNode : public BaseNode {
 public:
  RegreadNode() : BaseNode(OP_regread), regIdx(0) {}

  explicit RegreadNode(PregIdx pIdx) : BaseNode(OP_regread), regIdx(pIdx) {}

  RegreadNode(PrimType primType, PregIdx pIdx) : RegreadNode(pIdx) {
    ptyp = primType;
  }

  ~RegreadNode() = default;

  virtual void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  RegreadNode *CloneTree(MapleAllocator &allocator) const {
    return allocator.GetMemPool()->New<RegreadNode>(*this);
  }

  PregIdx GetRegIdx() const {
    return regIdx;
  }
  void SetRegIdx(PregIdx reg) {
    regIdx = reg;
  }

 private:
  PregIdx regIdx;  // 32bit, negative if special register
};

class AddroffuncNode : public BaseNode {
 public:
  AddroffuncNode() : BaseNode(OP_addroffunc), puIdx(0) {}

  AddroffuncNode(PrimType typ, PUIdx pIdx) : BaseNode(OP_addroffunc, typ, 0), puIdx(pIdx) {}

  ~AddroffuncNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  AddroffuncNode *CloneTree(MapleAllocator &allocator) const {
    return allocator.GetMemPool()->New<AddroffuncNode>(*this);
  }

  PUIdx GetPUIdx() const {
    return puIdx;
  }

  void SetPUIdx(PUIdx puIdxValue) {
    puIdx = puIdxValue;
  }

 private:
  PUIdx puIdx;  // 32bit now
};

class AddroflabelNode : public BaseNode {
 public:
  AddroflabelNode() : BaseNode(OP_addroflabel), offset(0) {}

  explicit AddroflabelNode(uint32 ofst) : BaseNode(OP_addroflabel), offset(ofst) {}

  ~AddroflabelNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  AddroflabelNode *CloneTree(MapleAllocator &allocator) const {
    return allocator.GetMemPool()->New<AddroflabelNode>(*this);
  }

  uint32 GetOffset() const {
    return offset;
  }

  void SetOffset(uint32 offsetValue) {
    offset = offsetValue;
  }

 private:
  uint32 offset;
};

// to store source position information
class SrcPosition {
 public:
  SrcPosition() : lineNum(0), mplLineNum(0) {
    u.word0 = 0;
  }

  virtual ~SrcPosition() = default;

  uint32 RawData() const {
    return u.word0;
  }

  uint32 FileNum() const {
    return u.fileColumn.fileNum;
  }

  uint32 Column() const {
    return u.fileColumn.column;
  }

  uint32 LineNum() const {
    return lineNum;
  }

  uint32 MplLineNum() const {
    return mplLineNum;
  }

  void SetFileNum(uint16 n) {
    u.fileColumn.fileNum = n;
  }

  void SetColumn(uint16 n) {
    u.fileColumn.column = n;
  }

  void SetLineNum(uint32 n) {
    lineNum = n;
  }

  void SetRawData(uint32 n) {
    u.word0 = n;
  }

  void SetMplLineNum(uint32 n) {
    mplLineNum = n;
  }

  void CondSetLineNum(uint32 n) {
    lineNum = lineNum ? lineNum : n;
  }

  void CondSetFileNum(uint16 n) {
    uint16 i = u.fileColumn.fileNum;
    u.fileColumn.fileNum = i ? i : n;
  }

 private:
  union {
    struct {
      uint16 fileNum;
      uint16 column;
      bool stmtBegin;
      bool bbBegin;
      uint16 unused;
    } fileColumn;

    uint32 word0;
  } u;

  uint32 lineNum;     // line number of original src file, like foo.java
  uint32 mplLineNum;  // line number of mpl file
};

// for cleanuptry, catch, finally, retsub, endtry, membaracquire, membarrelease,
// membarstoreload, membarstorestore
class StmtNode : public BaseNode, public PtrListNodeBase<StmtNode> {
 public:
  static uint32 stmtIDNext;          // for assigning stmtID, initialized to 1; 0 is reserved
  static uint32 lastPrintedLineNum;  // used during printing ascii output

  explicit StmtNode(Opcode o) : BaseNode(o), PtrListNodeBase(), stmtID(stmtIDNext) {
    stmtIDNext++;
  }

  StmtNode(Opcode o, uint8 numOpr) : BaseNode(o, numOpr), PtrListNodeBase(), stmtID(stmtIDNext) {
    stmtIDNext++;
  }

  StmtNode(Opcode o, PrimType typ, uint8 numOpr) : BaseNode(o, typ, numOpr), PtrListNodeBase(), stmtID(stmtIDNext) {
    stmtIDNext++;
  }

  virtual ~StmtNode() = default;

  void DumpBase(const MIRModule &mod, int32 indent) const;
  void Dump(const MIRModule &mod, int32 indent) const;
  void Dump(const MIRModule &mod) const;
  void InsertAfterThis(StmtNode &pos);
  void InsertBeforeThis(StmtNode &pos);

  virtual StmtNode *CloneTree(MapleAllocator &allocator) const {
    StmtNode *s = allocator.GetMemPool()->New<StmtNode>(*this);
    s->SetStmtID(stmtIDNext++);
    return s;
  }

  virtual bool Verify() const {
    return true;
  }

  const SrcPosition &GetSrcPos() const {
    return srcPosition;
  }

  SrcPosition &GetSrcPos() {
    return srcPosition;
  }

  void SetSrcPos(SrcPosition pos) {
    srcPosition = pos;
  }

  uint32 GetStmtID() const {
    return stmtID;
  }

  void SetStmtID(uint32 id) {
    stmtID = id;
  }

  StmtNode *GetRealNext() const;

 protected:
  SrcPosition srcPosition;

 private:
  uint32 stmtID;  // a unique ID assigned to it
};

class IassignNode : public StmtNode {
 public:
  IassignNode() : IassignNode(TyIdx(0), 0, nullptr, nullptr) {}

  IassignNode(TyIdx tyIdx, FieldID fieldID, BaseNode *addrOpnd, BaseNode *rhsOpnd)
      : StmtNode(OP_iassign), tyIdx(tyIdx), fieldID(fieldID), addrExpr(addrOpnd), rhs(rhsOpnd) {
    SetNumOpnds(kOperandNumBinary);
  }

  ~IassignNode() = default;

  TyIdx GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(TyIdx idx) {
    tyIdx = idx;
  }

  FieldID GetFieldID() const {
    return fieldID;
  }

  void SetFieldID(FieldID fid) {
    fieldID = fid;
  }

  BaseNode *Opnd(size_t i) const {
    if (i == 0) {
      return addrExpr;
    }
    return rhs;
  }

  uint8 NumOpnds() const {
    return kOperandNumBinary;
  }

  void SetOpnd(BaseNode *node, size_t i) {
    if (i == 0) {
      addrExpr = node;
    } else {
      rhs = node;
    }
  }

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  IassignNode *CloneTree(MapleAllocator &allocator) const {
    IassignNode *bn = allocator.GetMemPool()->New<IassignNode>(*this);
    bn->SetStmtID(stmtIDNext++);
    bn->SetOpnd(addrExpr->CloneTree(allocator), 0);
    bn->SetRHS(rhs->CloneTree(allocator));
    return bn;
  }

  // the base of an address expr is either a leaf or an iread
  BaseNode &GetAddrExprBase() const {
    BaseNode *base = addrExpr;
    while (base->NumOpnds() != 0 && base->GetOpCode() != OP_iread) {
      base = base->Opnd(0);
    }
    return *base;
  }

  void SetAddrExpr(BaseNode *exp) {
    addrExpr = exp;
  }

  BaseNode *GetRHS() const {
    return rhs;
  }

  void SetRHS(BaseNode *node) {
    rhs = node;
  }

 private:
  TyIdx tyIdx;
  FieldID fieldID;
  BaseNode *addrExpr;
  BaseNode *rhs;
};

// goto and gosub
class GotoNode : public StmtNode {
 public:
  explicit GotoNode(Opcode o) : StmtNode(o), offset(0) {}

  GotoNode(Opcode o, uint32 ofst) : StmtNode(o), offset(ofst) {}

  ~GotoNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  GotoNode *CloneTree(MapleAllocator &allocator) const {
    GotoNode *g = allocator.GetMemPool()->New<GotoNode>(*this);
    g->SetStmtID(stmtIDNext++);
    return g;
  }

  uint32 GetOffset() const {
    return offset;
  }

  void SetOffset(uint32 o) {
    offset = o;
  }

 private:
  uint32 offset;
};

// try
class JsTryNode : public StmtNode {
 public:
  JsTryNode() : StmtNode(OP_jstry), catchOffset(0), finallyOffset(0) {}

  JsTryNode(uint16 catchofst, uint16 finallyofset)
      : StmtNode(OP_jstry), catchOffset(catchofst), finallyOffset(finallyofset) {}

  ~JsTryNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  JsTryNode *CloneTree(MapleAllocator &allocator) const {
    JsTryNode *t = allocator.GetMemPool()->New<JsTryNode>(*this);
    t->SetStmtID(stmtIDNext++);
    return t;
  }

  uint16 GetCatchOffset() const {
    return catchOffset;
  }

  void SetCatchOffset(uint32 offset) {
    catchOffset = offset;
  }

  uint16 GetFinallyOffset() const {
    return finallyOffset;
  }

  void SetFinallyOffset(uint32 offset) {
    finallyOffset = offset;
  }

 private:
  uint16 catchOffset;
  uint16 finallyOffset;
};

// try
class TryNode : public StmtNode {
 public:
  explicit TryNode(MapleAllocator &allocator) : StmtNode(OP_try), offsets(allocator.Adapter()) {}

  explicit TryNode(const MapleVector<LabelIdx> &offsets) : StmtNode(OP_try), offsets(offsets) {}

  explicit TryNode(const MIRModule &mod) : TryNode(mod.GetCurFuncCodeMPAllocator()) {}

  TryNode(TryNode &node) = delete;
  TryNode &operator=(const TryNode &node) = delete;
  ~TryNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  void Dump(const MIRModule &mod) const;

  LabelIdx GetOffset(size_t i) const {
    ASSERT(i < offsets.size(), "array index out of range");
    return offsets.at(i);
  }

  void SetOffset(LabelIdx offsetValue, size_t i) {
    ASSERT(i < offsets.size(), "array index out of range");
    offsets[i] = offsetValue;
  }

  void AddOffset(LabelIdx offsetValue) {
    offsets.push_back(offsetValue);
  }

  void ResizeOffsets(size_t offsetSize) {
    offsets.resize(offsetSize);
  }

  void SetOffsets(const MapleVector<LabelIdx> &offsetsValue) {
    offsets = offsetsValue;
  }

  size_t GetOffsetsCount() const {
    return offsets.size();
  }

  MapleVector<LabelIdx>::iterator GetOffsetsBegin() {
    return offsets.begin();
  }

  MapleVector<LabelIdx>::iterator GetOffsetsEnd() {
    return offsets.end();
  }

  void OffsetsInsert(MapleVector<LabelIdx>::iterator a, MapleVector<LabelIdx>::iterator b,
                     MapleVector<LabelIdx>::iterator c) {
    offsets.insert(a, b, c);
  }

  TryNode *CloneTree(MapleAllocator &allocator) const {
    TryNode *nd = allocator.GetMemPool()->New<TryNode>(allocator);
    nd->SetStmtID(stmtIDNext++);
    for (size_t i = 0; i < offsets.size(); i++) {
      nd->AddOffset(offsets[i]);
    }
    return nd;
  }

 private:
  MapleVector<LabelIdx> offsets;
};

// catch
class CatchNode : public StmtNode {
 public:
  explicit CatchNode(MapleAllocator &allocator) : StmtNode(OP_catch), exceptionTyIdxVec(allocator.Adapter()) {}

  explicit CatchNode(const MapleVector<TyIdx> &tyIdxVec)
      : StmtNode(OP_catch), exceptionTyIdxVec(tyIdxVec) {}

  explicit CatchNode(const MIRModule &mod) : CatchNode(mod.GetCurFuncCodeMPAllocator()) {}

  CatchNode(CatchNode &node) = delete;
  CatchNode &operator=(const CatchNode &node) = delete;
  ~CatchNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  void Dump(const MIRModule &mod) const;

  TyIdx GetExceptionTyIdxVecElement(size_t i) const {
    CHECK_FATAL(i < exceptionTyIdxVec.size(), "array index out of range");
    return exceptionTyIdxVec[i];
  }

  const MapleVector<TyIdx> &GetExceptionTyIdxVec() const {
    return exceptionTyIdxVec;
  }

  size_t Size() const {
    return exceptionTyIdxVec.size();
  }

  void SetExceptionTyIdxVecElement(TyIdx idx, size_t i) {
    CHECK_FATAL(i < exceptionTyIdxVec.size(), "array index out of range");
    exceptionTyIdxVec[i] = idx;
  }

  void SetExceptionTyIdxVec(MapleVector<TyIdx> vec) {
    exceptionTyIdxVec = vec;
  }

  void PushBack(TyIdx idx) {
    exceptionTyIdxVec.push_back(idx);
  }

  CatchNode *CloneTree(MapleAllocator &allocator) const {
    CatchNode *j = allocator.GetMemPool()->New<CatchNode>(allocator);
    j->SetStmtID(stmtIDNext++);
    for (uint32 i = 0; i < Size(); i++) {
      j->PushBack(GetExceptionTyIdxVecElement(i));
    }
    return j;
  }

 private:
  // TyIdx exception_tyidx;
  MapleVector<TyIdx> exceptionTyIdxVec;
};

using CasePair = std::pair<int32, LabelIdx>;
using CaseVector = MapleVector<CasePair>;
class SwitchNode : public StmtNode {
 public:
  explicit SwitchNode(MapleAllocator &allocator)
      : StmtNode(OP_switch, 1), switchOpnd(nullptr), defaultLabel(0), switchTable(allocator.Adapter()) {}

  explicit SwitchNode(const MIRModule &mod) : SwitchNode(mod.GetCurFuncCodeMPAllocator()) {}

  SwitchNode(MapleAllocator &allocator, LabelIdx label) : SwitchNode(allocator, label, nullptr) {}

  SwitchNode(MapleAllocator &allocator, LabelIdx label, BaseNode *opnd)
      : StmtNode(OP_switch, 1), switchOpnd(opnd), defaultLabel(label), switchTable(allocator.Adapter()) {}

  SwitchNode(const MIRModule &mod, LabelIdx label) : SwitchNode(mod.GetCurFuncCodeMPAllocator(), label) {}

  SwitchNode(MapleAllocator &allocator, const SwitchNode &node)
      : StmtNode(node.GetOpCode(), node.GetPrimType(), node.numOpnds),
        switchOpnd(nullptr),
        defaultLabel(node.GetDefaultLabel()),
        switchTable(allocator.Adapter()) {}

  SwitchNode(const MIRModule &mod, const SwitchNode &node) : SwitchNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  SwitchNode(SwitchNode &node) = delete;
  SwitchNode &operator=(const SwitchNode &node) = delete;
  ~SwitchNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  SwitchNode *CloneTree(MapleAllocator &allocator) const {
    SwitchNode *nd = allocator.GetMemPool()->New<SwitchNode>(allocator, *this);
    nd->SetSwitchOpnd(switchOpnd->CloneTree(allocator));
    for (size_t i = 0; i < switchTable.size(); i++) {
      nd->InsertCasePair(switchTable[i]);
    }
    return nd;
  }

  BaseNode *Opnd(size_t i) const {
    ASSERT(i == 0, "it is not same as original");
    return switchOpnd;
  }

  void SetOpnd(BaseNode *node, size_t i = 0) {
    ASSERT(i == 0, "it is not same as original");
    switchOpnd = node;
  }

  BaseNode *GetSwitchOpnd() const {
    return switchOpnd;
  }

  void SetSwitchOpnd(BaseNode *node) {
    switchOpnd = node;
  }

  LabelIdx GetDefaultLabel() const {
    return defaultLabel;
  }

  void SetDefaultLabel(LabelIdx idx) {
    defaultLabel = idx;
  }

  const CaseVector &GetSwitchTable() const {
    return switchTable;
  }

  CasePair GetCasePair(size_t idx) const {
    ASSERT(idx < switchTable.size(), "out of range in SwitchNode::GetCasePair");
    return switchTable.at(idx);
  }

  void SetSwitchTable(CaseVector vec) {
    switchTable = vec;
  }

  void InsertCasePair(CasePair pair) {
    switchTable.push_back(pair);
  }

  void UpdateCaseLabelAt(uint32_t i, LabelIdx idx) {
    switchTable[i] = std::make_pair(switchTable[i].first, idx);
  }

  void SortCasePair(bool func(const CasePair&, const CasePair&)) {
    std::sort(switchTable.begin(), switchTable.end(), func);
  }

 private:
  BaseNode *switchOpnd;
  LabelIdx defaultLabel;
  CaseVector switchTable;
};

using MCasePair = std::pair<BaseNode*, LabelIdx>;
using MCaseVector = MapleVector<MCasePair>;
class MultiwayNode : public StmtNode {
 public:
  explicit MultiwayNode(MapleAllocator &allocator)
      : StmtNode(OP_multiway, 1), multiWayOpnd(nullptr), defaultLabel(0), multiWayTable(allocator.Adapter()) {}

  explicit MultiwayNode(const MIRModule &mod) : MultiwayNode(mod.GetCurFuncCodeMPAllocator()) {}

  MultiwayNode(MapleAllocator &allocator, LabelIdx label)
      : StmtNode(OP_multiway, 1), multiWayOpnd(nullptr), defaultLabel(label), multiWayTable(allocator.Adapter()) {}

  MultiwayNode(const MIRModule &mod, LabelIdx label) : MultiwayNode(mod.GetCurFuncCodeMPAllocator(), label) {}

  MultiwayNode(MapleAllocator &allocator, const MultiwayNode &node)
      : StmtNode(node.GetOpCode(), node.GetPrimType(), node.numOpnds),
        multiWayOpnd(nullptr),
        defaultLabel(node.defaultLabel),
        multiWayTable(allocator.Adapter()) {}

  MultiwayNode(const MIRModule &mod, const MultiwayNode &node)
      : MultiwayNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  MultiwayNode(MultiwayNode &node) = delete;
  MultiwayNode &operator=(const MultiwayNode &node) = delete;
  ~MultiwayNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  MultiwayNode *CloneTree(MapleAllocator &allocator) const {
    MultiwayNode *nd = allocator.GetMemPool()->New<MultiwayNode>(allocator, *this);
    nd->multiWayOpnd = static_cast<BaseNode*>(multiWayOpnd->CloneTree(allocator));
    for (size_t i = 0; i < multiWayTable.size(); i++) {
      BaseNode *node = multiWayTable[i].first->CloneTree(allocator);
      MCasePair pair(static_cast<BaseNode*>(node), multiWayTable[i].second);
      nd->multiWayTable.push_back(pair);
    }
    return nd;
  }

  BaseNode *Opnd(size_t i) const {
    return *(&multiWayOpnd + static_cast<uint32>(i));
  }

  const BaseNode *GetMultiWayOpnd() const {
    return multiWayOpnd;
  }

  void SetMultiWayOpnd(BaseNode *multiwayopndPara) {
    multiWayOpnd = multiwayopndPara;
  }

  void SetDefaultlabel(LabelIdx defaultlabelPara) {
    defaultLabel = defaultlabelPara;
  }

  void AppendElemToMultiWayTable(const MCasePair &mCasrPair) {
    multiWayTable.push_back(mCasrPair);
  }

  const MCaseVector &GetMultiWayTable() const {
    return multiWayTable;
  }

 private:
  BaseNode *multiWayOpnd;
  LabelIdx defaultLabel;
  MCaseVector multiWayTable;
};

// eval, throw, free, decref, incref, decrefreset, assertnonnull
class UnaryStmtNode : public StmtNode {
 public:
  explicit UnaryStmtNode(Opcode o) : StmtNode(o, 1), uOpnd(nullptr) {}

  UnaryStmtNode(Opcode o, PrimType typ) : StmtNode(o, typ, 1), uOpnd(nullptr) {}

  UnaryStmtNode(Opcode o, PrimType typ, BaseNode *opnd) : StmtNode(o, typ, 1), uOpnd(opnd) {}

  virtual ~UnaryStmtNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  void Dump(const MIRModule &mod) const;
  void DumpOpnd(const MIRModule &mod, int32 indent) const;

  bool Verify() const {
    return uOpnd->Verify();
  }

  UnaryStmtNode *CloneTree(MapleAllocator &allocator) const {
    UnaryStmtNode *nd = allocator.GetMemPool()->New<UnaryStmtNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetOpnd(uOpnd->CloneTree(allocator));
    return nd;
  }

  bool IsLeaf(void) const {
    return false;
  }

  virtual BaseNode *GetRHS() const {
    return Opnd(0);
  }

  virtual void SetRHS(BaseNode *rhs) {
    this->SetOpnd(rhs, 0);
  }

  BaseNode *Opnd(size_t i = 0) const {
    ASSERT(i == 0, "Unary operand");
    return uOpnd;
  }

  void SetOpnd(BaseNode *node, size_t i = 0) {
    ASSERT(i == 0, "Unary operand");
    uOpnd = node;
  }

 private:
  BaseNode *uOpnd;
};

// dassign, maydassign
class DassignNode : public UnaryStmtNode {
 public:
  DassignNode() : UnaryStmtNode(OP_dassign), stIdx(), fieldID(0) {}

  explicit DassignNode(PrimType typ) : UnaryStmtNode(OP_dassign, typ), stIdx(), fieldID(0) {}

  DassignNode(PrimType typ, BaseNode *opnd) : UnaryStmtNode(OP_dassign, typ, opnd), stIdx(), fieldID(0) {}

  DassignNode(PrimType typ, BaseNode *opnd, StIdx idx, FieldID fieldID)
      : UnaryStmtNode(OP_dassign, typ, opnd), stIdx(idx), fieldID(fieldID) {}

  DassignNode(BaseNode *opnd, StIdx idx, FieldID fieldID) : DassignNode(kPtyInvalid, opnd, idx, fieldID) {}

  ~DassignNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  DassignNode *CloneTree(MapleAllocator &allocator) const {
    DassignNode *nd = allocator.GetMemPool()->New<DassignNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    return nd;
  }

  uint8 NumOpnds(void) const {
    return 1;
  }

  bool IsIdentityDassign() const {
    BaseNode *rhs = GetRHS();
    if (rhs->GetOpCode() != OP_dread) {
      return false;
    }
    AddrofNode *dread = static_cast<AddrofNode*>(rhs);
    return (stIdx == dread->GetStIdx());
  }

  BaseNode *GetRHS() const {
    return UnaryStmtNode::GetRHS();
  }

  void SetRHS(BaseNode *rhs) {
    UnaryStmtNode::SetOpnd(rhs, 0);
  }

  StIdx GetStIdx() const {
    return stIdx;
  }
  void SetStIdx(StIdx s) {
    stIdx = s;
  }

  const FieldID &GetFieldID() const {
    return fieldID;
  }

  void SetFieldID(FieldID f) {
    fieldID = f;
  }

 private:
  StIdx stIdx;
  FieldID fieldID;
};

class RegassignNode : public UnaryStmtNode {
 public:
  RegassignNode() : UnaryStmtNode(OP_regassign), regIdx(0) {}

  explicit RegassignNode(const RegassignNode &node) : UnaryStmtNode(node), regIdx(node.regIdx) {}

  RegassignNode(PrimType primType, PregIdx idx, BaseNode *opnd)
      : UnaryStmtNode(OP_regassign, primType, opnd), regIdx(idx) {}

  ~RegassignNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  RegassignNode *CloneTree(MapleAllocator &allocator) const {
    RegassignNode *nd = allocator.GetMemPool()->New<RegassignNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    return nd;
  }

  BaseNode *GetRHS() const {
    return UnaryStmtNode::GetRHS();
  }

  void SetRHS(BaseNode *rhs) {
    UnaryStmtNode::SetOpnd(rhs, 0);
  }

  PregIdx GetRegIdx() const {
    return regIdx;
  }
  void SetRegIdx(PregIdx idx) {
    regIdx = idx;
  }

 private:
  PregIdx regIdx;  // 32bit, negative if special register
};

// brtrue and brfalse
class CondGotoNode : public UnaryStmtNode {
 public:
  explicit CondGotoNode(Opcode o) : CondGotoNode(o, 0, nullptr) {}

  CondGotoNode(Opcode o, uint32 offset, BaseNode *opnd) : UnaryStmtNode(o, kPtyInvalid, opnd), offset(offset) {
    SetNumOpnds(kOperandNumUnary);
  }

  ~CondGotoNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  uint32 GetOffset() const {
    return offset;
  }

  void SetOffset(uint32 offsetValue) {
    offset = offsetValue;
  }

  CondGotoNode *CloneTree(MapleAllocator &allocator) const {
    CondGotoNode *nd = allocator.GetMemPool()->New<CondGotoNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    return nd;
  }

 private:
  uint32 offset;
};

using SmallCasePair = std::pair<uint16, uint16>;
using SmallCaseVector = MapleVector<SmallCasePair>;
class RangeGotoNode : public UnaryStmtNode {
 public:
  explicit RangeGotoNode(MapleAllocator &allocator)
      : UnaryStmtNode(OP_rangegoto), tagOffset(0), rangegotoTable(allocator.Adapter()) {}

  explicit RangeGotoNode(const MIRModule &mod) : RangeGotoNode(mod.GetCurFuncCodeMPAllocator()) {}

  RangeGotoNode(MapleAllocator &allocator, const RangeGotoNode &node)
      : UnaryStmtNode(node.GetOpCode(), node.GetPrimType()),
        tagOffset(node.tagOffset),
        rangegotoTable(allocator.Adapter()) {}

  RangeGotoNode(const MIRModule &mod, const RangeGotoNode &node)
      : RangeGotoNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  RangeGotoNode(RangeGotoNode &node) = delete;
  RangeGotoNode &operator=(const RangeGotoNode &node) = delete;
  ~RangeGotoNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;
  RangeGotoNode *CloneTree(MapleAllocator &allocator) const {
    RangeGotoNode *nd = allocator.GetMemPool()->New<RangeGotoNode>(allocator, *this);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    for (size_t i = 0; i < rangegotoTable.size(); i++) {
      nd->rangegotoTable.push_back(rangegotoTable[i]);
    }
    return nd;
  }

  const SmallCaseVector &GetRangeGotoTable() const {
    return rangegotoTable;
  }

  void SetRangeGotoTable(SmallCaseVector rt) {
    rangegotoTable = rt;
  }

  void AddRangeGoto(uint32 tag, LabelIdx idx) {
    rangegotoTable.push_back(SmallCasePair(tag, idx));
  }

  int32 GetTagOffset() const {
    return tagOffset;
  }

  void SetTagOffset(int32 offset) {
    tagOffset = offset;
  }

 private:
  int32 tagOffset;
  // add each tag to tagOffset field to get the actual tag values
  SmallCaseVector rangegotoTable;
};

class BlockNode : public StmtNode {
 public:
  using StmtNodes = PtrListRef<StmtNode>;

  BlockNode() : StmtNode(OP_block) {}

  ~BlockNode() {
    stmtNodeList.clear();
  }

  void AddStatement(StmtNode *stmt);
  void AppendStatementsFromBlock(BlockNode &blk);
  void InsertFirst(StmtNode *stmt);  // Insert stmt as the first
  void InsertLast(StmtNode *stmt);   // Insert stmt as the last
  void ReplaceStmtWithBlock(StmtNode &stmtNode, BlockNode &blk);
  void ReplaceStmt1WithStmt2(StmtNode *stmtNode1, StmtNode *stmtNode2);
  void RemoveStmt(StmtNode *stmtNode2);
  void InsertBefore(StmtNode *stmtNode1, StmtNode *stmtNode2);  // Insert ss2 before ss1 in current block.
  void InsertAfter(StmtNode *stmtNode1, StmtNode *stmtNode2);   // Insert ss2 after ss1 in current block.
  // insert all the stmts in inblock to the current block after stmt1
  void InsertBlockAfter(BlockNode &inblock, StmtNode *stmt1);
  void Dump(const MIRModule &mod, int32 indent, const MIRSymbolTable *theSymTab, MIRPregTable *thePregTab,
            bool withInfo, bool isFuncbody) const;
  bool Verify() const;

  void Dump(const MIRModule &mod, int32 indent) const {
    Dump(mod, indent, nullptr, nullptr, false, false);
  }

  BlockNode *CloneTree(MapleAllocator &allocator) const {
    BlockNode *blk = allocator.GetMemPool()->New<BlockNode>();
    blk->SetStmtID(stmtIDNext++);
    for (auto &stmt : stmtNodeList) {
      StmtNode *newStmt = static_cast<StmtNode*>(stmt.CloneTree(allocator));
      ASSERT(newStmt != nullptr, "null ptr check");
      newStmt->SetPrev(nullptr);
      newStmt->SetNext(nullptr);
      blk->AddStatement(newStmt);
    }
    return blk;
  }

  BlockNode *CloneTreeWithSrcPosition(const MIRModule &mod) {
    MapleAllocator &allocator = mod.GetCurFuncCodeMPAllocator();
    BlockNode *blk = allocator.GetMemPool()->New<BlockNode>();
    blk->SetStmtID(stmtIDNext++);
    for (auto &stmt : stmtNodeList) {
      StmtNode *newStmt = static_cast<StmtNode*>(stmt.CloneTree(allocator));
      ASSERT(newStmt != nullptr, "null ptr check");
      newStmt->SetSrcPos(stmt.GetSrcPos());
      newStmt->SetPrev(nullptr);
      newStmt->SetNext(nullptr);
      blk->AddStatement(newStmt);
    }
    return blk;
  }

  bool IsEmpty() const {
    return stmtNodeList.empty();
  }

  void ResetBlock() {
    stmtNodeList.clear();
  }

  StmtNode *GetFirst() {
    return &(stmtNodeList.front());
  }

  const StmtNode *GetFirst() const {
    return &(stmtNodeList.front());
  }

  void SetFirst(StmtNode *node) {
    stmtNodeList.update_front(node);
  }

  StmtNode *GetLast() {
    return &(stmtNodeList.back());
  }

  const StmtNode *GetLast() const {
    return &(stmtNodeList.back());
  }

  void SetLast(StmtNode *node) {
    stmtNodeList.update_back(node);
  }

  StmtNodes &GetStmtNodes() {
    return stmtNodeList;
  }

  const StmtNodes &GetStmtNodes() const {
    return stmtNodeList;
  }

 private:
  StmtNodes stmtNodeList;
};

class IfStmtNode : public UnaryStmtNode {
 public:
  IfStmtNode() : UnaryStmtNode(OP_if), thenPart(nullptr), elsePart(nullptr) {
    numOpnds = kOperandNumBinary;
  }

  ~IfStmtNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  IfStmtNode *CloneTree(MapleAllocator &allocator) const {
    IfStmtNode *nd = allocator.GetMemPool()->New<IfStmtNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    nd->thenPart = thenPart->CloneTree(allocator);
    if (elsePart != nullptr) {
      nd->elsePart = elsePart->CloneTree(allocator);
    }
    return nd;
  }

  BaseNode *Opnd(size_t i = 0) const {
    if (i == 0) {
      return UnaryStmtNode::Opnd();
    } else if (i == 1) {
      return thenPart;
    } else if (i == 2) {
      ASSERT(elsePart != nullptr, "IfStmtNode has wrong numOpnds field, the elsePart is nullptr");
      ASSERT(numOpnds == kOperandNumTernary, "IfStmtNode has wrong numOpnds field, the elsePart is nullptr");
      return elsePart;
    }
    ASSERT(false, "IfStmtNode has wrong numOpnds field: %u", NumOpnds());
    return nullptr;
  }

  BlockNode *GetThenPart() const {
    return thenPart;
  }

  void SetThenPart(BlockNode *node) {
    thenPart = node;
  }

  BlockNode *GetElsePart() const {
    return elsePart;
  }

  void SetElsePart(BlockNode *node) {
    elsePart = node;
  }

  uint8 NumOpnds(void) const {
    return numOpnds;
  }

 private:
  BlockNode *thenPart;
  BlockNode *elsePart;
};

// for both while and dowhile
class WhileStmtNode : public UnaryStmtNode {
 public:
  explicit WhileStmtNode(Opcode o) : UnaryStmtNode(o), body(nullptr) {
    SetNumOpnds(kOperandNumBinary);
  }

  ~WhileStmtNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  WhileStmtNode *CloneTree(MapleAllocator &allocator) const {
    WhileStmtNode *nd = allocator.GetMemPool()->New<WhileStmtNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    nd->body = body->CloneTree(allocator);
    return nd;
  }

  void SetBody(BlockNode *node) {
    body = node;
  }

  BlockNode *GetBody() const {
    return body;
  }

 private:
  BlockNode *body;
};

class DoloopNode : public StmtNode {
 public:
  DoloopNode() : DoloopNode(StIdx(), false, nullptr, nullptr, nullptr, nullptr) {}

  DoloopNode(StIdx doVarStIdx, bool isPReg, BaseNode *startExp, BaseNode *contExp, BaseNode *incrExp, BlockNode *doBody)
      : StmtNode(OP_doloop, kOperandNumDoloop),
        doVarStIdx(doVarStIdx),
        isPreg(isPReg),
        startExpr(startExp),
        condExpr(contExp),
        incrExpr(incrExp),
        doBody(doBody) {}

  ~DoloopNode() = default;

  void DumpDoVar(const MIRModule &mod) const;
  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  DoloopNode *CloneTree(MapleAllocator &allocator) const {
    DoloopNode *nd = allocator.GetMemPool()->New<DoloopNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetStartExpr(startExpr->CloneTree(allocator));
    nd->SetContExpr(GetCondExpr()->CloneTree(allocator));
    nd->SetIncrExpr(GetIncrExpr()->CloneTree(allocator));
    nd->SetDoBody(GetDoBody()->CloneTree(allocator));
    return nd;
  }

  void SetDoVarStIdx(StIdx idx) {
    doVarStIdx = idx;
  }

  const StIdx &GetDoVarStIdx() const {
    return doVarStIdx;
  }

  void SetDoVarStFullIdx(uint32 idx) {
    doVarStIdx.SetFullIdx(idx);
  }

  void SetIsPreg(bool isPregVal) {
    isPreg = isPregVal;
  }

  bool IsPreg() const {
    return isPreg;
  }

  void SetStartExpr(BaseNode *node) {
    startExpr = node;
  }

  BaseNode *GetStartExpr() const {
    return startExpr;
  }

  void SetContExpr(BaseNode *node) {
    condExpr = node;
  }

  BaseNode *GetCondExpr() const {
    return condExpr;
  }

  void SetIncrExpr(BaseNode *node) {
    incrExpr = node;
  }

  BaseNode *GetIncrExpr() const {
    return incrExpr;
  }

  void SetDoBody(BlockNode *node) {
    doBody = node;
  }

  BlockNode *GetDoBody() const {
    return doBody;
  }

  BaseNode *Opnd(size_t i) const {
    if (i == 0) {
      return startExpr;
    }
    if (i == 1) {
      return condExpr;
    }
    if (i == 2) {
      return incrExpr;
    }
    return *(&doBody + i - 3);
  }

  uint8 NumOpnds() const {
    return kOperandNumDoloop;
  }

  void SetOpnd(BaseNode *node, size_t i) {
    if (i == 0) {
      startExpr = node;
    }
    if (i == 1) {
      SetContExpr(node);
    }
    if (i == 2) {
      incrExpr = node;
    } else {
      *(&doBody + i - 3) = static_cast<BlockNode*>(node);
    }
  }

 private:
  static constexpr int kOperandNumDoloop = 4;
  StIdx doVarStIdx;  // must be local; cast to PregIdx for preg
  bool isPreg;
  BaseNode *startExpr;
  BaseNode *condExpr;
  BaseNode *incrExpr;
  BlockNode *doBody;
};

class ForeachelemNode : public StmtNode {
 public:
  ForeachelemNode() : StmtNode(OP_foreachelem), loopBody(nullptr) {
    SetNumOpnds(kOperandNumUnary);
  }

  ~ForeachelemNode() = default;

  const StIdx &GetElemStIdx() const {
    return elemStIdx;
  }

  void SetElemStIdx(StIdx elemStIdxValue) {
    elemStIdx = elemStIdxValue;
  }

  const StIdx &GetArrayStIdx() const {
    return arrayStIdx;
  }

  void SetArrayStIdx(StIdx arrayStIdxValue) {
    arrayStIdx = arrayStIdxValue;
  }

  BlockNode *GetLoopBody() const {
    return loopBody;
  }

  void SetLoopBody(BlockNode *loopBodyValue) {
    loopBody = loopBodyValue;
  }

  BaseNode *Opnd(size_t i = 0) const {
    return loopBody;
  }

  uint8 NumOpnds(void) const {
    return numOpnds;
  }

  void Dump(const MIRModule &mod, int32 indent) const;

  ForeachelemNode *CloneTree(MapleAllocator &allocator) const {
    ForeachelemNode *nd = allocator.GetMemPool()->New<ForeachelemNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetLoopBody(loopBody->CloneTree(allocator));
    return nd;
  }

 private:
  StIdx elemStIdx;   // must be local symbol
  StIdx arrayStIdx;  // symbol table entry of the array/collection variable
  BlockNode *loopBody;
};

// used by assertge, assertlt
class BinaryStmtNode : public StmtNode, public BinaryOpnds {
 public:
  explicit BinaryStmtNode(Opcode o) : StmtNode(o, kOperandNumBinary) {}

  ~BinaryStmtNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  virtual bool Verify() const;
  BinaryStmtNode *CloneTree(MapleAllocator &allocator) const {
    BinaryStmtNode *nd = allocator.GetMemPool()->New<BinaryStmtNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetBOpnd(GetBOpnd(0)->CloneTree(allocator), 0);
    nd->SetBOpnd(GetBOpnd(1)->CloneTree(allocator), 1);
    return nd;
  }

  BaseNode *Opnd(size_t i) const {
    ASSERT(i < kOperandNumBinary, "Invalid operand idx in BinaryStmtNode");
    ASSERT(i >= 0, "Invalid operand idx in BinaryStmtNode");
    return GetBOpnd(i);
  }

  uint8 NumOpnds() const {
    return kOperandNumBinary;
  }

  void SetOpnd(BaseNode *node, size_t i) {
    SetBOpnd(node, i);
  }

  bool IsLeaf() const {
    return false;
  }
};

class IassignoffNode : public BinaryStmtNode {
 public:
  IassignoffNode() : BinaryStmtNode(OP_iassignoff), offset(0) {}

  explicit IassignoffNode(int32 ofst) : BinaryStmtNode(OP_iassignoff), offset(ofst) {}

  IassignoffNode(PrimType primType, int32 offset, BaseNode *addrOpnd, BaseNode *srcOpnd) : IassignoffNode(offset) {
    SetPrimType(primType);
    SetBOpnd(addrOpnd, 0);
    SetBOpnd(srcOpnd, 1);
  }

  ~IassignoffNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  IassignoffNode *CloneTree(MapleAllocator &allocator) const {
    IassignoffNode *nd = allocator.GetMemPool()->New<IassignoffNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetBOpnd(GetBOpnd(0)->CloneTree(allocator), 0);
    nd->SetBOpnd(GetBOpnd(1)->CloneTree(allocator), 1);
    return nd;
  }

  int32 GetOffset() const {
    return offset;
  }

  void SetOffset(int32 newOffset) {
    offset = newOffset;
  }

 private:
  int32 offset;
};

class IassignFPoffNode : public UnaryStmtNode {
 public:
  IassignFPoffNode() : UnaryStmtNode(OP_iassignfpoff), offset(0) {}

  explicit IassignFPoffNode(int32 ofst) : UnaryStmtNode(OP_iassignfpoff), offset(ofst) {}

  IassignFPoffNode(PrimType primType, int32 offset, BaseNode *src) : IassignFPoffNode(offset) {
    SetPrimType(primType);
    SetOpnd(src);
  }

  ~IassignFPoffNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  bool Verify() const;

  IassignFPoffNode *CloneTree(MapleAllocator &allocator) const {
    IassignFPoffNode *nd = allocator.GetMemPool()->New<IassignFPoffNode>(*this);
    nd->SetStmtID(stmtIDNext++);
    nd->SetOpnd(Opnd()->CloneTree(allocator));
    return nd;
  }

  void SetOffset(int32 ofst) {
    offset = ofst;
  }

  int32 GetOffset() const {
    return offset;
  }

 private:
  int32 offset;
};

// used by return, syncenter, syncexit
class NaryStmtNode : public StmtNode, public NaryOpnds {
 public:
  NaryStmtNode(MapleAllocator &allocator, Opcode o) : StmtNode(o), NaryOpnds(allocator) {}

  NaryStmtNode(const MIRModule &mod, Opcode o) : NaryStmtNode(mod.GetCurFuncCodeMPAllocator(), o) {}

  NaryStmtNode(MapleAllocator &allocator, const NaryStmtNode &node)
      : StmtNode(node.GetOpCode(), node.GetPrimType(), node.numOpnds), NaryOpnds(allocator) {}

  NaryStmtNode(const MIRModule &mod, const NaryStmtNode &node)
      : NaryStmtNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  NaryStmtNode(NaryStmtNode &node) = delete;
  NaryStmtNode &operator=(const NaryStmtNode &node) = delete;
  ~NaryStmtNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;
  virtual bool Verify() const;

  NaryStmtNode *CloneTree(MapleAllocator &allocator) const {
    NaryStmtNode *nd = allocator.GetMemPool()->New<NaryStmtNode>(allocator, *this);
    for (size_t i = 0; i < GetNopndSize(); i++) {
      nd->GetNopnd().push_back(GetNopndAt(i)->CloneTree(allocator));
    }
    nd->SetNumOpnds(GetNopndSize());
    return nd;
  }

  BaseNode *Opnd(size_t i) const {
    return GetNopndAt(i);
  }

  void SetOpnd(BaseNode *node, size_t i) {
    ASSERT(i < GetNopnd().size(), "array index out of range");
    SetNOpndAt(i, node);
  }

  uint8 NumOpnds() const {
    ASSERT(numOpnds == GetNopndSize(), "NaryStmtNode has wrong numOpnds field");
    return GetNopndSize();
  }

  void SetOpnds(const MapleVector<BaseNode*> &arguments) {
    SetNOpnd(arguments);
    SetNumOpnds(arguments.size());
  }

  void PushOpnd(BaseNode *node) {
    if (node != nullptr) {
      GetNopnd().push_back(node);
    }
    SetNumOpnds(GetNopndSize());
  }
};

class ReturnValuePart {
 public:
  explicit ReturnValuePart(MapleAllocator &allocator) : returnValues(allocator.Adapter()) {}

  virtual ~ReturnValuePart() = default;

 private:
  CallReturnVector returnValues;
};

// used by call, virtualcall, virtualicall, superclasscall, interfacecall,
// interfaceicall, customcall
// callassigned, virtualcallassigned, virtualicallassigned,
// superclasscallassigned, interfacecallassigned, interfaceicallassigned,
// customcallassigned
class CallNode : public NaryStmtNode {
 public:
  CallNode(MapleAllocator &allocator, Opcode o)
      : NaryStmtNode(allocator, o), puIdx(0), tyIdx(0), returnValues(allocator.Adapter()) {}

  CallNode(MapleAllocator &allocator, Opcode o, PUIdx idx) : CallNode(allocator, o, idx, TyIdx()) {}

  CallNode(MapleAllocator &allocator, Opcode o, PUIdx idx, TyIdx tdx)
      : NaryStmtNode(allocator, o), puIdx(idx), tyIdx(tdx), returnValues(allocator.Adapter()) {}

  CallNode(const MIRModule &mod, Opcode o) : CallNode(mod.GetCurFuncCodeMPAllocator(), o) {}

  CallNode(const MIRModule &mod, Opcode o, PUIdx idx, TyIdx tdx)
      : CallNode(mod.GetCurFuncCodeMPAllocator(), o, idx, tdx) {}

  CallNode(MapleAllocator &allocator, const CallNode &node)
      : NaryStmtNode(allocator, node),
        puIdx(node.GetPUIdx()),
        tyIdx(node.tyIdx),
        returnValues(allocator.Adapter()) {}

  CallNode(const MIRModule &mod, const CallNode &node) : CallNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  CallNode(CallNode &node) = delete;
  CallNode &operator=(const CallNode &node) = delete;
  ~CallNode() = default;
  virtual void Dump(const MIRModule &mod, int32 indent, bool newline) const;
  bool Verify() const;
  MIRType *GetCallReturnType();

  CallNode *CloneTree(MapleAllocator &allocator) const {
    CallNode *nd = allocator.GetMemPool()->New<CallNode>(allocator, *this);
    for (size_t i = 0; i < GetNopndSize(); i++) {
      nd->GetNopnd().push_back(GetNopndAt(i)->CloneTree(allocator));
    }
    for (size_t i = 0; i < returnValues.size(); i++) {
      nd->GetReturnVec().push_back(returnValues[i]);
    }
    nd->SetNumOpnds(GetNopndSize());
    return nd;
  }

  PUIdx GetPUIdx() const {
    return puIdx;
  }

  void SetPUIdx(const PUIdx idx) {
    puIdx = idx;
  }

  TyIdx GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(TyIdx idx) {
    tyIdx = idx;
  }

  CallReturnVector &GetReturnVec() {
    return returnValues;
  }

  CallReturnPair GetReturnPair(size_t idx) const {
    ASSERT(idx < returnValues.size(), "out of range in CallNode::GetReturnPair");
    return returnValues.at(idx);
  }

  void SetReturnPair(CallReturnPair retVal, size_t idx) {
    ASSERT(idx < returnValues.size(), "out of range in CallNode::GetReturnPair");
    returnValues.at(idx) = retVal;
  }

  const CallReturnVector &GetReturnVec() const {
    return returnValues;
  }

  CallReturnPair GetNthReturnVec(size_t i) const {
    ASSERT(i < returnValues.size(), "array index out of range");
    return returnValues[i];
  }

  void SetReturnVec(CallReturnVector &vec) {
    returnValues = vec;
  }

  uint8 NumOpnds(void) const {
    ASSERT(numOpnds == GetNopndSize(), "CallNode has wrong numOpnds field");
    return GetNopndSize();
  }

  virtual void Dump(const MIRModule &mod, int32 indent) const {
    Dump(mod, indent, true);
  }

  CallReturnVector *GetCallReturnVector() {
    return &returnValues;
  }

 private:
  PUIdx puIdx;
  TyIdx tyIdx;
  CallReturnVector returnValues;
};

class IcallNode : public NaryStmtNode {
 public:
  IcallNode(MapleAllocator &allocator, Opcode o)
      : NaryStmtNode(allocator, o), retTyIdx(0), returnValues(allocator.Adapter()) {
    SetNumOpnds(kOperandNumUnary);
  }

  IcallNode(MapleAllocator &allocator, Opcode o, TyIdx idx)
      : NaryStmtNode(allocator, o), retTyIdx(idx), returnValues(allocator.Adapter()) {
    SetNumOpnds(kOperandNumUnary);
  }

  IcallNode(const MIRModule &mod, Opcode o) : IcallNode(mod.GetCurFuncCodeMPAllocator(), o) {}

  IcallNode(const MIRModule &mod, Opcode o, TyIdx idx) : IcallNode(mod.GetCurFuncCodeMPAllocator(), o, idx) {}

  IcallNode(MapleAllocator &allocator, const IcallNode &node)
      : NaryStmtNode(allocator, node), retTyIdx(node.retTyIdx), returnValues(allocator.Adapter()) {}

  IcallNode(const MIRModule &mod, const IcallNode &node) : IcallNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  IcallNode(IcallNode &node) = delete;
  IcallNode &operator=(const IcallNode &node) = delete;
  ~IcallNode() = default;

  virtual void Dump(const MIRModule &mod, int32 indent, bool newline) const;
  bool Verify() const;
  MIRType *GetCallReturnType();
  IcallNode *CloneTree(MapleAllocator &allocator) const {
    IcallNode *nd = allocator.GetMemPool()->New<IcallNode>(allocator, *this);
    for (size_t i = 0; i < GetNopndSize(); i++) {
      nd->GetNopnd().push_back(GetNopndAt(i)->CloneTree(allocator));
    }
    for (size_t i = 0; i < returnValues.size(); i++) {
      nd->returnValues.push_back(returnValues[i]);
    }
    nd->SetNumOpnds(GetNopndSize());
    return nd;
  }

  TyIdx GetRetTyIdx() const {
    return retTyIdx;
  }

  void SetRetTyIdx(TyIdx idx) {
    retTyIdx = idx;
  }

  const CallReturnVector &GetReturnVec() const {
    return returnValues;
  }

  CallReturnVector &GetReturnVec() {
    return returnValues;
  }

  void SetReturnVec(CallReturnVector &vec) {
    returnValues = vec;
  }

  uint8 NumOpnds(void) const {
    ASSERT(numOpnds == GetNopndSize(), "IcallNode has wrong numOpnds field");
    return GetNopndSize();
  }

  virtual void Dump(const MIRModule &mod, int32 indent) const {
    Dump(mod, indent, true);
  }

  CallReturnVector *GetCallReturnVector() {
    return &returnValues;
  }

 private:
  TyIdx retTyIdx;  // return type for callee
  // the 0th operand is the function pointer
  CallReturnVector returnValues;
};

// used by intrinsiccall and xintrinsiccall
class IntrinsiccallNode : public NaryStmtNode {
 public:
  IntrinsiccallNode(MapleAllocator &allocator, Opcode o)
      : NaryStmtNode(allocator, o), intrinsic(INTRN_UNDEFINED), tyIdx(0), returnValues(allocator.Adapter()) {}

  IntrinsiccallNode(MapleAllocator &allocator, Opcode o, MIRIntrinsicID id)
      : NaryStmtNode(allocator, o), intrinsic(id), tyIdx(0), returnValues(allocator.Adapter()) {}

  IntrinsiccallNode(const MIRModule &mod, Opcode o) : IntrinsiccallNode(mod.GetCurFuncCodeMPAllocator(), o) {}

  IntrinsiccallNode(const MIRModule &mod, Opcode o, MIRIntrinsicID id)
      : IntrinsiccallNode(mod.GetCurFuncCodeMPAllocator(), o, id) {}

  IntrinsiccallNode(MapleAllocator &allocator, const IntrinsiccallNode &node)
      : NaryStmtNode(allocator, node),
        intrinsic(node.GetIntrinsic()),
        tyIdx(node.tyIdx),
        returnValues(allocator.Adapter()) {}

  IntrinsiccallNode(const MIRModule &mod, const IntrinsiccallNode &node)
      : IntrinsiccallNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  IntrinsiccallNode(IntrinsiccallNode &node) = delete;
  IntrinsiccallNode &operator=(const IntrinsiccallNode &node) = delete;
  ~IntrinsiccallNode() = default;

  virtual void Dump(const MIRModule &mod, int32 indent, bool newline) const;
  bool Verify() const;
  MIRType *GetCallReturnType();

  IntrinsiccallNode *CloneTree(MapleAllocator &allocator) const {
    IntrinsiccallNode *nd = allocator.GetMemPool()->New<IntrinsiccallNode>(allocator, *this);
    for (size_t i = 0; i < GetNopndSize(); i++) {
      nd->GetNopnd().push_back(GetNopndAt(i)->CloneTree(allocator));
    }
    for (size_t i = 0; i < returnValues.size(); i++) {
      nd->GetReturnVec().push_back(returnValues[i]);
    }
    nd->SetNumOpnds(GetNopndSize());
    return nd;
  }

  MIRIntrinsicID GetIntrinsic() const {
    return intrinsic;
  }

  void SetIntrinsic(MIRIntrinsicID id) {
    intrinsic = id;
  }

  TyIdx GetTyIdx() const {
    return tyIdx;
  }

  void SetTyIdx(TyIdx idx) {
    tyIdx = idx;
  }

  CallReturnVector &GetReturnVec() {
    return returnValues;
  }

  const CallReturnVector &GetReturnVec() const {
    return returnValues;
  }

  void SetReturnVec(CallReturnVector &vec) {
    returnValues = vec;
  }

  uint8 NumOpnds(void) const {
    ASSERT(numOpnds == GetNopndSize(), "IntrinsiccallNode has wrong numOpnds field");
    return GetNopndSize();
  }

  virtual void Dump(const MIRModule &mod, int32 indent) const {
    Dump(mod, indent, true);
  }

  CallReturnVector *GetCallReturnVector() {
    return &returnValues;
  }

  CallReturnPair &GetCallReturnPair(uint32 i) {
    ASSERT(i < returnValues.size(), "array index out of range");
    return returnValues.at(i);
  }

 private:
  MIRIntrinsicID intrinsic;
  TyIdx tyIdx;
  CallReturnVector returnValues;
};

// used by callinstant, virtualcallinstant, superclasscallinstant and
// interfacecallinstant
// for callinstantassigned, virtualcallinstantassigned,
// superclasscallinstantassigned and interfacecallinstantassigned
class CallinstantNode : public CallNode {
 public:
  CallinstantNode(MapleAllocator &allocator, Opcode o, TyIdx tIdx) : CallNode(allocator, o), instVecTyIdx(tIdx) {}

  CallinstantNode(const MIRModule &mod, Opcode o, TyIdx tIdx)
      : CallinstantNode(mod.GetCurFuncCodeMPAllocator(), o, tIdx) {}

  CallinstantNode(MapleAllocator &allocator, const CallinstantNode &node)
      : CallNode(allocator, node), instVecTyIdx(node.instVecTyIdx) {}

  CallinstantNode(const MIRModule &mod, const CallinstantNode &node)
      : CallinstantNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  CallinstantNode(CallinstantNode &node) = delete;
  CallinstantNode &operator=(const CallinstantNode &node) = delete;
  ~CallinstantNode() = default;

  virtual void Dump(const MIRModule &mod, int32 indent, bool newline) const;
  virtual void Dump(const MIRModule &mod, int32 indent) const {
    Dump(mod, indent, true);
  }

  CallinstantNode *CloneTree(MapleAllocator &allocator) const {
    CallinstantNode *nd = allocator.GetMemPool()->New<CallinstantNode>(allocator, *this);
    for (size_t i = 0; i < GetNopndSize(); i++) {
      nd->GetNopnd().push_back(GetNopndAt(i)->CloneTree(allocator));
    }
    for (size_t i = 0; i < GetReturnVec().size(); i++) {
      nd->GetReturnVec().push_back(GetNthReturnVec(i));
    }
    nd->SetNumOpnds(GetNopndSize());
    return nd;
  }

  CallReturnVector *GetCallReturnVector() {
    return &GetReturnVec();
  }

 private:
  TyIdx instVecTyIdx;
};

// for java boundary check
class AssertStmtNode : public BinaryStmtNode {
 public:
  explicit AssertStmtNode(Opcode op) : BinaryStmtNode(op) {
    isLt = (op == OP_assertlt);
  }

  ~AssertStmtNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

 private:
  bool isLt;
};

class LabelNode : public StmtNode {
 public:
  LabelNode() : StmtNode(OP_label), labelIdx(0) {}

  explicit LabelNode(LabelIdx idx) : StmtNode(OP_label), labelIdx(idx) {}

  ~LabelNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  LabelNode *CloneTree(MapleAllocator &allocator) const {
    LabelNode *l = allocator.GetMemPool()->New<LabelNode>(*this);
    l->SetStmtID(stmtIDNext++);
    return l;
  }

  LabelIdx GetLabelIdx() const {
    return labelIdx;
  }

  void SetLabelIdx(LabelIdx idx) {
    labelIdx = idx;
  }

 private:
  LabelIdx labelIdx;
};

class CommentNode : public StmtNode {
 public:
  explicit CommentNode(MapleAllocator &allocator) : StmtNode(OP_comment), comment(allocator.GetMemPool()) {}

  explicit CommentNode(const MIRModule &mod) : CommentNode(mod.GetCurFuncCodeMPAllocator()) {}

  CommentNode(MapleAllocator &allocator, const std::string &cmt)
      : StmtNode(OP_comment), comment(cmt, allocator.GetMemPool()) {}

  CommentNode(const MIRModule &mod, const std::string &cmt) : CommentNode(mod.GetCurFuncCodeMPAllocator(), cmt) {}

  CommentNode(MapleAllocator &allocator, const CommentNode &node)
      : StmtNode(node.GetOpCode(), node.GetPrimType(), node.numOpnds),
        comment(node.comment, allocator.GetMemPool()) {}

  CommentNode(const MIRModule &mod, const CommentNode &node) : CommentNode(mod.GetCurFuncCodeMPAllocator(), node) {}

  CommentNode(CommentNode &node) = delete;
  CommentNode &operator=(const CommentNode &node) = delete;
  ~CommentNode() = default;

  void Dump(const MIRModule &mod, int32 indent) const;

  CommentNode *CloneTree(MapleAllocator &allocator) const {
    CommentNode *c = allocator.GetMemPool()->New<CommentNode>(allocator, *this);
    return c;
  }

  const MapleString &GetComment() const {
    return comment;
  }

  void SetComment(MapleString com) {
    comment = com;
  }

  void SetComment(std::string &str) {
    comment = str;
  }

  void SetComment(const char *str) {
    comment = str;
  }

  void Append(const std::string &str) {
    comment.append(str);
  }

 private:
  MapleString comment;
};

void DumpCallReturns(const MIRModule &mod, CallReturnVector nrets, int32 indent);
}  // namespace maple
#endif  // MAPLE_IR_INCLUDE_MIR_NODES_H
