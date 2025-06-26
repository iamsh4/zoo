
#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "apu/aica_arm.h"

#include "fox/mmio_device.h"
#include "shared/fifo_engine.h"

#include "apu/audio.h"
#include "serialization/storage.h"
#include "shared/scheduler.h"

class Console;

namespace apu {

class AICA : public fox::MMIODevice, serialization::Serializer {
public:
  AICA(Console *console, Audio *output);
  ~AICA();

  void shutdown();
  void reset();
  void register_regions(fox::MemoryTable *memory) override;

  /* All access must be 32-bit */
  u8 read_u8(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 value) override;
  void write_u16(u32 addr, u16 value) override;
  void write_u32(u32 addr, u32 val) override;

  AicaArm *arm7di();

  void step_block();

  void serialize(serialization::Snapshot &) override;
  void deserialize(const serialization::Snapshot &) override;

  union CommonData {
    struct {
      /* Address 0x00702800 */
      struct {
        u16 MVOL : 4;
        u16 VER : 4;
        u16 D8 : 1; /* DAC 18 bit */
        u16 M8 : 1; /* MEM 8 MiB */
        u16 _reserved00 : 5;
        u16 MN : 1; /* Mono */
      };

      /* Address 0x00702804 */
      struct {
        u16 RBP_upper : 12;
        u16 _reserved04 : 1;
        u16 RBL : 2;
        u16 T : 1;
      };

      /* Address 0x00702808 : midi data */
      struct {
        u16 MIBUF : 8;
        u16 IE : 1; /* MIEMP */
        u16 IF : 1; /* MIFUL */
        u16 IO : 1; /* MIOVF */
        u16 OE : 1; /* MOEMP */
        u16 OF : 1; /* MOFUL */
        u16 _reserved08b : 3;
      };

      /* Address 0x0070280C */
      struct {
        u16 MOBUF : 8;
        u16 MSLC : 6;
        u16 AF : 1; /* AFSET */
        u16 _reserved0C : 1;
      };

      /* Address 0x00702810 */
      struct {
        u16 EG : 13;
        u16 SGC : 2;
        u16 LP : 1;
      };

      /* Address 0x00702814 */
      struct {
        u16 CA : 16;
      };

      /* Gap */
      struct {
        u16 _gap2816[(0x2880 - 0x2814) / sizeof(u32) - 1];
      };

      /* Address 0x00702880 */
      struct {
        u16 MRWINH : 4;
        u16 _reserved880 : 5;
        u16 DMEA_16_22 : 7;
      };

      /* Address 0x00702884 */
      struct {
        u16 _reserved884 : 2;
        u16 DMEA_2_15 : 14;
      };

      /* Address 0x00702888 */
      struct {
        u16 _reserved888 : 2;
        u16 DRGA_2_14 : 13;
        u16 GA : 1;
      };

      /* Address 0x0070288C */
      struct {
        u16 EX : 1;
        u16 _reserved88C : 1;
        u16 DLG_2_14 : 13;
        u16 DI : 1;
      };

      /* Address 0x00702890 */
      struct {
        u16 TIMA : 8;
        u16 TACTL : 3;
        u16 _reserved890 : 5;
      };

      /* Address 0x00702894 */
      struct {
        u16 TIMB : 8;
        u16 TBCTL : 3;
        u16 _reserved894 : 5;
      };

      /* Address 0x00702898 */
      struct {
        u16 TIMC : 8;
        u16 TCCTL : 3;
        u16 _reserved898 : 5;
      };

      /*
      The AICA and ARM7DI communicate interrupts via a set of "MCI" and "SCI"
      registers, which hold pending interrupts for the SH4 ("MCPU") and ARM ("SCPU")
      repsectively. MCIPD/SCPID are used to mark pending interrupts for either CPU,
      MCIEB/SCIEB mark which lines are enabled for triggering interrupts, and MCIRE/SCIRE
      are used to reset/answer/clear pending interrupts. The flow is...

      1) Updates to SCI* registers trigger the bits of L to be updated.
      2) If L indicates pending interrupt, then raise FIQ on the ARM to let it know.
      3) ARM will (on its own) read L from AICA to understand which interrupt is active.
      4) ARM will write to M to indicate that the interrupt is done processing.
      5) ARM may also write to MCIPD to trigger completion signal to SH4.

      */

      /* Address 0x0070289C */
      struct {
        u16 SCIEB : 11;
        u16 _reserved89C : 5;
      };

      /* Address 0x007028A0 */
      struct {
        u16 SCIPD : 11;
        u16 _reserved8A0 : 5;
      };

