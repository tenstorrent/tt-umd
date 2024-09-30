#include "chip.h"

namespace tt::umd {

std::string TensixSoftResetOptionsToString(TensixSoftResetOptions value) {
    std::string output;

    if((value & TensixSoftResetOptions::BRISC) != TensixSoftResetOptions::NONE) {
        output += "BRISC | ";
    }
    if((value & TensixSoftResetOptions::TRISC0) != TensixSoftResetOptions::NONE) {
        output += "TRISC0 | ";
    }
    if((value & TensixSoftResetOptions::TRISC1) != TensixSoftResetOptions::NONE) {
        output += "TRISC1 | ";
    }
    if((value & TensixSoftResetOptions::TRISC2) != TensixSoftResetOptions::NONE) {
        output += "TRISC2 | ";
    }
    if((value & TensixSoftResetOptions::NCRISC) != TensixSoftResetOptions::NONE) {
        output += "NCRISC | ";
    }
    if((value & TensixSoftResetOptions::STAGGERED_START) != TensixSoftResetOptions::NONE) {
        output += "STAGGERED_START | ";
    }

  if(output.empty()) {
    output = "UNKNOWN";
  } else {
    output.erase(output.end() - 3, output.end());
  }

  return output;
}

Chip::Chip() : soc_descriptor_per_chip({}) {
}

Chip::~Chip() {
}

const SocDescriptor *Chip::get_soc_descriptor(chip_id_t chip) const { return &soc_descriptor_per_chip.at(chip); }


}