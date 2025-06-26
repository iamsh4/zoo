#include "shared/types.h"
#include "systems/ps1/hw/cdrom.h"
#include "systems/ps1/console.h"
#include "systems/ps1/hw/disc.h"
#include "systems/ps1/hw/interrupts.h"

namespace zoo::ps1 {

// CDROM single speed reads 1 second worth of data every clock second.
// Divide by 75 sectors per second
// Divide by 2 since we're usually reading at double speed.
const u64 SECTOR_TIME_NANOS = 1'000'000'000llu / 75 / 2;

// INT0   No response received (no interrupt request)
// INT1   Received SECOND (or further) response to ReadS/ReadN (and Play+Report)
static constexpr u8 CDROM_INT1 = 1;
// INT2   Received SECOND response (to various commands)
static constexpr u8 CDROM_INT2 = 2;
// INT3   Received FIRST response (to any command)
static constexpr u8 CDROM_INT3 = 3;
// INT4   DataEnd (when Play/Forward reaches end of disk) (maybe also for Read?)
// static constexpr u8 CDROM_INT4 = 4;
// INT5   Received error-code (in FIRST or SECOND response)
static constexpr u8 CDROM_INT5 = 5;

// NOTE: General command request to CDROM
// 1. Param bytes
// 2. Command byte (Sends to CDROM Controller "Sub-CPU")
// 3. Once completed, interrupt is set in internal register...
//    - INT3 is generated on success.
//    - INT5 on an invalid command/parameter(s).
//    - Response bytes can be read from the response fifo
//    - (Some commands have a second response, which is another INT)
// 4. Raise CDROM interrupt on main cpu (r3000)
// 5. User interrupt reads response/data FIFO.
// 6. User code acknowledges interrupt (via 1F801803h.Index1). This resets the response
//    fifo.

CDROM::CDROM(Console *console)
  : m_console(console),
    m_dispatch_cdrom_interrupt("cdrom.dispatch_interrupts",
                               std::bind(&CDROM::dispatch_cdrom_interrupt, this),
                               console->scheduler()),
    m_delayed_handle_command("cdrom.handle_command_async",
                             std::bind(&CDROM::delayed_handle_command, this),
                             console->scheduler()),
    m_subcpu_logic("cdrom.subcpu_logic",
                   std::bind(&CDROM::subcpu_logic, this),
                   console->scheduler())
{
}

void
CDROM::set_disc(Disc *disc)
{
  m_disc = disc;
}

void
CDROM::subcpu_logic()
{
  // Function fires after a delay to raise an interrup that read from disc completed and
  // to present that data to the data fifo.

  // You can only make progress reading if there is no pending INT1
  bool already_has_int1 = false;
  for (auto el : m_subcpu_irq_queue) {
    if (el.num == CDROM_INT1) {
      already_has_int1 = true;
      break;
    }
  }

  // If we're in the middle of reading/playing, advance
  if ((m_read_status == ReadStatus::Reading || m_read_status == ReadStatus::Playing) &&
      !already_has_int1) {

    // One of the mode bits encodes whether or not 800h or 924h bytes should be presented
    // to the data fifo
    const SectorReadMode read_mode =
      (m_mode & 0x20) ? SectorReadMode_924 : SectorReadMode_800;

    if (m_disc) {
      m_next_read_data.clear();
      m_next_read_data.resize(2352);

      m_disc->read_sector_data(m_read_sector.minutes,
                               m_read_sector.seconds,
                               m_read_sector.sectors,
                               read_mode,
                               m_next_read_data.data());

      printf("cdrom: read_disc(min=%u,seconds=%u,sector=%u)\n",
             m_read_sector.minutes,
             m_read_sector.seconds,
             m_read_sector.sectors);

      // TODO : Probably this should be uncommented, needs testing
      const bool is_xaadpcm_enabled = false; // m_mode & 0x40;
      if (is_xaadpcm_enabled && read_mode == SectorReadMode_924) {
        const bool is_data = (m_next_read_data[0x6] & 0x08);
        if (is_data) {
          push_schedule_response(0xAA, CDROM_INT1, { drive_status() });
        }
      } else {
        push_schedule_response(0xAA, CDROM_INT1, { drive_status() });
      }

      advance_read_sector();

      // Schedule next sector read.
      m_subcpu_logic.cancel();
      m_console->schedule_event_nanos(SECTOR_TIME_NANOS, &m_subcpu_logic);

    } else {
      printf("cdrom: read but no disc?\n");
    }
  }
}

void
CDROM::push_schedule_response(u8 source_command,
                              u8 interrupt,
                              std::initializer_list<u8> data,
                              u64 cycles)
{
  push_response(source_command, interrupt, data);

  // Mark 'busy' between the time a response is scheduled and once the response is sent.
  m_status.busy = true;

  printf("cdrom: SCHEDULE response interrupt\n");
  m_dispatch_cdrom_interrupt.cancel();
  m_console->schedule_event(cycles, &m_dispatch_cdrom_interrupt);
}

void
CDROM::push_response(u8 source_command,
                     u8 interrupt,
                     std::initializer_list<u8> data,
                     u64 cycles)
{
  m_subcpu_irq_queue.push_back({ interrupt, source_command, cycles, data });
}

u8
CDROM::pop_param()
{
  u8 byte = m_parameter_fifo.front();
  m_parameter_fifo.pop_front();

  m_status.param_fifo_empty = m_parameter_fifo.empty();
  m_status.param_fifo_write_ready = true;

  return byte;
}

u8
CDROM::read_data_byte()
{
  u8 value = 0;

  if (m_read_data_head < m_read_data.size()) {
    value = m_read_data[m_read_data_head];
    m_read_data_head++;

    if (m_read_data_head == m_read_data.size()) {
      m_status.data_fifo_not_empty = false;
    }
  } else {
    printf("cdrom: WARNING read data fifo but no data present!\n");
  }

  return value;
}

u32
CDROM::read_data_fifo()
{
  u32 word = 0;
  word |= read_data_byte() << 0;
  word |= read_data_byte() << 8;
  word |= read_data_byte() << 16;
  word |= read_data_byte() << 24;

  if (m_read_data_head < 32)
    printf("cdrom: read_data_fifo [head=%u]... [0x%08x] (possibly more ommitted)\n",
           m_read_data_head - 4,
           word);

  return word;
}

void
CDROM::advance_read_sector()
{
  m_read_sector.sectors++;

  if (m_read_sector.sectors == 75) {
    m_read_sector.sectors = 0;
    m_read_sector.seconds++;
  }

  if (m_read_sector.seconds == 60) {
    m_read_sector.seconds = 0;
    m_read_sector.minutes++;
  }
}

void
CDROM::set_irq(u8 new_bits)
{
  const u8 signalled_before = m_irq_bits & m_interrupt_enable;
  m_irq_bits &= ~0b111;
  m_irq_bits |= new_bits;
  const u8 signalled_after = m_irq_bits & m_interrupt_enable;

  // edge triggered
  if (!signalled_before && signalled_after) {
    m_console->irq_control()->raise(interrupts::CDROM);
  }
}

void
CDROM::dispatch_cdrom_interrupt()
{
  m_status.busy = false;
  assert(!m_subcpu_irq_queue.empty());

  auto &response = m_subcpu_irq_queue.front();

  printf("cdrom: dispatch_func (cmd=0x%x, irq=%x)\n",
         response.originating_command,
         response.num);

  // Push response data to the response fifo
  for (u8 byte : response.response) {
    //  response fifo has limited size
    if (m_response_fifo.size() < 16) {
      m_response_fifo.push_back(byte);
      m_status.response_fifo_not_empty = 1;
    } else {
      printf("cdrom: warning fifo overrun\n");
    }
  }

  printf("cdrom: pushed new response bytes ... now we have\n");
  for (auto &e : m_subcpu_irq_queue) {
    printf("cdrom: - cmd=0x%x int=%u\n", e.originating_command, e.num);
  }

  // Raise pending interrupts

  const u8 mask = m_interrupt_enable;

  if (response.num & mask & 0b111) {
    printf("cdrom: dispatch int%u for command 0x%x\n",
           response.num,
           response.originating_command);

    set_irq(response.num);

    if (response.originating_command == 0x01 && response.num == 3) {
      printf(":: REACHED PAUSE cmd 1 int 3\n");
      // m_console->set_internal_pause(true);
    }

  } else {
    printf("cdrom: dispatch int%u for command 0x%x (IGNORED)\n",
           response.num,
           response.originating_command);
  }
}

u8
CDROM::read_u8(u32 addr)
{
  printf("cdrom: read_u8(0x%08x.%u)\n", addr, m_status.index);

  switch (addr) {
    case 0x1f80'1800:
      return m_status.raw;

    case 0x1f80'1802:
      return read_data_byte();

    case 0x1f80'1801:
      if (m_status.index == 1) {
        // The data in the fifo is read, then zero'padding up to 16 bytes, then loops.
        if (!m_response_fifo.empty()) {
          printf("cdrom: response fifo >> \n");

          u8 val = m_response_fifo.front();
          m_response_fifo.pop_front();

          if (m_response_fifo.empty()) {
            m_status.response_fifo_not_empty = false;
          }
          return val;
        } else {
          assert(false && "read from response fifo when empty");
          throw std::runtime_error("cdrom: read from response fifo when empty");
        }

      } else {
        throw std::runtime_error("cdrom: unhandled index for read");
      }
      break;
    case 0x1f80'1803:
      if (m_status.index == 0) {
        return m_interrupt_enable | 0b11100000;
      } else if (m_status.index == 1 || m_status.index == 3) {
        return m_irq_bits | 0b11100000;
      } else {
        throw std::runtime_error("cdrom: unhandled index for read");
      }
      break;
    default:
      throw std::runtime_error("cdrom: unhandled read_u8");
      break;
  }
}

u16
CDROM::read_u16(u32 addr)
{
  printf("cdrom: read_u16(0x%08x)\n", addr);
  assert(false);
  switch (addr) {
    default:
      throw std::runtime_error("cdrom: unhandled read_u16");
      break;
  }
}

void
CDROM::write_u16(u32 addr, u16 value)
{
  printf("cdrom: write_u16(0x%08x)\n", addr);
  assert(false);
}

void
CDROM::write_u32(u32 addr, u32 value)
{
  printf("cdrom: write_u32(0x%08x)\n", addr);
  assert(false);
}

u32
CDROM::read_u32(u32 addr)
{
  printf("cdrom: read_u32(0x%08x)\n", addr);
  assert(false);
  switch (addr) {
    default:
      throw std::runtime_error("cdrom: unhandled read_u32");
      break;
  }
}

void
CDROM::write_u8(u32 addr, u8 value)
{
  const u8 index = m_status.index;

  printf("cdrom: write_u8(addr=0x%08x, index=%u, val=0x%02x)\n", addr, index, value);

  if (addr == 0x1f80'1802 && index == 2) {
    // left cd out left spu
    return;
  }
  if (addr == 0x1f80'1803 && index == 2) {
    // left cd out left spu
    return;
  }
  if (addr == 0x1f80'1801 && index == 3) {
    // left cd out left spu
    return;
  }
  if (addr == 0x1f80'1802 && index == 3) {
    // left cd out left spu
    return;
  }

  switch (addr) {
    case 0x1F801800:
      m_status.index = value & 0b11;
      break;

    case 0x1F801801:
      if (m_status.index == 0) {
        printf("cdrom: command byte 0x%02x (pc 0x%08x)\n", value, m_console->cpu()->PC());
        m_command_byte = value;
        m_console->schedule_event(100, &m_delayed_handle_command);
      } else {
        throw std::runtime_error("cdrom: unhandled write");
      }
      break;

    case 0x1f80'1802:
      if (m_status.index == 0) {
        printf("cdrom: parameter fifo < 0x%02x\n", value);

        m_parameter_fifo.push_back(value);
        m_status.param_fifo_empty = false;
        m_status.param_fifo_write_ready = m_parameter_fifo.size() < 16;

      } else if (m_status.index == 1) {
        m_interrupt_enable = value;
      } else {
        throw std::runtime_error("cdrom: unhandled write");
      }
      break;

    case 0x1f801803:
      if (m_status.index == 0) {

        printf("cdrom: request_register (0x%02x)\n", value);
        if (value & 0x80) { // no$: "Want data"
          // Can only update data buffer to point at new sector once all of it has been
          // read
          printf("cdrom: want data set\n");
          if (m_read_data_head >= m_read_data.size()) {
            m_read_data = (m_next_read_data);
            m_status.data_fifo_not_empty = true;
            m_read_data_head = 0;
          }
        } else {
          // Clear data buffer
          m_read_data.clear();
          m_read_data_head = 0;
          m_status.data_fifo_not_empty = false;
        }

      } else if (m_status.index == 1) {
        // Writing "1" bits to bit0-4 resets the corresponding IRQ flags; normally one
        // should write 07h to reset the response bits, or 1Fh to reset all IRQ bits.
        // Writing values like 01h is possible (eg. that would change INT3 to INT2, but
        // doing that would be total nonsense). After acknowledge, the Response Fifo is
        // made empty, and if there's been a pending command, then that command gets
        // send to the controller.

        // Optionally clear the parameter fifo
        if (value & 0x40) {
          m_parameter_fifo.clear();
          m_status.param_fifo_empty = true;
          m_status.param_fifo_write_ready = true;
        }

        m_irq_bits &= ~(0x1f & value);

        // software needs to acknowledge IRQ to get queue to issue next interrupt
        if ((value & 0b111) && !m_subcpu_irq_queue.empty()) {
          printf("cdrom: software is ack'ing interrupt (cmd=0x%x, int=%u)\n",
                 m_subcpu_irq_queue.front().originating_command,
                 m_subcpu_irq_queue.front().num);

          // answer this interrupt
          printf("cdrom: irq queue non-empty (val=0x%x)\n", value);
          if (!m_dispatch_cdrom_interrupt.is_scheduled()) {
            printf("cdrom: popped int\n");
            m_subcpu_irq_queue.pop_front();
          }

          // schedule next if present
          if (!m_subcpu_irq_queue.empty() && !m_dispatch_cdrom_interrupt.is_scheduled()) {
            printf("cdrom: SCHEDULE next interrupt (cmd=0x%x, int=%u)\n",
                   m_subcpu_irq_queue.front().originating_command,
                   m_subcpu_irq_queue.front().num);
            m_console->schedule_event(m_subcpu_irq_queue.front().delay_cycles,
                                      &m_dispatch_cdrom_interrupt);
          }
        }

      } else if (index == 3) {
        printf("cdrom: bogus(?) write to 3.3\n");
      } else {
        throw std::runtime_error("cdrom: unhandled write");
      }
      break;
    default:
      throw std::runtime_error("cdrom: unhandled write_u8");
  }
}

u8
bcd_to_dec(u8 input)
{
  return (input / 16 * 10) + (input % 16);
}

void
CDROM::delayed_handle_command()
{
  printf("cdrom: executing delayed command 0x%x (params=", m_command_byte);
  for (u8 param : m_parameter_fifo) {
    printf("0x%x, ", param);
  }
  printf("\n");
  execute_command(m_command_byte);
  m_command_byte = 0xff;
}

u8
CDROM::drive_status()
{
  u8 result = 0;

  if (m_disc) {
    // Motor is always on
    result |= 1 << 1;
    // Read status flag (no seek tracking)
    result |= u32(m_read_status) << 5;
  } else {
    // Mark shell as open
    result = 0x10;
  }

  return result;
}

void
CDROM::execute_command(u8 command_byte)
{
  // Executing a command clears any pending commands.
  m_subcpu_irq_queue.clear();
  m_response_fifo.clear();
  m_dispatch_cdrom_interrupt.cancel();

  switch (command_byte) {

    case 0x01: { // GetStat
      printf("cdrom: getstat\n");
      push_schedule_response(command_byte, CDROM_INT3, { drive_status() });
    } break;

    case 0x02: { // SetLoc
      assert(m_parameter_fifo.size() >= 3);
      const u8 minutes = bcd_to_dec(pop_param());
      const u8 seconds = bcd_to_dec(pop_param());
      const u8 sectors = bcd_to_dec(pop_param());

      printf("cdrom: setloc(%u,%u,%u)\n", minutes, seconds, sectors);
      m_seek_sector = SectorAddress {
        .minutes = minutes,
        .seconds = seconds,
        .sectors = sectors,
      };
      // XXX : why does this seem to be required?
      m_read_sector = m_seek_sector;

      push_schedule_response(command_byte, CDROM_INT3, { drive_status() });
      break;
    }

    // ReadN
    case 0x06: {
      printf("cdrom: readn\n");
      m_read_status = ReadStatus::Reading;
      push_schedule_response(command_byte, CDROM_INT3, { drive_status() });

      m_subcpu_logic.cancel();
      m_console->schedule_event_nanos(SECTOR_TIME_NANOS, &m_subcpu_logic);
    } break;

    case 0x09: { // Pause
      printf("cdrom: pause\n");
      push_schedule_response(command_byte, CDROM_INT3, { drive_status() }, 50);
      m_read_status = ReadStatus::Idle;
      push_response(command_byte, CDROM_INT2, { drive_status() });
    } break;

    case 0x0a: { // Init
      printf("cdrom: init\n");
      push_schedule_response(command_byte, CDROM_INT3, { drive_status() });
      m_read_status = ReadStatus::Idle;
      m_subcpu_logic.cancel(); // xxx maybe?
      push_response(command_byte, CDROM_INT2, { drive_status() });
      // XXX : late state update? rustation does something here with async_init

    } break;

    case 0x0c: { // Demute
      printf("cdrom: demute\n");
      push_schedule_response(command_byte, CDROM_INT3, { drive_status() });

    } break;

    case 0x0d: { // SetFilter
      printf("cdrom: setfilter\n");
      /*const u8 file =*/pop_param();
      /*const u8 channel =*/pop_param();
      push_schedule_response(command_byte, CDROM_INT3, { drive_status() });

    } break;

    case 0x0e: { // SetMode
      push_schedule_response(command_byte, CDROM_INT3, { drive_status() });

      m_mode = pop_param();
      if (m_mode != 0x80 && m_mode != 0xa0) {
        printf("cdrom: xxx possibly unsupported mode 0x%02x\n", m_mode);
        // throw std::runtime_error("asd");
      }
      printf("cdrom: setmode(0x%02x)\n", m_mode);

    } break;

    case 0x11: { // GetLocP
      printf("cdrom: GetLocP\n");
      // track,index,mm,ss,sect,amm,ass,asect
      // XXX
      push_schedule_response(
        command_byte,
        CDROM_INT3,
        {
          u8(m_read_sector.track),
          1, // index
          u8(m_read_sector.minutes),
          u8(m_read_sector.seconds),
          u8(m_read_sector.sectors),
          u8(m_read_sector.minutes), // XXX : Should be disc-rel address
          u8(m_read_sector.seconds), // XXX : Should be disc-rel address
          u8(m_read_sector.sectors), // XXX : Should be disc-rel address
        });
    } break;

    case 0x13: { // GetTN
      printf("cdrom: GetTN\n");
      // XXX : handle multi-track discs
      push_schedule_response(command_byte, CDROM_INT3, { drive_status(), 1, 1 });
    } break;

    // 14h GetTD      E track (BCD)     INT3(stat,mm,ss)       ;BCD
    case 0x14: {
      const auto &tracks = m_disc->tracks();
      u8 track_i = pop_param();
      assert(track_i < tracks.size());
      const Track &track = track_i == 0 ? tracks[tracks.size() - 1] : tracks[track_i - 1];

      u8 mm = track.start_mm_bcd();
      u8 ss = track.start_ss_bcd();
      push_response(command_byte, CDROM_INT2, { drive_status(), mm, ss });
    } break;

    case 0x15: { // SeekL
      printf("cdrom: SeekL\n");

      push_schedule_response(command_byte, CDROM_INT3, { drive_status() });
      m_read_sector = m_seek_sector;
      m_read_status = ReadStatus::Seeking;
      push_response(command_byte, CDROM_INT2, { drive_status() });
    } break;

    case 0x16: { // SeekP
      printf("cdrom: SeekP\n");

      push_schedule_response(command_byte, CDROM_INT3, { drive_status() }, 1000);
      m_read_sector = m_seek_sector;
      m_read_status = ReadStatus::Seeking;

      push_response(command_byte, CDROM_INT2, { drive_status() });
    } break;

    case 0x19: { // Test (sub_function is first parameter byte)
      switch (pop_param()) {
        case 0x20:
          //   INT3(yy,mm,dd,ver) ;Get cdrom BIOS date/version (yy,mm,dd,ver)
          // 94h,09h,19h,C0h  ;PSX (PU-7)               19 Sep 1994, version vC0 (a)
          printf("cdrom: test subfunction: version subcommand\n");
          push_schedule_response(command_byte, CDROM_INT3, { 0x94, 0x09, 0x19, 0xc0 });
          break;
        default:
          throw std::runtime_error("cdrom: unhandled cdrom test command");
      }
    } break;

      // GetId
    case 0x1a: {
      // Drive Status           1st Response   2nd Response
      // Door Open              INT5(11h,80h)  N/A
      // Spin-up                INT5(01h,80h)  N/A
      // Detect busy            INT5(03h,80h)  N/A
      // No Disk                INT3(stat)     INT5(08h,40h, 00h,00h, 00h,00h,00h,00h)
      // Audio Disk             INT3(stat)     INT5(0Ah,90h, 00h,00h, 00h,00h,00h,00h)
      // Unlicensed:Mode1       INT3(stat)     INT5(0Ah,80h, 00h,00h, 00h,00h,00h,00h)
      // Unlicensed:Mode2       INT3(stat)     INT5(0Ah,80h, 20h,00h, 00h,00h,00h,00h)
      // Unlicensed:Mode2+Audio INT3(stat)     INT5(0Ah,90h, 20h,00h, 00h,00h,00h,00h)
      // Debug/Yaroze:Mode2     INT3(stat)     INT2(02h,00h, 20h,00h, 20h,20h,20h,20h)
      // Licensed:Mode2         INT3(stat)     INT2(02h,00h, 20h,00h, 53h,43h,45h,4xh)
      // Modchip:Audio/Mode1    INT3(stat)     INT2(02h,00h, 00h,00h, 53h,43h,45h,4xh)
      if (!m_disc) {
        push_schedule_response(command_byte, CDROM_INT5, { 0x11, 0x80 });
      } else if (m_disc) {
        push_schedule_response(command_byte, CDROM_INT3, { drive_status() });
        push_response(command_byte, CDROM_INT2, { 0x02, 0, 0x20, 0, 'S', 'C', 'E', 'A' });
      } else {
        push_schedule_response(command_byte, CDROM_INT3, { drive_status() });
        push_response(command_byte, CDROM_INT5, { 0x08, 0x40, 0, 0, 0, 0, 0, 0 });
      }
    } break;

    case 0x1b: { // ReadS
      printf("cdrom: reads\n");
      m_read_status = ReadStatus::Reading;
      push_schedule_response(command_byte, CDROM_INT3, { drive_status() });

      m_subcpu_logic.cancel();
      m_console->schedule_event_nanos(SECTOR_TIME_NANOS, &m_subcpu_logic);
    } break;

    case 0x1e: { // ReadTOC
      push_schedule_response(command_byte, CDROM_INT3, { drive_status() });
      push_schedule_response(command_byte, CDROM_INT2, { drive_status() });
    } break;

    default:
      throw std::runtime_error("unhandled cdrom command");
      break;
  }

  if (!m_response_fifo.empty()) {
    printf("cdrom: response fifo << ");
    for (auto byte : m_response_fifo) {
      printf("0x%02x, ", byte);
    }
    printf("\n");
  }

  m_parameter_fifo.clear();
  m_status.param_fifo_empty = true;
  m_status.param_fifo_write_ready = true;
  m_status.adpm_fifo_empty = false;
}

void
CDROM::register_regions(fox::MemoryTable *memory)
{
  // https://problemkaputt.de/psx-spx.htm#cdromcontrollerioports
  memory->map_mmio(0x1F80'1800, 4, "CDROM Controller I/O Ports", this);
}

}
