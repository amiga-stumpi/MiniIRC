# MiniIRC GUI

`MiniIRC` is now a standalone Intuition IRC client for AmigaOS 1.3. It
uses `bsdsocket.library` directly and keeps the networking path independent of
TheWire13 internals.

## Startup

Run:

```text
MiniIRC
```

Use `stack 16000` while testing. Open `Project -> Connect` to enter the IRC
server, port, and nick.

## Address Book

The connect requester includes a small address book. Entries are stored in the
current directory as:

```text
mini_irc.addr
```

Each line uses:

```text
host|port|nick
```

The requester can save the current host, port, and nick. The first saved entry
is loaded as the startup default.

## Channel Tabs

The top row contains chat tabs. `Status` is always present for server messages.
When joining a channel, MiniIRC creates a tab named after that channel and makes
it active. Incoming channel messages are stored in their channel tab. The output
area only shows the currently selected tab, and the active tab is highlighted.

Join channels with the `Join` field and button. `#` is added automatically when
omitted.

## Messages

Select a channel tab, type a message in the message field, and press `Send`.
Messages cannot be sent from the `Status` tab.

## Font

Use `Settings -> Font...` to scan `FONTS:` and select a font and available
bitmap size. MiniIRC prefers 8-pixel fonts, especially `IBM.font/8` or
`ibm.font/8`, then falls back to `ruby.font/8`, `topaz.font/11`, or the screen
font. The selected font is used for MiniIRC drawing and recalculates the visible
rows and columns.

## Menus

Project:

- `Connect`: opens the connect/address-book requester.
- `Disconnect`: sends `QUIT` and closes the socket.
- `Quit`: exits MiniIRC.

Settings:

- `Address Book...`: opens the same compact connect/address-book requester.
- `Font...`: opens the OS1.3-safe font selector.

## Event Loop Responsiveness

MiniIRC limits socket receive draining per GUI tick so IRC bursts cannot starve
Intuition events. Menus, window close, Join, and Send remain serviceable while
incoming data is being processed.

## Limits

This first GUI version is intentionally small:

- up to 8 tabs including `Status`
- up to 48 retained lines per tab
- up to 8 address-book entries
- plain text rendering; no IRC color formatting yet

The implementation uses plain Intuition gadgets and menus only; no GadTools,
MUI, ReAction, ASL, or OS2+ APIs are required.

## MiniIRC v0.2 Layout

MiniIRC now opens on its own black-background AmigaOS 1.3 custom screen. It chooses 2, 3, or 4 bitplanes using the same available Chip RAM thresholds as MiniTelnet. The main window uses a KVIrc-style split layout: channels are listed on the left, the active channel output is in the center, the active channel user list is on the right, and the Join/Input fields are fixed at the bottom. Selecting a channel in the left list changes the active view. The user list is populated from IRC NAMES replies and updated on basic JOIN/PART/QUIT events.
