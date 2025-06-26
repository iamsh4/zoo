#pragma once

#include "shared/types.h"

namespace maple {

/*!
 * @enum maple::CommandType
 * @brief Commands that can be transferred with the Maple protocol. These can
 *        be directly placed in the command field of Maple packets.
 */
enum CommandType : i8
{
  /* Normal */
  RequestDeviceInfo = 1,
  RequestExtDeviceInfo = 2,
  ResetDevice = 3,
  ShutdownDevice = 4,
  ReplyDeviceInfo = 5,
  ReplyExtDeviceInfo = 6,
  Acknowledge = 7,
  ReplyData = 8,
  RequestCondition = 9,
  RequestMemoryInfo = 10,
  ReadBlock = 11,
  WriteBlock = 12,
  GetLastError = 13,
  SetCondition = 14,

  /* Errors */
  NoResponse = -1,
  NotSupported = -2,
  UnknownCommand = -3,
  Retry = -4,
  FileError = -5
};

/*!
 * @struct maple::Header
 * @brief Header included with every maple DMA packet.
 */
struct Header {
  CommandType command;
  u8 destination;
  u8 source;
  u8 length; /* In units of 4 bytes. */
};

/*!
 * @struct maple::Packet
 * @brief Header included with every maple DMA packet. Size is limited by the
 *        length field of the header (8 bits, 4 byte granularity).
 */
struct Packet {
  Header header;
  u32 function;
  u8 data[255 * 4];
};

/*!
 * @struct maple::MediaInfo
 * @brief Data structure used for responding to RequestMemoryInfo commands.
 */
struct MediaInfo {
  u16 total_size;
  u16 partition_no;
  u16 system_block;
  u16 fat_block;
  u16 fat_num_blocks;
  u16 info_block;
  u16 info_num_blocks;
  u16 icon;
  u16 save_block;
  u16 num_blocks;
  u16 reserved0;
  u16 reserved1;
};

}
