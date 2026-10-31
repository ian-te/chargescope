#!/usr/bin/env python3
"""Turn a ChargeScope device report (All readings -> Copy all) into a test fixture.

    tests/import-report.py report.txt tests/devices/<device>/<state>

writes <state>/sys/class/... with one file per reading, and a uevent per power supply, as the
kernel has them. Then run the tests with CHARGESCOPE_UPDATE=1 and check the expected.txt it
writes against what the phone showed.
"""
import os
import sys


def main(report, target):
    supplies = {}   # path -> {name: value}
    path = None
    for line in open(report, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("/sys/class/"):
            path = line.strip()
            supplies[path] = {}
        elif path and line.startswith("  ") and " = " in line:
            name, _, value = line.strip().partition(" = ")
            supplies[path][name] = value
        elif not line.strip():
            path = None

    for path, readings in supplies.items():
        folder = os.path.join(target, path.lstrip("/"))
        os.makedirs(folder, exist_ok=True)
        for name, value in readings.items():
            if name.islower():
                with open(os.path.join(folder, name), "w") as f:
                    f.write(value + "\n")
        if path.startswith("/sys/class/power_supply/"):
            # The report lists a supply's uevent with POWER_SUPPLY_ taken off its own keys.
            lines = ["POWER_SUPPLY_NAME=" + os.path.basename(path)]
            for name, value in readings.items():
                key = "POWER_SUPPLY_" + name.upper() if name.islower() else name
                lines.append(key + "=" + value)
            with open(os.path.join(folder, "uevent"), "w") as f:
                f.write("\n".join(lines) + "\n")
    print("wrote %d folders under %s" % (len(supplies), target))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
