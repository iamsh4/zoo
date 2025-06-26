#include <map>
#include <cstring>
#include <algorithm>

#include "core/console.h"
#include "peripherals/gdrom.h"
#include "shared/log.h"

#include <libchdr/chd.h>

#define SPI_COMMAND_SIZE 12u

static Log::Logger<Log::LogModule::GDROM> logger;

// States in REQ_STAT status bits
#define GD_BUSY 0x00    // State transition
#define GD_PAUSE 0x01   // Pause
#define GD_STANDBY 0x02 // Standby (drive stop)
#define GD_PLAY 0x03    // CD playback
#define GD_SEEK 0x04    // Seeking
#define GD_SCAN 0x05    // Scanning
#define GD_OPEN 0x06    // Tray is open
#define GD_NODISC 0x07  // No disc
#define GD_RETRY 0x08   // Read retry in progress (option)
#define GD_ERROR 0x09   // Reading of disc TOC failed (state does not allow access)

// Every MODE 1 data sector has 16 bytes of synchronization signal which is not
// part of the user data.
static constexpr u32 kDataSectorSyncBytes = 16;

static const u8 mode_default[] = {
  "\x01\x80" /* Unused (2 bytes) */
  "\x00"     /* CD-ROM Speed */
  "\x00"     /* Unused (1 byte) */
  "\x00\xB4" /* Standby Time (2 bytes) */
  "\x19"     /* Misc Config (1 byte) */
  "\x00\x00" /* Unused (2 bytes) */
  "\x08"     /* Ready Retry Count (1 byte) */
  "SE      " /* Drive Information (8 bytes) */
  "Rev 6.43" /* System Version (8 bytes) */
  "990408  " /* System Date (8 bytes) */
};

static const u8 status_default[] = {
  "\x01"         /* STATUS (01h PAUSE) */
  "\x80"         /* Repeat Count / Disc Format (low/high) */
  "\x00"         /* Control / Address (low/high) */
  "\x00"         /* Subcode Q track number */
  "\x00"         /* Subcode Q index number */
  "\x00\x00\x00" /* Current FAD */
  "\x00"         /* Max read error retry times */
  "\x00"         /* Unused (1 byte) */
};

