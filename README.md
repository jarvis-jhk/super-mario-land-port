# Super Mario Land port

A cycle-exact C translation that loads its graphics, levels, music and timing
from the player's own **Super Mario Land (World) Rev A / v1.1** ROM. No ROM or
game artwork is distributed. The production core contains no SM83 interpreter.

The Linux frontend is implemented. Windows cross-builds; the four-platform M0
release, automatic updates and in-game changelog are still being prepared.
Planned releases cover Linux, Windows, PortMaster handhelds and Steam Deck.

- [Playing and controls](docs/PLAYING.md)
- [Building from source](docs/BUILDING.md)
- [Roadmap and current priorities](ROADMAP.md)
- [Verification and project state](docs/PLAN.md)
- [Public-tree content audit](docs/PUBLIC_AUDIT.md)
- [Anonymous fault reporting and opt-out](docs/FAULT_REPORTS.md)

Use a legally obtained personal ROM. Put it at rom/sml.gb for local development;
all ROM material and generated analysis stay outside the public tree.
