#pragma once

#include "peripherals/protocol.h"

namespace maple {

/*!
 * @class maple::Device
 * @brief Virtual base class for devices attached using the Maple bus.
 */
class Device {
public:
  Device();
  virtual ~Device();

  /*!
   * @brief Generate and return the buffer of data to return for
   *        RequestDeviceInfo commands. Returns the size in bytes of the
   *        generated buffer.
   */
  virtual ssize_t identify(const Header *in, Header *out, u8 *buffer) = 0;

  /*!
   * @brief Respond to a command sent by the bus master. If the response length
   *        is greater than 0, the data in the response buffer will be DMA'd.
   *        Otherwise it will be treated as "no signal".
   *
   * The returned output size does not include the size of the maple::Header but
   * must include the size of the function field. This method is not called for
   * the RequestDeviceInfo command - see identify().
   */
  virtual ssize_t run_command(const Packet *in, Packet *out) = 0;

  /*!
   * @brief Reset the device to its power-on state.
   */
  virtual void reset() = 0;
};

}