      /* Address 0x007028A4 */
      struct {
        u16 SCIRE : 11;
        u16 _reserved8A4 : 5;
      };

      /* Address 0x007028A8 */
      struct {
        u16 SCILV0 : 8;
        u16 _reserved8A8 : 5;
      };

      /* Address 0x007028AC */
      struct {
        u16 SCILV1 : 8;
        u16 _reserved8AC : 5;
      };

      /* Address 0x007028B0 */
      struct {
        u16 SCILV2 : 8;
        u16 _reserved8B0 : 5;
      };

      /* Address 0x007028B4 */
      struct {
        u16 MCIEB : 11;
        u16 _reserved8B4 : 5;
      };

      /* Address 0x007028B8 */
      struct {
        u16 MCIPD : 11;
        u16 _reserved8B8 : 5;
      };

      /* Address 0x007028BC */
      struct {
        u16 MCIRE : 11;
        u16 _reserved8BC : 5;
      };

      /* Gap */
      struct {
        u16 _gap28be[(0x2C00 - 0x28BC) / sizeof(u32) - 1];
      };

      /* Adddress 0x00702C00 */
      struct {
        u16 AR : 1; /* ARMRST : Arm reset */
        u16 _reserved2c00a : 7;
        u16 VREG : 1;
        u16 _reserved2c00b : 7;
      };

      /* Gap */
      struct {
        u16 _gap2c02[(0x2D00 - 0x2C00) / sizeof(u32) - 1];
      };

      /* Adddress 0x00702D00 */
      struct {
        u16 L : 8;
        u16 _reserved2d00 : 8;
      };

      /* Adddress 0x00702D04 */
      struct {
        u16 M : 8;
        u16 RP : 1;
        u16 _reserved2d04 : 7;
      };
    };

    /* Addressable common data registers span 0x00702800 - 0x00702D04 */
    u16 raw[(0x2D08 - 0x2800) / sizeof(u16)];
  };

  /* Supported audio encoding formats. */
  enum class AudioFormat : u32
  {
    PCM16,     /* Signed 2s complement */
    PCM8,      /* Signed 2s complement */
    Yamaha,    /* ADPCM 4-bit Yamaha format */
    LongStream /* 4-bit ADPCM */
  };

  /* Audio channel configuration */
  struct Channel {
    /* There are 128 bytes of registers. 4 byte stride but only 2 byte
     * values. Access is either 4 byte (from SH4 or ARM) or 1 byte (from
     * ARM only). */
    union Registers {
      struct {
        /* Offset 0x00 */
        struct {
          u16 SA_upper : 7;
          u16 PCMS : 2;
          u16 LP : 1;
          u16 SS : 1;
          u16 _reserved00 : 3;
          u16 KB : 1;
          u16 KX : 1;
        };

        u16 SA_lower; /* Offset 0x04 */
        u16 LSA;      /* Offset 0x08 */
        u16 LEA;      /* Offset 0x0C */

        /* Offset 0x10 */
        struct {
          u16 AR : 5;
          u16 _reserved10 : 1;
          u16 D1R : 5;
          u16 D2R : 5;
        };

        /* Offset 0x14 */
        struct {
          u16 RR : 5;
          u16 DL : 5;
          u16 KRS : 4;
          u16 LS : 1;
          u16 _reserved14 : 1;
        };

        /* Offset 0x18 */
        struct {
          u16 FNS : 10;
          u16 _reserved18a : 1;
          u16 OCT : 4;
          u16 _reserved18b : 1;
        };

        /* Offset 0x1C */
        struct {
          u16 ALFOS : 3;
          u16 ALFOWS : 2;
          u16 PLFOS : 3;
          u16 PLFOWS : 2;
          u16 LFOF : 5;
          u16 RE : 1;
        };

        /* Offset 0x20 */
        struct {
          u16 _reserved20a : 3;
          u16 ISEL : 4;
          u16 TL : 8;
          u16 _reserved20b : 1;
        };

        /* Offset 0x24 */
        struct {
          u16 DIPAN : 5;
          u16 _reserved24 : 3;
          u16 DISDL : 4;
          u16 IMXL : 4;
        };

        /* Offset 0x28 */
        struct {
          u16 Q : 5;
          u16 _reserved28 : 11;
        };

        /* Offset 0x2C */
        struct {
          u16 FLV0 : 13;
          u16 _reserved2C : 3;
        };

        /* Offset 0x30 */
        struct {
          u16 FLV1 : 13;
          u16 _reserved30 : 3;
        };

