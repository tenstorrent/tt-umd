## Building lib from source

The source can be found in the repository: https://github.com/tenstorrent/luwen

To build static lib, use the following commands:
```
cargo build -p create-ethernet-map --release --lib
cross build --target aarch64-unknown-linux-gnu --release --lib -p create-ethernet-map
```
