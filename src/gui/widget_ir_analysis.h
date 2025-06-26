#include <string>
#include <vector>

#include "fox/jit/cache.h"
#include "gui/widget.h"

namespace gui {

/*!
 * @class gui::IrAnalysisWidget
 */
class IrAnalysisWidget final : public Widget {
public:
  IrAnalysisWidget();
  ~IrAnalysisWidget();

  void set_target(fox::ref<fox::jit::CacheEntry> target);
  void render() override final;

private:
  fox::ref<fox::jit::CacheEntry> m_target;
};

}
