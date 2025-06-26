#include <stdexcept>
#include <cstring>
#include <map>
#include <algorithm>

#include "shared/profiling.h"
#include "shared/log.h"
#include "core/console.h"
#include "peripherals/maple.h"

using namespace maple;

enum Registers : u32
{
  /* Maple-DMA Control Registers */
  SB_MDSTAR = 0x005f6c04,
  SB_MDTSEL = 0x005f6c10,
  SB_MDEN = 0x005f6c14,
  SB_MDST = 0x005f6c18,

  /* Maple I/F Block Control Registers */
  SB_MSYS = 0x005f6c80,
  SB_MST = 0x005f6c84,
  SB_MSHTCL = 0x005f6c88,

  /* Maple-DMA Secret Register */
  SB_MDAPRO = 0x005f6c8c,

  /* Maple I/F BLock Hardware Control Register */
  SB_MMSEL = 0x005f6ce8,

  /* Maple-DMA Debug Registers */
  SB_MTXDAD = 0x005f6cf4,
  SB_MRXDAD = 0x005f6cf8,
  SB_MTXDBD = 0x005f6cfc
};

static std::map<u32, const char *> register_map = {
  /* Maple-DMA Control Registers */
  { 0x005f6c04, "DMA Command Table Address" },
  { 0x005f6c10, "DMA Trigger Selection" },
  { 0x005f6c14, "DMA Enable" },
  { 0x005f6c18, "DMA Start / Status" },

  /* Maple I/F Block Control Registers */
  { 0x005f6c80, "Maple System Control" },
  { 0x005f6c84, "Maple Status" },
  { 0x005f6c88, "Maple Status Hard Clear" },

  /* Maple-DMA Secret Register */
  { 0x005f6c8c, "Maple Memory Region Protect" },

  /* Maple I/F BLock Hardware Control Register */
  { 0x005f6ce8, "Maple MSB Selection" },

  /* Maple-DMA Debug Registers */
  { 0x005f6cf4, "Maple TXD Address Counter" },
  { 0x005f6cf8, "Maple RXD Address Counter" },
  { 0x005f6cfc, "Maple RXD Base Address" },
};

Maple::Maple(Console *console)
  : m_console(console),
    m_memory(console->memory()),
    m_engine(new SyncFifoEngine<u32>("MAple Bus",
                                     std::bind(&Maple::engine_callback,
                                               this,
                                               std::placeholders::_1,
                                               std::placeholders::_2)))
{
  reset();
}

Maple::~Maple()
{
  return;
}

void
Maple::reset()
{
  m_MDST.store(0u);
  m_MDSTAR = 0u;

  for (unsigned i = 0u; i < 4u; ++i) {
    /*
    memset(device[i].vmu.lcd_data, 0, sizeof(device[i].vmu.lcd_data));
    */
  }
}

void
Maple::register_regions(fox::MemoryTable *const memory)
{
  memory->map_mmio(0x005f6c00, 0x100, "Maple Bus", this);
}

void
Maple::add_device(const unsigned port, std::shared_ptr<Device> device)
{
  assert(port < 4u);
  assert(!m_devices[port]); /* TODO hot-swap */
  m_devices[port] = device;
}

u32
Maple::read_u32(u32 offset)
{
  const auto it = register_map.find(offset);
  if (it != register_map.end()) {
    log.info("Read from Maple register \"%s\" (u32)", it->second);
  } else {
    log.info("Read from Maple register @0x%08x (u32)", offset);
  }

  switch (offset) {
    case SB_MDST:
      return m_MDST.load();

    default:
      return 0u;
  }
}

void
Maple::write_u32(u32 offset, u32 value)
{
  switch (offset) {
    case SB_MDST:
      if (value & 1u) {
        m_MDST.fetch_or(1u);
        m_engine->issue(offset, value);
      }
      break;

    default:
      m_engine->issue(offset, value);
      break;
  }
}

