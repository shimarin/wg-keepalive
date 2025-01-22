wg-keepalive
===

## Pre-requisites

- gcc
- make
- argparse
- iniparser
- spdlog

## Build

```sh
make
```

## Usage

```sh
./wg-keepalive --help
```

## Configuration

/etc/wg-keepalive/<interface>.conf

```ini
timeout=<timeout in seconds default 300>
interval=<interval in seconds default 60>
pre-restart-command=<command to run before restarting the interface(optional)>
restart-command=<command to run to restart the interface default `systemctl restart wg-quick\@$WG_INTERFACE`>
post-restart-command=<command to run after restarting the interface(optional)>
```

WG_INTERFACE is the interface name passed as environment variable.

Commands will be passed to system shell directly. So you need to escape special characters.
