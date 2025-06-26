#include <fmt/core.h>
#include "shared/profiling.h"
#include "apu/audio_sdl.h"
#include "core/console.h"
#include "apu/aica.h"
#include "shared/stopwatch.h"

#if 0
#define DEBUG(args...) printf(args)
#else
#define DEBUG(args...)
#endif

namespace apu {

const u64 kSampleNanos         = 1'000'000'000llu / 44'100;
const u64 kSamplesPerStepBlock = 1;
const u64 kArm7StepsPerSample  = 64;

static Log::Logger<Log::LogModule::AUDIO> log;

AICA::AICA(Console *const console, Audio *const output)
  : m_console(console),
    m_arm7di(new AicaArm(console->memory())),
    m_output(output),
    m_sample_event(EventScheduler::Event("AICA Sample",
                                         std::bind(&AICA::step_block, this),
                                         console->scheduler())),
    m_fifo(new SyncFifoEngine<u32>("AICA Sampler",
                                   std::bind(&AICA::sampler_engine,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2)))
{
  reset();
}

AICA::~AICA()
{
  m_sample_event.cancel();
  delete m_arm7di;
}

void
AICA::shutdown()
{
  if (m_output) {
    m_output->pause();
  }
}

void
AICA::reset()
{
  log.debug("Resetting ARM and channels\n");
  for (unsigned i = 0; i < 64; ++i) {
    m_channels[i] = Channel();
  }
  m_sample_count = 0;
  memset(&m_common_data, 0, sizeof(m_common_data));
  m_common_data.SCIEB = (1 << 6);

  m_sample_event.cancel();
  m_console->schedule_event(kSampleNanos * kSamplesPerStepBlock, &m_sample_event);

  i32 gap[44100 * 2 / 10];
  memset(gap, 0, sizeof(gap));
  m_output->queue_samples(gap, sizeof(gap));

  // ARM core initially held in reset
  m_common_data.AR = 1u;
  m_arm7di->reset();
}

void
AICA::register_regions(fox::MemoryTable *const memory)
{
  memory->map_mmio(0x00700000u, 0x2000u, "AICA Channel Registers", this);
  memory->map_mmio(0x00702000u, 0x6000u, "AICA Control Registers", this);
}

u8
AICA::read_u8(const u32 address)
{
  log.verbose("read_u8 0x%08x", address);
  const u32 byte = address & 3u;
  if (byte > 1u) {
    /* 16 bit padding. */
    return 0u;
  }

  if (address >= 0x00700000u && address < 0x00702000u) {
    const u32 channel_index = ((address - 0x00700000u) / 128) & 63;
    const u16 value =
      m_channels[channel_index].registers.raw[(address & 0x7f) / sizeof(u32)];
    if (byte == 0u) {
      return value & 0xff;
    } else if (byte == 1u) {
      return (value >> 8u) & 0xff;
    }
  }

  if (address >= 0x00702800u && address <= 0x00702d07) {
    const u32 aligned = address & ~1u;
    const u16 mask    = 0xffu << (byte * 8u);
    u32 value         = common_read(aligned, mask);
    return ((value) >> (byte * 8u)) & 0xffu;
  }

  // DEBUG("AICA read 8 bits to address 0x%08x\n", address);
  return 0;
}

u32
AICA::read_u32(const u32 address)
{
  log.verbose("read_u32 0x%08x", address);
  if (address >= 0x00700000u && address < 0x00702000u) {
    const u32 channel_index = ((address - 0x00700000u) / 128u) & 63u;
    return m_channels[channel_index].registers.raw[(address & 0x7fu) / sizeof(u32)];
  }

  else if (address >= 0x00702800u && address <= 0x00702D04) {
    return common_read(address, 0xffffu);
  }

  else if (address >= 0x00702000 && address <= 0x00702044) {
    // DSP Sound level and pan
    return m_dsp_level_pan[(address - 0x00702000) / 4];
  }

  else if (address >= 0x00703000 && address < 0x00704600) {
    // DSP Data
    return m_dsp_data[(address - 0x00703000) / 4];
  }

  else {
    printf("AICA read 32 bits to address 0x%08x\n", address);
  }

  // TODO : DSP
  return 0;
}

void
AICA::write_u8(const u32 address, const u8 val)
{
  log.verbose("write_u8 0x%08x value 0x%02x", address, val);

  const u32 byte = address & 3u;
  if (byte > 1u) {
    /* Upper 16 bits are padding. */
    return;
  }

  if (address >= 0x00700000u && address < 0x00702000u) {
    /* Channel registers. Since these are 16-bit values, forward the write to the
     * u32 implementation with the preserved copy for the opposite byte. */
    const u32 channel             = (address & 0x1fff) >> 7;
    const u32 i                   = (address & 0x7f) / sizeof(u32);
    Channel::Registers &registers = m_channels[channel].registers;
    u16 result;
    if (byte == 0) {
      /* Lower byte */
      result = (registers.raw[i] & 0xff00u) | val;
    } else {
      /* Upper byte */
      result = (registers.raw[i] & 0x00ffu) | (val << 8u);
    }

    channel_write(channel, address & 0x7c, result);
    return;
  }

  if (address >= 0x00702800u && address <= 0x00702D04) {
    common_write(address & ~1u, val << (byte * 8), 0xff << (byte * 8));
    return;
  }

  // DEBUG("AICA write 8 bits to address 0x%08x value 0x%02x\n", address, val);
}

void
AICA::write_u16(const u32 address, const u16 val)
{
  log.error("write_u16 0x%08x value 0x%04x", address, val);
  throw std::runtime_error("AICA write_u16 not implemented");
}

void
AICA::write_u32(const u32 address, const u32 val)
{
  log.verbose("write_u32 0x%08x value 0x%08x", address, val);

  if (address >= 0x00700000u && address < 0x00702000u) {
    /* Channel registers */
    const u32 channel = (address & 0x1fffu) >> 7;
    const u32 reg     = address & 0x7f;
    channel_write(channel, reg, val);
    return;
  }

  else if (address >= 0x00702000 && address <= 0x00702044) {
    // Channel DSP settings
    return;
  }

  else if (address >= 0x00702800u && address <= 0x00702D04) {
    common_write(address, val, 0xffffu);
    return;
  }

  else if (address >= 0x00702000 && address < 0x00702044) {
    // DSP
    m_dsp_level_pan[(address - 0x00702000) / 4] = val;
    return;
  } else if (address >= 0x00703000 && address < 0x00704600) {
    // DSP Data
    m_dsp_data[(address - 0x00703000) / 4] = val;
    return;
  } else {
    printf("AICA write32 0x%08x < 0x%x\n", address, val);
  }
}

AicaArm *
AICA::arm7di()
{
  return m_arm7di;
}

void
AICA::sampler_engine(const u32 address, const u32 &value)
{
  /* Internal commands - not directly related to events from register
   * access. */
  switch (address) {
    case 2: /* Generate next set of samples. */
      prepare_samples();
      m_output->queue_samples(m_samples, sizeof(m_samples));
      break;
  }
}

void
AICA::step_block()
{
  u64 arm_nanos    = 0;
  u64 sample_nanos = 0;

  for (u64 i = 0; i < kSamplesPerStepBlock; ++i) {
    if (m_common_data.AR == 0u) {
      const u64 start = epoch_nanos();
      // The ARM7DI is stepped for a period required to generate 1 sample.
      for (u32 i = 0; i < kArm7StepsPerSample; ++i) {
        m_arm7di->step();
      }
      arm_nanos += epoch_nanos() - start;
    }

    m_sample_count++;
    raise_interrupt(AICAInterrupts::SampleInterval);

    tick_timers();
    update_sh4_interrupts();
    update_arm_interrupts();

    if (m_sample_count % Audio::QueueSize == 0) {
      const u64 start = epoch_nanos();
      m_fifo->issue(2, 0);
      sample_nanos += epoch_nanos() - start;
    }
  }

  m_console->metrics().increment(zoo::dreamcast::Metric::NanosARM7DI, arm_nanos);
  m_console->metrics().increment(zoo::dreamcast::Metric::NanosAICASampleGeneration,
                                 sample_nanos);
  m_console->metrics().increment(zoo::dreamcast::Metric::CountAudioSamples, kSamplesPerStepBlock);

  // Come back in (multiples of) 1/44100th of a second
  m_console->schedule_event(kSampleNanos * kSamplesPerStepBlock, &m_sample_event);
}

void
AICA::tick_timers()
{
  static const u64 increments[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

  if ((m_sample_count % increments[m_common_data.TACTL & 0b111]) == 0) {
    m_common_data.TIMA = (m_common_data.TIMA + 1) & 0xff;
    if (m_common_data.TIMA == 0) {
      log.verbose("Timer A interrupt\n");
      raise_interrupt(AICAInterrupts::TimerA);
    }
  }

  if ((m_sample_count % increments[m_common_data.TBCTL & 0b111]) == 0) {
    m_common_data.TIMB = (m_common_data.TIMB + 1) & 0xff;
    if (m_common_data.TIMB == 0) {
      log.verbose("Timer B interrupt\n");
      raise_interrupt(AICAInterrupts::TimerB);
    }
  }

  if ((m_sample_count % increments[m_common_data.TCCTL & 0b111]) == 0) {
    m_common_data.TIMC = (m_common_data.TIMC + 1) & 0xff;
    if (m_common_data.TIMC == 0) {
      log.verbose("Timer C interrupt\n");
      raise_interrupt(AICAInterrupts::TimerC);
    }
  }
}

void
AICA::channel_write(const unsigned channel_index, const u32 reg, u16 val)
{
  auto &channel = m_channels[channel_index].config;

  m_channels[channel_index].registers.raw[reg / sizeof(u32)] = val;

  switch (reg) {
    case 0x00: { /* KYONEX / KYONB / SS / LP /  PCMS / SA[22:16] */
      /* Manual says the first 7 bits of val are the upper 7 bits of SA,
       * which together can map 8MiB. But the production dreamcast only has
       * 2MiB of wave memory, so we cut it off at 5 bits. */
      channel.address = (channel.address & 0xffff) | ((val & 0x1f) << 16);
      channel.loop    = m_channels[channel_index].registers.LP;
      switch ((val >> 7) & 3) {
        case 0:
          channel.format = AudioFormat::PCM16;
          break;
        case 1:
          channel.format = AudioFormat::PCM8;
          break;
        case 2:
          channel.format = AudioFormat::Yamaha;
          break;
        case 3:
          channel.format = AudioFormat::LongStream;
          break;
      }
      channel.key_on = m_channels[channel_index].registers.KB;

      log.info(
        "Set channel details: channel=%u, address=%08x, format=%u, volume=%u, pan=%u\n",
        channel_index,
        channel.address,
        channel.format,
        m_channels[channel_index].registers.TL,
        m_channels[channel_index].registers.DIPAN);

      if (m_channels[channel_index].registers.KX) {
        /* Should reset channels / apply settings... */
        channel_start_stop();
        m_channels[channel_index].registers.KX = 0;
      }
      break;
    }

    case 0x04: /* Lower 16-bits of address */
      channel.address = (channel.address & 0x1f0000) | (val & 0xffff);
      break;

    case 0x08: /* Loop start */
      channel.loop_start = val & 0xffff;
      break;

    case 0x0C: /* End address */
      channel.loop_end = val & 0xffff;
      break;

    case 0x10: /* D2R / D1R / AR */
    case 0x14: /* LS / KRS / DL / RR */
      break;

    case 0x18: { /* OCT / FNS */
      /* Convert AICA frequency to actual frequency / sample rate */
      const u32 mantissa = (val & 0x03FF);
      uint32_t frequency;
      if (val & 0x4000) {
        /* Negative exponent - rate is below 44.1khz */
        const u32 exponent = 8 - ((val & 0x3800) >> 11);
        frequency = (44100u >> exponent) + ((44100u * mantissa) >> (10u + exponent));
      } else {
        /* Positive exponent - rate is above 44.1khz */
        const u32 exponent = (val & 0x3800) >> 11;
        frequency = (44100u << exponent) + ((44100u * mantissa) >> (10u - exponent));
      }
      channel.frequency = frequency;
      log.info(
        "Set channel frequency: channel=%u, freq=%uhz\n", channel_index, frequency);
      break;
    }

    case 0x1C: /* RE / LFOF / PLFOWS / PLFOS / ALFOWS / ALFOS */
    case 0x20: /* TL / ISEL */
    case 0x24: /* IMXL / DISDL / DIPAN */
    case 0x28: /* Q */
    case 0x2C: /* FLV0 */
    case 0x30: /* FLV1 */
    case 0x34: /* FLV2 */
    case 0x38: /* FLV3 */
    case 0x3C: /* FLV4 */
    case 0x40: /* FAR / FD1R */
    case 0x44: /* FD2R / FRR */
      break;

    default:
      /* Unsupported register */
      DEBUG(
        "Unknown channel write: channel=%u reg=%u value=%u\n", channel_index, reg, val);
      break;
  }
}

void
AICA::channel_start_stop()
{
  uint64_t out_mask = 0u;
  for (unsigned i = 0; i < 64; ++i) {
    Channel &channel   = m_channels[i];
    const bool running = channel.status.running;

    if (channel.config.key_on && running) {
      /* Already started. */
      out_mask |= (1lu << i);
      continue;
    } else if (!channel.config.key_on && !running) {
      /* Already stopped. */
      continue;
    }

    if (channel.config.key_on) {
      /* Start channel */
      channel.status.running    = true;
      channel.status.address    = channel.config.address;
      channel.status.loop       = channel.config.loop;
      channel.status.loop_done  = false;
      channel.status.format     = channel.config.format;
      channel.status.loop_start = channel.config.loop_start;
      channel.status.loop_end   = channel.config.loop_end;
      channel.status.frequency  = channel.config.frequency;
      channel.status.position   = 0;

      log.verbose("Starting channel %u\n", i);

      out_mask |= (1lu << i);
    } else {
      DEBUG("Stopping channel %u\n", i);
      channel.status.running = false;
    }
  }

  m_fifo->issue(1, out_mask);
}

void
AICA::common_write(const u32 address, const u16 value, const u16 mask)
{
  assert(address >= 0x00702800 && address <= 0x00702D04);
  const u32 index = (address - 0x00702800) / sizeof(u32);

  const bool low           = mask & 0xff;
  const bool high          = mask & 0xff00;
  m_common_data.raw[index] = (value & mask) | (m_common_data.raw[index] & ~mask);

  switch (address) {
    case 0x00702814u: {
      /* Writes not allowed */
      return;
    }

    case 0x00702800: {
      // MN, M8, D8, VER, MVOL
      break;
    }

    case 0x0070280c: {
      // MSLC, AF, MOBUF
      break;
    }

    case 0x00702804: {
      // Ring buffer config
      break;
    }

    /* TIMA */
    case 0x00702890u: {
      if (low) {
        m_common_data.TIMA = value & 0xff;
      }

      if (high) {
        m_common_data.TACTL = (value >> 8) & 0b111;
      }
      return;
    }

    /* TIMB */
    case 0x00702894: {
      if (low) {
        m_common_data.TIMB = value & 0xff;
      }

      if (high) {
        m_common_data.TBCTL = (value >> 8) & 0b111;
      }
      return;
    }

    /* TIMC */
    case 0x00702898: {
      if (low) {
        m_common_data.TIMC = value & 0xff;
      }

      if (high) {
        m_common_data.TCCTL = (value >> 8) & 0b111;
      }
      return;
    }

    /* SCIEB */
    case 0x0070289C: {
      log.debug("SCIEB write 0x%04x mask=0x%04x\n", value, mask);
      update_arm_interrupts();
      break;
    }

    /* SCIPD */
    case 0x007028A0: {
      /* The ARM or SH4 can write here, but the only valid externally-triggered interrupt
       is bit 5 (docs, pg 383). */
      log.debug("SCIPD write 0x%04x\n", value);
      const u16 data_bit = (1 << (size_t)AICAInterrupts::Data);
      if (value & mask & data_bit) {
        m_common_data.SCIPD |= data_bit;
        update_arm_interrupts();
      }
      break;
    }

    /* SCIRE */
    case 0x007028A4: {
      // Remove pending interrupts
      log.verbose("SCIRE write 0x%04x\n", value);
      m_common_data.SCIPD &= ~m_common_data.SCIRE;
      update_arm_interrupts();
      break;
    }

    /* SCILV0/1/2 */
    case 0x007028A8:
    case 0x007028AC:
    case 0x007028B0: {
      update_arm_interrupts();
      break;
    }

    /* MCIEB */
    case 0x007028B4: {
      // fmt::print("MCIEB write < 0x{:x} mask={:x}\n", value, mask);
      log.debug("MCIEB write < 0x{:x}\n", value);
      update_sh4_interrupts();
      break;
    }

    /* MCIPD */
    case 0x007028B8: {
      /* The ARM or SH4 can write here, but the only valid externally-triggered
       * interrupt is bit 5 (docs, pg 383). */
      log.debug("MCIPD write < 0x%04x\n", value);
      const u16 data_bit = 1 << (size_t)AICAInterrupts::Data;
      if (value & data_bit) {
        m_common_data.MCIPD |= data_bit;
      }
      update_sh4_interrupts();
      break;
    }

    /* MCIRE */
    case 0x007028BC: {
      /* Remove pending interrupts */
      m_common_data.MCIPD &= ~m_common_data.MCIRE;
      log.debug("MCIRE write < 0x%04x\n", value);
      m_console->system_bus()->drop_int_external(Interrupts::External::AICA);
      break;
    }

    /* RESET */
    case 0x00702C00u: {
      log.debug("ARM RESET\n", value);
      if (mask & value & 1u) {
        m_common_data.AR = 1u;
        m_arm7di->reset();
        m_trace_arm_reset_1 = m_console->current_time();
      } else {
        const u64 now = m_console->current_time();
        if (now > m_trace_arm_reset_1) {
          m_console->trace_zone("ARM Reset", TraceTrack::AICA, m_trace_arm_reset_1, now);
          m_trace_arm_reset_1 = (u64)-1;
        }
        m_common_data.AR = 0u;
      }
      break;
    }

    /* L bits. Illegal to write here. */
    case 0x00702D00: {
      assert(0);
      break;
    }

    /* "M" - IRQ CLEAR */
    case 0x00702D04: {
      log.verbose("M (IRQ CLEAR) write < 0x%04x\n", value);
      if (value & mask) {
        m_common_data.L = 0;
        update_arm_interrupts();
      }
      break;
    }

    default:
      printf(
        "Unhandled common write 0x%08x value 0x%04x mask 0x%04x\n", address, value, mask);
      break;
  }
}

u16
AICA::common_read(const u32 address, const u16 mask)
{
  const u32 index = (address - 0x00702800) / sizeof(u32);

  switch (address) {
    case 0x00702808: {
      m_common_data.IE = 1;
      m_common_data.OE = 1;
      break;
    }

    case 0x00702810u: {
      /* Reading resets the "has looped" status. */
      Channel::Status &status = m_channels[m_common_data.MSLC].status;
      m_common_data.EG        = 0x3ff; // 0u; /* TODO : Read actual AEG state (10 bits) */
      m_common_data.SGC       = 3u;    /* TODO : Read AEG state */
      m_common_data.LP        = status.loop_done ? 1u : 0u;

      // if (mask & 0x8000u) {
      /* Loop state reset only when directly read. */
      status.loop_done = false;
      // }
      break;
    }

    case 0x00702814u: {
      const Channel::Status &status = m_channels[m_common_data.MSLC].status;
      m_common_data.CA              = status.position;
      break;
    }

    default:
      break;
  }

  const u16 value = m_common_data.raw[index] & mask;
  // printf("common_read 0x%08x mslc %u mask %04x 0x%04x\n", address, m_common_data.MSLC,
  // mask, value);
  return value;
}

void
AICA::update_arm_interrupts()
{
  if (m_common_data.L == 0) {
    /* Check intersection of enabled and pending interrupts on the SCI* side. */
    const u32 pending = m_common_data.SCIEB & m_common_data.SCIPD & 0x7f9;

    if (pending) {
      for (u32 i = 0; i < NumInterrupts; ++i) {
        if ((pending & (1 << i)) != 0) {
          const u32 bit   = std::min(7u, i);
          const u32 L0    = (m_common_data.SCILV0 >> bit) & 1;
          const u32 L1    = (m_common_data.SCILV1 >> bit) & 1;
          const u32 L2    = (m_common_data.SCILV2 >> bit) & 1;
          m_common_data.L = (L2 << 2) | (L1 << 1) | L0;
        }
      }
    }
  }

  if (m_common_data.L) {
    m_arm7di->raise_fiq();
  } else {
    m_arm7di->clear_fiq();
  }
}

void
AICA::update_sh4_interrupts()
{
  const u32 pending = m_common_data.MCIPD & m_common_data.MCIEB;
  if (pending) {
    log.debug("AICA Raise SH4 interrupt\n");
    m_console->system_bus()->raise_int_external(Interrupts::External::AICA);
  } else {
    // Interrupts are only cleared via MCIRE
  }
}

void
AICA::raise_interrupt(AICAInterrupts interrupt)
{
  m_common_data.MCIPD |= (1u << (size_t)interrupt);
  m_common_data.SCIPD |= (1u << (size_t)interrupt);
}

void
AICA::serialize(serialization::Snapshot &snapshot)
{
  const unsigned expected_channel_bytes = 132;
  static_assert(sizeof(m_channels[0]) == expected_channel_bytes);
  snapshot.add_range("aica.channels", kNumChannels * sizeof(m_channels[0]), &m_channels[0]);

  const unsigned expected_common_bytes = 0x2D08 - 0x2800;
  static_assert(sizeof(m_common_data) == expected_common_bytes);
  snapshot.add_range("aica.common", sizeof(m_common_data), &m_common_data);

  static_assert(sizeof(m_sample_count) == 8);
  snapshot.add_range("aica.sample_count", sizeof(m_sample_count), &m_sample_count);

  static_assert(sizeof(m_dsp_level_pan[0]) == 4);
  snapshot.add_range("aica.dsp_level_pan", kNumChannels * sizeof(m_dsp_level_pan[0]), m_dsp_level_pan);

  const unsigned dsp_data_bytes = 0x00704600 - 0x00703000;
  static_assert(sizeof(m_dsp_data) == dsp_data_bytes);
  snapshot.add_range("aica.dsp_data", dsp_data_bytes, m_dsp_level_pan);

  m_arm7di->serialize(snapshot);
  m_sample_event.serialize(snapshot);
}

void
AICA::deserialize(const serialization::Snapshot &snapshot)
{
  snapshot.apply_all_ranges("aica.channels", &m_channels[0]);
  snapshot.apply_all_ranges("aica.common", &m_common_data);
  snapshot.apply_all_ranges("aica.sample_count", &m_sample_count);
  snapshot.apply_all_ranges("aica.dsp_level_pan", &m_dsp_level_pan);
  snapshot.apply_all_ranges("aica.dsp_data", &m_dsp_data);
  m_arm7di->deserialize(snapshot);
  m_sample_event.deserialize(snapshot);
}

void
AICA::prepare_samples()
{
  ProfileZone;

  /* Units of ~0.4db, such that bit3 represents 3db. All 0 is full volume
   * and bits subtract from this. This is encoded into 8 bits of effective
   * volume here. */
  static const i32 volume_table256[256] = {
    256, 249, 244, 238, 232, 227, 221, 216, 211, 206, 201, 196, 191, 187, 182, 178,
    174, 170, 166, 162, 158, 154, 151, 147, 143, 140, 137, 133, 130, 127, 124, 121,
    118, 116, 113, 110, 107, 105, 102, 100, 98,  95,  93,  91,  89,  86,  84,  82,
    80,  79,  77,  75,  73,  71,  70,  68,  66,  65,  63,  62,  60,  59,  57,  56,
    55,  53,  52,  51,  50,  48,  47,  46,  45,  44,  43,  42,  41,  40,  39,  38,
    37,  36,  35,  34,  34,  33,  32,  31,  31,  30,  29,  28,  28,  27,  26,  26,
    25,  24,  24,  23,  23,  22,  22,  21,  21,  20,  20,  19,  19,  18,  18,  17,
    17,  17,  16,  16,  15,  15,  15,  14,  14,  14,  13,  13,  13,  12,  12,  12,
    11,  11,  11,  11,  10,  10,  10,  10,  9,   9,   9,   9,   8,   8,   8,   8,
    8,   7,   7,   7,   7,   7,   7,   6,   6,   6,   6,   6,   6,   5,   5,   5,
    5,   5,   5,   5,   5,   4,   4,   4,   4,   4,   4,   4,   4,   4,   3,   3,
    3,   3,   3,   3,   3,   3,   3,   3,   3,   3,   2,   2,   2,   2,   2,   2,
    2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   2,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
  };

  static const i32 volume_table16[16] = { 0,  1,  1,  2,  3,  5,   8,   11,
                                          17, 25, 37, 55, 80, 118, 174, 256 };

  /* Units of 3db, starting with no reduction on either channel, and panning to
   * the right in the first half, to the left in the second half. */
  static const i32 left_volume_table[32] = {
    256, 174, 118, 80,  55,  37,  25,  17,  11,  8,   5,   3,   2,   1,   1,   0,
    256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256,
  };
  static const i32 right_volume_table[32] = { 256, 256, 256, 256, 256, 256, 256, 256,
                                              256, 256, 256, 256, 256, 256, 256, 256,
                                              256, 174, 118, 80,  55,  37,  25,  17,
                                              11,  8,   5,   3,   2,   1,   1,   0 };

  memset(m_samples, 0, sizeof(m_samples));
  const bool stereo = m_common_data.MN == 0 ? true : false;
  for (unsigned i = 0; i < 64; ++i) {
    if (!m_channels[i].status.running) {
      continue;
    }

    auto &status           = m_channels[i].status;
    const u32 frequency    = status.frequency;
    const u32 need_samples = status.frequency * Audio::QueueSize / Audio::QueueFrequency;
    const u32 source_size =
      status.loop ? (status.loop_end - status.loop_start) : status.loop_end;
    const u32 samples_remaining =
      status.position >= source_size ? 0 : source_size - status.position;
    const u32 transfer_samples =
      (status.loop ? need_samples : std::min(need_samples, samples_remaining));

    if (transfer_samples < need_samples) {
      status.running = false;
    } else {
      /*
      DEBUG("Channel %u position %u remaining %u total %u transfer %u loop %u start %u
      address %08x\n", i, status.position, samples_remaining, source_size,
      transfer_samples, status.loop, status.position % source_size, status.address);
      */
    }

    if (status.position == 0) {
      m_channels[i].status.step_size = 127;
      m_channels[i].status.history   = 0;
    }

    i16 decoded[transfer_samples];
    memset(&decoded[0], 0, sizeof(decoded)); /* XXX */
    const u32 decoded_count = decode_samples(&status, &decoded[0], transfer_samples);
    const i32 channel_volume =
      volume_table256[m_channels[i].registers.TL] * volume_table16[m_common_data.MVOL];
    const i32 left_volume =
      stereo ? left_volume_table[m_channels[i].registers.DIPAN] : 256;
    const i32 right_volume =
      stereo ? right_volume_table[m_channels[i].registers.DIPAN] : 256;

    if (frequency != Audio::QueueFrequency) {
      for (unsigned j = 0; j < Audio::QueueSize; ++j) {
        const u32 source_index = j * frequency / Audio::QueueFrequency;
        if (source_index >= decoded_count) {
          break;
        }

        /* Left and right channels */
        m_samples[j * 2 + 0] +=
          i32(decoded[source_index]) * ((channel_volume * left_volume) / 1024);
        m_samples[j * 2 + 1] +=
          i32(decoded[source_index]) * ((channel_volume * right_volume) / 1024);
      }
    } else {
      for (unsigned j = 0; j < Audio::QueueSize && j < decoded_count; ++j) {
        /* Left and right channels */
        m_samples[j * 2 + 0] += i32(decoded[j]) * ((channel_volume * left_volume) / 1024);
        m_samples[j * 2 + 1] +=
          i32(decoded[j]) * ((channel_volume * right_volume) / 1024);
      }
    }

    /* If the channel stopped, update KYONB */
    if (!status.running) {
      m_channels[i].config.key_on = false;
      m_channels[i].registers.KB  = 0;
    }
  }

  // Mix in external CDDA audio
  constexpr u32 CDDA_SECTOR_BYTES = 2352;
  static i16 cdda_sector_data[CDDA_SECTOR_BYTES / sizeof(i16)];
  static u32 cdda_sector_pos = 0;

  for (unsigned j = 0; j < Audio::QueueSize; ++j) {
    // Do we need to reload the next sector?
    if (cdda_sector_pos == 0) {
      m_console->gdrom()->get_cdda_audio_sector_data((u8 *)cdda_sector_data);
    }

    // TODO : Mix with the correct external audio levels.
    // Input data is (s16) L R L R ...
    const i32 gain_l = volume_table256[0x30] * volume_table16[15];
    const i32 gain_r = volume_table256[0x30] * volume_table16[15];
    m_samples[j * 2 + 0] += gain_l * i32(cdda_sector_data[cdda_sector_pos + 0]);
    m_samples[j * 2 + 1] += gain_r * i32(cdda_sector_data[cdda_sector_pos + 1]);

    cdda_sector_pos += 2;
    if (cdda_sector_pos == CDDA_SECTOR_BYTES / 2) {
      cdda_sector_pos = 0;
    }
  }
}

#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

static i16
adpcm_step(const u8 step, i16 *const previous, i16 *const step_size)
{
  static const i16 step_table[16] = { 230, 230, 230, 230, 307, 409, 512, 614 };
  const bool sign                 = !(step & 8);
  const i32 delta                 = step & 7;
  const i32 diff                  = ((1 + (delta << 1)) * *step_size) >> 3;
  const i32 next                  = sign ? *previous + diff : *previous - diff;
  const i32 next_step             = (step_table[delta] * *step_size) >> 8;
  *step_size                      = CLAMP(next_step, 127, 24576);
  *previous                       = CLAMP(next, -32768, 32767);
  return *previous;
}

u32
AICA::decode_samples(Channel::Status *const channel,
                     i16 *const result,
                     u32 result_samples)
{
  const u8 *const source =
    m_console->memory()->root() + ((channel->address & 0x1fffff) + 0x00800000u);
  switch (channel->format) {
    case AudioFormat::PCM16: {
      u32 i;
      const i16 *const as_i16 = (const i16 *)source;
      for (i = 0; i < result_samples; ++i) {
        result[i] = as_i16[channel->position];
        if (channel->position >= channel->loop_end) {
          channel->loop_done = true;
          if (channel->loop) {
            channel->position = channel->loop_start;
          } else {
            channel->position = channel->loop_end;
            channel->running  = false;
            break;
          }
        }
        if (channel->running) {
          ++channel->position;
        }
      }
      return i;
    }

    case AudioFormat::PCM8: {
      u32 i;
      const i8 *const as_i8 = (const i8 *)source;
      for (i = 0; i < result_samples; ++i) {
        result[i] = as_i8[channel->position] * 256;
        if (channel->position >= channel->loop_end) {
          channel->loop_done = true;
          if (channel->loop) {
            channel->position = channel->loop_start;
          } else {
            channel->running  = false;
            channel->position = channel->loop_end;
            break;
          }
        }

        if (channel->running) {
          ++channel->position;
        }
      }
      return i;
    }

    /* Yamaha and LongStream are both 4-bit ADPCM */
    case AudioFormat::Yamaha:
    case AudioFormat::LongStream: {
      u32 i;
      /* Decode only an even number of samples. */
      for (i = 0; i < (result_samples & (~1u)); i += 2) {
        const u8 adpcm   = source[channel->position / 2];
        const u8 nibbleA = adpcm & 0xf;
        const u8 nibbleB = (adpcm >> 4) & 0xf;

        channel->history = channel->history * 254 / 256; /* High pass filter */
        result[i]        = adpcm_step(nibbleA, &channel->history, &channel->step_size);

        channel->history = channel->history * 254 / 256; /* High pass filter */
        result[i + 1]    = adpcm_step(nibbleB, &channel->history, &channel->step_size);

        /* XXX Position can skip loop_end if it's odd */
        if (channel->position >= channel->loop_end) {
          channel->loop_done = true;
          if (channel->loop) {
            channel->position = channel->loop_start;
          } else {
            channel->position = channel->loop_end;
            channel->running  = false;
            break;
          }

          if (channel->format != AudioFormat::LongStream) {
            /* Long stream does not reset ADPCM state, since it's expected to
             * keep receiving new data as a ring buffer. */
            channel->step_size = 127;
            channel->history   = 0;
          }
        }

        if (channel->running) {
          channel->position += 2;
        }
      }

      /* XXX As a workaround, if the sample count wasn't even, just duplicate
       *     the last sample. */
      if (i < result_samples && (i & 1)) {
        result[i] = channel->history;
        ++i;
      }

      return i;
    }

    default:
      /* Format validated before entry - should not happen. */
      assert(false);
  }
}

}
