#pragma once

#include <string>

#include "arm7di_shared.h"
#include "shared/types.h"

namespace guest::arm7di {

std::string disassemble(Arm7DIInstructionInfo);

}
