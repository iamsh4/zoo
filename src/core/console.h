#pragma once

#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <memory>
#include <chrono>

#include "fox/memtable.h"
#include "fox/mmio_device.h"
#include "shared/scheduler.h"
#include "peripherals/maple.h"
#include "peripherals/flashrom.h"
#include "core/interrupt_types.h"
#include "core/system_bus_mmio.h"
#include "guest/sh4/sh4.h"
#include "gpu/holly.h"
#include "gpu/display_list.h"
#include "apu/aica_rtc.h"
#include "apu/aica.h"
#include "apu/audio.h"
#include "gpu/texture_manager.h"
#include "serialization/session.h"
#include "shared/trace.h"
#include "local/settings.h"
#include "shared/guest_memory_usage.h"
#include "systems/dreamcast/metrics/system_metrics.h"

#include "systems/dreamcast/renderer.h"

class G1Bus;
class G2Bus;

enum class TraceTrack : u32
{
  Console = 0,
  CPU,
  PVR,
  SPG,
  G1,
  G2,
  AICA,
  GDROM,
  MAPLE,
  TA
};

/** A Dreamcast Console. This object orchestrates and connects the various subsystems
 *  of the console. It also provides facilities for connecting/disconnecting controllers,
 *  changing game discs, VMUs, etc. */
class Console {
public:
  static constexpr u64 SH4_CLOCK_RATE = 200llu * 1000 * 1000;

  /*!
   * @brief The number of nanoseconds per master clock cycle.
   */
  static constexpr u64 SH4_NANOS_PER_CYCLE = 1000llu * 1000 * 1000 / SH4_CLOCK_RATE;

  Console(std::shared_ptr<zoo::local::Settings> settings, apu::Audio *audio, zoo::dreamcast::Renderer *renderer);
  ~Console();

  std::shared_ptr<zoo::local::Settings> &settings()
  {
    return m_settings;
  }

  cpu::SH4 *cpu()
  {
    return m_sh4.get();
  }

  gpu::Holly *gpu()
  {
    return m_holly;
  }

  fox::MemoryTable *memory()
  {
    return m_mem.get();
  }

  SystemBus *system_bus()
  {
    return m_sys_bus;
  }

  Maple *maple_bus()
  {
    return m_maple;
  }

  GDRom *gdrom()
  {
    return m_gdrom;
  }

  apu::RTC *rtc()
  {
    return m_aica_rtc;
  }

  apu::AICA *aica()
  {
    return m_aica;
  }

  FlashROM *flashrom()
  {
    return m_flashrom;
  }

  gpu::TextureManager *texture_manager()
  {
    return m_texture_manager;
  }

  EventScheduler *scheduler()
  {
    return &m_scheduler;
  }

  zoo::dreamcast::Renderer *renderer()
  {
    return m_renderer;
  }

  u32 get_vblank_in_count() const
  {
    return m_holly->get_vblank_in_count();
  }

  u64 current_time() const
  {
    return m_elapsed_nanos;
  }

  struct MemoryUsage {
    std::unique_ptr<MemoryPageData<dreamcast::MemoryUsage>> ram;
    std::unique_ptr<MemoryPageData<dreamcast::MemoryUsage>> vram;
    std::unique_ptr<MemoryPageData<dreamcast::MemoryUsage>> waveram;
  };
  MemoryUsage _memory_usage;
  MemoryUsage &memory_usage()
  {
    return _memory_usage;
  }

  void power_reset();
  void open_disc_drive();
  void close_disc_drive();

  void load_elf(const std::string &path);

  void run_for(std::chrono::duration<u64, std::nano> nanoseconds);

  /*!
   * @brief Run a single SH4 JIT block and return. This is the smallest unit of
   *        execution when running with JIT enabled.
   */
  void debug_run_single_block();

  /*!
   * @brief Run an "emulated" SH4 JIT block by repeated single stepping of the
   *        interpreter. Allows synchronization of a Console instance with JIT
   *        disabled to one with JIT enabled.
   *
   * Continues running until the indicated cycle count since reset has been
   * reached.
   */
  void debug_step_single_block(u64 stop_cycle);

