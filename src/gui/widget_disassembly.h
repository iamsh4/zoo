#include <string>
#include <vector>

#include "fox/jit/cache.h"
#include "gui/widget.h"

namespace gui {

/*!
 * @class gui::DisassemblyWidget
 * @brief Generic widget for showing a line-by-line disassembly. Can be
 *        replaced with more advanced language-specific tooling such as the
 *        IrAnalysisWidget.
 */
class DisassemblyWidget final : public Widget {
public:
  typedef std::function<std::vector<std::string>(fox::ref<fox::jit::CacheEntry>)> Disassembler;

  DisassemblyWidget(Disassembler disassembler);
  ~DisassemblyWidget();

  void set_target(fox::ref<fox::jit::CacheEntry> target);
  void render() override final;

private:
  const Disassembler m_disassembler;
  fox::ref<fox::jit::CacheEntry> m_target;
  std::vector<std::string> m_lines;
};

}
