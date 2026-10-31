# ChargeScope

Shows what a plugged-in charger offers and how a Sailfish OS phone uses it. Made for the Jolla
Phone (MediaTek Dimensity with an MT6720 charger and charge pump, Sailfish OS 5.x, aarch64); most
of it works on any phone whose kernel has the standard power-supply and Type-C sysfs classes.

- **Live power into the battery**, `battery/current_now × voltage_now`, on a gauge, with the mode:
  **Direct charging (PPS)**, **USB PD**, **Basic USB**, **Wireless** or **Not charging**.
- **Charger offers**: the source PDOs the charger advertised, with the one in use marked and the
  voltage range of any PPS (programmable) offer.
- **Charging**: VBUS, the input current and voltage limits, the charge current and voltage the
  charger IC is set to (with their ceilings), pre-charge and termination current, charger status
  and temperature.
- **Charge pump**: its status, voltage, current, temperature and limits (direct charging).
- **Battery**: current and average current, voltage, temperature, level, status, health,
  capacity against new, charge remaining, cycle count, and time to full while charging.
- **USB-C**: the charger's PD revision, the Type-C power mode (default, 1.5 A, 3.0 A or PD), power
  and data role, USB type, and the phone's own PD and Type-C revisions.
- **Charger chips**: every power supply with a plain name ("Main charge pump",
  "Charger IC · MT6720") and whether it's online.
- **The last 10 minutes** of charging power as a sparkline.
- **All readings** in the pull-down menu lists every attribute of every power supply and Type-C
  port, raw and live; **Copy report** puts a plain-text summary on the clipboard.

Every reading shows the sysfs attribute it comes from in small print under its name.

The cover shows the power and the mode. ChargeScope reads sysfs every 2 s while it is in front and
stops in the background; the cover's refresh action takes one more reading.

## Where the numbers come from

| | |
|---|---|
| Battery | `/sys/class/power_supply/battery/`: `current_now` (µA), `voltage_now` (µV), `temp` (0.1 °C), `capacity`, `status`. Drivers disagree on the sign of the current, so it is learnt from `status` while charging or discharging clearly |
| VBUS, input limit | `voltage_now` and `input_current_limit` of the first of `mtk-master-charger`, `primary_chg`, `mtk_charger_type`, `usb`, `ac`, then any other online supply, that reports one |
| Charging | The charger IC is the supply with `constant_charge_current` (`primary_chg` on the MT6720); its temperature comes from `mtk-master-charger` |
| Charge pump | The first divider supply (`*div*`, `*dvchg*`) with a `current_now` (`primary_dvchg`) |
| USB-C | `/sys/class/typec/port0/` and, while something is plugged in, `port0-partner/` |
| Charger chips | Every supply in `/sys/class/power_supply` that isn't a battery, named from its name and its `model_name` or device-tree compatible string |
| Input current, power | The charge pump's `current_now` (`primary_dvchg`): it measures the current drawn from the charger whether or not it's pumping. Input power is that times VBUS, compared with the power reaching the battery |
| Offers | The kernel log, read with `klogctl` as `dmesg` does, at start, on plug-in (again 2, 5 and 15 s later, while PD negotiates, then every 10 s while none are found) and on unplug. Offers found once are kept until the charger is unplugged, since other drivers' messages soon push them out of the log. MediaTek's `pd_adapter` logs one line per offer, `pd_get_cap_pdo cap[1], 9000mV, 3000mA, 27000000uW`, and these make one listing, and the newest listing since the last unplug is shown. If the log has none, `/sys/class/typec/port*-partner/usb_power_delivery/source-capabilities` is used where the kernel has it |
| Offer in use | The driver's `pd_get_cap_pdo nr:4 idx:2` line before each listing (idx counts from 1), unless VBUS is far from that offer; otherwise the offer VBUS matches: the nearest fixed voltage within 10 %, or the PPS offer while direct charging |
| PD revision | `usb_power_delivery_revision` of the Type-C partner, else of the port |
| Mode | Not charging when no supply is online. Wireless when a supply of type `Wireless` is online. Direct charging when a divider (charge-pump) supply is online, `usb_type` is `PD_PPS` or the offer in use is PPS. USB PD when `usb_type` is `PD`, the partner speaks PD or offers were found. Basic USB otherwise |

