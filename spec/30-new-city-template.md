# 30 — New-city template (observed initial state)

Status: observed data. Provenance tag for everything here: `[OBSERVED]` (build 2.0.949, original
running under a compatibility layer; city saved immediately after **New City** and accepting the
terrain, before the simulator ran). This document closes issue 1.1 of `29-open-issues.md`.

## 1. Method

Fourteen cities were created and saved with no other action: map sizes Miniature, Small, Medium and
Large × difficulty Easy, Medium, Hard (start year 2000), plus Small / Easy with start years 1900 and
1950. Every record of every file parses with the layouts of `10-save-format.md`. Records were then
compared byte for byte across the files.

## 2. What varies with what

| Record group | Varies with | Notes |
|---|---|---|
| All fixed-size records of §4 (police, fire, water, power, flammability trailer, petitions, rewards, advisor flags, disaster table entries, …) | nothing | Identical in all 14 files. One template serves every size, difficulty and year |
| Layer records of §5 | map size | Size is a function of the map edge W; content is zero except as listed |
| Terrain, flora, water, and the terrain-derived layers | random terrain | Not templates; produced by the terrain generator (`11-world-terrain.md`) |
| Budget record (§6) | difficulty | Opening funds word; Hard adds one bond |
| Advisor topic record `013DEE82:628D0C45` | difficulty | 108 bytes on Easy and Medium, 129 bytes on Hard; byte 12 is 4 on Easy and Medium, 5 on Hard |
| Tool-availability record `41193C3A:E1193C2A` | start year | Same size, 256 bytes; flag bytes at fixed offsets change with the year (`09-ui-screens.md` §7a.2) |
| Neighbour record `C2910E7D:82937B60` | random | Names and populations are random (§6.6 of `10-save-format.md`); size 277–299 bytes |
| World record `206C6E7C:E11BDDF6` (725 bytes) | random | Only the last four bytes differ between cities: a per-city 32-bit value, see §7 |
| Disaster manager `206C6E7C:21F6ABCA` (28 bytes) | nothing meaningful | Bytes 20–23 were 0 in 11 files and a fixed non-zero word in the 3 Miniature files; write 0 |

## 3. Occupant record

A new city has no buildings: the entry record is the 8-byte header alone and the counters record
holds count 0, entry format 1 (the "occupant entries (empty)" and "occupant counters" rows of §4).

## 4. Constant records (identical in all 14 files)

Each record is shown in full as hexadecimal bytes, offsets in the left column. Sizes include the
8-byte record header.

| Record `T:G:I` | Size | Owner |
|---|---|---|
| `013DEE82:022E288E:0000000B` | 45 | advisor topic |
| `013DEE82:028D0FC5:0000000B` | 108 | advisor topic |
| `013DEE82:828D04EB:0000000B` | 24 | advisor topic |
| `013DEE82:828D0A2F:0000000B` | 66 | advisor topic |
| `013DEE82:C28D0B6E:0000000B` | 87 | advisor topic |
| `013DEE82:C28D0F40:0000000B` | 66 | advisor topic |
| `022E288E:422E28E8:00000000` | 14 | utilities-advisor flags |
| `20631788:A0AB89F0:00000000` | 16 | occupant counters |
| `206C6E7C:00ABF2EC:00000000` | 34 | police |
| `206C6E7C:02619041:00000000` | 24 | petitions |
| `206C6E7C:02BF0033:00000000` | 212 | water |
| `206C6E7C:20EC9849:00000000` | 94 | — |
| `206C6E7C:2147C2DD:00000000` | 24 | network list header |
| `206C6E7C:A0F42214:00000000` | 34 | fire |
| `206C6E7C:A106CF3D:00000000` | 94 | — |
| `206C6E7C:C106C4F5:00000000` | 1994 | — |
| `206C6E7C:C28D0B6E:00000000` | 12 | rewards |
| `206C6E7C:E0AFDF68:00000000` | 668 | power |
| `E0FAADC7:A0AB89F0:00000001` | 8 | occupant entries (empty) |
| `E1F6ABE2:02963821:00000000` | 12 | disaster table entry |
| `E1F6ABE2:0336F76A:00000000` | 12 | disaster table entry |
| `E1F6ABE2:22963800:00000000` | 12 | disaster table entry |
| `E1F6ABE2:24889F78:00000000` | 12 | disaster table entry |
| `E1F6ABE2:4296380E:00000000` | 12 | disaster table entry |
| `E1F6ABE2:45326359:00000000` | 16 | disaster table entry |
| `E1F6ABE2:621CDA33:00000000` | 12 | disaster table entry |
| `E1F6ABE2:C336F77C:00000000` | 12 | disaster table entry |
| `E1F6ABE2:C446F87C:00000000` | 12 | disaster table entry |
| `E1F6ABE2:C4C90997:00000000` | 46 | disaster table entry |

