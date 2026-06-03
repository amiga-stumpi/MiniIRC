# MiniIRC

MiniIRC is an experimental GUI-only AmigaOS 1.3 IRC client for 68000 systems. It opens
its own Intuition screen with selectable background color, uses a KVIrc-style layout, and talks
to the network through `bsdsocket.library`.

Version:

```text
MiniIRC v0.7 by Marcel Jaehne (c)2026
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

The main GUI output binary is:

```text
build/MiniIRC
```

Copy it to the Amiga, for example:

```text
copy build/MiniIRC C:
```

MiniIRC no longer includes a Shell/CLI client; the project is GUI-only.

## Usage

Start `MiniIRC` from Shell or Workbench. The client opens its own screen. Use
the menu entry `Project -> Connect` to open the connect/address-book window
with server, port, and nick fields.

The main screen layout is:

- left side: channel list
- center: active channel output with a single-line topic header
- right side: user list for the active channel
- bottom: join field and message input field

Use `/nick newname` in the message input field to request a nick change on the
current IRC server. The active channel topic is shown as one truncated header
line above the chat output, separated by a horizontal line. Chat messages are
shown with the current system-time prefix, for example `[12:34] <nick> text`,
and long messages wrap inside the active chat output pane. Incoming private
messages open a separate
private chat tab with the sender, and replies from that tab go back to that
nick. Double-clicking a nick in the user list opens the same kind of private
chat tab.

Use `Settings -> Background...` to choose the screen background color. The
selection is saved to `mini_irc.conf` in the startup/current directory. Use
`? -> Info` to open the MiniIRC version and author information dialog.

Use `Settings -> Font...` to select a runtime font from the local `fonts`
directory or `FONTS:` and choose the text color. On startup MiniIRC checks local `fonts/IBM.font/8` and `fonts/ibm.font/8`
first. It also accepts the normal Amiga bitmap font layout with
`fonts/IBM.font` plus `fonts/IBM/8`, or `fonts/ibm.font` plus `fonts/ibm/8`,
before using `FONTS:`. It then tries `IBM.font/8`, `ibm.font/8`, `ruby.font/8`, and
`topaz.font/11` before falling back to the screen font. A small 8-pixel font is
recommended for the IRC lists and output. MiniIRC chooses 2, 3, or 4 bitplanes
based on available Chip RAM, preferring 4 bitplanes when enough memory is
available.

Joining a channel creates/selects the channel entry. The `List` button requests
the server channel list and opens a separate list window. The window shows
`Retrieving channel list... please wait` while replies are collected, then draws
the completed list once. Double-click a listed channel to join it, or use
`Cancel` to close the list without joining. Channel names such as `##amiga` are
preserved exactly; MiniIRC only adds `#` when the entered name has no IRC channel
prefix. Selecting
a channel on the left changes the active conversation. Inactive channels with
new messages are shown in green until selected. The active channel row
shows a Leave button for sending PART and closing that tab. The user list is
filled from IRC NAMES replies and updated for basic JOIN/PART/QUIT/MODE events.
Users are sorted alphabetically, with OP and HalfOP users kept at the top and
drawn in green. If there are more users than visible rows, `Up` and `Down`
buttons scroll the user list. MiniIRC refreshes WHOIS idle information in small
rotating batches on the active tab every 120 seconds. Normal users idle for at least
five minutes are drawn in yellow
when the active screen depth has a yellow pen available.

## Address Book

MiniIRC stores address entries in the startup/current directory using the
existing MiniIRC address-book file format. Entries contain server, port, and
nickname data. The Connect requester can save and delete address-book entries.

## Notes

- The GUI is plain Intuition and avoids OS2+ APIs.
- Only one IRC server connection is supported at a time.
- TLS is not supported.
- DCC is not supported.
- IRCv3 extensions are not supported.
