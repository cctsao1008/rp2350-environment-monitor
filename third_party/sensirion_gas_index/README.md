# Sensirion Gas Index Algorithm (vendored)

Source: https://github.com/Sensirion/gas-index-algorithm

Pinned upstream commit: `2ef9f13d225e8a0dedd3ff42dd229af4dbb1aae4`.

The upstream C implementation and header are copied **without modifications**,
under the BSD-3-Clause license reproduced in `LICENSE`.

The SGP40 is measured once per second with BME280 temperature and relative
humidity compensation. The VOC Index is 0 during the algorithm's initial ~45
seconds blackout period (do not display 0 as a measured zero VOC concentration).

Keep this library offline and pinned; no automatic download at CMake build.
