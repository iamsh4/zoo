#include <fmt/printf.h>

#include "core/placeholder_mmio.h"

#include "systems/ps1/console.h"
#include "systems/ps1/hw/cdrom.h"
#include "systems/ps1/hw/controllers.h"
#include "systems/ps1/hw/dma.h"
#include "systems/ps1/hw/gpu.h"
#include "systems/ps1/hw/gte.h"
#include "systems/ps1/hw/timers.h"

namespace zoo::ps1 {

const u32 NUM_REGS = 8;
struct Entry {
  u32 pc;
  u32 regs[NUM_REGS];
};

const u32 MAX_BRANCHES = 300;
std::deque<Entry> branch_history;

// master clock cycles per second (30mhz CPU)
constexpr u64 CPU_HZ = 30 * 1000 * 1000;
// nanos per master clock
constexpr u64 NANOS_PER_CPU_CYCLE = 1000 * 1000 * 1000 / CPU_HZ;

template<u32 value>
class ConstantReadMMIO : public fox::MMIODevice {
public:
  ConstantReadMMIO(const char *name, u32 start_address, u32 stop_address)
    : name(name),
      phys_start(start_address),
      phys_end(stop_address)
  {
  }

  u8 read_u8(u32 addr) override
  {
    printf("ConstantMMIO read_u8(0x%08x)\n", addr);
    return u8(value);
  }
  u16 read_u16(u32 addr) override
  {
    printf("ConstantMMIO read_u16(0x%08x)\n", addr);
    return u16(value);
  }
  u32 read_u32(u32 addr) override
  {
    printf("ConstantMMIO read_u32(0x%08x)\n", addr);
    return u32(value);
  }
  u64 read_u64(u32 addr) override
  {
    return u64(value);
  }
  void register_regions(fox::MemoryTable *memory) override
  {
    memory->map_mmio(phys_start, phys_end - phys_start, name.c_str(), this);
  }

private:
  const std::string name;
  const u32 phys_start;
  const u32 phys_end;
};

// 32-bit address space
#define MAX_VIRTUAL_ADDRESS (u64(1) << 32)

#define MAX_PHYSICAL_ADDRESS (MAX_VIRTUAL_ADDRESS >> 3)

// 2MiB of addressable Main/System RAM
#define RAM_SIZE (2 * 1024 * 1024)

using R3000 = guest::r3000::R3000;

Console::Console(Vulkan *vulkan)
  : m_mem_table(
      std::make_unique<fox::MemoryTable>(MAX_VIRTUAL_ADDRESS, MAX_PHYSICAL_ADDRESS)),
    m_cpu(std::make_unique<R3000>(m_mem_table.get()))
{
  m_mmio_registry = std::make_unique<MMIORegistry>();

  // TODO: complete this
  m_cpu->set_write_watch_callback([&](u32 addr, u32 val) { set_internal_pause(true); });

  // https://psx-spx.consoledev.net/memorymap/
  // KUSEG : 0x0000'0000 - 0x7fff'ffff | Cached | MMU
  // KSEG0 : 0x8000'0000 - 0x9fff'ffff | Cached | ...
  // KSEG1 : 0xa000'0000 - 0xbfff'ffff | ...    | ...
  // KSEG2 : 0xc000'0000 - 0xffff'ffff | Cached | MMU (Kernel mode-only)

  // - PS1 has no MMU
  // - Seems everything runs in kernel-mode

  // Internally, this is mapped to multiple regions, handled within R3000
  m_mem_table->map_sdram(0x0000'0000u, RAM_SIZE, "Main RAM");
  m_mem_table->map_sdram(0x1f80'0000u, 1024, "D-Cache Scratchpad");

  // XXX : KSEG1 interacts with a cache, which this is not aware of.
  // XXX : should move to MMIO device(s).

  std::vector<fox::MMIODevice *> mmios;
  mmios.emplace_back(new PlaceholderMMIO("MEM_CTRL", 0x1F80'1000, 0x1F80'1000 + 36));
  mmios.emplace_back(new PlaceholderMMIO("RAM_SIZE", 0x1F80'1060, 0x1F80'1060 + 4));
  mmios.emplace_back(new PlaceholderMMIO("SIO", 0x1F80'1050, 0x1F80'1060));

  m_mdec = std::make_unique<MDEC>(this);
  m_spu = std::make_unique<SPU>(this);
  m_renderer = std::make_unique<Renderer>(vulkan);
  m_gpu = std::make_unique<GPU>(this, m_renderer.get());
  m_cdrom = std::make_unique<CDROM>(this);
  m_timers = std::make_unique<Timers>(this);

  mmios.emplace_back(m_mdec.get());
  mmios.emplace_back(m_spu.get());
  mmios.emplace_back(m_timers.get());
  mmios.emplace_back(m_gpu.get());
  mmios.emplace_back(m_cdrom.get());
  mmios.emplace_back(new DMA(this));
  mmios.emplace_back(new Controllers(this));

  m_irq_control = std::make_unique<IRQControl>(this);
  mmios.emplace_back(m_irq_control.get());

  // For debug hardware only.
  mmios.emplace_back(new PlaceholderMMIO("Expansion 2", 0x1F80'2000, 0x1F80'2000 + 66));
  // Parallel port stuff (https://psx-spx.consoledev.net/iomap/)
  mmios.emplace_back(new ConstantReadMMIO<0xFFFF'FFFF>(
    "Expansion 1", 0x1F000000, 0x1F000000 + 8 * 1024 * 1024));

  for (auto &mmio : mmios) {
    mmio->register_regions(m_mem_table.get());
  }
  m_mem_table->map_file(0x1FC00000, 512 * 1024, "bios-files/SCPH1001.BIN", 0);
  m_mem_table->finalize();

  m_cpu->set_coprocessor(2, new GTE());
}

void
Console::schedule_event(u64 system_clocks, EventScheduler::Event *event)
{
  event->schedule(m_cycles_elapsed + system_clocks);
}

void
Console::schedule_event_nanos(u64 delta_nanos, EventScheduler::Event *event)
{
  const u64 delta_cycles = delta_nanos / NANOS_PER_CPU_CYCLE;
  event->schedule(m_cycles_elapsed + delta_cycles);
}

u64
Console::elapsed_nanos() const
{
  return m_cycles_elapsed * NANOS_PER_CPU_CYCLE;
}

const char *bios_functions[] = {
  "A(00h) or B(32h) FileOpen(filename,accessmode)",
  "A(01h) or B(33h) FileSeek(fd,offset,seektype)",
  "A(02h) or B(34h) FileRead(fd,dst,length)",
  "A(03h) or B(35h) FileWrite(fd,src,length)",
  "A(04h) or B(36h) FileClose(fd)",
  "A(05h) or B(37h) FileIoctl(fd,cmd,arg)",
  "A(06h) or B(38h) exit(exitcode)",
  "A(07h) or B(39h) FileGetDeviceFlag(fd)",
  "A(08h) or B(3Ah) FileGetc(fd)",
  "A(09h) or B(3Bh) FilePutc(char,fd)",
  "A(0Ah) todigit(char)",
  "A(0Bh) atof(src)     ;Does NOT work - uses (ABSENT) cop1 !!!",
  "A(0Ch) strtoul(src,src_end,base)",
  "A(0Dh) strtol(src,src_end,base)",
  "A(0Eh) abs(val)",
  "A(0Fh) labs(val)",
  "A(10h) atoi(src)",
  "A(11h) atol(src)",
  "A(12h) atob(src,num_dst)",
  "A(13h) SaveState(buf)",
  "A(14h) RestoreState(buf,param)",
  "A(15h) strcat(dst,src)",
  "A(16h) strncat(dst,src,maxlen)",
  "A(17h) strcmp(str1,str2)",
  "A(18h) strncmp(str1,str2,maxlen)",
  "A(19h) strcpy(dst,src)",
  "A(1Ah) strncpy(dst,src,maxlen)",
  "A(1Bh) strlen(src)",
  "A(1Ch) index(src,char)",
  "A(1Dh) rindex(src,char)",
  "A(1Eh) strchr(src,char)  ;exactly the same as index",
  "A(1Fh) strrchr(src,char) ;exactly the same as rindex",
  "A(20h) strpbrk(src,list)",
  "A(21h) strspn(src,list)",
  "A(22h) strcspn(src,list)",
  "A(23h) strtok(src,list)  ;use strtok(0,list) in further calls",
  "A(24h) strstr(str,substr) - buggy",
  "A(25h) toupper(char)",
  "A(26h) tolower(char)",
  "A(27h) bcopy(src,dst,len)",
  "A(28h) bzero(dst,len)",
  "A(29h) bcmp(ptr1,ptr2,len)      ;Bugged",
  "A(2Ah) memcpy(dst,src,len)",
  "A(2Bh) memset(dst,fillbyte,len)",
  "A(2Ch) memmove(dst,src,len)     ;Bugged",
  "A(2Dh) memcmp(src1,src2,len)    ;Bugged",
  "A(2Eh) memchr(src,scanbyte,len)",
  "A(2Fh) rand()",
  "A(30h) srand(seed)",
  "A(31h) qsort(base,nel,width,callback)",
  "A(32h) strtod(src,src_end) ;Does NOT work - uses (ABSENT) cop1 !!!",
  "A(33h) malloc(size)",
  "A(34h) free(buf)",
  "A(35h) lsearch(key,base,nel,width,callback)",
  "A(36h) bsearch(key,base,nel,width,callback)",
  "A(37h) calloc(sizx,sizy)            ;SLOW!",
  "A(38h) realloc(old_buf,new_siz)     ;SLOW!",
  "A(39h) InitHeap(addr,size)",
  "A(3Ah) SystemErrorExit(exitcode)",
  "A(3Bh) or B(3Ch) std_in_getchar()",
  "A(3Ch) or B(3Dh) std_out_putchar(char)",
  "A(3Dh) or B(3Eh) std_in_gets(dst)",
  "A(3Eh) or B(3Fh) std_out_puts(src)",
  "A(3Fh) printf(txt,param1,param2,etc.)",
  "A(40h) SystemErrorUnresolvedException()",
  "A(41h) LoadExeHeader(filename,headerbuf)",
  "A(42h) LoadExeFile(filename,headerbuf)",
  "A(43h) DoExecute(headerbuf,param1,param2)",
  "A(44h) FlushCache()",
  "A(45h) init_a0_b0_c0_vectors",
  "A(46h) GPU_dw(Xdst,Ydst,Xsiz,Ysiz,src)",
  "A(47h) gpu_send_dma(Xdst,Ydst,Xsiz,Ysiz,src)",
  "A(48h) SendGP1Command(gp1cmd)",
  "A(49h) GPU_cw(gp0cmd)   ;send GP0 command word",
  "A(4Ah) GPU_cwp(src,num) ;send GP0 command word and parameter words",
  "A(4Bh) send_gpu_linked_list(src)",
  "A(4Ch) gpu_abort_dma()",
  "A(4Dh) GetGPUStatus()",
  "A(4Eh) gpu_sync()",
  "A(4Fh) SystemError",
  "A(50h) SystemError",
  "A(51h) LoadAndExecute(filename,stackbase,stackoffset)",
  "A(52h) SystemError ----OR---- 'GetSysSp()' ?",
  "A(53h) SystemError           ;PS2: set_ioabort_handler(src)",
  "A(54h) or A(71h) CdInit()",
  "A(55h) or A(70h) _bu_init()",
  "A(56h) or A(72h) CdRemove()  ;does NOT work due to SysDeqIntRP bug",
  "A(57h) return 0",
  "A(58h) return 0",
  "A(59h) return 0",
  "A(5Ah) return 0",
  "A(5Bh) dev_tty_init()                                      ;PS2: SystemError",
  "A(5Ch) dev_tty_open(fcb,and unused:'path\name',accessmode) ;PS2: SystemError",
  "A(5Dh) dev_tty_in_out(fcb,cmd)                             ;PS2: SystemError",
  "A(5Eh) dev_tty_ioctl(fcb,cmd,arg)                          ;PS2: SystemError",
  "A(5Fh) dev_cd_open(fcb,'path\name',accessmode)",
  "A(60h) dev_cd_read(fcb,dst,len)",
  "A(61h) dev_cd_close(fcb)",
  "A(62h) dev_cd_firstfile(fcb,'path\name',direntry)",
  "A(63h) dev_cd_nextfile(fcb,direntry)",
  "A(64h) dev_cd_chdir(fcb,'path')",
  "A(65h) dev_card_open(fcb,'path\name',accessmode)",
  "A(66h) dev_card_read(fcb,dst,len)",
  "A(67h) dev_card_write(fcb,src,len)",
  "A(68h) dev_card_close(fcb)",
  "A(69h) dev_card_firstfile(fcb,'path\name',direntry)",
  "A(6Ah) dev_card_nextfile(fcb,direntry)",
  "A(6Bh) dev_card_erase(fcb,'p'ath\name')",
  "A(6Ch) dev_card_undelete(fcb,'path\name')",
  "A(6Dh) dev_card_format(fcb)",
  "A(6Eh) dev_card_rename(fcb1,'path\name1',fcb2,'path\name2')",
  "A(6Fh) ?   ;card ;[r4+18h]=00000000h  ;card_clear_error(fcb) or so",
  "A(70h) or A(55h) _bu_init()",
  "A(71h) or A(54h) CdInit()",
  "A(72h) or A(56h) CdRemove()   ;does NOT work due to SysDeqIntRP bug",
  "A(73h) return 0",
  "A(74h) return 0",
  "A(75h) return 0",
  "A(76h) return 0",
  "A(77h) return 0",
  "A(78h) CdAsyncSeekL(src)",
  "A(79h) return 0               ;DTL-H: Unknown?",
  "A(7Ah) return 0               ;DTL-H: Unknown?",
  "A(7Bh) return 0               ;DTL-H: Unknown?",
  "A(7Ch) CdAsyncGetStatus(dst)",
  "A(7Dh) return 0               ;DTL-H: Unknown?",
  "A(7Eh) CdAsyncReadSector(count,dst,mode)",
  "A(7Fh) return 0               ;DTL-H: Unknown?",
  "A(80h) return 0               ;DTL-H: Unknown?",
  "A(81h) CdAsyncSetMode(mode)",
  "A(82h) return 0               ;DTL-H: Unknown?",
  "A(83h) return 0               ;DTL-H: Unknown?",
  "A(84h) return 0               ;DTL-H: Unknown?",
  "A(85h) return 0               ;DTL-H: Unknown?, or reportedly, CdStop (?)",
  "A(86h) return 0               ;DTL-H: Unknown?",
  "A(87h) return 0               ;DTL-H: Unknown?",
  "A(88h) return 0               ;DTL-H: Unknown?",
  "A(89h) return 0               ;DTL-H: Unknown?",
  "A(8Ah) return 0               ;DTL-H: Unknown?",
  "A(8Bh) return 0               ;DTL-H: Unknown?",
  "A(8Ch) return 0               ;DTL-H: Unknown?",
  "A(8Dh) return 0               ;DTL-H: Unknown?",
  "A(8Eh) return 0               ;DTL-H: Unknown?",
  "A(8Fh) return 0               ;DTL-H: Unknown?",
  "A(90h) CdromIoIrqFunc1()",
  "A(91h) CdromDmaIrqFunc1()",
  "A(92h) CdromIoIrqFunc2()",
  "A(93h) CdromDmaIrqFunc2()",
  "A(94h) CdromGetInt5errCode(dst1,dst2)",
  "A(95h) CdInitSubFunc()",
  "A(96h) AddCDROMDevice()",
  "A(97h) AddMemCardDevice()     ;DTL-H: SystemError",
  "A(98h) AddDuartTtyDevice()    ;DTL-H: AddAdconsTtyDevice ;PS2: SystemError",
  "A(99h) AddDummyTtyDevice()",
  "A(9Ah) SystemError            ;DTL-H: AddMessageWindowDevice",
  "A(9Bh) SystemError            ;DTL-H: AddCdromSimDevice",
  "A(9Ch) SetConf(num_EvCB,num_TCB,stacktop)",
  "A(9Dh) GetConf(num_EvCB_dst,num_TCB_dst,stacktop_dst)",
  "A(9Eh) SetCdromIrqAutoAbort(type,flag)",
  "A(9Fh) SetMemSize(megabytes)",
};

const char *bios_functions_b[] = {

  "B(00h) alloc_kernel_memory(size)",
  "B(01h) free_kernel_memory(buf)",
  "B(02h) init_timer(t,reload,flags)",
  "B(03h) get_timer(t)",
  "B(04h) enable_timer_irq(t)",
  "B(05h) disable_timer_irq(t)",
  "B(06h) restart_timer(t)",
  "B(07h) DeliverEvent(class, spec)",
  "B(08h) OpenEvent(class,spec,mode,func)",
  "B(09h) CloseEvent(event)",
  "B(0Ah) WaitEvent(event)",
  "B(0Bh) TestEvent(event)",
  "B(0Ch) EnableEvent(event)",
  "B(0Dh) DisableEvent(event)",
  "B(0Eh) OpenThread(reg_PC,reg_SP_FP,reg_GP)",
  "B(0Fh) CloseThread(handle)",
  "B(10h) ChangeThread(handle)",
  "B(11h) jump_to_00000000h",
  "B(12h) InitPad(buf1,siz1,buf2,siz2)",
  "B(13h) StartPad()",
  "B(14h) StopPad()",
  "B(15h) OutdatedPadInitAndStart(type,button_dest,unused,unused)",
  "B(16h) OutdatedPadGetButtons()",
  "B(17h) ReturnFromException()",
  "B(18h) SetDefaultExitFromException()",
  "B(19h) SetCustomExitFromException(addr)",
  "B(1Ah) SystemError  ;PS2: return 0",
  "B(1Bh) SystemError  ;PS2: return 0",
  "B(1Ch) SystemError  ;PS2: return 0",
  "B(1Dh) SystemError  ;PS2: return 0",
  "B(1Eh) SystemError  ;PS2: return 0",
  "B(1Fh) SystemError  ;PS2: return 0",
  "B(20h) UnDeliverEvent(class,spec)",
  "B(21h) SystemError  ;PS2: return 0",
  "B(22h) SystemError  ;PS2: return 0",
  "B(23h) SystemError  ;PS2: return 0",
  "B(24h) jump_to_00000000h",
  "B(25h) jump_to_00000000h",
  "B(26h) jump_to_00000000h",
  "B(27h) jump_to_00000000h",
  "B(28h) jump_to_00000000h",
  "B(29h) jump_to_00000000h",
  "B(2Ah) SystemError  ;PS2: return 0",
  "B(2Bh) SystemError  ;PS2: return 0",
  "B(2Ch) jump_to_00000000h",
  "B(2Dh) jump_to_00000000h",
  "B(2Eh) jump_to_00000000h",
  "B(2Fh) jump_to_00000000h",
  "B(30h) jump_to_00000000h",
  "B(31h) jump_to_00000000h",
  "B(32h) or A(00h) FileOpen(filename,accessmode)",
  "B(33h) or A(01h) FileSeek(fd,offset,seektype)",
  "B(34h) or A(02h) FileRead(fd,dst,length)",
  "B(35h) or A(03h) FileWrite(fd,src,length)",
  "B(36h) or A(04h) FileClose(fd)",
  "B(37h) or A(05h) FileIoctl(fd,cmd,arg)",
  "B(38h) or A(06h) exit(exitcode)",
  "B(39h) or A(07h) FileGetDeviceFlag(fd)",
  "B(3Ah) or A(08h) FileGetc(fd)",
  "B(3Bh) or A(09h) FilePutc(char,fd)",
  "B(3Ch) or A(3Bh) std_in_getchar()",
  "B(3Dh) or A(3Ch) std_out_putchar(char)",
  "B(3Eh) or A(3Dh) std_in_gets(dst)",
  "B(3Fh) or A(3Eh) std_out_puts(src)",
  "B(40h) chdir(name)",
  "B(41h) FormatDevice(devicename)",
  "B(42h) firstfile(filename,direntry)",
  "B(43h) nextfile(direntry)",
  "B(44h) FileRename(old_filename,new_filename)",
  "B(45h) FileDelete(filename)",
  "B(46h) FileUndelete(filename)",
  "B(47h) AddDevice(device_info)  ;subfunction for AddXxxDevice functions",
  "B(48h) RemoveDevice(device_name_lowercase)",
  "B(49h) PrintInstalledDevices()",
};

void
Console::intercept_bios_calls()
{
  using namespace guest::r3000;
  const auto *regs = m_cpu->registers();

  const u32 pc = regs[Registers::PC];
  const u32 r9 = regs[Registers::R0 + 9];
  const u32 r4 = regs[Registers::R0 + 4];
  const u32 r5 = regs[Registers::R0 + 5];
  const u32 r6 = regs[Registers::R0 + 6];

  const bool is_putc = (pc == 0x00B0 && r9 == 0x3D) || (pc == 0x00A0 && r9 == 0x3C);
  if(!is_putc) {
    return;
  }

  // putc
  if (is_putc) {
    static char msg[2048];
    static u32 msg_i = 0;

    msg[msg_i++] = (char)(u8)r4;
    if (u8(r4) == '\n') {
      msg[msg_i] = 0;
      printf("bios_msg(putc): %s", msg);
      msg_i = 0;
    }
  }

  // puts
  else if ((pc == 0x00B0 && r9 == 0x3F) || (pc == 0x00A0 && r9 == 0x3E)) {
    const char *root = (char *)m_mem_table->root();
    const char *str = &root[r4 & 0x00ff'ffff];
    printf("bios_msg(puts): %s\n", str);
  } else if ((pc == 0x00A0 && r9 == 0x5F)) {
    const char *root = (char *)m_mem_table->root();
    const char *str = &root[r5 & 0x00ff'ffff];
    printf("bios: dev_cd_open(0x%08x,%s,0x%08x)\n", r4, str, r6);
  } else if ((pc == 0x00A0 && (r9 == 0x54 || r9 == 0x71))) {
    printf("bios: cdinit\n");
  } else if (pc == 0x00A0 && r9 < 0xA0) {
    printf("bios: [%s] pc=0x%08x\n", bios_functions[r9], pc);
  } else if (pc == 0x00B0 && r9 < 0x4a) {
    printf("bios: [%s] pc=0x%08x (0x%08x,0x%08x,0x%08x)\n",
           bios_functions_b[r9],
           pc,
           r4,
           r5,
           r6);
  }

  if (pc == 0xa0 && r9 == 0x17) {
    const char *root = (char *)m_mem_table->root();
    const char *a = &root[r4 & 0x001f'ffff];
    const char *b = &root[r5 & 0x001f'ffff];
    printf("bios: strcmp('%s','%s')\n", a, b);
  }

  if ((pc == 0xa0 && r9 == 0x00) || (pc == 0xb0 && r9 == 0x32)) {
    const char *root = (char *)m_mem_table->root();
    const char *a = &root[r4 & 0x001f'ffff];
    printf("bios: FileOpen('%s', %u)\n", a, r5);
    // set_internal_pause(true);
  }

  if ((pc == 0xa0 && r9 == 0x03) || (pc == 0xb0 && r9 == 0x35) || (pc == 0x80063b1c)) {
    const char *root = (char *)m_mem_table->root();
    const char *msg = &root[r5 & 0x001f'ffff];
    printf("bios: FileWrite(fd=%u, msg='%s')\n", r4, msg);
    // set_internal_pause(true);
  }
}

void
Console::step_instruction()
{
  intercept_bios_calls();

  const u32 pc = m_cpu->PC();
  if (m_cpu->has_breakpoint(pc) && pc != last_r3000_breakpoint) {
    last_r3000_breakpoint = pc;
    throw std::runtime_error("breakpoint");
  } else {
    last_r3000_breakpoint = 0xffff'ffff;
  }

  try {
    const u64 cpu_cycles = m_cpu->step_instruction();
    m_cycles_elapsed += cpu_cycles;
    m_scheduler.run_until(m_cycles_elapsed);
  } catch (...) {
    printf("BRANCH HISTORY... (oldest=top to newest=bottom)\n");
    u32 i = 0;
    while (!branch_history.empty()) {
      Entry &e = branch_history.back();
      printf(" - %i = 0x%08x : ", i++, e.pc);
      for (u32 j = 0; j < NUM_REGS; ++j)
        printf("r%i=0x%08x, ", j, e.regs[j]);
      printf("\n");
      branch_history.pop_back();
    }

    throw;
  }

  const u32 pc_after = m_cpu->PC();
  {
    Entry e;
    e.pc = pc_after;
    memcpy(
      e.regs, &m_cpu->registers()[guest::r3000::Registers::R0], sizeof(u32) * NUM_REGS);
    branch_history.push_front(e);
  }
  if (branch_history.size() > MAX_BRANCHES) {
    branch_history.pop_back();
  }
}

void
Console::set_internal_pause(bool is_set)
{
  m_internal_pause_requested = is_set;
  m_cpu->m_halted = is_set;
}

bool
Console::is_internal_pause_requested() const
{
  return m_internal_pause_requested || m_cpu->m_halted;
}

guest::r3000::R3000 *
Console::cpu()
{
  return m_cpu.get();
}

fox::MemoryTable *
Console::memory()
{
  return m_mem_table.get();
}

GPU *
Console::gpu()
{
  return m_gpu.get();
}

IRQControl *
Console::irq_control()
{
  return m_irq_control.get();
}

Renderer *
Console::renderer()
{
  return m_renderer.get();
}

MDEC *
Console::mdec()
{
  return m_mdec.get();
}

CDROM *
Console::cdrom()
{
  return m_cdrom.get();
}

SPU *
Console::spu()
{
  return m_spu.get();
}

EventScheduler *
Console::scheduler()
{
  return &m_scheduler;
}

Timers *
Console::timers()
{
  return m_timers.get();
}

MMIORegistry *
Console::mmio_registry()
{
  return m_mmio_registry.get();
}

void
Console::reset()
{
  m_cycles_elapsed = 0;
  // TODO : reset CPU/memory/etc.
}

void
Console::set_controller(u8 port, std::unique_ptr<Controller> controller)
{
  assert(port < 2);
  m_controllers[port] = std::move(controller);
}

Controller *
Console::controller(u8 port)
{
  assert(port < 2);
  return m_controllers[port].get();
}

u64
Console::elapsed_cycles() const
{
  return m_cycles_elapsed;
}

}
