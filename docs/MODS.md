# Mods: package model and delivery plan

Requested 2026-10-11. This is the implementation contract for the roadmap,
not a claim that mods or the lobby service are implemented.

## Modes and package types

The stock game remains the cycle-exact runtime. Presentation packages replace
output only. Packages that change collision, physics, enemy behavior or level
data run in Custom mode; loading one cannot silently turn a stock run into a
modified run. Official RetroAchievements and stock timing categories are
disabled in Custom mode. Custom records are keyed by the complete resolved
package lockfile and engine version.

| Type | Contents | Typical dependencies |
| --- | --- | --- |
| skin | Player/enemy appearance and animations; no hitbox changes | Optional target player/enemy package |
| player | Custom playable character, movement and abilities | Skins, behavior modules |
| enemy | Enemy/object definition, collision and behavior | Skins, behavior modules |
| tileset | Tile IDs, graphics, collision/material definitions | Behavior modules for custom tiles |
| level | Map, spawns, exits, rules and editor metadata | Tilesets, enemies, optional player/music |
| world | Ordered levels, transitions and world metadata | Levels |
| worldset | Ordered worlds and campaign progression | Worlds, optional player/theme |
| music | Original replacement music/effects | None |
| theme | Coordinated visual/audio/UI preset | Skins, tilesets, music |
| behavior | Reusable sandboxed Lua modules | Other behavior modules |

Each package has one primary type; compose multiple packages instead of
ambiguous all-purpose archives. Stock assets are addressed by logical IDs and
read from the installed ROM. Uploaded packages must contain original or
licensed assets, never exported ROM tiles/music. A tileset or theme that changes
collision or gameplay is Custom content even if its name sounds cosmetic.

## Manifest and dependency resolution

Use a versioned JSON manifest in a zip archive. Required fields: schema version,
namespaced immutable package ID, semantic version, type, display name, author,
license, description, engine/API range, content entrypoints, and dependencies
as package IDs plus version constraints. Optional fields: capabilities,
conflicts, provided logical assets, tags and preview metadata. The catalog
adds immutable archive SHA-256, compressed/uncompressed sizes and publication
metadata. A version's bytes cannot be replaced after publication.

Example dependency chain: `community:campaign@1` requires
`community:world-one@1`, which requires `community:level-one@2`, which requires
`community:cave-tiles@1` and `community:bat@3`. Pin every transitive dependency
to its exact version and hash in a local profile lockfile. Resolve the entire
graph before downloading or changing the installed profile. Reject cycles,
missing dependencies, incompatible engine APIs, incompatible version ranges,
duplicate logical definitions and declared conflicts with an actionable chain
such as “campaign → world-one → level-one needs cave-tiles >=2”.

An activation profile has one version per package ID. Separate profiles may
use different versions. Namespaced asset IDs avoid ordering-based overrides;
skins explicitly target an asset and conflicting replacements require a
visible selection. Never choose a winner from archive/download order.

## In-game browser and management

The lobby service belongs to this project. Start with HTTPS catalog/search,
package details, compatibility/dependency metadata and immutable downloads.
Keep the network/catalog adapter separate from the local resolver and runtime;
installed packages and profiles must work offline.

Browse by type, tags, compatibility, author and popularity. Detail pages show
previews, license, dependencies, required download size, gameplay impact and
version history. Installation previews the complete dependency plan and any
conflicts before confirmation. Management supports install, enable/disable,
profiles, update available, pinned versions, rollback and uninstall; explain
dependents before removal. Updates are offered rather than silently changing
a campaign or a replay's dependency set. Multiplayer lobbies, when implemented,
must agree on the same lockfile before play.

Download into staging, verify hashes and manifests, reject traversal/symlinks,
duplicate archive paths and archive bombs, and enforce size/count limits.
Activate all packages and the lockfile atomically only after complete validation.
Interrupted installs leave the previous profile usable. Garbage collection
removes only versions unreferenced by profiles, saves and retained replays.

Upload/edit/version publication requires an account; downloads need no account.
Add reporting, moderation/quarantine, ownership and licensing checks before
opening uploads. Voting and reviews follow the core install/management work.
Catalog outages show cached content and allow local play; authentication and
ordinary compatibility errors are user errors, not crash reports.

## Lua behavior API

Use a pinned Lua runtime and a versioned Custom-mode API. Expose narrow entity
handles and tile/player/enemy hooks: spawn/despawn, fixed tick, input, collision,
damage and interaction. Provide declared per-entity state, read-only world
queries and bounded queued commands; no pointers or arbitrary writes into the
stock RAM/timing shadow. Apply queued effects at defined tick boundaries.

Remove filesystem/process/network access, `io`, `os`, `debug`, native modules,
`package.loadlib`, arbitrary module paths and bytecode loading. Module imports
resolve only declared dependencies. Enforce instruction, memory, entity and
event-depth budgets. Use seeded engine RNG and fixed-tick time, never host clock;
define deterministic iteration and numeric rules on every supported platform.
Do not promise cross-platform scripted replays until replay tests demonstrate
identical results. Lua never runs inside the stock interrupt/timing path.

Script faults pause the affected custom session with the package ID and a useful
diagnostic; keep stock play usable. Do not continue a ranked run after a fault.
Save/replay metadata stores API version, engine version, lockfile hashes and RNG
state; upgrading scripts requires a fresh profile or explicit save migration.
Fault reporting sends only bounded error codes and package/version identifiers,
never uploaded script bodies, account tokens or user paths.

## Delivery and acceptance

1. Define schemas, namespaced asset IDs, local package loader, dependency solver,
   lockfiles and archive validation. Test transitive resolution, cycles,
   incompatible ranges, offline operation and interrupted activation.
2. Integrate presentation packages and local profiles; verify all stock TAS
   movies with packs active and no game-state changes.
3. Build Custom-mode level/tileset/enemy/player definitions and editor export;
   world/worldset composition and isolated saves/replays follow.
4. Build the lobby catalog/download service and controller-friendly in-game
   browser/manager. Verify rollback, uninstall dependents and offline starts.
5. Add bounded Lua hooks and reusable behavior packages. Test runaway scripts,
   forbidden APIs, resource exhaustion, deterministic replay and crash isolation.
6. Enable authenticated publication, moderation, voting and reviews. Release
   only after Linux, Windows, Deck and PortMaster compatibility/performance checks.
