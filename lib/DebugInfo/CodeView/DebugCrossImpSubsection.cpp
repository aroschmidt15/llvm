//===- DebugCrossImpSubsection.cpp ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/DebugCrossImpSubsection.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::codeview;

Error VarStreamArrayExtractor<CrossModuleImportItem>::
operator()(BinaryStreamRef Stream, uint32_t &Len,
           codeview::CrossModuleImportItem &Item) {
  BinaryStreamReader Reader(Stream);
  if (Reader.bytesRemaining() < sizeof(CrossModuleImport))
    return make_error<CodeViewError>(
        cv_error_code::insufficient_buffer,
        "Not enough bytes for a Cross Module Import Header!");
  if (auto EC = Reader.readObject(Item.Header))
    return EC;
  if (Reader.bytesRemaining() < Item.Header->Count * sizeof(uint32_t))
    return make_error<CodeViewError>(
        cv_error_code::insufficient_buffer,
        "Not enough to read specified number of Cross Module References!");
  if (auto EC = Reader.readArray(Item.Imports, Item.Header->Count))
    return EC;
  return Error::success();
}

Error DebugCrossModuleImportsSubsectionRef::initialize(
    BinaryStreamReader Reader) {
  return Reader.readArray(References, Reader.bytesRemaining());
}

Error DebugCrossModuleImportsSubsectionRef::initialize(BinaryStreamRef Stream) {
  BinaryStreamReader Reader(Stream);
  return initialize(Reader);
}

void DebugCrossModuleImportsSubsection::addImport(StringRef Module,
                                                  uint32_t ImportId) {
  Strings.insert(Module);
  std::vector<support::ulittle32_t> Targets = {support::ulittle32_t(ImportId)};
  auto Result = Mappings.insert(std::make_pair(Module, Targets));
  if (!Result.second)
    Result.first->getValue().push_back(Targets[0]);
}

uint32_t DebugCrossModuleImportsSubsection::calculateSerializedSize() const {
  uint32_t Size = 0;
  for (const auto &Item : Mappings) {
    Size += sizeof(CrossModuleImport);
    Size += sizeof(support::ulittle32_t) * Item.second.size();
  }
  return Size;
}

Error DebugCrossModuleImportsSubsection::commit(
    BinaryStreamWriter &Writer) const {
  using T = decltype(&*Mappings.begin());
  std::vector<T> Ids;
  Ids.reserve(Mappings.size());

  for (const auto &M : Mappings)
    Ids.push_back(&M);

  llvm::sort(Ids.begin(), Ids.end(), [this](const T &L1, const T &L2) {
    return Strings.getIdForString(L1->getKey()) <
           Strings.getIdForString(L2->getKey());
  });

  for (const auto &Item : Ids) {
    CrossModuleImport Imp;
    Imp.ModuleNameOffset = Strings.getIdForString(Item->getKey());
    Imp.Count = Item->getValue().size();
    if (auto EC = Writer.writeObject(Imp))
      return EC;
    if (auto EC = Writer.writeArray(makeArrayRef(Item->getValue())))
      return EC;
  }
  return Error::success();
}
