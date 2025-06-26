#pragma once

#include "fox/mmio_device.h"
#include "shared/log.h"
#include "shared/scheduler.h"
#include "media/disc.h"
#include "serialization/serializer.h"

class Console;

class GDRom : public fox::MMIODevice, public serialization::Serializer {
  static constexpr size_t MAX_PIO_IN        = 32lu;
  static constexpr size_t MAX_PIO_OUT       = 16384lu;
  static constexpr size_t MAX_SECTOR_SIZE   = 2352lu;
  static constexpr size_t CDDA_SECTOR_BYTES = 2352;

public:
  GDRom(Console *console);

  void register_regions(fox::MemoryTable *memory) override;
  void reset();

  void open_drive();
  void close_drive();
  void mount_gdrom(const std::string &path);
  void mount_disc(std::shared_ptr<zoo::media::Disc> disc);

  std::shared_ptr<zoo::media::Disc> get_disc()
  {
    return m_disc;
  }

  void trigger_dma_transfer(u32 dma_length, u8 *dma_transfer_buffer);

  void get_cdda_audio_sector_data(u8 *destination);

  void serialize(serialization::Snapshot &snapshot) override;
  void deserialize(const serialization::Snapshot &snapshot) override;

protected:
  u8 read_u8(u32 addr) override;
  u16 read_u16(u32 addr) override;
  u32 read_u32(u32 addr) override;

  void write_u8(u32 addr, u8 val) override;
  void write_u16(u32 addr, u16 val) override;
  void write_u32(u32 addr, u32 val) override;

private:
  Console *const m_console;

  /*!
   * @brief Event used for GDROM-BSY clearing.
   */
  EventScheduler::Event m_event_bsy;

  /*!
   * @brief Virtual disc currently in the GDROM drive. May be empty.
   */
  std::shared_ptr<zoo::media::Disc> m_disc;

  enum State
  {
    GD_READ_COMMAND,
    GD_READ_ATA_DATA,
    GD_READ_SPI_DATA,
    GD_WRITE_SPI_DATA
  };

  State m_state;

  /* Current disc's table of contents in GDROM format */
  struct toc_t {
    static constexpr size_t ENTRY_COUNT = 100;

    struct track_t {
      u8 adr : 4;
      u8 control : 4;
      u8 fad_msb;
      u8 fad;
      u8 fad_lsb;
    } tracks[ENTRY_COUNT];

    struct start_track_t {
      u8 adr : 4;
      u8 control : 4;
      u8 start;
      u16 rsvd0;
    } start;

    struct end_track_t {
      u8 adr : 4;
      u8 control : 4;
      u8 end;
      u16 rsvd0;
    } end;

    struct lead_out_t {
      u8 adr : 4;
      u8 control : 4;
      u8 fad_msb;
      u8 fad;
      u8 fad_lsb;
    } leadout;
  } m_toc;

  /* Current PIO read status */
  u8 m_pio_input[MAX_PIO_IN];
  u8 *m_pio_target;
  u32 m_pio_input_offset;
  u16 m_pio_input_length;

  /* Current PIO write status */
  u8 m_pio_output[MAX_PIO_OUT];
  u32 m_pio_out_offset;
  u32 m_pio_out_length;

  /* Current DMA write status */
  u8 m_dma_output[MAX_SECTOR_SIZE];
  u32 m_dma_output_size;
  u32 m_dma_byte_offset;

  /* Sector read status */
  u32 m_sector_read_offset;
  u32 m_sector_read_count;

  /*!
   * @brief GdStatus: GD-ROM Status Register
   *
   * Access:
   *  Read 8-bit (Writes go to Device Status register)
   */
  union GDSTATUS_bits {
    struct {
      u8 check : 1; /* TODO document */
      u8 _rsvd0 : 1;
      u8 corr : 1;
      u8 drq : 1;
      u8 dsc : 1;
      u8 df : 1;
      u8 drdy : 1;
      u8 bsy : 1;
    };

    u8 raw;
  };

  GDSTATUS_bits GDSTATUS;

  /*!
   * @brief GD-ROM interrupt reason register.
   *
   * Access:
   *   ReadOnly 8-bit (Writes go to sector count register)
   *
   *  IO  DRQ  COD  Meaning
   *  0   1    1    Ready to receive command packet
   *  1   1    1    Ready to send message from device to host
   *  1   1    0    Ready to send data to the host
   *  0   1    0    Ready to receive data from the host
   *  1   0    1    The "completed" status is in the status register
   */
  union IREASON_bits {
    struct {
      u32 cod : 1; /* Command:1 or Data:0 */
      u32 io : 1;  /* Direction C2G:0 or G2C:1 */
      u32 _rsvd0 : 30;
    };

    u32 raw;
  };

  IREASON_bits IREASON;

  /*!
   * @brief GD-ROM transfer byte count register.
   *
   * Access:
   *   ReadWrite 8-bit (Upper and lower half in separate MMIO registers)
   */
  union BYTECOUNT_bits {
    struct {
      u16 low : 8;
      u16 high : 8;
    };

    u16 raw;
  };

  BYTECOUNT_bits BYTECOUNT;

  /*!
   * @brief GD-ROM features control (DMA mode enable)
   *
   * May also be used for Set Features command, not yet implemented.
   *
   * Access:
   *   WriteOnly 8-bit (Reads go to error register)
   */
  union FEATURES_bits {
    struct {
      u32 dma : 1; /* DMA transfer mode */
      u32 _rsvd0 : 31;
    };

    u32 raw;
  };

  FEATURES_bits FEATURES;

  /*!
   * @brief Mode status
   */
  u8 MODE[64];

  /*!
   * @brief CD status
   */
  u8 STATUS[10];

  struct SECTNUM_bits {
    union {
      struct {
        u8 status : 4;
        u8 disc_format : 4;
      };
      u8 raw;
    };
  };
  SECTNUM_bits SECTNUM;

  struct CDDA_state {
    u32 current_fad = 0;
    u32 start_fad   = 0;
    u32 end_fad     = 0;
    /* 0: no repeat, 1-0xE: valid repeat counts, 0xF: Infinite repeats. */
    u8 repeat_count = 0;
    u8 is_playing = false;
  };
  CDDA_state m_cdda;

  void pio_write(u16 value);
  u16 pio_read();
  void pio_command_exec();

  void spi_result(u16 length, const u8 *buffer);
  void spi_input(u16 length, u8 *buffer);
  void spi_done();
};
