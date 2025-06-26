#pragma once

#include <functional>
#include <vector>
#include <queue>
#include <cassert>
#include <string>
#include <cstdio>

#include "serialization/serializer.h"
#include "types.h"

/*!
 * @class EventScheduler
 * @brief Handles scheduling work to run a fixed time in the future. Timing
 *        is done externally so it can be tied to whatever clock makes sense
 *        (real time, emulation time, etc.).
 *
 * Work is scheduled by adding an Event class to the scheduler with a delay to
 * wait before running it. An Event must finish executing or be cancelled
 * before it can be scheduled to run again.
 *
 * Note: If two events are scheduled to execute at the same time, the order in
 *       which they execute is not guaranteed.
 *
 * This implementation is not thread safe.
 */
class EventScheduler {
public:
  /*!
   * @class EventScheduler::Event
   * @brief Class representing a single event that can be scheduled to run in
   *        the EventScheduler.
   */
  class Event : public serialization::Serializer {
  public:
    Event(const std::string &name,
          std::function<void()> callback,
          EventScheduler *const scheduler)
      : m_name(name),
        m_callback(callback),
        m_scheduler(scheduler),
        m_timestamp(UINT64_MAX)
    {
      return;
    }

    ~Event()
    {
      assert(m_timestamp == UINT64_MAX);
    }

    /*!
     * @brief Return the name assigned to this event. Useful for debugging.
     */
    const std::string &name() const
    {
      return m_name;
    }

    /*!
     * @brief Schedule this Event for execution at the indicated time. The Event
     *        must not be currently scheduled.
     */
    void schedule(const u64 timestamp)
    {
      assert(m_timestamp == UINT64_MAX);
      m_timestamp = timestamp;
      m_scheduler->on_scheduled(this);
    }

    /*!
     * @brief Cancel execution of the event. This is a convenience method to
     *        avoid needing to know the EventScheduler instance. No-op if the
     *        event is not currently scheduled.
     */
    void cancel()
    {
      if (m_timestamp != UINT64_MAX) {
        m_scheduler->cancel_event(this);
        m_timestamp = UINT64_MAX;
      }
    }

    /*!
     * @brief Check if the event is currently scheduled.
     */
    bool is_scheduled() const
    {
      return m_timestamp != UINT64_MAX;
    }

    /*!
     * @brief Return the time that the Event is currently scheduled to run. If
     *        not currently scheduled returns UINT64_MAX.
     */
    u64 timestamp() const
    {
      return m_timestamp;
    }

    void serialize(serialization::Snapshot &snapshot) final;
    void deserialize(const serialization::Snapshot &snapshot) final;

  private:
    const std::string m_name;
    const std::function<void()> m_callback;
    EventScheduler *const m_scheduler;
    u64 m_timestamp;

    /*!
     * @brief Execute the event. Should only be called by EventScheduler.
     */
    void run()
    {
      /* Update schedule state before running callback to allow the callback
       * to reschedule itself. */
      assert(m_timestamp != UINT64_MAX);
      m_timestamp = UINT64_MAX;

      m_callback();
    }

    /*!
     * @brief Mark this Event has having been cancelled. Should only be called
     *        by EventScheduler.
     */
    void on_cancelled(EventScheduler *const scheduler)
    {
      assert(m_scheduler == scheduler);
      m_timestamp = UINT64_MAX;
    }

    friend EventScheduler;
  };

  EventScheduler();
  ~EventScheduler();

  /*!
   * @brief Returns true if there are currently no events scheduled.
   */
  bool empty() const
  {
    return m_queue.empty();
  }

  /*!
   * @brief Returns the timestamp of when the next event should run If there
   *        are no events currently queued returns UINT64_MAX..
   */
  u64 next_timestamp() const
  {
    return m_next_timestamp;
  }

  /*!
   * @brief Run scheduled events until the specified timestamp (inclusive).
   */
  void run_until(u64 timestamp);

  /*!
   * @brief Handle an Event request to schedule itself.
   */
  void on_scheduled(Event *event);

  /*!
   * @brief Cancel a previously scheduled event.
   */
  void cancel_event(Event *event);

  void clear();

private:
  typedef std::pair<u64, Event *> EventPair;

  /*!
   * @brief Helper class implementing ordering of Event pointers by their
   *        timestamps.
   */
  class EventCompare final {
  public:
    bool operator()(const Event *const a, const Event *const b) const
    {
      assert(a->timestamp() != UINT64_MAX);
      assert(b->timestamp() != UINT64_MAX);

      return a->timestamp() > b->timestamp();
    }
  };

  /*!
   * @brief The set of currently scheduled events.
   */
  std::priority_queue<Event *, std::vector<Event *>, EventCompare> m_queue;

  /*!
   * @brief The timestamp of the next scheduled event or UINT64_MAX if there
   *        are no current events.
   */
  u64 m_next_timestamp;
};
