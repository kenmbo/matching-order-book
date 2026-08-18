# Phase 1 Performance Baseline

Profile: `phase1_allocating_storage`

Boundary: public `MatchingEngine::process()` entry through return

Accepted canonical baseline: yes

The matching-core endpoint was not independently measured. Report-producing commands include outbox cursor publication; zero-event commands are process-completion measurements only.

## Method

Every command trace and bounded result buffer is constructed before timing. Each elapsed interval surrounds exactly one public `process()` call. Result checks, checksums, invariant queries, and execution-outbox draining occur after that interval; no repair command is included in a target latency. The mixed trace maintains 5,000--10,000 live orders with an exact 70/20/10 operation mix and at least 80% of priced volume near the top five levels. Unknown-ID paths are separate. Isolated cases restore the same target order state between calls with untimed cancellation, amendment, or repopulation. The multi-level case restores four orders at each of four prices outside timing, so every target really sweeps four levels.

## Environment

- Git: `18ec82904737cd8d06a58a1ca0af3d828fcfaaa2` (dirty: yes)
- CPU:  Intel(R) Xeon(R) CPU E3-1230 V2 @ 3.30GHz
- Microcode:  0x21
- Kernel: Linux 6.1.0-52-amd64 x86_64
- Compiler: 12.2.0
- Flags: `-O3 -march=native -ffast-math -Wall -Wextra -Werror -std=c++20 -DNDEBUG`
- Affinity: 1
- SMT sibling: 1,5 (launcher_present_not_continuously_monitored)
- Governor/frequency: schedutil / 3347785 kHz
- NUMA node: 0
- Clock: `std::chrono::steady_clock` backed by monotonic clock; resolution 1 ns; median call-pair overhead 23 ns; overhead was not subtracted.
- Mode: acceptance, repetitions: 5

## Results

| Workload | Seed | Samples | p50 ns | p90 ns | p99 ns | p99.9 ns | p99.99 ns | Max ns | Throughput/s | Gate |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| mixed | 24301 | 1000000 | 691 | 1056 | 1391 | 9364 | 10325 | 24159 | 1247944 | pass |
| cancel | 24301 | 500000 | 720 | 763 | 1360 | 9466 | 10157 | 29478 | 1278932 | pass |
| unknown_cancel | 24301 | 1000000 | 535 | 552 | 913 | 1336 | 9612 | 21503 | 1800908 | pass |
| reduce | 24301 | 500000 | 663 | 673 | 946 | 1328 | 9562 | 17376 | 1485249 | pass |
| increase | 24301 | 500000 | 682 | 703 | 1152 | 9398 | 9839 | 27698 | 1411729 | pass |
| noop | 24301 | 1000000 | 569 | 963 | 1367 | 9373 | 10865 | 41286 | 1547494 | pass |
| unknown_amend | 24301 | 1000000 | 541 | 554 | 969 | 6752 | 9783 | 29357 | 1783242 | pass |
| noncross_add | 24301 | 500000 | 984 | 1019 | 1601 | 8406 | 10371 | 29076 | 989519 | pass |
| fill1 | 24301 | 200000 | 1085 | 1457 | 1963 | 10064 | 12892 | 32278 | 852904 | pass |
| fill4 | 24301 | 50000 | 1417 | 1540 | 2563 | 10663 | 14632 | 20965 | 659305 | informational |
| fill16 | 24301 | 20000 | 2439 | 2566 | 3574 | 11631 | 13366 | 15459 | 396077 | informational |
| fill64 | 24301 | 5000 | 6652 | 6897 | 15426 | 19302 | 25396 | 25396 | 144923 | informational |
| fill256 | 24301 | 1000 | 22780 | 31535 | 43837 | 52892 | 57663 | 57663 | 41195 | informational |
| multi_level | 24301 | 20000 | 2624 | 2719 | 4545 | 12946 | 18972 | 21676 | 362674 | informational |
| mixed | 12648430 | 1000000 | 710 | 1088 | 1688 | 9603 | 10920 | 34168 | 1209675 | pass |
| cancel | 12648430 | 500000 | 721 | 754 | 1203 | 9462 | 10498 | 18900 | 1334891 | pass |
| unknown_cancel | 12648430 | 1000000 | 535 | 549 | 599 | 1086 | 9515 | 15929 | 1840360 | pass |
| reduce | 12648430 | 500000 | 659 | 676 | 904 | 1344 | 9691 | 16141 | 1483818 | pass |
| increase | 12648430 | 500000 | 670 | 710 | 1239 | 2513 | 9916 | 27239 | 1417316 | pass |
| noop | 12648430 | 1000000 | 561 | 582 | 1074 | 9293 | 9781 | 28787 | 1707552 | pass |
| unknown_amend | 12648430 | 1000000 | 535 | 550 | 984 | 1374 | 9602 | 26038 | 1807115 | pass |
| noncross_add | 12648430 | 500000 | 988 | 1027 | 1704 | 9704 | 10539 | 31783 | 971802 | pass |
| fill1 | 12648430 | 200000 | 1068 | 1105 | 1669 | 9929 | 10658 | 18278 | 906304 | pass |
| fill4 | 12648430 | 50000 | 1360 | 1372 | 1517 | 2484 | 10399 | 13801 | 725605 | informational |
| fill16 | 12648430 | 20000 | 2431 | 2471 | 2525 | 11155 | 14022 | 16967 | 407982 | informational |
| fill64 | 12648430 | 5000 | 6467 | 6490 | 6618 | 11097 | 16184 | 16184 | 154107 | informational |
| fill256 | 12648430 | 1000 | 22220 | 22866 | 35664 | 40051 | 42039 | 42039 | 44005 | informational |
| multi_level | 12648430 | 20000 | 2561 | 2586 | 2654 | 5654 | 12016 | 15015 | 388639 | informational |

