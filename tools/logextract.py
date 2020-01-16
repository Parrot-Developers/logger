#!/usr/bin/env python3

import sys, os, logging
import argparse
import struct
import re
import datetime
import binascii
import requests
from io import BytesIO

import hashlib
import lz4framed
from Crypto.Cipher import AES

from ulogbin2txt import Entry as UlogEntry
from sysmon import Sysmon

LOG = logging.getLogger("logextract")

LOGGERD_FILE_MAGIC = 0x21474f4c
LOGGERD_FILE_VERSION = 3

LOGGERD_ID_SOURCE_DESC = 0
LOGGERD_ID_LZ4 = 1
LOGGERD_ID_AES_DESC = 2	# v3
LOGGERD_ID_AES = 3	# v3
LOGGERD_ID_BASE = 256

RSAREMOTE_URL = "https://noserver.parrot.biz"

################################################################################
################################################################################
def escape_csv_field(field):
    if "\"" in field or "," in field:
        return "\"" + field.replace("\"", "\"\"") + "\""
    else:
        return field

################################################################################
################################################################################
class LogError(Exception):
    pass

class CryptoError(LogError):
    pass

class CorruptError(LogError):
    pass

class IntegrityError(LogError):
    pass
################################################################################
################################################################################
class LogDataReader(object):
    def __init__(self, src, progress=None):
        if isinstance(src, str) or isinstance(src, bytes):
            self._src = BytesIO(src)
        else:
            self._src = src
        # Determine size
        self._src.seek(0, os.SEEK_END)
        self._size = self._src.tell()
        self._src.seek(0, os.SEEK_SET)
        self._progress = progress

    def read(self, count):
        return self.read_data(count)

    def read_data(self, count):
        data = self._src.read(count)
        if len(data) != count:
            raise CorruptError("Unable to read %d bytes" % count)
        if self._progress:
            self._progress(self._src.tell(), self._size)
        return data

    def read_u8(self):
        return struct.unpack("<B", self.read_data(1))[0]

    def read_u16(self):
        return struct.unpack("<H", self.read_data(2))[0]

    def read_i32(self):
        return struct.unpack("<i", self.read_data(4))[0]

    def read_u32(self):
        return struct.unpack("<I", self.read_data(4))[0]

    def read_double(self):
        return struct.unpack("<d", self.read_data(8))[0]

    def read_string(self):
        slen = self.read_u16()
        if slen == 0:
            raise LogError("Invalid string length: %d" % slen)
        sdata = self.read_data(slen).decode("UTF-8", errors="replace")
        if len(sdata) == 0 or sdata[-1] != "\0":
            logging.warning("String is not null-terminated: '%s'", sdata)
            return sdata
        return sdata[:-1]

    def rewind(self, count):
        self._src.seek(-count, os.SEEK_CUR)

    def remaining(self):
        return self._size - self._src.tell()

################################################################################
################################################################################
class LogSourceDesc(object):
    def __init__(self, data):
        self.source_id = data.read_u32()
        self.version = data.read_u32()
        self.plugin = data.read_string()
        self.name = data.read_string()

    def get_full_name(self):
        if self.plugin == self.name:
            return self.plugin
        else:
            return "%s-%s" % (self.plugin, self.name)

################################################################################
################################################################################
class LogSourceFactory(object):
    def __init__(self):
        classes = [
            LogSourceFile,
            LogSourceInternal,
            LogSourceProperties,
            LogSourceSettings,
            LogSourceSysmon,
            LogSourceTelemetry,
            LogSourceUlog,
        ]
        self.classes = {}
        for cls in classes:
            self.classes[cls.PLUGIN] = cls

    def create(self, parent, options, desc, num):
        cls = self.classes.get(desc.plugin, LogSource)
        filepath = os.path.join(options.outdir, desc.get_full_name())
        if num > 0:
            filepath += "-%d" % num
        filepath += cls.EXTENSION
        return cls(parent, options, desc, filepath)

    def finish(self, parent, options):
        for cls in self.classes.values():
            cls.finish(parent, options)

################################################################################
################################################################################
class LogSource(object):
    EXTENSION = ".bin"

    def __init__(self, parent, options, desc, filepath):
        self.parent = parent
        self.desc = desc
        self.filepath = filepath
        if filepath:
            self.fout = open(filepath, "wb")
        else:
            self.fout = None

    def finish(self):
        if self.fout:
            self.fout.close()
            self.fout = None

    def add_entry(self, data):
        self.fout.write(data.read_data(data.remaining()))

