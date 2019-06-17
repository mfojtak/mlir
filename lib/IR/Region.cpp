//===- Region.cpp - MLIR Region Class -------------------------------------===//
//
// Copyright 2019 The MLIR Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================

#include "mlir/IR/Region.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/Function.h"
#include "mlir/IR/Operation.h"
using namespace mlir;

Region::Region(Function *container) : container(container) {}

Region::Region(Operation *container) : container(container) {}

Region::~Region() {
  // Operations may have cyclic references, which need to be dropped before we
  // can start deleting them.
  for (auto &bb : *this)
    bb.dropAllReferences();
}

/// Return the context this region is inserted in. The region must have a valid
/// parent container.
MLIRContext *Region::getContext() {
  assert(!container.isNull() && "region is not attached to a container");
  if (auto *inst = getContainingOp())
    return inst->getContext();
  return getContainingFunction()->getContext();
}

/// Return a location for this region. This is the location attached to the
/// parent container. The region must have a valid parent container.
Location Region::getLoc() {
  assert(!container.isNull() && "region is not attached to a container");
  if (auto *inst = getContainingOp())
    return inst->getLoc();
  return getContainingFunction()->getLoc();
}

Region *Region::getContainingRegion() {
  if (auto *inst = getContainingOp())
    return inst->getContainingRegion();
  return nullptr;
}

Operation *Region::getContainingOp() {
  return container.dyn_cast<Operation *>();
}

Function *Region::getContainingFunction() {
  return container.dyn_cast<Function *>();
}

bool Region::isProperAncestor(Region *other) {
  if (this == other)
    return false;

  while ((other = other->getContainingRegion())) {
    if (this == other)
      return true;
  }
  return false;
}

/// Clone the internal blocks from this region into `dest`. Any
/// cloned blocks are appended to the back of dest.
void Region::cloneInto(Region *dest, BlockAndValueMapping &mapper,
                       MLIRContext *context) {
  assert(dest && "expected valid region to clone into");

  // If the list is empty there is nothing to clone.
  if (empty())
    return;

  iterator lastOldBlock = --dest->end();
  for (Block &block : *this) {
    Block *newBlock = new Block();
    mapper.map(&block, newBlock);

    // Clone the block arguments. The user might be deleting arguments to the
    // block by specifying them in the mapper. If so, we don't add the
    // argument to the cloned block.
    for (auto *arg : block.getArguments())
      if (!mapper.contains(arg))
        mapper.map(arg, newBlock->addArgument(arg->getType()));

    // Clone and remap the operations within this block.
    for (auto &op : block)
      newBlock->push_back(op.clone(mapper, context));

    dest->push_back(newBlock);
  }

  // Now that each of the blocks have been cloned, go through and remap the
  // operands of each of the operations.
  auto remapOperands = [&](Operation *op) {
    for (auto &operand : op->getOpOperands())
      if (auto *mappedOp = mapper.lookupOrNull(operand.get()))
        operand.set(mappedOp);
    for (auto &succOp : op->getBlockOperands())
      if (auto *mappedOp = mapper.lookupOrNull(succOp.get()))
        succOp.set(mappedOp);
  };

  for (auto it = std::next(lastOldBlock), e = dest->end(); it != e; ++it)
    it->walk(remapOperands);
}

/// Find the values used by operations in `region` that are defined outside its
/// ancestor region `limit`.  That is, given `A{B{C{}}}` with region `C` and
/// limit `B`, the values defined in `A` will be found while the values defined
/// in `B` will not.  Append these values to `values`.  If `stopAfterOne` is
/// set, return immediate after one such value was found (used for isolation
/// checks).  Additionally, emit errors if `noteLoc` is provided; this location
/// is used to point to the operation containing the region, the actual error is
/// reported at the operation with an offending use.
static void findValuesDefinedAbove(Region &region, Region &limit,
                                   SmallVectorImpl<Value *> &values,
                                   llvm::Optional<Location> noteLoc,
                                   bool stopAfterOne = false) {
  assert(limit.isAncestor(&region) &&
         "expected isolation limit to be an ancestor of the given region");

  // List of regions to analyze.  Each region is processed independently, with
  // respect to the common `limit` region, so we can look at them in any order.
  // Therefore, use a simple vector and push/pop back the current region.
  SmallVector<Region *, 8> pendingRegions;
  pendingRegions.push_back(&region);

  // Traverse all operations in the region.
  while (!pendingRegions.empty()) {
    for (Block &block : *pendingRegions.pop_back_val()) {
      for (Operation &op : block) {
        for (Value *operand : op.getOperands()) {
          // Check that any value that is used by an operation is defined in the
          // same region as either an operation result or a block argument.
          if (operand->getContainingRegion()->isProperAncestor(&limit)) {
            if (noteLoc) {
              op.emitOpError("using value defined outside the region")
                      .attachNote(noteLoc)
                  << "required by region isolation constraints";
            }
            values.push_back(operand);
            if (stopAfterOne)
              return;
          }
        }
        // Schedule any regions the operations contain for further checking.
        pendingRegions.reserve(pendingRegions.size() + op.getNumRegions());
        for (Region &subRegion : op.getRegions())
          pendingRegions.push_back(&subRegion);
      }
    }
  }
}

SmallVector<Value *, 8> Region::getUsedValuesDefinedAbove() {
  SmallVector<Value *, 8> values;
  findValuesDefinedAbove(*this, *this, values, llvm::None,
                         /*stopAfterOne=*/false);
  return values;
}

bool Region::isIsolatedFromAbove(llvm::Optional<Location> noteLoc) {
  SmallVector<Value *, 1> values;
  findValuesDefinedAbove(*this, *this, values, noteLoc, /*stopAfterOne=*/true);
  return values.empty();
}

/// Walk the operations in this block in postorder, calling the callback for
/// each operation.
void Region::walk(const std::function<void(Operation *)> &callback) {
  for (auto &block : *this)
    block.walk(callback);
}

Region *llvm::ilist_traits<::mlir::Block>::getContainingRegion() {
  size_t Offset(
      size_t(&((Region *)nullptr->*Region::getSublistAccess(nullptr))));
  iplist<Block> *Anchor(static_cast<iplist<Block> *>(this));
  return reinterpret_cast<Region *>(reinterpret_cast<char *>(Anchor) - Offset);
}

/// This is a trait method invoked when a basic block is added to a region.
/// We keep the region pointer up to date.
void llvm::ilist_traits<::mlir::Block>::addNodeToList(Block *block) {
  assert(!block->getParent() && "already in a region!");
  block->parentValidInstOrderPair.setPointer(getContainingRegion());
}

/// This is a trait method invoked when an operation is removed from a
/// region.  We keep the region pointer up to date.
void llvm::ilist_traits<::mlir::Block>::removeNodeFromList(Block *block) {
  assert(block->getParent() && "not already in a region!");
  block->parentValidInstOrderPair.setPointer(nullptr);
}

/// This is a trait method invoked when an operation is moved from one block
/// to another.  We keep the block pointer up to date.
void llvm::ilist_traits<::mlir::Block>::transferNodesFromList(
    ilist_traits<Block> &otherList, block_iterator first, block_iterator last) {
  // If we are transferring operations within the same function, the parent
  // pointer doesn't need to be updated.
  auto *curParent = getContainingRegion();
  if (curParent == otherList.getContainingRegion())
    return;

  // Update the 'parent' member of each Block.
  for (; first != last; ++first)
    first->parentValidInstOrderPair.setPointer(curParent);
}