### Per-repetition evidence

| Workload | Seed | Rep | p50 | p90 | p99 | p99.9 | p99.99 | Max | Throughput/s | Public gate | Core upper-bound | Multi-fill upper-bound | Checksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- | ---: |
| mixed | 24301 | 0 | 691 | 1059 | 1496 | 9500 | 10610 | 29227 | 1247944 | pass | miss | not applicable | 11976050521989200683 |
| mixed | 24301 | 1 | 687 | 1050 | 1220 | 9364 | 10294 | 18076 | 1272147 | pass | miss | not applicable | 11976050521989200683 |
| mixed | 24301 | 2 | 683 | 1056 | 1205 | 1987 | 9997 | 14781 | 1285068 | pass | miss | not applicable | 11976050521989200683 |
| mixed | 24301 | 3 | 696 | 1056 | 1391 | 4017 | 10325 | 24159 | 1247228 | pass | miss | not applicable | 11976050521989200683 |
| mixed | 24301 | 4 | 1038 | 1703 | 2094 | 10081 | 14179 | 33624 | 875778 | pass | miss | not applicable | 11976050521989200683 |
| cancel | 24301 | 0 | 737 | 763 | 1372 | 9684 | 10873 | 29478 | 1278932 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 24301 | 1 | 746 | 919 | 1513 | 9499 | 12012 | 28412 | 1242549 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 24301 | 2 | 720 | 1134 | 1360 | 9466 | 10157 | 35925 | 1192651 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 24301 | 3 | 716 | 740 | 1013 | 2163 | 9871 | 32279 | 1361767 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 24301 | 4 | 714 | 718 | 779 | 1273 | 9412 | 16060 | 1394327 | pass | miss | not applicable | 1708398209335453120 |
| unknown_cancel | 24301 | 0 | 534 | 540 | 555 | 958 | 4804 | 16429 | 1861557 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 24301 | 1 | 534 | 549 | 605 | 1194 | 9538 | 16810 | 1835368 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 24301 | 2 | 549 | 565 | 1056 | 9223 | 9857 | 36727 | 1748864 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 24301 | 3 | 540 | 552 | 913 | 1993 | 9830 | 35466 | 1800908 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 24301 | 4 | 535 | 552 | 1004 | 1336 | 9612 | 21503 | 1791159 | pass | miss | not applicable | 6645398710458996416 |
| reduce | 24301 | 0 | 662 | 673 | 744 | 1128 | 9562 | 15567 | 1491355 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 24301 | 1 | 666 | 699 | 1116 | 1470 | 9715 | 17376 | 1450241 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 24301 | 2 | 669 | 727 | 1167 | 9307 | 9856 | 29035 | 1410422 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 24301 | 3 | 663 | 673 | 946 | 1328 | 9524 | 16803 | 1485249 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 24301 | 4 | 663 | 671 | 742 | 1014 | 9423 | 20527 | 1494630 | pass | miss | not applicable | 1708398209335453120 |
| increase | 24301 | 0 | 670 | 694 | 947 | 1468 | 9653 | 12615 | 1458966 | pass | miss | not applicable | 1708398209335453120 |
| increase | 24301 | 1 | 684 | 713 | 1158 | 9458 | 11135 | 36236 | 1406290 | pass | miss | not applicable | 1708398209335453120 |
| increase | 24301 | 2 | 684 | 703 | 1152 | 9398 | 9922 | 27698 | 1411729 | pass | miss | not applicable | 1708398209335453120 |
| increase | 24301 | 3 | 681 | 713 | 1157 | 9294 | 9839 | 30200 | 1404958 | pass | miss | not applicable | 1708398209335453120 |
| increase | 24301 | 4 | 682 | 693 | 1007 | 9452 | 9780 | 24534 | 1429341 | pass | miss | not applicable | 1708398209335453120 |
| noop | 24301 | 0 | 552 | 569 | 760 | 1330 | 9556 | 27247 | 1772879 | pass | miss | not applicable | 4872455860064465152 |
| noop | 24301 | 1 | 566 | 591 | 1321 | 9373 | 10382 | 44488 | 1621678 | pass | miss | not applicable | 4872455860064465152 |
| noop | 24301 | 2 | 583 | 1092 | 1490 | 9401 | 14074 | 33588 | 1367858 | pass | miss | not applicable | 4872455860064465152 |
| noop | 24301 | 3 | 569 | 1042 | 1394 | 9345 | 11705 | 46420 | 1468944 | pass | miss | not applicable | 4872455860064465152 |
| noop | 24301 | 4 | 571 | 963 | 1367 | 9429 | 10865 | 41286 | 1547494 | pass | miss | not applicable | 4872455860064465152 |
| unknown_amend | 24301 | 0 | 546 | 563 | 1080 | 9312 | 10229 | 30677 | 1753075 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 24301 | 1 | 547 | 565 | 969 | 6752 | 9783 | 24182 | 1774890 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 24301 | 2 | 541 | 554 | 971 | 9167 | 10140 | 42721 | 1783242 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 24301 | 3 | 537 | 551 | 700 | 3725 | 9689 | 29357 | 1812912 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 24301 | 4 | 534 | 548 | 567 | 1079 | 9496 | 18614 | 1845001 | pass | miss | not applicable | 6645398710458996416 |
| noncross_add | 24301 | 0 | 984 | 1013 | 1469 | 3300 | 10184 | 17630 | 996227 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 24301 | 1 | 984 | 1019 | 1601 | 5557 | 10394 | 29155 | 989519 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 24301 | 2 | 985 | 1056 | 1842 | 8406 | 10583 | 40040 | 945958 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 24301 | 3 | 985 | 1042 | 1622 | 9635 | 10202 | 20674 | 964030 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 24301 | 4 | 984 | 1015 | 1453 | 9791 | 10371 | 29076 | 990815 | pass | miss | not applicable | 14680700484052686336 |
| fill1 | 24301 | 0 | 1070 | 1100 | 1639 | 9818 | 10231 | 34804 | 910358 | pass | miss | miss | 4209633463992593536 |
| fill1 | 24301 | 1 | 1068 | 1087 | 1142 | 3405 | 10233 | 26039 | 925291 | pass | miss | pass | 4209633463992593536 |
| fill1 | 24301 | 2 | 1085 | 1457 | 1963 | 10064 | 12892 | 22813 | 852904 | pass | miss | miss | 4209633463992593536 |
| fill1 | 24301 | 3 | 1605 | 2042 | 2372 | 10507 | 15118 | 42392 | 638934 | fail | miss | miss | 4209633463992593536 |
| fill1 | 24301 | 4 | 1128 | 1685 | 2099 | 10184 | 15032 | 32278 | 786501 | pass | miss | miss | 4209633463992593536 |
| fill4 | 24301 | 0 | 1428 | 2206 | 2644 | 10800 | 17601 | 22395 | 608617 | not applicable | miss | miss | 18118962843518811632 |
| fill4 | 24301 | 1 | 1422 | 2016 | 2563 | 10663 | 14632 | 18866 | 645719 | not applicable | miss | miss | 18118962843518811632 |
| fill4 | 24301 | 2 | 1417 | 1540 | 2594 | 10813 | 14400 | 20965 | 659305 | not applicable | miss | miss | 18118962843518811632 |
| fill4 | 24301 | 3 | 1377 | 1423 | 2022 | 10369 | 15280 | 27371 | 705494 | not applicable | miss | pass | 18118962843518811632 |
| fill4 | 24301 | 4 | 1362 | 1398 | 2282 | 10390 | 12256 | 17556 | 703817 | not applicable | miss | miss | 18118962843518811632 |
| fill16 | 24301 | 0 | 2554 | 3610 | 4395 | 11657 | 13366 | 24467 | 366464 | not applicable | miss | pass | 4870814945307920736 |
| fill16 | 24301 | 1 | 2432 | 2517 | 2644 | 11477 | 14925 | 15459 | 401714 | not applicable | miss | pass | 4870814945307920736 |
| fill16 | 24301 | 2 | 2439 | 2566 | 2658 | 11476 | 11756 | 15243 | 396077 | not applicable | miss | pass | 4870814945307920736 |
| fill16 | 24301 | 3 | 2515 | 2586 | 4388 | 11981 | 17340 | 20331 | 382063 | not applicable | miss | pass | 4870814945307920736 |
| fill16 | 24301 | 4 | 2434 | 2520 | 3574 | 11631 | 13192 | 13416 | 398607 | not applicable | miss | pass | 4870814945307920736 |
| fill64 | 24301 | 0 | 6703 | 7182 | 15615 | 19405 | 25424 | 25424 | 142721 | not applicable | miss | pass | 16852925845242151096 |
| fill64 | 24301 | 1 | 6636 | 6879 | 15705 | 19302 | 27151 | 27151 | 146303 | not applicable | miss | pass | 16852925845242151096 |
| fill64 | 24301 | 2 | 6652 | 6756 | 15426 | 18500 | 24729 | 24729 | 147093 | not applicable | miss | pass | 16852925845242151096 |
| fill64 | 24301 | 3 | 6671 | 6970 | 15378 | 18368 | 24491 | 24491 | 144283 | not applicable | miss | pass | 16852925845242151096 |
| fill64 | 24301 | 4 | 6542 | 6897 | 15396 | 19828 | 25396 | 25396 | 144923 | not applicable | miss | pass | 16852925845242151096 |
| fill256 | 24301 | 0 | 22650 | 23464 | 43853 | 60745 | 63601 | 63601 | 42103 | not applicable | miss | pass | 11418948980634061528 |
| fill256 | 24301 | 1 | 22805 | 35415 | 43747 | 52408 | 52443 | 52443 | 39699 | not applicable | miss | pass | 11418948980634061528 |
| fill256 | 24301 | 2 | 23328 | 35451 | 43837 | 53368 | 57663 | 57663 | 38904 | not applicable | miss | pass | 11418948980634061528 |
| fill256 | 24301 | 3 | 22694 | 31535 | 45230 | 52621 | 53345 | 53345 | 41195 | not applicable | miss | pass | 11418948980634061528 |
| fill256 | 24301 | 4 | 22780 | 26046 | 41319 | 52892 | 58684 | 58684 | 41512 | not applicable | miss | pass | 11418948980634061528 |
| multi_level | 24301 | 0 | 2629 | 2748 | 5321 | 13033 | 20764 | 21676 | 357356 | not applicable | miss | pass | 1168931345846841760 |
| multi_level | 24301 | 1 | 2638 | 2737 | 4545 | 12948 | 21212 | 23855 | 360978 | not applicable | miss | pass | 1168931345846841760 |
| multi_level | 24301 | 2 | 2624 | 2719 | 4561 | 12876 | 18179 | 21329 | 362674 | not applicable | miss | pass | 1168931345846841760 |
| multi_level | 24301 | 3 | 2604 | 2697 | 4330 | 12946 | 18972 | 23415 | 369673 | not applicable | miss | pass | 1168931345846841760 |
| multi_level | 24301 | 4 | 2614 | 2689 | 4416 | 12072 | 18103 | 20497 | 369158 | not applicable | miss | pass | 1168931345846841760 |
| mixed | 12648430 | 0 | 701 | 1084 | 1688 | 9603 | 10920 | 34168 | 1214758 | pass | miss | not applicable | 3435255765795108418 |
| mixed | 12648430 | 1 | 712 | 1266 | 1955 | 9746 | 12356 | 29515 | 1126105 | pass | miss | not applicable | 3435255765795108418 |
| mixed | 12648430 | 2 | 767 | 1158 | 1909 | 9809 | 13019 | 40962 | 1117346 | pass | miss | not applicable | 3435255765795108418 |
| mixed | 12648430 | 3 | 682 | 1047 | 1277 | 9535 | 10589 | 21628 | 1271097 | pass | miss | not applicable | 3435255765795108418 |
| mixed | 12648430 | 4 | 710 | 1088 | 1673 | 9447 | 10865 | 40095 | 1209675 | pass | miss | not applicable | 3435255765795108418 |
| cancel | 12648430 | 0 | 721 | 757 | 1098 | 9414 | 10282 | 29599 | 1341938 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 12648430 | 1 | 731 | 758 | 1383 | 9603 | 10498 | 18900 | 1303827 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 12648430 | 2 | 721 | 754 | 1268 | 9462 | 11392 | 18939 | 1334891 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 12648430 | 3 | 731 | 749 | 1203 | 9643 | 10831 | 18725 | 1326661 | pass | miss | not applicable | 1708398209335453120 |
| cancel | 12648430 | 4 | 715 | 722 | 783 | 1629 | 9720 | 15838 | 1379426 | pass | miss | not applicable | 1708398209335453120 |
| unknown_cancel | 12648430 | 0 | 540 | 553 | 606 | 9041 | 9665 | 20791 | 1805572 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 12648430 | 1 | 535 | 550 | 573 | 1086 | 9549 | 15713 | 1837750 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 12648430 | 2 | 535 | 549 | 601 | 1147 | 9515 | 18015 | 1840360 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 12648430 | 3 | 535 | 549 | 581 | 1071 | 9487 | 15860 | 1842932 | pass | miss | not applicable | 6645398710458996416 |
| unknown_cancel | 12648430 | 4 | 534 | 542 | 599 | 917 | 9391 | 15929 | 1852982 | pass | miss | not applicable | 6645398710458996416 |
| reduce | 12648430 | 0 | 657 | 675 | 746 | 1208 | 9571 | 13825 | 1497506 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 12648430 | 1 | 665 | 680 | 976 | 9189 | 9820 | 18434 | 1465480 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 12648430 | 2 | 659 | 680 | 1024 | 9122 | 9794 | 16141 | 1469007 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 12648430 | 3 | 659 | 676 | 904 | 1344 | 9691 | 19830 | 1483818 | pass | miss | not applicable | 1708398209335453120 |
| reduce | 12648430 | 4 | 657 | 676 | 826 | 1299 | 9662 | 14727 | 1489353 | pass | miss | not applicable | 1708398209335453120 |
| increase | 12648430 | 0 | 670 | 690 | 931 | 2513 | 9798 | 27239 | 1454623 | pass | miss | not applicable | 1708398209335453120 |
| increase | 12648430 | 1 | 669 | 687 | 898 | 1336 | 9745 | 16473 | 1464849 | pass | miss | not applicable | 1708398209335453120 |
| increase | 12648430 | 2 | 670 | 710 | 1242 | 2137 | 9916 | 21260 | 1417316 | pass | miss | not applicable | 1708398209335453120 |
| increase | 12648430 | 3 | 687 | 716 | 1239 | 9589 | 11402 | 27476 | 1371550 | pass | miss | not applicable | 1708398209335453120 |
| increase | 12648430 | 4 | 688 | 765 | 1285 | 9585 | 10719 | 28723 | 1345297 | pass | miss | not applicable | 1708398209335453120 |
| noop | 12648430 | 0 | 564 | 590 | 1164 | 9345 | 9854 | 40854 | 1638784 | pass | miss | not applicable | 4872455860064465152 |
| noop | 12648430 | 1 | 557 | 582 | 1033 | 9189 | 9723 | 28545 | 1717074 | pass | miss | not applicable | 4872455860064465152 |
| noop | 12648430 | 2 | 555 | 567 | 999 | 1343 | 9541 | 19682 | 1752839 | pass | miss | not applicable | 4872455860064465152 |
| noop | 12648430 | 3 | 565 | 582 | 1102 | 9315 | 9826 | 28787 | 1694701 | pass | miss | not applicable | 4872455860064465152 |
| noop | 12648430 | 4 | 561 | 575 | 1074 | 9293 | 9781 | 32552 | 1707552 | pass | miss | not applicable | 4872455860064465152 |
| unknown_amend | 12648430 | 0 | 538 | 553 | 1078 | 2262 | 9722 | 21524 | 1781541 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 12648430 | 1 | 535 | 550 | 1001 | 1374 | 9602 | 29541 | 1807115 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 12648430 | 2 | 538 | 552 | 984 | 2527 | 9622 | 26038 | 1795687 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 12648430 | 3 | 534 | 549 | 610 | 1208 | 9528 | 27918 | 1831725 | pass | miss | not applicable | 6645398710458996416 |
| unknown_amend | 12648430 | 4 | 535 | 550 | 969 | 1294 | 9595 | 21176 | 1813653 | pass | miss | not applicable | 6645398710458996416 |
| noncross_add | 12648430 | 0 | 987 | 1016 | 1683 | 9823 | 10656 | 26735 | 981981 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 12648430 | 1 | 982 | 1005 | 1385 | 3108 | 10142 | 23487 | 1001673 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 12648430 | 2 | 1008 | 1504 | 1896 | 10066 | 11959 | 34883 | 900834 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 12648430 | 3 | 1008 | 1110 | 2008 | 9704 | 10539 | 31783 | 921866 | pass | miss | not applicable | 14680700484052686336 |
| noncross_add | 12648430 | 4 | 988 | 1027 | 1704 | 5310 | 10311 | 51947 | 971802 | pass | miss | not applicable | 14680700484052686336 |
| fill1 | 12648430 | 0 | 1064 | 1087 | 1119 | 4376 | 10216 | 18278 | 928531 | pass | miss | pass | 9694914282976820864 |
| fill1 | 12648430 | 1 | 1119 | 1672 | 2197 | 10166 | 14759 | 20668 | 796235 | pass | miss | miss | 9694914282976820864 |
| fill1 | 12648430 | 2 | 1093 | 1128 | 1862 | 10095 | 12793 | 20625 | 874204 | pass | miss | miss | 9694914282976820864 |
| fill1 | 12648430 | 3 | 1068 | 1105 | 1669 | 9929 | 10658 | 17400 | 906304 | pass | miss | miss | 9694914282976820864 |
| fill1 | 12648430 | 4 | 1065 | 1088 | 1327 | 3704 | 10159 | 14672 | 925717 | pass | miss | miss | 9694914282976820864 |
| fill4 | 12648430 | 0 | 1362 | 1431 | 1517 | 2522 | 10283 | 11195 | 724145 | not applicable | miss | pass | 10283548236130005040 |
| fill4 | 12648430 | 1 | 1360 | 1370 | 1394 | 2159 | 10404 | 18756 | 732606 | not applicable | miss | pass | 10283548236130005040 |
| fill4 | 12648430 | 2 | 1363 | 1402 | 1679 | 10166 | 10823 | 13801 | 719217 | not applicable | miss | pass | 10283548236130005040 |
| fill4 | 12648430 | 3 | 1358 | 1372 | 1950 | 2484 | 10399 | 143369 | 725605 | not applicable | miss | pass | 10283548236130005040 |
| fill4 | 12648430 | 4 | 1359 | 1369 | 1390 | 1722 | 10172 | 10446 | 734088 | not applicable | miss | pass | 10283548236130005040 |
| fill16 | 12648430 | 0 | 2427 | 2443 | 2509 | 7309 | 11775 | 16967 | 409505 | not applicable | miss | pass | 3003463614242969760 |
| fill16 | 12648430 | 1 | 2444 | 3906 | 4465 | 11512 | 17453 | 22529 | 368486 | not applicable | miss | pass | 3003463614242969760 |
| fill16 | 12648430 | 2 | 2500 | 3903 | 4557 | 12646 | 16856 | 25686 | 347343 | not applicable | miss | pass | 3003463614242969760 |
| fill16 | 12648430 | 3 | 2429 | 2453 | 2523 | 5397 | 11549 | 11917 | 409086 | not applicable | miss | pass | 3003463614242969760 |
| fill16 | 12648430 | 4 | 2431 | 2471 | 2525 | 11155 | 14022 | 14952 | 407982 | not applicable | miss | pass | 3003463614242969760 |
| fill64 | 12648430 | 0 | 6470 | 6559 | 6734 | 15457 | 16184 | 16184 | 153569 | not applicable | miss | pass | 8101109951146213944 |
| fill64 | 12648430 | 1 | 6473 | 6639 | 8656 | 16212 | 20656 | 20656 | 152276 | not applicable | miss | pass | 8101109951146213944 |
| fill64 | 12648430 | 2 | 6466 | 6489 | 6574 | 8899 | 15350 | 15350 | 154424 | not applicable | miss | pass | 8101109951146213944 |
| fill64 | 12648430 | 3 | 6467 | 6490 | 6618 | 11097 | 34394 | 34394 | 154107 | not applicable | miss | pass | 8101109951146213944 |
| fill64 | 12648430 | 4 | 6467 | 6487 | 6582 | 9190 | 15975 | 15975 | 154375 | not applicable | miss | pass | 8101109951146213944 |
| fill256 | 12648430 | 0 | 22169 | 22256 | 22889 | 32206 | 32294 | 32294 | 44998 | not applicable | miss | pass | 6490150999850096856 |
| fill256 | 12648430 | 1 | 23495 | 26687 | 35664 | 41874 | 42039 | 42039 | 41205 | not applicable | miss | pass | 6490150999850096856 |
| fill256 | 12648430 | 2 | 22176 | 22336 | 24738 | 39984 | 47410 | 47410 | 44798 | not applicable | miss | pass | 6490150999850096856 |
| fill256 | 12648430 | 3 | 22791 | 23649 | 40856 | 54603 | 63262 | 63262 | 42612 | not applicable | miss | pass | 6490150999850096856 |
| fill256 | 12648430 | 4 | 22220 | 22866 | 35847 | 40051 | 40061 | 40061 | 44005 | not applicable | miss | pass | 6490150999850096856 |
| multi_level | 12648430 | 0 | 2646 | 2751 | 4604 | 12848 | 20024 | 21622 | 357505 | not applicable | miss | pass | 4833899198899577056 |
| multi_level | 12648430 | 1 | 2563 | 2659 | 2760 | 11718 | 13327 | 13542 | 382735 | not applicable | miss | pass | 4833899198899577056 |
| multi_level | 12648430 | 2 | 2560 | 2580 | 2609 | 4412 | 11271 | 15015 | 389582 | not applicable | miss | pass | 4833899198899577056 |
| multi_level | 12648430 | 3 | 2558 | 2580 | 2654 | 5654 | 12016 | 15489 | 388642 | not applicable | miss | pass | 4833899198899577056 |
| multi_level | 12648430 | 4 | 2561 | 2586 | 2649 | 4955 | 11644 | 12483 | 388639 | not applicable | miss | pass | 4833899198899577056 |