static const u16 security_check_response_data[506] = {
  0x0b96, 0xf045, 0xff7e, 0x063d, 0x7d4d, 0xbf10, 0x0007, 0xcf73, 0x009c, 0x0cbc, 0xaf1c,
  0x301c, 0xa7e7, 0xa803, 0x0098, 0x0fbd, 0x5bbd, 0x50aa, 0x3923, 0x1031, 0x690e, 0xe513,
  0xd200, 0x660d, 0xbf54, 0xfd5f, 0x7437, 0x5bf4, 0x0022, 0x09c6, 0xca0f, 0xe893, 0xaba4,
  0x6100, 0x2e0e, 0x4be1, 0x8b76, 0xa56a, 0xe69c, 0xc423, 0x4b00, 0x1b06, 0x0191, 0xe200,
  0xcf0d, 0x38ca, 0xb93a, 0x91e7, 0xefe5, 0x004b, 0x09d6, 0x68d3, 0xc43e, 0x2daf, 0x2a00,
  0xf90d, 0x78fc, 0xaeed, 0xb399, 0x5a32, 0x00e7, 0x0a4c, 0x9722, 0x825b, 0x7a06, 0x004c,
  0x0e42, 0x7857, 0xf546, 0xfc20, 0xcb6b, 0x5b01, 0x0086, 0x0ee4, 0x26b2, 0x71cd, 0xa5e3,
  0x0633, 0x9a8e, 0x0050, 0x0707, 0x34f5, 0xe6ef, 0x3200, 0x130f, 0x5941, 0x0f56, 0x3802,
  0x642a, 0x072a, 0x003e, 0x1152, 0x1d2a, 0x765f, 0xa066, 0x2fb2, 0xc797, 0x6e5e, 0xe252,
  0x5800, 0xca09, 0xa589, 0x0adf, 0x00de, 0x0650, 0xb849, 0x00b4, 0x0577, 0xe824, 0xbb00,
  0x910c, 0xa289, 0x628b, 0x6ade, 0x60c6, 0xe700, 0x0f0f, 0x9611, 0xd255, 0xe6bf, 0x0b48,
  0xab5c, 0x00dc, 0x0aba, 0xd730, 0x0e48, 0x6378, 0x000c, 0x0dd2, 0x8afb, 0xfea3, 0x3af8,
  0x88dd, 0x4ba9, 0xa200, 0x750a, 0x0d5d, 0x2437, 0x9dc5, 0xf700, 0x250b, 0xdbef, 0xe041,
  0x3e52, 0x004e, 0x03b7, 0xe500, 0xb911, 0x5ade, 0xcf57, 0x1ab9, 0x7ffc, 0xee26, 0xcd7b,
  0x002b, 0x084b, 0x09b8, 0x6a70, 0x009f, 0x114b, 0x158c, 0xa387, 0x4f05, 0x8e37, 0xde63,
  0x39ef, 0x4bfc, 0xab00, 0x0b10, 0xaa91, 0xe10f, 0xaee9, 0x3a69, 0x03f8, 0xd269, 0xe200,
  0xc107, 0x3d5c, 0x0082, 0x08a9, 0xc468, 0x2ead, 0x00d1, 0x0ef7, 0x47c6, 0xcdc8, 0x7c8e,
  0x5c00, 0xb995, 0x00f4, 0x04e3, 0x005b, 0x0774, 0xc765, 0x8e84, 0xc600, 0x6107, 0x4480,
  0x003f, 0x0ec8, 0x7872, 0xd347, 0x4dc2, 0xc0af, 0x1354, 0x0031, 0x0df7, 0xd848, 0x92e2,
  0x7f9f, 0x442f, 0x3368, 0x0d00, 0xab10, 0xeafe, 0x198e, 0xf881, 0x7c6f, 0xe1de, 0x06b3,
  0x4d00, 0x6611, 0x4cae, 0xb7f9, 0xee2f, 0x8eb0, 0xe17e, 0x958d, 0x006f, 0x0df4, 0x9d88,
  0xe3ca, 0xb2c4, 0xbb47, 0x69a0, 0xf300, 0x480b, 0x4117, 0xa064, 0x710e, 0x0082, 0x1e34,
  0x4d18, 0x8085, 0xa94c, 0x660b, 0x759b, 0x6113, 0x2770, 0x7a81, 0xcd02, 0xab57, 0x02df,
  0x5293, 0xdf83, 0xa848, 0x9ea6, 0x6f74, 0x0389, 0x2528, 0x9652, 0x67ff, 0xd87a, 0xb13c,
  0x462c, 0xef84, 0xc1e1, 0xc9c6, 0x96dc, 0xa9aa, 0x82c4, 0x2758, 0x7557, 0x3467, 0x3bfb,
  0xbf25, 0x3bfb, 0x13f6, 0x96ec, 0x16e5, 0xfd26, 0xdaa8, 0xc61b, 0x7f50, 0xff47, 0x5508,
  0xed08, 0x9300, 0xc49b, 0x6771, 0xa6ec, 0x16cc, 0x8720, 0x0747, 0x00a6, 0x5d79, 0xab4f,
  0x6fa1, 0x6b7a, 0xc427, 0xa3da, 0x94c3, 0x7f4f, 0xe5f3, 0x6f1b, 0xe5cc, 0xe5f0, 0xc99d,
  0xfdae, 0xac39, 0xe54c, 0x8358, 0x6525, 0x7492, 0x819e, 0xb6a0, 0x02a9, 0x079b, 0xe7b6,
  0x5779, 0x4ad9, 0xface, 0x94b4, 0xcc05, 0x3c86, 0x06dd, 0xa6cd, 0x2424, 0xc1fa, 0x48f9,
  0x0cc9, 0xc46c, 0x8296, 0xf617, 0x0931, 0xe2c4, 0xfd77, 0x46cf, 0xb218, 0x015f, 0xd16b,
  0x567b, 0x94b8, 0xe54a, 0x196c, 0xc0f0, 0x70b6, 0xf793, 0xd1d3, 0x6e2b, 0x537c, 0x856d,
  0x0cd1, 0x778b, 0x90ee, 0x15da, 0xe055, 0x0958, 0xfc56, 0x9f31, 0x46af, 0xc3cb, 0x718d,
  0xf275, 0xc32c, 0xa1bb, 0xcfc4, 0x5627, 0x9b7c, 0xaffe, 0x4e3e, 0xcdb4, 0xaa6a, 0xf3f5,
  0x22e3, 0xe182, 0x68a5, 0xdbb3, 0x9e8f, 0x7b5e, 0xf090, 0x3f79, 0x8c52, 0x8861, 0xae76,
  0x6314, 0x0f19, 0xce1d, 0x63a1, 0xb210, 0xd7e2, 0xb194, 0xcb33, 0x8528, 0x9b7d, 0xf4f5,
  0x5025, 0xdb9b, 0xa535, 0x9cb0, 0x9209, 0x31e3, 0xab40, 0xf44d, 0xe835, 0x0ab3, 0xc321,
  0x9c86, 0x29cb, 0x77a4, 0xbc57, 0xdad8, 0x82a5, 0xe880, 0x72cf, 0xad81, 0x282e, 0xd8ff,
  0xd1b6, 0x972b, 0xff00, 0x06e1, 0x3944, 0x4b1c, 0x19ab, 0x4d5b, 0x3ed6, 0x5c1b, 0xbb64,
  0x6832, 0x7cf5, 0x9ec9, 0xb4e8, 0x1b29, 0x4d7f, 0x8080, 0x8b7e, 0x0a1c, 0x9ae6, 0x49bf,
  0xc51e, 0x67b6, 0x057d, 0x90e4, 0x4b40, 0x9baf, 0xde52, 0x8017, 0x5681, 0x3aea, 0x8253,
  0x628c, 0x96fb, 0x6f97, 0x16c1, 0xd478, 0xe77b, 0x5ab9, 0xeb2a, 0x6887, 0xd333, 0x4531,
  0xfefa, 0x1cf4, 0x8690, 0x7773, 0xa9d9, 0x4ad1, 0xcf4a, 0x23ae, 0xf9db, 0xd809, 0xdc18,
  0x0d6a, 0x19e4, 0x658c, 0x64c6, 0xdcc7, 0xe3a9, 0xb191, 0xc84c, 0x9ec1, 0x7f3b, 0xa3cb,
  0xddcf, 0x1df0, 0x6e07, 0xcedc, 0xcd0d, 0x1e7e, 0x1155, 0xdf8b, 0xab3a, 0x3bb6, 0x526e,
  0xa77f, 0xd100, 0xbe33, 0x9bf2, 0x4afc, 0x9dcf, 0xc68f, 0x7bc4, 0xe7da, 0x1c2a, 0x6e26
};

