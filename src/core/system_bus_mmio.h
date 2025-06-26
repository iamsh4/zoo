#pragma once

#include <atomic>
#include <array>

#include "fox/mmio_device.h"
#include "shared/fifo_engine.h"
#include "shared/log.h"
#include "peripherals/gdrom.h"
#include "serialization/serializer.h"
#include "guest/sh4/sh4.h"

class Console;

/*
 * From page 294+ of DreamcastDevBoxSystemArchitecture.pdf:
 *
 * ===== SB_ISTNRM bitmask =====
 * Writing a bit clears that bit.
 *
 * (1u << 21u): End of Transferring (Punch Through List)
 * (1u << 20u): End of DMA (Sort-DMA, Transferring for alpha sorting)
 * (1u << 19u): End of DMA (ch2-DMA)
 * (1u << 18u): End of DMA (Dev-DMA, Development Tool DMA)
 * (1u << 17u): End of DMA (Ext-DMA2, External 2)
 * (1u << 16u): End of DMA (Ext-DMA1, External 1)
 * (1u << 15u): End of DMA (AICA-DMA)
 * (1u << 14u): End of DMA (GD-DMA)
 * (1u << 13u): Maple V-Blank Over
 * (1u << 12u): End of DMA (Maple-DMA)
 * (1u << 11u): End of DMA (PVR-DMA)
 * (1u << 10u): End of Transferring (Translucent Modifier Volume List)
 * (1u << 9u):  End of Transferring (Translucent List)
 * (1u << 8u):  End of Transferring (Opaque Modifier Volume List)
 * (1u << 7u):  End of Transferring (Opaque List)
 * (1u << 6u):  End of Transferring (YUV)
 * (1u << 5u):  H Blank-in
 * (1u << 4u):  V Blank-out
 * (1u << 3u):  V Blank-in
 * (1u << 2u):  End of Render (TSP)
 * (1u << 1u):  End of Render (ISP)
 * (1u << 0u):  End of Render (Video)
 *
 * Additionally:
 *  - bit 31 is the OR of the following error interrupts:
 *    Render ISP out of cache, Render aborted by frame change (see: SB_ISTEXT)
 *  - bit 30 is the OR for G1/G2/Ext interrupts and two SB_ISTERR bits
 *    GD-ROM, AICA, Modem, etc.
 *    SB_ISTERR: bit 0 (ISP out of cache)
 *               bit 1 (Hazard processing of strip buffer)
 *
 * ===== SB_ISTEXT =====
 * No writes are allowed here.
 *
 * (1u << 3u): External Device
 * (1u << 2u): Modem
 * (1u << 1u): AICA
 * (1u << 0u): GD-ROM
 *
 * ===== SB_ISTERR =====
 * Writing a bit will clear it and the associated interrupt
 *
 * (see page 295)
 */

class SystemBus : public fox::MMIODevice, serialization::Serializer {
public:
  SystemBus(Console *console);
  ~SystemBus();

  void raise_int_normal(Interrupts::Normal::Type id);
  void raise_int_external(Interrupts::External::Type id);
  void raise_int_error(Interrupts::Error::Type id);

  void drop_int_external(unsigned id);

  void reset();
  void register_regions(fox::MemoryTable *memory) override;

  void serialize(serialization::Snapshot &snapshot) final;
  void deserialize(const serialization::Snapshot &snapshot) final;

  // TODO : These should be consolidated with Holly. They're the same device.
  u32 get_SB_LMMODE0() const { return regs[(size_t)Regs::SB_LMMODE0]; }
  u32 get_SB_LMMODE1() const { return regs[(size_t)Regs::SB_LMMODE1]; }

protected:
  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 val) override;
  void write_u16(u32 addr, u16 val) override;
  void write_u32(u32 addr, u32 val) override;

  /*!
   * @brief Entry point for thread which handles requests that can be done
   *        asynchronously.
   */
  void async_handler();

private:
  Log::Logger<Log::LogModule::HOLLY> log;

  Console *m_console;

  /* External Hardware Connections */
  cpu::SH4 *const cpu;
  fox::MemoryTable *const memory;

  /* Internal Emulator State */
  FifoEngine<u32> *m_engine;

  /* Register States */
  enum class Regs
  {
    SB_ISTEXT = 0,
    SB_ISTNRM,
    SB_ISTERR,
    SB_IML2NRM,
    SB_IML4NRM,
    SB_IML6NRM,
    SB_IML2EXT,
    SB_IML4EXT,
    SB_IML6EXT,
    SB_IML2ERR,
    SB_IML4ERR,
    SB_IML6ERR,
    SB_PDTNRM,
    SB_PDTEXT,
    SB_G2DNRM,
    SB_G2DEXT,
    SB_C2DSTAT,
    SB_C2DLEN,
    SB_LMMODE0,
    SB_LMMODE1,
    N_REGISTERS
  };
  std::array<std::atomic<u32>, (size_t)Regs::N_REGISTERS> regs;
  std::atomic<u32> &reg(const Regs &reg)
  {
    return regs[(size_t)reg];
  }

  /*
   * @brief Recalculate the levels for ASIC connected IRQs following a change
   *        of interrupt acknowledge or masks.
   */
  void recalculate_irqs();

  /*!
   * @brief Callback run by the FifoEngine to handle DMA logic
   */
  void engine_callback(u32 address, const u32 &value);
};