  // Step backward one instruction (if possible)
  void debug_step_back(serialization::Session &session);

  // Step forward one instruction
  void debug_step();

  void dump_ram(const std::string_view &file_path, u32 address, u32 length);

  /* Save/Load state */
  void save_state(serialization::Snapshot &snapshot);
  void load_state(const serialization::Snapshot &snapshot);

  void interrupt_normal(Interrupts::Normal::Type id)
  {
    m_sys_bus->raise_int_normal(id);
  }

  void interrupt_external(Interrupts::External::Type id)
  {
    m_sys_bus->raise_int_external(id);
  }

  void interrupt_error(Interrupts::Error::Type id)
  {
    m_sys_bus->raise_int_error(id);
  }

  // TODO : Controller connect/unconnect

  // TODO : Controller VMUs / peripherals
  //        (Possibly managed by controllers)

  // TODO : Modem interface (Last priority? :p)

  /*!
   * @brief Schedule an event to execute on the CPU emulation thread in the
   *        future. The time of execution is specified in nanoseconds relative
   *        to the current time.
   *
   * Note: Must only be called from the CPU thread.
   */
  void schedule_event(u64 delta_nanos, EventScheduler::Event *event);

  gpu::render::FrameData &get_frame_data()
  {
    return m_frame_data;
  }

  gpu::render::FrameData &get_last_frame_data()
  {
    return m_last_frame_data;
  }

  std::mutex &render_lock()
  {
    return m_frontend_render_lock;
  }

  using callback_func = std::function<void()>;
  void set_vblank_in_callback(callback_func callback)
  {
    m_vblank_in_callback = callback;
  }
  callback_func get_vblank_in_callback() const
  {
    return m_vblank_in_callback;
  }

  void set_trace(std::unique_ptr<Trace> trace)
  {
    m_trace = std::move(trace);
  }
  void trace_zone(const std::string_view name,
                  TraceTrack track,
                  u64 start_nanos,
                  u64 end_nanos);

  void trace_event(const std::string_view name, TraceTrack track, u64 nanos);

  zoo::dreamcast::SystemMetrics &metrics()
  {
    return m_metrics;
  }

private:
  std::shared_ptr<zoo::local::Settings> m_settings;

  zoo::dreamcast::Renderer* m_renderer;

  u64 m_elapsed_nanos;

  std::unique_ptr<Trace> m_trace = {};

  /*!
   * @brief Scheduling queue for determenistic timing of Console logic.
   */
  EventScheduler m_scheduler;

  /*!
   * @brief Console memory map. This is from the perspective of the SH4 CPU so
   *        it can be directly referenced from JIT'd code. Includes a mix of
   *        physical addresses and emulated virtual addresses (e.g. multiple
   *        maps of RAM).
   */
  std::unique_ptr<fox::MemoryTable> m_mem;

  /*!
   * @brief The SH4 / primary CPU for the Dreamcast.
   */
  std::unique_ptr<cpu::SH4> m_sh4;

  /*
   * Note: The following pointers are stored here only for quick lookup. The
   *       lifetime of these is tied to the MMIO device management in the
   *       memory table.
   */

  gpu::Holly *m_holly;
  SystemBus *m_sys_bus;
  G1Bus *m_g1_bus;
  G2Bus *m_g2_bus;
  Maple *m_maple;
  GDRom *m_gdrom;
  apu::RTC *m_aica_rtc;
  apu::AICA *m_aica;
  FlashROM *m_flashrom;
  gpu::TextureManager *m_texture_manager;

  zoo::dreamcast::SystemMetrics m_metrics;

  gpu::render::FrameData m_frame_data;
  gpu::render::FrameData m_last_frame_data;

  /*!
   * @brief All of the MMIO hardware available in the dreamcast, registered
   *        against the memory table.
   */
  std::set<std::unique_ptr<fox::MMIODevice>> mmio_devices;

  std::mutex m_frontend_render_lock;

  callback_func m_vblank_in_callback;

  /*!
   * @brief Setup MMIO, map memory regions and bootrom data
   */
  void prepare_hardware();
};