enum SpiCommand : u8
{
  SPI_VERIFY_READY = 0x00,
  SPI_REQ_STAT     = 0x10,
  SPI_REQ_MODE     = 0x11,
  SPI_SET_MODE     = 0x12,
  SPI_GET_ERROR    = 0x13,
  SPI_GET_TOC      = 0x14,
  SPI_GET_SESSION  = 0x15,
  SPI_OPEN_TRAY    = 0x16,
  SPI_PLAY         = 0x20,
  SPI_SEEK         = 0x21,
  SPI_SCAN         = 0x22,
  SPI_READ         = 0x30,
  SPI_READ_POS     = 0x31,
  SPI_GET_SUBCODE  = 0x40,
  SPI_SECURITY_CHK = 0x70,
  SPI_SECURITY_RES = 0x71
};

static const std::map<u8, const char *> spi_command_names = {
  { SPI_VERIFY_READY, "Verify Access Readiness" },
  { SPI_REQ_STAT, "Get CD Status" },
  { SPI_REQ_MODE, "Get Various Settings" },
  { SPI_SET_MODE, "Make Various Settings" },
  { SPI_GET_ERROR, "Get Error Details" },
  { SPI_GET_TOC, "Get All TOC Data" },
  { SPI_GET_SESSION, "Get Specified Session Data" },
  { SPI_OPEN_TRAY, "Open Tray" },
  { SPI_PLAY, "Play CD" },
  { SPI_SEEK, "Seek For Playback Position" },
  { SPI_SCAN, "Perform Scan" },
  { SPI_READ, "Read CD" },
  { SPI_READ_POS, "CD Read (Pre-Read Position)" },
  { SPI_GET_SUBCODE, "Get Subcode" }
};

static const std::map<u32, const char *> register_names = {
  //  {  },
};

static const char *
get_ata_command_name(const u8 command)
{
  static const std::map<u8, const char *> ata_command_names = {
    { 0x00, "NOP" },
    { 0x08, "Soft Reset" },
    { 0x90, "Execute Device Diagnostic" },
    { 0xA0, "Packet Command" },
    { 0xA1, "Identify Device" },
    { 0xEF, "Set Features" }
  };

  const auto it = ata_command_names.find(command);
  if (it != ata_command_names.end()) {
    return it->second;
  } else {
    return "Unknown Command";
  }
}

// Unused
#if 0
static const char *
get_spi_packet_command_name(const u8 command)
{
  const auto it = spi_command_names.find(command);
  if(it != spi_command_names.end()) {
    return it->second;
  } else {
    return "Unknown";
  }
}
#endif