################################################################################
################################################################################
class LogSourceFile(LogSource):
    PLUGIN = "file"
    EXTENSION = ".bin"
    _TAG_HEADER = 0
    _TAG_CHUNK = 1
    _TAG_STATUS = 2
    _STATUS_OK = 0
    _STATUS_CORRUPTED = 1

    def __init__(self, parent, options, desc, filepath):
        LogSource.__init__(self, parent, options, desc, None)
        self.dirpath = os.path.join(os.path.dirname(filepath), "fs")
        self.current_file = None
        self.current_file_id = 0
        self.current_size = 0

    def add_entry(self, data):
        while data.remaining() > 0:
            tag = data.read_u8()
            if tag == LogSourceFile._TAG_HEADER:
                # Get fields
                self.current_file_id = data.read_u32()
                self.current_size = data.read_u32()
                filepath = data.read_string()

                # Create file
                while filepath.startswith("/"):
                    filepath = filepath[1:]
                filepath = os.path.join(self.dirpath, filepath)
                os.makedirs(os.path.dirname(filepath), exist_ok=True)
                try:
                    # Avoid overwriting previous file
                    num = 0
                    filepath2 = filepath
                    while os.path.exists(filepath2):
                        num += 1
                        filepath2 = "%s.%d" % (filepath, num)
                    self.current_file = open(filepath2, "wb")
                except IOError as ex:
                    self.current_file = None
                    LOG.warning("Failed to create '%s': %s",
                            filepath2, str(ex))
            elif tag == LogSourceFile._TAG_CHUNK:
                # Get fields
                file_id = data.read_u32()
                chunk_size = data.read_u32()
                chunk = data.read_data(chunk_size)

                # Write chunk
                if file_id != self.current_file_id:
                    LOG.warning("File id mismatch: %d(%d)",
                            file_id, self.current_file_id)
                elif self.current_file is not None:
                    self.current_file.write(chunk)
            elif tag == LogSourceFile._TAG_STATUS:
                # Get fields
                file_id = data.read_u32()
                status = data.read_u8()

                # Finish file
                if file_id != self.current_file_id:
                    LOG.warning("File id mismatch: %d(%d)",
                            file_id, self.current_file_id)
                elif self.current_file is not None:
                    if status != LogSourceFile._STATUS_OK:
                        LOG.warning("File '%s' is corrupted",
                                self.current_file.name)
                    self.current_file.close()
                self.current_file = None
                self.current_file_id = 0
            else:
                raise LogError("Unknown file tag: %d", tag)

################################################################################
################################################################################
class LogSourceInternal(LogSource):
    PLUGIN = "internal"
    EXTENSION = ".txt"

    def __init__(self, parent, options, desc, filepath):
        if options.print_header:
            filepath = None
        LogSource.__init__(self, parent, options, desc, filepath)
        self.print_header = options.print_header

    def add_entry(self, data):
        while data.remaining() > 0:
            key = data.read_string()
            value = data.read_string()
            if self.fout:
                line = "[%s]: [%s]\n" % (key, value)
                self.fout.write(line.encode("UTF-8"))
            # Store header in parent log file
            if self.desc.name == "header":
                self.parent.internal_header[key] = value

################################################################################
################################################################################
class LogSourceProperties(LogSource):
    PLUGIN = "properties"
    EXTENSION = ".csv"

    def __init__(self, parent, options, desc, filepath):
        LogSource.__init__(self, parent, options, desc, filepath)
        self.fout.write(b"ts, key, value\n")

    def add_entry(self, data):
        while data.remaining() > 0:
            tv_sec = data.read_u32()
            tv_nsec = data.read_u32()
            key = escape_csv_field(data.read_string())
            value = escape_csv_field(data.read_string())
            line  = "%d.%09d, %s, %s\n" % (tv_sec, tv_nsec, key, value)
            self.fout.write(line.encode("UTF-8"))

