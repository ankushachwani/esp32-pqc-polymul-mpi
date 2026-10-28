"""Send commands to the firmware and print the replies:  python3 host/cmd.py "INFO" "HWLAT" """
import sys
from board import Board
b = Board()
for c in sys.argv[1:]:
    print('>', c)
    for ln in b.cmd(c, timeout=600): print(ln)
