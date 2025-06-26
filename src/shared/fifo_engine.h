#pragma once

#include <functional>
#include <thread>

#include "shared/async_fifo.h"

/*!
 * @brief Generic class for submitting command sequences using a FIFO. Can
 *        be implemented as a synchronous function call or an asynchronous
 *        thread.
 *
 *
 * Note: The issue address UINT32_MAX is reserved for internal usage.
 */
template<typename T>
class FifoEngine {
public:
  FifoEngine(const char *name, std::function<void(u32, const T &)> callback)
    : m_name(name),
      m_callback(callback)
  {
    return;
  }

  virtual ~FifoEngine()
  {
    return;
  }

  virtual void issue(u32 address, const T &value) = 0;

protected:
  const char *const m_name;
  std::function<void(u32, const T &)> m_callback;
};

/*!
 * @brief Implementation of FifoEngine that uses a synchronous method call
 *        for execution.
 */
template<typename T>
class SyncFifoEngine : public FifoEngine<T> {
public:
  SyncFifoEngine(const char *name, std::function<void(u32, const T &)> callback);
  ~SyncFifoEngine();

  void issue(u32 address, const T &value) override;
};

/*!
 * @brief Implementation of FifoEngine that uses a asynchronous thread for
 *        execution.
 */
template<typename T>
class AsyncFifoEngine : public FifoEngine<T> {
public:
  AsyncFifoEngine(const char *name,
                  std::function<void(u32, const T &)> callback,
                  unsigned size = 32u);

  ~AsyncFifoEngine();

  void issue(u32 address, const T &value) override;

private:
  std::thread m_thread;
  AsyncFIFO<T> m_fifo;

  void issue_thread();
};
