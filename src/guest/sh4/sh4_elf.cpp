#include "shared/elf2.h"
#include "sh4.h"

namespace cpu {

struct prstatus_sh4_t {
  uint8_t padding[0x48];
  uint32_t R[16];
  uint32_t PC;
  uint32_t PR;
  uint32_t unknown1;
  uint32_t GBR;
  uint32_t MACH;
  uint32_t MACL;
  uint32_t unknown2;
  uint32_t unknown3;
};

struct fpregs_sh4_t {
  float FR[16];
  float XF[16];
  uint32_t FPSCR;
  uint32_t FPUL;
};

void
SH4::debug_save_core(const std::string &filename)
{
  FILE *const fp = fopen(filename.c_str(), "wb");
  if (fp == nullptr) {
    return; /* XXX */
  }

  Elf32_Ehdr hdr;
  memset(&hdr, 0, sizeof(hdr));

  hdr.e_ident[EI_MAG0] = 0x7f;
  hdr.e_ident[EI_MAG1] = 'E';
  hdr.e_ident[EI_MAG2] = 'L';
  hdr.e_ident[EI_MAG3] = 'F';
  hdr.e_ident[EI_CLASS] = ELFCLASS32;
  hdr.e_ident[EI_DATA] = ELFDATA2LSB;
  hdr.e_ident[EI_VERSION] = EV_CURRENT;
  hdr.e_ident[EI_OSABI] = ELFOSABI_OPENBSD;
  hdr.e_ident[EI_ABIVERSION] = 0;

  hdr.e_type = ET_CORE;
  hdr.e_machine = EM_SH;
  hdr.e_version = EV_CURRENT;

  hdr.e_phoff = 4096;
  hdr.e_shoff = 0;
  hdr.e_flags = EF_SH4A; /* Machine flags */
  hdr.e_ehsize = sizeof(Elf32_Ehdr);
  hdr.e_phentsize = sizeof(Elf32_Phdr);
  hdr.e_phnum = 9; /* Number of program headers */
  hdr.e_shentsize = 0;
  hdr.e_shnum = 0;
  hdr.e_shstrndx = 0; /* SHN_UNDEF */

  fwrite(&hdr, sizeof(hdr), 1, fp);

  fseek(fp, hdr.e_phoff, SEEK_SET);

  /** Notes Program Header **/
  std::vector<Note> notes;

  {
    /* PRSTATUS */
    prstatus_sh4_t prstatus;
    memset(&prstatus, 0, sizeof(prstatus));

    for (unsigned i = 0; i < 16; ++i) {
      prstatus.R[i] = gpr(i);
    }
    prstatus.PC = regs.PC;
    prstatus.PR = regs.PR;
    prstatus.GBR = regs.GBR;
    prstatus.MACH = regs.MACH;
    prstatus.MACL = regs.MACL;

    notes.push_back(Note("NT_PRSTATUS", NT_PRSTATUS));
    notes.rbegin()->set_payload((uint8_t *)&prstatus, 0xa8);
  }

  {
    /* PRPSINFO */
    Elf32_PRPSINFO prpsinfo;
    memset(&prpsinfo, 0, sizeof(prpsinfo));

    strcpy(prpsinfo.pr_fname, "dolphin");
    strcpy(prpsinfo.pr_psargs, "dolphin");

    notes.push_back(Note("NT_PRPSINFO", NT_PRPSINFO));
    notes.rbegin()->set_payload((uint8_t *)&prpsinfo, sizeof(prpsinfo));
  }

  {
    /* FPREGSET */
    fpregs_sh4_t fpregs;
    for (unsigned i = 0; i < 16; ++i) {
      fpregs.FR[i] = FPU.FR(i);
      fpregs.XF[i] = FPU.XF(i);
    }
    fpregs.FPUL = FPU.FPUL;
    fpregs.FPSCR = FPU.FPSCR.raw;

    notes.push_back(Note("NT_FPREGSET", NT_FPREGSET));
    notes.rbegin()->set_payload((uint8_t *)&fpregs, sizeof(fpregs));
  }

  {
    /* AUXV */
    uint32_t auxv[0xb8 / 4];
    memset(&auxv[0], 0, sizeof(auxv));

    /* TODO I don't understand what's supposed to go in here. */
    notes.push_back(Note("NT_AUXV", NT_AUXV));
    notes.rbegin()->set_payload((uint8_t *)&auxv[0], sizeof(auxv));
  }

  size_t total_size = 0lu;
  for (const Note &note : notes) {
    total_size += note.size();
  }

  Elf32_Phdr ph_notes;
  memset(&ph_notes, 0, sizeof(ph_notes));
  ph_notes.p_type = PT_NOTE;
  ph_notes.p_offset = 8192;
  ph_notes.p_vaddr = 0;
  ph_notes.p_paddr = 0;
  ph_notes.p_filesz = total_size;
  ph_notes.p_memsz = total_size;
  ph_notes.p_flags = 0;
  ph_notes.p_align = 0;

  fwrite(&ph_notes, sizeof(ph_notes), 1, fp);

  /** System RAM Program Header **/

  Elf32_Phdr ph_load;
  memset(&ph_load, 0, sizeof(ph_load));
  ph_load.p_type = PT_LOAD;
  ph_load.p_offset = 32768;
  ph_load.p_vaddr = 0x8C000000u;
  ph_load.p_paddr = 0x0C000000u;
  ph_load.p_filesz = 16 * 1024 * 1024;
  ph_load.p_memsz = 16 * 1024 * 1024;
  ph_load.p_flags = 0;
  ph_load.p_align = 0;


  /* Duplicate this for each virtual mapping, pointing to the same physical
   * memory location. */
  for (u64 i = 0x0C000000u; i < 0x100000000lu; i += 0x20000000u) {
    ph_load.p_vaddr = i;
    fwrite(&ph_load, sizeof(ph_load), 1, fp);
  }

  /** Program Header Payloads **/
  fseek(fp, ph_notes.p_offset, SEEK_SET);
  for (const Note &note : notes) {
    note.write(fp);
  }

  fseek(fp, ph_load.p_offset, SEEK_SET);
  fwrite(m_phys_mem->root() + 0x8C000000u, 1, ph_load.p_filesz, fp);

  fclose(fp);
}

}
