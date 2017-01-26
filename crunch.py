#!/usr/bin/env python3.6

import sys
from elftools.elf.elffile import ELFFile

ENCODING = sys.getdefaultencoding()

def bail(msg):
    print(msg)
    sys.exit(1)


class ELFBinary:
    def __init__(self, path):
        self.path = path

        with open(path ,"rb") as fh:
            self.elf = ELFFile(fh)

            if not self.elf.has_dwarf_info():
                bail("no dwarf info in binary: %s" % binary_path)
            self.dwarf = self.elf.get_dwarf_info()

    def get_func_addr(self, func_name):
        # XXX when we look up >1 func, load all funcs into a dict ahead of time
        # to avoid quadratic lookup complexity.

        func_name = bytes(func_name, ENCODING)

        for cu in self.dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                if die.tag != 'DW_TAG_subprogram':
                    continue # not a function

                try:
                    die_name = die.attributes['DW_AT_name'].value
                except KeyError:
                    continue  # couldn't get name of that function

                if die_name != func_name:
                    continue  # another function

                try:
                    return die.attributes['DW_AT_low_pc'].value
                except KeyError:
                    print("arg")
                    pass  # found a function of right name, but no address

        bail("failed to find symbol address for: %s" % func_name)

    def get_source_location(self, address):
        """this logic comes from the dwarf_decode_address.py example"""

        for cu in self.dwarf.iter_CUs():
            line_prog = self.dwarf.line_program_for_CU(cu)
            prev_state = None

            for entry in line_prog.get_entries():
                if entry.state is None or entry.state.end_sequence:
                    continue

                if prev_state and prev_state.address <= address < entry.state.address:
                    source_file = line_prog['file_entry'][prev_state.file - 1].name
                    source_line = prev_state.line
                    return source_file.decode(ENCODING), source_line
                prev_state = entry.state
        bail("couldn't get source location for: %s" % hex(address))


def main(bin_path, func_name, offset):
    eb = ELFBinary(bin_path)
    func_addr = eb.get_func_addr(func_name)
    source_file, source_line = eb.get_source_location(func_addr)
    print("%s:%s" % (source_file, source_line))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        bail("bad usage")

    _, bin_path, func_name, offset = sys.argv
    main(bin_path, func_name, offset)