Nearest-rank percentiles are computed independently per repetition; the table reports the median of repetition metrics. At least four of five repetitions must pass each applicable public-path gate. Multi-fill matching-core ceilings are informational conservative evidence only.

## Allocation classification

Global allocation overrides were enabled only in the separate audit executable. Timed `process()` totals include permitted Phase 1 storage activity. Timed sample/checksum collection reported zero allocation. Exact dynamic attribution inside the engine was not attempted; source-audited sites are map level nodes, list FIFO nodes, and unordered-map active-ID nodes. Strict total zero allocation remains a Milestone 10 validity rule.

| Workload | Seed | Repetition | Timed process allocs/bytes/frees | Timed collection allocs/bytes/frees |
| --- | ---: | ---: | ---: | ---: |
| mixed | 24301 | 0 | 400066/17604752/400069 | 0/0/0 |
| mixed | 24301 | 1 | 400066/17604752/400069 | 0/0/0 |
| mixed | 24301 | 2 | 400066/17604752/400069 | 0/0/0 |
| mixed | 24301 | 3 | 400066/17604752/400069 | 0/0/0 |
| mixed | 24301 | 4 | 400066/17604752/400069 | 0/0/0 |
| cancel | 24301 | 0 | 0/0/1500000 | 0/0/0 |
| cancel | 24301 | 1 | 0/0/1500000 | 0/0/0 |
| cancel | 24301 | 2 | 0/0/1500000 | 0/0/0 |
| cancel | 24301 | 3 | 0/0/1500000 | 0/0/0 |
| cancel | 24301 | 4 | 0/0/1500000 | 0/0/0 |
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
| noncross_add | 24301 | 0 | 1500000/80000000/0 | 0/0/0 |
| noncross_add | 24301 | 1 | 1500000/80000000/0 | 0/0/0 |
| noncross_add | 24301 | 2 | 1500000/80000000/0 | 0/0/0 |
| noncross_add | 24301 | 3 | 1500000/80000000/0 | 0/0/0 |
| noncross_add | 24301 | 4 | 1500000/80000000/0 | 0/0/0 |
| fill1 | 24301 | 0 | 0/0/600000 | 0/0/0 |
| fill1 | 24301 | 1 | 0/0/600000 | 0/0/0 |
| fill1 | 24301 | 2 | 0/0/600000 | 0/0/0 |
| fill1 | 24301 | 3 | 0/0/600000 | 0/0/0 |
| fill1 | 24301 | 4 | 0/0/600000 | 0/0/0 |
| fill4 | 24301 | 0 | 0/0/450000 | 0/0/0 |
| fill4 | 24301 | 1 | 0/0/450000 | 0/0/0 |
| fill4 | 24301 | 2 | 0/0/450000 | 0/0/0 |
| fill4 | 24301 | 3 | 0/0/450000 | 0/0/0 |
| fill4 | 24301 | 4 | 0/0/450000 | 0/0/0 |
| fill16 | 24301 | 0 | 0/0/660000 | 0/0/0 |
| fill16 | 24301 | 1 | 0/0/660000 | 0/0/0 |
| fill16 | 24301 | 2 | 0/0/660000 | 0/0/0 |
| fill16 | 24301 | 3 | 0/0/660000 | 0/0/0 |
| fill16 | 24301 | 4 | 0/0/660000 | 0/0/0 |
| fill64 | 24301 | 0 | 0/0/645000 | 0/0/0 |
| fill64 | 24301 | 1 | 0/0/645000 | 0/0/0 |
| fill64 | 24301 | 2 | 0/0/645000 | 0/0/0 |
| fill64 | 24301 | 3 | 0/0/645000 | 0/0/0 |
| fill64 | 24301 | 4 | 0/0/645000 | 0/0/0 |
| fill256 | 24301 | 0 | 0/0/513000 | 0/0/0 |
| fill256 | 24301 | 1 | 0/0/513000 | 0/0/0 |
| fill256 | 24301 | 2 | 0/0/513000 | 0/0/0 |
| fill256 | 24301 | 3 | 0/0/513000 | 0/0/0 |
| fill256 | 24301 | 4 | 0/0/513000 | 0/0/0 |
| multi_level | 24301 | 0 | 0/0/720000 | 0/0/0 |
| multi_level | 24301 | 1 | 0/0/720000 | 0/0/0 |
| multi_level | 24301 | 2 | 0/0/720000 | 0/0/0 |
| multi_level | 24301 | 3 | 0/0/720000 | 0/0/0 |
| multi_level | 24301 | 4 | 0/0/720000 | 0/0/0 |
| mixed | 12648430 | 0 | 400086/17606192/400087 | 0/0/0 |
| mixed | 12648430 | 1 | 400086/17606192/400087 | 0/0/0 |
| mixed | 12648430 | 2 | 400086/17606192/400087 | 0/0/0 |
| mixed | 12648430 | 3 | 400086/17606192/400087 | 0/0/0 |
| mixed | 12648430 | 4 | 400086/17606192/400087 | 0/0/0 |
| cancel | 12648430 | 0 | 0/0/1500000 | 0/0/0 |
| cancel | 12648430 | 1 | 0/0/1500000 | 0/0/0 |
| cancel | 12648430 | 2 | 0/0/1500000 | 0/0/0 |
| cancel | 12648430 | 3 | 0/0/1500000 | 0/0/0 |
| cancel | 12648430 | 4 | 0/0/1500000 | 0/0/0 |
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
| noncross_add | 12648430 | 0 | 1500000/80000000/0 | 0/0/0 |
| noncross_add | 12648430 | 1 | 1500000/80000000/0 | 0/0/0 |
| noncross_add | 12648430 | 2 | 1500000/80000000/0 | 0/0/0 |
| noncross_add | 12648430 | 3 | 1500000/80000000/0 | 0/0/0 |
| noncross_add | 12648430 | 4 | 1500000/80000000/0 | 0/0/0 |
| fill1 | 12648430 | 0 | 0/0/600000 | 0/0/0 |
| fill1 | 12648430 | 1 | 0/0/600000 | 0/0/0 |
| fill1 | 12648430 | 2 | 0/0/600000 | 0/0/0 |
| fill1 | 12648430 | 3 | 0/0/600000 | 0/0/0 |
| fill1 | 12648430 | 4 | 0/0/600000 | 0/0/0 |
| fill4 | 12648430 | 0 | 0/0/450000 | 0/0/0 |
| fill4 | 12648430 | 1 | 0/0/450000 | 0/0/0 |
| fill4 | 12648430 | 2 | 0/0/450000 | 0/0/0 |
| fill4 | 12648430 | 3 | 0/0/450000 | 0/0/0 |
| fill4 | 12648430 | 4 | 0/0/450000 | 0/0/0 |
| fill16 | 12648430 | 0 | 0/0/660000 | 0/0/0 |
| fill16 | 12648430 | 1 | 0/0/660000 | 0/0/0 |
| fill16 | 12648430 | 2 | 0/0/660000 | 0/0/0 |
| fill16 | 12648430 | 3 | 0/0/660000 | 0/0/0 |
| fill16 | 12648430 | 4 | 0/0/660000 | 0/0/0 |
| fill64 | 12648430 | 0 | 0/0/645000 | 0/0/0 |
| fill64 | 12648430 | 1 | 0/0/645000 | 0/0/0 |
| fill64 | 12648430 | 2 | 0/0/645000 | 0/0/0 |
| fill64 | 12648430 | 3 | 0/0/645000 | 0/0/0 |
| fill64 | 12648430 | 4 | 0/0/645000 | 0/0/0 |
| fill256 | 12648430 | 0 | 0/0/513000 | 0/0/0 |
| fill256 | 12648430 | 1 | 0/0/513000 | 0/0/0 |
| fill256 | 12648430 | 2 | 0/0/513000 | 0/0/0 |
| fill256 | 12648430 | 3 | 0/0/513000 | 0/0/0 |
| fill256 | 12648430 | 4 | 0/0/513000 | 0/0/0 |
| multi_level | 12648430 | 0 | 0/0/720000 | 0/0/0 |
| multi_level | 12648430 | 1 | 0/0/720000 | 0/0/0 |
| multi_level | 12648430 | 2 | 0/0/720000 | 0/0/0 |
| multi_level | 12648430 | 3 | 0/0/720000 | 0/0/0 |
| multi_level | 12648430 | 4 | 0/0/720000 | 0/0/0 |

Trace construction, sample-buffer setup, initial population, warm-up, timed collection, and post-run statistics are separate allocation phases in the machine-readable artifact. Trace generation and all serialization occur outside timing.

## Reproduction

```bash
cmake --preset release
cmake --build --preset release --target benchmarks
./build/release/benchmarks/lob_phase1_allocation_audit --mode acceptance --workload all --seeds 24301,12648430 --repetitions 5 --warmup 10000 --cpu 1 --allocation-output <path>
./build/release/benchmarks/lob_phase1_benchmark --mode acceptance --workload all --seeds 24301,12648430 --repetitions 5 --warmup 10000 --cpu 1 --sibling-occupancy launcher_present_not_continuously_monitored --allocation-input <path> --json <path> --report <path>
```
