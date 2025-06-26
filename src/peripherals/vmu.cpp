#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "peripherals/vmu.h"

namespace maple {

/* Default identification data for a Dreamcast VMU */
static const u8 vmu_identification[] = {
  0x00, 0x00, 0x00, 0x0e, 0x7E, 0x7E, 0x3F, 0x40, 0x00, 0x05, 0x10, 0x00, 0x00, 0x0F,
  0x41, 0x00, 0xFF, 0x00, 0x56, 0x69, 0x73, 0x75, 0x61, 0x6C, 0x20, 0x4D, 0x65, 0x6D,
  0x6F, 0x72, 0x79, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x50, 0x72, 0x6F, 0x64, 0x75, 0x63, 0x65, 0x64,
  0x20, 0x42, 0x79, 0x20, 0x6F, 0x72, 0x20, 0x55, 0x6E, 0x64, 0x65, 0x72, 0x20, 0x4C,
  0x69, 0x63, 0x65, 0x6E, 0x73, 0x65, 0x20, 0x46, 0x72, 0x6F, 0x6D, 0x20, 0x53, 0x45,
  0x47, 0x41, 0x20, 0x45, 0x4E, 0x54, 0x45, 0x52, 0x50, 0x52, 0x49, 0x53, 0x45, 0x53,
  0x2C, 0x4C, 0x54, 0x44, 0x2E, 0x20, 0x20, 0x20, 0x20, 0x20, 0x7C, 0x00, 0x82, 0x00
};

VMU::VMU(const std::string &filename)
  : m_lcd_pixels(new u8[LCD_WIDTH * LCD_HEIGHT]),
    m_flash(nullptr)
{
  const int vmu_fd = open(filename.c_str(), O_RDWR | O_CLOEXEC | O_CREAT, 0666);
  if (vmu_fd < 0) {
    m_flash = nullptr;
    printf("******** Could not open VMU save!\n");
    return;
  }

  /* XXX Don't reduce size if larger */
  if (ftruncate(vmu_fd, 256 * 512u) != 0) {
    printf("******** Could not resize VMU save!\n");
    close(vmu_fd);
    return;
  }

  void *const vmu_map =
    mmap(NULL, 256 * 512u, PROT_READ | PROT_WRITE, MAP_SHARED, vmu_fd, 0);
  if (vmu_map == MAP_FAILED) {
    printf("******** Could not map VMU save!\n");
    close(vmu_fd);
    return;
  }

  m_flash = (u8 *)vmu_map;
  close(vmu_fd);

  reset();
}

VMU::~VMU()
{
  if (m_flash) {
    munmap(m_flash, 256u * 512u /* XXX */);
    m_flash = nullptr;
  }
}

ssize_t
VMU::identify(const Header *const in, Header *const out, u8 *const buffer)
{
  out->length = (sizeof(vmu_identification) + 3u) / 4u;
  memcpy(buffer, vmu_identification, sizeof(vmu_identification));
  return sizeof(vmu_identification);
}

ssize_t
VMU::run_command(const Packet *const in, Packet *const out)
{
  /* TODO Move to protocol.h */
  struct ReadBlockHeader {
    u8 partition;
    u8 phase;
    u16 sector;
  };
  struct WriteBlockHeader {
    u8 partition;
    u8 phase;
    u16 sector;
  };

  switch (in->header.command) {
    case RequestMemoryInfo: {
      if (in->function != 0x02000000u) {
        return -1;
      }

      out->header.command = ReplyData;
      out->header.length = sizeof(MediaInfo) / 4u + 1u;
      memcpy(&out->data[0], &m_info, sizeof(m_info));
      return sizeof(MediaInfo) + 4u;
    }

    case ReadBlock: {
      if (in->function != 0x02000000u || m_flash == nullptr) {
        return -1;
      }

      const ReadBlockHeader *const header = (const ReadBlockHeader *)&in->data[0];
      const u16 sector = (header->sector << 8u) | (header->sector >> 8u); /* ntohs */
      if (sector >= 256) {
        return -1;
      }

      out->header.command = ReplyData;
      out->header.length = (512u + sizeof(ReadBlockHeader)) / 4u + 1u;
      memcpy(&out->data[0], header, sizeof(ReadBlockHeader));
      memcpy(&out->data[sizeof(ReadBlockHeader)], &m_flash[sector * 512u], 512u);
      return 4u + sizeof(ReadBlockHeader) + 512u;
    }

    case WriteBlock: {
      const WriteBlockHeader *const header = (const WriteBlockHeader *)&in->data[0];
      if (in->function == 0x02000000u) {
        /* VMU flash write */
        if (m_flash == nullptr) {
          return -1;
        }

        const u16 sector = (header->sector << 8u) | (header->sector >> 8u); /* ntohs */
        if (sector >= 256 || header->phase > 3u) {
          return -1;
        }

        /* XXX
         * The manual discusses phases in a way that makes it seem like they could
         * be of any size... but never specified how that size is actually set?
         * Should we base it on the DMA size? But then how does the example setup
         * with phase=3 work, where it doesn't divide evenly? Hmmmm.... For now,
         * software seems to always use 4 phase with 128 byte payloads.
         */
        const u32 byte_offset = sector * 512u + header->phase * 128u;
        memcpy(&m_flash[byte_offset], &in->data[sizeof(WriteBlockHeader)], 128u);

        out->header.command = Acknowledge;
        out->header.length = 0u;
        return 0u;
      } else if (in->function == 0x04000000u) {
        /* VMU LCD Display */
        /* TODO Use partition / sector to determine LCD number / plane */
        // const size_t data_size = in->header.length * 4u;
        size_t offset = 0lu;
        for (int y = LCD_HEIGHT - 1u; y >= 0; --y) {
          for (int x = LCD_WIDTH - 1u; x >= 0; --x) {
            const u8 byte = in->data[(y * LCD_WIDTH + x) / 8 + sizeof(*header)];
            const u8 bit = (byte >> (7 - (x % 8))) & 1u;
            m_lcd_pixels[offset++] = bit;
          }
        }

        out->header.command = Acknowledge;
        out->header.length = 0u;
        return 0u;
      }

      return -1;
    }

    case GetLastError: {
      if (in->function != 0x02000000u) {
        return -1;
      }

      /* TODO Actually check device status */
      out->header.command = Acknowledge;
      out->header.length = 0u;
      return 0u;
    }

    default: {
      return -1;
    }
  }
}

void
VMU::reset()
{
  /* Hard-coded configuration for flash memory size, single partition. */
  memset(&m_info, 0, sizeof(m_info));
  m_info.total_size = 0xff;
  m_info.partition_no = 0x00;
  m_info.system_block = 0xff;
  m_info.fat_block = 0xfe;
  m_info.fat_num_blocks = 0x01;
  m_info.info_block = 0xfd;
  m_info.info_num_blocks = 0x0d;
  m_info.icon = 0x00;
  m_info.save_block = 0xc8;
  m_info.num_blocks = 0x1f;

  /* Clear LCD screen */
  memset(m_lcd_pixels.get(), 0, LCD_WIDTH * LCD_HEIGHT);
}

}
