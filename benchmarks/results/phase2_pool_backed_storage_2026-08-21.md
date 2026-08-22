# Phase 2 Pool-backed Storage Baseline

Profile: `phase2_pool_backed_storage`

Boundary: public `MatchingEngine::process()` entry through return

Accepted canonical run: yes

The matching-core endpoint was not independently measured. Report-producing commands include outbox cursor publication; zero-event commands are process-completion measurements only.

## Method

Every command trace and bounded result buffer is constructed before timing. Each elapsed interval surrounds exactly one public `process()` call. Result checks, checksums, invariant queries, and execution-outbox draining occur after that interval; no repair command is included in a target latency. The mixed trace maintains 5,000--10,000 live orders with an exact 70/20/10 operation mix and at least 80% of priced volume near the top five levels. Unknown-ID paths are separate. Isolated cases restore the same target order state between calls with untimed cancellation, amendment, or repopulation. The multi-level case restores four orders at each of four prices outside timing, so every target really sweeps four levels.

## Environment

- Git: `87f92095fbe82e8d9b097dd45f37118da5e103fe` (dirty: yes)
- CPU:  Intel(R) Xeon(R) CPU E3-1230 V2 @ 3.30GHz
- Microcode:  0x21
- Kernel: Linux 6.1.0-52-amd64 x86_64
- Compiler: 12.2.0
- Flags: `-O3 -march=native -ffast-math -Wall -Wextra -Werror -std=c++20 -DNDEBUG`
- Affinity: 1
- SMT sibling: 1,5 (launcher_present_not_continuously_monitored)
- Governor/frequency: schedutil / 2879352 kHz
- NUMA node: 0
- Clock: `std::chrono::steady_clock` backed by monotonic clock; resolution 1 ns; median call-pair overhead 28 ns; overhead was not subtracted.
- Mode: acceptance, repetitions: 5

## Configured storage

- Active-order capacity: 131072
- Order node: 80 bytes, alignment 8 bytes
- Pool slot: 96 bytes, alignment 8 bytes
- Pool backing (slots plus free indexes): 13107200 bytes
  - Slot backing: 12582912 bytes
  - Free-index backing: 524288 bytes
- Active-ID table: 262144 buckets, 10485760 bytes
- Bid/ask price-level arrays: 589824 bytes
- Total configured storage backing: 24182784 bytes

Canonical audit construction totals (engine, storage, outboxes, and pre-sized sample buffers): `3180/9715208320/0` allocations/bytes/deallocations. Destruction totals: `0/0/3040`.

## Results

| Workload | Seed | Samples | p50 ns | p90 ns | p99 ns | p99.9 ns | p99.99 ns | Max ns | Throughput/s | Gate |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| mixed | 24301 | 1000000 | 643 | 1063 | 1276 | 1856 | 9999 | 28833 | 1312284 | pass |
| cancel | 24301 | 500000 | 678 | 683 | 752 | 1237 | 9580 | 20028 | 1459492 | pass |
| unknown_cancel | 24301 | 1000000 | 535 | 542 | 565 | 980 | 9402 | 19413 | 1851334 | pass |
| reduce | 24301 | 500000 | 658 | 666 | 726 | 1145 | 9531 | 22109 | 1504487 | pass |
| increase | 24301 | 500000 | 665 | 672 | 704 | 986 | 9566 | 22293 | 1494551 | pass |
| noop | 24301 | 1000000 | 547 | 554 | 573 | 864 | 9427 | 14395 | 1815375 | pass |
| unknown_amend | 24301 | 1000000 | 539 | 545 | 566 | 890 | 9402 | 25576 | 1841605 | pass |
| noncross_add | 24301 | 500000 | 962 | 977 | 1024 | 1590 | 9931 | 23292 | 1032036 | pass |
| fill1 | 24301 | 200000 | 1009 | 1021 | 1069 | 1861 | 10055 | 18118 | 981541 | pass |
| fill4 | 24301 | 50000 | 1248 | 1261 | 1303 | 2096 | 10208 | 17773 | 796654 | informational |
| fill16 | 24301 | 20000 | 2116 | 2142 | 2203 | 5212 | 11162 | 18946 | 470045 | informational |
| fill64 | 24301 | 5000 | 5369 | 5500 | 5550 | 8609 | 14143 | 14143 | 185222 | informational |
| fill256 | 24301 | 1000 | 17623 | 17738 | 19251 | 26791 | 26937 | 26937 | 56472 | informational |
| multi_level | 24301 | 20000 | 2163 | 2182 | 2217 | 3528 | 11022 | 13046 | 461005 | informational |
| mixed | 12648430 | 1000000 | 635 | 1039 | 1226 | 1366 | 5333 | 26551 | 1352890 | pass |
| cancel | 12648430 | 500000 | 679 | 684 | 749 | 1039 | 4271 | 17848 | 1466231 | pass |
| unknown_cancel | 12648430 | 1000000 | 536 | 542 | 551 | 899 | 4070 | 21448 | 1856256 | pass |
| reduce | 12648430 | 500000 | 664 | 671 | 728 | 1008 | 9568 | 18388 | 1496423 | pass |
| increase | 12648430 | 500000 | 664 | 670 | 685 | 995 | 4227 | 16470 | 1501502 | pass |
| noop | 12648430 | 1000000 | 551 | 555 | 563 | 911 | 3966 | 22779 | 1811204 | pass |
| unknown_amend | 12648430 | 1000000 | 542 | 547 | 556 | 910 | 3719 | 22732 | 1840147 | pass |
| noncross_add | 12648430 | 500000 | 969 | 979 | 1011 | 1494 | 9757 | 45865 | 1023918 | pass |
| fill1 | 12648430 | 200000 | 1008 | 1019 | 1029 | 1126 | 4854 | 15896 | 989233 | pass |
| fill4 | 12648430 | 50000 | 1228 | 1240 | 1262 | 1418 | 4691 | 13544 | 812521 | informational |
| fill16 | 12648430 | 20000 | 2102 | 2130 | 2188 | 2469 | 11099 | 12156 | 474045 | informational |
| fill64 | 12648430 | 5000 | 5314 | 5359 | 5561 | 8838 | 14832 | 14832 | 187633 | informational |
| fill256 | 12648430 | 1000 | 18590 | 18762 | 21332 | 27716 | 28787 | 28787 | 54331 | informational |
| multi_level | 12648430 | 20000 | 2142 | 2165 | 2236 | 3308 | 10755 | 11170 | 465657 | informational |

### Storage high-water evidence

| Workload | Seed | Pool | Bid levels | Ask levels |
| --- | ---: | ---: | ---: | ---: |
| mixed | 24301 | 6822 | 50 | 50 |
| cancel | 24301 | 1 | 1 | 0 |
| unknown_cancel | 24301 | 0 | 0 | 0 |
| reduce | 24301 | 1 | 1 | 0 |
| increase | 24301 | 1 | 1 | 0 |
| noop | 24301 | 1 | 1 | 0 |
| unknown_amend | 24301 | 0 | 0 | 0 |
| noncross_add | 24301 | 1 | 0 | 1 |
| fill1 | 24301 | 1 | 0 | 1 |
| fill4 | 24301 | 4 | 0 | 1 |
| fill16 | 24301 | 16 | 0 | 1 |
| fill64 | 24301 | 64 | 0 | 1 |
| fill256 | 24301 | 256 | 0 | 1 |
| multi_level | 24301 | 16 | 0 | 4 |
| mixed | 12648430 | 6110 | 50 | 50 |
| cancel | 12648430 | 1 | 1 | 0 |
| unknown_cancel | 12648430 | 0 | 0 | 0 |
| reduce | 12648430 | 1 | 1 | 0 |
| increase | 12648430 | 1 | 1 | 0 |
| noop | 12648430 | 1 | 1 | 0 |
| unknown_amend | 12648430 | 0 | 0 | 0 |
| noncross_add | 12648430 | 1 | 1 | 0 |
| fill1 | 12648430 | 1 | 0 | 1 |
| fill4 | 12648430 | 4 | 0 | 1 |
| fill16 | 12648430 | 16 | 0 | 1 |
| fill64 | 12648430 | 64 | 0 | 1 |
| fill256 | 12648430 | 256 | 0 | 1 |
| multi_level | 12648430 | 16 | 0 | 4 |

### Per-repetition evidence

