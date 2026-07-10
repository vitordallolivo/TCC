# SPDX-License-Identifier: Apache-2.0
board_runner_args(openocd
  "--config=interface/wch-linke.cfg"
  "--config=target/ch32v307.cfg"
  "--file-type=elf"
)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
