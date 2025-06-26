
#include "input_timeline.h"

namespace serialization {

InputTimeline::InputTimeline(const char *path) {}

void
InputTimeline::save(const char *path)
{
  // TODO
  assert(false);
}

void
InputTimeline::set(u64 timestamp, const CompleteInputState &state)
{
  m_input_states.insert({ timestamp, state });
}

const CompleteInputState &
InputTimeline::get(u64 timestamp, GetMode mode) const
{
  assert(mode == GetMode::ExactMatch_Or_ReturnNothingPressed);
  return m_input_states.at(timestamp);
}

}