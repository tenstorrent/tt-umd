
// tt_zebu_wrapper stub
// --------------------------------------------
#include "tt_zebu_wrapper.h"
#include "tt_emu_zemi3_wrapper.h"

tt_zebu_wrapper::tt_zebu_wrapper(){}
tt_zebu_wrapper::~tt_zebu_wrapper(){}

void tt_zebu_wrapper::zebu_create_xtors(xtor_amba_master_svs  *objAmba){}

// AXI xtor functions
int tt_zebu_wrapper::axi_read(int ring, int phys_x, int phys_y, uint64_t addr, int length, std::vector<uint8_t> &data){return 0;}
int tt_zebu_wrapper::axi_write(int ring, int phys_x, int phys_y, uint64_t addr, int length, std::vector<uint8_t> data){return 0;}
void tt_zebu_wrapper::wait_axi_xtor_reset(){}

// Command xtor functions - tensix reset
void tt_zebu_wrapper::all_tensix_reset_assert(){}
void tt_zebu_wrapper::all_tensix_reset_deassert(){}
void tt_zebu_wrapper::tensix_reset_assert(uint32_t tensix_id){}
void tt_zebu_wrapper::tensix_reset_deassert(uint32_t tensix_id){}

void tt_zebu_wrapper::zebu_runtime_init() {}
// --------------------------------------------


tt_emu_zemi3_wrapper::tt_emu_zemi3_wrapper(){}
tt_emu_zemi3_wrapper::~tt_emu_zemi3_wrapper(){}

void tt_emu_zemi3_wrapper::zebu_start(){}
void tt_emu_zemi3_wrapper::zebu_finish(){}

void tt_emu_zemi3_wrapper::zebu_runtime_init(){}