The twelve 4-byte records `206C6E7C:409FF3BA:<n>` (instances 2, 3, 4, 6, 7, 8, 10, 11, 12, 15, 16, 18) hold one u32 each:

| Instances | Value (little-endian bytes) |
|---|---|
| 2, 3, 4 | 4ea69b61 |
| 6, 7, 8 | c1ada341 |
| 10, 11, 12 | 303ba5e1 |
| 15 | 7e1ef8c1 |
| 16 | 2bd7d282 |
| 18 | e58d3482 |

`013DEE82:022E288E:0000000B` (advisor topic, 45 bytes)

```
0000  01 00 02 00 ef be ad de 01 00 00 00 01 00 00 00 01 00 00 00 01 00 00 00 0e 01 00 00 06 00 00 00
0020  00 00 00 00 00 00 00 00 08 00 00 00 00
```

`013DEE82:028D0FC5:0000000B` (advisor topic, 108 bytes)

```
0000  01 00 02 00 ef be ad de 01 00 00 00 04 00 00 00 01 00 00 00 01 00 00 00 18 01 00 00 0f 00 00 00
0020  00 00 00 00 00 00 00 00 08 00 00 00 00 53 00 00 00 10 00 00 00 00 00 00 00 00 00 00 00 08 00 00
0040  00 00 23 01 00 00 00 00 00 00 ba 99 a4 c2 00 00 00 00 08 00 00 00 00 24 01 00 00 00 00 00 00 be
0060  99 a4 e2 00 00 00 00 08 00 00 00 00
```

`013DEE82:828D04EB:0000000B` (advisor topic, 24 bytes)

```
0000  01 00 02 00 ef be ad de 01 00 00 00 00 00 00 00 01 00 00 00 01 00 00 00
```

`013DEE82:828D0A2F:0000000B` (advisor topic, 66 bytes)

```
0000  01 00 02 00 ef be ad de 01 00 00 00 02 00 00 00 01 00 00 00 01 00 00 00 1f 01 00 00 00 00 00 00
0020  c9 98 a4 42 00 00 00 00 08 00 00 00 00 20 01 00 00 00 00 00 00 d1 98 a4 22 00 00 00 00 08 00 00
0040  00 00
```

`013DEE82:C28D0B6E:0000000B` (advisor topic, 87 bytes)

```
0000  01 00 02 00 ef be ad de 01 00 00 00 03 00 00 00 01 00 00 00 01 00 00 00 0f 01 00 00 0b 00 00 00
0020  00 00 00 00 00 00 00 00 08 00 00 00 00 de 00 00 00 0c 00 00 00 00 00 00 00 00 00 00 00 08 00 00
0040  00 00 db 00 00 00 0d 00 00 00 00 00 00 00 00 00 00 00 08 00 00 00 00
```

`013DEE82:C28D0F40:0000000B` (advisor topic, 66 bytes)

```
0000  01 00 02 00 ef be ad de 01 00 00 00 02 00 00 00 01 00 00 00 01 00 00 00 21 01 00 00 00 00 00 00
0020  84 94 a4 e2 00 00 00 00 08 00 00 00 00 26 01 00 00 00 00 00 00 7c 94 a4 a2 00 00 00 00 08 00 00
0040  00 00
```

`022E288E:422E28E8:00000000` (utilities-advisor flags, 14 bytes)

```
0000  01 00 02 00 ef be ad de 01 01 01 01 01 01
```

`20631788:A0AB89F0:00000000` (occupant counters, 16 bytes)

```
0000  02 00 02 00 ef be ad de 00 00 00 00 01 00 00 00
```

`206C6E7C:00ABF2EC:00000000` (police, 34 bytes)

```
0000  00 04 00 00 00 08 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0020  00 00
```

`206C6E7C:02619041:00000000` (petitions, 24 bytes)

```
0000  01 00 02 00 ef be ad de 01 00 00 00 00 00 00 00 01 00 00 00 01 00 00 00
```

`206C6E7C:02BF0033:00000000` (water, 212 bytes)

