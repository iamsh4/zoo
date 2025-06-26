#pragma once

#include <memory>
#include <string>

#include <imgui.h>

#include "frontend/console_director.h"
#include "core/console.h"
#include "gui/window.h"

namespace gui {

class Window {
public:
  Window(const char *name) : m_name(std::string(name)), m_is_visible(true)
  {
    return;
  }

  virtual ~Window()
  {
    return;
  }

  void draw()
  {
    if (is_visible()) {
      render();
    }
  }

  void show()
  {
    m_is_visible = true;
  }

  void hide()
  {
    m_is_visible = false;
  }

  void toggle_visible()
  {
    m_is_visible = !m_is_visible;
  }

  bool is_visible() const
  {
    return m_is_visible;
  }

  const std::string &name() const
  {
    return m_name;
  }

protected:
  std::string m_name;
  bool m_is_visible;

  virtual void render() = 0;
};

}
