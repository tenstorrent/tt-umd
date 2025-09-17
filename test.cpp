#include <fcntl.h>      // for ::open
#include <linux/pci.h>  // for PCI_SLOT, PCI_FUNC
#include <sys/ioctl.h>  // for ioctl
#include <sys/mman.h>   // for mmap, munmap
#include <unistd.h>     // for ::close
#include <iostream>

#include "ioctl.h"

int main() {
    tenstorrent_allocate_dma_buf dma_buf{};
    dma_buf.in.requested_size = 16 * (1 << 20);
    dma_buf.in.buf_index = 0;

    int pci_device_file_desc = open("/dev/tenstorrent/0", O_RDWR | O_CLOEXEC);
    
    int ioctl_ret = ioctl(pci_device_file_desc, TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF, &dma_buf);

    std::cout << "ioctl ret: " << ioctl_ret << std::endl;

    if (ioctl_ret == -1) {
        std::cout << "errno: " << errno << std::endl;
    }

    if (ioctl_ret) {
        std::cout << "error" << std::endl;
    } else {
        std::cout << "managed to allocate" << std::endl;
    }
}