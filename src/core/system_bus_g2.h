#pragma once

#include <thread>

#include "fox/mmio_device.h"
#include "shared/fifo_engine.h"
#include "shared/log.h"
#include "peripherals/gdrom.h"
#include "serialization/serializer.h"

class Console;

class G2Bus : public fox::MMIODevice, serialization::Serializer {
  Log::Logger<Log::LogModule::G2> log;

public:
  G2Bus(Console *console);
  ~G2Bus();

  void reset();
  void register_regions(fox::MemoryTable *memory) override;

  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 val) override;
  void write_u16(u32 addr, u16 val) override;
  void write_u32(u32 addr, u32 val) override;

  void serialize(serialization::Snapshot &snapshot) final;
  void deserialize(const serialization::Snapshot &snapshot) final;

private:
  Console *m_console;
  fox::MemoryTable *const m_memory;

  /* DMA engine executor */
  FifoEngine<u32> *m_engine;

  struct RegAddress {
#define G2_REG(reg_name, reg_addr, dma_channel, description)                             \
  static constexpr u32 reg_name = reg_addr;
#include "system_bus_g2_regs.inc.h"
#undef G2_REG
  };

  struct DMAChannelRegisters {
    u32 STAG;
    u32 STAR;
    u32 LEN;
    u32 DIR;
    u32 TSEL;
    u32 EN;
    u32 ST;
    u32 SUSP;
  };
  static const u32 NUM_DMA_CHANNELS = 4;
  DMAChannelRegisters dma_registers[NUM_DMA_CHANNELS];

  void finish_aica_dma();
  EventScheduler::Event m_event_aica_dma;

  /*!
   * @brief Callback run by the FifoEngine to handle G1 bus logic
   *        execution.
   */
  void engine_callback(u32 address, const u32 &value);
};
