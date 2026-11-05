# Factories

A factory is a function that builds an object for its caller. UMD uses two shapes, chosen by what the factory decides.

## Picking an implementation: a free `create_<thing>`

When the factory chooses which implementation of an interface to build (by architecture, transport, or simulator type), make it a free function `create_<thing>` declared in `<thing>_factory.hpp`.

The interface then never names its implementations: only the factory includes them, and adding an implementation touches the factory, not the interface header.

```cpp
// tt_device/simulation_device_factory.hpp
// Builds a TTSimTTDevice for a .so, an RtlSimulationTTDevice otherwise.
std::unique_ptr<TTDevice> create_simulation_tt_device(
    const std::filesystem::path &simulator_path, int num_host_mem_channels = 0);
```

## Building from a source: a static named constructor

When a class builds itself from some input (a file, a parent object, a device) and there is no implementation to choose, make the factory a static member of that class. Name it after the source when the class can be built from more than one.

```cpp
// cluster_descriptor.hpp
static std::unique_ptr<ClusterDescriptor> create_from_yaml(const std::string &cluster_descriptor_file_path);
static std::unique_ptr<ClusterDescriptor> create_from_yaml_content(const std::string &cluster_descriptor_file_content);

// chip/local_chip.hpp
static std::unique_ptr<LocalChip> create(std::unique_ptr<TTDevice> tt_device, int num_host_mem_channels = 0);
```

## Return `std::unique_ptr`

Factories return `std::unique_ptr`, so the caller owns the object and decides whether to share it. A `std::unique_ptr` converts to `std::shared_ptr`; the reverse is impossible.

## Moving or renaming a factory

Update every in-repo caller in the same change. Keep the old name as a `[[deprecated]]` forwarder only when callers outside this repository use it, and remove the forwarder once they have moved:

```cpp
class Thing {
    [[deprecated("Use create_thing()")]] static std::unique_ptr<Thing> create(int id) { return create_thing(id); }
};
```

## Existing factories

Some factories predate this guide, for example static factories that pick an implementation by architecture or return `std::shared_ptr`. Convert them as separate changes, not as part of unrelated work.
