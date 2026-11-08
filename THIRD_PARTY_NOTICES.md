# Third-party software and publication review

PicoLogger's project-owned code is offered under the root MIT `LICENSE`.
Third-party code, tools, firmware blobs and their notices are **not relicensed**
by that file. The copied texts in `LICENSES/` preserve notices from the pinned
upstream sources; the original files and any additional file-level notices
remain authoritative. This inventory is an engineering review, not a legal
opinion or a guarantee that all redistribution obligations are satisfied.

## Release review required: SDIO provenance

The pinned no-OS-FatFS-SD-SDIO-SPI-RPi-Pico repository at commit
`6c644cfc3ab03c161fee2dd7be4877e5b832fa71` has a top-level Apache-2.0
license, but its README says its SDIO driver is derived from ZuluSCSI firmware;
the included `src/ZuluSCSI-firmware/LICENSE` identifies GPL-3.0-or-later terms.
The upstream SDIO files and their provenance must be reviewed independently:
some files also describe an independent implementation and do not all carry
the same explicit file header. Although PicoLogger configures an SPI card, the
current Pico and Pico W linked ELFs contain `sd_sdio_*` and `rp2040_sdio_*`
symbols. Their presence alone does not resolve the applicable license or legal
effect. **Do not assume the complete source/dependency bundle or generated UF2s
are MIT-only; obtain an appropriate provenance/license review before public
publication or binary distribution.**

The upstream Apache text, ZuluSCSI notice and referenced GPL-3.0 text are
preserved in `LICENSES/`. The exact fetched source paths are
`build/pico/_deps/picologger_fatfs-src/src/sd_driver/SDIO/` and
`build/pico/_deps/picologger_fatfs-src/src/ZuluSCSI-firmware/LICENSE` after
setup/build; the [pinned upstream source](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/tree/6c644cfc3ab03c161fee2dd7be4877e5b832fa71)
contains the same notices. `sd_card_spi.c` also attributes its Mbed-derived
SPI implementation to Arm Limited under Apache-2.0. FatFs R0.15 source files
carry their own 2022 ChaN notice; both that source notice and the upstream
FatFs license text are included.

## Other pinned components

| Component | Use and notice |
| --- | --- |
| Pico SDK 2.1.1, commit `bddd20f928ce76142793bef434d4f75f4af6e433` | SDK code is generally BSD-3-Clause; retain `LICENSES/pico-sdk-BSD-3-Clause.txt` and inspect individual files, including CMSIS files bearing Arm/Raspberry Pi Apache-2.0 and BSD notices. |
| TinyUSB, commit `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` | USB stack; upstream MIT notice is `LICENSES/tinyusb-MIT.txt`. Individual ports may have additional notices. |
| CYW43 driver, commit `c1075d4bc440422cf2b2fd12c64a1f53f77660ee` | Pico W build includes this driver. Its default license limits use to personal/non-commercial purposes; the alternative `LICENSE.RP` permits use only with Raspberry Pi semiconductor devices. Both texts are preserved; review the applicable terms and firmware-blob rights before distribution. |
| BTstack, commit `501e6d2b86e6c92bfb9c390bcf55709938e25ac1` | Fetched as an SDK submodule but no BTstack C sources appeared in the current Pico/Pico W compilation databases. Its default license has a non-commercial restriction; see `LICENSES/btstack-default.txt` and upstream terms. |
| Unity 2.6.1 | Host tests; upstream MIT notice is `LICENSES/unity-MIT.txt`. |
| SdFat and Mbed OS notices | Included in the pinned SD library's source tree; see `LICENSES/sdfat-MIT.txt` and `LICENSES/mbed-os-notice.txt` and source-file notices. |
| Arm GNU Toolchain 13.3.Rel1 and picotool 2.1.1 | Project-local build tools, not project-owned source. Picotool uses Raspberry Pi's BSD-3-Clause notice; review Arm/GCC/newlib and any linked runtime terms for a specific binary distribution. |
| Python tools in `requirements.txt` | Project-local pyserial, matplotlib, pytest, clang-tidy and their pinned dependencies retain their respective distribution licenses; inspect the installed package metadata and license files when redistributing them. |

Do not remove upstream copyright or license headers. Any source or binary
release should include the notices required by the actual included components
and resolve the SDIO provenance question first.
