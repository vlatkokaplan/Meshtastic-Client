# Meshtastic protobufs

Verbatim copy of https://github.com/meshtastic/protobufs (`meshtastic/` directory),
commit `51028ca5a6945c76d3977c2bb803f9947d319ac5`. GPL-3.0, see `LICENSE`.

Do not edit these files. Field numbers and wire types must match the firmware
exactly; a hand-edited copy previously sent `set_channel` as `set_owner`, reboot
as `reboot_ota_seconds`, and a malformed heartbeat. To update, run
`scripts/update-protos.sh [ref]`.
