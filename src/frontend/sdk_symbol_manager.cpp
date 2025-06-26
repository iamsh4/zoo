#include "frontend/sdk_symbols.h"
#include "shared/crc32.h"

namespace sdk::sh4 {

bool
is_return(u16 opcode)
{
  const u32 RET = 0b0000000000001011;
  const u32 RTE = 0b0000000000101011;
  return opcode == RET || opcode == RTE;
}

const u32 NO_RETURN_FOUND = 0xFFFFFFFF;

u32
get_first_return(const fox::MemoryTable &mem_table, u32 start_address)
{
  // Need 16bit aligned
  if (start_address % 2 != 0)
    return NO_RETURN_FOUND;

  const u32 CHECK_LENGTH = 2048;
  if (!mem_table.check_ram(start_address, CHECK_LENGTH))
    return NO_RETURN_FOUND;

  const u8 *mem_root = mem_table.root();
  for (u32 i = 0; i < CHECK_LENGTH; i += 2) {
    const u16 opcode =
      (mem_root[start_address + i + 1] << 8) | mem_root[start_address + i];
    if (is_return(opcode)) {
      return start_address + i;
    }
  }
  return NO_RETURN_FOUND;
}

}

SDKSymbolManager::SDKSymbolManager()
{
  for (const auto &sym : get_sdk_symbols()) {
    m_hash_to_symbols.insert({ sym.first_return_hash, &sym });
  }
}

SDKSymbolManager &
SDKSymbolManager::instance()
{
  static SDKSymbolManager manager;
  return manager;
}

u32
SDKSymbolManager::get_matching_function_symbols(const fox::MemoryTable &mem_table,
                                                u32 start_address,
                                                std::vector<const SDKSymbol *> &output,
                                                u32 limit)
{
  output.clear();

  // No return address == no iterator
  const u32 first_return_address = sdk::sh4::get_first_return(mem_table, start_address);
  if (first_return_address == sdk::sh4::NO_RETURN_FOUND)
    return 0;

  const u32 full_check_size = first_return_address - start_address;
  if (!mem_table.check_ram(start_address, full_check_size))
    return 0;

  const u8 *mem_root = mem_table.root();
  const u32 first_ret_hash = crc32(&mem_root[start_address], full_check_size);

  u32 count = 0;
  auto it = m_hash_to_symbols.find(first_ret_hash);
  for (; count < limit && it != m_hash_to_symbols.end(); ++it) {
    output.push_back(it->second);
    count++;
  }
  return count;
}
