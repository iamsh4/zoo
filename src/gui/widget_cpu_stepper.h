#include <string>
#include <vector>

#include "gui/window_cpu_guest.h"
#include "gui/window_jit_workbench.h"
#include "gui/widget.h"

namespace gui {

/*!
 * @class gui::CpuStepperWidget
 * @brief SH4-specific widget for showing instructions around the current
 *        PC address and setting / removing breakpoints. Also visualizes
 *        jit blocks.
 */
class CpuStepperWidget final : public Widget {
public:
  CpuStepperWidget(std::shared_ptr<CPUWindowGuest> guest,
                   JitWorkbenchWindow *workbench,
                   unsigned context_lines);
  ~CpuStepperWidget();

  void render() override final;

private:
  std::shared_ptr<CPUWindowGuest> m_cpu_guest;
  JitWorkbenchWindow *const m_workbench;
  const unsigned m_context_lines;
  u32 m_last_pc;
};

}
