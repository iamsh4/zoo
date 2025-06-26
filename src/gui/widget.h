#pragma once

#include <memory>
#include <imgui.h>

#include "frontend/console_director.h"
#include "core/console.h"

namespace gui {

class Widget {
public:
  Widget();
  virtual ~Widget();

  virtual void render() = 0;
};

}
