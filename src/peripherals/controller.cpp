#include <algorithm>
#include <cstring>

#include "peripherals/controller.h"

namespace maple {

/* Default identification data for a Dreamcast controller */
static const u8 controller_identification[112] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x0f, 0x06, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0xff, 0x00, 0x44, 0x72, 0x65, 0x61, 0x6d, 0x63, 0x61, 0x73, 0x74, 0x20,
  0x43, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x6c, 0x65, 0x72, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x50, 0x72, 0x6f, 0x64, 0x75, 0x63, 0x65, 0x64,
  0x20, 0x42, 0x79, 0x20, 0x6f, 0x72, 0x20, 0x55, 0x6e, 0x64, 0x65, 0x72, 0x20, 0x4c,
  0x69, 0x63, 0x65, 0x6e, 0x73, 0x65, 0x20, 0x46, 0x72, 0x6f, 0x6d, 0x20, 0x53, 0x45,
  0x47, 0x41, 0x20, 0x45, 0x4e, 0x54, 0x45, 0x52, 0x50, 0x52, 0x49, 0x53, 0x45, 0x53,
  0x2c, 0x4c, 0x54, 0x44, 0x2e, 0x20, 0x20, 0x20, 0x20, 0x20, 0xae, 0x01, 0xf4, 0x01
};

Controller::Controller()
{
  reset();
}

Controller::~Controller()
{
  return;
}

void
Controller::button_down(const Button button)
{
  switch (button) {
    case Button::A:
      m_status_data.button_a = 0u;
      break;
    case Button::B:
      m_status_data.button_b = 0u;
      break;
    case Button::X:
      m_status_data.button_x = 0u;
      break;
    case Button::Y:
      m_status_data.button_y = 0u;
      break;
    case Button::Start:
      m_status_data.button_start = 0u;
      break;
    case Button::DpadUp:
      m_status_data.dpad_up = 0u;
      break;
    case Button::DpadDown:
      m_status_data.dpad_down = 0u;
      break;
    case Button::DpadLeft:
      m_status_data.dpad_left = 0u;
      break;
    case Button::DpadRight:
      m_status_data.dpad_right = 0u;
      break;
    case Button::N_Buttons:
      break;
  }
}

void
Controller::button_up(const Button button)
{
  switch (button) {
    case Button::A:
      m_status_data.button_a = 1u;
      break;
    case Button::B:
      m_status_data.button_b = 1u;
      break;
    case Button::X:
      m_status_data.button_x = 1u;
      break;
    case Button::Y:
      m_status_data.button_y = 1u;
      break;
    case Button::Start:
      m_status_data.button_start = 1u;
      break;
    case Button::DpadUp:
      m_status_data.dpad_up = 1u;
      break;
    case Button::DpadDown:
      m_status_data.dpad_down = 1u;
      break;
    case Button::DpadLeft:
      m_status_data.dpad_left = 1u;
      break;
    case Button::DpadRight:
      m_status_data.dpad_right = 1u;
      break;
    case Button::N_Buttons:
      break;
  }
}

void
Controller::trigger_left(const float value)
{
  const float clamped = std::max(std::min(value, 1.0f), 0.0f);
  m_status_data.trigger_left = clamped * 255.0f;
}

void
Controller::trigger_right(const float value)
{
  const float clamped = std::max(std::min(value, 1.0f), 0.0f);
  m_status_data.trigger_right = clamped * 255.0f;
}

void
Controller::joystick_x(const float value)
{
  const float clamped = std::max(std::min(value, 1.0f), 0.0f);
  m_status_data.joystick_x = clamped * 255.0f;
}

void
Controller::joystick_y(const float value)
{
  const float clamped = std::max(std::min(value, 1.0f), 0.0f);
  m_status_data.joystick_y = clamped * 255.0f;
}

void
Controller::add_device(const unsigned slot, std::shared_ptr<Device> device)
{
  assert(slot < 2u);
  assert(!m_slots[slot]); /* TODO Hot-swap */
  m_slots[slot] = device;
}

ssize_t
Controller::identify(const Header *const in, Header *const out, u8 *const buffer)
{
  /* Check if the command target is the controller or a plug-in device. */
  /* TODO locking to protect against hotswap */
  if ((in->destination & 0x0f) != 0u) {
    if (m_slots[0u] && (in->destination & 0x0fu) == 0x01u) {
      out->source |= 0x01u;
      return m_slots[0u]->identify(in, out, buffer);
    } else if (m_slots[1u] && (in->destination & 0x0fu) == 0x02u) {
      out->source |= 0x02u;
      return m_slots[1u]->identify(in, out, buffer);
    }
    return -1;
  }

  out->source |= 0x20u;
  out->source |= m_slots[0] ? 0x01u : 0x00u;
  out->source |= m_slots[1] ? 0x02u : 0x00u;
  out->length = sizeof(controller_identification) / 4u;

  memcpy(buffer, controller_identification, sizeof(controller_identification));
  return sizeof(controller_identification);
}

ssize_t
Controller::run_command(const Packet *const in, Packet *const out)
{
  /* Check if the command target is the controller or a plug-in device. */
  /* TODO locking to protect against hotswap */
  if ((in->header.destination & 0x0f) != 0) {
    if (m_slots[0] && (in->header.destination & 0x0f) == 0x01) {
      out->header.source |= 0x01u;
      return m_slots[0]->run_command(in, out);
    } else if (m_slots[1] && (in->header.destination & 0x0f) == 0x02) {
      out->header.source |= 0x02u;
      return m_slots[1]->run_command(in, out);
    }
    return -1;
  }

  /* Prepare common fields. Source bits are set to indicate presence of devices
   * in add-on slots. */
  out->header.source |= 0x20;
  out->header.source |= m_slots[0] ? 1u : 0u;
  out->header.source |= m_slots[1] ? 2u : 0u;

  if (in->function != 0x01000000u) {
    return -1;
  }

  switch (in->header.command) {
    case RequestCondition: {
      out->header.command = ReplyData;
      out->header.length = sizeof(m_status_data) / 4u + 1u;
      memcpy(&out->data[0], &m_status_data, sizeof(m_status_data));
      return sizeof(m_status_data) + 4u;
    }

    default:
      return -1;
  }
}

void
Controller::reset()
{
  memset(&m_status_data, 0xff, sizeof(m_status_data));
  m_status_data.trigger_left = 0x00;
  m_status_data.trigger_right = 0x00;
  m_status_data.joystick_x = 0x80;
  m_status_data.joystick_y = 0x80;
  m_status_data.altjoystick_x = 0x80;
  m_status_data.altjoystick_y = 0x80;
}

}
