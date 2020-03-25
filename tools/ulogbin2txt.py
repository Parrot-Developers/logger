#!/usr/bin/env python3

import sys, logging
import argparse
import struct
import re
from datetime import datetime
from io import BytesIO, SEEK_SET

LEVEL_CHARS = [
    ' ', ' ', 'C', 'E', 'W', 'N', 'I', 'D'
]
LEVEL_COLORS = [
    "",
    "",
    "\x1b[4;1;31m",
    "\x1b[1;31m",
    "\x1b[1;33m",
    "\x1b[35m",
    "",
    "\x1b[1;30m"
]

################################################################################
# Format: <pname:N>\0<tname:N>\0<priority:4><tag:N>\0<message:N>
################################################################################
def parse_payload(payload, has_tname):
    # Get the next null-terminated string. If no null byte remaning,
    # it returns None
    def read_string():
        string = b""
        while True:
            char = payload.read(1)
            if not char:
                return None
            elif char == b'\0':
                break
            else:
                string += char
        return string.decode("UTF-8", errors="replace")

    def unformatted():
        payload.seek(pos, SEEK_SET)
        msg = payload.read()
        return (pname, tname, 0, "", msg)

    # Read pname and tname
    pname = read_string()
    if has_tname:
        tname = read_string()
    else:
        tname = None

    # Remember where we are in the payload and try to get priority
    pos = payload.tell()
    priority = payload.read(4)
    if not priority or len(priority) < 4:
        # Not enough bytes to be a normal, entry must be unformatted
        return unformatted()
    else:
        priority = struct.unpack("<I", priority)[0]
        tag = read_string()
        if tag is None:
            # No tag found, entry must be unformatted
            return unformatted()
        else:
            msg = payload.read()

    return (pname, tname, priority, tag, msg)

################################################################################
################################################################################
class Entry(object):
    _RE_KMSGD = re.compile(r"<([0-9]+)>\[ *([0-9]+)\.([0-9]+)\] (.*)")
    def __init__(self, fin):
        payload_len = struct.unpack("<H", fin.read(2))[0]
        hdr_len = struct.unpack("<H", fin.read(2))[0]
        if hdr_len != 24:
            raise IOError("Invalid header size: %d(%s)" % (hdr_len, 24))
        (self.pid, self.tid, tv_sec, tv_nsec, self.euid) = \
                struct.unpack("<IIIII", fin.read(20))
        payload = BytesIO(fin.read(payload_len))
        self.ts = tv_sec * 1000000 + tv_nsec // 1000

        # Parse payload
        has_tname = self.pid != self.tid
        (self.pname, self.tname, priority, self.tag, msg) = \
                parse_payload(payload, has_tname)

        # Extract more information
        self.level = priority & 0x7
        self.binary = (priority & 0x80) != 0
        self.color = priority >> 8
        if not self.binary:
            while len(msg) > 0 and (msg[-1] == 0 or msg[-1] == ord(b'\n')):
                msg = msg[:-1]
            self.msg = msg.decode("UTF-8", errors="replace")
        else:
            self.msg = msg

        self.domain = "U"
        if self.pname =="kmsgd" and not self.binary:
            self._fix_klog_entry()

    def _fix_klog_entry(self):
        match = Entry._RE_KMSGD.match(self.msg)
        if not match:
            return
        self.domain = "K"
        self.level = int(match.group(1)) & 0x7
        self.ts = int(match.group(2)) * 1000000 + int(match.group(3))
        self.msg = match.group(4)
        self.pid = 0
        self.tid = 0
        self.pname = None
        self.tname = None
        self.tag = "KERNEL"
        self.is_binary = False
        self.color = 0

    def dump(self, fout, with_color):
        msec = (self.ts % 1000000) // 1000
        time = datetime.utcfromtimestamp(self.ts // 1000000)
        level_char = LEVEL_CHARS[self.level]

        if with_color:
            cstart = LEVEL_COLORS[self.level]
            cend = "\x1b[0m"
        else:
            cstart = ""
            cend = ""

        if self.tname:
            info = "%-12s(%s-%d/%s-%d)" % (self.tag, self.pname,
                    self.pid, self.tname, self.tid)
        elif self.pname:
            info = "%-12s(%s-%d)" % (self.tag, self.pname, self.pid)
        else:
            info = "%-12s" % self.tag
        header = "%c %s.%03d %c %-45s" % (self.domain,
                time.strftime("%m-%d %H:%M:%S"), msec,
                level_char, info)

        if self.binary:
            fout.write("%s%s: BINARY%s\n" % (cstart, header, cend))
        else:
            for line in self.msg.split("\n"):
                fout.write("%s%s: %s%s\n" % (cstart, header, line, cend))

################################################################################
################################################################################
def main():
    # Setup logging
    logging.basicConfig(
            level=logging.INFO,
            format="[%(levelname)s] %(message)s",
            stream=sys.stderr)
    logging.addLevelName(logging.CRITICAL, "C")
    logging.addLevelName(logging.ERROR, "E")
    logging.addLevelName(logging.WARNING, "W")
    logging.addLevelName(logging.INFO, "I")
    logging.addLevelName(logging.DEBUG, "D")

    # Setup argument parser
    parser = argparse.ArgumentParser()
    parser.add_argument("infile",
            nargs=1,
            help="Ulog binary file")
    parser.add_argument("-o", "--output",
            dest="outfile",
            default=None,
            help="Output file")

    # Parse arguments
    options = parser.parse_args()

    # Open input file
    try:
        fin = open(options.infile[0], "rb")
    except IOError as ex:
        logging.error("Failed to open '%s': err=%d(%s)",
                ex.filename, ex.errno, ex.strerror)
        sys.exit(1)

    # Open ouput file
    if options.outfile:
        try:
            fout = open(options.outfile, "w")
        except IOError as ex:
            logging.error("Failed to create '%s': err=%d(%s)",
                    ex.filename, ex.errno, ex.strerror)
            sys.exit(1)
    else:
        fout = sys.stdout

    while True:
        try:
            entry = Entry(fin)
        except struct.error:
            # TODO: Log error if not at EOF
            break
        entry.dump(fout, fout.isatty())

################################################################################
################################################################################
if __name__ == "__main__":
    main()
