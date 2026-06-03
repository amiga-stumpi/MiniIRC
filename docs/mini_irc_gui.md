# MiniIRC GUI

`MiniIRC` is a standalone GUI-only Intuition IRC client for AmigaOS 1.3. It
uses `bsdsocket.library` directly and keeps the networking path independent of
TheWire13 internals.

## Startup

Run:

```text
MiniIRC
```

MiniIRC opens its own screen and does not provide a Shell/CLI mode. Use
`stack 16000` while testing. Open `Project -> Connect` to enter the IRC
server, port, and nick.

## Address Book

The connect requester includes a small address book. Entries are stored in the
current directory as:

```text
mini_irc.addr
```

Runtime settings are stored in:

```text
mini_irc.conf
```

Supported settings include:

```text
background=Black
text_color=White
```

Each line uses:

```text
host|port|nick
```

The requester can save the current host, port, and nick. The Delete button removes the selected address-book entry and rewrites the file. The first saved entry
is loaded as the startup default.

## Channel Tabs

The top row contains chat tabs. `Status` is always present for server messages.
When joining a channel, MiniIRC creates a tab named after that channel and makes
it active. Incoming channel messages are stored in their channel tab. The output
area only shows the currently selected tab, and the active tab is highlighted.
The output pane shows the active channel topic as a single truncated header line
with a horizontal separator below it. Inactive channels with new messages are
drawn in green until they are selected.
When a channel has more users than visible rows, the user list shows `Up` and
`Down` buttons for scrolling. After the NAMES list finishes, MiniIRC sends
WHOIS requests for the visible channel users and uses reply `317` to mark users
idle for at least five minutes. The user list is sorted alphabetically, with OP
and HalfOP users kept at the top and drawn in green. MiniIRC also tracks basic
`MODE +o/-o/+h/-h` changes for that display. WHOIS data for the active tab is
refreshed every 120 seconds. Normal idle users are drawn in yellow when the
current screen depth provides a yellow pen.

Join channels with the `Join` field and button. `#` is added automatically when
omitted. Use the `List` button to request the server channel list. The channel
list opens in a separate window. While the server replies are being collected,
the window shows `Retrieving channel list... please wait`; the completed list is
drawn once the server finishes the response. Double-click a channel to join it,
or press `Cancel` to close the list without choosing a channel.

## Messages

Select a channel tab, type a message in the message field, and press `Send`.
Messages cannot be sent from the `Status` tab. Channel and private chat messages
are displayed with a current system-time prefix, for example `[12:34] <nick> text`.
Long messages wrap inside the active chat output pane instead of disappearing
behind the user list.

The connect requester has a Nick field. While connected, type `/nick newname`
in the message field to send the normal IRC `NICK` command. MiniIRC updates the
local nick when the command is sent and also follows server `NICK` messages in
the channel user lists.

## Font

Use `Settings -> Font...` to scan the local `fonts` directory and `FONTS:` and
select a font, available bitmap size, and text color. MiniIRC prefers 8-pixel
fonts, especially `IBM.font/8` or `ibm.font/8`, then falls back to
`ruby.font/8`, `topaz.font/11`, or the screen font. On startup MiniIRC checks local `fonts/IBM.font/8` and `fonts/ibm.font/8`
first. It also accepts the normal Amiga bitmap font layout with
`fonts/IBM.font` plus `fonts/IBM/8`, or `fonts/ibm.font` plus `fonts/ibm/8`,
before using `FONTS:`. The selected font is used for MiniIRC drawing and recalculates the
visible rows and columns. The selected text color is saved to `mini_irc.conf`.

## Menus

Project:

- `Connect`: opens the connect/address-book requester.
- `Disconnect`: sends `QUIT :MiniIRC Kick1.3 v0.7` directly, waits up to one second for the server to close the connection, and then clears local channel tabs.
- `Quit`: exits MiniIRC.

Settings:

- `Address Book...`: opens the same compact connect/address-book requester.
- `Font...`: opens the OS1.3-safe font, size, and text color selector.
- `Background...`: opens the OS1.3-safe background color selector and saves the selection to `mini_irc.conf`.

?:

- `Info`: opens the MiniIRC info dialog with version and author information.

## Private Chats

When another user sends a private message to your current nick, MiniIRC opens a
private chat tab named after that user. Double-clicking a nick in the user list
opens the same kind of private chat tab. The tab user list contains your nick
and the other user. Typing in that private tab sends replies back to that nick.
The Leave button closes private chat tabs locally without sending `PART`.

## Debug Log

The live file debug log is disabled in the default build. It can be re-enabled
for diagnostics with `MINI_IRC_FILE_DEBUG` in the source.

## IRC Keepalive

PING/PONG handling is command-token based and case-insensitive. MiniIRC sends
`PONG` using the parsed IRC payload and avoids long blocking waits in the GUI
send path.

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

## MiniIRC v0.7 Layout

MiniIRC now opens on its own AmigaOS 1.3 custom screen with selectable background color. It chooses 2, 3, or 4 bitplanes based on available Chip RAM and now prefers 4 bitplanes more aggressively when enough memory is available. The main window uses a KVIrc-style split layout: channels are listed on the left, the active channel output is in the center, the active channel user list is on the right, and separate Channel/Join and Text/Send input rows are fixed at the bottom with visible field frames. Selecting a channel in the left list changes the active view. The active channel row contains a Leave button that sends PART and removes the channel tab. The user list is populated from IRC NAMES replies and updated on basic JOIN/PART/QUIT events.
