#pragma once

#include "shared/types.h"

namespace iso {

struct __attribute__((packed)) PrimaryVolumeDescriptor {
  u8 type[1];        // 	 	Type Code 	int8 	Always 0x01 for a Primary Volume Descriptor.
  u8 standard_id[5]; // 	 	Standard Identifier 	strA 	Always 'CD001'.
  u8 version[1];     // 	 	Version 	int8 	Always 0x01.
  u8 _unused1[1];    // 	 	Unused 	- 	Always 0x00.
  u8 system_id[32];  // 	 	System Identifier 	strA 	The name of the system that can act
                     // upon sectors 0x00-0x0F for the volume.
  u8 volume_id[32];  // 	 	Volume Identifier 	strD 	Identification of this volume.
  u8 _unused2[8];    // 	 	Unused Field 	- 	All zeroes.
  u8 volume_space_size[8]; // 	 	Volume Space Size 	int32_LSB-MSB 	Number of Logical
                           // Blocks in which the volume is recorded.
  u8 _unused3[32];         // 	 	Unused Field 	- 	All zeroes.
  u8 volume_set_size[4];   // 	 	Volume Set Size 	int16_LSB-MSB 	The size of the set in
                           // this logical volume (number of disks).
  u8 volume_sequence_number[4]; // 	 	Volume Sequence Number 	int16_LSB-MSB 	The number
                                // of this disk in the Volume Set.
  u8 logical_block_size[4]; // 	 	Logical Block Size 	int16_LSB-MSB 	The size in bytes of
                            // a logical block. NB: This means that a logical block on a
                            // CD could be something other than 2 KiB!
  u8 path_table_size[8];    // 	 	Path Table Size 	int32_LSB-MSB 	The size in bytes of the
                            // path table.
  u8 lba_path_table[4]; // 	 	Location of Type-L Path Table 	int32_LSB 	LBA location of
                        // the path table. The path table pointed to contains only
                        // little-endian values.
  u8 lba_optional_path_table[4]; // 	 	Location of the Optional Type-L Path Table
                                 // int32_LSB 	LBA location of the optional path table.
                                 // The path table pointed to contains only little-endian
                                 // values. Zero means that no optional path table exists.
  u8 lba_path_table_be[4]; // 	 	Location of Type-M Path Table 	int32_MSB 	LBA location
                           // of the path table. The path table pointed to contains only
                           // big-endian values.
  u8 lba_optional_path_table_be[4]; // 	 	Location of Optional Type-M Path Table int32_MSB
                                    // LBA location of the optional path table. The path
                                    // table pointed to contains only big-endian values.
                                    // Zero means that no optional path table exists.
  u8 root_directory_entry[34]; // 	 	Directory entry for the root directory 	- 	Note
                               // that this is not an LBA address, it is the actual
                               // Directory Record, which contains a single byte Directory
                               // Identifier (0x00), hence the fixed 34 byte size.
  u8 volume_set_id[128]; // 	 	Volume Set Identifier 	strD 	Identifier of the volume set
                         // of which this volume is a member.
  u8 publisher_id[128];  // 	 	Publisher Identifier 	strA 	The volume publisher. For
                         // extended publisher information, the first byte should be 0x5F,
                         // followed by the filename of a file in the root directory. If
                         // not specified, all bytes should be 0x20.
  u8 data_preparer_id[128]; // 	 	Data Preparer Identifier 	strA 	The identifier of the
                            // person(s) who prepared the data for this volume. For
                            // extended preparation information, the first byte should be
                            // 0x5F, followed by the filename of a file in the root
                            // directory. If not specified, all bytes should be 0x20.
  u8
    application_id[128]; // 	 	Application Identifier 	strA 	Identifies how the data are
                         // recorded on this volume. For extended information, the first
                         // byte should be 0x5F, followed by the filename of a file in the
                         // root directory. If not specified, all bytes should be 0x20.
  u8
    copyright_file_id[37]; // 	 	Copyright File Identifier 	strD 	Filename of a file in
                           // the root directory that contains copyright information for
                           // this volume set. If not specified, all bytes should be 0x20.
  u8 abstract_file_id[37]; // 	 	Abstract File Identifier 	strD 	Filename of a file in
                           // the root directory that contains abstract information for
                           // this volume set. If not specified, all bytes should be 0x20.
  u8 bibliography_file_id[37];   // 	 	Bibliographic File Identifier 	strD 	Filename of a
                                 // file in the root directory that contains bibliographic
                                 // information for this volume set. If not specified, all
                                 // bytes should be 0x20.
  u8 volume_create_datetime[17]; // 	 	Volume Creation Date and Time 	dec-datetime 	The
                                 // date and time of when the volume was created.
  u8 volume_modification_datetime[17]; // 	 	Volume Modification Date and Time
                                       // dec-datetime 	The date and time of when the
                                       // volume was modified.
  u8 volume_expiration_datetime[17];   // 	 	Volume Expiration Date and Time 	dec-datetime
                                       // The date and time after which this volume is
                                     // considered to be obsolete. If not specified, then
                                     // the volume is never considered to be obsolete.
  u8
    volume_effective_datetime[17]; // 	 	Volume Effective Date and Time 	dec-datetime The
                                   // date and time after which the volume may be used. If
                                   // not specified, the volume may be used immediately.
  u8 file_structure_version[1]; // 	 	File Structure Version 	int8 	The directory records
                                // and path table version (always 0x01).
  u8 _unused4[1];               // 	 	Unused 	- 	Always 0x00.
  u8 application_specific[512]; // 	 	Application Used 	- 	Contents not defined by ISO
                                // 9660.
  u8 _reserved[653];            // 	 	Reserved 	- 	Reserved by ISO.
};
static_assert(sizeof(PrimaryVolumeDescriptor) == 2048);
static_assert(offsetof(PrimaryVolumeDescriptor,root_directory_entry) == 156, "");

struct __attribute__((packed)) __attribute__((aligned(1))) Directory {
  u8 length;
  u8 ext_attr_length;
  u8 extent_lba[8];
  u8 extent_size[8];
  u8 datetime[7];
  u8 flags;
  u8 unit_size;
  u8 gap_size;
  u8 seq_num[4];
  u8 name_len;
  char name[1];
};
static_assert(sizeof(Directory) == 34);
static_assert(offsetof(Directory,length) == 0, "");
static_assert(offsetof(Directory,extent_lba) == 2, "");
static_assert(offsetof(Directory,name_len) == 32, "");
static_assert(offsetof(Directory,name) == 33, "");

};
