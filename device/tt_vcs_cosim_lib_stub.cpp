
#include <stdint.h>

#ifdef __cplusplus
   extern "C" {
#endif

int axi_read(int ring, int phys_x, int phys_y, uint64_t addr, int length, uint8_t* data){return 0;};
int axi_write(int ring, int phys_x, int phys_y, uint64_t addr, int length, uint8_t* data){return 0;};

void all_tensix_reset_assert(){};
void all_tensix_reset_deassert(){};
void tensix_reset_assert(uint32_t tensix_id){};
void tensix_reset_deassert(uint32_t tensix_id){};

// When we compile libdevice.so without this stub (VCS_DEVICE_EN==1)
// linking needs libcosim_bfm_dpi.so, which contains functions above
// that is fine for test that is running on vcs_device

// However, net2pipe compiles as a separate exe file and links with libdevice.so
// but does not need to be linked with libcosim_bfm_dpi.so, so it needs this stub also
// othwerise we get an undefined reference to test_fn(which is a function called 
// in libcosim_bfm_dpi.so, but defined test file)
// int test_fn(int argc, char** argv) {return 0;}


#ifdef __cplusplus
   }
#endif