        /* Offset 0x34 */
        struct {
          u16 FLV2 : 13;
          u16 _reserved34 : 3;
        };

        /* Offset 0x38 */
        struct {
          u16 FLV3 : 13;
          u16 _reserved38 : 3;
        };

        /* Offset 0x3C */
        struct {
          u16 FLV4 : 13;
          u16 _reserved3C : 3;
        };

        /* Offset 0x40 */
        struct {
          u16 FD1R : 5;
          u16 _reserved40a : 3;
          u16 FAR : 5;
          u16 _reserved40b : 3;
        };

        /* Offset 0x44 */
        struct {
          u16 FRR : 5;
          u16 _reserved44a : 3;
          u16 FD2R : 5;
          u16 _reserved44b : 3;
        };

        /* Remaining registers are unused. */
        u16 _reserved[14];
      };
      u16 raw[32] = {
        0,
      };
    } registers;

    /* System-applied channel configuration. */
    struct Config {
      // Start address (SA)
      u32 address = 0;
      u32 loop = false;
      u32 key_on = false;
      AudioFormat format = AudioFormat::PCM16;
      // Loop start address (LSA)
      u32 loop_start = 0;
      // Loop end address (LEA)
      u32 loop_end = 0;
      u32 frequency = 0;
    } config;

    /* Configuration currently in effect. */
    struct Status {
      u32 running = false; /* XXX atomic */
      u32 address = 0;
      u32 loop = false;
      u32 loop_done = false;
      AudioFormat format = AudioFormat::PCM16;
      u32 loop_start = 0;
      u32 loop_end = 0;
      u32 frequency = 0;
      u32 position = 0;

      /* State only used by the decoder. */
      i16 step_size = 0;
      i16 history = 0;
    } status;
  };

  const CommonData &get_common_data() const
  {
    return m_common_data;
  }

  const Channel &get_channel_data(unsigned index)
  {
    assert(index < 64);
    return m_channels[index];
  }

  Audio* output() const
  {
    return m_output.get();
  }

private:
  Console *const m_console;
  AicaArm *const m_arm7di;
  std::unique_ptr<Audio> m_output;

  static const unsigned kNumChannels = 64;
  Channel m_channels[kNumChannels];
  CommonData m_common_data;
  u32 m_dsp_level_pan[kNumChannels];
  u32 m_dsp_data[(0x00704600 - 0x00703000) / sizeof(u32)];
  u64 m_sample_count;
  u64 m_trace_arm_reset_1;
  EventScheduler::Event m_sample_event;

  // TODO : Events for samples

  void channel_write(unsigned channel, u32 reg, u16 val);
  u32 channel_read(unsigned channel, u32 reg);
  void channel_start_stop();

  void common_write(u32 address, u16 value, u16 mask);
  u16 common_read(u32 address, u16 mask);

  enum class AICAInterrupts
  {
    External = 0,
    Reserved1,
    Reserved2,
    MidiInput,
    DMAEnd,
    Data,
    TimerA,
    TimerB,
    TimerC,
    MidiOutput,
    SampleInterval
  };
  static constexpr unsigned NumInterrupts = 11;

  u32 calculate_arm_L_register(u32 interrupt_num);
  void raise_interrupt(AICAInterrupts interrupt_num);
  void update_arm_interrupts();
  void update_sh4_interrupts();
  void tick_timers();

  /*!
   * @brief Buffer storing (up)sampled channel data ready for playing. The
   *        merged channel data is always converted to a 44100hz 32-bit signed
   *        sample format.
   */
  i32 m_samples[Audio::QueueSize * Audio::QueueChannels];

  /*!
   * @brief FIFO used to communicate audio events to the secondary thread that
   *        handles audio calculations / playing.
   */
  std::unique_ptr<FifoEngine<u32>> m_fifo;

  /*!
   * @brief Bitmask identifying which channels should currently be running.
   */
  uint64_t m_channel_mask = 0;

  std::atomic<bool> m_output_pending { false };

  /*!
   * @brief Create the next set of samples for passing to the audio output
   *        device and update channel positions.
   *
   * Internal bitmask identifies which channels are active.
   */
  void prepare_samples();

  /*!
   * @brief Decode AICA audio samples in wave memory into the provided buffer
   *        as S16 format at their native frequency. Returns the number of
   *        samples actually generated, which may be less than result_samples
   *        if the end of the stream was reached.
   */
  u32 decode_samples(Channel::Status *channel, i16 *result, u32 result_samples);

  void sampler_engine(u32 address, const u32 &value);
};

}