################################################################################
################################################################################
class LogSourceSettings(LogSource):
    PLUGIN = "settings"
    EXTENSION = ".csv"
    _SHS_TYPE_BOOLEAN = 0
    _SHS_TYPE_INT = 1
    _SHS_TYPE_DOUBLE = 2
    _SHS_TYPE_STRING = 3

    def __init__(self, parent, options, desc, filepath):
        LogSource.__init__(self, parent, options, desc, filepath)
        self.fout.write(b"ts, name, type, value\n")

    def add_entry(self, data):
        while data.remaining() > 0:
            tv_sec = data.read_u32()
            tv_nsec = data.read_u32()
            name = data.read_string()
            type_num = data.read_u8()
            if type_num == LogSourceSettings._SHS_TYPE_BOOLEAN:
                type_str = "BOOL"
                value = "true" if data.read_u8() else "false"
            elif type_num == LogSourceSettings._SHS_TYPE_INT:
                type_str = "INT"
                value = str(data.read_i32())
            elif type_num == LogSourceSettings._SHS_TYPE_DOUBLE:
                type_str = "DOUBLE"
                value = str(data.read_double())
            elif type_num == LogSourceSettings._SHS_TYPE_STRING:
                type_str = "STRING"
                value = escape_csv_field(data.read_string())
            else:
                raise LogError("Unknown setting type: %d" % type_num)
            line = "%d.%09d, %s, %s, %s\n" % (
                    tv_sec, tv_nsec, name, type_str, value)
            self.fout.write(line.encode("UTF-8"))

################################################################################
################################################################################
class LogSourceSysmon(LogSource):
    PLUGIN = "sysmon"
    EXTENSION = ".json"

    _TAG_SYSTEM_CONFIG = 0
    _TAG_SYSTEM_STAT = 1
    _TAG_SYSTEM_MEM = 2
    _TAG_SYSTEM_DISK = 3
    _TAG_SYSTEM_NET = 4
    _TAG_PROCESS_STAT = 5
    _TAG_THREAD_STAT = 6
    _TAG_RESERVED = 7

    def __init__(self, parent, options, desc, filepath):
        LogSource.__init__(self, parent, options, desc, filepath)
        self.sysmon = Sysmon()

    def finish(self):
        self.sysmon.write(self.fout)
        LogSource.finish(self)

    def add_entry(self, data):
        while self._read_tag(data):
            pass

    def _read_tag(self, data):
        if data.remaining() == 0:
            return False

        def read_ts():
            tv_sec = data.read_u32()
            tv_nsec = data.read_u32()
            return tv_sec * 1000000 + tv_nsec / 1000

        def read_data():
            ts_acq_begin = read_ts()
            ts_acq_end = read_ts()
            return (ts_acq_begin, data.read_string())

        tag = data.read_u8()
        if tag == LogSourceSysmon._TAG_SYSTEM_CONFIG:
            clk_tck = data.read_u32()
            page_size = data.read_u32()
            self.sysmon.set_system_config(clk_tck, page_size)
        elif tag == LogSourceSysmon._TAG_SYSTEM_STAT:
            self.sysmon.add_system_stat(*read_data())
        elif tag == LogSourceSysmon._TAG_SYSTEM_MEM:
            self.sysmon.add_system_mem(*read_data())
        elif tag == LogSourceSysmon._TAG_SYSTEM_DISK:
            self.sysmon.add_system_disk(*read_data())
        elif tag == LogSourceSysmon._TAG_SYSTEM_NET:
            self.sysmon.add_system_net(*read_data())
        elif tag == LogSourceSysmon._TAG_PROCESS_STAT:
            pid = data.read_u32()
            self.sysmon.add_process_stat(pid, *read_data())
        elif tag == LogSourceSysmon._TAG_THREAD_STAT:
            pid = data.read_u32()
            tid = data.read_u32()
            self.sysmon.add_thread_stat(pid, tid, *read_data())
        elif tag == LogSourceSysmon._TAG_RESERVED:
            read_data()
        else:
            raise LogError("Unknown sysmon tag: %d" % tag)

        return True

################################################################################
################################################################################
class LogSourceTelemetryCtx(object):
    def __init__(self, options):
        self._block_start = 0
        if options.decrypt is not None or options.print_header:
            self._fout = None
        else:
            # Write TLMB file header
            filepath = os.path.join(options.outdir, "telemetry.tlmb")
            self._fout = open(filepath, "wb")
            self._write(struct.pack("<IBI",
                    LogSourceTelemetry._TLMB_MAGIC,
                    LogSourceTelemetry._TLMB_VERSION,
                    0))
            self._begin_block()

    def finish(self, options):
        self._finish_block()
        self._fout.write(struct.pack("<I", 0xffffffff))

    def _tell(self):
        return self._fout.tell()

    def _seek(self, off, whence):
        return self._fout.seek(off, whence)

    def _write(self, buf):
        self._fout.write(buf)

    def _begin_block(self):
        self._block_start = self._tell()
        self._fout.write(struct.pack("<I", 0xffffffff))

    def _finish_block(self):
        # Write size of block at the start
        block_size = self._get_block_size()
        self._seek(self._block_start, os.SEEK_SET)
        self._write(struct.pack("<I", block_size))
        self._seek(block_size, os.SEEK_CUR)

    def _get_block_size(self):
        return self._tell() - self._block_start - 4

