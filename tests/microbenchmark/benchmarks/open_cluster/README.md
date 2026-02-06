# Cluster open benchmark

This benchmark contains tests that are measuring performance of opening/constructing Cluster object. Since work done in the cluster objects is non-trivial, it is important to measure how long it takes to open the cluster and how long it takes to construct the Cluster object. This should run on all our configurations.

# Example result

```
|          ns/cluster |           cluster/s |    err% |     total | ClusterConstructor
|--------------------:|--------------------:|--------:|----------:|:-------------------
|       16,093,794.00 |               62.14 |    0.6% |      0.17 | `default`
```