GDRom::GDRom(Console *const console)
  : m_console(console),
    m_event_bsy(
      "gdrom.clear_bsy",
      [this]() { this->GDSTATUS.bsy = 0; },
      console->scheduler())
{
  reset();
  return;
}

void
GDRom::register_regions(fox::MemoryTable *const memory)
{
  memory->map_mmio(0x5f7000u, 0x100u, "GDRom", this);
}

void
GDRom::reset()
{
  m_state              = GD_READ_COMMAND;
  m_pio_input_offset   = 0u;
  m_sector_read_count  = 0u;
  m_sector_read_offset = 0u;

  BYTECOUNT.raw = 0u;
  GDSTATUS.raw  = 0u;
  IREASON.raw   = 0u;

  GDSTATUS.dsc        = 1u;
  SECTNUM.disc_format = 0;
  SECTNUM.status      = GD_NODISC;

  m_cdda = CDDA_state {};

  memcpy(MODE, mode_default, sizeof(mode_default));
  memcpy(STATUS, status_default, sizeof(status_default));
}

void
GDRom::serialize(serialization::Snapshot &snapshot)
{
  m_event_bsy.serialize(snapshot);

  snapshot.add_range("gdrom.m_state", sizeof(m_state), &m_state);
  snapshot.add_range("gdrom.m_toc", sizeof(m_toc), &m_toc);

  snapshot.add_range("gdrom.pio_input", sizeof(m_pio_input), m_pio_input);
  snapshot.add_range(
    "gdrom.pio_input_offset", sizeof(m_pio_input_offset), &m_pio_input_offset);
  snapshot.add_range(
    "gdrom.pio_input_length", sizeof(m_pio_input_length), &m_pio_input_length);

  snapshot.add_range("gdrom.pio_output", sizeof(m_pio_output), m_pio_output);
  snapshot.add_range(
    "gdrom.pio_output_offset", sizeof(m_pio_out_offset), &m_pio_out_offset);
  snapshot.add_range(
    "gdrom.pio_output_length", sizeof(m_pio_out_length), &m_pio_out_length);

  snapshot.add_range("gdrom.dma_output", sizeof(m_dma_output), m_dma_output);
  snapshot.add_range("gdrom.dma_size", sizeof(m_dma_output_size), &m_dma_output_size);
  snapshot.add_range("gdrom.dma_offset", sizeof(m_dma_byte_offset), &m_dma_byte_offset);

  snapshot.add_range(
    "gdrom.sector_read_offset", sizeof(m_sector_read_offset), &m_sector_read_offset);
  snapshot.add_range(
    "gdrom.sector_read_count", sizeof(m_sector_read_count), &m_sector_read_count);

  snapshot.add_range("gdrom.GDSTATUS", sizeof(GDSTATUS), &GDSTATUS);
  snapshot.add_range("gdrom.IREASON", sizeof(IREASON), &IREASON);
  snapshot.add_range("gdrom.BYTECOUNT", sizeof(BYTECOUNT), &BYTECOUNT);
  snapshot.add_range("gdrom.FEATURES", sizeof(FEATURES), &FEATURES);
  snapshot.add_range("gdrom.STATUS", sizeof(STATUS), &STATUS);
  snapshot.add_range("gdrom.SECTNUM", sizeof(SECTNUM), &SECTNUM);
  snapshot.add_range("gdrom.m_cdda", sizeof(m_cdda), &m_cdda);

  snapshot.add_range("gdrom.MODE", sizeof(MODE), MODE);

  // XXX u8 *m_pio_target;
}

void
GDRom::deserialize(const serialization::Snapshot &snapshot)
{
  m_event_bsy.deserialize(snapshot);

  snapshot.apply_all_ranges("gdrom.m_state", &m_state);
  snapshot.apply_all_ranges("gdrom.m_toc", &m_toc);

  snapshot.apply_all_ranges("gdrom.pio_input", m_pio_input);
  snapshot.apply_all_ranges("gdrom.pio_input_offset", &m_pio_input_offset);
  snapshot.apply_all_ranges("gdrom.pio_input_length", &m_pio_input_length);

  snapshot.apply_all_ranges("gdrom.pio_output", m_pio_output);
  snapshot.apply_all_ranges("gdrom.pio_output_offset", &m_pio_out_offset);
  snapshot.apply_all_ranges("gdrom.pio_output_length", &m_pio_out_length);

  snapshot.apply_all_ranges("gdrom.dma_output", m_dma_output);
  snapshot.apply_all_ranges("gdrom.dma_size", &m_dma_output_size);
  snapshot.apply_all_ranges("gdrom.dma_offset", &m_dma_byte_offset);

  snapshot.apply_all_ranges("gdrom.sector_read_offset", &m_sector_read_offset);
  snapshot.apply_all_ranges("gdrom.sector_read_count", &m_sector_read_count);

  snapshot.apply_all_ranges("gdrom.GDSTATUS", &GDSTATUS);
  snapshot.apply_all_ranges("gdrom.IREASON", &IREASON);
  snapshot.apply_all_ranges("gdrom.BYTECOUNT", &BYTECOUNT);
  snapshot.apply_all_ranges("gdrom.FEATURES", &FEATURES);
  snapshot.apply_all_ranges("gdrom.STATUS", &STATUS);
  snapshot.apply_all_ranges("gdrom.SECTNUM", &SECTNUM);
  snapshot.apply_all_ranges("gdrom.m_cdda", &m_cdda);

  snapshot.apply_all_ranges("gdrom.MODE", MODE);

  // XXX u8 *m_pio_target;
}