################################################################################
################################################################################
class LogSourceTelemetry(LogSource):
    PLUGIN = "telemetry"
    EXTENSION = ".tlmb"
    _TAG_HEADER = 0
    _TAG_SAMPLE = 1
    _TLM_SHM_MAGIC = 0x214d4c54 # Metadata magic TLM!
    _TLM_SHM_MAGIC_DBG = 0x444d4c54
    _TLMB_MAGIC = 0x424d4c54    # File magic 'TLMB'
    _TLMB_VERSION = 1           # File format version
    _TLMB_TAG_SECTION_ADDED = 0
    _TLMB_TAG_SECTION_REMOVED = 1
    _TLMB_TAG_SECTION_CHANGED = 2
    _TLMB_TAG_SAMPLE = 3

    def __init__(self, parent, options, desc, filepath):
        LogSource.__init__(self, parent, options, desc, None)
        self.sample_count = 0
        self.sample_size = 0
        self.sample_rate = 0
        self.metadata_size = 0
        self.metadata = None
        self.is_tlmb = False
        self.dump = len(options.tlm_sections) == 0 or \
                self.desc.name in options.tlm_sections

        # Get global context from parent
        self._ctx = self.parent.telemetry_ctx

        # Write 'SECTION_ADDED' tag
        self._ctx._write(struct.pack("<BIB",
                LogSourceTelemetry._TLMB_TAG_SECTION_ADDED,
                self.desc.source_id,
                len(self.desc.name) + 1))
        self._ctx._write(self.desc.name.encode("UTF-8"))
        self._ctx._write(b"\0")


    def add_entry(self, data):
        if self.dump:
            while self._read_tag(data):
                pass

    def _read_tag(self, data):
        if data.remaining() == 0:
            return False

        tag = data.read_u8()
        if tag == LogSourceTelemetry._TAG_HEADER:
            # Read header and metadata
            self.sample_count = data.read_u32()
            self.sample_size = data.read_u32()
            self.sample_rate = data.read_u32()
            self.metadata_size = data.read_u32()
            self.metadata = None
            self.is_tlmb = False
            if self.metadata_size != 0:
                self.metadata = data.read_data(self.metadata_size)
                magic = struct.unpack("<I", self.metadata[0:4])[0]
                self.is_tlmb = self.metadata_size >= 4 and (\
                        magic == LogSourceTelemetry._TLM_SHM_MAGIC or \
                        magic == LogSourceTelemetry._TLM_SHM_MAGIC_DBG)
            if self.is_tlmb:
                # Write 'SECTION_CHANGED' tag
                self._ctx._write(struct.pack("<BII",
                        LogSourceTelemetry._TLMB_TAG_SECTION_CHANGED,
                        self.desc.source_id,
                        self.metadata_size - 4))
                self._ctx._write(self.metadata[4:])
        elif tag == LogSourceTelemetry._TAG_SAMPLE:
            # Read sample info and data
            tv_sec = data.read_u32()
            tv_nsec = data.read_u32()
            data.read_u32() # seqnum
            sample_data = data.read_data(self.sample_size)
            if self.is_tlmb:
                # Write 'SAMPLE' tag
                self._ctx._write(struct.pack("<BIIII",
                        LogSourceTelemetry._TLMB_TAG_SAMPLE,
                        self.desc.source_id,
                        tv_sec,
                        tv_nsec,
                        self.sample_size))
                self._ctx._write(sample_data)
        else:
            raise LogError("Unknown telemetry tag: %d" % tag)

        if self._ctx._get_block_size() >= 1024 * 1024:
            self._ctx._finish_block()
            self._ctx._begin_block()
        return True

