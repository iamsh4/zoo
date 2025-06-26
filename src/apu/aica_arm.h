#pragma once

#include <guest/arm7di/arm7di.h>
#include "serialization/serializer.h"

// TODO : change to systems::dreamcast::aica or something
namespace apu {

class AicaArm : public guest::arm7di::Arm7DI, public serialization::Serializer {
public:
  AicaArm(fox::MemoryTable *);

  void serialize(serialization::Snapshot &snapshot) override;
  void deserialize(const serialization::Snapshot &snapshot) override;

private:
  fox::Value guest_load(u32 address, size_t bytes) final;
  void guest_store(u32 address, size_t bytes, fox::Value value) final;

  /*!
   * @brief Read from a memory location from the viewpoint of the ARM
   *        core.
   */
  template<typename T>
  T mem_read(u32 address);

  /*!
   * @brief Write to a memory location from the viewpoint of the ARM
   *        core.
   */
  template<typename T>
  void mem_write(u32 address, T value);

  fox::MemoryTable *m_memory;
};

} // namespace apu