```
0000  04 00 02 00 ef be ad de 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0020  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0040  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0060  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0080  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 64 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00a0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00c0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

`206C6E7C:20EC9849:00000000` (—, 94 bytes)

```
0000  02 00 02 00 ef be ad de 02 00 00 00 88 13 00 00 00 00 00 00 00 00 00 00 00 00 00 00 19 00 00 00
0020  00 00 00 00 88 88 88 3d 00 00 00 00 00 00 00 00 00 00 00 00 00 89 13 00 00 a8 61 00 00 00 00 00
0040  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 58 9e ff ff a8 61 00 00 00 00 00 00
```

`206C6E7C:2147C2DD:00000000` (network list header, 24 bytes)

```
0000  04 00 02 00 ef be ad de 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00
```

`206C6E7C:A0F42214:00000000` (fire, 34 bytes)

```
0000  00 04 00 00 00 0c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0020  00 00
```

`206C6E7C:A106CF3D:00000000` (—, 94 bytes)

```
0000  02 00 02 00 ef be ad de 02 00 00 00 40 1f 00 00 00 00 00 00 00 00 00 00 00 00 00 00 7d 00 00 00
0020  00 00 00 00 88 88 88 3d 00 00 00 00 00 00 00 00 00 00 00 00 00 41 1f 00 00 70 11 01 00 00 00 00
0040  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 90 ee fe ff 70 11 01 00 00 00 00 00
```

`206C6E7C:C106C4F5:00000000` (—, 1994 bytes)

```
0000  03 00 02 00 ef be ad de 02 00 00 00 b8 0b 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0020  00 00 00 00 88 88 88 3d 00 00 00 00 00 00 00 00 00 00 00 00 00 b9 0b 00 00 a8 61 00 00 00 00 00
0040  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 58 9e ff ff a8 61 00 00 00 00 00 00 00 00
0060  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0080  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00a0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00c0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 27
00e0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0100  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0120  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0140  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0160  00 00 96 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0180  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01a0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01c0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01e0  00 00 00 00 00 00 59 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0200  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0220  00 00 00 00 80 40 00 00 00 41 00 00 40 41 00 00 88 41 00 00 a8 41 00 00 c8 41 00 00 e8 41 00 00
0240  08 42 00 00 18 42 00 00 28 42 00 00 38 42 00 00 4c 42 00 00 5c 42 00 00 6c 42 00 00 7c 42 00 00
0260  88 42 00 00 90 42 00 00 98 42 00 00 a0 42 00 00 aa 42 c4 c0 a6 42 69 91 a3 42 a1 71 a0 42 1f 61
0280  9d 42 99 5f 9a 42 c6 6c 97 42 5e 88 94 42 1a b2 91 42 b5 e9 8e 42 eb 2e 8c 42 7a 81 89 42 21 e1
02a0  86 42 9f 4d 84 42 b6 c6 81 42 50 98 7e 42 72 bb 79 42 5b f6 74 42 96 48 70 42 b2 b1 6b 42 3f 31
02c0  67 42 cf c6 62 42 f6 71 5e 42 4b 32 5a 42 66 07 56 42 e2 f0 51 42 5b ee 4d 42 6f ff 49 42 bf 23
02e0  46 42 ec 5a 42 42 9a a4 3e 42 6f 00 3b 42 12 6e 37 42 2c ed 33 42 67 7d 30 42 70 1e 2d 42 f4 cf
0300  29 42 a3 91 26 42 2e 63 23 42 48 44 20 42 a4 34 1d 42 f8 33 1a 42 fa 41 17 42 63 5e 14 42 ec 88
0320  11 42 50 c1 0e 42 4c 07 0c 42 9d 5a 09 42 02 bb 06 42 3b 28 04 42 09 a2 01 42 5d 50 fe 41 df 74
0340  f9 41 21 b1 f4 41 af 04 f0 41 17 6f eb 41 e9 ef e6 41 b8 86 e2 41 18 33 de 41 a0 f4 d9 41 e9 ca
0360  d5 41 8d b5 d1 41 28 b4 cd 41 59 c6 c9 41 c0 eb c5 41 ff 23 c2 41 ba 6e be 41 96 cb ba 41 3b 3a
0380  b7 41 52 ba b3 41 33 33 a0 42 3c 18 9e 42 e7 fe 9b 42 32 e7 99 42 1c d1 97 42 a5 bc 95 42 ce a9
03a0  93 42 93 98 91 42 f2 88 8f 42 eb 7a 8d 42 7e 6e 8b 42 a6 63 89 42 64 5a 87 42 b8 52 85 42 a0 4c
03c0  83 42 1a 48 81 42 46 8a 7e 42 7d 87 7a 42 cd 87 76 42 3b 8b 72 42 bd 91 6e 42 58 9b 6a 42 08 a8
03e0  66 42 c6 b7 62 42 92 ca 5e 42 6e e0 5a 42 52 f9 56 42 40 15 53 42 31 34 4f 42 27 56 4b 42 1b 7b
0400  47 42 0e a3 43 42 00 ce 3f 42 ea fb 3b 42 c9 2c 38 42 9c 60 34 42 63 97 30 42 1e d1 2c 42 c7 0d
0420  29 42 5a 4d 25 42 d6 8f 21 42 3b d5 1d 42 84 1d 1a 42 b2 68 16 42 be b6 12 42 a9 07 0f 42 72 5b
0440  0b 42 15 b2 07 42 8c 0b 04 42 dc 67 00 42 01 8e f9 41 e7 51 f2 41 6a 1b eb 41 8b ea e3 41 3e bf
0460  dc 41 85 99 d5 41 53 79 ce 41 aa 5e c7 41 89 49 c0 41 e5 39 b9 41 bf 2f b2 41 0b 2b ab 41 cb 2b
0480  a4 41 fd 31 9d 41 97 3d 96 41 9a 4e 8f 41 fa 64 88 41 b8 80 81 41 a6 43 75 41 82 90 67 41 04 e8
04a0  59 41 2c 4a 4c 41 e4 b6 3e 41 18 2e 31 41 dc af 23 41 1b 3c 16 41 c0 d2 08 41 96 e7 f6 40 78 3e
04c0  dc 40 26 aa c1 40 75 2a a7 40 3a bf 8c 40 ed d0 64 40 52 4c 30 40 4a e1 f7 3f 20 7b 8f 3f 96 98
04e0  9d 3e 00 00 00 00 00 00 00 00 00 00 00 00 2e 15 88 3c 9d bf 87 3c 4e 6a 87 3c 41 15 87 3c 75 c0
0500  86 3c ea 6b 86 3c a0 17 86 3c 97 c3 85 3c ce 6f 85 3c 46 1c 85 3c fe c8 84 3c f6 75 84 3c 2d 23
0520  84 3c a4 d0 83 3c 5b 7e 83 3c 51 2c 83 3c 86 da 82 3c f9 88 82 3c ab 37 82 3c 9c e6 81 3c cb 95
0540  81 3c 38 45 81 3c e3 f4 80 3c cb a4 80 3c f1 54 80 3c 55 05 80 3c ea 6b 7f 3c a5 cd 7e 3c da 2f
0560  7e 3c 88 92 7d 3c af f5 7c 3c 4e 59 7c 3c 65 bd 7b 3c f4 21 7b 3c fa 86 7a 3c 77 ec 79 3c 6b 52
0580  79 3c d5 b8 78 3c b5 1f 78 3c 0b 87 77 3c d6 ee 76 3c 16 57 76 3c cb bf 75 3c f4 28 75 3c 91 92
05a0  74 3c a1 fc 73 3c 24 67 73 3c 1a d2 72 3c 82 3d 72 3c 5d a9 71 3c a9 15 71 3c 67 82 70 3c 96 ef
05c0  6f 3c 36 5d 6f 3c 46 cb 6e 3c c6 39 6e 3c b6 a8 6d 3c 15 18 6d 3c e3 87 6c 3c 20 f8 6b 3c cc 68
05e0  6b 3c e6 d9 6a 3c 6d 4b 6a 3c 62 bd 69 3c c4 2f 69 3c 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0600  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0620  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0640  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 82 42 12 9b 81 42 72 36
0660  81 42 20 d2 80 42 1c 6e 80 42 66 0a 80 42 fb 4d 7f 42 c4 87 7e 42 27 c2 7d 42 24 fd 7c 42 b9 38
0680  7c 42 e7 74 7b 42 ad b1 7a 42 0b ef 79 42 00 2d 79 42 8b 6b 78 42 ad aa 77 42 64 ea 76 42 b1 2a
06a0  76 42 92 6b 75 42 08 ad 74 42 12 ef 73 42 af 31 73 42 df 74 72 42 a2 b8 71 42 f7 fc 70 42 de 41
06c0  70 42 56 87 6f 42 5f cd 6e 42 f8 13 6e 42 21 5b 6d 42 da a2 6c 42 22 eb 6b 42 f8 33 6b 42 5c 7d
06e0  6a 42 4e c7 69 42 ce 11 69 42 db 5c 68 42 74 a8 67 42 99 f4 66 42 4a 41 66 42 86 8e 65 42 4d dc
0700  64 42 9e 2a 64 42 79 79 63 42 de c8 62 42 cc 18 62 42 42 69 61 42 41 ba 60 42 c8 0b 60 42 d6 5d
0720  5f 42 6b b0 5e 42 87 03 5e 42 29 57 5d 42 51 ab 5c 42 fe ff 5b 42 30 55 5b 42 e7 aa 5a 42 22 01
0740  5a 42 e1 57 59 42 23 af 58 42 e8 06 58 42 30 5f 57 42 fa b7 56 42 46 11 56 42 13 6b 55 42 61 c5
0760  54 42 30 20 54 42 7f 7b 53 42 4e d7 52 42 9d 33 52 42 6b 90 51 42 b7 ed 50 42 82 4b 50 42 cb a9
0780  4f 42 91 08 4f 42 d4 67 4e 42 94 c7 4d 42 d1 27 4d 42 8a 88 4c 42 be e9 4b 42 6d 4b 4b 42 97 ad
07a0  4a 42 3c 10 4a 42 5b 73 49 42 f4 d6 48 42 06 3b 48 42 91 9f 47 42 95 04 47 42 11 6a 46 42 00 00
07c0  00 00 00 00 00 00 00 00 00 00
```

`206C6E7C:C28D0B6E:00000000` (rewards, 12 bytes)

```
0000  01 00 02 00 ef be ad de 00 00 00 00
```

`206C6E7C:E0AFDF68:00000000` (power, 668 bytes)

```
0000  01 00 02 00 ef be ad de 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0020  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0040  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0060  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0080  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00a0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00c0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00e0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0100  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0120  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0140  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0160  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0180  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01a0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01c0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01e0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0200  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0220  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0240  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0260  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0280  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 64 00 00 00 00 00 00 00
```

`E0FAADC7:A0AB89F0:00000001` (occupant entries (empty), 8 bytes)

```
0000  02 00 02 00 ef be ad de
```

`E1F6ABE2:02963821:00000000` (disaster table entry, 12 bytes)

```
0000  01 00 02 00 ef be ad de 00 00 00 00
```

`E1F6ABE2:0336F76A:00000000` (disaster table entry, 12 bytes)

```
0000  01 00 02 00 ef be ad de 00 00 00 00
```

`E1F6ABE2:22963800:00000000` (disaster table entry, 12 bytes)

```
0000  01 00 02 00 ef be ad de 00 00 00 00
```

`E1F6ABE2:24889F78:00000000` (disaster table entry, 12 bytes)

```
0000  01 00 02 00 ef be ad de 00 00 00 00
```

`E1F6ABE2:4296380E:00000000` (disaster table entry, 12 bytes)

```
0000  01 00 02 00 ef be ad de 00 00 00 00
```

`E1F6ABE2:45326359:00000000` (disaster table entry, 16 bytes)

```
0000  02 00 02 00 ef be ad de 00 00 00 00 00 00 00 00
```

`E1F6ABE2:621CDA33:00000000` (disaster table entry, 12 bytes)

```
0000  01 00 02 00 ef be ad de 00 00 00 00
```

`E1F6ABE2:C336F77C:00000000` (disaster table entry, 12 bytes)

```
0000  01 00 02 00 ef be ad de 00 00 00 00
```

`E1F6ABE2:C446F87C:00000000` (disaster table entry, 12 bytes)

```
0000  01 00 02 00 ef be ad de 00 00 00 00
```

`E1F6ABE2:C4C90997:00000000` (disaster table entry, 46 bytes)

```
0000  02 00 02 00 ef be ad de 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 5f 74 17 a3 0c 3b
0020  00 00 1f 74 23 e2 5f 74 17 a3 00 00 00 00
```

## 5. Layer records (size depends on map edge W)

Each layer starts with its own 8-byte header (version word, flags word, as in `10-save-format.md` §4);
the payload follows. In a new city the payload is zero except where stated. Sizes are shown for
W = 64 / 128 / 192 / 256 (Miniature to Large).

| Record | Size (W = 64 / 128 / 192 / 256) | New-city payload |
|---|---|---|
| Flammability `206C6E7C:61448030` | 4 121 / 16 409 / 36 889 / 65 561 | All zero [OBSERVED 14/14] |
| `206C6E7C:80F1E6D3` | 8 224 / 32 800 / 73 760 / 131 104 | All zero |
| `206C6E7C:01FD7A8C` | 264 / 1 032 / 2 312 / 4 104 | All zero |
| Traffic `206C6E7C:029CA804` | 25 773 / 102 573 / 230 573 / 409 773 | Zero except two bytes: value 2 at record offset 3.25·W² + 16 (13 328 / 53 264 / 120 080 / 213 008) and value 0xDC (220) 133 bytes later [OBSERVED 3 sizes; the Medium offset follows from the formula] |
| Land value `206C6E7C:C0AB8A88` | 1 186 / 4 258 / 9 378 / 16 546 | Zero except: value 4 at record offsets (size − 153) and (size − 149), and the two bytes 04 01 at offsets (size − 4) and (size − 3) |
| `206C6E7C:C259C02D` | 406 / 1 174 / 2 454 / 4 246 | Zero except: value 4 at record offset (size − 149), value 8 four bytes later, then thirty-two bytes of 0x7F at four-byte spacing, the first 13 bytes after the 8 |
| Geometry layer `406B1196:80AB8AB0`, terrain-derived layers `206C6E7C:409FF3BA:0`, `C0A81498`, `20A7AE7F` | W² dependent | Generated from the terrain, not a template |

Offsets in this table are measured from the start of the record including its 8-byte header unless
the text says "payload". An implementation should verify its generated layers against a save written
by the original for the same size.

## 6. Budget record `E11BCC69:A11BCC54:0` (1 981 bytes; 2 006 on Hard)

The first u32 of the record body is the opening funds (50 000 Easy, 20 000 Medium, 10 000 Hard; the
file shows 0xC350, 0x4E20, 0x2710). Every other non-zero byte below is identical for the three
difficulties on Easy and Medium; offsets are from the record start.

| Offset | Bytes (hex) | Count / note |
|---|---|---|
| 8 | 03 | |
| 12 | b8 0b | |
| 16 | 07 | |
| 121 | 88 13 | |
| 125 | 07 | |
| 230 | 40 1f | |
| 234 | 07 | |
| 339 | 06 | |
| 343 | 5d 2a 92 02 64 | |
| 460 | 6c 2a 92 42 64 | |
| 577 | 28 39 96 62 64 | |
| 694 | 05 f6 90 82 64 | |
| 811 | 83 39 96 a2 64 | |
| 928 | 09 f6 90 c2 64 | |
| 1425 to 1549, step 4 | 64 | 32 single bytes of value 100 |
| 1553 | c8 | |

On **Hard** the record is 25 bytes longer. Compared with Easy, the Hard record holds these extra
non-zero values near offset 1253 (offsets are in the Hard file): byte 1 at 1253; byte 1 at 1257;
the u16 2000 (the start year) at 1258; u16 10 000 at 1262; u16 12 000 at 1266 and at 1270; byte 100
at 1278; u16 10 000 at 1338. This is the single 10 000 bond of `19-sim-economy.md` §3; only the
values are observed here, not each word's role. Everything after offset 1253 moves down by 25 bytes,
so the run of 100-valued bytes begins at 1450 and ends with 0xC8 at 1578. Easy and Medium store no
bond. Other meanings are those of `19-sim-economy.md` §3 and §7.

## 7. Per-city values

* **World record last word** (`206C6E7C:E11BDDF6`, bytes 721–724): a fresh 32-bit value per city. All
  14 files differ. Write a random u32.
* **City id** and the **neighbour names/populations**: see `10-save-format.md` §3.2 and §6.6.
* **Terrain**: random, from the terrain generator.

## 8. Use

An implementation creating a city should (1) generate the terrain, (2) write every record of §4
verbatim, (3) write the layers of §5 for the chosen size, (4) write the budget record of §6 with the
funds and bond for the chosen difficulty, (5) set the tool-availability record from the start year,
and (6) fill the per-city values of §7.

## Open questions

1. Meanings of the individual words of the records in §4. Their values are given, not explained.
2. Why the three Miniature saves carry a non-zero word at bytes 20–23 of the disaster manager.
3. Whether the traffic record's two non-zero bytes and the land-value trailing bytes are scalar
   statistics or part of a layout the format document describes differently.