################################################################################
################################################################################
class LogSourceUlogCtx(object):
    _RE_EVT_TIME = re.compile(r"EVT:TIME;date='([^']*)';time='([^']*)'")
    _RE_EVT_KTIME = re.compile(r"EVT:KTIME;tv_sec=([0-9]*);tv_nsec=([0-9]*)")

    def __init__(self, options):
        self._sources = []
        self._printk_to_monotonic_offset = 0
        self._monotonic_to_absolute_offset = 0
        self._utc_offset = 0
        self._ulog_absolute = options.ulog_absolute
        self._mainbin_txt = []

    def finish(self, options):
        LOG.info("Merge ulog files")
        filepath = os.path.join(options.outdir, "ulog-merge.txt")

        # Read all entries in a single array
        entries = self._mainbin_txt
        for binfilepath in self._sources:
            with open(binfilepath, "rb") as fin:
                while True:
                    try:
                        entry = UlogEntry(fin)
                        if not entry.binary and entry.msg.startswith("EVT:"):
                            self._parse_evt(entry)
                    except struct.error:
                        # TODO: Log error if not at EOF
                        break
                    entries.append(entry)

        # Fixup kernel entries
        if self._printk_to_monotonic_offset != 0:
            for entry in entries:
                if entry.domain == "K":
                    entry.ts += self._printk_to_monotonic_offset
                    if entry.ts < 0:
                        entry.ts = 0
        # Convert to absolute time if needed
        if self._ulog_absolute and \
                self._monotonic_to_absolute_offset != 0:
            for entry in entries:
                entry.ts += self._monotonic_to_absolute_offset

        # Sort entries and write them
        entries.sort(key=lambda x: x.ts)
        with open(filepath, "w", encoding="utf-8") as fout:
            for entry in entries:
                entry.dump(fout, with_color=False)

    def _parse_evt(self, entry):
        match = LogSourceUlogCtx._RE_EVT_TIME.match(entry.msg)
        if match is not None:
            # ts: absolute, entry.ts: monotonic
            dt = datetime.datetime.strptime(match.group(1) + match.group(2),
                                            "%Y-%m-%dT%H%M%S%z")
            ts = (dt.timestamp() + dt.utcoffset().total_seconds())* 1000000
            self._monotonic_to_absolute_offset = ts - entry.ts
            return

        match = LogSourceUlogCtx._RE_EVT_KTIME.match(entry.msg)
        if match is not None:
            # t1: monotonic, entry.ts: printk
            tv_sec = int(match.group(1))
            tv_nsec = int(match.group(2))
            ts = tv_sec * 1000000 + tv_nsec / 1000
            self._printk_to_monotonic_offset = ts - entry.ts

################################################################################
################################################################################
class LogSourceUlog(LogSource):
    PLUGIN = "ulog"
    EXTENSION = ".bin"
    def __init__(self, parent, options, desc, filepath):
        # Get global context from parent
        self._ctx = parent.ulog_ctx

        # mainbin is special, entries are parsed and extracted by tags
        if desc.name == "mainbin":
            LogSource.__init__(self, parent, options, desc, None)
            self.mainbin_dir = os.path.dirname(filepath)
            self.mainbin = {}
        else:
            LogSource.__init__(self, parent, options, desc, filepath)
            self._ctx._sources.append(filepath)

    def add_entry(self, data):
        if self.desc.name == "mainbin":
            while data.remaining() > 0:
                entry = UlogEntry(data)
                if entry.binary:
                    if entry.tag not in self.mainbin:
                        # Create file the first time
                        filename = entry.tag + ".bin"
                        filepath = os.path.join(self.mainbin_dir, filename)
                        self.mainbin[entry.tag] = open(filepath, "wb")
                    self.mainbin[entry.tag].write(entry.msg)
                else:
                    self._ctx._mainbin_txt.append(entry)
        else:
            self.fout.write(data.read_data(data.remaining()))

