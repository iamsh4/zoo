#pragma once

#include <iterator>
#include <cstddef>

#include <map>
#include <vector>
#include <initializer_list>

#include <fox/memtable.h>
#include "shared/types.h"

struct SDKSymbol {
  const char *sdk_name;
  const char *library_name;
  const char *symbol_name;
  u32 first_return_hash;
  u32 total_length;
  u32 total_hash;
  /*! Defines whether or not this function has a collision with some other symbol, either
   * in the same SDK or another SDK. This can happen if the functions within an SDK are
   * literally the same, or the same byte-for-byte function exists in another SDK (which
   * happens a lot of course) */
  bool is_ambiguous;
};

class SDKSymbolManager {
private:
  // { SDKSymbol.first_return_hash -> SDKSymbol[] }
  using SymbolMultimap = std::multimap<u32, const SDKSymbol *>;

  SymbolMultimap m_hash_to_symbols;

  SDKSymbolManager();

public:
  static SDKSymbolManager &instance();

  u32 get_matching_function_symbols(const fox::MemoryTable& mem_table,
                                    u32 start_address,
                                    std::vector<const SDKSymbol *> &output,
                                    u32 limit);
};

const std::vector<SDKSymbol> &get_sdk_symbols();
