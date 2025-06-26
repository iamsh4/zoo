#pragma once

#include <tuple>
#include <vector>
#include "shared/types.h"

namespace dreamcast {
enum MemoryUsage : u8
{
  General,
  AICA_WaveData,
  AICA_Arm7Code,
  G1_DiscReadBuffer,
  G2_AICA_DMA,
  SH4_Code,
  GPU_TA_OPB,
  GPU_Texture,
  GPU_FrameBufferWrite,
  GPU_FrameBufferRead,
};
}

template<typename DataT>
class MemoryPageData {
public:
  MemoryPageData() : _range_start(0), _range_length(0) {}
  MemoryPageData(u32 address_range_start, u32 address_range_length, u32 page_size)
    : _range_start(address_range_start),
      _range_length(address_range_length),
      _page_size(page_size)
  {
    const u32 page_count = (address_range_length + page_size - 1) / page_size;
    _data.resize(page_count, dreamcast::MemoryUsage::General);
    _age.resize(page_count, 0);
  }

  void set(u32 address, DataT data)
  {
    const u32 offset = address - _range_start;
    const u32 page   = offset / _page_size;
    if (page < _data.size()) {
      _data[page] = data;
      _age[page]  = 0;
    }
  }

  const std::tuple<DataT, u64> get(u32 address)
  {
    const u32 offset = address - _range_start;
    const u32 page   = offset / _page_size;
    assert(page < _data.size());
    _age[page]++;
    return { _data[page], _age[page] };
  }

  const std::tuple<DataT, u64> get_page(u32 page)
  {
    assert(page < _data.size());
    _age[page]++;
    return { _data[page], _age[page] };
  }

  u32 range_start() const
  {
    return _range_start;
  }

  u32 range_length() const
  {
    return _range_length;
  }

  u32 page_size() const
  {
    return _page_size;
  }

  u32 page_count() const
  {
    return _data.size();
  }

private:
  const u32 _range_start;
  const u32 _range_length;
  const u32 _page_size;
  std::vector<DataT> _data;
  std::vector<u64> _age;
};
