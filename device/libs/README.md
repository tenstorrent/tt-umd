## Building lib from source

The source can be found in the repository: https://github.com/tenstorrent/luwen

To build static lib, here's a list of helpful commands:
```
git clone git@github.com:tenstorrent/luwen.git && cd luwen
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
cargo build -p create-ethernet-map --release --lib
cargo install cross --git https://github.com/cross-rs/cross
cross build --target aarch64-unknown-linux-gnu --release --lib -p create-ethernet-map
cross build --target riscv64gc-unknown-linux-gnu --release --lib -p create-ethernet-map
```
