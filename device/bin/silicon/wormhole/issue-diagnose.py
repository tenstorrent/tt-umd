# SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0

from tenstorrent.scripts.eth.utils.snps_utils import pcs_errors_report
from tenstorrent.scripts.dram import calc_gddr_margin, constants
import time

def main(chip, args):
  print("GATHERING ETH STATUS")
  for id, (x, y) in enumerate(chip.ETH_LOCATIONS):
    print(f"Reading status for ETH[{id}; x = {x}, y = {y}]")

    heartbeat_1 = chip.NOC.read(x, y, 0xffb9408c)
    time.sleep(0.1)
    heartbeat_2 = chip.NOC.read(x, y, 0xffb9408c)

    if ((heartbeat_1 >> 16) & 0xFFFF) != 0xABCD or ((heartbeat_1 >> 16) & 0xFFFF) != 0xABCD:
      print(f"WARNING: heartbeat has unexpected format, may be reading from the wrong register! [heartbeat_1 = {heartbeat_1}, heartbeat_2 = {heartbeat_2}]")

    if heartbeat_1 == heartbeat_2:
      print(f"    ETH[{id}] appears to be hung [{heartbeat_1} == {heartbeat_2}]")

    pcs_errors_report(chip, x, y)
    print()

  print("GATHERING DRAM STATUS")
  for id in range(len(constants.GDDR_MAP_X)):
    print(f"Reading status for GDDR[{id}; x = {constants.GDDR_MAP_X[id]}, y = {constants.GDDR_MAP_Y[id]}]")
    calc_gddr_margin.get_error_status(chip, id)
