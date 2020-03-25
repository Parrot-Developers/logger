# Logger: Parrot Drones Flight Data Recording framework

## liblogger

Library implementing the core of the logging framework. It creates `log.bin`
files containing several debug data from several data sources. Data is
compressed with `lz4` and optionally encrypted with an asymmetric RSA key.

## loggerd

Daemon on top of `liblogger`.

## plugins

Plugins for each source of logs.

- **file**: dump any file with monitoring of modifications.
- **properties**: dump system properties.
- **settings**: dump settings from shared settings library.
- **sysmon**: dump various system files from `/proc`/.
- **telemetry**: dump telemetry data from shared memory.
- **ulog**: dump debug logs from the `ulog` library.

## libloghdr

Library to access information stored in the header of `log.bin` files.

## liblogextract

Library to extract data from a `log.bin`.

## liblog2gutma

Convert a `log.bin` into a json file conforming to the `gutma` specification.

https://github.com/gutma-org/flight-logging-protocol

## tools

Python scripts to extract data from a `log.bin`.
