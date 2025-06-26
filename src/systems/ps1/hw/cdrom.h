#pragma once

#include <deque>

#include "fox/mmio_device.h"
#include "shared/scheduler.h"
#include "shared/types.h"

namespace zoo::ps1 {

class Console;
class Disc;

class CDROM : public fox::MMIODevice {
private:
  union RequestRegister {
    struct {
      u8 _unused : 5;
      // Want Command Start Interrupt on Next Command (0=No change, 1=Yes)
      u8 SMEN : 1;
      // ???
      u8 BFWR : 1;
      // Want Data (0=No/Reset Data Fifo, 1=Yes/Load Data Fifo)
      u8 BFRD : 1;
    };
    u8 raw;
  } m_request_register = {};

  /*! Current index for register R/W. */
  union StatusRegister {
    struct {
      u8 index : 2; // Port 1F801801h-1F801803h index (0..3 = Index0..Index3)   (R/W)
      u8 adpm_fifo_empty : 1; // XA-ADPCM fifo empty  (0=Empty) ;set when playing XA-ADPCM
                              // sound
      u8 param_fifo_empty : 1; // Parameter fifo empty (1=Empty) ;triggered before writing
                               // 1st byte
      u8 param_fifo_write_ready : 1;  // Parameter fifo full  (0=Full)  ;triggered after
                                      // writing 16 bytes
      u8 response_fifo_not_empty : 1; // Response fifo empty  (0=Empty) ;triggered after
                                      // reading LAST byte
      u8 data_fifo_not_empty : 1;     //  Data fifo empty      (0=Empty) ;triggered after
                                      //  reading LAST byte
      u8 busy : 1;                    // Command/parameter transmission busy  (1=Busy)
    };
    u8 raw;
  } m_status = {
    .param_fifo_empty = 1, // parameter fifo initially empty
    .param_fifo_write_ready = 1,
  };

  union InterruptFlagRegister {
    struct {
      u8 reason : 3;
      u8 _unknown : 1;
      u8 command_start : 1;
      u8 SMADPCLR : 1;
      u8 CLRPRM : 1;
      u8 CHPRST : 1;
    };
    u8 raw;
  } m_interrupt_flags = {
    ._unknown = 0,
    .SMADPCLR = 1,
    .CLRPRM = 1,
    .CHPRST = 1,
  };

  // Calculates the drive status byte which is reported as data in most interrupts
  u8 drive_status();

  // The current status of the drive, used in forming the drive status value
  enum class ReadStatus
  {
    Idle = 0,
    Reading = 1,
    Seeking = 2,
    Playing = 4,
  };
  ReadStatus m_read_status = ReadStatus::Idle;

  struct SectorAddress {
    u32 track = 1;
    u32 minutes = 0;
    u32 seconds = 0;
    u32 sectors = 0;
  };

  // Read and seek are separately tracked. Only once read begins does the seek position
  // reflect in the read logic
  SectorAddress m_read_sector;
  SectorAddress m_seek_sector;

  u8 m_interrupt_enable;

  // Current data presented as the data fifo
  std::vector<u8> m_read_data;
  std::vector<u8> m_next_read_data;

  // Data read from the cdrom, becomes the next m_read_data
  u32 m_read_data_head = 0;

  // Response fifo for CPU to consume data from commands
  std::deque<u8> m_response_fifo;

  // See SetMode description
  u8 m_mode = 0;

  Console *m_console;
  Disc *m_disc = nullptr;

  u8 m_irq_bits = 0;
  void set_irq(u8 new_bits);

  EventScheduler::Event m_dispatch_cdrom_interrupt;

  void dispatch_cdrom_interrupt();

  // Writes to command do not happen for a small period of time, triggered by event
  u8 m_command_byte;
  EventScheduler::Event m_delayed_handle_command;
  void delayed_handle_command();

  void execute_command(u8 command);

  void advance_read_sector();

  // Consume from the data queue
  u8 read_data_byte();

  //////
  EventScheduler::Event m_subcpu_logic;
  void subcpu_logic();

  // queue of pending command responses (IRQ portion)
  struct InterruptRequest {
    u8 num;
    u8 originating_command;
    u64 delay_cycles = 100;
    std::vector<u8> response;
  };
  std::deque<InterruptRequest> m_subcpu_irq_queue;

  // Pushes a response interrupt and response bytes to the response queue.
  // This also schedules the interrupt to take place.
  void push_schedule_response(u8 source_command,
                              u8 interrupt,
                              std::initializer_list<u8>,
                              u64 delay = 200);

  // Pushes a response interrupt and response bytes to the response queue.
  void push_response(u8 source_command,
                     u8 interrupt,
                     std::initializer_list<u8>,
                     u64 delay = 200);

  std::deque<u8> m_parameter_fifo;
  u8 pop_param();

public:
  CDROM(Console *console);
  void set_disc(Disc *disc);

  u32 read_data_fifo();

  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 value) override;
  void write_u16(u32 addr, u16 value) override;
  void write_u32(u32 addr, u32 value) override;

  void register_regions(fox::MemoryTable *memory) override;
};

} // namespace zoo::ps1
