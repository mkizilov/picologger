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
3. After trial ten, allow recovery to finish. For a final inspection pass,
   power down, disconnect the BME280 while unpowered, then boot once more with
   the card still inserted. Wait for `SD online=1` and at least one status
   report. With no sensor samples, the new file should contain only its synced
   header. Power down and move the card to a computer. These extra power cycles
   are preparation/inspection, not part of the ten trials; there is no clean
   eject command, so even this procedure cannot eliminate all write risk.
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