################################################################################
################################################################################
class LogFile(object):
    _MAGIC = LOGGERD_FILE_MAGIC
    _VERSION = LOGGERD_FILE_VERSION
    _DEFAULT_MD5 = "ffffffffffffffffffffffffffffffff"

    def __init__(self, options, reader, factory, hdr_out=sys.stdout):
        self._options = options
        self.magic = 0
        self.version = 0
        self._factory = factory
        self._sources_by_id = {}
        self._sources_by_full_name = {}
        self._total = reader.remaining()
        self._aes_cipher = None
        self.md5 = hashlib.md5()
        if self._options.decrypt is not None:
            self.decrypt_only = True
            self._out = open(self._options.decrypt, "wb")
        else:
            self.decrypt_only = False
            self._out = None

        self.telemetry_ctx = LogSourceTelemetryCtx(options)
        self.ulog_ctx = LogSourceUlogCtx(options)
        self.internal_header = {}

        self._read_header(reader)
        try:
            self._read_entries(reader)
        except LogError as ex:
            self._finalize()
            raise CorruptError("%s" % ex)

        # If only printing header, do not finalize
        if self._options.print_header:
            for key in self.internal_header:
                value = self.internal_header[key]
                line = line = "[%s]: [%s]\n" % (key, value)
                hdr_out.write(line)
            return

        # Verify integrity then pursue entries reading
        if self._options.integrity:
                if 'md5' in self.internal_header:
                        md5 = self.internal_header['md5']
                        self._md5_computation(reader)

                        if md5 == self._DEFAULT_MD5 and md5 != self.md5.hexdigest():
                                LOG.info("Can't verify integrity of this file")
                        elif md5 == self.md5.hexdigest():
                                LOG.info("File integrity verified successfully")
                        else:
                                raise IntegrityError("This file seems corrupted\n")
                else:
                        LOG.info("Integrity check is not possible\n")

                #Integrity option need to be false to pursue entries reading
                self._options.integrity = False
                try:
                    self._read_entries(reader)
                except LogError as ex:
                    self._finalize()
                    raise CorruptError("%s" % ex)

        self._finalize()

    def _finalize(self):
        # Finalize unless only decrypting
        if not self.decrypt_only:
            for source in self._sources_by_id.values():
                source.finish()
            self.telemetry_ctx.finish(self._options)
            self.ulog_ctx.finish(self._options)

    def _md5_computation(self, reader):
        remaining = reader.remaining()
        data = reader.read(remaining)
        reader.rewind(remaining)
        self.md5.update(data)

    def _read_header(self, reader):
        # Read file magic and version
        self.magic = reader.read_u32()
        self.version = reader.read_u32()
        if self.magic != LogFile._MAGIC:
            raise LogError("Bad magic: 0x%08x(0x%08x)" % (
                    self.magic, LogFile._MAGIC))
        if self.version > LogFile._VERSION:
            raise LogError("Bad version: %d" % self.version)
        if self.decrypt_only:
            self._out.write(struct.pack("<II", self.magic, self.version))

    def _read_entries(self, reader):
        while reader.remaining() > 0:
            # Stop once internal header is found if only interrested in it
            if (self._options.print_header or self._options.integrity) and self.internal_header:
                break

            try:
                entry_id = reader.read_u32()
                entry_len = reader.read_u32()
                entry_data = reader.read_data(entry_len)
            except LogError as ex:
                # Raise error and stop reading
                raise LogError("Truncated entry: %s" % str(ex))

            if self.version >= 3 and entry_id == LOGGERD_ID_AES_DESC:
                self._read_aes_desc(LogDataReader(entry_data))
            elif self.version >= 3 and entry_id == LOGGERD_ID_AES:
                new_data = self._aes_decrypt(entry_data)
                if not self.decrypt_only:
                   self._read_entries(LogDataReader(new_data))
                else:
                    self._out.write(new_data)
            elif self.decrypt_only:
                self._out.write(struct.pack("<II", entry_id, entry_len))
                self._out.write(entry_data)
            elif entry_id == LOGGERD_ID_SOURCE_DESC:
                desc = LogSourceDesc(LogDataReader(entry_data))
                self._add_source(desc)
            elif entry_id == LOGGERD_ID_LZ4:
                try:
                    new_data = lz4framed.decompress(entry_data)
                except lz4framed.Lz4FramedError as ex:
                    # Print message and try continue reading
                    LOG.warning("Truncated lz4 entry: %s", str(ex))
                    continue
                self._read_entries(LogDataReader(new_data))
            else:
                source = self._get_source(entry_id)
                source.add_entry(LogDataReader(entry_data))

    def _add_source(self, desc):
        LOG.info("Source: id=%d version=%d plugin='%s' name='%s'",
              desc.source_id, desc.version, desc.plugin, desc.name)
        if desc.source_id in self._sources_by_id:
            raise LogError("Source with id=%d already added" % desc.source_id)
        full_name = desc.get_full_name()
        if full_name not in self._sources_by_full_name:
            self._sources_by_full_name[full_name] = []
        num = len(self._sources_by_full_name[full_name])
        source = self._factory.create(self, self._options, desc, num)
        self._sources_by_id[desc.source_id] = source
        self._sources_by_full_name[full_name].append(source)

    def _get_source(self, source_id):
        if source_id not in self._sources_by_id:
            raise LogError("Source with id=%d not found" % source_id)
        return self._sources_by_id[source_id]

    def _read_aes_desc(self, reader):
        # Get info about encryption
        rsa_key_hash_len = reader.read_u32()
        rsa_key_hash = reader.read_data(rsa_key_hash_len)
        aes_encrypted_key_len = reader.read_u32()
        aes_encrypted_key = reader.read_data(aes_encrypted_key_len)
        aes_iv_len = reader.read_u32()
        aes_iv = reader.read_data(aes_iv_len)

        # Decrypt aes key via remote rsa server
        try:
            response = requests.get(RSAREMOTE_URL, params={
                    "action": "decrypt",
                    "key": binascii.b2a_hex(rsa_key_hash),
                    "input": binascii.b2a_hex(aes_encrypted_key)})
            if response.status_code != 200:
                raise CryptoError("%s\n%d %s\n%s" % (
                        "Failed to decrypt aes key",
                        response.status_code, response.reason,
                        response.text))
            aes_key = binascii.a2b_hex(response.json()["message"])
        except requests.exceptions.RequestException as ex:
            raise CryptoError("%s: %s" % ("Failed to decrypt aes key", str(ex)))

        # Initialize aes cypher
        self._aes_cipher = AES.new(key=aes_key, mode=AES.MODE_CBC, IV=aes_iv)

    def _aes_decrypt(self, in_data):
        # Remove padding
        # See PKCS#7 (RFC 2315 - 10.3 Content-encryption process)
        out_data = self._aes_cipher.decrypt(in_data)
        pad_byte = out_data[-1]
        pad_len = pad_byte
        return out_data[:-pad_len]

