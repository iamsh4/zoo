#pragma once

#include <vector>
#include <cstring>

#include "types.h"
#include "utils.h"

#define EI_NIDENT 16

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7
#define EI_ABIVERSION 8

#define ELFCLASSNONE 0
#define ELFCLASS32 1
#define ELFCLASS64 2

#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

#define EV_NONE 0
#define EV_CURRENT 1

#define ELFOSABI_NONE 0
#define ELFOSABI_HPUX 1
#define ELFOSABI_NETBSD 2
#define ELFOSABI_LINUX 3
#define ELFOSABI_SOLARIS 6
#define ELFOSABI_AIX 7
#define ELFOSABI_IRIX 8
#define ELFOSABI_FREEBSD 9
#define ELFOSABI_TRU64 10
#define ELFOSABI_MODESTO 11
#define ELFOSABI_OPENBSD 12
#define ELFOSABI_OPENVMS 13
#define ELFOSABI_NSK 14

#define ET_CORE 4
#define EM_SH 42

#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_SHLIB 5
#define PT_PHDR 6


#define NT_PRSTATUS 1
#define NT_FPREGSET 2
#define NT_PRPSINFO 3
#define NT_AUXV 6

typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;

enum E_Machine : Elf32_Half {
  EF_SH_UNKNOWN = 0,
  EF_SH1 = 1,
  EF_SH2 = 2,
  EF_SH3 = 3,
  EF_SH_DSP = 4,
  EF_SH3_DSP = 5,
  EF_SH4AL_DSP = 6,
  EF_SH3E = 8,
  EF_SH4 = 9,
  EF_SH2E = 11,
  EF_SH4A = 12,
  EF_SH2A = 13,
};

struct Elf32_Ehdr {
  unsigned char e_ident[EI_NIDENT];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

struct Elf32_Phdr {
  Elf32_Word p_type;
  Elf32_Word p_offset;
  Elf32_Word p_vaddr;
  Elf32_Word p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

struct Elf32_Nhdr {
  Elf32_Word n_namesz;
  Elf32_Word n_descsz;
  Elf32_Word n_type;
};

struct Elf32_PRPSINFO {
  u8 pr_state;
  u8 pr_sname;
  u8 pr_zomb;
  u8 pr_nice;
  u32 pr_flag; /* XXX */
  u16 pr_uid; /* XXX */
  u16 pr_gid; /* XXX */
  u32 pr_pid;
  u32 pr_ppid;
  u32 pr_pgrp;
  u32 pr_sid;
  /* Lots missing */
  char pr_fname[16];
  char pr_psargs[80];
};

struct Note {
  std::string name;
  uint32_t type;
  std::vector<uint8_t> payload;

  Note(const std::string &_name, const uint32_t _type)
    : name(_name), type(_type)
  {
    return;
  }

  void set_payload(const uint8_t *data, const size_t length)
  {
    payload.resize(length);
    memcpy(payload.data(), data, length);
  }

  size_t size() const
  {
    return sizeof(Elf32_Nhdr) +
           round_up(name.size() + 1, 4lu) +
           round_up(payload.size(), 4lu);
  }

  void write(FILE *const fp) const
  {
    Elf32_Nhdr header;
    header.n_namesz = name.size() + 1;
    header.n_descsz = payload.size();
    header.n_type = type;

    uint8_t filler[3] = { 0, 0, 0 };
    fwrite(&header, sizeof(header), 1, fp);
    fwrite(name.c_str(), 1, name.size() + 1, fp);
    fwrite(filler, 1, round_up(name.size() + 1, 4lu) - (name.size() + 1), fp);

    fwrite(payload.data(), 1, payload.size(), fp);
    fwrite(filler, 1, round_up(payload.size(), 4lu) - payload.size(), fp);
  }
};
