// vim: expandtab:ts=2:sw=2

#pragma once

/*!
 * @class intrusive_list
 * @brief Implementation of a doubly-linked list that stores a class derived
 *        from intrusive_entry.
 */
template<class T>
class intrusive_list {
public:
  intrusive_list() : m_head(nullptr), m_tail(nullptr)
  {
    return;
  }

  intrusive_list(intrusive_list<T> &&other) : m_head(other.m_head), m_tail(other.m_tail)
  {
    other.m_head = nullptr;
    other.m_tail = nullptr;
  }

  ~intrusive_list()
  {
    assert(m_head == nullptr);
    assert(m_tail == nullptr);
  }

  intrusive_list<T> &operator=(intrusive_list<T> &&other)
  {
    assert(m_head == nullptr);
    assert(m_tail == nullptr);

    m_head = other.m_head;
    m_tail = other.m_tail;

    other.m_head = nullptr;
    other.m_tail = nullptr;

    return *this;
  }

  void push_front(T *element)
  {
    if (m_tail == nullptr) {
      m_tail = element;
      m_head = element;
    } else {
      m_head->m_previous = element;
      element->m_next = m_head;
      m_head = element;
    }
  }

  void push_back(T *element)
  {
    if (m_head == nullptr) {
      m_head = element;
      m_tail = element;
    } else {
      m_tail->m_next = element;
      element->m_previous = m_tail;
      m_tail = element;
    }
  }

  T *pop_front()
  {
    assert(!empty());

    T *element = m_head;
    if (element->m_next == nullptr) {
      m_head = nullptr;
      m_tail = nullptr;
    } else {
      m_head = element->m_next;
    }

    element->m_next = nullptr;
    element->m_previous = nullptr;

    return element;
  }

  T *pop_back()
  {
    assert(!empty());

    T *element = m_tail;
    if (element->m_previous == nullptr) {
      m_head = nullptr;
      m_tail = nullptr;
    } else {
      m_tail = element->m_previous;
    }

    element->m_next = nullptr;
    element->m_previous = nullptr;

    return element;
  }

  void erase(T *element)
  {
    if (element->m_previous != nullptr) {
      element->m_previous->m_next = element->m_next;
    } else {
      assert(m_head == element);
      m_head = element->m_next;
    }

    if (element->m_next != nullptr) {
      element->m_next->m_previous = element->m_previous;
    } else {
      assert(m_tail == element);
      m_tail = element->m_previous;
    }

    element->m_next = nullptr;
    element->m_previous = nullptr;
  }

  void clear()
  {
    while (!empty()) {
      T *const element = pop_front();
      delete element;
    }
  }

  T *front()
  {
    return m_head;
  }

  T *back()
  {
    return m_tail;
  }

  bool empty() const
  {
    return m_head == nullptr;
  }

private:
  T *m_head;
  T *m_tail;
};

/*!
 * @class intrusive_entry
 * @brief Base class to for classes used in intrusive data structures.
 */
template<class T>
class intrusive_entry {
public:
  intrusive_entry() : m_previous(nullptr), m_next(nullptr)
  {
    return;
  }

  ~intrusive_entry()
  {
    assert(m_previous == nullptr);
    assert(m_next == nullptr);
  }

private:
  T *m_previous;
  T *m_next;

  friend class intrusive_list<T>;
};
