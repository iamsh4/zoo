#include "systems/ps1/hw/controllers.h"
#include "systems/ps1/console.h"

namespace zoo::ps1 {
Controllers::Controllers(Console *console)
  : m_console(console),
    m_irq_event("controllers.irq",
                std::bind(&Controllers::irq_event, this),
                console->scheduler())
{
}

u8
Controllers::read_u8(u32 addr)
{
  printf("controllers: read_u8(0x%08x)\n", addr);
  switch (addr) {
    case 0x1f80'1040: {
      u8 ret = m_data;
      m_data = 0xff;
      m_rx_pending = false;
      return ret;
    }
    default:
      throw std::runtime_error("Unhandled Controllers read_u8");
  }
}

u16
Controllers::read_u16(u32 addr)
{
  printf("controllers: read_u16(0x%08x)\n", addr);
  switch (addr) {
    case 0x1f80'1044: {
      u16 val = 0;
      val |= (m_ack << 7);
      val |= (1 << 2); // TX_READY flag 2;
      val |= (m_rx_pending << 1);
      val |= (1 << 0); // TX_READY flag 21
      val |= (m_irq << 9);
      m_ack = false;
      return val;
    }
    case 0x1f80'104a:
      return m_joy_ctrl;

    case 0x1f80'104e:
      return m_joy_baud;

    default:
      throw std::runtime_error("Unhandled Controllers read_u16");
  }
}

u32
Controllers::read_u32(u32 addr)
{
  printf("controllers: read_u32(0x%08x)\n", addr);
  throw std::runtime_error("Unhandled Controllers read_u32");
}

void
Controllers::handle_input(u8 value)
{
  m_rx_pending = 1;

  if (m_current_device == CurrentDevice::None) {
    if (value == 0x01)
      m_current_device = CurrentDevice::Controller;
    else if (value == 0x81)
      m_current_device = CurrentDevice::MemoryCard;
  }

  const u8 port = (m_joy_ctrl >> 13) & 1;
  Controller *controller = m_console->controller(port);

  if (m_current_device == CurrentDevice::Controller && controller) {
    m_data = controller->handle_data(value);
    m_ack = controller->ack();
    if (m_ack) {
      m_irq_event.cancel();
      m_console->schedule_event(5, &m_irq_event);
    } else {
      m_current_device = CurrentDevice::None;
    }
  } else {
    m_data = 0xff;
    m_ack = 0;
    // printf("controller: no controller plugged in\n");
  }
}

void
Controllers::write_u8(u32 addr, u8 value)
{
  printf("controllers: write_u8(0x%08x) < %x\n", addr, value);
  switch (addr) {
    case 0x1f80'1040:
      handle_input(value);
      break;

    default:
      throw std::runtime_error("Unhandled Controllers write_u8");
  }
}

void
Controllers::write_u16(u32 addr, u16 value)
{
  printf("controllers: write_u16(0x%08x) < 0x%x\n", addr, value);
  switch (addr) {
    case 0x1f80'1048:
      m_joy_mode = value;
      break;
    case 0x1f80'104a:
      if (value & 0x10) {
        m_irq = false;
      }
      m_joy_ctrl = value;
      if (!(m_joy_ctrl & 2)) {
        m_current_device = CurrentDevice::None;
        for (u8 port = 0; port < 2; ++port) {
          if (Controller *controller = m_console->controller(port)) {
            controller->reset_state();
          }
        }
      }
      break;
    case 0x1f80'104e:
      m_joy_baud = value;
      break;
    default:
      throw std::runtime_error("Unhandled Controllers write_u16");
  }
}

void
Controllers::irq_event()
{
  if (m_irq == 0) {
    m_ack = 0;
    m_irq = 1;
  }

  if (m_irq) {
    m_console->irq_control()->raise(interrupts::ControllerAndMemoryCard);
  }

  m_console->schedule_event(100, &m_irq_event);
}

void
Controllers::write_u32(u32 addr, u32 value)
{
  printf("controllers: write_u32(0x%08x) < %x\n", addr, value);
  throw std::runtime_error("Unhandled Controllers write_u32");
}

void
Controllers::register_regions(fox::MemoryTable *memory)
{
  memory->map_mmio(0x1F80'1040, 16, "Controller Ports", this);
}
}
