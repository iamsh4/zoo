#include "scheduler.h"

void
EventScheduler::Event::serialize(serialization::Snapshot &snapshot)
{
  snapshot.add_range(m_name, sizeof(m_timestamp), &m_timestamp);
}

void
EventScheduler::Event::deserialize(const serialization::Snapshot &snapshot)
{
  if (m_timestamp != UINT64_MAX) {
    cancel();
  }

  static_assert(sizeof(m_timestamp) == 8);
  snapshot.apply_all_ranges(m_name, &m_timestamp);

  if (m_timestamp != UINT64_MAX) {
    m_scheduler->on_scheduled(this);
  }
}

EventScheduler::EventScheduler() : m_next_timestamp(UINT64_MAX)
{
  return;
}

EventScheduler::~EventScheduler()
{
  clear();
}

void
EventScheduler::clear()
{
  while (!empty()) {
    m_queue.top()->on_cancelled(this);
    m_queue.pop();
  }
}

void
EventScheduler::run_until(const u64 timestamp)
{
  while (!empty() && m_queue.top()->timestamp() <= timestamp) {
    auto e = m_queue.top();
    m_queue.pop();
    e->run();
  }

  if (!m_queue.empty()) {
    m_next_timestamp = m_queue.top()->timestamp();
  } else {
    m_next_timestamp = UINT64_MAX;
  }
}

void
EventScheduler::on_scheduled(Event *const event)
{
  m_queue.emplace(event);
  m_next_timestamp = m_queue.top()->timestamp();
}

void
EventScheduler::cancel_event(Event *const event)
{
  /* The standard priority queue interface doesn't provide an interface for
   * directly removing elements. Transfer all but the removed element to a new
   * queue. */
  decltype(m_queue) old_queue = std::move(m_queue);
  m_queue                     = decltype(m_queue)();

  while (!old_queue.empty()) {
    if (old_queue.top() != event) {
      m_queue.emplace(old_queue.top());
    }

    old_queue.pop();
  }

  if (!m_queue.empty()) {
    m_next_timestamp = m_queue.top()->timestamp();
  } else {
    m_next_timestamp = UINT64_MAX;
  }

  event->on_cancelled(this);
}
