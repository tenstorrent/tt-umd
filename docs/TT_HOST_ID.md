# TT_HOST_ID

`TT_HOST_ID` is an environment variable that sets the **host id** UMD stamps on the cluster descriptor it produces during topology discovery. On bare metal it should be left unset — UMD then uses the OS hostname, which is what it did before this variable existed.

---

## What a host id is

A host id is a unique string identifying **a group of Tenstorrent accelerators connected to a common host / controller / root complex**. One cluster descriptor describes exactly one such group, so the id is a descriptor-level field, not a per-chip one:

```yaml
host_id: bh-glx-110-c01u02
arch:
  0: blackhole
  ...
```

It is available as `ClusterDescriptor::get_host_id()` (`Optional[str]` from Python), and is empty on any descriptor that predates the field or was never given one.

The value is currently that group's **bare metal hostname**, because the factory system descriptor (FSD) and the fabric topology solver join on hostnames. But the field identifies the accelerator group, not a machine — it is named `host_id` rather than `hostname` so the value scheme can change later without the field changing meaning.

## Why the variable exists

UMD falls back to `gethostname()`, which is only the right answer when the process runs on bare metal:

| Where the process runs | `gethostname()` returns | Usable as a host id? |
|---|---|---|
| Bare metal | the machine's hostname, e.g. `bh-glx-110-c01u02` | yes |
| Container | the container id, e.g. `7f3a91c2b4de` | **no** — changes every run, unrelated to the hardware |
| VM | whatever the guest was named, e.g. `pod-fabric-worker-3` | **no** — names the guest, not the accelerator group |

In the last two cases the value is stable enough to look convincing while pointing at nothing physical, which is worse than having no value at all: fabric would build a topology keyed on it and silently disagree with the FSD. Set `TT_HOST_ID` there to the id of the accelerator group the container or VM actually has.

## Resolution order

1. `TT_HOST_ID`, when set to a non-empty value.
2. Otherwise the OS hostname, stored raw (no FQDN stripping — consumers canonicalize).

```bash
# In a container that owns the accelerators of bh-glx-110-c01u02:
export TT_HOST_ID=bh-glx-110-c01u02
```

Read in exactly one place, `tt::umd::utils::local_host_id()`, which discovery calls. In particular the variable is **not** consulted when loading a cluster descriptor from YAML: that descriptor may describe a different machine, and substituting the local id would relabel it.

## Legal values

At most 63 characters (it has to fit a fixed 64-byte buffer that consumers pack it into, NUL included) and matching:

```
^[A-Za-z0-9]([A-Za-z0-9._-]*[A-Za-z0-9])?$
```

The charset is deliberately hostname-shaped while host ids are hostname-valued.

| Situation | Behavior |
|---|---|
| `TT_HOST_ID` unset | OS hostname is used |
| `TT_HOST_ID` set and legal | that value is used, logged at info |
| `TT_HOST_ID` set but empty or whitespace | treated as unset, warning logged — an exported-but-empty variable is a launcher accident |
| `TT_HOST_ID` set to an illegal value | **throws.** The variable was set on purpose; silently falling back to a container hostname would produce a wrong-but-plausible topology, which is the failure this variable exists to prevent |
| OS hostname unusable as a host id (over 63 characters, exotic characters) | warning logged, host id left unset — discovery still succeeds and consumers fall back to what they did before |
| `ClusterDescriptor::set_host_id()` with an illegal value | throws |

## Serialization

`serialize()` writes `host_id` first in the map, and omits the key entirely when the id is unset, so descriptors that never had one serialize byte-identically to before. A descriptor recaptured on hardware (`serialize_to_file`) carries the live host id for free.

Old UMD reading a YAML that has `host_id` ignores the unknown key, and new UMD reading a YAML without it leaves the field empty. Neither is an error, and the key is not in the schema's `required` list.
