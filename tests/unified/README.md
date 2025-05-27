## Perf measurement between ordered and unordered collections

Averaged out over 10^7 operations. all times in milliseconds

#### Accessing existing elements:
| Number of elements | Unordered set | Set     | Unordered map | Map     |
|--------------------|---------------|---------|---------------|---------|
|                  1 |           324 |     205 |           347 |     315 |
|                 10 |           337 |     442 |           330 |     729 |
|                100 |           337 |     971 |           329 |    1600 |
|               1000 |           332 |    1497 |           344 |    2536 |
|              10000 |           448 |    2344 |           521 |    4184 |

#### Accessing non-existing elements:
| Number of elements | Unordered set | Set     | Unordered map | Map     |
|--------------------|---------------|---------|---------------|---------|
|                  1 |           332 |     179 |           325 |     190 |
|                 10 |           450 |     479 |           450 |     426 |
|                100 |           462 |     993 |           456 |     935 |
|               1000 |           466 |    1503 |           461 |    1400 |
|              10000 |           537 |    2345 |           547 |    2398 |

#### Iterating over the whole collection:
| Number of elements | Unordered set | Set     | Unordered map | Map     |
|--------------------|---------------|---------|---------------|---------|
|                  1 |            38 |     282 |            38 |     307 |
|                 10 |            39 |     287 |            39 |     290 |
|                100 |           127 |     300 |           127 |     296 |
|               1000 |           160 |     416 |           156 |     384 |
|              10000 |           377 |     424 |           392 |     420 |
