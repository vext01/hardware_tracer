#!/usr/bin/env python3
"""
Runs ptxed, mapping the contents of a map file at the right location
run_ptxed <map-file> <trace-file>
"""


import sys
import os
from subprocess import Popen


class ExecSec:
    def __init__(self, start, path):
        self.start = start
        self.path = path

    def __str__(self):
        return "0x%x: %s" % (self.start, self.path)

    def to_ptxed_args(self):
        return ["--raw", "%s:0x%x" % (self.path, self.start)]


def main(trace_file, map_file):
    # Identify executable sections from the map
    exec_secs = []
    with open(map_file) as map_fh:
        for line in map_fh:
            elems = line.strip().split()
            rng, flags = elems[0], elems[1]
            if 'x' not in flags:
                continue
            path = elems[5]  # optional, but always present for 'x' sections
            if path.startswith('['):
                continue  # special, like '[vdso]'
            start, _= rng.split("-")
            start= int("0x" + start, 16)
            exec_secs.append(ExecSec(start, path))

    ptxed =  os.path.join(".", "deps", "inst", "bin", "ptxed")
    args = [ptxed, "-v", "--cpu", "auto", "--pt", trace_file]
    for sec in exec_secs:
        args += sec.to_ptxed_args()
    print("Running ptxed: %s" % args)
    p = Popen(args)
    p.communicate()


if __name__ == "__main__":
    try:
        trace_file = sys.argv[1]
        map_file = sys.argv[2]
    except IndexError:
        print(__doc__)
        sys.exit(1)
    main(trace_file, map_file)
