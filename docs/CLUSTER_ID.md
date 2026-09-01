# Cluster id

The **cluster id** is a unique string identifying **a group of Tenstorrent accelerators connected to a common host / controller / root complex**. One cluster descriptor describes exactly one such group, so the id is a descriptor-level field and not a per-chip one:

```yaml
cluster_id: bh-glx-110-c01u02
arch:
  0: blackhole
  ...
```

It is available as `ClusterDescriptor::get_cluster_id()` (`Optional[str]` from Python), and is empty on any descriptor that predates the field or was never given one.

The value is currently that group's **bare metal hostname**, because the factory system descriptor (FSD) and the fabric topology solver join on hostnames. But the field identifies the accelerator group, not a machine — it is named `cluster_id` rather than `hostname` so the value scheme can change later without the field changing meaning.

Note that this is unrelated to `EthCoord::cluster_id`, which is a per-chip integer grouping ethernet-connected chips within one descriptor.

## Where the value comes from

| Descriptor | Cluster id |
|---|---|
| Parsed from YAML | the `cluster_id` key, or empty when the key is absent |
| Built by topology discovery | `TopologyDiscoveryOptions::cluster_id` when the caller supplied one, otherwise the OS hostname |
| `create_mock_cluster` | empty |

Discovery is the only place that calls `gethostname()`, and it never does so for a descriptor loaded from YAML: that descriptor may describe a different machine, and substituting the local hostname would relabel it.

## Supplying a cluster id

`gethostname()` is only the right answer when the process runs on bare metal:

| Where the process runs | `gethostname()` returns | Usable as a cluster id? |
|---|---|---|
| Bare metal | the machine's hostname, e.g. `bh-glx-110-c01u02` | yes |
| Container | the container id, e.g. `7f3a91c2b4de` | **no** — changes every run, unrelated to the hardware |
| VM | whatever the guest was named, e.g. `pod-fabric-worker-3` | **no** — names the guest, not the accelerator group |

In the last two cases the hostname is stable enough to look convincing while pointing at nothing physical, which is worse than having no value at all: fabric would build a topology keyed on it and silently disagree with the FSD. Callers that run there know where the value has to come from — a scheduler, an orchestrator label, an environment variable of their own — so they pass it in:

```cpp
TopologyDiscoveryOptions options;
options.cluster_id = "bh-glx-110-c01u02";
auto cluster_desc = TopologyDiscovery::discover(options).first;
```

```python
options = tt_umd.TopologyDiscoveryOptions()
options.cluster_id = "bh-glx-110-c01u02"
cluster_desc = tt_umd.TopologyDiscovery.create_cluster_descriptor(options)
```

UMD deliberately does not read an environment variable for this. Container and VM plumbing belongs to whoever launches the process, and one string on the options struct is enough for them to pass it down.

## Legal values

1 to 128 characters from `[A-Za-z0-9._-]`. The charset is deliberately hostname-shaped while cluster ids are hostname-valued, and the length limit is the fixed 128-byte buffer that consumers pack the id into (no NUL terminator, hence 128 and not 127).

| Situation | Behavior |
|---|---|
| No cluster id supplied to discovery | OS hostname is used, raw, with no FQDN stripping — consumers canonicalize |
| A legal cluster id supplied to discovery | that value is used |
| An illegal cluster id supplied to discovery | **throws.** The caller passed it on purpose; silently falling back to a container hostname would produce a wrong-but-plausible topology, which is the failure that supplying an id is meant to prevent |
| OS hostname unusable as a cluster id (over 128 characters, exotic characters), or `gethostname()` fails | warning logged, cluster id left unset — discovery still succeeds and consumers fall back to what they did before |
| An illegal `cluster_id` in a YAML being parsed | throws |

A cluster id never changes over a descriptor's lifetime, so there is no setter. Both entry points validate, which means every cluster id on a descriptor is a legal one.

## Serialization

`serialize()` writes `cluster_id` first in the map, and omits the key entirely when the id is unset, so descriptors that never had one serialize byte-identically to before. A descriptor recaptured on hardware (`serialize_to_file`) carries the live cluster id for free.

Old UMD reading a YAML that has `cluster_id` ignores the unknown key, and new UMD reading a YAML without it leaves the field empty. Neither is an error, and the key is not in the schema's `required` list.