u8
GDRom::read_u8(const u32 address)
{
  switch (address) {
    case 0x005f7018u: /* Alternate Status */
      return GDSTATUS.raw;

    case 0x005f7080u:
      logger.warn("PIO read from GD-ROM Register returning 0x%04x", 0);
      return 0u;

    case 0x005f7088u: /* Interrupt Reason */
      logger.error("Unhandled read from GD-ROM Register Interrupt Reason");
      m_console->system_bus()->drop_int_external(0u);
      return IREASON.raw;

    case 0x005f708Cu: /* REQ_STAT */
      logger.debug("Read from GD-ROM REQ_STAT");
      return SECTNUM.raw;

    case 0x005f7090: /* DRQ response byte count low */
      return BYTECOUNT.low;
      break;

    case 0x005f7094: /* DRQ response byte count hight */
      return BYTECOUNT.high;
      break;

    case 0x005f709cu: /* Status (Clear Interrupt Status) */
      m_console->system_bus()->drop_int_external(Interrupts::External::GDROM);
      return GDSTATUS.raw;

    default:
      logger.warn("Unhandled read from GD-ROM Register @0x%08x (u8)", address);
      return 0u;
  }
}

u16
GDRom::read_u16(const u32 address)
{
  switch (address) {
    case 0x005f7080u: {
      return pio_read();
    }

    default:
      logger.warn("Unhandled read from GD-ROM Register @0x%08x (u16)", address);
      return 0u;
  }
}

u32
GDRom::read_u32(const u32 address)
{
  logger.warn("Unhandled read from GD-ROM Register @0x%08x (u32)", address);
  return 0u;
}

void
GDRom::write_u8(const u32 address, const u8 value)
{
  switch (address) {
    case 0x005f7018u: /* Device Control */
      /* TODO */
      logger.info("Received unhandled GD-ROM interrupt %s",
                  (value & 2) ? "enable" : "disable");
      break;

    case 0x005f7084u: /* Features */
      FEATURES.raw = value;
      break;

    case 0x005f7088u: /* Sector Count */
      logger.info("Received GD-ROM Write Sector Count (TC=0x%02x, MV=0x%x)",
                  (value & 0xf8u) >> 3u,
                  value & 0x7u);
      break;

    case 0x005f7090: /* DRQ response byte count low */
      BYTECOUNT.low = value;
      break;

    case 0x005f7094: /* DRQ response byte count hight */
      BYTECOUNT.high = value;
      break;

    case 0x005f709Cu: /* Send Command (write) */
      logger.info("Received GD-ROM ATA Command [%s]", get_ata_command_name(value));

      // Mark busy, schedule busy clear
      GDSTATUS.bsy = 1;
      m_event_bsy.cancel();
      m_event_bsy.schedule(400);

      /* Hack - assuming PIO mode */
      if (value == 0xA0u) {
        GDSTATUS.drq  = 1u;
        GDSTATUS.drdy = 0u;
        IREASON.cod   = 1;
        IREASON.io    = 0;
      } else {
        m_console->interrupt_external(Interrupts::External::GDROM);
      }
      break;

    default:
      logger.warn(
        "Unhandled u8 write to GD-ROM Register @0x%08x <- 0x%02x", address, value);
      return;
  }
}

void
GDRom::write_u16(u32 address, u16 value)
{
  switch (address) {
    case 0x005f7080u: /* PIO Data */
      pio_write(value);
      break;

    default:
      logger.warn(
        "Unhandled u16 write to GD-ROM Register @0x%08x <- 0x%04x", address, value);
      return;
  }
  return;
}

