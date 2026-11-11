# Phenac City and the world-map step: original floor identification

This records which original floors and archives make up the world map and
Phenac City, and which of them the main story reaches first after Outskirt
Stand. All identities below are metadata read from the user's private
GC6E01 revision 0 files; no original bytes are in this repository.

Nothing here changes the native story route. Floors 1 and 2 remain the only
story-reachable floors, and GSVM native 98 stays suspended at Willie
(encounter 10) until an authentic battle result exists. Phenac City is only
viewable through the separately built [developer floor loader](dev-floor-loader.md).

## Sources

| Source | Use |
| --- | --- |
| `common_rel.rel` (REL125) section 4, lifecycle table `0x8BCF4`, 196 rows of 76 bytes | Floor ID, resource group, script/callback words (`tools/prepare_story_index.py metadata`) |
| `files/GSfsys.toc` | Resource group to archive name |
| `vendor/colosseum/src/rel/M1_out/M1_out.c` (REL15) | Phenac exterior entrance, person and camera tables |
| `vendor/colosseum/src/rel/worldmap_menu_rel/worldmap_menu_rel.c` (REL3) | World-map location, route-camera and cursor-link tables |
| Private story index (`tools/prepare_story_index.py`, 2026-11-09 run, counts match [story-source-index.md](story-source-index.md)) | Which script writes each world-map unlock flag |

## World map

| Field | Value |
| --- | --- |
| Lifecycle row | 121 |
| Floor | `0x38E` (a menu-class floor, row flags `0xA0090000`) |
| Resource group / archive | `0x13` / `world_map.fsys` (1,583,840 bytes, SHA256 `50ddbbeb40b0c18b77eb567efc1bbbebc74160767674f81cf7cc7d7ac92a759e`) |
| REL | `worldmap_menu_rel.rel`, module 3; also member `0D701C00` |
| Script bank | `01E00E00` |
| 3D Orre map | `0C6A0200` (type 02) |
| Bike actors | `0C6C0400` (the same bike model as the Outskirt arrival) and `0C6B0400` |
| Route cameras | 18 type-0x18 members `0C6D1800`–`0C7B1800` |
| Location images | 12 type-0x12 members (`0C5F1200`–`0C691200`, `0C991200`) |

REL3 publishes four tables. `lbl_3_data_0` holds nine 16-byte location
records `{unlock flag, destination floor, name message, image}`:

| Location | Unlock flag | Floor | Archive | Name msg | Flag writer (native 0x4E) |
| ---: | ---: | ---: | --- | ---: | --- |
| 0 | none | `0x01` | `S1_out.fsys` (Outskirt Stand) | `0x907` | always available |
| 1 | `0xADF` | `0x88` | `S2_out_2.fsys` | `0x908` | `M2_out.fsys` bank `01400E00` fn 7 |
| 2 | `0xDCF` | `0x41` | `D1_out.fsys` | `0x909` | `M3_houseC_1F.fsys` bank `021C0E00` fn 4 |
| 3 | `0x42E` | `0x4A` | `D2_out.fsys` | `0x90A` | `M3_houseC_1F.fsys` bank `021C0E00` fn 14 |
| 4 | `0xA59` | `0x66` | `D4_out.fsys` | `0x90B` | `M1_out.fsys` bank `013E0E00` fn 26 |
| **5** | **`0x3DF`** | **`0x03`** | **`M1_out.fsys` (Phenac City)** | **`0x90C`** | **`S1_out.fsys` bank `01430E00` fn 9** |
| 6 | `0x3F8` | `0x0F` | `M2_out.fsys` (Pyrite Town) | `0x90D` | `D4_out.fsys` bank `02CC0E00` fn 2 |
| 7 | `0x9DF` | `0x24` | `M3_out.fsys` (Agate Village) | `0x918` | `M2_out.fsys` bank `01400E00` fn 26 |
| 8 | `0xA0E` | `0x7A` | `M4_out_2.fsys` | `0x919` | `M4_out_2.fsys` bank `02340E00` fn 2 |

