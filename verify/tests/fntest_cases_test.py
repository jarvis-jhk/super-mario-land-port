#!/usr/bin/env python3
"""Exercise fntest's input and coverage failures using the local ROM oracle."""
from pathlib import Path
import subprocess
import tempfile


def run(cases, requirements=None, status=0, diagnostic=""):
    with tempfile.TemporaryDirectory(dir="build", prefix="fntest-input-") as tmp:
        case_path = Path(tmp) / "cases.txt"
        case_path.write_text(cases)
        command = ["build/fntest", "rom/sml.gb", "--model", "agb", "--movie",
                   "build/resync11.movie", "--polls", "100", "--samples", "0",
                   "--fuzz", "0", "--cases", str(case_path)]
        if requirements is not None:
            required_path = Path(tmp) / "required.txt"
            required_path.write_text(requirements)
            command += ["--require-coverage", str(required_path)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        assert result.returncode == status, (command, result.returncode, output)
        assert diagnostic in output, (diagnostic, output)


# All register inputs run through the real ROM and the translation; no mock.
run("SetupChannel.square2 hl=66EE\nSpawnPowerup a=0\n"
    "CopySlotToBufferAt hl=D100\nCopyBufferToSlotAt hl=D100\n"
    "CopyPointer hl=DF10 de=DF20\nScrollEnemies bc=0\nScrollEnemies bc=100\n",
    "SetupChannel.square2 tested\nScrollEnemies 0:2CA1\n")

# Deleting a sample, keeping only one branch side, or naming a site from
# another bank must fail even though the differential comparisons pass.
run("ScrollEnemies bc=0\n", "SetupChannel.square2 tested\n", 1, "missing coverage")
run("ScrollEnemies bc=0\n", "ScrollEnemies 0:2CA1\n", 1, "missing coverage")
run("SetupChannel.square2 hl=66EE\n", "SetupChannel.square2 2:6A03\n",
    1, "missing coverage")

for field in ["hl=10000", "de=10000", "bc=10000", "a=100", "a=", "hl=",
              "hl=oops", "C000=100", "C000=", "=1", "bank=0", "bank=4",
              "FFFF=1", "0000=1"]:
    run("ScrollEnemies " + field + "\n", status=2, diagnostic="invalid function case")

for requirement in ["Unknown tested", "ScrollEnemies", "ScrollEnemies tested extra",
                    "ScrollEnemies 4:2CA1", "ScrollEnemies 1:2CA1",
                    "ScrollEnemies 0:6A03", "ScrollEnemies 0:10000",
                    "ScrollEnemies :2CA1", "ScrollEnemies 0:"]:
    run("ScrollEnemies bc=0\n", requirement + "\n", 1,
        "invalid coverage requirement")

print("fntest inputs and coverage contracts: ok")
