# Jolla Phone (2026)

MediaTek Dimensity with an MT6720 charger (`primary_chg`) and divide-by-2 charge pump
(`primary_dvchg`), Sailfish OS 5.

- `unplugged/`: copied from the phone, nothing plugged in.
- `pd-9v-charging/`: copied from the phone on a 45 W USB PD charger without PPS (5 V 2.4 A,
  9 V 3 A, 15 V 3 A, 20 V 2.25 A), charging on the 9 V offer through `primary_chg`; the charge
  pump is idle but its meter reads VBUS and the input current. `dmesg.txt` holds the kernel
  log's PD lines from the same moment.
- `pps-simulated/`: `unplugged/` edited by hand to look like a PPS direct charge (VBUS 8.42 V,
  charge pump online at 2.05 A, `usb_type` `PD_PPS`, a PD 3.0 partner). Replace it with a real
  copy taken while direct charging.
