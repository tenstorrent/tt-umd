#ifndef TT_VCS_COSIM_LIB_H
#define TT_VCS_COSIM_LIB_H

#include <stdint.h>

#ifdef __cplusplus
   extern "C" {
#endif

int axi_read(int ring, int phys_x, int phys_y, uint64_t addr, int length, uint8_t* data);
int axi_write(int ring, int phys_x, int phys_y, uint64_t addr, int length, uint8_t* data);

void all_tensix_reset_assert();
void all_tensix_reset_deassert();
void tensix_reset_assert(uint32_t tensix_id);
void tensix_reset_deassert(uint32_t tensix_id);

void print_with_path(const char *fmt, ...);

#ifdef __cplusplus
   }
#endif

#endif //TT_VCS_COSIM_LIB_H