################################################################################
################################################################################
class Progress(object):
    def __init__(self, verbose=True):
        self._last_percent = 0
        self._verbose = verbose

    def __call__(self, pos, total):
        percent = (100 * pos) // total
        if self._verbose and percent != self._last_percent:
            sys.stdout.write("\r%d%%" % percent)
            if percent == 100:
                sys.stdout.write("\n")
            self._last_percent = percent

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
    parser.add_argument("logfile",
            nargs=1,
            help="File generated by logger daemon")
    parser.add_argument("-d", "--decrypt",
            dest="decrypt",
            metavar="FILE",
            help="Simply decrypt file without extraction to given file")
    parser.add_argument("-o", "--output",
            dest="outdir",
            metavar="DIR",
            default=".",
            help="Output directory")
    parser.add_argument("-p", "--print-header",
            dest="print_header",
            action="store_true",
            help="Print file header and do not extract file")
    parser.add_argument("--ulog-absolute",
            dest="ulog_absolute",
            action="store_true",
            help="Use absolute (local) time for ulog timestamp")
    parser.add_argument("--tlm-section",
            dest="tlm_sections",
            action="append",
            default=[],
            help="Only extract named section")
    parser.add_argument("-i", "--integrity",
            dest="integrity",
            action="store_true",
            help="Verify file integrity")

    # Parse arguments
    options = parser.parse_args()

    if options.print_header:
        LOG.setLevel(logging.WARNING)

    # Open input file
    try:
        fin = open(options.logfile[0], "rb")
    except IOError as ex:
        LOG.error("Failed to open '%s': err=%d(%s)",
                ex.filename, ex.errno, ex.strerror)
        sys.exit(1)

    # Create output directory
    try:
        os.makedirs(options.outdir, exist_ok=True)
    except IOError as ex:
        LOG.error("Failed to create '%s': err=%d(%s)",
                ex.filename, ex.errno, ex.strerror)
        sys.exit(1)

    # Read input file and extract data
    try:
        LogFile(options, LogDataReader(fin, Progress()), LogSourceFactory())
    except CorruptError as ex:
        LOG.error("File corrupted: %s", ex)
        sys.exit(1)
    except LogError as ex:
        LOG.error("%s",  ex)
        sys.exit(1)
    except IOError as ex:
        LOG.error("Failed to create '%s': err=%d(%s)",
                ex.filename, ex.errno, ex.strerror)
        sys.exit(1)

    fin.close()

################################################################################
################################################################################
if __name__ == "__main__":
    main()
