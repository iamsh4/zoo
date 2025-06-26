#pragma once

#include <memory>
#include "media/disc.h"

class RegionFreeDreamcastDisc : public zoo::media::Disc {
private:
  std::shared_ptr<zoo::media::Disc> m_underlying;

public:
  RegionFreeDreamcastDisc(std::shared_ptr<zoo::media::Disc>);

  const std::vector<zoo::media::Track> &tracks() const override;
  const std::vector<zoo::media::Session> &get_toc() const override;
  zoo::media::SectorReadResult read_sector(u32 fad, util::Span<u8> output) override;
};
