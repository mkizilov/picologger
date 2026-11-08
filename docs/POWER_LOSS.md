# Power-loss recovery checklist

This is a **manual, destructive test** on an expendable FAT32 card. It has not
been performed by the agent. Back up anything important first. The recovery
algorithm is best effort: FAT metadata, SD-card internal state, sector tears,
CRC collisions, and unsynced RAM data prevent a guarantee of survival at every
possible power-loss point. ADC blocks are not logged. See `SD_LOGGING.md` for
the format, wiring, batching and durability limits.

1. With power disconnected, connect the BME280 and a 3.3 V-compatible SD module
   per `BME280.md` and `SD_LOGGING.md`. Build and flash the matching UF2 using
   `FLASHING.md`. Insert an expendable FAT32 card and connect USB power and the
   serial console. Confirm the BME280 produces readings and `SD online=1`.
2. Leave the card installed. **Ten times**, at varied points while samples are
   being logged, abruptly disconnect USB power, wait for the board to power
   down, and reconnect it. After each reconnect, wait for `SD online=1` and at
   least one ten-second status report before the next cut. Record the cycle
   number, `recovered_bytes`, `recovery_errors`, `dropped` and `uncertain`.
   Do not remove or hot-swap the card during the ten trials.
3. After trial ten, allow recovery to finish, send USB `stop` and wait for
   `@OK stopped`. Record the original `config get` values. To perform a final
   recovery pass without producing more BME records, send `config set channels 0`,
   wait for `@OK config ...` and confirm the saved value with `config get`;
   abort this inspection pass if the save fails or is uncertain. Reboot with the
   card still inserted. This disables
   BME and ADC; the storage worker still mounts and recovers the
   newest log without creating a new one. Wait for `SD online=1`, record the
   recovery counters, then power down and move the card to a computer. Do not
   simply disconnect an enabled BME sensor: the watchdog may repeatedly reset
   the board. Restore the original channel setting afterward. These extra steps
   are not part of the ten trials; no eject command exists, so write risk remains.
4. From the project directory, decode **every** `LOG_*.BIN` on the card, for
   example after substituting the card's path:

   ```sh
   mkdir -p build/recovery-check
   for f in /path/to/card/LOG_*.BIN; do
     python3 tools/decode_log.py "$f" "build/recovery-check/$(basename "$f" .BIN).csv"
   done
   ```

   A clean file exits 0. Exit 2 reports CRC failures or sequence gaps; gaps can
   reflect missed or dropped samples. Exit 1 reports a malformed header or
   truncated record. Investigate and retain the card image if any file fails;
   do not claim all data survived merely because files decode.
5. `.TMP` files are deliberately not promoted or deleted automatically. Inspect
   or preserve them separately. If recovery stays offline, a header is invalid,
   the filesystem will not mount, or the final inspection creates another torn
   newest file, record the failure rather than repeatedly deleting evidence.