void
GDRom::write_u32(u32 address, u32 value)
{
  switch (address) {
    default:
      logger.warn("Unhandled write to GD-ROM Register @0x%08x <- 0x%08x", address, value);
      return;
  }
}

void
GDRom::mount_disc(std::shared_ptr<zoo::media::Disc> disc)
{
  memset(&m_toc, 0xff, sizeof(m_toc));

  m_disc = disc;
  if (!m_disc) {
    return;
  }

  /* Calculate new disc's TOC */
  const auto &tracks = m_disc->tracks();
  for (unsigned i = 0; i < tracks.size(); ++i) {
    const bool is_audio_track =
      tracks[i].sector_layout.mode == zoo::media::SectorMode_Audio;

    m_toc.tracks[i].adr     = 0u;
    m_toc.tracks[i].control = is_audio_track ? 0b000 : 0b100;
    m_toc.tracks[i].fad_msb = (tracks[i].fad >> 16u) & 0xFFu;
    m_toc.tracks[i].fad     = (tracks[i].fad >> 8u) & 0xFFu;
    m_toc.tracks[i].fad_lsb = (tracks[i].fad) & 0xFFu;
  }

  m_toc.start.adr     = 0u;
  m_toc.start.control = 0u;
  m_toc.start.start   = 1u;
  m_toc.start.rsvd0   = 0u;

  m_toc.end.adr     = 0u;
  m_toc.end.control = 0u;
  m_toc.end.end     = 1u + tracks.size();
  m_toc.end.rsvd0   = 0u;

  m_toc.leadout.adr     = 0u;
  m_toc.leadout.control = 4u;
  m_toc.leadout.fad_msb = 0x08u;
  m_toc.leadout.fad     = 0x61u;
  m_toc.leadout.fad_lsb = 0xb4u;

  /* GDROM-format disc */
  SECTNUM.disc_format = 8;
  SECTNUM.status      = GD_STANDBY;
}

void
GDRom::close_drive()
{
  /* TODO */
}

void
GDRom::open_drive()
{
  /* TODO */
}

u16
GDRom::pio_read()
{
  const u32 bytes_remaining = m_pio_out_length - m_pio_out_offset;
  u16 result                = 0u;

  if (bytes_remaining >= 2u) {
    memcpy(&result, &m_pio_output[m_pio_out_offset], 2);
    BYTECOUNT.raw = bytes_remaining - 2u;
    m_pio_out_offset += 2u;
  }

  if (m_pio_out_offset == m_pio_out_length) {
    if (m_sector_read_count == 0) {
      /* Not a GD-ROM read result */
      m_pio_out_length = 0u;
      spi_done();
    } else {
      /* Reading from GD-ROM, prepare next sector */
      --m_sector_read_count;
      spi_result(2048, m_pio_output);
    }
  }

  return result;
}

void
GDRom::pio_write(const u16 value)
{
  switch (m_state) {
    case GD_READ_COMMAND: {
      m_pio_input[m_pio_input_offset + 0] = value & 0xFF;
      m_pio_input[m_pio_input_offset + 1] = (value & 0xFF00) >> 8;
      m_pio_input_offset += 2u;
      if (m_pio_input_offset == SPI_COMMAND_SIZE) {
        GDSTATUS.drq = 0;
        pio_command_exec();
      }
      break;
    }

    case GD_READ_SPI_DATA: {
      m_pio_target[0] = value & 0xFF;
      m_pio_target[1] = (value >> 8) & 0xFF;
      m_pio_target += 2;

      m_pio_input_length -= 2u;
      if (m_pio_input_length == 0u) {
        spi_done();
      }
      break;
    }

    default:
      logger.warn("PIO write not handled with state==%u", u32(m_state));
      break;
  }
}