| Workload | Seed | Rep | p50 | p90 | p99 | p99.9 | p99.99 | Max | Throughput/s | Public gate | Core upper-bound | Multi-fill upper-bound | Checksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- | ---: |
| mixed | 24301 | 0 | 653 | 1092 | 1722 | 2663 | 10224 | 29529 | 1247124 | pass | miss | not applicable | 11976050521989200683 |
| mixed | 24301 | 1 | 652 | 1069 | 1287 | 2844 | 10106 | 2789633 | 1285231 | pass | miss | not applicable | 11976050521989200683 |
| mixed | 24301 | 2 | 640 | 1056 | 1255 | 1632 | 9771 | 18717 | 1329066 | pass | miss | not applicable | 11976050521989200683 |
| mixed | 24301 | 3 | 640 | 1057 | 1255 | 1641 | 9817 | 26083 | 1326972 | pass | miss | not applicable | 11976050521989200683 |
| mixed | 24301 | 4 | 643 | 1063 | 1276 | 1856 | 9999 | 28833 | 1312284 | pass | miss | not applicable | 11976050521989200683 |
| cancel | 24301 | 0 | 678 | 685 | 762 | 1293 | 9652 | 14923 | 1454643 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 24301 | 1 | 678 | 682 | 747 | 1079 | 9514 | 142295 | 1465112 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 24301 | 2 | 678 | 687 | 752 | 1237 | 9613 | 17750 | 1457955 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 24301 | 3 | 678 | 683 | 750 | 1194 | 9571 | 20028 | 1462622 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 24301 | 4 | 678 | 683 | 752 | 1266 | 9580 | 22337 | 1459492 | pass | miss | not applicable | 1708398209335453120 |
| unknown_cancel | 24301 | 0 | 535 | 542 | 554 | 891 | 9393 | 13538 | 1853809 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 24301 | 1 | 535 | 543 | 566 | 995 | 9412 | 2775657 | 1838409 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 24301 | 2 | 535 | 542 | 554 | 884 | 9382 | 20422 | 1853551 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 24301 | 3 | 535 | 542 | 565 | 980 | 9402 | 14678 | 1851334 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 24301 | 4 | 535 | 542 | 566 | 1040 | 9417 | 19413 | 1849336 | pass | miss | not applicable | 6645398710458996416 |
| reduce | 24301 | 0 | 658 | 666 | 724 | 1066 | 9552 | 21943 | 1506642 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 24301 | 1 | 658 | 666 | 728 | 1165 | 9542 | 22109 | 1504487 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 24301 | 2 | 658 | 666 | 726 | 1145 | 9523 | 2770370 | 1493248 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 24301 | 3 | 658 | 666 | 729 | 1217 | 9531 | 35042 | 1503369 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 24301 | 4 | 658 | 666 | 709 | 1016 | 9530 | 17703 | 1507452 | pass | miss | not applicable | 1708398209335453120 |
| increase | 24301 | 0 | 665 | 672 | 709 | 1018 | 9568 | 24565 | 1493532 | pass | miss | not applicable | 1708398209335453120 |
| increase | 24301 | 1 | 665 | 672 | 702 | 961 | 9546 | 22214 | 1494747 | pass | miss | not applicable | 1708398209335453120 |
| increase | 24301 | 2 | 665 | 672 | 703 | 986 | 9566 | 12502 | 1494551 | pass | miss | not applicable | 1708398209335453120 |
| increase | 24301 | 3 | 665 | 672 | 704 | 971 | 9564 | 23361 | 1494873 | pass | miss | not applicable | 1708398209335453120 |
| increase | 24301 | 4 | 665 | 672 | 710 | 1111 | 9590 | 22293 | 1491327 | pass | miss | not applicable | 1708398209335453120 |
| noop | 24301 | 0 | 547 | 554 | 572 | 716 | 9429 | 14395 | 1816615 | pass | miss | not applicable | 4872455860064465152 |
| noop | 24301 | 1 | 547 | 553 | 581 | 897 | 9427 | 16771 | 1815054 | pass | miss | not applicable | 4872455860064465152 |
| noop | 24301 | 2 | 547 | 554 | 575 | 864 | 9402 | 13614 | 1815375 | pass | miss | not applicable | 4872455860064465152 |
| noop | 24301 | 3 | 548 | 554 | 573 | 875 | 9440 | 14544 | 1814080 | pass | miss | not applicable | 4872455860064465152 |
| noop | 24301 | 4 | 547 | 553 | 572 | 757 | 9414 | 13330 | 1818138 | pass | miss | not applicable | 4872455860064465152 |
| unknown_amend | 24301 | 0 | 539 | 545 | 566 | 817 | 9393 | 19685 | 1842555 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 24301 | 1 | 539 | 545 | 566 | 954 | 9418 | 2762951 | 1831286 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 24301 | 2 | 539 | 545 | 567 | 890 | 9402 | 36956 | 1841605 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 24301 | 3 | 539 | 546 | 571 | 907 | 9450 | 25576 | 1839545 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 24301 | 4 | 539 | 545 | 566 | 790 | 9373 | 22194 | 1844958 | pass | miss | not applicable | 6645398710458996416 |
| noncross_add | 24301 | 0 | 962 | 977 | 1024 | 1599 | 9926 | 16222 | 1032036 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 24301 | 1 | 962 | 977 | 1024 | 1536 | 9931 | 2785603 | 1025435 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 24301 | 2 | 961 | 976 | 1020 | 1590 | 9985 | 18211 | 1032225 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 24301 | 3 | 962 | 976 | 1019 | 1460 | 9892 | 23292 | 1032616 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 24301 | 4 | 979 | 1015 | 1689 | 9729 | 11062 | 26135 | 987584 | pass | miss | not applicable | 14680700484052686336 |
| fill1 | 24301 | 0 | 1006 | 1020 | 1129 | 2061 | 10055 | 26584 | 980166 | pass | miss | pass | 4209633463992593536 |
| fill1 | 24301 | 1 | 1007 | 1026 | 1069 | 1861 | 10014 | 16090 | 981541 | pass | miss | pass | 4209633463992593536 |
| fill1 | 24301 | 2 | 1013 | 1394 | 1779 | 4966 | 12538 | 184776 | 893787 | pass | miss | miss | 4209633463992593536 |
| fill1 | 24301 | 3 | 1009 | 1020 | 1042 | 1846 | 10099 | 17049 | 982117 | pass | miss | pass | 4209633463992593536 |
| fill1 | 24301 | 4 | 1010 | 1021 | 1042 | 1405 | 9981 | 18118 | 984223 | pass | miss | pass | 4209633463992593536 |
| fill4 | 24301 | 0 | 1248 | 1284 | 1333 | 6559 | 10749 | 17773 | 790399 | not applicable | miss | pass | 18118962843518811632 |
| fill4 | 24301 | 1 | 1248 | 1264 | 1303 | 3553 | 10239 | 11032 | 794756 | not applicable | miss | pass | 18118962843518811632 |
| fill4 | 24301 | 2 | 1248 | 1261 | 1279 | 1447 | 4483 | 19291 | 799559 | not applicable | miss | pass | 18118962843518811632 |
| fill4 | 24301 | 3 | 1246 | 1261 | 1301 | 1933 | 10099 | 13885 | 798041 | not applicable | miss | pass | 18118962843518811632 |
| fill4 | 24301 | 4 | 1246 | 1261 | 1306 | 2096 | 10208 | 19296 | 796654 | not applicable | miss | pass | 18118962843518811632 |
| fill16 | 24301 | 0 | 2117 | 2142 | 2203 | 3439 | 10956 | 18946 | 470782 | not applicable | miss | pass | 4870814945307920736 |
| fill16 | 24301 | 1 | 2116 | 2138 | 2194 | 3397 | 10969 | 18426 | 471252 | not applicable | miss | pass | 4870814945307920736 |
| fill16 | 24301 | 2 | 2116 | 2143 | 2211 | 5212 | 12159 | 19702 | 469901 | not applicable | miss | pass | 4870814945307920736 |
| fill16 | 24301 | 3 | 2116 | 2142 | 2201 | 6288 | 11162 | 18184 | 470045 | not applicable | miss | pass | 4870814945307920736 |
| fill16 | 24301 | 4 | 2116 | 2143 | 2223 | 10507 | 12095 | 20174 | 468944 | not applicable | miss | pass | 4870814945307920736 |
| fill64 | 24301 | 0 | 5369 | 5500 | 5572 | 10384 | 14542 | 14542 | 185019 | not applicable | miss | pass | 16852925845242151096 |
| fill64 | 24301 | 1 | 5369 | 5497 | 5543 | 8638 | 14211 | 14211 | 185222 | not applicable | miss | pass | 16852925845242151096 |
| fill64 | 24301 | 2 | 5368 | 5495 | 5536 | 8585 | 10331 | 10331 | 185433 | not applicable | miss | pass | 16852925845242151096 |
| fill64 | 24301 | 3 | 5370 | 5505 | 5648 | 8609 | 14143 | 14143 | 185074 | not applicable | miss | pass | 16852925845242151096 |
| fill64 | 24301 | 4 | 5369 | 5500 | 5550 | 7917 | 9457 | 9457 | 185334 | not applicable | miss | pass | 16852925845242151096 |
| fill256 | 24301 | 0 | 17619 | 17735 | 20634 | 26616 | 26703 | 26703 | 56472 | not applicable | miss | pass | 11418948980634061528 |
| fill256 | 24301 | 1 | 17623 | 17738 | 18843 | 23187 | 25485 | 25485 | 56560 | not applicable | miss | pass | 11418948980634061528 |
| fill256 | 24301 | 2 | 17614 | 17729 | 19570 | 26791 | 26937 | 26937 | 56482 | not applicable | miss | pass | 11418948980634061528 |
| fill256 | 24301 | 3 | 17624 | 17757 | 19251 | 28197 | 30268 | 30268 | 56363 | not applicable | miss | pass | 11418948980634061528 |
| fill256 | 24301 | 4 | 17623 | 17780 | 19174 | 26823 | 27199 | 27199 | 56400 | not applicable | miss | pass | 11418948980634061528 |
| multi_level | 24301 | 0 | 2163 | 2182 | 2214 | 3419 | 11022 | 19505 | 461079 | not applicable | miss | pass | 1168931345846841760 |
| multi_level | 24301 | 1 | 2163 | 2184 | 2298 | 5381 | 12132 | 14312 | 459311 | not applicable | miss | pass | 1168931345846841760 |
| multi_level | 24301 | 2 | 2163 | 2183 | 2239 | 3937 | 11071 | 13046 | 460549 | not applicable | miss | pass | 1168931345846841760 |
| multi_level | 24301 | 3 | 2163 | 2182 | 2217 | 3528 | 10914 | 12042 | 461005 | not applicable | miss | pass | 1168931345846841760 |
| multi_level | 24301 | 4 | 2163 | 2182 | 2216 | 3357 | 10828 | 11303 | 461214 | not applicable | miss | pass | 1168931345846841760 |
| mixed | 12648430 | 0 | 638 | 1043 | 1229 | 1369 | 5290 | 144677 | 1347364 | pass | miss | not applicable | 3435255765795108418 |
| mixed | 12648430 | 1 | 635 | 1039 | 1226 | 1364 | 5289 | 16598 | 1353551 | pass | miss | not applicable | 3435255765795108418 |
| mixed | 12648430 | 2 | 635 | 1039 | 1226 | 1370 | 5867 | 26551 | 1352890 | pass | miss | not applicable | 3435255765795108418 |
| mixed | 12648430 | 3 | 635 | 1039 | 1226 | 1366 | 5406 | 23755 | 1353773 | pass | miss | not applicable | 3435255765795108418 |
| mixed | 12648430 | 4 | 635 | 1038 | 1226 | 1361 | 5333 | 2782826 | 1348778 | pass | miss | not applicable | 3435255765795108418 |
| cancel | 12648430 | 0 | 679 | 684 | 749 | 1032 | 4267 | 19203 | 1466627 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 12648430 | 1 | 679 | 684 | 749 | 1048 | 4502 | 17329 | 1466086 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 12648430 | 2 | 679 | 684 | 749 | 1039 | 4271 | 17848 | 1466231 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 12648430 | 3 | 679 | 683 | 749 | 1028 | 3813 | 16950 | 1467279 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 12648430 | 4 | 679 | 685 | 752 | 1149 | 9561 | 20092 | 1459499 | pass | miss | not applicable | 1708398209335453120 |
| unknown_cancel | 12648430 | 0 | 536 | 542 | 552 | 905 | 4020 | 2778972 | 1846347 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 12648430 | 1 | 536 | 542 | 550 | 901 | 3976 | 18102 | 1857040 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 12648430 | 2 | 536 | 542 | 553 | 891 | 4118 | 21448 | 1856256 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 12648430 | 3 | 536 | 542 | 551 | 899 | 4440 | 25452 | 1856010 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 12648430 | 4 | 536 | 542 | 551 | 894 | 4070 | 18444 | 1856496 | pass | miss | not applicable | 6645398710458996416 |
| reduce | 12648430 | 0 | 663 | 669 | 724 | 987 | 3526 | 15805 | 1503865 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 12648430 | 1 | 664 | 672 | 738 | 1269 | 9609 | 19323 | 1490608 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 12648430 | 2 | 664 | 671 | 723 | 1002 | 9568 | 24171 | 1496423 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 12648430 | 3 | 663 | 670 | 730 | 1142 | 9491 | 18388 | 1498906 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 12648430 | 4 | 664 | 672 | 728 | 1008 | 9572 | 15151 | 1495822 | pass | miss | not applicable | 1708398209335453120 |
| increase | 12648430 | 0 | 664 | 670 | 712 | 1124 | 9425 | 14072 | 1498776 | pass | miss | not applicable | 1708398209335453120 |
| increase | 12648430 | 1 | 664 | 671 | 724 | 1144 | 4192 | 16470 | 1498458 | pass | miss | not applicable | 1708398209335453120 |
| increase | 12648430 | 2 | 664 | 670 | 685 | 983 | 5314 | 22261 | 1501502 | pass | miss | not applicable | 1708398209335453120 |
| increase | 12648430 | 3 | 663 | 669 | 679 | 993 | 4017 | 11519 | 1503357 | pass | miss | not applicable | 1708398209335453120 |
| increase | 12648430 | 4 | 663 | 669 | 678 | 995 | 4227 | 20702 | 1503353 | pass | miss | not applicable | 1708398209335453120 |
| noop | 12648430 | 0 | 551 | 555 | 564 | 924 | 3977 | 25068 | 1811156 | pass | miss | not applicable | 4872455860064465152 |
| noop | 12648430 | 1 | 551 | 555 | 562 | 906 | 3863 | 15546 | 1812546 | pass | miss | not applicable | 4872455860064465152 |
| noop | 12648430 | 2 | 551 | 555 | 563 | 911 | 3966 | 22779 | 1811204 | pass | miss | not applicable | 4872455860064465152 |
| noop | 12648430 | 3 | 551 | 555 | 559 | 908 | 3353 | 2776642 | 1803576 | pass | miss | not applicable | 4872455860064465152 |
| noop | 12648430 | 4 | 551 | 555 | 563 | 915 | 3986 | 22561 | 1812007 | pass | miss | not applicable | 4872455860064465152 |
| unknown_amend | 12648430 | 0 | 542 | 547 | 557 | 907 | 3579 | 14039 | 1840147 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 12648430 | 1 | 542 | 547 | 556 | 910 | 3719 | 49161 | 1840189 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 12648430 | 2 | 542 | 547 | 551 | 895 | 3569 | 22732 | 1840800 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 12648430 | 3 | 542 | 547 | 558 | 934 | 4153 | 17353 | 1838635 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 12648430 | 4 | 542 | 547 | 552 | 911 | 3754 | 2779481 | 1830628 | pass | miss | not applicable | 6645398710458996416 |
| noncross_add | 12648430 | 0 | 964 | 975 | 1011 | 1494 | 9757 | 19322 | 1032283 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 12648430 | 1 | 1463 | 1993 | 2694 | 3843 | 15290 | 61724 | 671197 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 12648430 | 2 | 970 | 1633 | 2482 | 2958 | 10335 | 45865 | 904105 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 12648430 | 3 | 969 | 979 | 996 | 1157 | 4550 | 14015 | 1029450 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 12648430 | 4 | 968 | 978 | 999 | 1135 | 9640 | 2764385 | 1023918 | pass | miss | not applicable | 14680700484052686336 |
| fill1 | 12648430 | 0 | 1008 | 1020 | 1035 | 1126 | 4854 | 15143 | 989233 | pass | miss | pass | 9694914282976820864 |
| fill1 | 12648430 | 1 | 1009 | 1019 | 1029 | 1127 | 9714 | 14039 | 988574 | pass | miss | pass | 9694914282976820864 |
| fill1 | 12648430 | 2 | 1008 | 1019 | 1028 | 1085 | 4646 | 20783 | 989527 | pass | miss | pass | 9694914282976820864 |
| fill1 | 12648430 | 3 | 1007 | 1018 | 1034 | 1137 | 9747 | 15896 | 989771 | pass | miss | pass | 9694914282976820864 |
| fill1 | 12648430 | 4 | 1009 | 1020 | 1029 | 1085 | 4355 | 20666 | 989029 | pass | miss | pass | 9694914282976820864 |
| fill4 | 12648430 | 0 | 1228 | 1240 | 1267 | 1529 | 10194 | 13544 | 811691 | not applicable | miss | pass | 10283548236130005040 |
| fill4 | 12648430 | 1 | 1228 | 1240 | 1263 | 1418 | 4481 | 18878 | 812890 | not applicable | miss | pass | 10283548236130005040 |
| fill4 | 12648430 | 2 | 1228 | 1240 | 1261 | 1525 | 6100 | 13232 | 812725 | not applicable | miss | pass | 10283548236130005040 |
| fill4 | 12648430 | 3 | 1229 | 1241 | 1261 | 1418 | 4598 | 18726 | 812356 | not applicable | miss | pass | 10283548236130005040 |
| fill4 | 12648430 | 4 | 1229 | 1241 | 1262 | 1382 | 4691 | 13202 | 812521 | not applicable | miss | pass | 10283548236130005040 |
| fill16 | 12648430 | 0 | 2103 | 2137 | 2237 | 3967 | 11099 | 24050 | 472320 | not applicable | miss | pass | 3003463614242969760 |
| fill16 | 12648430 | 1 | 2102 | 2126 | 2169 | 2469 | 6055 | 11061 | 474861 | not applicable | miss | pass | 3003463614242969760 |
| fill16 | 12648430 | 2 | 2102 | 2127 | 2176 | 2453 | 10909 | 11056 | 474453 | not applicable | miss | pass | 3003463614242969760 |
| fill16 | 12648430 | 3 | 2102 | 2130 | 2188 | 2448 | 11391 | 16507 | 474045 | not applicable | miss | pass | 3003463614242969760 |
| fill16 | 12648430 | 4 | 2103 | 2133 | 2194 | 3539 | 11958 | 12156 | 473539 | not applicable | miss | pass | 3003463614242969760 |
| fill64 | 12648430 | 0 | 5313 | 5351 | 5523 | 8838 | 13138 | 13138 | 187740 | not applicable | miss | pass | 8101109951146213944 |
| fill64 | 12648430 | 1 | 5316 | 5359 | 5558 | 8264 | 18195 | 18195 | 187633 | not applicable | miss | pass | 8101109951146213944 |
| fill64 | 12648430 | 2 | 5314 | 5364 | 5617 | 14342 | 15409 | 15409 | 186983 | not applicable | miss | pass | 8101109951146213944 |
| fill64 | 12648430 | 3 | 5313 | 5350 | 5561 | 8509 | 9593 | 9593 | 187735 | not applicable | miss | pass | 8101109951146213944 |
| fill64 | 12648430 | 4 | 5314 | 5361 | 5580 | 10606 | 14832 | 14832 | 187338 | not applicable | miss | pass | 8101109951146213944 |
| fill256 | 12648430 | 0 | 18578 | 18762 | 19389 | 23814 | 23850 | 23850 | 54426 | not applicable | miss | pass | 6490150999850096856 |
| fill256 | 12648430 | 1 | 18590 | 18762 | 20747 | 27716 | 28787 | 28787 | 54331 | not applicable | miss | pass | 6490150999850096856 |
| fill256 | 12648430 | 2 | 18584 | 18760 | 21459 | 24835 | 27767 | 27767 | 54379 | not applicable | miss | pass | 6490150999850096856 |
| fill256 | 12648430 | 3 | 18610 | 18767 | 21332 | 35376 | 43246 | 43246 | 54167 | not applicable | miss | pass | 6490150999850096856 |
| fill256 | 12648430 | 4 | 18590 | 18754 | 23308 | 35206 | 36558 | 36558 | 54193 | not applicable | miss | pass | 6490150999850096856 |
| multi_level | 12648430 | 0 | 2144 | 2182 | 2312 | 10766 | 11458 | 14278 | 462245 | not applicable | miss | pass | 4833899198899577056 |
| multi_level | 12648430 | 1 | 2142 | 2165 | 2236 | 3308 | 10755 | 11170 | 465657 | not applicable | miss | pass | 4833899198899577056 |
| multi_level | 12648430 | 2 | 2143 | 2169 | 2265 | 5339 | 11064 | 11990 | 464213 | not applicable | miss | pass | 4833899198899577056 |
| multi_level | 12648430 | 3 | 2142 | 2165 | 2234 | 3286 | 6443 | 11019 | 465832 | not applicable | miss | pass | 4833899198899577056 |
| multi_level | 12648430 | 4 | 2142 | 2164 | 2229 | 3306 | 6505 | 7011 | 466093 | not applicable | miss | pass | 4833899198899577056 |