Each flag has exactly one static writer in the index. Phenac's flag `0x3DF` is
written only by Outskirt Stand's own field bank, so Phenac City is the one
world-map destination that Outskirt Stand unlocks. That is why it is the
first floor the main story visits after Outskirt Stand. The index does not
resolve the branch conditions leading to that function 9 site; executing it
requires the authentic Willie result and post-battle scripts.

`lbl_3_data_270` holds twenty route-camera records
`{0x01000000, index, camera member, rate}` (rates 1.0 and 0.5) over the 18
camera members. `lbl_3_data_3B0` holds 66 cursor links
`{direction mask, from name, to name}`; Outskirt (`0x907`) links to Phenac
(`0x90C`) with masks `0x08` and `0x0C`. The DOL code that chooses a route
camera for a journey is not decompiled at the pinned source, so the camera
for Outskirt→Phenac is **not yet identified**.

The world map is a menu scene, not a walkable field: it has no CCD collision
or entrance table, and its behaviour lives in DOL menu code plus REL3 tables.
It is therefore **not** part of the developer field loader. Bringing it up
needs the world-map menu logic, route-camera selection and the bike/marker
presentation.

## Phenac City

Phenac's floors all use resource groups `0x1F`–`0x2C` (REL row flag high word
`0x2002`). The story's first destination is the exterior:

| Field | Value |
| --- | --- |
| Lifecycle row | 2 |
| Floor | `0x03` |
| Resource group / archive | `0x1F` / `M1_out.fsys` (2,554,304 bytes, SHA256 `85b1bbd23d9dc32287f90ac19eb8b8bd1b42b83dcfa9c92025490aaf5b2b5987`) |
| Field script bank | `013E0E00` (48,416 bytes) |
| Map scene | `00620200`: nine models, one camera, five lights |
| CCD collision | `00640600`: 46 identity objects, 683 walk and 914 player-wall triangles |
| REL | `06611C00`, module 15, data in section 3 (same layout as REL131) |
| Entrances | `lbl_15_data_0`, 10 records at section offset 0 |
| People | `lbl_15_data_A0`, 27 records of 36 bytes |
| Message pack | `015A0A00` |
| Other cameras | `00601800`, `00611800`, `005F1800`, `0C101800` (event cameras, not used) |

Entrance 0 is at source `(0, 0, 336)` facing 180°, inside the source
camera's framing of the town square. The world-map menu ends with
`floorLink(floor, 0)`, so the arrival uses entrance 0 (see
[world-map.md](world-map.md)).

The other Phenac floors, from the lifecycle table:

| Row | Floor | Archive |
| ---: | ---: | --- |
| 62 | `0x04` | `M1_pc_1F.fsys` |
| 115 | `0x77` | `M1_pc_B1.fsys` |
| 63 | `0x05` | `M1_houseA_1F.fsys` |
| 64 | `0x06` | `M1_houseA_2F.fsys` |
| 65 | `0x07` | `M1_houseB_1F.fsys` |
| 66 | `0x08` | `M1_houseC_1F.fsys` |
| 67 | `0x09` | `M1_shop_1F.fsys` |
| 68 | `0x0A` | `M1_shop_2F.fsys` |
| 69 | `0x0B` | `M1_gym_1F.fsys` |
| 70 | `0x0C` | `M1_gym_B1.fsys` |
| 71 | `0x0D` | `M1_stadium_1F.fsys` |
| 72 | `0x0E` | `M1_water_colo.fsys` (colosseum, row flags `0x40020000`) |
| 129 | `0x80` | `M1_carde_1.fsys` |
| 150–152 | `0xCA`–`0xCC` | `M1_out_bf`, `M1_gym_1F_bf`, `M1_houseA_1F_bf` (battle fields) |

Only floor `0x03` has a developer pack profile. The interiors' story order is
not established here.
