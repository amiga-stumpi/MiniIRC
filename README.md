# MiniIRC

MiniIRC is an experimental AmigaOS 1.3 IRC client for 68000 systems. It opens
its own Intuition screen, uses a KVIrc-style layout, and talks to the network
through `bsdsocket.library`.

Version:

```text
MiniIRC v0.2 by Marcel Jaehne (c)2026
```

## Requirements

- AmigaOS 1.3 / Kickstart 1.3
- 68000-compatible Amiga
- TheWire13 running with a working SANA-II device
- `bsdsocket.library` installed
- bebbo `m68k-amigaos-gcc` toolchain for building

MiniIRC is a standalone project. It does not link against TheWire13 internals;
it only uses the public bsdsocket-compatible API.

## Build

```sh
make clean && make
```

The output binary is:

```text
build/MiniIRC
```

Copy it to the Amiga, for example:

```text
copy build/MiniIRC C:
```

## Usage

Start `MiniIRC` from Shell or Workbench. The client opens its own screen. Use
the menu entry `Project -> Connect` to open the connect/address-book window.

The main screen layout is:

- left side: channel list
- center: active channel output
- right side: user list for the active channel
- bottom: join field and message input field

Joining a channel creates/selects the channel entry. Selecting a channel on the
left changes the active conversation. The user list is filled from IRC NAMES
replies and updated for basic JOIN/PART/QUIT events.

## Address Book

MiniIRC stores address entries in the startup/current directory using the
existing MiniIRC address-book file format. Entries contain server, port, and
nickname data.

## Notes

- The GUI is plain Intuition and avoids OS2+ APIs.
- Only one IRC server connection is supported at a time.
- TLS is not supported.
- DCC is not supported.
- IRCv3 extensions are not supported.
