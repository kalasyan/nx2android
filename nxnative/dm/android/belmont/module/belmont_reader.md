# Belmont read-only companion substrate

Engineering scaffolding; no product layout or distributable companion yet. Supports only
title `0100BA4026E94000` and complete executable build
`0F1169AA7D0FA4529C861F96ADEC306E00000000000000000000000000000000`
(effective update1.1.0). Unsupported builds perform no guest reads. State and dictionary
accessors additionally require exact original instruction spans. Successful reads are
journalled and rechecked before publication; incoherence discards the whole snapshot.
No guest writes, guest calls or gameplay actions are implemented.

State flags use -1 unavailable,0 false,1 true. Managed initial/loading pointer identities
are checked separately from exact class/namespace and native cached-pointer liveness.
Class identity alone never establishes gameplay. Components are sampled only when the
initial gameplay identity is active or a validated native paused state is active,
and both loading flags are known false. Native paused classification requires
pauseEntityContext/pausePlaytimeCount/hideHUD true and stopRoomManagerSystem false,
with exact original getters, class/native liveness and coherent flags. It enables only
fresh read-only map data; no menu input privilege follows from it.

`belmont.map_ready` and `belmont.marker_ready` always publish0. Every diagnostic key is
overwritten each sample, including failures. `belmont.state_reason` is diagnostic text,
not localized product copy. Raw candidate coordinates require position_candidate; zero
coordinates are not evidence of availability. Exploration count is dictionary live count,
not completion. Only raw enum2/3 denote revealed chunks;0/1 remain distinct.

The component dictionary walk validates counts, capacity, entry links, hashes, deleted
entries, duplicate keys/types and class/namespace. Explorer ownership must match the
sampled Entity. Room candidates require Ready0 and a live RoomInstance; null lazily
initialized room IDs remain unavailable. Position is finite targetPosition, pending live
projection comparison. Typed exploration records require exact string/state classes,
dimensions, array lengths, enum bounds and coherent bytes. An available empty dictionary
is distinct from an unavailable one; all records clear on invalidation. Reader caps are
safety policies, not asserted game maxima.

Entered-zone diagnostics are separate from chunk cells. Each ExplorationRoom exposes
`entered_zones_ready` and exact `entered_zone_ids`; snapshot `exploration_zones_ready`
requires every decoded room list to be available. The native source is
WorldRoomExplorationState.enteredZonesIds+0x50. Original EnteredZone(int)1C298A8 and
SetEnteredZone1C296B0/1C296E0 instruction spans are checked before sampling: list
items+10,size+18,version+1C, array length+18,data+20 with4-byte signed IDs.
The exact managed class/namespace must be `List\`1` / `System.Collections.Generic`.
List count<=4096, backing capacity<=8192, IDs0..65535 and uniqueness are reader policy
limits. A coherent non-null list ofsize0 is known empty; a null/malformed list is
unavailable, preserving valid chunk diagnostics but clearing entered IDs. Native
SetEnteredZone lazy-allocates a null list; native fog processing requires a list.
Every successful header/payload read is journalled. List pointer, backing pointer,
version, count or in-place item mutation invalidates the entire snapshot; loading,
entity loss and subsequent unavailable samples clear prior IDs. Read/recheck is not
an atomic game snapshot and cannot rule out ABA changes.

Native fog uses entered IDs joined to actual authored room/zone shapes plus navigation
zone culling. Chunk state2/3 is not a general replacement. Synthetic tests establish
parser behavior only. The read-only RSP6593 snapshot on2026-10-20 was coherent but in
non_gameplay_identity, so no list was exposed and the actual List class namespace is
still pending live validation. Map readiness remains false.

`belmont_map_assets` reads installed native StreetsPoor registry/room chunk geometry;
it contains no derived positions or occupancy. Full serialized WorldData parsing and a
bounded native RoomData prefix are required. Missing/malformed input returns unavailable.
Geometry is static and never a reveal mask; callers must join actual valid exploration.
Native meshes and fog rendering are not exposed yet.

`belmont_assets` decodes native Switch textures, including GOB=1 block layouts, and exact
TMP LatinFont/NotoSans_Latin source metrics. The callback adapter supplies paired coverage
atlases/host integer metrics from supported regular-face SDF materials. Native artwork and
font data come from the player's own RomFS; no extracted assets ship. Unsupported codecs,
materials and fonts decline without a fallback. Crunched textures are unsupported.

Image keys: `module:native:<bundle basename>#<signed texture path ID>` or
`module:belmont-font:LatinFont:35` (also NotoSans_Latin, cap24..96). Authored font descriptors
are exact ASCII `BELMONT_FONT_V1 LatinFont 35` plus one LF. Matching atlas/metrics share a
bounded cache. The source metrics stay floating point; the host font ABI rounds bearings
and advances. Coverage conversion handles regular orthographic SDF faces, not arbitrary
bold/smallcaps/perspective/outline text effects. ASTC uses the existing negotiated host decoder.

Linux development build/test, without shared build graph changes:

```sh
bash native/modules/belmont_build.sh /absolute/path/to/runtime /tmp/belmont-reader-build
```

Requires the runtime's existing bc_decoder source and system LZ4. Exports the module, base
extension and font extension entrypoints only. Synthetic tests cover build/code rejection,
tri-state loading, native liveness, bounds, malformed dictionaries/masks, deleted entries,
concurrent byte mutation and state clearing; no game fixtures are required. The separate
map integration test accepts player-extracted native bundle paths as development inputs.

Native texture/font/geometry decoding has independent desktop comparisons. Android arm64
links are compile evidence; execution and real AYN Thor verification remain pending. Live
map/marker projection, menu transitions and gameplay performance require further evidence.