If the kernel log can't be read, the page says so and the rest still works.

ChargeScope is distributed through SailfishOS:Chum and OpenRepos, not Harbour. Harbour apps must
run in Sailjail, and Sailjail's seccomp filter blocks reading the kernel log, which is where
MediaTek's driver writes the charger's offers. So the app opts out of Sailjail
(`Sandboxing=Disabled`); everything else it reads is world-readable sysfs.

## Install

- **SailfishOS:Chum**: search for **ChargeScope** in the Chum app, or
  `devel-su pkcon refresh && devel-su pkcon install harbour-chargescope` once the Chum repository
  is set up.
- **OpenRepos**: from [openrepos.net](https://openrepos.net) or the Storeman app.
- **By hand**: RPMs are on the [GitHub Releases](https://github.com/ian-te/chargescope/releases)
  page, or [build](#build) your own. Copy the RPM to the phone and install it as root; `devel-su`
  asks for the Developer Mode password. With the phone at `PHONE`:

  ```bash
  scp harbour-chargescope-*.aarch64.rpm defaultuser@PHONE:
  ```

  ```bash
  ssh -t defaultuser@PHONE 'devel-su rpm -Uvh harbour-chargescope-*.aarch64.rpm'
  ```

## Other phones

ChargeScope is built and tested on the Jolla Phone (MediaTek). The battery, USB-C and
All readings parts work on any phone, but which supply is the charger, the charge pump and
VBUS, and where the charger's offers are logged, differ between kernels. To help support
yours, open an issue with the **Device report** template: plug in a USB PD charger, open
ChargeScope, pull down → **All readings** → pull down → **Copy all**, and paste it in.

Each report becomes a test: `tests/devices/<device>/` holds a copy of that phone's sysfs files
and the readings ChargeScope should make from them.

## Build

With the Sailfish SDK (`sfdk`), in a shadow build:

```bash
mkdir -p build-aarch64 && cd build-aarch64 && sfdk -c target=SailfishOS-5.1.0.11-aarch64 build ../
```

The RPM lands in `build-aarch64/RPMS/`.

The tests build against desktop Qt 5 and need no SDK:

```bash
cd tests && qmake && make && ./tst_chargescope
```

CI (`.github/workflows/sailfish.yml`) runs the tests and builds aarch64 in
`coderus/sailfishos-platform-sdk` on every push and pull request, and keeps the RPM as a workflow
artifact for three days.

## Releasing

Versions come from git tags only: a tag `vX.Y.Z` is release X.Y.Z. CI and OBS's `tar_git` set the
spec's `Version:` from it.

1. Make sure CI is green on `main` and the build has been tried on a phone.
2. Set `Version:` in the spec to X.Y.Z, commit and push.
3. Tag and push the tag:

   ```bash
   git tag -a vX.Y.Z -m "vX.Y.Z" && git push origin vX.Y.Z
   ```

   CI builds the RPM and publishes the GitHub Release `vX.Y.Z`.
4. **Chum**: set `revision` to `vX.Y.Z` in `obs/_service`, commit, and put the same file on the
   `harbour-chargescope` package on [build.sailfishos.org](https://build.sailfishos.org) (in
   `sailfishos:chum:testing` once the package is there). When it builds, submit it to
   `sailfishos:chum`. The one-time OBS setup is the same as
   [Sailtrack's](https://github.com/ian-te/sailtrack#one-time-setup-on-obs), with this repo's names.
5. **OpenRepos**: on openrepos.net, open the ChargeScope application → **Upload** the release's
   RPM (from the GitHub Release), and add the changes to the description.

## License

MIT
