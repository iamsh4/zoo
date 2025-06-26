#pragma once

#include <map>
#include "serialization/serializer.h"
#include "shared/types.h"
#include "peripherals/controller.h"

namespace serialization {

// Full Controller State = 2 analog + 2 triggers + 2 digital = 6B / controller

struct CompleteInputState {
  struct ControllerState {
    float analog_x = 0.5;
    float analog_y = 0.5;
    float trigger_left = 0;
    float trigger_right = 0;
    u32 buttons = 0;

    void button_down(maple::Controller::Button button)
    {
      const u32 button_mask = 1 << (u32)button;
      buttons |= button_mask;
    }
    void button_up(maple::Controller::Button button)
    {
      const u32 button_mask = 1 << (u32)button;
      buttons = (buttons & ~button_mask);
    }
    bool is_button_down(maple::Controller::Button button) const
    {
      const u32 button_mask = 1 << (u32)button;
      return buttons & button_mask;
    }
  };

  // TODO : support keyboards etc.

  ControllerState controllers[4] = {};
};

class InputTimeline {
public:
  InputTimeline() {}
  InputTimeline(const char *path);
  void save(const char *path);

  bool has(u64 timestamp) const
  {
    return m_input_states.find(timestamp) != m_input_states.end();
  }
  void set(u64 timestamp, const CompleteInputState &);

  /**! Defines the behavior of get(..) for a given timestamp.*/
  enum class GetMode
  {
    /**! Return data from the timeline for the timestmap provided, otherwise return input
       states as if nothing is being pressed. */
    ExactMatch_Or_ReturnNothingPressed,

    // TODO : Potentially other behaviors if get(..) is
    // called for a timestamp which we don't have.
  };

  /**! Return inputs for a given timestamp. */
  const CompleteInputState &get(
    u64 timestamp,
    GetMode mode = GetMode::ExactMatch_Or_ReturnNothingPressed) const;

private:
  std::map<u64, CompleteInputState> m_input_states;
};

}