Nearest-rank percentiles are computed independently per repetition; the table reports the median of repetition metrics. At least four of five repetitions must pass each applicable public-path gate. Multi-fill matching-core ceilings are informational conservative evidence only.

## Allocation classification

Global allocation overrides were enabled only in the separate audit executable. Strict validity requires both timed public `process()` and timed sample/checksum collection to report zero allocations, allocated bytes, and deallocations in every repetition. Order FIFOs, active IDs, and price levels are all startup-backed bounded storage.

| Workload | Seed | Repetition | Timed process allocs/bytes/frees | Timed collection allocs/bytes/frees |
| --- | ---: | ---: | ---: | ---: |
| mixed | 24301 | 0 | 0/0/0 | 0/0/0 |
| mixed | 24301 | 1 | 0/0/0 | 0/0/0 |
| mixed | 24301 | 2 | 0/0/0 | 0/0/0 |
| mixed | 24301 | 3 | 0/0/0 | 0/0/0 |
| mixed | 24301 | 4 | 0/0/0 | 0/0/0 |
| cancel | 24301 | 0 | 0/0/0 | 0/0/0 |
| cancel | 24301 | 1 | 0/0/0 | 0/0/0 |
| cancel | 24301 | 2 | 0/0/0 | 0/0/0 |
| cancel | 24301 | 3 | 0/0/0 | 0/0/0 |
| cancel | 24301 | 4 | 0/0/0 | 0/0/0 |
| unknown_cancel | 24301 | 0 | 0/0/0 | 0/0/0 |
| unknown_cancel | 24301 | 1 | 0/0/0 | 0/0/0 |
| unknown_cancel | 24301 | 2 | 0/0/0 | 0/0/0 |
| unknown_cancel | 24301 | 3 | 0/0/0 | 0/0/0 |
| unknown_cancel | 24301 | 4 | 0/0/0 | 0/0/0 |
| reduce | 24301 | 0 | 0/0/0 | 0/0/0 |
| reduce | 24301 | 1 | 0/0/0 | 0/0/0 |
| reduce | 24301 | 2 | 0/0/0 | 0/0/0 |
| reduce | 24301 | 3 | 0/0/0 | 0/0/0 |
| reduce | 24301 | 4 | 0/0/0 | 0/0/0 |
| increase | 24301 | 0 | 0/0/0 | 0/0/0 |
| increase | 24301 | 1 | 0/0/0 | 0/0/0 |
| increase | 24301 | 2 | 0/0/0 | 0/0/0 |
| increase | 24301 | 3 | 0/0/0 | 0/0/0 |
| increase | 24301 | 4 | 0/0/0 | 0/0/0 |
| noop | 24301 | 0 | 0/0/0 | 0/0/0 |
| noop | 24301 | 1 | 0/0/0 | 0/0/0 |
| noop | 24301 | 2 | 0/0/0 | 0/0/0 |
| noop | 24301 | 3 | 0/0/0 | 0/0/0 |
| noop | 24301 | 4 | 0/0/0 | 0/0/0 |
| unknown_amend | 24301 | 0 | 0/0/0 | 0/0/0 |
| unknown_amend | 24301 | 1 | 0/0/0 | 0/0/0 |
| unknown_amend | 24301 | 2 | 0/0/0 | 0/0/0 |
| unknown_amend | 24301 | 3 | 0/0/0 | 0/0/0 |
| unknown_amend | 24301 | 4 | 0/0/0 | 0/0/0 |
| noncross_add | 24301 | 0 | 0/0/0 | 0/0/0 |
| noncross_add | 24301 | 1 | 0/0/0 | 0/0/0 |
| noncross_add | 24301 | 2 | 0/0/0 | 0/0/0 |
| noncross_add | 24301 | 3 | 0/0/0 | 0/0/0 |
| noncross_add | 24301 | 4 | 0/0/0 | 0/0/0 |
| fill1 | 24301 | 0 | 0/0/0 | 0/0/0 |
| fill1 | 24301 | 1 | 0/0/0 | 0/0/0 |
| fill1 | 24301 | 2 | 0/0/0 | 0/0/0 |
| fill1 | 24301 | 3 | 0/0/0 | 0/0/0 |
| fill1 | 24301 | 4 | 0/0/0 | 0/0/0 |
| fill4 | 24301 | 0 | 0/0/0 | 0/0/0 |
| fill4 | 24301 | 1 | 0/0/0 | 0/0/0 |
| fill4 | 24301 | 2 | 0/0/0 | 0/0/0 |
| fill4 | 24301 | 3 | 0/0/0 | 0/0/0 |
| fill4 | 24301 | 4 | 0/0/0 | 0/0/0 |
| fill16 | 24301 | 0 | 0/0/0 | 0/0/0 |
| fill16 | 24301 | 1 | 0/0/0 | 0/0/0 |
| fill16 | 24301 | 2 | 0/0/0 | 0/0/0 |
| fill16 | 24301 | 3 | 0/0/0 | 0/0/0 |
| fill16 | 24301 | 4 | 0/0/0 | 0/0/0 |
| fill64 | 24301 | 0 | 0/0/0 | 0/0/0 |
| fill64 | 24301 | 1 | 0/0/0 | 0/0/0 |
| fill64 | 24301 | 2 | 0/0/0 | 0/0/0 |
| fill64 | 24301 | 3 | 0/0/0 | 0/0/0 |
| fill64 | 24301 | 4 | 0/0/0 | 0/0/0 |
| fill256 | 24301 | 0 | 0/0/0 | 0/0/0 |
| fill256 | 24301 | 1 | 0/0/0 | 0/0/0 |
| fill256 | 24301 | 2 | 0/0/0 | 0/0/0 |
| fill256 | 24301 | 3 | 0/0/0 | 0/0/0 |
| fill256 | 24301 | 4 | 0/0/0 | 0/0/0 |
| multi_level | 24301 | 0 | 0/0/0 | 0/0/0 |
| multi_level | 24301 | 1 | 0/0/0 | 0/0/0 |
| multi_level | 24301 | 2 | 0/0/0 | 0/0/0 |
| multi_level | 24301 | 3 | 0/0/0 | 0/0/0 |
| multi_level | 24301 | 4 | 0/0/0 | 0/0/0 |
| mixed | 12648430 | 0 | 0/0/0 | 0/0/0 |
| mixed | 12648430 | 1 | 0/0/0 | 0/0/0 |
| mixed | 12648430 | 2 | 0/0/0 | 0/0/0 |
| mixed | 12648430 | 3 | 0/0/0 | 0/0/0 |
| mixed | 12648430 | 4 | 0/0/0 | 0/0/0 |
| cancel | 12648430 | 0 | 0/0/0 | 0/0/0 |
| cancel | 12648430 | 1 | 0/0/0 | 0/0/0 |
| cancel | 12648430 | 2 | 0/0/0 | 0/0/0 |
| cancel | 12648430 | 3 | 0/0/0 | 0/0/0 |
| cancel | 12648430 | 4 | 0/0/0 | 0/0/0 |
| unknown_cancel | 12648430 | 0 | 0/0/0 | 0/0/0 |
| unknown_cancel | 12648430 | 1 | 0/0/0 | 0/0/0 |
| unknown_cancel | 12648430 | 2 | 0/0/0 | 0/0/0 |
| unknown_cancel | 12648430 | 3 | 0/0/0 | 0/0/0 |
| unknown_cancel | 12648430 | 4 | 0/0/0 | 0/0/0 |
| reduce | 12648430 | 0 | 0/0/0 | 0/0/0 |
| reduce | 12648430 | 1 | 0/0/0 | 0/0/0 |
| reduce | 12648430 | 2 | 0/0/0 | 0/0/0 |
| reduce | 12648430 | 3 | 0/0/0 | 0/0/0 |
| reduce | 12648430 | 4 | 0/0/0 | 0/0/0 |
| increase | 12648430 | 0 | 0/0/0 | 0/0/0 |
| increase | 12648430 | 1 | 0/0/0 | 0/0/0 |
| increase | 12648430 | 2 | 0/0/0 | 0/0/0 |
| increase | 12648430 | 3 | 0/0/0 | 0/0/0 |
| increase | 12648430 | 4 | 0/0/0 | 0/0/0 |
| noop | 12648430 | 0 | 0/0/0 | 0/0/0 |
| noop | 12648430 | 1 | 0/0/0 | 0/0/0 |
| noop | 12648430 | 2 | 0/0/0 | 0/0/0 |
| noop | 12648430 | 3 | 0/0/0 | 0/0/0 |
| noop | 12648430 | 4 | 0/0/0 | 0/0/0 |
| unknown_amend | 12648430 | 0 | 0/0/0 | 0/0/0 |
| unknown_amend | 12648430 | 1 | 0/0/0 | 0/0/0 |
| unknown_amend | 12648430 | 2 | 0/0/0 | 0/0/0 |
| unknown_amend | 12648430 | 3 | 0/0/0 | 0/0/0 |
| unknown_amend | 12648430 | 4 | 0/0/0 | 0/0/0 |
| noncross_add | 12648430 | 0 | 0/0/0 | 0/0/0 |
| noncross_add | 12648430 | 1 | 0/0/0 | 0/0/0 |
| noncross_add | 12648430 | 2 | 0/0/0 | 0/0/0 |
| noncross_add | 12648430 | 3 | 0/0/0 | 0/0/0 |
| noncross_add | 12648430 | 4 | 0/0/0 | 0/0/0 |
| fill1 | 12648430 | 0 | 0/0/0 | 0/0/0 |
| fill1 | 12648430 | 1 | 0/0/0 | 0/0/0 |
| fill1 | 12648430 | 2 | 0/0/0 | 0/0/0 |
| fill1 | 12648430 | 3 | 0/0/0 | 0/0/0 |
| fill1 | 12648430 | 4 | 0/0/0 | 0/0/0 |
| fill4 | 12648430 | 0 | 0/0/0 | 0/0/0 |
| fill4 | 12648430 | 1 | 0/0/0 | 0/0/0 |
| fill4 | 12648430 | 2 | 0/0/0 | 0/0/0 |
| fill4 | 12648430 | 3 | 0/0/0 | 0/0/0 |
| fill4 | 12648430 | 4 | 0/0/0 | 0/0/0 |
| fill16 | 12648430 | 0 | 0/0/0 | 0/0/0 |
| fill16 | 12648430 | 1 | 0/0/0 | 0/0/0 |
| fill16 | 12648430 | 2 | 0/0/0 | 0/0/0 |
| fill16 | 12648430 | 3 | 0/0/0 | 0/0/0 |
| fill16 | 12648430 | 4 | 0/0/0 | 0/0/0 |
| fill64 | 12648430 | 0 | 0/0/0 | 0/0/0 |
| fill64 | 12648430 | 1 | 0/0/0 | 0/0/0 |
| fill64 | 12648430 | 2 | 0/0/0 | 0/0/0 |
| fill64 | 12648430 | 3 | 0/0/0 | 0/0/0 |
| fill64 | 12648430 | 4 | 0/0/0 | 0/0/0 |
| fill256 | 12648430 | 0 | 0/0/0 | 0/0/0 |
| fill256 | 12648430 | 1 | 0/0/0 | 0/0/0 |
| fill256 | 12648430 | 2 | 0/0/0 | 0/0/0 |
| fill256 | 12648430 | 3 | 0/0/0 | 0/0/0 |
| fill256 | 12648430 | 4 | 0/0/0 | 0/0/0 |
| multi_level | 12648430 | 0 | 0/0/0 | 0/0/0 |
| multi_level | 12648430 | 1 | 0/0/0 | 0/0/0 |
| multi_level | 12648430 | 2 | 0/0/0 | 0/0/0 |
| multi_level | 12648430 | 3 | 0/0/0 | 0/0/0 |
| multi_level | 12648430 | 4 | 0/0/0 | 0/0/0 |

