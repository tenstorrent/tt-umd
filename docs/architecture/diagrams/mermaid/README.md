# UMD architecture diagrams (Mermaid)

The same diagrams as the PlantUML sources in [`../plantuml/`](../plantuml/). GitHub renders each `.mmd` file when opened. Keep the two formats in sync when changing either.

- [`umd_context.mmd`](umd_context.mmd): UMD's layers and what they reach in the kernel and on the device
- [`umd_static_decomposition.mmd`](umd_static_decomposition.mmd): components of each layer; solid arrows are required components, dashed arrows optional ones
- [`umd_device_lifecycle.mmd`](umd_device_lifecycle.mmd): device states, each transition labelled with what triggers it
- [`umd_bringup_sequence.mmd`](umd_bringup_sequence.mmd): device bring-up on WH/BH over PCIe
- [`umd_write_sequence.mmd`](umd_write_sequence.mmd): a control-path write on WH/BH over PCIe, with its failure outcomes
