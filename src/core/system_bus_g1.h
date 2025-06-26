#pragma once

#include "fox/mmio_device.h"
#include "shared/log.h"
#include "shared/fifo_engine.h"
#include "shared/scheduler.h"
#include "peripherals/gdrom.h"
#include "serialization/serializer.h"

class Console;

class G1Bus : public fox::MMIODevice, serialization::Serializer {
  Log::Logger<Log::LogModule::HOLLY> log;

public:
  G1Bus(Console *console);
  ~G1Bus();

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
  Console *const m_console;
  fox::MemoryTable *const m_memory;

  /*!
   * @brief DMA engine executor
   */
  std::unique_ptr<FifoEngine<u32>> m_engine;

  /*!
   * @brief Event used for GDROM-DMA completion scheduling.
   */
  EventScheduler::Event m_event_gdrom_dma;

  struct {
    u32 GD_DMA_START_ADDRESS = 0;
    u32 GD_DMA_ADDRESS_COUNT = 0;
    u32 GD_DMA_LENGTH = 0;
    u32 GD_DMA_DIRECTION = 0;
    u32 GD_DMA_ENABLE = 0;
    u32 GD_DMA_START = 0;
    u32 GD_DMA_TRANSFER_COUNTER = 0;
  } regs;

  /*!
   * @brief Details for an in-progress DMA operation.
   */
  struct {
    u32 destination = 0u;
    u32 length = 0u;
    u64 start_time;
  } m_dma;

  /*!
   * @brief Callback run by the FifoEngine to handle G1 bus logic
   *        execution.
   */
  void engine_callback(u32 address, const u32 &value);

  /*!
   * @brief Scheduled callback used to complete a DMA operation.
   */
  void finish_dma();
};