Trace construction, sample-buffer setup, initial population, warm-up, timed collection, and post-run statistics are separate allocation phases in the machine-readable artifact. Trace generation and all serialization occur outside timing.

## Reproduction

```bash
cmake --preset release
cmake --build --preset release --target benchmarks
./build/release/benchmarks/lob_phase2_pool_allocation_audit --mode acceptance --workload all --seeds 24301,12648430 --repetitions 5 --warmup 10000 --cpu 1 --allocation-output <path>
./build/release/benchmarks/lob_phase2_pool_benchmark --mode acceptance --workload all --seeds 24301,12648430 --repetitions 5 --warmup 10000 --cpu 1 --sibling-occupancy launcher_present_not_continuously_monitored --allocation-input <path> --json <path> --report <path>
```

## Retained Phase 1 comparison

Baseline: `phase1_allocating_storage_2026-08-17`

| Workload | Seed | Public path | Multi-fill | Allocation | Validity |
| --- | ---: | --- | --- | --- | --- |
| mixed | 24301 | pass | not_applicable | pass | pass |
| cancel | 24301 | pass | not_applicable | pass | pass |
| unknown_cancel | 24301 | pass | not_applicable | pass | pass |
| reduce | 24301 | pass | not_applicable | pass | pass |
| increase | 24301 | pass | not_applicable | pass | pass |
| noop | 24301 | pass | not_applicable | pass | pass |
| unknown_amend | 24301 | pass | not_applicable | pass | pass |
| noncross_add | 24301 | pass | not_applicable | pass | pass |
| fill1 | 24301 | pass | pass | pass | pass |
| fill4 | 24301 | informational | pass | pass | pass |
| fill16 | 24301 | informational | pass | pass | pass |
| fill64 | 24301 | informational | pass | pass | pass |
| fill256 | 24301 | informational | pass | pass | pass |
| multi_level | 24301 | informational | pass | pass | pass |
| mixed | 12648430 | pass | not_applicable | pass | pass |
| cancel | 12648430 | pass | not_applicable | pass | pass |
| unknown_cancel | 12648430 | pass | not_applicable | pass | pass |
| reduce | 12648430 | pass | not_applicable | pass | pass |
| increase | 12648430 | pass | not_applicable | pass | pass |
| noop | 12648430 | pass | not_applicable | pass | pass |
| unknown_amend | 12648430 | pass | not_applicable | pass | pass |
| noncross_add | 12648430 | pass | not_applicable | pass | pass |
| fill1 | 12648430 | pass | pass | pass | pass |
| fill4 | 12648430 | informational | pass | pass | pass |
| fill16 | 12648430 | informational | pass | pass | pass |
| fill64 | 12648430 | informational | pass | pass | pass |
| fill256 | 12648430 | informational | pass | pass | pass |
| multi_level | 12648430 | informational | pass | pass | pass |