void
GDRom::pio_command_exec()
{
  switch (m_pio_input[0]) {
    case SPI_VERIFY_READY: {
      logger.info("Command SPI_VERIFY_READY received");

      /* Command done - no data result */
      spi_done();
      m_console->interrupt_external(Interrupts::External::GDROM);
    } break;

    case SPI_REQ_STAT: {
      logger.info("Command SPI_REQ_STAT received");
      assert(m_pio_input[2] + m_pio_input[4] <= sizeof(STATUS));
      spi_result(m_pio_input[4], &STATUS[m_pio_input[2]]);
    } break;

    case SPI_REQ_MODE: {
      logger.info("Command SPI_REQ_MODE received");
      assert(m_pio_input[2] + m_pio_input[4] <= sizeof(MODE));
      spi_result(m_pio_input[4], &MODE[m_pio_input[2]]);
    } break;

    case SPI_SET_MODE: {
      logger.warn("Command SPI_SET_MODE received");
      assert(m_pio_input[2] + m_pio_input[4] <= sizeof(MODE));
      spi_input(m_pio_input[4], &MODE[m_pio_input[2]]);
    } break;

    case SPI_GET_TOC: {
      logger.warn("Command SPI_GET_TOC received");

      const u16 allocation_length = (m_pio_input[3] << 8) + m_pio_input[4];
      assert(allocation_length <= sizeof(m_toc));
      spi_result(allocation_length, (u8 *)&m_toc);
      break;
    }

    case SPI_GET_SESSION: {
      logger.warn("Command SPI_GET_SESSION received");

      static const u8 broken[6] = { 0, 0, 0, 0, 0, 0 };
      const u16 request_length  = m_pio_input[4];
      assert(request_length <= 6);

      spi_result(request_length, broken);
      break;
    }

    case SPI_GET_SUBCODE: {
      logger.warn("Command SPI_GET_SUBCODE received");

      const u16 request_length = m_pio_input[4];
      spi_result(request_length, (u8 *)security_check_response_data);
      break;
    }

    case SPI_SEEK: {
      logger.info("Command SPI_SEEK received");
      // Nothing to do here except indicate the seek completed.
      GDSTATUS.dsc = 1;
      spi_done();
      break;
    }

    case SPI_READ: {
      logger.info("Command SPI_READ received with DMA=%u", FEATURES.dma);

      if (!m_disc) {
        spi_done();
        break;
      }

      m_sector_read_offset =
        (m_pio_input[2] << 16) | (m_pio_input[3] << 8) | m_pio_input[4];
      m_sector_read_count =
        (m_pio_input[8] << 16) | (m_pio_input[9] << 8) | m_pio_input[10];
      // printf("read initiated offset=%u length=%u sectors select=%02x\n",
      //       m_sector_read_offset, m_sector_read_count, m_pio_input[1]);
      if (m_sector_read_offset == 548440) {
        m_sector_read_offset -= 7;
        m_sector_read_count += 7;
      }

      SECTNUM.status = GD_STANDBY;
      m_cdda.is_playing = false;

      if (FEATURES.dma) {
        // m_dma_output_size = m_disc->read_sector(m_sector_read_offset, {m_dma_output,
        // MAX_SECTOR_SIZE}); m_dma_output_size =
        // m_disc->read_sector_data(m_sector_read_offset, m_dma_output);

        u8 buffer[MAX_SECTOR_SIZE];
        m_disc->read_sector(m_sector_read_offset, { buffer, MAX_SECTOR_SIZE });
        memcpy(m_dma_output, buffer + 16, 2048);
        m_dma_output_size = 2048;

        assert(m_dma_output_size <= MAX_SECTOR_SIZE);

        IREASON.io    = 1;
        GDSTATUS.drdy = 1;
        IREASON.cod   = 1;

        GDSTATUS.bsy = 0;
        GDSTATUS.drq = 0;

        m_dma_byte_offset = 0u;
      } else {
        spi_result(2048, m_pio_output);
        --m_sector_read_count;
      }
      FEATURES.raw = 0;
    } break;

    case SPI_SECURITY_CHK:
      spi_done();
      break;

    case SPI_SECURITY_RES: {
      spi_result(sizeof(security_check_response_data),
                 (u8 *)security_check_response_data);
      break;
    }

    case SPI_PLAY: {
      const u8 repeats        = m_pio_input[6] & 0x0F;
      const u8 parameter_type = m_pio_input[1] & 0b111;

      // Start/End parameters come in three flavors:
      if (parameter_type == 0b001) {
        // Bytes encode the FAD format
        m_cdda.start_fad =
          (m_pio_input[2] << 16) | (m_pio_input[3] << 8) | (m_pio_input[4]);
        m_cdda.end_fad =
          (m_pio_input[8] << 16) | (m_pio_input[9] << 8) | (m_pio_input[10]);
        m_cdda.current_fad = m_cdda.start_fad;
      } else if (parameter_type == 0b010) {
        // Bytes encode the MSF (minutes, seconds, frames) format
        m_cdda.start_fad =
          (m_pio_input[2] * 60 * 75) | (m_pio_input[3] * 75) | (m_pio_input[4]);
        m_cdda.end_fad =
          (m_pio_input[8] * 60 * 75) | (m_pio_input[9] * 75) | (m_pio_input[10]);
        m_cdda.current_fad = m_cdda.start_fad;
      } else {
        // The only other valid case is to continue from the present CD play position.
        // Nothing to do in this case.
      }

      // Update our internal state.
      m_cdda.repeat_count = repeats;
      m_cdda.is_playing   = true;
      SECTNUM.status      = GD_PLAY;
      GDSTATUS.dsc        = 1; // Mark seek completed.
      spi_done();

      // TODO : Support "End point"

      printf("Playing CD track @ [fad %u -> %u], repeats=0x%x...\n",
             m_cdda.start_fad,
             m_cdda.end_fad,
             repeats);
      break;
    }

    default: {
      logger.error("Unhandled GDROM PIO command type 0x%02x", m_pio_input[0]);
      printf("Unhandled GDROM SPI command dump:\n");
      for (unsigned i = 0; i < SPI_COMMAND_SIZE; ++i) {
        printf("%02x (%u)\n", m_pio_input[i], m_pio_input[i]);
      }
      m_console->interrupt_external(Interrupts::External::GDROM);
    } break;
  }

  m_pio_input_offset = 0u;
  return;
}

