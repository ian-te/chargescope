# ChargeScope

Shows what a plugged-in charger offers and how a Sailfish OS phone uses it. Made for the Jolla
Phone (MediaTek Dimensity, MT6379 charger, Sailfish OS 5.x, aarch64); most of it works on any
phone whose kernel has the standard power-supply and Type-C sysfs classes.

- **Live power into the battery**, `battery/current_now × voltage_now`, on a gauge, with the mode:
  **Direct charging (PPS)**, **USB PD**, **Basic USB** or **Not charging**.
- **Charger offers**: the source PDOs the charger advertised, with the one in use marked and the
  voltage range of any PPS (programmable) offer.
- **Phone**: VBUS, input current limit, battery current, voltage and temperature, charge level,
  the PD revision, and which `mtk*` charger chips are online.
- **The last 10 minutes** of charging power as a sparkline.
- **Copy report** in the pull-down menu puts a plain-text summary on the clipboard.

The cover shows the power and the mode. ChargeScope reads sysfs every 2 s while it is in front and
stops in the background; the cover's refresh action takes one more reading.

## Where the numbers come from

| | |
|---|---|
| Battery | `/sys/class/power_supply/battery/`: `current_now` (µA), `voltage_now` (µV), `temp` (0.1 °C), `capacity`, `status`. Drivers disagree on the sign of the current, so it is learnt from `status` while charging or discharging clearly |
| VBUS, input limit | `voltage_now` and `input_current_limit` of the first of `mtk-master-charger`, `mtk_charger_type`, `usb`, `ac`, then any other online supply, that reports one |
| Charger chips | Every `mtk*` supply in `/sys/class/power_supply` that isn't a battery, with its `online` |
| Offers | The kernel log, read with `klogctl` as `dmesg` does, at start, on plug-in (again 2, 5 and 15 s later, while PD negotiates) and on unplug. Lines `pd_get_cap_pdo cap[n], <mV>, <mA>` (or `<min mV>, <max mV>, <mA>` for a range) make one listing, and the newest listing since the last unplug is shown. If the log has none, `/sys/class/typec/port*-partner/usb_power_delivery/source-capabilities` is used where the kernel has it |
| Offer in use | The driver's `nr: idx:` line if it logs one, otherwise the offer VBUS matches: the nearest fixed voltage within 10 %, or the PPS offer while direct charging |
| PD revision | `usb_power_delivery_revision` of the Type-C partner, else of the port |
| Mode | Not charging when no supply is online. Direct charging when a divider (charge-pump) supply is online, `usb_type` is `PD_PPS` or the offer in use is PPS. USB PD when `usb_type` is `PD`, the partner speaks PD or offers were found. Basic USB otherwise |

If the kernel log can't be read (`kernel.dmesg_restrict`), the page says so and the rest still
works. The app opts out of Sailjail (`Sandboxing=Disabled`): its seccomp filter blocks `klogctl`.
This is not a Harbour app.

## Install

RPMs are on the [GitHub Releases](https://github.com/ian-te/chargescope/releases) page, or
[build](#build) your own. Copy the RPM to the phone and install it as root; `devel-su` asks for the
Developer Mode password. With the phone at `PHONE`:

```bash
scp harbour-chargescope-*.aarch64.rpm defaultuser@PHONE:
```

```bash
ssh -t defaultuser@PHONE 'devel-su rpm -Uvh harbour-chargescope-*.aarch64.rpm'
```

## Build

With the Sailfish SDK (`sfdk`), in a shadow build:

```bash
mkdir -p build-aarch64 && cd build-aarch64 && sfdk -c target=SailfishOS-5.1.0.11-aarch64 build ../
```

The RPM lands in `build-aarch64/RPMS/`.

CI (`.github/workflows/sailfish.yml`) builds aarch64 in `coderus/sailfishos-platform-sdk` on every
push and pull request and keeps the RPM as a workflow artifact for three days. A tag `vX.Y.Z`
publishes the GitHub Release X.Y.Z. Releasing works as in
[Sailtrack](https://github.com/ian-te/sailtrack#releasing): set `Version:` in the spec, commit,
then tag and push the tag.

## License

MIT
