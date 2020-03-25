import copy
import json
import logging
import math
import re
import sys

# Scan value with unit
re_scan_value = re.compile(r"(\d+) (kB|MB|GB)?")
def scan_value(s):
    match = re_scan_value.match(s)
    if not match:
        return 0
    val = int(match.group(1))
    factor = {
        "kB": 1024,
        "MB": 1024 * 1024,
        "GB": 1024 * 1024 * 1024,
    }.get(match.group(2), 1)
    return val * factor


class ParseError(Exception):
    pass


class Cpu(object):
    class RawData(object):
        def __init__(self, timestamp, s):
            self.timestamp = timestamp
            fields = s.split(' ')
            if len(fields) < 7:
                raise ParseError("Invalid cpu data: '%s'" % s)
            self.user = int(fields[0])
            self.nice = int(fields[1])
            self.system = int(fields[2])
            self.idle = int(fields[3])
            self.iowait = int(fields[4])
            self.irq = int(fields[5])
            self.softirq = int(fields[6])

    def __init__(self):
        self.last_raw_data = None
        self.samples = []

    def add_data(self, raw_data, clk_tck):
        if self.last_raw_data is not None:
            delta_timestamp = raw_data.timestamp - self.last_raw_data.timestamp
            delta_user = raw_data.user - self.last_raw_data.user
            delta_nice = raw_data.nice - self.last_raw_data.nice
            delta_system = raw_data.system - self.last_raw_data.system
            delta_idle = raw_data.idle - self.last_raw_data.idle
            delta_iowait = raw_data.iowait - self.last_raw_data.iowait
            delta_irq = raw_data.irq - self.last_raw_data.irq
            delta_softirq = raw_data.softirq - self.last_raw_data.softirq

            def calc_usage(ticks):
                if clk_tck == 0 or delta_timestamp == 0:
                    return 0
                return 100 * (ticks / clk_tck) / (delta_timestamp / 1000000)

            self.samples.append({
                "timestamp": raw_data.timestamp,
                "user": calc_usage(delta_user),
                "nice": calc_usage(delta_nice),
                "system": calc_usage(delta_system),
                "idle": calc_usage(delta_idle),
                "iowait": calc_usage(delta_iowait),
                "irq": calc_usage(delta_irq),
                "softirq": calc_usage(delta_softirq),
            })

        self.last_raw_data = copy.copy(raw_data)


class Memory(object):
    class RawData(object):
        def __init__(self, timestamp, s):
            self.timestamp = timestamp
            for line in s.split('\n'):
                fields = line.split(' ', 1)
                if len(fields) == 2:
                    val = scan_value(fields[1].strip())
                    if fields[0] == "MemTotal:":
                        self.total = val
                    elif fields[0] == "MemFree:":
                        self.free = val
                    elif fields[0] == "MemAvailable:":
                        self.available = val
                    elif fields[0] == "Buffers:":
                        self.buffers = val
                    elif fields[0] == "Cached:":
                        self.cached = val
                    elif fields[0] == "Dirty:":
                        self.dirty = val

    def __init__(self):
        self.samples = []

    def add_data(self, raw_data):
        self.samples.append({
            "timestamp": raw_data.timestamp,
            "total": raw_data.total,
            "free": raw_data.free,
            "available": raw_data.available,
            "buffers": raw_data.buffers,
            "cached": raw_data.cached,
            "dirty": raw_data.dirty,
        })


class Process(object):
    _RE = re.compile(r"\d+ \(([^\)]+)\) (.*)")

    class RawData(object):
        def __init__(self, timestamp, s):
            self.timestamp = timestamp
            match = Process._RE.match(s)
            if not match:
                raise ParseError("Invalid process data: '%s'" % s)
            self.name = match.group(1)
            fields = match.group(2).split(' ')
            if len(fields) < 22:
                raise ParseError("Invalid process data: '%s'" % s)
            self.state = fields[0]
            self.user = int(fields[11])
            self.system = int(fields[12])
            self.vsize = int(fields[20])
            self.rss = int(fields[21])

    def __init__(self, pid, name):
        self.pid = pid
        self.name = name
        self.last_raw_data = None
        self.samples = []

    def add_data(self, raw_data, clk_tck, page_size):
        if self.last_raw_data is not None:
            delta_timestamp = raw_data.timestamp - self.last_raw_data.timestamp
            delta_user = raw_data.user - self.last_raw_data.user
            delta_system = raw_data.system - self.last_raw_data.system

            def calc_usage(ticks):
                if clk_tck == 0 or delta_timestamp == 0:
                    return 0
                return 100 * (ticks / clk_tck) / (delta_timestamp / 1000000)

            self.samples.append({
                "timestamp": raw_data.timestamp,
                "state": raw_data.state,
                "user": calc_usage(delta_user),
                "system": calc_usage(delta_system),
                "vsize": raw_data.vsize,
                "rss": raw_data.rss * page_size,
            })

        self.last_raw_data = copy.copy(raw_data)


class Encoder(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, Memory):
            return obj.samples
        elif isinstance(obj, Process):
            return { "name": obj.name, "samples": obj.samples }
        elif isinstance(obj, Cpu):
            return obj.samples
        else:
            return json.JSONEncoder.default(self, obj)


class Sysmon(object):
    def __init__(self):
        self.clk_tck = 0
        self.page_size = 0
        self.memory = Memory()
        self.processes = {}
        self.total_cpu = Cpu()
        self.cpus = {}

    def set_system_config(self, clk_tck, page_size):
        self.clk_tck = clk_tck
        self.page_size = page_size

    def add_system_stat(self, timestamp, data):
        for line in data.split('\n'):
            fields = line.split(' ', 1)
            if len(fields) == 2:
                if fields[0] == "cpu":
                    raw_data = Cpu.RawData(timestamp, fields[1].strip())
                    self.total_cpu.add_data(raw_data, self.clk_tck)
                elif fields[0].startswith("cpu"):
                    idx = int(fields[0][3:])
                    raw_data = Cpu.RawData(timestamp, fields[1].strip())
                    try:
                        cpu = self.cpus[idx]
                    except KeyError:
                        cpu = Cpu()
                        self.cpus[idx] = cpu
                    cpu.add_data(raw_data, self.clk_tck)

    def add_system_mem(self, timestamp, data):
        try:
            raw_data = Memory.RawData(timestamp, data)
            self.memory.add_data(raw_data)
        except ParseError as ex:
            logging.warning(ex)

    def add_system_disk(self, timestamp, data):
        pass

    def add_system_net(self, timestamp, data):
        pass

    def add_process_stat(self, pid, timestamp, data):
        try:
            raw_data = Process.RawData(timestamp, data)
            try:
                process = self.processes[pid]
            except KeyError:
                process = Process(pid, raw_data.name)
                self.processes[pid] = process
            process.add_data(raw_data, self.clk_tck, self.page_size)
        except ParseError as ex:
            logging.warning(ex)

    def add_thread_stat(self, pid, tid, timestamp, data):
        pass

    def write(self, fout):
        obj = {
            "memory": self.memory,
            "processes": self.processes,
            "total_cpu": self.total_cpu,
            "cpus": self.cpus,
        }
        fout.write(json.dumps(obj, cls=Encoder, indent=2).encode("UTF-8"))