void
GDRom::spi_result(u16 length, const u8 *buffer)
{
  assert(length <= MAX_PIO_OUT);

  BYTECOUNT.raw = length;
  IREASON.cod   = 0u;
  IREASON.io    = 1u;
  GDSTATUS.drq  = 1u;
  GDSTATUS.drdy = 1u;
  GDSTATUS.bsy  = 0u;

  memcpy(m_pio_output, buffer, length);
  m_pio_out_length = length;
  m_pio_out_offset = 0;

  m_state = GD_WRITE_SPI_DATA;
  m_console->interrupt_external(Interrupts::External::GDROM);
}

void
GDRom::spi_input(u16 length, u8 *buffer)
{
  BYTECOUNT.raw = length;
  IREASON.cod   = 0u;
  IREASON.io    = 1u;
  GDSTATUS.drq  = 1u;
  GDSTATUS.drdy = 1u;
  GDSTATUS.bsy  = 0u;

  m_pio_input_length = length;
  m_pio_target       = buffer;

  m_state = GD_READ_SPI_DATA;
  m_console->interrupt_external(Interrupts::External::GDROM);
}

void
GDRom::spi_done()
{
  IREASON.cod   = 1u;
  IREASON.io    = 1u;
  GDSTATUS.drdy = 1u;

  GDSTATUS.drq = 0u;
  GDSTATUS.bsy = 0u;

  m_state = GD_READ_COMMAND;
  m_console->interrupt_external(Interrupts::External::GDROM);
}

void
GDRom::trigger_dma_transfer(u32 dma_length, u8 *const dma_transfer_buffer)
{
  dma_length = std::min(dma_length, m_dma_output_size - m_dma_byte_offset);

  if (m_dma_output_size == 0u) {
    memset(dma_transfer_buffer, 0, dma_length);
    return;
  }

  memcpy(dma_transfer_buffer, &m_dma_output[m_dma_byte_offset], dma_length);
  m_dma_byte_offset += dma_length;

  if (m_dma_byte_offset >= m_dma_output_size) {
    --m_sector_read_count;
    ++m_sector_read_offset;

    if (m_sector_read_count == 0) {
      spi_done();
      m_console->interrupt_external(Interrupts::External::GDROM);
    } else {
      u8 buffer[MAX_SECTOR_SIZE];
      m_disc->read_sector(m_sector_read_offset, { buffer, MAX_SECTOR_SIZE });
      memcpy(m_dma_output, buffer + kDataSectorSyncBytes, 2048);
      m_dma_output_size = 2048;
      m_dma_byte_offset = 0u;
    }
  }
}

void
GDRom::get_cdda_audio_sector_data(u8 *destination)
{
  if (m_cdda.is_playing) {
    zoo::media::SectorReadResult result =
      m_disc->read_sector(m_cdda.current_fad, { destination, 2352 });
    assert(result.bytes_read == 2352);
    (void)result;

    m_cdda.current_fad++;

    // Did we reach the end of the playback area?
    if (m_cdda.current_fad == m_cdda.end_fad) {

      // Maybe repeat, maybe not.
      if (m_cdda.repeat_count == 0) {
        m_cdda.is_playing = false;
        SECTNUM.status    = GD_STANDBY;
      } else if (m_cdda.repeat_count > 0 && m_cdda.repeat_count < 0xF) {
        m_cdda.repeat_count--;
        m_cdda.current_fad = m_cdda.start_fad;
      } else {
        // Repeat infinitely
        m_cdda.current_fad = m_cdda.start_fad;
      }
    }
  } else {
    memset(destination, 0, CDDA_SECTOR_BYTES);
  }
}