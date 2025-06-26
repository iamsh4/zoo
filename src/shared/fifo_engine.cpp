#include "shared/profiling.h"
#include "fifo_engine.h"

/*******************************************************************************
 * SyncFifoEngine Implementation
 ******************************************************************************/

template<typename T>
SyncFifoEngine<T>::SyncFifoEngine(const char *name,
                                  std::function<void(u32, const T &)> callback)
  : FifoEngine<T>(name, callback)
{
  return;
}

template<typename T>
SyncFifoEngine<T>::~SyncFifoEngine()
{
  return;
}

template<typename T>
void
SyncFifoEngine<T>::issue(const u32 address, const T &value)
{
  assert(address != UINT32_MAX);
  this->m_callback(address, value);
}

template class SyncFifoEngine<u32>;

/*******************************************************************************
 * AsyncFifoEngine Implementation
 ******************************************************************************/

template<typename T>
AsyncFifoEngine<T>::AsyncFifoEngine(const char *const name,
                                    std::function<void(u32, const T &)> callback,
                                    const unsigned size)
  : FifoEngine<T>(name, callback),
    m_thread(std::bind(&AsyncFifoEngine::issue_thread, this)),
    m_fifo(size)
{
  return;
}

template<typename T>
AsyncFifoEngine<T>::~AsyncFifoEngine()
{
  m_fifo.write(UINT32_MAX, T());
  m_thread.join();
}

template<typename T>
void
AsyncFifoEngine<T>::issue(const u32 address, const T &value)
{
  assert(address != UINT32_MAX);
  m_fifo.write(address, value);
}

template<typename T>
void
AsyncFifoEngine<T>::issue_thread()
{
  ProfileSetThreadName("FifoEngineThread");

  while (true) {
    u32 address;
    T value;
    m_fifo.read(&address, &value);

    if (address == UINT32_MAX) {
      break;
    }

    this->m_callback(address, value);
  }
}

template class AsyncFifoEngine<u32>;