bool
Maple::read_command_file(fox::MemoryTable *const mem, u32 *address)
{
  /* DevBox page 243 */
  union MapleInstruction {
    struct {
      uint32_t transfer_length : 8;
      uint32_t pattern : 3;
      uint32_t _rsvd0 : 5;
      uint32_t port_select : 2;
      uint32_t _rsvd1 : 13;
      uint32_t end_flag : 1;
    };

    u32 raw;
  };

  /* Check that entire header is readable */
  if (!mem->check_ram(*address, 12u)) {
    log.error("Maple DMA request to non-RAM address 0x%08x", *address);
    return false;
  }

  MapleInstruction instruction;
  instruction.raw = mem->read<u32>(*address);
  if (instruction.pattern != 0) {
    /* Light gun, NOP, etc. */
    *address += 4u;
    return instruction.end_flag == 0u;
  }

  const u32 dma_target = mem->read<u32>(*address + 4u);
  Packet packet;
  mem->dma_read(&packet, *address + 8u, instruction.transfer_length * 4u + 8u);

  /* TODO Ensure transfer length is >= header size + function size */
  /* TODO locking against hot-swap */

  // printf("Received command=%u source=%02x dest=%02x\n", packet.header.command,
  // packet.header.source, packet.header.destination);
  switch (packet.header.command) {
    case RequestDeviceInfo: {
      log.debug("Maple DMA Device Information Request Port=%u", instruction.port_select);

      if (!m_devices[instruction.port_select]) {
        const u32 no_signal_value = 0xFFFFFFFFu;
        mem->dma_write(dma_target, &no_signal_value, sizeof(no_signal_value));
        break;
      }

      Header header;
      header.command = ReplyDeviceInfo;
      header.destination = packet.header.source;
      header.source = u8(instruction.port_select << 6u);

      u8 identify_buffer[255 * 4];
      const size_t identify_size = m_devices[instruction.port_select]->identify(
        &packet.header, &header, identify_buffer);
      if (!mem->check_ram(dma_target, sizeof(Header) + identify_size)) {
        /* Error! Should set error bits? */
        break;
      }
      // printf(" - Responding command=%u source=%02x dest=%02x length=%u\n",
      // header.command, header.source, header.destination, header.length);

      mem->dma_write(dma_target, &header, 4u);
      mem->dma_write(dma_target + 4u, identify_buffer, identify_size);
      break;
    }

    case SetCondition:
    case RequestCondition:
    case RequestMemoryInfo:
    case ReadBlock:
    case WriteBlock:
    case GetLastError: {
      log.debug("Maple DMA Device Send Port=%u Command=%u Function=0x%08x",
                instruction.port_select,
                packet.header.command,
                packet.function);

      if (!m_devices[instruction.port_select]) {
        const u32 no_signal_value = 0xFFFFFFFFu;
        mem->dma_write(dma_target, &no_signal_value, sizeof(no_signal_value));
        break;
      }

      /* Initialize response header */
      Packet response;
      response.header.command = NoResponse;
      response.header.destination = packet.header.source;
      response.header.source = u8(instruction.port_select << 6u);
      response.header.length = 0u;
      response.function = packet.function;

      const ssize_t payload_size =
        m_devices[instruction.port_select]->run_command(&packet, &response);
      if (payload_size < 0) {
        /* TODO For unsupported functions, do we return no signal? */
        const u32 no_signal_value = 0xFFFFFFFFu;
        mem->dma_write(dma_target, &no_signal_value, sizeof(no_signal_value));
        break;
      }
      // printf(" - Responding command=%u source=%02x dest=%02x length=%u TL:%u\n",
      // response.header.command, response.header.source, response.header.destination,
      // response.header.length, instruction.transfer_length);

      const size_t response_size = payload_size + sizeof(Header);
      if (!mem->check_ram(dma_target, response_size)) {
        /* Error! Should set error bits? */
        break;
      }

      mem->dma_write(dma_target, &response, response_size);
      break;
    }

    default:
      printf("Unimplemented Maple DMA Command=%u Address=0x%08x Port=%u DataSize=%u Function=%02x\n",
             packet.header.command,
             dma_target,
             instruction.port_select,
             instruction.transfer_length,
             packet.function);
      log.error("Unimplemented Maple DMA Command=%u Address=0x%08x Port=%u DataSize=%u",
                packet.header.command,
                dma_target,
                instruction.port_select,
                instruction.transfer_length);
  }

  *address += 12u + (instruction.transfer_length << 2u);
  return instruction.end_flag == 0u;
}

void
Maple::engine_callback(const u32 address, const u32 &value)
{
  ProfileZoneNamed("Maple DMA Reuqest");

  const auto it = register_map.find(address);
  if (it != register_map.end()) {
    log.info("Write to Maple register \"%s\" value 0x%08x", it->second, value);
  } else {
    log.info("Write to Maple register @0x%08x value 0x%08x", address, value);
  }

  switch (address) {
    case SB_MDSTAR:
      m_MDSTAR = value; /* XXX sync */
      break;

    case SB_MDST:
      /* Start DMA */
      if (!(value & 1u)) {
        break;
      }

      u32 target_address = m_MDSTAR;
      while (read_command_file(m_memory, &target_address)) {
        continue;
      }

      m_MDST.store(0u);
      m_console->interrupt_normal(Interrupts::Normal::EndOfDMA_Maple);
      break;
  }
}