| Workload | Seed | Metric | Before | After | Absolute | Percent | Gate |
| --- | ---: | --- | ---: | ---: | ---: | ---: | --- |
| mixed | 24301 | p50_ns | 691.000 | 643.000 | -48.000 | -6.946% | informational |
| mixed | 24301 | p90_ns | 1056.000 | 1063.000 | 7.000 | 0.663% | informational |
| mixed | 24301 | p99_ns | 1391.000 | 1276.000 | -115.000 | -8.267% | pass |
| mixed | 24301 | p999_ns | 9364.000 | 1856.000 | -7508.000 | -80.179% | pass |
| mixed | 24301 | p9999_ns | 10325.000 | 9999.000 | -326.000 | -3.157% | informational |
| mixed | 24301 | maximum_ns | 24159.000 | 28833.000 | 4674.000 | 19.347% | informational |
| mixed | 24301 | throughput_per_second | 1247944.861 | 1312284.509 | 64339.648 | 5.156% | pass |
| cancel | 24301 | p50_ns | 720.000 | 678.000 | -42.000 | -5.833% | informational |
| cancel | 24301 | p90_ns | 763.000 | 683.000 | -80.000 | -10.485% | informational |
| cancel | 24301 | p99_ns | 1360.000 | 752.000 | -608.000 | -44.706% | pass |
| cancel | 24301 | p999_ns | 9466.000 | 1237.000 | -8229.000 | -86.932% | pass |
| cancel | 24301 | p9999_ns | 10157.000 | 9580.000 | -577.000 | -5.681% | informational |
| cancel | 24301 | maximum_ns | 29478.000 | 20028.000 | -9450.000 | -32.058% | informational |
| cancel | 24301 | throughput_per_second | 1278932.759 | 1459492.533 | 180559.774 | 14.118% | pass |
| unknown_cancel | 24301 | p50_ns | 535.000 | 535.000 | 0.000 | 0.000% | informational |
| unknown_cancel | 24301 | p90_ns | 552.000 | 542.000 | -10.000 | -1.812% | informational |
| unknown_cancel | 24301 | p99_ns | 913.000 | 565.000 | -348.000 | -38.116% | pass |
| unknown_cancel | 24301 | p999_ns | 1336.000 | 980.000 | -356.000 | -26.647% | pass |
| unknown_cancel | 24301 | p9999_ns | 9612.000 | 9402.000 | -210.000 | -2.185% | informational |
| unknown_cancel | 24301 | maximum_ns | 21503.000 | 19413.000 | -2090.000 | -9.720% | informational |
| unknown_cancel | 24301 | throughput_per_second | 1800908.749 | 1851334.716 | 50425.967 | 2.800% | pass |
| reduce | 24301 | p50_ns | 663.000 | 658.000 | -5.000 | -0.754% | informational |
| reduce | 24301 | p90_ns | 673.000 | 666.000 | -7.000 | -1.040% | informational |
| reduce | 24301 | p99_ns | 946.000 | 726.000 | -220.000 | -23.256% | pass |
| reduce | 24301 | p999_ns | 1328.000 | 1145.000 | -183.000 | -13.780% | pass |
| reduce | 24301 | p9999_ns | 9562.000 | 9531.000 | -31.000 | -0.324% | informational |
| reduce | 24301 | maximum_ns | 17376.000 | 22109.000 | 4733.000 | 27.239% | informational |
| reduce | 24301 | throughput_per_second | 1485249.359 | 1504487.919 | 19238.560 | 1.295% | pass |
| increase | 24301 | p50_ns | 682.000 | 665.000 | -17.000 | -2.493% | informational |
| increase | 24301 | p90_ns | 703.000 | 672.000 | -31.000 | -4.410% | informational |
| increase | 24301 | p99_ns | 1152.000 | 704.000 | -448.000 | -38.889% | pass |
| increase | 24301 | p999_ns | 9398.000 | 986.000 | -8412.000 | -89.508% | pass |
| increase | 24301 | p9999_ns | 9839.000 | 9566.000 | -273.000 | -2.775% | informational |
| increase | 24301 | maximum_ns | 27698.000 | 22293.000 | -5405.000 | -19.514% | informational |
| increase | 24301 | throughput_per_second | 1411729.472 | 1494551.648 | 82822.176 | 5.867% | pass |
| noop | 24301 | p50_ns | 569.000 | 547.000 | -22.000 | -3.866% | informational |
| noop | 24301 | p90_ns | 963.000 | 554.000 | -409.000 | -42.471% | informational |
| noop | 24301 | p99_ns | 1367.000 | 573.000 | -794.000 | -58.083% | pass |
| noop | 24301 | p999_ns | 9373.000 | 864.000 | -8509.000 | -90.782% | pass |
| noop | 24301 | p9999_ns | 10865.000 | 9427.000 | -1438.000 | -13.235% | informational |
| noop | 24301 | maximum_ns | 41286.000 | 14395.000 | -26891.000 | -65.133% | informational |
| noop | 24301 | throughput_per_second | 1547494.387 | 1815375.983 | 267881.596 | 17.311% | pass |
| unknown_amend | 24301 | p50_ns | 541.000 | 539.000 | -2.000 | -0.370% | informational |
| unknown_amend | 24301 | p90_ns | 554.000 | 545.000 | -9.000 | -1.625% | informational |
| unknown_amend | 24301 | p99_ns | 969.000 | 566.000 | -403.000 | -41.589% | pass |
| unknown_amend | 24301 | p999_ns | 6752.000 | 890.000 | -5862.000 | -86.819% | pass |
| unknown_amend | 24301 | p9999_ns | 9783.000 | 9402.000 | -381.000 | -3.895% | informational |
| unknown_amend | 24301 | maximum_ns | 29357.000 | 25576.000 | -3781.000 | -12.879% | informational |
| unknown_amend | 24301 | throughput_per_second | 1783242.397 | 1841605.714 | 58363.317 | 3.273% | pass |
| noncross_add | 24301 | p50_ns | 984.000 | 962.000 | -22.000 | -2.236% | informational |
| noncross_add | 24301 | p90_ns | 1019.000 | 977.000 | -42.000 | -4.122% | informational |
| noncross_add | 24301 | p99_ns | 1601.000 | 1024.000 | -577.000 | -36.040% | pass |
| noncross_add | 24301 | p999_ns | 8406.000 | 1590.000 | -6816.000 | -81.085% | pass |
| noncross_add | 24301 | p9999_ns | 10371.000 | 9931.000 | -440.000 | -4.243% | informational |
| noncross_add | 24301 | maximum_ns | 29076.000 | 23292.000 | -5784.000 | -19.893% | informational |
| noncross_add | 24301 | throughput_per_second | 989519.810 | 1032036.736 | 42516.926 | 4.297% | pass |
| fill1 | 24301 | p50_ns | 1085.000 | 1009.000 | -76.000 | -7.005% | informational |
| fill1 | 24301 | p90_ns | 1457.000 | 1021.000 | -436.000 | -29.925% | informational |
| fill1 | 24301 | p99_ns | 1963.000 | 1069.000 | -894.000 | -45.543% | pass |
| fill1 | 24301 | p999_ns | 10064.000 | 1861.000 | -8203.000 | -81.508% | pass |
| fill1 | 24301 | p9999_ns | 12892.000 | 10055.000 | -2837.000 | -22.006% | informational |
| fill1 | 24301 | maximum_ns | 32278.000 | 18118.000 | -14160.000 | -43.869% | informational |
| fill1 | 24301 | throughput_per_second | 852904.659 | 981541.788 | 128637.129 | 15.082% | pass |
| fill4 | 24301 | p50_ns | 1417.000 | 1248.000 | -169.000 | -11.927% | informational |
| fill4 | 24301 | p90_ns | 1540.000 | 1261.000 | -279.000 | -18.117% | informational |
| fill4 | 24301 | p99_ns | 2563.000 | 1303.000 | -1260.000 | -49.161% | pass |
| fill4 | 24301 | p999_ns | 10663.000 | 2096.000 | -8567.000 | -80.343% | pass |
| fill4 | 24301 | p9999_ns | 14632.000 | 10208.000 | -4424.000 | -30.235% | informational |
| fill4 | 24301 | maximum_ns | 20965.000 | 17773.000 | -3192.000 | -15.225% | informational |
| fill4 | 24301 | throughput_per_second | 659305.442 | 796654.269 | 137348.827 | 20.832% | pass |
| fill16 | 24301 | p50_ns | 2439.000 | 2116.000 | -323.000 | -13.243% | informational |
| fill16 | 24301 | p90_ns | 2566.000 | 2142.000 | -424.000 | -16.524% | informational |
| fill16 | 24301 | p99_ns | 3574.000 | 2203.000 | -1371.000 | -38.360% | pass |
| fill16 | 24301 | p999_ns | 11631.000 | 5212.000 | -6419.000 | -55.189% | pass |
| fill16 | 24301 | p9999_ns | 13366.000 | 11162.000 | -2204.000 | -16.490% | informational |
| fill16 | 24301 | maximum_ns | 15459.000 | 18946.000 | 3487.000 | 22.556% | informational |
| fill16 | 24301 | throughput_per_second | 396077.737 | 470045.615 | 73967.878 | 18.675% | pass |
| fill64 | 24301 | p50_ns | 6652.000 | 5369.000 | -1283.000 | -19.287% | informational |
| fill64 | 24301 | p90_ns | 6897.000 | 5500.000 | -1397.000 | -20.255% | informational |
| fill64 | 24301 | p99_ns | 15426.000 | 5550.000 | -9876.000 | -64.022% | pass |
| fill64 | 24301 | p999_ns | 19302.000 | 8609.000 | -10693.000 | -55.398% | pass |
| fill64 | 24301 | p9999_ns | 25396.000 | 14143.000 | -11253.000 | -44.310% | informational |
| fill64 | 24301 | maximum_ns | 25396.000 | 14143.000 | -11253.000 | -44.310% | informational |
| fill64 | 24301 | throughput_per_second | 144923.445 | 185222.010 | 40298.565 | 27.807% | pass |
| fill256 | 24301 | p50_ns | 22780.000 | 17623.000 | -5157.000 | -22.638% | informational |
| fill256 | 24301 | p90_ns | 31535.000 | 17738.000 | -13797.000 | -43.751% | informational |
| fill256 | 24301 | p99_ns | 43837.000 | 19251.000 | -24586.000 | -56.085% | pass |
| fill256 | 24301 | p999_ns | 52892.000 | 26791.000 | -26101.000 | -49.348% | pass |
| fill256 | 24301 | p9999_ns | 57663.000 | 26937.000 | -30726.000 | -53.285% | informational |
| fill256 | 24301 | maximum_ns | 57663.000 | 26937.000 | -30726.000 | -53.285% | informational |
| fill256 | 24301 | throughput_per_second | 41195.717 | 56472.860 | 15277.143 | 37.084% | pass |
| multi_level | 24301 | p50_ns | 2624.000 | 2163.000 | -461.000 | -17.569% | informational |
| multi_level | 24301 | p90_ns | 2719.000 | 2182.000 | -537.000 | -19.750% | informational |
| multi_level | 24301 | p99_ns | 4545.000 | 2217.000 | -2328.000 | -51.221% | pass |
| multi_level | 24301 | p999_ns | 12946.000 | 3528.000 | -9418.000 | -72.748% | pass |
| multi_level | 24301 | p9999_ns | 18972.000 | 11022.000 | -7950.000 | -41.904% | informational |
| multi_level | 24301 | maximum_ns | 21676.000 | 13046.000 | -8630.000 | -39.814% | informational |
| multi_level | 24301 | throughput_per_second | 362674.472 | 461005.599 | 98331.127 | 27.113% | pass |
| mixed | 12648430 | p50_ns | 710.000 | 635.000 | -75.000 | -10.563% | informational |
| mixed | 12648430 | p90_ns | 1088.000 | 1039.000 | -49.000 | -4.504% | informational |
| mixed | 12648430 | p99_ns | 1688.000 | 1226.000 | -462.000 | -27.370% | pass |
| mixed | 12648430 | p999_ns | 9603.000 | 1366.000 | -8237.000 | -85.775% | pass |
| mixed | 12648430 | p9999_ns | 10920.000 | 5333.000 | -5587.000 | -51.163% | informational |
| mixed | 12648430 | maximum_ns | 34168.000 | 26551.000 | -7617.000 | -22.293% | informational |
| mixed | 12648430 | throughput_per_second | 1209675.344 | 1352890.317 | 143214.973 | 11.839% | pass |
| cancel | 12648430 | p50_ns | 721.000 | 679.000 | -42.000 | -5.825% | informational |
| cancel | 12648430 | p90_ns | 754.000 | 684.000 | -70.000 | -9.284% | informational |
| cancel | 12648430 | p99_ns | 1203.000 | 749.000 | -454.000 | -37.739% | pass |
| cancel | 12648430 | p999_ns | 9462.000 | 1039.000 | -8423.000 | -89.019% | pass |
| cancel | 12648430 | p9999_ns | 10498.000 | 4271.000 | -6227.000 | -59.316% | informational |
| cancel | 12648430 | maximum_ns | 18900.000 | 17848.000 | -1052.000 | -5.566% | informational |
| cancel | 12648430 | throughput_per_second | 1334891.807 | 1466231.819 | 131340.012 | 9.839% | pass |
| unknown_cancel | 12648430 | p50_ns | 535.000 | 536.000 | 1.000 | 0.187% | informational |
| unknown_cancel | 12648430 | p90_ns | 549.000 | 542.000 | -7.000 | -1.275% | informational |
| unknown_cancel | 12648430 | p99_ns | 599.000 | 551.000 | -48.000 | -8.013% | pass |
| unknown_cancel | 12648430 | p999_ns | 1086.000 | 899.000 | -187.000 | -17.219% | pass |
| unknown_cancel | 12648430 | p9999_ns | 9515.000 | 4070.000 | -5445.000 | -57.225% | informational |
| unknown_cancel | 12648430 | maximum_ns | 15929.000 | 21448.000 | 5519.000 | 34.647% | informational |
| unknown_cancel | 12648430 | throughput_per_second | 1840360.193 | 1856256.653 | 15896.460 | 0.864% | pass |
| reduce | 12648430 | p50_ns | 659.000 | 664.000 | 5.000 | 0.759% | informational |
| reduce | 12648430 | p90_ns | 676.000 | 671.000 | -5.000 | -0.740% | informational |
| reduce | 12648430 | p99_ns | 904.000 | 728.000 | -176.000 | -19.469% | pass |
| reduce | 12648430 | p999_ns | 1344.000 | 1008.000 | -336.000 | -25.000% | pass |
| reduce | 12648430 | p9999_ns | 9691.000 | 9568.000 | -123.000 | -1.269% | informational |
| reduce | 12648430 | maximum_ns | 16141.000 | 18388.000 | 2247.000 | 13.921% | informational |
| reduce | 12648430 | throughput_per_second | 1483818.326 | 1496423.431 | 12605.105 | 0.850% | pass |
| increase | 12648430 | p50_ns | 670.000 | 664.000 | -6.000 | -0.896% | informational |
| increase | 12648430 | p90_ns | 710.000 | 670.000 | -40.000 | -5.634% | informational |
| increase | 12648430 | p99_ns | 1239.000 | 685.000 | -554.000 | -44.713% | pass |
| increase | 12648430 | p999_ns | 2513.000 | 995.000 | -1518.000 | -60.406% | pass |
| increase | 12648430 | p9999_ns | 9916.000 | 4227.000 | -5689.000 | -57.372% | informational |
| increase | 12648430 | maximum_ns | 27239.000 | 16470.000 | -10769.000 | -39.535% | informational |
| increase | 12648430 | throughput_per_second | 1417316.827 | 1501502.575 | 84185.748 | 5.940% | pass |
| noop | 12648430 | p50_ns | 561.000 | 551.000 | -10.000 | -1.783% | informational |
| noop | 12648430 | p90_ns | 582.000 | 555.000 | -27.000 | -4.639% | informational |
| noop | 12648430 | p99_ns | 1074.000 | 563.000 | -511.000 | -47.579% | pass |
| noop | 12648430 | p999_ns | 9293.000 | 911.000 | -8382.000 | -90.197% | pass |
| noop | 12648430 | p9999_ns | 9781.000 | 3966.000 | -5815.000 | -59.452% | informational |
| noop | 12648430 | maximum_ns | 28787.000 | 22779.000 | -6008.000 | -20.871% | informational |
| noop | 12648430 | throughput_per_second | 1707552.182 | 1811204.912 | 103652.730 | 6.070% | pass |
| unknown_amend | 12648430 | p50_ns | 535.000 | 542.000 | 7.000 | 1.308% | informational |
| unknown_amend | 12648430 | p90_ns | 550.000 | 547.000 | -3.000 | -0.545% | informational |
| unknown_amend | 12648430 | p99_ns | 984.000 | 556.000 | -428.000 | -43.496% | pass |
| unknown_amend | 12648430 | p999_ns | 1374.000 | 910.000 | -464.000 | -33.770% | pass |
| unknown_amend | 12648430 | p9999_ns | 9602.000 | 3719.000 | -5883.000 | -61.268% | informational |
| unknown_amend | 12648430 | maximum_ns | 26038.000 | 22732.000 | -3306.000 | -12.697% | informational |
| unknown_amend | 12648430 | throughput_per_second | 1807115.692 | 1840147.976 | 33032.284 | 1.828% | pass |
| noncross_add | 12648430 | p50_ns | 988.000 | 969.000 | -19.000 | -1.923% | informational |
| noncross_add | 12648430 | p90_ns | 1027.000 | 979.000 | -48.000 | -4.674% | informational |
| noncross_add | 12648430 | p99_ns | 1704.000 | 1011.000 | -693.000 | -40.669% | pass |
| noncross_add | 12648430 | p999_ns | 9704.000 | 1494.000 | -8210.000 | -84.604% | pass |
| noncross_add | 12648430 | p9999_ns | 10539.000 | 9757.000 | -782.000 | -7.420% | informational |
| noncross_add | 12648430 | maximum_ns | 31783.000 | 45865.000 | 14082.000 | 44.307% | informational |
| noncross_add | 12648430 | throughput_per_second | 971802.690 | 1023918.482 | 52115.792 | 5.363% | pass |
| fill1 | 12648430 | p50_ns | 1068.000 | 1008.000 | -60.000 | -5.618% | informational |
| fill1 | 12648430 | p90_ns | 1105.000 | 1019.000 | -86.000 | -7.783% | informational |
| fill1 | 12648430 | p99_ns | 1669.000 | 1029.000 | -640.000 | -38.346% | pass |
| fill1 | 12648430 | p999_ns | 9929.000 | 1126.000 | -8803.000 | -88.659% | pass |
| fill1 | 12648430 | p9999_ns | 10658.000 | 4854.000 | -5804.000 | -54.457% | informational |
| fill1 | 12648430 | maximum_ns | 18278.000 | 15896.000 | -2382.000 | -13.032% | informational |
| fill1 | 12648430 | throughput_per_second | 906304.513 | 989233.881 | 82929.368 | 9.150% | pass |
| fill4 | 12648430 | p50_ns | 1360.000 | 1228.000 | -132.000 | -9.706% | informational |
| fill4 | 12648430 | p90_ns | 1372.000 | 1240.000 | -132.000 | -9.621% | informational |
| fill4 | 12648430 | p99_ns | 1517.000 | 1262.000 | -255.000 | -16.809% | pass |
| fill4 | 12648430 | p999_ns | 2484.000 | 1418.000 | -1066.000 | -42.915% | pass |
| fill4 | 12648430 | p9999_ns | 10399.000 | 4691.000 | -5708.000 | -54.890% | informational |
| fill4 | 12648430 | maximum_ns | 13801.000 | 13544.000 | -257.000 | -1.862% | informational |
| fill4 | 12648430 | throughput_per_second | 725605.260 | 812521.687 | 86916.427 | 11.978% | pass |
| fill16 | 12648430 | p50_ns | 2431.000 | 2102.000 | -329.000 | -13.534% | informational |
| fill16 | 12648430 | p90_ns | 2471.000 | 2130.000 | -341.000 | -13.800% | informational |
| fill16 | 12648430 | p99_ns | 2525.000 | 2188.000 | -337.000 | -13.347% | pass |
| fill16 | 12648430 | p999_ns | 11155.000 | 2469.000 | -8686.000 | -77.866% | pass |
| fill16 | 12648430 | p9999_ns | 14022.000 | 11099.000 | -2923.000 | -20.846% | informational |
| fill16 | 12648430 | maximum_ns | 16967.000 | 12156.000 | -4811.000 | -28.355% | informational |
| fill16 | 12648430 | throughput_per_second | 407982.046 | 474045.769 | 66063.723 | 16.193% | pass |
| fill64 | 12648430 | p50_ns | 6467.000 | 5314.000 | -1153.000 | -17.829% | informational |
| fill64 | 12648430 | p90_ns | 6490.000 | 5359.000 | -1131.000 | -17.427% | informational |
| fill64 | 12648430 | p99_ns | 6618.000 | 5561.000 | -1057.000 | -15.972% | pass |
| fill64 | 12648430 | p999_ns | 11097.000 | 8838.000 | -2259.000 | -20.357% | pass |
| fill64 | 12648430 | p9999_ns | 16184.000 | 14832.000 | -1352.000 | -8.354% | informational |
| fill64 | 12648430 | maximum_ns | 16184.000 | 14832.000 | -1352.000 | -8.354% | informational |
| fill64 | 12648430 | throughput_per_second | 154107.748 | 187633.961 | 33526.213 | 21.755% | pass |
| fill256 | 12648430 | p50_ns | 22220.000 | 18590.000 | -3630.000 | -16.337% | informational |
| fill256 | 12648430 | p90_ns | 22866.000 | 18762.000 | -4104.000 | -17.948% | informational |
| fill256 | 12648430 | p99_ns | 35664.000 | 21332.000 | -14332.000 | -40.186% | pass |
| fill256 | 12648430 | p999_ns | 40051.000 | 27716.000 | -12335.000 | -30.798% | pass |
| fill256 | 12648430 | p9999_ns | 42039.000 | 28787.000 | -13252.000 | -31.523% | informational |
| fill256 | 12648430 | maximum_ns | 42039.000 | 28787.000 | -13252.000 | -31.523% | informational |
| fill256 | 12648430 | throughput_per_second | 44005.938 | 54331.842 | 10325.904 | 23.465% | pass |
| multi_level | 12648430 | p50_ns | 2561.000 | 2142.000 | -419.000 | -16.361% | informational |
| multi_level | 12648430 | p90_ns | 2586.000 | 2165.000 | -421.000 | -16.280% | informational |
| multi_level | 12648430 | p99_ns | 2654.000 | 2236.000 | -418.000 | -15.750% | pass |
| multi_level | 12648430 | p999_ns | 5654.000 | 3308.000 | -2346.000 | -41.493% | pass |
| multi_level | 12648430 | p9999_ns | 12016.000 | 10755.000 | -1261.000 | -10.494% | informational |
| multi_level | 12648430 | maximum_ns | 15015.000 | 11170.000 | -3845.000 | -25.608% | informational |
| multi_level | 12648430 | throughput_per_second | 388639.515 | 465657.709 | 77018.194 | 19.817% | pass |

Trace checksums match retained baseline: yes

Relative non-regression gates: pass

Phase 1 timed process allocations/bytes/deallocations: 19000760/976054720/54880780

Milestone 10 timed process allocations/bytes/deallocations: 0/0/0

Final canonical acceptance: pass
