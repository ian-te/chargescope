# Jolla Phone (2026)

MediaTek Dimensity with an MT6720 charger (`primary_chg`) and divide-by-2 charge pump
(`primary_dvchg`), Sailfish OS 5.

- `unplugged/`: copied from the phone, nothing plugged in.
- `pps-simulated/`: `unplugged/` edited by hand to look like a PPS direct charge (VBUS 8.42 V,
  charge pump online at 2.05 A, `usb_type` `PD_PPS`, a PD 3.0 partner). Replace it with a real
  copy taken while direct charging.
