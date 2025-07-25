# IOMMU benchmark

This benchmark contains tests that are measuring performance of different IOMMU operations through UMD and KMD.

IOMMU operations that UMD does are mapping the buffer through IOMMU and unampping it when it is not needed anymore. Both operations are done by making ioctl call to KMD.

