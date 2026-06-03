#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>

#include "amitcp13/bsdsocket.h"
#include "amitcp13/tools/mini_irc_session.h"

#define MINI_IRC_VERSION "v0.7"
#define MINI_IRC_GUI_TITLE "MiniIRC " MINI_IRC_VERSION " by Marcel Jaehne (c)2026"
#define MINI_IRC_QUIT_MESSAGE "QUIT :MiniIRC Kick1.3 " MINI_IRC_VERSION
#define MINI_IRC_ADDRBOOK_PATH "mini_irc.addr"
#define MINI_IRC_CONFIG_PATH "mini_irc.conf"
#define MINI_IRC_DEBUG_LOG_PATH "MiniIRC-debug.log"
#define MINI_IRC_FILE_DEBUG 0
#define MINI_IRC_QUIT_WAIT_TICKS 10
#define MINI_IRC_SEND_WAIT_SECONDS 1

#define MINI_IRC_HOST_SIZE 128
#define MINI_IRC_NICK_SIZE 32
#define MINI_IRC_CHAN_SIZE 64
#define MINI_IRC_LINE_SIZE 160
#define MINI_IRC_MSG_SIZE 256
#define MINI_IRC_RECV_SIZE 2048
#define MINI_IRC_SEND_SIZE 512
#define MINI_IRC_MAX_TABS 8
#define MINI_IRC_TAB_LINES 48
#define MINI_IRC_MAX_ADDRS 8
#define MINI_IRC_MAX_USERS 48
#define MINI_IRC_IDLE_THRESHOLD_SECONDS 300UL
#define MINI_IRC_WHOIS_INTERVAL_TICKS 6000UL
#define MINI_IRC_LIST_MAX_CHANNELS 96
#define MINI_IRC_LIST_VISIBLE 13
#define MINI_IRC_LIST_ROW_H 10
#define MINI_IRC_LIST_WIN_W 430
#define MINI_IRC_LIST_WIN_H 190
#define MINI_IRC_LIST_X 12
#define MINI_IRC_LIST_Y 24
#define MINI_IRC_LIST_W 406
#define MINI_IRC_LEFT_W 112
#define MINI_IRC_RIGHT_W 118
#define MINI_IRC_BOTTOM_H 46
#define MINI_IRC_DEFAULT_PORT 6667
#define MINI_IRC_CONNECT_TIMEOUT 15
#define MINI_IRC_RX_CHUNKS_PER_TICK 4
#define MINI_IRC_CHIPRAM_80K 81920UL
#define MINI_IRC_CHIPRAM_160K 163840UL
#define MINI_IRC_CONNECT_WIN_W 424
#define MINI_IRC_CONNECT_WIN_H 152
#define MINI_IRC_CONNECT_LIST_X 12
#define MINI_IRC_CONNECT_LIST_Y 26
#define MINI_IRC_CONNECT_LIST_W 170
#define MINI_IRC_CONNECT_ROW_H 10
#define MINI_IRC_CONNECT_VISIBLE 8
#define MINI_IRC_CONNECT_HOST_X 246
#define MINI_IRC_CONNECT_HOST_Y 30
#define MINI_IRC_CONNECT_HOST_W 170
#define MINI_IRC_CONNECT_PORT_X 246
#define MINI_IRC_CONNECT_PORT_Y 54
#define MINI_IRC_CONNECT_PORT_W 60
#define MINI_IRC_CONNECT_NICK_X 246
#define MINI_IRC_CONNECT_NICK_Y 78
#define MINI_IRC_CONNECT_NICK_W 170
#define MINI_IRC_CONNECT_STRING_H 12
#define MINI_IRC_CONNECT_BUTTON_Y 118
#define MINI_IRC_FONT_NAME_MAX 64
#define MINI_IRC_FONT_MAX 64
#define MINI_IRC_FONT_SIZE_MAX 16
#define MINI_IRC_FONT_VISIBLE 10
#define MINI_IRC_FONT_ROW_H 10
#define MINI_IRC_FONT_LIST_X 12
#define MINI_IRC_FONT_LIST_Y 24
#define MINI_IRC_FONT_LIST_W 150
#define MINI_IRC_FONT_SIZE_X 174
#define MINI_IRC_FONT_SIZE_W 58
#define MINI_IRC_FONT_COLOR_X 238
#define MINI_IRC_FONT_COLOR_W 94
#define MINI_IRC_FONT_LIST_H (MINI_IRC_FONT_VISIBLE * MINI_IRC_FONT_ROW_H)
#define MINI_IRC_FONT_WIN_W 390
#define MINI_IRC_FONT_WIN_H 178
#define MINI_IRC_TEXT_COLOR_MAX 8
#define MINI_IRC_BG_COLOR_MAX 8
#define MINI_IRC_BG_WIN_W 210
#define MINI_IRC_BG_WIN_H 142
#define MINI_IRC_BG_LIST_X 14
#define MINI_IRC_BG_LIST_Y 24
#define MINI_IRC_BG_ROW_H 12

#define MINI_IRC_GID_JOIN_STR 1
#define MINI_IRC_GID_JOIN     2
#define MINI_IRC_GID_LIST     3
#define MINI_IRC_GID_MSG_STR  4
#define MINI_IRC_GID_SEND     5

#define MINI_IRC_CGID_HOST    20
#define MINI_IRC_CGID_PORT    21
#define MINI_IRC_CGID_NICK    22
#define MINI_IRC_CGID_CONNECT 23
#define MINI_IRC_CGID_SAVE    24
#define MINI_IRC_CGID_DELETE  25
#define MINI_IRC_CGID_CANCEL  26

#define SOL_SOCKET AMITCP13_SOL_SOCKET
#define SO_ERROR   AMITCP13_SO_ERROR
#define FIONBIO    AMITCP13_FIONBIO

struct MiniIrcSocketCtx
{
    struct Library *base;
    int fd;
    struct MiniIrcSession *session;
};

struct MiniIrcTab
{
    char name[MINI_IRC_CHAN_SIZE];
    char lines[MINI_IRC_TAB_LINES][MINI_IRC_LINE_SIZE];
    char users[MINI_IRC_MAX_USERS][MINI_IRC_NICK_SIZE];
    ULONG idle_seconds[MINI_IRC_MAX_USERS];
    UBYTE idle_known[MINI_IRC_MAX_USERS];
    int user_count;
    int names_receiving;
    int next_line;
    int line_count;
    UBYTE unread;
};

struct MiniIrcAddr
{
    char host[MINI_IRC_HOST_SIZE];
    char port[8];
    char nick[MINI_IRC_NICK_SIZE];
};

struct MiniIrcChannelListEntry
{
    char name[MINI_IRC_CHAN_SIZE];
    char users[8];
    char topic[72];
};

struct MiniIrcBgColor
{
    const char *name;
    UWORD rgb;
};

struct MiniIrcGui
{
    struct Library *socket_base;
    int fd;
    int connected;
    int running;
    struct MiniIrcSession session;
    struct MiniIrcSocketCtx sock_ctx;
};

static struct Screen *g_screen;
static struct Window *g_win;
static struct TextFont *g_screen_font;
static struct TextFont *g_gui_font;
static UBYTE g_gui_font_opened;
static char g_font_name[MINI_IRC_FONT_NAME_MAX];
static UWORD g_font_size;
static ULONG g_screen_chip_free;
static UBYTE g_screen_depth;
static UBYTE g_bg_color_index;
static UBYTE g_text_color_index;
static UBYTE g_font_color_selected;
static WORD g_char_w = 8;
static WORD g_char_h = 8;
static WORD g_baseline = 7;

static BPTR g_debug_fh;
static unsigned long g_debug_seq;
static struct MiniIrcGui g_gui;
static struct MiniIrcTab g_tabs[MINI_IRC_MAX_TABS];
static int g_tab_count;
static int g_active_tab;
static struct MiniIrcAddr g_addrs[MINI_IRC_MAX_ADDRS];
static int g_addr_count;
static char g_last_user_click_nick[MINI_IRC_NICK_SIZE];
static ULONG g_last_user_click_seconds;
static ULONG g_last_user_click_micros;
static ULONG g_whois_interval_ticks;
static int g_user_scroll_top;
static WORD g_user_up_x;
static WORD g_user_up_y;
static WORD g_user_down_x;
static WORD g_user_down_y;
static WORD g_user_scroll_w;
static WORD g_user_scroll_h;
static struct MiniIrcChannelListEntry g_channel_list[MINI_IRC_LIST_MAX_CHANNELS];
static int g_channel_list_count;
static int g_channel_list_complete;
static struct Window *g_channel_list_window;
static int g_channel_list_top;
static int g_channel_list_selected;
static char g_last_channel_list_click[MINI_IRC_CHAN_SIZE];
static ULONG g_last_channel_list_click_seconds;
static ULONG g_last_channel_list_click_micros;

static char g_host_buf[MINI_IRC_HOST_SIZE] = "irc.libera.chat";
static char g_host_undo[MINI_IRC_HOST_SIZE];
static char g_port_buf[8] = "6667";
static char g_port_undo[8];
static char g_nick_buf[MINI_IRC_NICK_SIZE] = "amiga13";
static char g_nick_undo[MINI_IRC_NICK_SIZE];
static char g_join_buf[MINI_IRC_CHAN_SIZE];
static char g_join_undo[MINI_IRC_CHAN_SIZE];
static char g_msg_buf[MINI_IRC_MSG_SIZE];
static char g_msg_undo[MINI_IRC_MSG_SIZE];
static UBYTE g_recv_buf[MINI_IRC_RECV_SIZE];
static char g_rx_line[MINI_IRC_SEND_SIZE];
static int g_rx_len;
static char g_send_buf[MINI_IRC_SEND_SIZE];
static struct Amitcp13BsdSockAddrIn g_addr;
static struct Amitcp13BsdFdSet g_read_fds;
static struct Amitcp13BsdFdSet g_write_fds;
static struct Amitcp13BsdTimeVal g_timeout;
static ULONG g_wait_signals;
static LONG g_one = 1;

static struct StringInfo g_join_si;
static struct StringInfo g_msg_si;
static struct Gadget g_msg_gadget;
static struct Gadget g_send_gadget;
static struct Gadget g_join_gadget;
static struct Gadget g_join_button;
static struct Gadget g_list_button;

static struct Menu g_menus[3];
static struct MenuItem g_project_items[3];
static struct MenuItem g_settings_items[3];
static struct MenuItem g_help_items[1];
static struct IntuiText g_project_text[3];
static struct IntuiText g_settings_text[3];
static struct IntuiText g_help_text[1];


static const struct MiniIrcBgColor g_text_colors[MINI_IRC_TEXT_COLOR_MAX] = {
    { "White", 0xfff },
    { "Green", 0x0f0 },
    { "Yellow", 0xff0 },
    { "Cyan", 0x0ff },
    { "Red", 0xf00 },
    { "Blue", 0x00f },
    { "Purple", 0xf0f },
    { "Gray", 0xaaa }
};

static const struct MiniIrcBgColor g_bg_colors[MINI_IRC_BG_COLOR_MAX] = {
    { "Black", 0x000 },
    { "Blue", 0x008 },
    { "Green", 0x080 },
    { "Red", 0x800 },
    { "Purple", 0x808 },
    { "Teal", 0x088 },
    { "Gray", 0x333 },
    { "Dark Gray", 0x111 }
};
static char g_font_names[MINI_IRC_FONT_MAX][MINI_IRC_FONT_NAME_MAX];
static UWORD g_font_count;
static UWORD g_font_selected;
static UWORD g_font_top;
static UWORD g_font_sizes[MINI_IRC_FONT_SIZE_MAX];
static UWORD g_font_size_count;
static UWORD g_font_size_selected;
struct Library *DiskfontBase;

static int text_len(const char *s);
static void layout_window(void);
static void redraw_all(void);
static void draw_channel_list(void);
static void draw_user_list(void);
static void draw_output(void);
static void update_main_gadget_positions(void);
static void process_rx_bytes(const char *data, int len);
static void poll_socket(void);
static void draw_button_window(struct Window *win, WORD x, WORD y, WORD w, WORD h, const char *label);
static void draw_field_box_window(struct Window *win, WORD x, WORD y, WORD w, WORD h);
static void open_background_selector(void);
static void open_channel_list_window(void);
static void open_info_dialog(void);
static void apply_screen_palette(void);

static WORD g_list_top;
static WORD g_list_bottom;
static WORD g_input_y;
static WORD g_status_y;
static WORD g_chan_x;
static WORD g_chan_w;
static WORD g_user_x;
static WORD g_user_w;
static WORD g_term_x;
static WORD g_term_y;
static WORD g_term_w;
static WORD g_term_h;
static WORD g_leave_x;
static WORD g_leave_y;
static WORD g_leave_w;
static WORD g_leave_h;


static void debug_write_raw(const char *text)
{
    if (g_debug_fh && text)
        Write(g_debug_fh, (APTR)text, text_len(text));
}

static void debug_write_ulong(unsigned long value)
{
    char tmp[12];
    char out[12];
    int pos = 0;
    int i;

    if (value == 0) {
        debug_write_raw("0");
        return;
    }
    while (value && pos < (int)sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (value % 10UL));
        value /= 10UL;
    }
    for (i = 0; i < pos; ++i)
        out[i] = tmp[pos - 1 - i];
    out[i] = 0;
    debug_write_raw(out);
}

static void debug_log(const char *tag, const char *text)
{
    if (!g_debug_fh)
        return;
    debug_write_ulong(++g_debug_seq);
    debug_write_raw(" ");
    debug_write_raw(tag ? tag : "EVENT");
    if (text && text[0]) {
        debug_write_raw(" ");
        debug_write_raw(text);
    }
    debug_write_raw("\n");
}

static void debug_log_num(const char *tag, LONG value)
{
    char sign[2];

    if (!g_debug_fh)
        return;
    debug_write_ulong(++g_debug_seq);
    debug_write_raw(" ");
    debug_write_raw(tag ? tag : "NUM");
    debug_write_raw(" ");
    if (value < 0) {
        sign[0] = '-';
        sign[1] = 0;
        debug_write_raw(sign);
        value = -value;
    }
    debug_write_ulong((unsigned long)value);
    debug_write_raw("\n");
}

static void debug_open(void)
{
#if MINI_IRC_FILE_DEBUG
    g_debug_fh = Open((STRPTR)MINI_IRC_DEBUG_LOG_PATH, MODE_NEWFILE);
    g_debug_seq = 0;
    debug_log("START", MINI_IRC_GUI_TITLE);
#else
    g_debug_fh = 0;
    g_debug_seq = 0;
#endif
}

static void debug_close(void)
{
#if MINI_IRC_FILE_DEBUG
    debug_log("STOP", "MiniIRC exit");
    if (g_debug_fh) {
        Close(g_debug_fh);
        g_debug_fh = 0;
    }
#else
    g_debug_fh = 0;
#endif
}

#if MINI_IRC_FILE_DEBUG
static void session_debug_cb(void *ctx, const char *tag, const char *text)
{
    (void)ctx;
    debug_log(tag, text);
}
#endif

static int text_len(const char *s)
{
    int n = 0;

    if (!s)
        return 0;
    while (s[n])
        ++n;
    return n;
}

static void copy_text(char *dst, int max_len, const char *src)
{
    int i = 0;

    if (!dst || max_len <= 0)
        return;
    if (!src)
        src = "";
    while (src[i] && i < max_len - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int append_text(char *dst, int *pos, int max_len, const char *src)
{
    int i = 0;

    while (src && src[i]) {
        if (*pos >= max_len - 1)
            return 0;
        dst[*pos] = src[i];
        ++(*pos);
        ++i;
    }
    dst[*pos] = 0;
    return 1;
}

static int append_char(char *dst, int *pos, int max_len, char c)
{
    if (*pos >= max_len - 1)
        return 0;
    dst[*pos] = c;
    ++(*pos);
    dst[*pos] = 0;
    return 1;
}

static int append_two_digits(char *dst, int *pos, int max_len, int value)
{
    if (value < 0)
        value = 0;
    if (value > 99)
        value = value % 100;
    return append_char(dst, pos, max_len, (char)('0' + (value / 10))) &&
           append_char(dst, pos, max_len, (char)('0' + (value % 10)));
}

static int append_time_prefix(char *dst, int *pos, int max_len)
{
    struct DateStamp ds;
    LONG minutes;
    int hour;
    int minute;

    DateStamp(&ds);
    minutes = ds.ds_Minute;
    if (minutes < 0)
        minutes = 0;
    minutes %= 1440;
    hour = (int)(minutes / 60);
    minute = (int)(minutes % 60);

    return append_char(dst, pos, max_len, '[') &&
           append_two_digits(dst, pos, max_len, hour) &&
           append_char(dst, pos, max_len, ':') &&
           append_two_digits(dst, pos, max_len, minute) &&
           append_text(dst, pos, max_len, "] ");
}

static int append_chat_prefix(char *dst, int *pos, int max_len, const char *nick)
{
    return append_time_prefix(dst, pos, max_len) &&
           append_text(dst, pos, max_len, "<") &&
           append_text(dst, pos, max_len, nick ? nick : "") &&
           append_text(dst, pos, max_len, "> ");
}

static int text_equal_ci(const char *a, const char *b)
{
    char ca;
    char cb;

    if (!a || !b)
        return 0;
    while (*a && *b) {
        ca = *a;
        cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb + 32);
        if (ca != cb)
            return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static char upper_ascii(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - 32);
    return c;
}

static const char *skip_spaces(const char *p)
{
    while (p && (*p == ' ' || *p == '\t'))
        ++p;
    return p;
}

static void trim_text(char *s)
{
    char *p;
    int len;

    if (!s)
        return;
    p = s;
    while (*p == ' ' || *p == '\t')
        ++p;
    if (p != s)
        memmove(s, p, text_len(p) + 1);
    len = text_len(s);
    while (len > 0 &&
           (s[len - 1] == ' ' || s[len - 1] == '\t' ||
            s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = 0;
    }
}

static int parse_ulong_token(const char **p, ULONG *out)
{
    ULONG value = 0;
    int digits = 0;
    const char *s;

    if (!p || !*p || !out)
        return 0;
    s = skip_spaces(*p);
    while (*s >= '0' && *s <= '9') {
        value = value * 10UL + (ULONG)(*s - '0');
        ++digits;
        ++s;
    }
    if (digits == 0)
        return 0;
    *out = value;
    *p = s;
    return 1;
}

static int parse_port(const char *text, UWORD *out_port)
{
    ULONG value = 0;
    int digits = 0;
    const char *p = text;

    p = skip_spaces(p);
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (ULONG)(*p - '0');
        if (value > 65535)
            return 0;
        ++digits;
        ++p;
    }
    p = skip_spaces(p);
    if (*p || digits == 0 || value == 0)
        return 0;
    *out_port = (UWORD)value;
    return 1;
}

static int parse_dec_octet(const char **p, UBYTE *out)
{
    ULONG value = 0;
    int digits = 0;
    const char *s = *p;

    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (ULONG)(*s - '0');
        if (value > 255)
            return 0;
        ++digits;
        ++s;
    }
    if (digits == 0)
        return 0;
    *out = (UBYTE)value;
    *p = s;
    return 1;
}

static int parse_ipv4(const char *text, ULONG *out_ip)
{
    UBYTE parts[4];
    const char *p = text;
    int i;

    for (i = 0; i < 4; ++i) {
        if (!parse_dec_octet(&p, &parts[i]))
            return 0;
        if (i != 3) {
            if (*p != '.')
                return 0;
            ++p;
        }
    }
    if (*p)
        return 0;
    *out_ip = ((ULONG)parts[0] << 24) | ((ULONG)parts[1] << 16) |
              ((ULONG)parts[2] << 8) | (ULONG)parts[3];
    return 1;
}


static int ends_with_font(const char *name)
{
    int len;

    if (!name)
        return 0;
    len = text_len(name);
    if (len < 6)
        return 0;
    return text_equal_ci(name + len - 5, ".font");
}

static void copy_font_name(char *dst, int max_len, const char *src)
{
    copy_text(dst, max_len, src ? src : "");
}

static int font_name_has_path(const char *name)
{
    int i;

    if (!name)
        return 0;
    for (i = 0; name[i]; ++i) {
        if (name[i] == '/' || name[i] == ':')
            return 1;
    }
    return 0;
}

static int parse_numeric_name(const char *name, UWORD *out)
{
    ULONG value = 0;
    int i = 0;

    if (!name || !name[0])
        return 0;
    while (name[i]) {
        if (name[i] < '0' || name[i] > '9')
            return 0;
        value = value * 10UL + (ULONG)(name[i] - '0');
        if (value > 32UL)
            return 0;
        ++i;
    }
    if (value < 4UL)
        return 0;
    *out = (UWORD)value;
    return 1;
}

static void strip_font_suffix(const char *font_name, char *base, int base_size)
{
    int len;
    int copy_len;
    int i;

    if (!base || base_size <= 0)
        return;
    len = text_len(font_name);
    copy_len = len;
    if (len > 5 && ends_with_font(font_name))
        copy_len = len - 5;
    if (copy_len >= base_size)
        copy_len = base_size - 1;
    for (i = 0; i < copy_len; ++i)
        base[i] = font_name[i];
    base[copy_len] = 0;
}

static void make_font_size_path(const char *font_name, char *path, int path_size)
{
    char base[MINI_IRC_FONT_NAME_MAX];
    int pos = 0;

    strip_font_suffix(font_name, base, sizeof(base));
    path[0] = 0;
    if (!font_name_has_path(font_name))
        append_text(path, &pos, path_size, "FONTS:");
    append_text(path, &pos, path_size, base);
}

static void add_font_size(UWORD size)
{
    UWORD i;

    for (i = 0; i < g_font_size_count; ++i) {
        if (g_font_sizes[i] == size)
            return;
    }
    if (g_font_size_count < MINI_IRC_FONT_SIZE_MAX)
        g_font_sizes[g_font_size_count++] = size;
}

static void add_scanned_font(const char *prefix, const char *name)
{
    int pos;

    if (g_font_count >= MINI_IRC_FONT_MAX || !name || !ends_with_font(name))
        return;
    pos = 0;
    g_font_names[g_font_count][0] = 0;
    if (prefix)
        append_text(g_font_names[g_font_count], &pos, MINI_IRC_FONT_NAME_MAX, prefix);
    append_text(g_font_names[g_font_count], &pos, MINI_IRC_FONT_NAME_MAX, name);
    if (text_equal_ci(g_font_names[g_font_count], g_font_name))
        g_font_selected = g_font_count;
    ++g_font_count;
}

static void scan_font_dir(const char *path, const char *prefix, struct FileInfoBlock *fib)
{
    BPTR lock;

    lock = Lock((STRPTR)path, ACCESS_READ);
    if (lock) {
        if (Examine(lock, fib)) {
            while (ExNext(lock, fib) && g_font_count < MINI_IRC_FONT_MAX) {
                if (fib->fib_DirEntryType < 0)
                    add_scanned_font(prefix, (const char *)fib->fib_FileName);
            }
        }
        UnLock(lock);
    }
}

static void scan_fonts(void)
{
    struct FileInfoBlock *fib;

    g_font_count = 0;
    g_font_selected = 0;
    g_font_top = 0;
    fib = (struct FileInfoBlock *)AllocMem(sizeof(struct FileInfoBlock), MEMF_CLEAR);
    if (!fib)
        return;
    scan_font_dir("fonts", "fonts/", fib);
    scan_font_dir("FONTS:", 0, fib);
    FreeMem(fib, sizeof(struct FileInfoBlock));
}

static void scan_font_sizes(void)
{
    BPTR lock;
    struct FileInfoBlock *fib;
    char path[96];
    UWORD size;
    UWORD i;

    g_font_size_count = 0;
    g_font_size_selected = 0;
    if (g_font_count == 0)
        return;
    fib = (struct FileInfoBlock *)AllocMem(sizeof(struct FileInfoBlock), MEMF_CLEAR);
    if (!fib)
        return;
    make_font_size_path(g_font_names[g_font_selected], path, sizeof(path));
    lock = Lock((STRPTR)path, ACCESS_READ);
    if (lock) {
        if (Examine(lock, fib)) {
            while (ExNext(lock, fib) && g_font_size_count < MINI_IRC_FONT_SIZE_MAX) {
                if (fib->fib_DirEntryType < 0 && parse_numeric_name((const char *)fib->fib_FileName, &size))
                    add_font_size(size);
            }
        }
        UnLock(lock);
    }
    FreeMem(fib, sizeof(struct FileInfoBlock));
    if (g_font_size_count == 0)
        add_font_size(g_font_size ? g_font_size : 8);
    for (i = 0; i < g_font_size_count; ++i) {
        if (g_font_sizes[i] == g_font_size) {
            g_font_size_selected = i;
            break;
        }
    }
}

static void number_to_text(UWORD value, char *dst, int dst_size)
{
    char tmp[8];
    int pos = 0;
    int i;

    if (!dst || dst_size <= 0)
        return;
    if (value == 0) {
        copy_text(dst, dst_size, "0");
        return;
    }
    while (value && pos < (int)sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (i = 0; i < pos && i < dst_size - 1; ++i)
        dst[i] = tmp[pos - 1 - i];
    dst[i] = 0;
}


static int local_font_size_path_exists(const char *path)
{
    BPTR lock;

    lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock)
        return 0;
    UnLock(lock);
    return 1;
}

static int local_font_size_available(const char *font_name, UWORD size)
{
    char path[96];
    char num[8];
    int pos;

    if (!font_name || !font_name_has_path(font_name))
        return 1;
    if (!local_font_size_path_exists(font_name))
        return 0;

    number_to_text(size, num, sizeof(num));

    path[0] = 0;
    pos = 0;
    append_text(path, &pos, sizeof(path), font_name);
    append_text(path, &pos, sizeof(path), "/");
    append_text(path, &pos, sizeof(path), num);
    if (local_font_size_path_exists(path))
        return 1;

    make_font_size_path(font_name, path, sizeof(path));
    pos = text_len(path);
    append_text(path, &pos, sizeof(path), "/");
    append_text(path, &pos, sizeof(path), num);
    return local_font_size_path_exists(path);
}

static struct TextFont *open_named_font(const char *name, UWORD size)
{
    struct TextAttr attr;
    struct TextFont *font;

    if (!name || !name[0] || size == 0)
        return 0;
    if (!local_font_size_available(name, size))
        return 0;
    attr.ta_Name = (STRPTR)name;
    attr.ta_YSize = size;
    attr.ta_Style = FS_NORMAL;
    attr.ta_Flags = FPF_ROMFONT | FPF_DISKFONT;
    font = OpenFont(&attr);
    if (font)
        return font;
    if (!DiskfontBase)
        DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 0);
    if (DiskfontBase)
        font = OpenDiskFont(&attr);
    return font;
}

static void update_font_metrics(void)
{
    if (g_gui_font) {
        g_char_w = g_gui_font->tf_XSize;
        g_char_h = g_gui_font->tf_YSize;
        g_baseline = g_gui_font->tf_Baseline;
    }
}

static int apply_gui_font(const char *font_name, UWORD font_size)
{
    struct TextFont *new_font;
    struct TextFont *old_font;
    UBYTE old_opened;

    new_font = open_named_font(font_name, font_size);
    if (!new_font)
        return 0;
    old_font = g_gui_font;
    old_opened = g_gui_font_opened;
    g_gui_font = new_font;
    g_gui_font_opened = 1;
    copy_font_name(g_font_name, sizeof(g_font_name), font_name);
    g_font_size = font_size;
    update_font_metrics();
    if (g_win)
        SetFont(g_win->RPort, g_gui_font);
    if (old_opened && old_font)
        CloseFont(old_font);
    if (g_win) {
        layout_window();
        update_main_gadget_positions();
        redraw_all();
    }
    return 1;
}

static void install_default_font(void)
{
    g_gui_font = g_screen_font;
    g_gui_font_opened = 0;
    copy_font_name(g_font_name, sizeof(g_font_name), "window font");
    g_font_size = 0;
    if (apply_gui_font("fonts/IBM.font", 8))
        return;
    if (apply_gui_font("fonts/ibm.font", 8))
        return;
    if (apply_gui_font("IBM.font", 8))
        return;
    if (apply_gui_font("ibm.font", 8))
        return;
    if (apply_gui_font("ruby.font", 8))
        return;
    if (apply_gui_font("topaz.font", 11))
        return;
    update_font_metrics();
}

static void close_gui_font(void)
{
    if (g_win && g_screen_font)
        SetFont(g_win->RPort, g_screen_font);
    if (g_gui_font_opened && g_gui_font)
        CloseFont(g_gui_font);
    g_gui_font = g_screen_font;
    g_gui_font_opened = 0;
    if (DiskfontBase) {
        CloseLibrary(DiskfontBase);
        DiskfontBase = 0;
    }
}

static void gui_rp(void)
{
    if (!g_win)
        return;
    if (g_gui_font)
        SetFont(g_win->RPort, g_gui_font);
    SetAPen(g_win->RPort, 1);
    SetBPen(g_win->RPort, 0);
    SetDrMd(g_win->RPort, JAM2);
}

static void draw_text_at(WORD x, WORD y, const char *text)
{
    if (!g_win || !text)
        return;
    Move(g_win->RPort, x, y);
    Text(g_win->RPort, (STRPTR)text, text_len(text));
}

static void clear_rect(WORD x1, WORD y1, WORD x2, WORD y2)
{
    if (!g_win)
        return;
    SetAPen(g_win->RPort, 0);
    RectFill(g_win->RPort, x1, y1, x2, y2);
    SetAPen(g_win->RPort, 1);
}

static void status_text(const char *text)
{
    gui_rp();
    clear_rect(4, (WORD)(g_status_y - g_baseline - 2),
               (WORD)(g_win->Width - 5), (WORD)(g_status_y + 3));
    draw_text_at(6, g_status_y, text ? text : "");
}

static void layout_window(void)
{
    WORD inner_left;
    WORD inner_right;
    WORD inner_top;
    WORD inner_bottom;

    inner_left = (WORD)(g_win->BorderLeft + 4);
    inner_right = (WORD)(g_win->Width - g_win->BorderRight - 5);
    inner_top = (WORD)(g_win->BorderTop + 4);
    inner_bottom = (WORD)(g_win->Height - g_win->BorderBottom - 5);
    g_chan_x = inner_left;
    g_chan_w = MINI_IRC_LEFT_W;
    g_user_w = MINI_IRC_RIGHT_W;
    g_user_x = (WORD)(inner_right - g_user_w + 1);
    g_term_x = (WORD)(g_chan_x + g_chan_w + 6);
    g_term_w = (WORD)(g_user_x - g_term_x - 6);
    if (g_term_w < 120)
        g_term_w = 120;
    g_input_y = (WORD)(inner_bottom - MINI_IRC_BOTTOM_H + 1);
    g_status_y = (WORD)(g_input_y - 5);
    g_list_top = (WORD)(inner_top + 12);
    g_list_bottom = (WORD)(g_status_y - g_char_h - 4);
    g_term_y = g_list_top;
    g_term_h = (WORD)(g_list_bottom - g_term_y + 1);
    if (g_term_h < g_char_h * 3)
        g_term_h = (WORD)(g_char_h * 3);
}

static void tab_append(int idx, const char *line)
{
    struct MiniIrcTab *tab;

    if (idx < 0 || idx >= g_tab_count)
        return;
    tab = &g_tabs[idx];
    copy_text(tab->lines[tab->next_line], MINI_IRC_LINE_SIZE, line);
    tab->next_line = (tab->next_line + 1) % MINI_IRC_TAB_LINES;
    if (tab->line_count < MINI_IRC_TAB_LINES)
        ++tab->line_count;
}

static void tab_mark_unread(int idx)
{
    if (idx > 0 && idx < g_tab_count && idx != g_active_tab)
        g_tabs[idx].unread = 1;
}

static int tab_find(const char *name)
{
    int i;

    for (i = 0; i < g_tab_count; ++i) {
        if (text_equal_ci(g_tabs[i].name, name))
            return i;
    }
    return -1;
}

static int tab_add(const char *name)
{
    int idx;

    idx = tab_find(name);
    if (idx >= 0)
        return idx;
    if (g_tab_count >= MINI_IRC_MAX_TABS)
        return -1;
    idx = g_tab_count++;
    copy_text(g_tabs[idx].name, MINI_IRC_CHAN_SIZE, name);
    g_tabs[idx].next_line = 0;
    g_tabs[idx].line_count = 0;
    g_tabs[idx].user_count = 0;
    g_tabs[idx].names_receiving = 0;
    g_tabs[idx].unread = 0;
    return idx;
}

static void tab_user_add(int tab_idx, const char *nick)
{
    struct MiniIrcTab *tab;
    char clean[MINI_IRC_NICK_SIZE];
    int i;
    int j;

    if (tab_idx < 0 || tab_idx >= g_tab_count || !nick || !nick[0])
        return;
    j = 0;
    while (*nick == '@' || *nick == '+' || *nick == '%' || *nick == '&' || *nick == '~')
        ++nick;
    while (nick[j] && nick[j] != ' ' && nick[j] != '\r' && nick[j] != '\n' &&
           j < (int)sizeof(clean) - 1) {
        clean[j] = nick[j];
        ++j;
    }
    clean[j] = 0;
    if (!clean[0])
        return;
    tab = &g_tabs[tab_idx];
    for (i = 0; i < tab->user_count; ++i) {
        if (text_equal_ci(tab->users[i], clean))
            return;
    }
    if (tab->user_count < MINI_IRC_MAX_USERS) {
        copy_text(tab->users[tab->user_count], MINI_IRC_NICK_SIZE, clean);
        tab->idle_seconds[tab->user_count] = 0;
        tab->idle_known[tab->user_count] = 0;
        ++tab->user_count;
    }
}

static void tab_user_remove(int tab_idx, const char *nick)
{
    struct MiniIrcTab *tab;
    int i;

    if (tab_idx < 0 || tab_idx >= g_tab_count || !nick || !nick[0])
        return;
    tab = &g_tabs[tab_idx];
    for (i = 0; i < tab->user_count; ++i) {
        if (text_equal_ci(tab->users[i], nick)) {
            while (i + 1 < tab->user_count) {
                copy_text(tab->users[i], MINI_IRC_NICK_SIZE, tab->users[i + 1]);
                tab->idle_seconds[i] = tab->idle_seconds[i + 1];
                tab->idle_known[i] = tab->idle_known[i + 1];
                ++i;
            }
            --tab->user_count;
            return;
        }
    }
}

static void tab_user_rename(int tab_idx, const char *old_nick, const char *new_nick)
{
    if (!old_nick || !old_nick[0] || !new_nick || !new_nick[0])
        return;
    tab_user_remove(tab_idx, old_nick);
    tab_user_add(tab_idx, new_nick);
}

static void tab_users_clear(int tab_idx)
{
    if (tab_idx < 0 || tab_idx >= g_tab_count)
        return;
    g_tabs[tab_idx].user_count = 0;
    memset(g_tabs[tab_idx].idle_seconds, 0, sizeof(g_tabs[tab_idx].idle_seconds));
    memset(g_tabs[tab_idx].idle_known, 0, sizeof(g_tabs[tab_idx].idle_known));
}


static void tab_user_set_idle(const char *nick, ULONG idle_seconds)
{
    int tab_idx;
    int i;
    int changed = 0;
    struct MiniIrcTab *tab;

    if (!nick || !nick[0])
        return;
    for (tab_idx = 0; tab_idx < g_tab_count; ++tab_idx) {
        tab = &g_tabs[tab_idx];
        for (i = 0; i < tab->user_count; ++i) {
            if (text_equal_ci(tab->users[i], nick)) {
                tab->idle_seconds[i] = idle_seconds;
                tab->idle_known[i] = 1;
                changed = 1;
            }
        }
    }
    if (changed)
        draw_user_list();
}

static int send_whois_for_nick(const char *nick)
{
    int pos = 0;

    if (!g_gui.connected || !nick || !nick[0])
        return 0;
    g_send_buf[0] = 0;
    if (!append_text(g_send_buf, &pos, sizeof(g_send_buf), "WHOIS ") ||
        !append_text(g_send_buf, &pos, sizeof(g_send_buf), nick))
        return 0;
    return mini_irc_session_send_line(&g_gui.session, g_send_buf);
}

static void request_whois_for_tab(int tab_idx)
{
    int i;
    struct MiniIrcTab *tab;

    if (tab_idx < 0 || tab_idx >= g_tab_count || !g_gui.connected)
        return;
    tab = &g_tabs[tab_idx];
    for (i = 0; i < tab->user_count; ++i)
        send_whois_for_nick(tab->users[i]);
}

static void open_private_chat_tab(const char *nick)
{
    int idx;

    if (!nick || !nick[0])
        return;
    if (text_equal_ci(nick, g_nick_buf)) {
        status_text("This is your nick");
        return;
    }
    idx = tab_add(nick);
    if (idx < 0) {
        status_text("No free chat tab");
        return;
    }
    tab_user_add(idx, g_nick_buf);
    tab_user_add(idx, nick);
    g_active_tab = idx;
    draw_channel_list();
    draw_user_list();
    draw_output();
    status_text("Private chat opened");
}

static void draw_panel_box(WORD x, WORD y, WORD w, WORD h, const char *title)
{
    SetAPen(g_win->RPort, 1);
    Move(g_win->RPort, x, y);
    Draw(g_win->RPort, (WORD)(x + w - 1), y);
    Draw(g_win->RPort, (WORD)(x + w - 1), (WORD)(y + h - 1));
    Draw(g_win->RPort, x, (WORD)(y + h - 1));
    Draw(g_win->RPort, x, y);
    if (title) {
        Move(g_win->RPort, (WORD)(x + 4), (WORD)(y + g_baseline + 2));
        Text(g_win->RPort, (STRPTR)title, text_len(title));
    }
}

static void draw_channel_list(void)
{
    WORD y;
    int i;
    char tmp[MINI_IRC_CHAN_SIZE];
    int max_chars;

    gui_rp();
    clear_rect(g_chan_x, (WORD)(g_win->BorderTop + 2),
               (WORD)(g_chan_x + g_chan_w - 1), g_list_bottom);
    draw_panel_box(g_chan_x, (WORD)(g_win->BorderTop + 2), g_chan_w,
                   (WORD)(g_list_bottom - g_win->BorderTop - 1), "Channels");
    g_leave_w = 0;
    g_leave_h = 0;
    max_chars = (g_chan_w - 8) / g_char_w;
    if (max_chars > MINI_IRC_CHAN_SIZE - 1)
        max_chars = MINI_IRC_CHAN_SIZE - 1;
    for (i = 0; i < g_tab_count; ++i) {
        y = (WORD)(g_list_top + g_baseline + 2 + i * g_char_h);
        if (y > g_list_bottom)
            break;
        SetAPen(g_win->RPort, 1);
        if (i == g_active_tab) {
            g_tabs[i].unread = 0;
            Move(g_win->RPort, (WORD)(g_chan_x + 2), (WORD)(y - g_baseline - 2));
            Draw(g_win->RPort, (WORD)(g_chan_x + g_chan_w - 3), (WORD)(y - g_baseline - 2));
            Draw(g_win->RPort, (WORD)(g_chan_x + g_chan_w - 3), (WORD)(y + 3));
            Draw(g_win->RPort, (WORD)(g_chan_x + 2), (WORD)(y + 3));
            Draw(g_win->RPort, (WORD)(g_chan_x + 2), (WORD)(y - g_baseline - 2));
            draw_text_at((WORD)(g_chan_x + 5), y, ">");
            copy_text(tmp, max_chars + 1, g_tabs[i].name);
            Move(g_win->RPort, (WORD)(g_chan_x + 14), y);
        } else {
            if (g_tabs[i].unread)
                SetAPen(g_win->RPort, 3);
            max_chars = (g_chan_w - 8) / g_char_w;
            if (max_chars > MINI_IRC_CHAN_SIZE - 1)
                max_chars = MINI_IRC_CHAN_SIZE - 1;
            copy_text(tmp, max_chars + 1, g_tabs[i].name);
            Move(g_win->RPort, (WORD)(g_chan_x + 4), y);
        }
        Text(g_win->RPort, (STRPTR)tmp, text_len(tmp));
    }
    if (g_active_tab > 0 && g_active_tab < g_tab_count) {
        g_leave_w = 50;
        g_leave_h = 14;
        g_leave_x = (WORD)(g_chan_x + ((g_chan_w - g_leave_w) / 2));
        g_leave_y = (WORD)(g_list_bottom - g_leave_h - 4);
        draw_button_window(g_win, g_leave_x, g_leave_y, g_leave_w, g_leave_h, "Leave");
    }
    SetAPen(g_win->RPort, 1);
}

static int user_visible_rows(void)
{
    int rows;

    rows = (g_list_bottom - g_list_top - 22) / g_char_h;
    if (rows < 1)
        rows = 1;
    return rows;
}

static void clamp_user_scroll(struct MiniIrcTab *tab)
{
    int max_top;

    if (!tab) {
        g_user_scroll_top = 0;
        return;
    }
    max_top = tab->user_count - user_visible_rows();
    if (max_top < 0)
        max_top = 0;
    if (g_user_scroll_top > max_top)
        g_user_scroll_top = max_top;
    if (g_user_scroll_top < 0)
        g_user_scroll_top = 0;
}

static void draw_user_list(void)
{
    WORD y;
    int i;
    int idx;
    int rows;
    int max_chars;
    struct MiniIrcTab *tab;
    char tmp[MINI_IRC_NICK_SIZE];

    gui_rp();
    clear_rect(g_user_x, (WORD)(g_win->BorderTop + 2),
               (WORD)(g_user_x + g_user_w - 1), g_list_bottom);
    draw_panel_box(g_user_x, (WORD)(g_win->BorderTop + 2), g_user_w,
                   (WORD)(g_list_bottom - g_win->BorderTop - 1), "Users");
    g_user_scroll_w = 46;
    g_user_scroll_h = 14;
    g_user_up_x = (WORD)(g_user_x + 8);
    g_user_down_x = (WORD)(g_user_x + g_user_w - g_user_scroll_w - 8);
    g_user_up_y = (WORD)(g_list_bottom - g_user_scroll_h - 4);
    g_user_down_y = g_user_up_y;
    if (g_active_tab < 0 || g_active_tab >= g_tab_count)
        return;
    tab = &g_tabs[g_active_tab];
    clamp_user_scroll(tab);
    max_chars = (g_user_w - 8) / g_char_w;
    if (max_chars > MINI_IRC_NICK_SIZE - 1)
        max_chars = MINI_IRC_NICK_SIZE - 1;
    rows = user_visible_rows();
    for (i = 0; i < rows; ++i) {
        idx = g_user_scroll_top + i;
        if (idx >= tab->user_count)
            break;
        y = (WORD)(g_list_top + g_baseline + 2 + i * g_char_h);
        if (y > g_user_up_y - 3)
            break;
        copy_text(tmp, max_chars + 1, tab->users[idx]);
        if (tab->idle_known[idx] &&
            tab->idle_seconds[idx] >= MINI_IRC_IDLE_THRESHOLD_SECONDS) {
            SetAPen(g_win->RPort, (g_screen_depth >= 3) ? 6 : 3);
            Move(g_win->RPort, (WORD)(g_user_x + 4), y);
            Text(g_win->RPort, (STRPTR)tmp, text_len(tmp));
            SetAPen(g_win->RPort, 1);
        } else {
            draw_text_at((WORD)(g_user_x + 4), y, tmp);
        }
    }
    if (tab->user_count > rows) {
        draw_button_window(g_win, g_user_up_x, g_user_up_y,
                           g_user_scroll_w, g_user_scroll_h, "Up");
        draw_button_window(g_win, g_user_down_x, g_user_down_y,
                           g_user_scroll_w, g_user_scroll_h, "Down");
    }
}

static void draw_static_controls(void)
{
    gui_rp();
    clear_rect(0, (WORD)(g_status_y - g_char_h - 4),
               (WORD)(g_win->Width - 1), (WORD)(g_win->Height - 1));
    SetAPen(g_win->RPort, 1);
    Move(g_win->RPort, 4, (WORD)(g_status_y - g_char_h - 5));
    Draw(g_win->RPort, (WORD)(g_win->Width - 5), (WORD)(g_status_y - g_char_h - 5));
    draw_text_at(8, (WORD)(g_join_gadget.TopEdge + g_baseline + 5), "Channel");
    draw_text_at(8, (WORD)(g_msg_gadget.TopEdge + g_baseline + 5), "Text");
}

static void draw_field_box(WORD x, WORD y, WORD w, WORD h);
static void draw_main_buttons(void);

static void draw_main_buttons(void)
{
    gui_rp();
    draw_field_box((WORD)(g_join_gadget.LeftEdge - 2), (WORD)(g_join_gadget.TopEdge - 3),
                   (WORD)(g_join_gadget.Width + 4), (WORD)(g_join_gadget.Height + 5));
    draw_field_box((WORD)(g_msg_gadget.LeftEdge - 2), (WORD)(g_msg_gadget.TopEdge - 3),
                   (WORD)(g_msg_gadget.Width + 4), (WORD)(g_msg_gadget.Height + 5));
    draw_button_window(g_win, g_join_button.LeftEdge, g_join_button.TopEdge,
                       g_join_button.Width, g_join_button.Height, "Join");
    draw_button_window(g_win, g_list_button.LeftEdge, g_list_button.TopEdge,
                       g_list_button.Width, g_list_button.Height, "List");
    draw_button_window(g_win, g_send_gadget.LeftEdge, g_send_gadget.TopEdge,
                       g_send_gadget.Width, g_send_gadget.Height, "Send");
}

static int output_wrap_count(const char *line, int max_chars)
{
    int len;

    if (max_chars <= 0)
        return 0;
    len = text_len(line);
    if (len <= 0)
        return 1;
    return (len + max_chars - 1) / max_chars;
}

static void copy_output_wrap_segment(char *dst, int dst_size,
                                     const char *line, int max_chars, int segment)
{
    int start;
    int i;

    if (!dst || dst_size <= 0)
        return;
    if (!line)
        line = "";
    if (max_chars <= 0) {
        dst[0] = 0;
        return;
    }
    start = segment * max_chars;
    i = 0;
    while (line[start + i] && i < max_chars && i < dst_size - 1) {
        dst[i] = line[start + i];
        ++i;
    }
    dst[i] = 0;
}

static void draw_output(void)
{
    int visible_rows;
    int line_index;
    int row;
    int remaining_rows;
    int lines_seen;
    int total_wraps;
    int skipped_wraps;
    int wrap;
    struct MiniIrcTab *tab;
    char tmp[MINI_IRC_LINE_SIZE];
    int max_chars;

    gui_rp();
    clear_rect(g_term_x, (WORD)(g_win->BorderTop + 2),
               (WORD)(g_term_x + g_term_w - 1), g_list_bottom);
    draw_panel_box(g_term_x, (WORD)(g_win->BorderTop + 2), g_term_w,
                   (WORD)(g_list_bottom - g_win->BorderTop - 1),
                   (g_active_tab >= 0 && g_active_tab < g_tab_count) ? g_tabs[g_active_tab].name : "Output");
    if (g_active_tab < 0 || g_active_tab >= g_tab_count)
        return;
    tab = &g_tabs[g_active_tab];
    visible_rows = (g_term_h - g_char_h - 4) / g_char_h;
    if (visible_rows < 1)
        return;
    max_chars = (g_term_w - 8) / g_char_w;
    if (max_chars < 1)
        return;
    if (max_chars > MINI_IRC_LINE_SIZE - 1)
        max_chars = MINI_IRC_LINE_SIZE - 1;

    total_wraps = 0;
    for (row = 0; row < tab->line_count; ++row) {
        line_index = tab->next_line - tab->line_count + row;
        while (line_index < 0)
            line_index += MINI_IRC_TAB_LINES;
        total_wraps += output_wrap_count(tab->lines[line_index % MINI_IRC_TAB_LINES], max_chars);
    }
    skipped_wraps = total_wraps - visible_rows;
    if (skipped_wraps < 0)
        skipped_wraps = 0;

    lines_seen = 0;
    remaining_rows = 0;
    for (row = 0; row < tab->line_count && remaining_rows < visible_rows; ++row) {
        line_index = tab->next_line - tab->line_count + row;
        while (line_index < 0)
            line_index += MINI_IRC_TAB_LINES;
        line_index %= MINI_IRC_TAB_LINES;
        for (wrap = 0; wrap < output_wrap_count(tab->lines[line_index], max_chars); ++wrap) {
            if (lines_seen++ < skipped_wraps)
                continue;
            copy_output_wrap_segment(tmp, sizeof(tmp), tab->lines[line_index], max_chars, wrap);
            draw_text_at((WORD)(g_term_x + 4),
                         (WORD)(g_term_y + g_char_h + g_baseline + 2 + remaining_rows * g_char_h),
                         tmp);
            ++remaining_rows;
            if (remaining_rows >= visible_rows)
                break;
        }
    }
}

static void redraw_all(void)
{
    if (!g_win)
        return;
    layout_window();
    draw_channel_list();
    draw_user_list();
    draw_static_controls();
    RefreshGList(&g_join_gadget, g_win, 0, 4);
    draw_main_buttons();
    draw_output();
    status_text(g_gui.connected ? "Connected" : "Ready");
}

static int call_socket(struct Library *base, int domain, int type, int protocol)
{
    register int d0 __asm("d0") = domain;
    register int d1 __asm("d1") = type;
    register int d2 __asm("d2") = protocol;
    register struct Library *a6 __asm("a6") = base;

    __asm volatile ("jsr a6@(-30:W)"
        : "+r" (d0), "+r" (d1), "+r" (d2)
        : "r" (a6)
        : "a0", "a1", "cc", "memory");
    return d0;
}

static int call_close_socket(struct Library *base, int fd)
{
    register int d0 __asm("d0") = fd;
    register struct Library *a6 __asm("a6") = base;

    __asm volatile ("jsr a6@(-120:W)"
        : "+r" (d0)
        : "r" (a6)
        : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

static int call_connect(struct Library *base,
                        int fd,
                        const struct Amitcp13BsdSockAddr *addr,
                        int addrlen)
{
    register int d0 __asm("d0") = fd;
    register const struct Amitcp13BsdSockAddr *a0 __asm("a0") = addr;
    register int d1 __asm("d1") = addrlen;
    register struct Library *a6 __asm("a6") = base;

    __asm volatile ("jsr a6@(-54:W)"
        : "+r" (d0), "+r" (a0), "+r" (d1)
        : "r" (a6)
        : "a1", "cc", "memory");
    return d0;
}

static int call_send(struct Library *base, int fd, const void *buf, int len, int flags)
{
    register int d0 __asm("d0") = fd;
    register const void *a0 __asm("a0") = buf;
    register int d1 __asm("d1") = len;
    register int d2 __asm("d2") = flags;
    register struct Library *a6 __asm("a6") = base;

    __asm volatile ("jsr a6@(-66:W)"
        : "+r" (d0), "+r" (a0), "+r" (d1), "+r" (d2)
        : "r" (a6)
        : "a1", "cc", "memory");
    return d0;
}

static int call_recv(struct Library *base, int fd, void *buf, int len, int flags)
{
    register int d0 __asm("d0") = fd;
    register void *a0 __asm("a0") = buf;
    register int d1 __asm("d1") = len;
    register int d2 __asm("d2") = flags;
    register struct Library *a6 __asm("a6") = base;

    __asm volatile ("jsr a6@(-78:W)"
        : "+r" (d0), "+r" (a0), "+r" (d1), "+r" (d2)
        : "r" (a6)
        : "a1", "cc", "memory");
    return d0;
}

static int call_ioctl(struct Library *base, int fd, ULONG request, void *argp)
{
    register int d0 __asm("d0") = fd;
    register ULONG d1 __asm("d1") = request;
    register void *a0 __asm("a0") = argp;
    register struct Library *a6 __asm("a6") = base;

    __asm volatile ("jsr a6@(-114:W)"
        : "+r" (d0), "+r" (d1), "+r" (a0)
        : "r" (a6)
        : "a1", "cc", "memory");
    return d0;
}

static int call_waitselect(struct Library *base,
                           int nfds,
                           struct Amitcp13BsdFdSet *readfds,
                           struct Amitcp13BsdFdSet *writefds,
                           const struct Amitcp13BsdTimeVal *timeout)
{
    register int d0 __asm("d0") = nfds;
    register ULONG *d1 __asm("d1") = &g_wait_signals;
    register struct Amitcp13BsdFdSet *a0 __asm("a0") = readfds;
    register struct Amitcp13BsdFdSet *a1 __asm("a1") = writefds;
    register struct Amitcp13BsdFdSet *a2 __asm("a2") = 0;
    register const struct Amitcp13BsdTimeVal *a3 __asm("a3") = timeout;
    register struct Library *a6 __asm("a6") = base;

    __asm volatile ("jsr a6@(-126:W)"
        : "+r" (d0), "+r" (d1), "+r" (a0), "+r" (a1), "+r" (a2), "+r" (a3)
        : "r" (a6)
        : "cc", "memory");
    return d0;
}

static int call_errno(struct Library *base)
{
    register int d0 __asm("d0");
    register struct Library *a6 __asm("a6") = base;

    __asm volatile ("jsr a6@(-162:W)"
        : "=r" (d0)
        : "r" (a6)
        : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

static struct hostent *call_gethostbyname(struct Library *base, const char *name)
{
    register const char *a0 __asm("a0") = name;
    register struct Library *a6 __asm("a6") = base;
    register struct hostent *d0 __asm("d0");

    __asm volatile ("jsr a6@(-210:W)"
        : "=r" (d0), "+r" (a0)
        : "r" (a6)
        : "d1", "a1", "cc", "memory");
    return d0;
}

static int wait_for_socket_write(struct Library *base, int fd)
{
    int result;

    AMITCP13_BSD_FD_ZERO(&g_write_fds);
    AMITCP13_BSD_FD_SET(fd, &g_write_fds);
    g_timeout.tv_sec = MINI_IRC_SEND_WAIT_SECONDS;
    g_timeout.tv_usec = 0;
    g_wait_signals = 0;
    result = call_waitselect(base, fd + 1, 0, &g_write_fds, &g_timeout);
    if (result <= 0)
        return 0;
    return AMITCP13_BSD_FD_ISSET(fd, &g_write_fds) ? 1 : 0;
}

static int send_all(struct Library *base, int fd, const char *buf, int len)
{
    int total = 0;
    int sent;
    int err;

    while (total < len) {
        sent = call_send(base, fd, buf + total, len - total, 0);
        if (sent > 0) {
            total += sent;
            continue;
        }
        err = call_errno(base);
        if (err == AMITCP13_EWOULDBLOCK || err == AMITCP13_EAGAIN) {
            if (wait_for_socket_write(base, fd))
                continue;
            return 0;
        }
        return 0;
    }
    return 1;
}

static int irc_socket_send(void *ctx, const char *data, int len)
{
    struct MiniIrcSocketCtx *sock_ctx = (struct MiniIrcSocketCtx *)ctx;
    int result;

    if (!sock_ctx || !sock_ctx->base || sock_ctx->fd < 0) {
        debug_log("SEND_BADCTX", "missing socket context");
        return 0;
    }
    debug_log_num("SEND_LEN", len);
    result = send_all(sock_ctx->base, sock_ctx->fd, data, len);
    debug_log_num(result ? "SEND_OK" : "SEND_FAIL", len);
    return result;
}

static int resolve_server(struct Library *base, const char *server, ULONG *out_ip)
{
    struct hostent *he;

    if (parse_ipv4(server, out_ip))
        return 1;
    he = call_gethostbyname(base, server);
    if (!he || !he->h_addr_list || !he->h_addr_list[0])
        return 0;
    *out_ip = *(ULONG *)he->h_addr_list[0];
    return 1;
}

static int connect_socket(struct Library *base, int fd, ULONG ip, UWORD port)
{
    int result;

    g_addr.sin_len = sizeof(g_addr);
    g_addr.sin_family = AMITCP13_AF_INET;
    g_addr.sin_port = port;
    g_addr.sin_addr.s_addr = ip;
    result = call_connect(base,
                          fd,
                          (const struct Amitcp13BsdSockAddr *)&g_addr,
                          sizeof(g_addr));
    if (result < 0)
        return 0;
    g_one = 1;
    if (call_ioctl(base, fd, FIONBIO, &g_one) < 0)
        return 0;
    return 1;
}

static int send_registration(void)
{
    int pos;

    pos = 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, "NICK "))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, g_nick_buf))
        return 0;
    if (!mini_irc_session_send_line(&g_gui.session, g_send_buf))
        return 0;
    pos = 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, "USER "))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, g_nick_buf))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, " 0 * :MiniIRC AmigaOS 1.3"))
        return 0;
    return mini_irc_session_send_line(&g_gui.session, g_send_buf);
}

static void reset_channel_tabs(void)
{
    g_tab_count = 1;
    g_active_tab = 0;
    g_tabs[0].user_count = 0;
    g_tabs[0].names_receiving = 0;
    g_leave_w = 0;
    g_leave_h = 0;
}


static int wait_quit_remote_close(void)
{
    int i;
    int result;
    int got;
    int err;

    if (!g_gui.socket_base || g_gui.fd < 0)
        return 0;
    for (i = 0; i < MINI_IRC_QUIT_WAIT_TICKS; ++i) {
        AMITCP13_BSD_FD_ZERO(&g_read_fds);
        AMITCP13_BSD_FD_SET(g_gui.fd, &g_read_fds);
        AMITCP13_BSD_FD_ZERO(&g_write_fds);
        AMITCP13_BSD_FD_SET(g_gui.fd, &g_write_fds);
        g_timeout.tv_sec = 0;
        g_timeout.tv_usec = 100000;
        g_wait_signals = 0;
        result = call_waitselect(g_gui.socket_base,
                                 g_gui.fd + 1,
                                 &g_read_fds,
                                 &g_write_fds,
                                 &g_timeout);
        if (result <= 0)
            continue;
        if (!AMITCP13_BSD_FD_ISSET(g_gui.fd, &g_read_fds))
            continue;
        got = call_recv(g_gui.socket_base, g_gui.fd, g_recv_buf, sizeof(g_recv_buf), 0);
        if (got > 0) {
            process_rx_bytes((const char *)g_recv_buf, got);
            continue;
        }
        if (got == 0)
            return 1;
        err = call_errno(g_gui.socket_base);
        if (err == AMITCP13_EWOULDBLOCK || err == AMITCP13_EAGAIN)
            continue;
        return 0;
    }
    return 0;
}

static void disconnect_irc(const char *reason)
{
    debug_log("DISCONNECT", reason ? reason : "Disconnected");
    if (g_gui.connected) {
        status_text("Quitting...");
        mini_irc_session_send_line(&g_gui.session, MINI_IRC_QUIT_MESSAGE);
        wait_quit_remote_close();
    }
    if (g_gui.fd >= 0 && g_gui.socket_base)
        call_close_socket(g_gui.socket_base, g_gui.fd);
    g_gui.fd = -1;
    g_gui.connected = 0;
    g_gui.sock_ctx.fd = -1;
    reset_channel_tabs();
    tab_append(0, reason ? reason : "Disconnected");
    draw_channel_list();
    draw_user_list();
    draw_output();
    status_text(reason ? reason : "Disconnected");
}

static int connect_irc(void)
{
    ULONG ip;
    UWORD port;

    debug_log("CONNECT_START", g_host_buf);
    trim_text(g_host_buf);
    trim_text(g_port_buf);
    trim_text(g_nick_buf);
    if (!g_host_buf[0]) {
        status_text("Enter server");
        return 0;
    }
    if (!g_nick_buf[0])
        copy_text(g_nick_buf, sizeof(g_nick_buf), "amiga13");
    if (!g_port_buf[0])
        copy_text(g_port_buf, sizeof(g_port_buf), "6667");
    if (!parse_port(g_port_buf, &port)) {
        status_text("Invalid port");
        return 0;
    }
    if (!g_gui.socket_base) {
        g_gui.socket_base = OpenLibrary((STRPTR)"bsdsocket.library", 0);
        if (!g_gui.socket_base) {
            status_text("bsdsocket.library missing");
            return 0;
        }
    }
    disconnect_irc("Disconnected");
    status_text("Resolving...");
    if (!resolve_server(g_gui.socket_base, g_host_buf, &ip)) {
        status_text("DNS failed");
        return 0;
    }
    g_gui.fd = call_socket(g_gui.socket_base,
                           AMITCP13_AF_INET,
                           AMITCP13_SOCK_STREAM,
                           AMITCP13_IPPROTO_TCP);
    if (g_gui.fd < 0) {
        status_text("Socket failed");
        return 0;
    }
    status_text("Connecting...");
    if (!connect_socket(g_gui.socket_base, g_gui.fd, ip, port)) {
        call_close_socket(g_gui.socket_base, g_gui.fd);
        g_gui.fd = -1;
        status_text("Connect failed");
        return 0;
    }

    mini_irc_session_init(&g_gui.session, irc_socket_send, &g_gui.sock_ctx);
#if MINI_IRC_FILE_DEBUG
    mini_irc_session_set_debug(&g_gui.session, session_debug_cb, 0);
#endif
    mini_irc_session_set_nick(&g_gui.session, g_nick_buf);
    g_gui.sock_ctx.base = g_gui.socket_base;
    g_gui.sock_ctx.fd = g_gui.fd;
    g_gui.sock_ctx.session = &g_gui.session;
    g_gui.connected = 1;
    g_rx_len = 0;
    if (!send_registration()) {
        disconnect_irc("Registration failed");
        return 0;
    }
    tab_append(0, "Connected");
    draw_output();
    status_text("Connected");
    return 1;
}

static int parse_target_token(const char *text, char *out, int out_size)
{
    int i;

    if (!text || !out || out_size <= 0)
        return 0;
    text = skip_spaces(text);
    if (*text == ':')
        ++text;
    i = 0;
    while (text[i] && text[i] != ' ' && i < out_size - 1) {
        out[i] = text[i];
        ++i;
    }
    out[i] = 0;
    return i > 0;
}


static void channel_list_reset(void)
{
    g_channel_list_count = 0;
    g_channel_list_complete = 0;
    g_channel_list_top = 0;
    g_channel_list_selected = -1;
    g_last_channel_list_click[0] = 0;
}

static void channel_list_add(const char *name, const char *users, const char *topic)
{
    struct MiniIrcChannelListEntry *entry;

    if (!name || !name[0] || g_channel_list_count >= MINI_IRC_LIST_MAX_CHANNELS)
        return;
    entry = &g_channel_list[g_channel_list_count++];
    copy_text(entry->name, sizeof(entry->name), name);
    copy_text(entry->users, sizeof(entry->users), users ? users : "");
    copy_text(entry->topic, sizeof(entry->topic), topic ? topic : "");
}

static void parse_list_reply(const char *target)
{
    const char *p;
    const char *topic;
    char chan[MINI_IRC_CHAN_SIZE];
    char users[8];
    int i;

    p = skip_spaces(target);
    while (*p && *p != ' ')
        ++p;
    p = skip_spaces(p);
    if (!parse_target_token(p, chan, sizeof(chan)))
        return;
    while (*p && *p != ' ')
        ++p;
    p = skip_spaces(p);
    i = 0;
    while (p[i] && p[i] != ' ' && i < (int)sizeof(users) - 1) {
        users[i] = p[i];
        ++i;
    }
    users[i] = 0;
    topic = p + i;
    topic = skip_spaces(topic);
    if (*topic == ':')
        ++topic;
    channel_list_add(chan, users, topic);
}

static void draw_channel_list_window(void)
{
    struct Window *win = g_channel_list_window;
    int row;
    int idx;
    WORD y;
    char line[MINI_IRC_CHAN_SIZE + 86];
    int pos;

    if (!win)
        return;
    SetAPen(win->RPort, 0);
    RectFill(win->RPort, 0, 0, win->Width - 1, win->Height - 1);
    SetAPen(win->RPort, 1);
    SetBPen(win->RPort, 0);
    SetDrMd(win->RPort, JAM2);
    if (g_gui_font)
        SetFont(win->RPort, g_gui_font);
    Move(win->RPort, MINI_IRC_LIST_X, 18);
    if (!g_channel_list_complete) {
        Text(win->RPort, (STRPTR)"Retrieving channel list... please wait", 38);
        draw_button_window(win, 176, 164, 70, 14, "Cancel");
        return;
    }
    Text(win->RPort, (STRPTR)"Channels", 8);
    draw_field_box_window(win, MINI_IRC_LIST_X, MINI_IRC_LIST_Y,
                          MINI_IRC_LIST_W,
                          (WORD)(MINI_IRC_LIST_VISIBLE * MINI_IRC_LIST_ROW_H + 4));
    draw_button_window(win, 304, 164, 48, 14, "Up");
    draw_button_window(win, 360, 164, 58, 14, "Down");
    draw_button_window(win, 176, 164, 70, 14, "Cancel");

    for (row = 0; row < MINI_IRC_LIST_VISIBLE; ++row) {
        idx = g_channel_list_top + row;
        if (idx >= g_channel_list_count)
            break;
        y = (WORD)(MINI_IRC_LIST_Y + 10 + row * MINI_IRC_LIST_ROW_H);
        if (idx == g_channel_list_selected) {
            SetAPen(win->RPort, 3);
            RectFill(win->RPort, (WORD)(MINI_IRC_LIST_X + 1), (WORD)(y - 8),
                     (WORD)(MINI_IRC_LIST_X + MINI_IRC_LIST_W - 1), (WORD)(y + 1));
            SetAPen(win->RPort, 0);
        } else {
            SetAPen(win->RPort, 1);
        }
        pos = 0;
        append_text(line, &pos, sizeof(line), g_channel_list[idx].name);
        append_text(line, &pos, sizeof(line), " (");
        append_text(line, &pos, sizeof(line), g_channel_list[idx].users);
        append_text(line, &pos, sizeof(line), ") ");
        append_text(line, &pos, sizeof(line), g_channel_list[idx].topic);
        Move(win->RPort, (WORD)(MINI_IRC_LIST_X + 4), y);
        Text(win->RPort, (STRPTR)line, text_len(line));
    }
    SetAPen(win->RPort, 1);
}

static void join_list_channel(const char *channel)
{
    if (!channel || !channel[0])
        return;
    if (!g_gui.connected) {
        status_text("Not connected");
        return;
    }
    copy_text(g_join_buf, sizeof(g_join_buf), channel);
    g_join_si.BufferPos = text_len(g_join_buf);
    g_join_si.NumChars = g_join_si.BufferPos;
    mini_irc_session_join(&g_gui.session, g_join_buf);
    RefreshGList(&g_join_gadget, g_win, 0, 1);
    status_text("JOIN sent");
}

static void handle_channel_list_click(WORD mx, WORD my, ULONG seconds, ULONG micros, int *done)
{
    int row;
    int idx;
    char chan[MINI_IRC_CHAN_SIZE];

    if (mx < MINI_IRC_LIST_X || mx >= MINI_IRC_LIST_X + MINI_IRC_LIST_W ||
        my < MINI_IRC_LIST_Y ||
        my >= MINI_IRC_LIST_Y + MINI_IRC_LIST_VISIBLE * MINI_IRC_LIST_ROW_H)
        return;
    row = (my - MINI_IRC_LIST_Y) / MINI_IRC_LIST_ROW_H;
    idx = g_channel_list_top + row;
    if (idx < 0 || idx >= g_channel_list_count)
        return;
    g_channel_list_selected = idx;
    copy_text(chan, sizeof(chan), g_channel_list[idx].name);
    if (text_equal_ci(chan, g_last_channel_list_click) &&
        DoubleClick(g_last_channel_list_click_seconds, g_last_channel_list_click_micros,
                    seconds, micros)) {
        join_list_channel(chan);
        if (done)
            *done = 1;
        return;
    }
    copy_text(g_last_channel_list_click, sizeof(g_last_channel_list_click), chan);
    g_last_channel_list_click_seconds = seconds;
    g_last_channel_list_click_micros = micros;
    draw_channel_list_window();
}

static void open_channel_list_window(void)
{
    struct NewWindow nw;
    struct Window *win;
    struct IntuiMessage *msg;
    ULONG cls;
    UWORD code;
    struct Gadget *gad;
    WORD mx;
    WORD my;
    int done = 0;
    static struct Gadget cancel_gad;
    static struct Gadget up_gad;
    static struct Gadget down_gad;

    if (!g_gui.connected) {
        status_text("Not connected");
        return;
    }
    channel_list_reset();
    if (!mini_irc_session_send_line(&g_gui.session, "LIST")) {
        status_text("LIST failed");
        return;
    }

    memset(&cancel_gad, 0, sizeof(cancel_gad));
    memset(&up_gad, 0, sizeof(up_gad));
    memset(&down_gad, 0, sizeof(down_gad));
    cancel_gad.NextGadget = &up_gad;
    cancel_gad.LeftEdge = 176;
    cancel_gad.TopEdge = 164;
    cancel_gad.Width = 70;
    cancel_gad.Height = 14;
    cancel_gad.Flags = GFLG_GADGHCOMP;
    cancel_gad.Activation = GACT_RELVERIFY;
    cancel_gad.GadgetType = GTYP_BOOLGADGET;
    cancel_gad.GadgetID = 301;
    up_gad.NextGadget = &down_gad;
    up_gad.LeftEdge = 304;
    up_gad.TopEdge = 164;
    up_gad.Width = 48;
    up_gad.Height = 14;
    up_gad.Flags = GFLG_GADGHCOMP;
    up_gad.Activation = GACT_RELVERIFY;
    up_gad.GadgetType = GTYP_BOOLGADGET;
    up_gad.GadgetID = 302;
    down_gad.LeftEdge = 360;
    down_gad.TopEdge = 164;
    down_gad.Width = 58;
    down_gad.Height = 14;
    down_gad.Flags = GFLG_GADGHCOMP;
    down_gad.Activation = GACT_RELVERIFY;
    down_gad.GadgetType = GTYP_BOOLGADGET;
    down_gad.GadgetID = 303;

    memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = 70;
    nw.TopEdge = 30;
    nw.Width = MINI_IRC_LIST_WIN_W;
    nw.Height = MINI_IRC_LIST_WIN_H;
    nw.DetailPen = 1;
    nw.BlockPen = 0;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_MOUSEBUTTONS | IDCMP_REFRESHWINDOW;
    nw.Flags = WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_SMART_REFRESH | WFLG_ACTIVATE;
    nw.FirstGadget = &cancel_gad;
    nw.Title = (STRPTR)"MiniIRC Channel List";
    nw.Screen = g_screen;
    nw.Type = CUSTOMSCREEN;
    win = OpenWindow(&nw);
    if (!win) {
        status_text("List window failed");
        return;
    }
    g_channel_list_window = win;
    if (g_gui_font)
        SetFont(win->RPort, g_gui_font);
    draw_channel_list_window();
    status_text("LIST requested");

    while (!done) {
        poll_socket();
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) {
            cls = msg->Class;
            code = msg->Code;
            gad = (struct Gadget *)msg->IAddress;
            mx = msg->MouseX;
            my = msg->MouseY;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) {
                done = 1;
            } else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(win);
                draw_channel_list_window();
                EndRefresh(win, TRUE);
            } else if (cls == IDCMP_GADGETUP && gad) {
                if (gad->GadgetID == 301) {
                    done = 1;
                } else if (gad->GadgetID == 302) {
                    if (g_channel_list_top > 0)
                        --g_channel_list_top;
                    draw_channel_list_window();
                } else if (gad->GadgetID == 303) {
                    if (g_channel_list_top + MINI_IRC_LIST_VISIBLE < g_channel_list_count)
                        ++g_channel_list_top;
                    draw_channel_list_window();
                }
            } else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                handle_channel_list_click(mx, my, msg->Seconds, msg->Micros, &done);
            }
        }
        Delay(1);
    }
    g_channel_list_window = 0;
    CloseWindow(win);
    redraw_all();
}

static void parse_names_reply(const char *target)
{
    const char *p;
    const char *names;
    char chan[MINI_IRC_CHAN_SIZE];
    char nick[MINI_IRC_NICK_SIZE];
    int idx;
    int i;

    p = skip_spaces(target);
    while (*p && *p != ' ')
        ++p;
    p = skip_spaces(p);
    while (*p && *p != ' ')
        ++p;
    p = skip_spaces(p);
    if (!parse_target_token(p, chan, sizeof(chan)))
        return;
    idx = tab_add(chan);
    if (idx < 0)
        return;
    names = p;
    while (*names && *names != ':')
        ++names;
    if (*names == ':')
        ++names;
    if (!g_tabs[idx].names_receiving) {
        tab_users_clear(idx);
        g_tabs[idx].names_receiving = 1;
    }
    while (*names) {
        names = skip_spaces(names);
        if (!*names)
            break;
        i = 0;
        while (names[i] && names[i] != ' ' && i < (int)sizeof(nick) - 1) {
            nick[i] = names[i];
            ++i;
        }
        nick[i] = 0;
        tab_user_add(idx, nick);
        names += i;
    }
}

static void finish_names_reply(const char *target)
{
    const char *p;
    char chan[MINI_IRC_CHAN_SIZE];
    int idx;

    p = skip_spaces(target);
    while (*p && *p != ' ')
        ++p;
    if (!parse_target_token(p, chan, sizeof(chan)))
        return;
    idx = tab_find(chan);
    if (idx >= 0) {
        g_tabs[idx].names_receiving = 0;
        request_whois_for_tab(idx);
    }
}

static void parse_whois_idle_reply(const char *target)
{
    const char *p;
    char nick[MINI_IRC_NICK_SIZE];
    ULONG idle;

    p = skip_spaces(target);
    while (*p && *p != ' ')
        ++p;
    p = skip_spaces(p);
    if (!parse_target_token(p, nick, sizeof(nick)))
        return;
    while (*p && *p != ' ')
        ++p;
    if (!parse_ulong_token(&p, &idle))
        return;
    tab_user_set_idle(nick, idle);
}

static void route_line_to_tab(const char *line)
{
    const char *p = line;
    const char *cmd;
    const char *target;
    const char *payload;
    char nick[MINI_IRC_NICK_SIZE];
    char chan[MINI_IRC_CHAN_SIZE];
    char out[MINI_IRC_LINE_SIZE];
    int i;
    int pos;
    int idx;

    mini_irc_session_handle_line(&g_gui.session, line);

    if (*p == ':') {
        ++p;
        i = 0;
        while (*p && *p != '!' && *p != ' ' && i < (int)sizeof(nick) - 1)
            nick[i++] = *p++;
        nick[i] = 0;
        while (*p && *p != ' ')
            ++p;
        p = skip_spaces(p);
    } else {
        copy_text(nick, sizeof(nick), "server");
    }

    cmd = p;
    while (*p && *p != ' ')
        ++p;
    if (*p)
        ++p;
    target = skip_spaces(p);

    if (cmd[0] == '3' && cmd[1] == '1' && cmd[2] == '7') {
        parse_whois_idle_reply(target);
        return;
    }

    if ((cmd[0] == '3' && cmd[1] == '0' && cmd[2] == '1') ||
        (cmd[0] == '3' && cmd[1] == '1' && cmd[2] == '1') ||
        (cmd[0] == '3' && cmd[1] == '1' && cmd[2] == '2') ||
        (cmd[0] == '3' && cmd[1] == '1' && cmd[2] == '3') ||
        (cmd[0] == '3' && cmd[1] == '1' && cmd[2] == '8') ||
        (cmd[0] == '3' && cmd[1] == '1' && cmd[2] == '9') ||
        (cmd[0] == '3' && cmd[1] == '3' && cmd[2] == '0') ||
        (cmd[0] == '6' && cmd[1] == '7' && cmd[2] == '1')) {
        return;
    }

    if (cmd[0] == '3' && cmd[1] == '2' && cmd[2] == '2') {
        parse_list_reply(target);
        return;
    }

    if (cmd[0] == '3' && cmd[1] == '2' && cmd[2] == '3') {
        g_channel_list_complete = 1;
        draw_channel_list_window();
        status_text("LIST complete");
        return;
    }

    if (cmd[0] == '3' && cmd[1] == '5' && cmd[2] == '3') {
        parse_names_reply(target);
        draw_user_list();
        return;
    }

    if (cmd[0] == '3' && cmd[1] == '6' && cmd[2] == '6') {
        finish_names_reply(target);
        draw_user_list();
        return;
    }

    if (cmd[0] == 'P' && cmd[1] == 'R' && cmd[2] == 'I' && cmd[3] == 'V' &&
        cmd[4] == 'M' && cmd[5] == 'S' && cmd[6] == 'G') {
        i = 0;
        while (target[i] && target[i] != ' ' && i < (int)sizeof(chan) - 1) {
            chan[i] = target[i];
            ++i;
        }
        chan[i] = 0;
        payload = target + i;
        payload = skip_spaces(payload);
        if (*payload == ':')
            ++payload;
        pos = 0;
        append_chat_prefix(out, &pos, sizeof(out), nick);
        append_text(out, &pos, sizeof(out), payload);
        if (chan[0] == '#') {
            idx = tab_find(chan);
            if (idx < 0)
                idx = tab_add(chan);
        } else if (text_equal_ci(chan, g_nick_buf)) {
            idx = tab_find(nick);
            if (idx < 0) {
                idx = tab_add(nick);
                if (idx >= 0) {
                    tab_user_add(idx, g_nick_buf);
                    tab_user_add(idx, nick);
                    g_active_tab = idx;
                    draw_channel_list();
                    draw_user_list();
                }
            }
        } else {
            idx = 0;
        }
        if (idx < 0)
            idx = 0;
        tab_append(idx, out);
        if (idx == g_active_tab)
            draw_output();
        else {
            tab_mark_unread(idx);
            draw_channel_list();
        }
        return;
    }

    if (cmd[0] == 'J' && cmd[1] == 'O' && cmd[2] == 'I' && cmd[3] == 'N') {
        copy_text(chan, sizeof(chan), target);
        if (chan[0] == ':')
            memmove(chan, chan + 1, text_len(chan));
        if (chan[0] == '#') {
            idx = tab_add(chan);
            if (idx >= 0)
                tab_user_add(idx, nick);
            if (text_equal_ci(nick, g_nick_buf) && idx >= 0)
                g_active_tab = idx;
            draw_channel_list();
            draw_user_list();
        }
    }

    if (cmd[0] == 'P' && cmd[1] == 'A' && cmd[2] == 'R' && cmd[3] == 'T') {
        if (parse_target_token(target, chan, sizeof(chan))) {
            idx = tab_find(chan);
            if (idx >= 0)
                tab_user_remove(idx, nick);
            draw_user_list();
        }
    } else if (cmd[0] == 'Q' && cmd[1] == 'U' && cmd[2] == 'I' && cmd[3] == 'T') {
        for (idx = 0; idx < g_tab_count; ++idx)
            tab_user_remove(idx, nick);
        draw_user_list();
    } else if (cmd[0] == 'N' && cmd[1] == 'I' && cmd[2] == 'C' && cmd[3] == 'K') {
        copy_text(chan, sizeof(chan), target);
        if (chan[0] == ':')
            memmove(chan, chan + 1, text_len(chan));
        trim_text(chan);
        if (chan[0]) {
            for (idx = 0; idx < g_tab_count; ++idx)
                tab_user_rename(idx, nick, chan);
            if (text_equal_ci(nick, g_nick_buf)) {
                copy_text(g_nick_buf, sizeof(g_nick_buf), chan);
                mini_irc_session_set_nick(&g_gui.session, g_nick_buf);
            }
            draw_user_list();
        }
    }

    tab_append(0, line);
    if (g_active_tab == 0)
        draw_output();
}

static void process_rx_bytes(const char *data, int len)
{
    int i;
    char c;

    for (i = 0; i < len; ++i) {
        c = data[i];
        if (c == '\r')
            continue;
        if (c == '\n') {
            g_rx_line[g_rx_len] = 0;
            if (g_rx_len > 0) {
                debug_log("LINE_COMPLETE", g_rx_line);
                route_line_to_tab(g_rx_line);
            }
            g_rx_len = 0;
            continue;
        }
        if (g_rx_len < (int)sizeof(g_rx_line) - 1) {
            g_rx_line[g_rx_len++] = c;
        } else {
            g_rx_len = 0;
        }
    }
}

static void poll_socket(void)
{
    int result;
    int got;
    int err;
    int chunks;

    if (!g_gui.connected || g_gui.fd < 0)
        return;
    AMITCP13_BSD_FD_ZERO(&g_read_fds);
    AMITCP13_BSD_FD_SET(g_gui.fd, &g_read_fds);
    g_timeout.tv_sec = 0;
    g_timeout.tv_usec = 0;
    g_wait_signals = 0;
    result = call_waitselect(g_gui.socket_base, g_gui.fd + 1, &g_read_fds, 0, &g_timeout);
    if (result < 0)
        debug_log_num("WAITSELECT_ERR", call_errno(g_gui.socket_base));
    if (result <= 0 || !AMITCP13_BSD_FD_ISSET(g_gui.fd, &g_read_fds))
        return;
    debug_log_num("WAITSELECT_READY", result);
    chunks = 0;
    while (chunks < MINI_IRC_RX_CHUNKS_PER_TICK) {
        got = call_recv(g_gui.socket_base, g_gui.fd, g_recv_buf, sizeof(g_recv_buf), 0);
        if (got > 0) {
            debug_log_num("RECV", got);
            process_rx_bytes((const char *)g_recv_buf, got);
            ++chunks;
            continue;
        }
        if (got == 0) {
            debug_log("RECV_EOF", "remote closed");
            disconnect_irc("Connection closed");
            return;
        }
        err = call_errno(g_gui.socket_base);
        if (err == AMITCP13_EWOULDBLOCK || err == AMITCP13_EAGAIN)
            return;
        debug_log_num("RECV_ERR", err);
        disconnect_irc("Socket error");
        return;
    }
}

static void join_channel(void)
{
    char chan[MINI_IRC_CHAN_SIZE];
    int idx;

    copy_text(chan, sizeof(chan), g_join_buf);
    trim_text(chan);
    if (!chan[0]) {
        status_text("Enter channel");
        return;
    }
    if (chan[0] != '#') {
        memmove(chan + 1, chan, text_len(chan) + 1);
        chan[0] = '#';
    }
    if (!g_gui.connected) {
        status_text("Not connected");
        return;
    }
    idx = tab_add(chan);
    if (idx >= 0)
        g_active_tab = idx;
    mini_irc_session_join(&g_gui.session, chan);
    g_join_buf[0] = 0;
    g_join_si.BufferPos = 0;
    g_join_si.NumChars = 0;
    RefreshGList(&g_join_gadget, g_win, 0, 1);
    draw_channel_list();
    draw_user_list();
    draw_output();
    status_text("JOIN sent");
}

static void clear_message_input(void)
{
    g_msg_buf[0] = 0;
    g_msg_si.BufferPos = 0;
    g_msg_si.NumChars = 0;
    RefreshGList(&g_msg_gadget, g_win, 0, 1);
}

static int is_nick_command(const char *text)
{
    if (!text || text[0] != '/')
        return 0;
    if (upper_ascii(text[1]) != 'N' || upper_ascii(text[2]) != 'I' ||
        upper_ascii(text[3]) != 'C' || upper_ascii(text[4]) != 'K')
        return 0;
    return text[5] == 0 || text[5] == ' ' || text[5] == '\t';
}

static void send_nick_command(const char *line)
{
    const char *p;
    char nick[MINI_IRC_NICK_SIZE];
    int i;
    int pos;

    p = skip_spaces(line + 5);
    i = 0;
    while (p[i] && p[i] != ' ' && p[i] != '\t' &&
           i < (int)sizeof(nick) - 1) {
        nick[i] = p[i];
        ++i;
    }
    nick[i] = 0;
    if (!nick[0]) {
        status_text("Usage: /nick name");
        return;
    }
    pos = 0;
    g_send_buf[0] = 0;
    if (!append_text(g_send_buf, &pos, sizeof(g_send_buf), "NICK ") ||
        !append_text(g_send_buf, &pos, sizeof(g_send_buf), nick) ||
        !mini_irc_session_send_line(&g_gui.session, g_send_buf)) {
        status_text("NICK failed");
        return;
    }
    copy_text(g_nick_buf, sizeof(g_nick_buf), nick);
    mini_irc_session_set_nick(&g_gui.session, g_nick_buf);
    status_text("NICK sent");
    tab_append(0, "NICK change requested");
    if (g_active_tab == 0)
        draw_output();
}

static void send_message(void)
{
    const char *target;
    char local[MINI_IRC_MSG_SIZE];
    int pos = 0;

    if (!g_gui.connected) {
        status_text("Not connected");
        return;
    }
    copy_text(local, sizeof(local), g_msg_buf);
    trim_text(local);
    if (!local[0])
        return;
    if (is_nick_command(local)) {
        send_nick_command(local);
        clear_message_input();
        return;
    }
    if (g_active_tab <= 0 || g_active_tab >= g_tab_count) {
        status_text("Select channel tab");
        return;
    }
    target = g_tabs[g_active_tab].name;
    if (!mini_irc_session_privmsg(&g_gui.session, target, local)) {
        status_text("Send failed");
        return;
    }
    append_chat_prefix(g_send_buf, &pos, sizeof(g_send_buf), g_nick_buf);
    append_text(g_send_buf, &pos, sizeof(g_send_buf), local);
    tab_append(g_active_tab, g_send_buf);
    clear_message_input();
    draw_output();
}

static void draw_button_window(struct Window *win,
                               WORD x,
                               WORD y,
                               WORD w,
                               WORD h,
                               const char *label)
{
    WORD oldx;

    SetAPen(win->RPort, 1);
    SetBPen(win->RPort, 0);
    SetDrMd(win->RPort, JAM2);
    Move(win->RPort, x, y);
    Draw(win->RPort, x + w, y);
    Draw(win->RPort, x + w, y + h);
    Draw(win->RPort, x, y + h);
    Draw(win->RPort, x, y);
    oldx = (WORD)(x + 6);
    Move(win->RPort, oldx, y + g_baseline + 5);
    Text(win->RPort, (STRPTR)label, text_len(label));
}

static void draw_field_box_window(struct Window *win, WORD x, WORD y, WORD w, WORD h)
{
    SetAPen(win->RPort, 1);
    SetBPen(win->RPort, 0);
    SetDrMd(win->RPort, JAM1);
    Move(win->RPort, x, y);
    Draw(win->RPort, (WORD)(x + w), y);
    Draw(win->RPort, (WORD)(x + w), (WORD)(y + h));
    Draw(win->RPort, x, (WORD)(y + h));
    Draw(win->RPort, x, y);
}

static void draw_field_box(WORD x, WORD y, WORD w, WORD h)
{
    SetAPen(g_win->RPort, 1);
    SetBPen(g_win->RPort, 0);
    SetDrMd(g_win->RPort, JAM1);
    Move(g_win->RPort, x, y);
    Draw(g_win->RPort, (WORD)(x + w), y);
    Draw(g_win->RPort, (WORD)(x + w), (WORD)(y + h));
    Draw(g_win->RPort, x, (WORD)(y + h));
    Draw(g_win->RPort, x, y);
}

static void load_addrbook(void)
{
    BPTR fh;
    char ch;
    char line[MINI_IRC_HOST_SIZE + MINI_IRC_NICK_SIZE + 16];
    LONG got;
    int len = 0;
    int field;
    char *p;

    g_addr_count = 0;
    fh = Open((STRPTR)MINI_IRC_ADDRBOOK_PATH, MODE_OLDFILE);
    if (!fh)
        return;
    for (;;) {
        got = Read(fh, &ch, 1);
        if (got <= 0)
            break;
        if (ch == '\r')
            continue;
        if (ch != '\n' && len < (int)sizeof(line) - 1) {
            line[len++] = ch;
            continue;
        }
        line[len] = 0;
        len = 0;
        trim_text(line);
        if (!line[0] || line[0] == '#')
            continue;
        if (g_addr_count >= MINI_IRC_MAX_ADDRS)
            continue;
        field = 0;
        p = line;
        while (p) {
            char *sep = strchr(p, '|');
            if (sep)
                *sep = 0;
            if (field == 0)
                copy_text(g_addrs[g_addr_count].host, sizeof(g_addrs[g_addr_count].host), p);
            else if (field == 1)
                copy_text(g_addrs[g_addr_count].port, sizeof(g_addrs[g_addr_count].port), p);
            else if (field == 2)
                copy_text(g_addrs[g_addr_count].nick, sizeof(g_addrs[g_addr_count].nick), p);
            ++field;
            p = sep ? sep + 1 : 0;
        }
        if (g_addrs[g_addr_count].host[0])
            ++g_addr_count;
    }
    if (len > 0 && g_addr_count < MINI_IRC_MAX_ADDRS) {
        line[len] = 0;
        copy_text(g_addrs[g_addr_count].host, sizeof(g_addrs[g_addr_count].host), line);
        copy_text(g_addrs[g_addr_count].port, sizeof(g_addrs[g_addr_count].port), "6667");
        copy_text(g_addrs[g_addr_count].nick, sizeof(g_addrs[g_addr_count].nick), "amiga13");
        ++g_addr_count;
    }
    Close(fh);
}

static void save_addrbook(void)
{
    BPTR fh;
    int i;

    fh = Open((STRPTR)MINI_IRC_ADDRBOOK_PATH, MODE_NEWFILE);
    if (!fh)
        return;
    for (i = 0; i < g_addr_count; ++i) {
        Write(fh, g_addrs[i].host, text_len(g_addrs[i].host));
        Write(fh, "|", 1);
        Write(fh, g_addrs[i].port, text_len(g_addrs[i].port));
        Write(fh, "|", 1);
        Write(fh, g_addrs[i].nick, text_len(g_addrs[i].nick));
        Write(fh, "\n", 1);
    }
    Close(fh);
}


static int background_color_index_by_name(const char *name)
{
    int i;

    for (i = 0; i < MINI_IRC_BG_COLOR_MAX; ++i) {
        if (text_equal_ci(name, g_bg_colors[i].name))
            return i;
    }
    return -1;
}

static int text_color_index_by_name(const char *name)
{
    int i;

    for (i = 0; i < MINI_IRC_TEXT_COLOR_MAX; ++i) {
        if (text_equal_ci(name, g_text_colors[i].name))
            return i;
    }
    return -1;
}

static void load_config(void)
{
    BPTR fh;
    char ch;
    char line[96];
    LONG got;
    int len = 0;
    char *eq;
    int idx;

    fh = Open((STRPTR)MINI_IRC_CONFIG_PATH, MODE_OLDFILE);
    if (!fh)
        return;
    for (;;) {
        got = Read(fh, &ch, 1);
        if (got <= 0)
            break;
        if (ch == '\r')
            continue;
        if (ch != '\n' && len < (int)sizeof(line) - 1) {
            line[len++] = ch;
            continue;
        }
        line[len] = 0;
        len = 0;
        trim_text(line);
        if (!line[0] || line[0] == '#')
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        trim_text(line);
        trim_text(eq + 1);
        if (text_equal_ci(line, "background")) {
            idx = background_color_index_by_name(eq + 1);
            if (idx >= 0)
                g_bg_color_index = (UBYTE)idx;
        } else if (text_equal_ci(line, "text_color")) {
            idx = text_color_index_by_name(eq + 1);
            if (idx >= 0)
                g_text_color_index = (UBYTE)idx;
        }
    }
    if (len > 0) {
        line[len] = 0;
        trim_text(line);
        eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            trim_text(line);
            trim_text(eq + 1);
            if (text_equal_ci(line, "background")) {
                idx = background_color_index_by_name(eq + 1);
                if (idx >= 0)
                    g_bg_color_index = (UBYTE)idx;
            } else if (text_equal_ci(line, "text_color")) {
                idx = text_color_index_by_name(eq + 1);
                if (idx >= 0)
                    g_text_color_index = (UBYTE)idx;
            }
        }
    }
    Close(fh);
}

static void save_config(void)
{
    BPTR fh;

    if (g_bg_color_index >= MINI_IRC_BG_COLOR_MAX)
        g_bg_color_index = 0;
    if (g_text_color_index >= MINI_IRC_TEXT_COLOR_MAX)
        g_text_color_index = 0;
    fh = Open((STRPTR)MINI_IRC_CONFIG_PATH, MODE_NEWFILE);
    if (!fh) {
        status_text("Could not save settings");
        return;
    }
    Write(fh, "# MiniIRC configuration\n", 24);
    Write(fh, "background=", 11);
    Write(fh, g_bg_colors[g_bg_color_index].name, text_len(g_bg_colors[g_bg_color_index].name));
    Write(fh, "\n", 1);
    Write(fh, "text_color=", 11);
    Write(fh, g_text_colors[g_text_color_index].name, text_len(g_text_colors[g_text_color_index].name));
    Write(fh, "\n", 1);
    Close(fh);
}

static void add_current_to_addrbook(void)
{
    int i;
    int slot = -1;

    debug_log("ADDR_SAVE", g_host_buf);
    trim_text(g_host_buf);
    trim_text(g_port_buf);
    trim_text(g_nick_buf);
    if (!g_host_buf[0])
        return;
    for (i = 0; i < g_addr_count; ++i) {
        if (text_equal_ci(g_addrs[i].host, g_host_buf)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (g_addr_count >= MINI_IRC_MAX_ADDRS)
            return;
        slot = g_addr_count++;
    }
    copy_text(g_addrs[slot].host, sizeof(g_addrs[slot].host), g_host_buf);
    copy_text(g_addrs[slot].port, sizeof(g_addrs[slot].port), g_port_buf);
    copy_text(g_addrs[slot].nick, sizeof(g_addrs[slot].nick), g_nick_buf);
    save_addrbook();
}

static void delete_addrbook_entry(int *selected)
{
    int i;
    int idx;

    if (!selected)
        return;
    idx = *selected;
    if (idx < 0 || idx >= g_addr_count)
        return;

    for (i = idx; i + 1 < g_addr_count; ++i)
        g_addrs[i] = g_addrs[i + 1];
    --g_addr_count;

    if (idx >= g_addr_count)
        idx = g_addr_count - 1;
    *selected = idx;
    if (idx >= 0) {
        copy_text(g_host_buf, sizeof(g_host_buf), g_addrs[idx].host);
        copy_text(g_port_buf, sizeof(g_port_buf), g_addrs[idx].port);
        copy_text(g_nick_buf, sizeof(g_nick_buf), g_addrs[idx].nick);
    }
    save_addrbook();
}

static void draw_connect_dialog(struct Window *win, int selected)
{
    int i;
    WORD y;

    SetAPen(win->RPort, 0);
    RectFill(win->RPort, 0, 0, win->Width - 1, win->Height - 1);
    SetAPen(win->RPort, 1);
    SetBPen(win->RPort, 0);
    SetDrMd(win->RPort, JAM2);
    if (g_gui_font)
        SetFont(win->RPort, g_gui_font);
    Move(win->RPort, MINI_IRC_CONNECT_LIST_X, 20);
    Text(win->RPort, (STRPTR)"Address book", 12);
    draw_field_box_window(win, MINI_IRC_CONNECT_LIST_X, MINI_IRC_CONNECT_LIST_Y,
                          MINI_IRC_CONNECT_LIST_W,
                          (WORD)(MINI_IRC_CONNECT_VISIBLE * MINI_IRC_CONNECT_ROW_H + 4));
    Move(win->RPort, 198, (WORD)(MINI_IRC_CONNECT_HOST_Y + g_baseline + 4));
    Text(win->RPort, (STRPTR)"Host", 4);
    Move(win->RPort, 198, (WORD)(MINI_IRC_CONNECT_PORT_Y + g_baseline + 4));
    Text(win->RPort, (STRPTR)"Port", 4);
    Move(win->RPort, 198, (WORD)(MINI_IRC_CONNECT_NICK_Y + g_baseline + 4));
    Text(win->RPort, (STRPTR)"Nick", 4);
    draw_field_box_window(win, (WORD)(MINI_IRC_CONNECT_HOST_X - 2),
                          (WORD)(MINI_IRC_CONNECT_HOST_Y - 3),
                          (WORD)(MINI_IRC_CONNECT_HOST_W + 4),
                          (WORD)(MINI_IRC_CONNECT_STRING_H + 5));
    draw_field_box_window(win, (WORD)(MINI_IRC_CONNECT_PORT_X - 2),
                          (WORD)(MINI_IRC_CONNECT_PORT_Y - 3),
                          (WORD)(MINI_IRC_CONNECT_PORT_W + 4),
                          (WORD)(MINI_IRC_CONNECT_STRING_H + 5));
    draw_field_box_window(win, (WORD)(MINI_IRC_CONNECT_NICK_X - 2),
                          (WORD)(MINI_IRC_CONNECT_NICK_Y - 3),
                          (WORD)(MINI_IRC_CONNECT_NICK_W + 4),
                          (WORD)(MINI_IRC_CONNECT_STRING_H + 5));
    for (i = 0; i < g_addr_count && i < MINI_IRC_CONNECT_VISIBLE; ++i) {
        y = (WORD)(MINI_IRC_CONNECT_LIST_Y + 10 + i * MINI_IRC_CONNECT_ROW_H);
        if (i == selected) {
            SetAPen(win->RPort, 3);
            RectFill(win->RPort, (WORD)(MINI_IRC_CONNECT_LIST_X + 1), (WORD)(y - 8),
                     (WORD)(MINI_IRC_CONNECT_LIST_X + MINI_IRC_CONNECT_LIST_W - 1),
                     (WORD)(y + 1));
            SetAPen(win->RPort, 0);
        } else {
            SetAPen(win->RPort, 1);
        }
        Move(win->RPort, (WORD)(MINI_IRC_CONNECT_LIST_X + 3), y);
        Text(win->RPort, (STRPTR)g_addrs[i].host, text_len(g_addrs[i].host));
    }
    SetAPen(win->RPort, 1);
    draw_button_window(win, 12, MINI_IRC_CONNECT_BUTTON_Y, 46, 14, "Up");
    draw_button_window(win, 68, MINI_IRC_CONNECT_BUTTON_Y, 46, 14, "Down");
    draw_button_window(win, 120, MINI_IRC_CONNECT_BUTTON_Y, 70, 14, "Connect");
    draw_button_window(win, 198, MINI_IRC_CONNECT_BUTTON_Y, 48, 14, "Save");
    draw_button_window(win, 254, MINI_IRC_CONNECT_BUTTON_Y, 58, 14, "Delete");
    draw_button_window(win, 320, MINI_IRC_CONNECT_BUTTON_Y, 62, 14, "Cancel");
}

static void open_connect_dialog(void)
{
    struct NewWindow nw;
    struct Window *win;
    struct IntuiMessage *msg;
    ULONG cls;
    UWORD code;
    struct Gadget *gad;
    int done = 0;
    int selected = -1;
    static struct StringInfo host_si;
    static struct StringInfo port_si;
    static struct StringInfo nick_si;
    static struct Gadget host_gad;
    static struct Gadget port_gad;
    static struct Gadget nick_gad;
    static struct Gadget connect_gad;
    static struct Gadget save_gad;
    static struct Gadget delete_gad;
    static struct Gadget cancel_gad;
    WORD win_w;
    WORD win_h;
    WORD button_y;

    memset(&host_si, 0, sizeof(host_si));
    memset(&port_si, 0, sizeof(port_si));
    memset(&nick_si, 0, sizeof(nick_si));
    host_si.Buffer = (STRPTR)g_host_buf;
    host_si.UndoBuffer = (STRPTR)g_host_undo;
    host_si.MaxChars = sizeof(g_host_buf);
    port_si.Buffer = (STRPTR)g_port_buf;
    port_si.UndoBuffer = (STRPTR)g_port_undo;
    port_si.MaxChars = sizeof(g_port_buf);
    nick_si.Buffer = (STRPTR)g_nick_buf;
    nick_si.UndoBuffer = (STRPTR)g_nick_undo;
    nick_si.MaxChars = sizeof(g_nick_buf);

    memset(&host_gad, 0, sizeof(host_gad));
    memset(&port_gad, 0, sizeof(port_gad));
    memset(&nick_gad, 0, sizeof(nick_gad));
    memset(&connect_gad, 0, sizeof(connect_gad));
    memset(&save_gad, 0, sizeof(save_gad));
    memset(&delete_gad, 0, sizeof(delete_gad));
    memset(&cancel_gad, 0, sizeof(cancel_gad));
    host_gad.NextGadget = &port_gad;
    host_gad.LeftEdge = MINI_IRC_CONNECT_HOST_X;
    host_gad.TopEdge = MINI_IRC_CONNECT_HOST_Y;
    host_gad.Width = MINI_IRC_CONNECT_HOST_W;
    host_gad.Height = MINI_IRC_CONNECT_STRING_H;
    host_gad.Activation = GACT_RELVERIFY;
    host_gad.GadgetType = GTYP_STRGADGET;
    host_gad.SpecialInfo = &host_si;
    host_gad.GadgetID = MINI_IRC_CGID_HOST;
    port_gad.NextGadget = &nick_gad;
    port_gad.LeftEdge = MINI_IRC_CONNECT_PORT_X;
    port_gad.TopEdge = MINI_IRC_CONNECT_PORT_Y;
    port_gad.Width = MINI_IRC_CONNECT_PORT_W;
    port_gad.Height = MINI_IRC_CONNECT_STRING_H;
    port_gad.Activation = GACT_RELVERIFY;
    port_gad.GadgetType = GTYP_STRGADGET;
    port_gad.SpecialInfo = &port_si;
    port_gad.GadgetID = MINI_IRC_CGID_PORT;
    nick_gad.NextGadget = &connect_gad;
    nick_gad.LeftEdge = MINI_IRC_CONNECT_NICK_X;
    nick_gad.TopEdge = MINI_IRC_CONNECT_NICK_Y;
    nick_gad.Width = MINI_IRC_CONNECT_NICK_W;
    nick_gad.Height = MINI_IRC_CONNECT_STRING_H;
    nick_gad.Activation = GACT_RELVERIFY;
    nick_gad.GadgetType = GTYP_STRGADGET;
    nick_gad.SpecialInfo = &nick_si;
    nick_gad.GadgetID = MINI_IRC_CGID_NICK;
    connect_gad.NextGadget = &save_gad;
    button_y = MINI_IRC_CONNECT_BUTTON_Y;
    connect_gad.LeftEdge = 120;
    connect_gad.TopEdge = button_y;
    connect_gad.Width = 70;
    connect_gad.Height = 14;
    connect_gad.Flags = GFLG_GADGHCOMP;
    connect_gad.Activation = GACT_RELVERIFY;
    connect_gad.GadgetType = GTYP_BOOLGADGET;
    connect_gad.GadgetID = MINI_IRC_CGID_CONNECT;
    save_gad.NextGadget = &delete_gad;
    save_gad.LeftEdge = 198;
    save_gad.TopEdge = button_y;
    save_gad.Width = 48;
    save_gad.Height = 14;
    save_gad.Flags = GFLG_GADGHCOMP;
    save_gad.Activation = GACT_RELVERIFY;
    save_gad.GadgetType = GTYP_BOOLGADGET;
    save_gad.GadgetID = MINI_IRC_CGID_SAVE;
    delete_gad.NextGadget = &cancel_gad;
    delete_gad.LeftEdge = 254;
    delete_gad.TopEdge = button_y;
    delete_gad.Width = 58;
    delete_gad.Height = 14;
    delete_gad.Flags = GFLG_GADGHCOMP;
    delete_gad.Activation = GACT_RELVERIFY;
    delete_gad.GadgetType = GTYP_BOOLGADGET;
    delete_gad.GadgetID = MINI_IRC_CGID_DELETE;
    cancel_gad.LeftEdge = 320;
    cancel_gad.TopEdge = button_y;
    cancel_gad.Width = 62;
    cancel_gad.Height = 14;
    cancel_gad.Flags = GFLG_GADGHCOMP;
    cancel_gad.Activation = GACT_RELVERIFY;
    cancel_gad.GadgetType = GTYP_BOOLGADGET;
    cancel_gad.GadgetID = MINI_IRC_CGID_CANCEL;

    memset(&nw, 0, sizeof(nw));
    win_w = MINI_IRC_CONNECT_WIN_W;
    win_h = MINI_IRC_CONNECT_WIN_H;
    if (g_screen) {
        if (g_screen->Width < win_w)
            win_w = g_screen->Width;
        nw.LeftEdge = (WORD)((g_screen->Width - win_w) / 2);
        nw.TopEdge = (WORD)((g_screen->Height - win_h) / 2);
        if (nw.LeftEdge < 0)
            nw.LeftEdge = 0;
        if (nw.TopEdge < 12)
            nw.TopEdge = 12;
    } else {
        nw.LeftEdge = 30;
        nw.TopEdge = 24;
    }
    nw.Width = win_w;
    nw.Height = win_h;
    nw.DetailPen = 1;
    nw.BlockPen = 0;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_GADGETUP |
                    IDCMP_MOUSEBUTTONS | IDCMP_REFRESHWINDOW;
    nw.Flags = WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
               WFLG_SMART_REFRESH | WFLG_ACTIVATE;
    nw.FirstGadget = &host_gad;
    nw.Title = (STRPTR)"MiniIRC Connect";
    nw.Screen = g_screen;
    nw.Type = CUSTOMSCREEN;
    win = OpenWindow(&nw);
    if (!win) {
        status_text("Connect window failed");
        return;
    }
    WindowToFront(win);
    ActivateWindow(win);
    if (g_gui_font)
        SetFont(win->RPort, g_gui_font);
    draw_connect_dialog(win, selected);
    RefreshGList(&host_gad, win, 0, 3);

    while (!done) {
        Wait(1L << win->UserPort->mp_SigBit);
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) {
            cls = msg->Class;
            code = msg->Code;
            gad = (struct Gadget *)msg->IAddress;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) {
                done = 1;
            } else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(win);
                draw_connect_dialog(win, selected);
                RefreshGList(&host_gad, win, 0, 3);
                EndRefresh(win, TRUE);
            } else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                WORD mx = win->MouseX;
                WORD my = win->MouseY;
                if (my >= MINI_IRC_CONNECT_LIST_Y &&
                    my < MINI_IRC_CONNECT_LIST_Y + MINI_IRC_CONNECT_VISIBLE * MINI_IRC_CONNECT_ROW_H) {
                    selected = (my - MINI_IRC_CONNECT_LIST_Y) / MINI_IRC_CONNECT_ROW_H;
                    if (selected >= 0 && selected < g_addr_count) {
                        copy_text(g_host_buf, sizeof(g_host_buf), g_addrs[selected].host);
                        copy_text(g_port_buf, sizeof(g_port_buf), g_addrs[selected].port);
                        copy_text(g_nick_buf, sizeof(g_nick_buf), g_addrs[selected].nick);
                        draw_connect_dialog(win, selected);
                        RefreshGList(&host_gad, win, 0, 3);
                    }
                }
                (void)mx;
            } else if (cls == IDCMP_GADGETUP && gad) {
                if (gad->GadgetID == MINI_IRC_CGID_CONNECT) {
                    done = 1;
                    connect_irc();
                } else if (gad->GadgetID == MINI_IRC_CGID_SAVE) {
                    add_current_to_addrbook();
                    draw_connect_dialog(win, selected);
                    RefreshGList(&host_gad, win, 0, 3);
                } else if (gad->GadgetID == MINI_IRC_CGID_DELETE) {
                    delete_addrbook_entry(&selected);
                    draw_connect_dialog(win, selected);
                    RefreshGList(&host_gad, win, 0, 3);
                } else if (gad->GadgetID == MINI_IRC_CGID_CANCEL) {
                    done = 1;
                }
            }
        }
    }
    CloseWindow(win);
    redraw_all();
}


static void draw_font_selector(struct Window *win)
{
    UWORD row;
    UWORD idx;
    WORD y;
    char line[MINI_IRC_FONT_NAME_MAX + 3];
    char num[8];
    int i;
    int j;

    SetAPen(win->RPort, 0);
    RectFill(win->RPort, 0, 0, win->Width - 1, win->Height - 1);
    SetAPen(win->RPort, 1);
    SetBPen(win->RPort, 0);
    SetDrMd(win->RPort, JAM2);
    if (g_gui_font)
        SetFont(win->RPort, g_gui_font);
    Move(win->RPort, MINI_IRC_FONT_LIST_X, 18);
    Text(win->RPort, (STRPTR)"Font", 4);
    Move(win->RPort, MINI_IRC_FONT_SIZE_X, 18);
    Text(win->RPort, (STRPTR)"Size", 4);
    Move(win->RPort, MINI_IRC_FONT_COLOR_X, 18);
    Text(win->RPort, (STRPTR)"Color", 5);
    Move(win->RPort, MINI_IRC_FONT_LIST_X, MINI_IRC_FONT_LIST_Y);
    Draw(win->RPort, MINI_IRC_FONT_LIST_X + MINI_IRC_FONT_LIST_W, MINI_IRC_FONT_LIST_Y);
    Draw(win->RPort, MINI_IRC_FONT_LIST_X + MINI_IRC_FONT_LIST_W, MINI_IRC_FONT_LIST_Y + MINI_IRC_FONT_LIST_H);
    Draw(win->RPort, MINI_IRC_FONT_LIST_X, MINI_IRC_FONT_LIST_Y + MINI_IRC_FONT_LIST_H);
    Draw(win->RPort, MINI_IRC_FONT_LIST_X, MINI_IRC_FONT_LIST_Y);
    Move(win->RPort, MINI_IRC_FONT_SIZE_X, MINI_IRC_FONT_LIST_Y);
    Draw(win->RPort, MINI_IRC_FONT_SIZE_X + MINI_IRC_FONT_SIZE_W, MINI_IRC_FONT_LIST_Y);
    Draw(win->RPort, MINI_IRC_FONT_SIZE_X + MINI_IRC_FONT_SIZE_W, MINI_IRC_FONT_LIST_Y + MINI_IRC_FONT_LIST_H);
    Draw(win->RPort, MINI_IRC_FONT_SIZE_X, MINI_IRC_FONT_LIST_Y + MINI_IRC_FONT_LIST_H);
    Draw(win->RPort, MINI_IRC_FONT_SIZE_X, MINI_IRC_FONT_LIST_Y);
    Move(win->RPort, MINI_IRC_FONT_COLOR_X, MINI_IRC_FONT_LIST_Y);
    Draw(win->RPort, MINI_IRC_FONT_COLOR_X + MINI_IRC_FONT_COLOR_W, MINI_IRC_FONT_LIST_Y);
    Draw(win->RPort, MINI_IRC_FONT_COLOR_X + MINI_IRC_FONT_COLOR_W, MINI_IRC_FONT_LIST_Y + MINI_IRC_FONT_LIST_H);
    Draw(win->RPort, MINI_IRC_FONT_COLOR_X, MINI_IRC_FONT_LIST_Y + MINI_IRC_FONT_LIST_H);
    Draw(win->RPort, MINI_IRC_FONT_COLOR_X, MINI_IRC_FONT_LIST_Y);
    draw_button_window(win, 342, 48, 44, 14, "Up");
    draw_button_window(win, 342, 68, 44, 14, "Down");
    draw_button_window(win, 132, 150, 42, 14, "OK");
    draw_button_window(win, 190, 150, 62, 14, "Cancel");

    for (row = 0; row < MINI_IRC_FONT_VISIBLE; ++row) {
        idx = (UWORD)(g_font_top + row);
        if (idx >= g_font_count)
            break;
        line[0] = (idx == g_font_selected) ? '>' : ' ';
        line[1] = ' ';
        i = 2;
        for (j = 0; g_font_names[idx][j] && i < (int)sizeof(line) - 1; ++j)
            line[i++] = g_font_names[idx][j];
        line[i] = 0;
        y = (WORD)(MINI_IRC_FONT_LIST_Y + 10 + row * MINI_IRC_FONT_ROW_H);
        Move(win->RPort, (WORD)(MINI_IRC_FONT_LIST_X + 3), y);
        Text(win->RPort, (STRPTR)line, i);
    }
    if (g_font_count == 0) {
        Move(win->RPort, (WORD)(MINI_IRC_FONT_LIST_X + 3), (WORD)(MINI_IRC_FONT_LIST_Y + 18));
        Text(win->RPort, (STRPTR)"No .font files", 14);
    }
    for (row = 0; row < g_font_size_count && row < MINI_IRC_FONT_VISIBLE; ++row) {
        line[0] = (row == g_font_size_selected) ? '>' : ' ';
        line[1] = ' ';
        number_to_text(g_font_sizes[row], num, sizeof(num));
        i = 2;
        for (j = 0; num[j] && i < (int)sizeof(line) - 1; ++j)
            line[i++] = num[j];
        line[i] = 0;
        y = (WORD)(MINI_IRC_FONT_LIST_Y + 10 + row * MINI_IRC_FONT_ROW_H);
        Move(win->RPort, (WORD)(MINI_IRC_FONT_SIZE_X + 3), y);
        Text(win->RPort, (STRPTR)line, i);
    }
    for (row = 0; row < MINI_IRC_TEXT_COLOR_MAX && row < MINI_IRC_FONT_VISIBLE; ++row) {
        line[0] = (row == g_font_color_selected) ? '>' : ' ';
        line[1] = ' ';
        i = 2;
        for (j = 0; g_text_colors[row].name[j] && i < (int)sizeof(line) - 1; ++j)
            line[i++] = g_text_colors[row].name[j];
        line[i] = 0;
        y = (WORD)(MINI_IRC_FONT_LIST_Y + 10 + row * MINI_IRC_FONT_ROW_H);
        Move(win->RPort, (WORD)(MINI_IRC_FONT_COLOR_X + 3), y);
        Text(win->RPort, (STRPTR)line, i);
    }
}

static void open_font_selector(void)
{
    struct NewWindow nw;
    struct Window *win;
    struct IntuiMessage *msg;
    ULONG cls;
    UWORD code;
    struct Gadget *gad;
    WORD mx;
    WORD my;
    UWORD row;
    int done = 0;
    static struct Gadget up_gad;
    static struct Gadget down_gad;
    static struct Gadget ok_gad;
    static struct Gadget cancel_gad;

    scan_fonts();
    scan_font_sizes();
    g_font_color_selected = g_text_color_index;
    if (g_font_color_selected >= MINI_IRC_TEXT_COLOR_MAX)
        g_font_color_selected = 0;
    memset(&up_gad, 0, sizeof(up_gad));
    memset(&down_gad, 0, sizeof(down_gad));
    memset(&ok_gad, 0, sizeof(ok_gad));
    memset(&cancel_gad, 0, sizeof(cancel_gad));
    up_gad.NextGadget = &down_gad;
    up_gad.LeftEdge = 342;
    up_gad.TopEdge = 48;
    up_gad.Width = 44;
    up_gad.Height = 14;
    up_gad.Flags = GFLG_GADGHCOMP;
    up_gad.Activation = GACT_RELVERIFY;
    up_gad.GadgetType = GTYP_BOOLGADGET;
    up_gad.GadgetID = 101;
    down_gad.NextGadget = &ok_gad;
    down_gad.LeftEdge = 342;
    down_gad.TopEdge = 68;
    down_gad.Width = 44;
    down_gad.Height = 14;
    down_gad.Flags = GFLG_GADGHCOMP;
    down_gad.Activation = GACT_RELVERIFY;
    down_gad.GadgetType = GTYP_BOOLGADGET;
    down_gad.GadgetID = 102;
    ok_gad.NextGadget = &cancel_gad;
    ok_gad.LeftEdge = 132;
    ok_gad.TopEdge = 150;
    ok_gad.Width = 42;
    ok_gad.Height = 14;
    ok_gad.Flags = GFLG_GADGHCOMP;
    ok_gad.Activation = GACT_RELVERIFY;
    ok_gad.GadgetType = GTYP_BOOLGADGET;
    ok_gad.GadgetID = 103;
    cancel_gad.LeftEdge = 190;
    cancel_gad.TopEdge = 150;
    cancel_gad.Width = 62;
    cancel_gad.Height = 14;
    cancel_gad.Flags = GFLG_GADGHCOMP;
    cancel_gad.Activation = GACT_RELVERIFY;
    cancel_gad.GadgetType = GTYP_BOOLGADGET;
    cancel_gad.GadgetID = 104;

    memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = 90;
    nw.TopEdge = 35;
    nw.Width = MINI_IRC_FONT_WIN_W;
    nw.Height = MINI_IRC_FONT_WIN_H;
    nw.DetailPen = 1;
    nw.BlockPen = 0;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_MOUSEBUTTONS | IDCMP_REFRESHWINDOW;
    nw.Flags = WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_SMART_REFRESH | WFLG_ACTIVATE;
    nw.FirstGadget = &up_gad;
    nw.Title = (STRPTR)"MiniIRC Font";
    nw.Screen = g_screen;
    nw.Type = CUSTOMSCREEN;
    win = OpenWindow(&nw);
    if (!win) {
        status_text("Font window failed");
        return;
    }
    if (g_gui_font)
        SetFont(win->RPort, g_gui_font);
    draw_font_selector(win);
    while (!done) {
        Wait(1L << win->UserPort->mp_SigBit);
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) {
            cls = msg->Class;
            code = msg->Code;
            gad = (struct Gadget *)msg->IAddress;
            mx = msg->MouseX;
            my = msg->MouseY;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) {
                done = 1;
            } else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(win);
                draw_font_selector(win);
                EndRefresh(win, TRUE);
            } else if (cls == IDCMP_GADGETUP && gad) {
                if (gad->GadgetID == 101) {
                    if (g_font_top > 0)
                        --g_font_top;
                    draw_font_selector(win);
                } else if (gad->GadgetID == 102) {
                    if ((ULONG)g_font_top + MINI_IRC_FONT_VISIBLE < g_font_count)
                        ++g_font_top;
                    draw_font_selector(win);
                } else if (gad->GadgetID == 103) {
                    if (g_font_count && g_font_size_count) {
                        if (apply_gui_font(g_font_names[g_font_selected], g_font_sizes[g_font_size_selected]))
                            status_text("Font changed");
                        else
                            status_text("Font not available");
                    }
                    g_text_color_index = g_font_color_selected;
                    apply_screen_palette();
                    save_config();
                    redraw_all();
                    done = 1;
                } else if (gad->GadgetID == 104) {
                    done = 1;
                }
            } else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                if (mx >= MINI_IRC_FONT_LIST_X && mx < MINI_IRC_FONT_LIST_X + MINI_IRC_FONT_LIST_W &&
                    my >= MINI_IRC_FONT_LIST_Y && my < MINI_IRC_FONT_LIST_Y + MINI_IRC_FONT_LIST_H) {
                    row = (UWORD)((my - MINI_IRC_FONT_LIST_Y) / MINI_IRC_FONT_ROW_H);
                    if ((ULONG)g_font_top + row < g_font_count) {
                        g_font_selected = (UWORD)(g_font_top + row);
                        scan_font_sizes();
                        draw_font_selector(win);
                    }
                } else if (mx >= MINI_IRC_FONT_SIZE_X && mx < MINI_IRC_FONT_SIZE_X + MINI_IRC_FONT_SIZE_W &&
                    my >= MINI_IRC_FONT_LIST_Y && my < MINI_IRC_FONT_LIST_Y + MINI_IRC_FONT_LIST_H) {
                    row = (UWORD)((my - MINI_IRC_FONT_LIST_Y) / MINI_IRC_FONT_ROW_H);
                    if (row < g_font_size_count) {
                        g_font_size_selected = row;
                        draw_font_selector(win);
                    }
                } else if (mx >= MINI_IRC_FONT_COLOR_X && mx < MINI_IRC_FONT_COLOR_X + MINI_IRC_FONT_COLOR_W &&
                    my >= MINI_IRC_FONT_LIST_Y && my < MINI_IRC_FONT_LIST_Y + MINI_IRC_FONT_LIST_H) {
                    row = (UWORD)((my - MINI_IRC_FONT_LIST_Y) / MINI_IRC_FONT_ROW_H);
                    if (row < MINI_IRC_TEXT_COLOR_MAX) {
                        g_font_color_selected = (UBYTE)row;
                        draw_font_selector(win);
                    }
                }
            }
        }
    }
    CloseWindow(win);
    redraw_all();
}


static void draw_background_selector(struct Window *win, UBYTE selected)
{
    int i;
    WORD y;

    SetAPen(win->RPort, 0);
    RectFill(win->RPort, 0, 0, win->Width - 1, win->Height - 1);
    SetAPen(win->RPort, 1);
    SetBPen(win->RPort, 0);
    SetDrMd(win->RPort, JAM2);
    if (g_gui_font)
        SetFont(win->RPort, g_gui_font);
    Move(win->RPort, 12, 16);
    Text(win->RPort, (STRPTR)"Background", 10);
    draw_field_box_window(win, MINI_IRC_BG_LIST_X, MINI_IRC_BG_LIST_Y,
                          140, MINI_IRC_BG_COLOR_MAX * MINI_IRC_BG_ROW_H + 4);
    for (i = 0; i < MINI_IRC_BG_COLOR_MAX; ++i) {
        y = (WORD)(MINI_IRC_BG_LIST_Y + 10 + i * MINI_IRC_BG_ROW_H);
        if (i == selected) {
            SetAPen(win->RPort, 3);
            RectFill(win->RPort, (WORD)(MINI_IRC_BG_LIST_X + 1), (WORD)(y - 8),
                     (WORD)(MINI_IRC_BG_LIST_X + 139), (WORD)(y + 1));
            SetAPen(win->RPort, 0);
        } else {
            SetAPen(win->RPort, 1);
        }
        Move(win->RPort, (WORD)(MINI_IRC_BG_LIST_X + 4), y);
        Text(win->RPort, (STRPTR)g_bg_colors[i].name, text_len(g_bg_colors[i].name));
    }
    SetAPen(win->RPort, 1);
    draw_button_window(win, 36, 118, 42, 14, "OK");
    draw_button_window(win, 92, 118, 62, 14, "Cancel");
}

static void open_background_selector(void)
{
    struct NewWindow nw;
    struct Window *win;
    struct IntuiMessage *msg;
    ULONG cls;
    UWORD code;
    struct Gadget *gad;
    WORD mx;
    WORD my;
    UBYTE selected;
    UWORD row;
    int done = 0;
    static struct Gadget ok_gad;
    static struct Gadget cancel_gad;

    selected = g_bg_color_index;
    if (selected >= MINI_IRC_BG_COLOR_MAX)
        selected = 0;
    memset(&ok_gad, 0, sizeof(ok_gad));
    memset(&cancel_gad, 0, sizeof(cancel_gad));
    ok_gad.NextGadget = &cancel_gad;
    ok_gad.LeftEdge = 36;
    ok_gad.TopEdge = 118;
    ok_gad.Width = 42;
    ok_gad.Height = 14;
    ok_gad.Flags = GFLG_GADGHCOMP;
    ok_gad.Activation = GACT_RELVERIFY;
    ok_gad.GadgetType = GTYP_BOOLGADGET;
    ok_gad.GadgetID = 201;
    cancel_gad.LeftEdge = 92;
    cancel_gad.TopEdge = 118;
    cancel_gad.Width = 62;
    cancel_gad.Height = 14;
    cancel_gad.Flags = GFLG_GADGHCOMP;
    cancel_gad.Activation = GACT_RELVERIFY;
    cancel_gad.GadgetType = GTYP_BOOLGADGET;
    cancel_gad.GadgetID = 202;

    memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = 110;
    nw.TopEdge = 45;
    nw.Width = MINI_IRC_BG_WIN_W;
    nw.Height = MINI_IRC_BG_WIN_H;
    nw.DetailPen = 1;
    nw.BlockPen = 0;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_MOUSEBUTTONS | IDCMP_REFRESHWINDOW;
    nw.Flags = WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_SMART_REFRESH | WFLG_ACTIVATE;
    nw.FirstGadget = &ok_gad;
    nw.Title = (STRPTR)"MiniIRC Background";
    nw.Screen = g_screen;
    nw.Type = CUSTOMSCREEN;
    win = OpenWindow(&nw);
    if (!win) {
        status_text("Background window failed");
        return;
    }
    if (g_gui_font)
        SetFont(win->RPort, g_gui_font);
    draw_background_selector(win, selected);
    while (!done) {
        Wait(1L << win->UserPort->mp_SigBit);
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) {
            cls = msg->Class;
            code = msg->Code;
            gad = (struct Gadget *)msg->IAddress;
            mx = msg->MouseX;
            my = msg->MouseY;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) {
                done = 1;
            } else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(win);
                draw_background_selector(win, selected);
                EndRefresh(win, TRUE);
            } else if (cls == IDCMP_GADGETUP && gad) {
                if (gad->GadgetID == 201) {
                    g_bg_color_index = selected;
                    apply_screen_palette();
                    save_config();
                    redraw_all();
                    status_text("Background changed");
                    done = 1;
                } else if (gad->GadgetID == 202) {
                    done = 1;
                }
            } else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                if (mx >= MINI_IRC_BG_LIST_X && mx < MINI_IRC_BG_LIST_X + 140 &&
                    my >= MINI_IRC_BG_LIST_Y &&
                    my < MINI_IRC_BG_LIST_Y + MINI_IRC_BG_COLOR_MAX * MINI_IRC_BG_ROW_H) {
                    row = (UWORD)((my - MINI_IRC_BG_LIST_Y) / MINI_IRC_BG_ROW_H);
                    if (row < MINI_IRC_BG_COLOR_MAX) {
                        selected = (UBYTE)row;
                        draw_background_selector(win, selected);
                    }
                }
            }
        }
    }
    CloseWindow(win);
    redraw_all();
}


static void draw_info_dialog(struct Window *win)
{
    SetAPen(win->RPort, 0);
    RectFill(win->RPort, 0, 0, win->Width - 1, win->Height - 1);
    SetAPen(win->RPort, 1);
    SetBPen(win->RPort, 0);
    SetDrMd(win->RPort, JAM2);
    if (g_gui_font)
        SetFont(win->RPort, g_gui_font);
    Move(win->RPort, 14, 24);
    Text(win->RPort, (STRPTR)"MiniIRC for Kick1.3", text_len("MiniIRC for Kick1.3"));
    Move(win->RPort, 14, 40);
    Text(win->RPort, (STRPTR)"Version: " MINI_IRC_VERSION, text_len("Version: " MINI_IRC_VERSION));
    Move(win->RPort, 14, 56);
    Text(win->RPort, (STRPTR)"by Marcel Jaehne", text_len("by Marcel Jaehne"));
    Move(win->RPort, 14, 72);
    Text(win->RPort, (STRPTR)"(c) 2026", text_len("(c) 2026"));
    Move(win->RPort, 14, 92);
    Text(win->RPort, (STRPTR)"If you want to buy me a coffe, send me", text_len("If you want to buy me a coffe, send me"));
    Move(win->RPort, 14, 108);
    Text(win->RPort, (STRPTR)"a buck to: https://paypal.me/mytubefree", text_len("a buck to: https://paypal.me/mytubefree"));
    draw_button_window(win, 142, 134, 42, 14, "OK");
}

static void open_info_dialog(void)
{
    struct NewWindow nw;
    struct Window *win;
    struct IntuiMessage *msg;
    ULONG cls;
    struct Gadget *gad;
    int done = 0;
    static struct Gadget ok_gad;

    memset(&ok_gad, 0, sizeof(ok_gad));
    ok_gad.LeftEdge = 142;
    ok_gad.TopEdge = 134;
    ok_gad.Width = 42;
    ok_gad.Height = 14;
    ok_gad.Flags = GFLG_GADGHCOMP;
    ok_gad.Activation = GACT_RELVERIFY;
    ok_gad.GadgetType = GTYP_BOOLGADGET;
    ok_gad.GadgetID = 401;

    memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = 120;
    nw.TopEdge = 45;
    nw.Width = 330;
    nw.Height = 164;
    nw.DetailPen = 1;
    nw.BlockPen = 0;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW;
    nw.Flags = WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_SMART_REFRESH | WFLG_ACTIVATE;
    nw.FirstGadget = &ok_gad;
    nw.Title = (STRPTR)"MiniIRC Info";
    nw.Screen = g_screen;
    nw.Type = CUSTOMSCREEN;
    win = OpenWindow(&nw);
    if (!win) {
        status_text("Info window failed");
        return;
    }
    if (g_gui_font)
        SetFont(win->RPort, g_gui_font);
    draw_info_dialog(win);
    while (!done) {
        Wait(1L << win->UserPort->mp_SigBit);
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != 0) {
            cls = msg->Class;
            gad = (struct Gadget *)msg->IAddress;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) {
                done = 1;
            } else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(win);
                draw_info_dialog(win);
                EndRefresh(win, TRUE);
            } else if (cls == IDCMP_GADGETUP && gad && gad->GadgetID == 401) {
                done = 1;
            }
        }
    }
    CloseWindow(win);
    redraw_all();
}

static void setup_menu_text(struct IntuiText *it, char *text)
{
    it->FrontPen = 1;
    it->BackPen = 0;
    it->DrawMode = JAM2;
    it->LeftEdge = 4;
    it->TopEdge = 1;
    it->ITextFont = 0;
    it->IText = (STRPTR)text;
    it->NextText = 0;
}

static void setup_menu(void)
{
    memset(g_menus, 0, sizeof(g_menus));
    memset(g_project_items, 0, sizeof(g_project_items));
    memset(g_settings_items, 0, sizeof(g_settings_items));
    memset(g_help_items, 0, sizeof(g_help_items));
    setup_menu_text(&g_project_text[0], "Connect");
    setup_menu_text(&g_project_text[1], "Disconnect");
    setup_menu_text(&g_project_text[2], "Quit");
    setup_menu_text(&g_settings_text[0], "Address Book...");
    setup_menu_text(&g_settings_text[1], "Font...");
    setup_menu_text(&g_settings_text[2], "Background...");
    setup_menu_text(&g_help_text[0], "Info");

    g_menus[0].NextMenu = &g_menus[1];
    g_menus[0].LeftEdge = 0;
    g_menus[0].TopEdge = 0;
    g_menus[0].Width = 64;
    g_menus[0].Height = 10;
    g_menus[0].Flags = MENUENABLED;
    g_menus[0].MenuName = (STRPTR)"Project";
    g_menus[0].FirstItem = &g_project_items[0];
    g_menus[1].NextMenu = &g_menus[2];
    g_menus[1].LeftEdge = 74;
    g_menus[1].TopEdge = 0;
    g_menus[1].Width = 70;
    g_menus[1].Height = 10;
    g_menus[1].Flags = MENUENABLED;
    g_menus[1].MenuName = (STRPTR)"Settings";
    g_menus[1].FirstItem = &g_settings_items[0];
    g_menus[2].LeftEdge = 154;
    g_menus[2].TopEdge = 0;
    g_menus[2].Width = 16;
    g_menus[2].Height = 10;
    g_menus[2].Flags = MENUENABLED;
    g_menus[2].MenuName = (STRPTR)"?";
    g_menus[2].FirstItem = &g_help_items[0];

    g_project_items[0].NextItem = &g_project_items[1];
    g_project_items[1].NextItem = &g_project_items[2];
    g_project_items[0].ItemFill = &g_project_text[0];
    g_project_items[1].ItemFill = &g_project_text[1];
    g_project_items[2].ItemFill = &g_project_text[2];
    g_settings_items[0].NextItem = &g_settings_items[1];
    g_settings_items[1].NextItem = &g_settings_items[2];
    g_settings_items[0].ItemFill = &g_settings_text[0];
    g_settings_items[1].ItemFill = &g_settings_text[1];
    g_settings_items[2].ItemFill = &g_settings_text[2];
    g_help_items[0].ItemFill = &g_help_text[0];
    g_project_items[0].TopEdge = 0;
    g_project_items[1].TopEdge = 10;
    g_project_items[2].TopEdge = 20;
    g_settings_items[0].TopEdge = 0;
    g_settings_items[1].TopEdge = 10;
    g_settings_items[2].TopEdge = 20;
    g_help_items[0].TopEdge = 0;
    g_project_items[0].Width = 92;
    g_project_items[1].Width = 92;
    g_project_items[2].Width = 92;
    g_settings_items[0].Width = 132;
    g_settings_items[1].Width = 132;
    g_settings_items[2].Width = 132;
    g_help_items[0].Width = 58;
    g_project_items[0].Height = 10;
    g_project_items[1].Height = 10;
    g_project_items[2].Height = 10;
    g_settings_items[0].Height = 10;
    g_settings_items[1].Height = 10;
    g_settings_items[2].Height = 10;
    g_help_items[0].Height = 10;
    g_project_items[0].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    g_project_items[1].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    g_project_items[2].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    g_settings_items[0].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    g_settings_items[1].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    g_settings_items[2].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    g_help_items[0].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
}

static void update_main_gadget_positions(void);

static void setup_main_gadgets(void)
{
    memset(&g_join_si, 0, sizeof(g_join_si));
    memset(&g_msg_si, 0, sizeof(g_msg_si));
    memset(&g_join_gadget, 0, sizeof(g_join_gadget));
    memset(&g_join_button, 0, sizeof(g_join_button));
    memset(&g_list_button, 0, sizeof(g_list_button));
    memset(&g_msg_gadget, 0, sizeof(g_msg_gadget));
    memset(&g_send_gadget, 0, sizeof(g_send_gadget));

    g_join_si.Buffer = (STRPTR)g_join_buf;
    g_join_si.UndoBuffer = (STRPTR)g_join_undo;
    g_join_si.MaxChars = sizeof(g_join_buf);
    g_msg_si.Buffer = (STRPTR)g_msg_buf;
    g_msg_si.UndoBuffer = (STRPTR)g_msg_undo;
    g_msg_si.MaxChars = sizeof(g_msg_buf);

    g_join_gadget.NextGadget = &g_join_button;
    g_join_gadget.Height = 16;
    g_join_gadget.Activation = GACT_RELVERIFY;
    g_join_gadget.GadgetType = GTYP_STRGADGET;
    g_join_gadget.SpecialInfo = &g_join_si;
    g_join_gadget.GadgetID = MINI_IRC_GID_JOIN_STR;
    g_join_button.NextGadget = &g_list_button;
    g_join_button.Width = 52;
    g_join_button.Height = 16;
    g_join_button.Flags = GFLG_GADGHCOMP;
    g_join_button.Activation = GACT_RELVERIFY;
    g_join_button.GadgetType = GTYP_BOOLGADGET;
    g_join_button.GadgetID = MINI_IRC_GID_JOIN;
    g_list_button.NextGadget = &g_msg_gadget;
    g_list_button.Width = 48;
    g_list_button.Height = 16;
    g_list_button.Flags = GFLG_GADGHCOMP;
    g_list_button.Activation = GACT_RELVERIFY;
    g_list_button.GadgetType = GTYP_BOOLGADGET;
    g_list_button.GadgetID = MINI_IRC_GID_LIST;
    g_msg_gadget.NextGadget = &g_send_gadget;
    g_msg_gadget.Height = 16;
    g_msg_gadget.Activation = GACT_RELVERIFY;
    g_msg_gadget.GadgetType = GTYP_STRGADGET;
    g_msg_gadget.SpecialInfo = &g_msg_si;
    g_msg_gadget.GadgetID = MINI_IRC_GID_MSG_STR;
    g_send_gadget.Width = 46;
    g_send_gadget.Height = 16;
    g_send_gadget.Flags = GFLG_GADGHCOMP;
    g_send_gadget.Activation = GACT_RELVERIFY;
    g_send_gadget.GadgetType = GTYP_BOOLGADGET;
    g_send_gadget.GadgetID = MINI_IRC_GID_SEND;
    update_main_gadget_positions();
}

static void update_main_gadget_positions(void)
{
    WORD w = g_win->Width;
    WORD join_y = (WORD)(g_input_y + 3);
    WORD msg_y = (WORD)(g_input_y + 24);
    WORD send_x;
    WORD join_button_x;
    WORD list_button_x;

    g_join_gadget.LeftEdge = 76;
    g_join_gadget.TopEdge = join_y;
    g_join_gadget.Width = 180;
    join_button_x = (WORD)(g_join_gadget.LeftEdge + g_join_gadget.Width + 8);
    g_join_button.LeftEdge = join_button_x;
    g_join_button.TopEdge = join_y;
    list_button_x = (WORD)(g_join_button.LeftEdge + g_join_button.Width + 6);
    g_list_button.LeftEdge = list_button_x;
    g_list_button.TopEdge = join_y;
    g_msg_gadget.LeftEdge = 76;
    g_msg_gadget.TopEdge = msg_y;
    send_x = (WORD)(w - 52);
    g_send_gadget.LeftEdge = send_x;
    g_send_gadget.TopEdge = msg_y;
    g_msg_gadget.Width = (WORD)(send_x - g_msg_gadget.LeftEdge - 8);
    if (g_msg_gadget.Width < 80)
        g_msg_gadget.Width = 80;
    if (list_button_x + g_list_button.Width + 4 > send_x) {
        g_join_gadget.Width = (WORD)(send_x - g_join_gadget.LeftEdge - g_join_button.Width - g_list_button.Width - 22);
        if (g_join_gadget.Width < 80)
            g_join_gadget.Width = 80;
        g_join_button.LeftEdge = (WORD)(g_join_gadget.LeftEdge + g_join_gadget.Width + 8);
        g_list_button.LeftEdge = (WORD)(g_join_button.LeftEdge + g_join_button.Width + 6);
    }
}

static void remove_tab(int idx)
{
    int i;

    if (idx <= 0 || idx >= g_tab_count)
        return;
    for (i = idx; i + 1 < g_tab_count; ++i)
        g_tabs[i] = g_tabs[i + 1];
    --g_tab_count;
    if (g_active_tab >= g_tab_count)
        g_active_tab = g_tab_count - 1;
    if (g_active_tab < 0)
        g_active_tab = 0;
}

static void leave_active_channel(void)
{
    int pos = 0;
    char channel[MINI_IRC_CHAN_SIZE];

    if (g_active_tab <= 0 || g_active_tab >= g_tab_count) {
        status_text("Select channel");
        return;
    }

    copy_text(channel, sizeof(channel), g_tabs[g_active_tab].name);
    if (g_gui.connected && channel[0] == '#') {
        g_send_buf[0] = 0;
        if (!append_text(g_send_buf, &pos, sizeof(g_send_buf), "PART ") ||
            !append_text(g_send_buf, &pos, sizeof(g_send_buf), channel) ||
            !mini_irc_session_send_line(&g_gui.session, g_send_buf)) {
            status_text("PART failed");
            return;
        }
    }

    remove_tab(g_active_tab);
    draw_channel_list();
    draw_user_list();
    draw_output();
    status_text(channel[0] == '#' ? "Channel left" : "Chat closed");
}


static void service_whois_interval(void)
{
    if (!g_gui.connected || g_active_tab <= 0 || g_active_tab >= g_tab_count) {
        g_whois_interval_ticks = 0;
        return;
    }
    ++g_whois_interval_ticks;
    if (g_whois_interval_ticks < MINI_IRC_WHOIS_INTERVAL_TICKS)
        return;
    g_whois_interval_ticks = 0;
    request_whois_for_tab(g_active_tab);
}

static void handle_menu(UWORD code)
{
    UWORD menu = MENUNUM(code);
    UWORD item = ITEMNUM(code);

    if (menu == 0) {
        if (item == 0)
            open_connect_dialog();
        else if (item == 1)
            disconnect_irc("Disconnected");
        else if (item == 2)
            g_gui.running = 0;
    } else if (menu == 1) {
        if (item == 0)
            open_connect_dialog();
        else if (item == 1)
            open_font_selector();
        else if (item == 2)
            open_background_selector();
    } else if (menu == 2) {
        if (item == 0)
            open_info_dialog();
    }
}

static int handle_user_list_click(WORD mx, WORD my, ULONG seconds, ULONG micros)
{
    int row;
    struct MiniIrcTab *tab;
    char nick[MINI_IRC_NICK_SIZE];

    if (mx < g_user_x || mx > g_user_x + g_user_w ||
        my < g_list_top || my > g_list_bottom)
        return 0;
    if (g_active_tab < 0 || g_active_tab >= g_tab_count)
        return 1;
    tab = &g_tabs[g_active_tab];
    clamp_user_scroll(tab);
    if (tab->user_count > user_visible_rows()) {
        if (mx >= g_user_up_x && mx <= g_user_up_x + g_user_scroll_w &&
            my >= g_user_up_y && my <= g_user_up_y + g_user_scroll_h) {
            if (g_user_scroll_top > 0)
                --g_user_scroll_top;
            draw_user_list();
            return 1;
        }
        if (mx >= g_user_down_x && mx <= g_user_down_x + g_user_scroll_w &&
            my >= g_user_down_y && my <= g_user_down_y + g_user_scroll_h) {
            if (g_user_scroll_top + user_visible_rows() < tab->user_count)
                ++g_user_scroll_top;
            draw_user_list();
            return 1;
        }
    }
    row = (my - g_list_top) / g_char_h;
    row += g_user_scroll_top;
    if (row < 0 || row >= tab->user_count)
        return 1;
    copy_text(nick, sizeof(nick), tab->users[row]);
    if (text_equal_ci(nick, g_last_user_click_nick) &&
        DoubleClick(g_last_user_click_seconds, g_last_user_click_micros, seconds, micros)) {
        g_last_user_click_nick[0] = 0;
        open_private_chat_tab(nick);
        return 1;
    }
    copy_text(g_last_user_click_nick, sizeof(g_last_user_click_nick), nick);
    g_last_user_click_seconds = seconds;
    g_last_user_click_micros = micros;
    return 1;
}

static void handle_mouse_click(WORD mx, WORD my, ULONG seconds, ULONG micros)
{
    int row;

    if (g_leave_w > 0 &&
        mx >= g_leave_x && mx <= g_leave_x + g_leave_w &&
        my >= g_leave_y && my <= g_leave_y + g_leave_h) {
        leave_active_channel();
        return;
    }
    if (handle_user_list_click(mx, my, seconds, micros))
        return;

    if (mx < g_chan_x || mx > g_chan_x + g_chan_w ||
        my < g_list_top || my > g_list_bottom)
        return;
    row = (my - g_list_top) / g_char_h;
    if (row >= 0 && row < g_tab_count) {
        g_active_tab = row;
        g_tabs[row].unread = 0;
        g_user_scroll_top = 0;
        draw_channel_list();
        draw_user_list();
        draw_output();
    }
}


static UBYTE choose_screen_depth(void)
{
    ULONG chip_free;

    chip_free = AvailMem(MEMF_CHIP);
    g_screen_chip_free = chip_free;
    if (chip_free <= MINI_IRC_CHIPRAM_80K)
        return 2;
    if (chip_free < MINI_IRC_CHIPRAM_160K)
        return 3;
    return 4;
}

static void apply_screen_palette(void)
{
    UWORD colors[16] = {
        0x000, 0xfff, 0xf00, 0x0f0, 0x00f, 0xf0f, 0xff0, 0x0ff,
        0x777, 0xaaa, 0x800, 0x080, 0x008, 0x808, 0x880, 0x088
    };
    struct ViewPort *vp;
    UWORD count;

    if (!g_win)
        return;
    vp = ViewPortAddress(g_win);
    if (!vp)
        return;
    if (g_bg_color_index >= MINI_IRC_BG_COLOR_MAX)
        g_bg_color_index = 0;
    if (g_text_color_index >= MINI_IRC_TEXT_COLOR_MAX)
        g_text_color_index = 0;
    colors[0] = g_bg_colors[g_bg_color_index].rgb;
    colors[1] = g_text_colors[g_text_color_index].rgb;
    count = 1U << g_screen_depth;
    if (count > 16)
        count = 16;
    LoadRGB4(vp, colors, count);
}

static int open_irc_screen(void)
{
    struct NewScreen ns;
    struct Screen *base;

    memset(&ns, 0, sizeof(ns));
    ns.LeftEdge = 0;
    ns.TopEdge = 0;
    ns.Width = 640;
    ns.Height = 256;
    ns.Depth = choose_screen_depth();
    ns.DetailPen = 1;
    ns.BlockPen = 0;
    ns.ViewModes = HIRES;
    ns.Type = CUSTOMSCREEN;
    ns.DefaultTitle = (STRPTR)"MiniIRC Screen";
    if (IntuitionBase) {
        base = IntuitionBase->ActiveScreen;
        if (!base)
            base = IntuitionBase->FirstScreen;
        if (base) {
            ns.Width = base->Width;
            ns.Height = base->Height;
            ns.ViewModes = (base->Width >= 640) ? HIRES : 0;
        }
    }
    if (ns.Width < 320)
        ns.Width = 320;
    if (ns.Height < 200)
        ns.Height = 200;
    while (ns.Depth >= 2) {
        g_screen = OpenScreen(&ns);
        if (g_screen) {
            g_screen_depth = ns.Depth;
            break;
        }
        --ns.Depth;
    }
    return g_screen != 0;
}

static int open_main_window(void)
{
    struct NewWindow nw;

    if (!open_irc_screen())
        return 0;
    memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = 0;
    nw.TopEdge = 0;
    nw.Width = g_screen->Width;
    nw.Height = g_screen->Height;
    nw.DetailPen = 1;
    nw.BlockPen = 0;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_MOUSEBUTTONS |
                    IDCMP_MENUPICK | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE;
    nw.Flags = WFLG_BORDERLESS | WFLG_SMART_REFRESH | WFLG_ACTIVATE;
    nw.Title = (STRPTR)MINI_IRC_GUI_TITLE;
    nw.Screen = g_screen;
    nw.Type = CUSTOMSCREEN;
    g_win = OpenWindow(&nw);
    if (!g_win) {
        if (g_screen) {
            CloseScreen(g_screen);
            g_screen = 0;
        }
        return 0;
    }
    apply_screen_palette();
    g_screen_font = g_win->RPort->Font;
    g_gui_font = g_screen_font;
    install_default_font();
    layout_window();
    setup_main_gadgets();
    AddGList(g_win, &g_join_gadget, -1, 5, 0);
    RefreshGList(&g_join_gadget, g_win, 0, 5);
    setup_menu();
    SetMenuStrip(g_win, &g_menus[0]);
    return 1;
}

static void close_main_window(void)
{
    if (!g_win)
        return;
    ClearMenuStrip(g_win);
    RemoveGList(g_win, &g_join_gadget, 5);
    close_gui_font();
    CloseWindow(g_win);
    g_win = 0;
    if (g_screen) {
        CloseScreen(g_screen);
        g_screen = 0;
    }
}

int main(int argc, char **argv)
{
    struct IntuiMessage *msg;
    ULONG cls;
    UWORD code;
    struct Gadget *gad;

    (void)argc;
    (void)argv;
    debug_open();
    memset(&g_gui, 0, sizeof(g_gui));
    g_gui.fd = -1;
    g_gui.running = 1;
    g_tab_count = 0;
    tab_add("Status");
    g_active_tab = 0;
    load_addrbook();
    load_config();
    if (g_addr_count > 0) {
        copy_text(g_host_buf, sizeof(g_host_buf), g_addrs[0].host);
        copy_text(g_port_buf, sizeof(g_port_buf), g_addrs[0].port);
        copy_text(g_nick_buf, sizeof(g_nick_buf), g_addrs[0].nick);
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 0);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 0);
    if (!IntuitionBase || !GfxBase) {
        debug_log("ERROR", "library open failed");
        if (GfxBase)
            CloseLibrary((struct Library *)GfxBase);
        if (IntuitionBase)
            CloseLibrary((struct Library *)IntuitionBase);
        debug_close();
        return 20;
    }
    if (!open_main_window()) {
        debug_log("ERROR", "main window failed");
        if (GfxBase)
            CloseLibrary((struct Library *)GfxBase);
        if (IntuitionBase)
            CloseLibrary((struct Library *)IntuitionBase);
        debug_close();
        return 20;
    }
    tab_append(0, "Use Project/Connect to open a server.");
    redraw_all();

    while (g_gui.running) {
        while ((msg = (struct IntuiMessage *)GetMsg(g_win->UserPort)) != 0) {
            cls = msg->Class;
            code = msg->Code;
            gad = (struct Gadget *)msg->IAddress;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_CLOSEWINDOW) {
                g_gui.running = 0;
            } else if (cls == IDCMP_REFRESHWINDOW) {
                BeginRefresh(g_win);
                redraw_all();
                EndRefresh(g_win, TRUE);
            } else if (cls == IDCMP_NEWSIZE) {
                layout_window();
                update_main_gadget_positions();
                redraw_all();
            } else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL) {
                    struct MenuItem *item_ptr = ItemAddress(&g_menus[0], code);
                    UWORD next_code = item_ptr ? item_ptr->NextSelect : MENUNULL;
                    handle_menu(code);
                    code = next_code;
                }
            } else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                handle_mouse_click(g_win->MouseX, g_win->MouseY, msg->Seconds, msg->Micros);
            } else if (cls == IDCMP_GADGETUP && gad) {
                if (gad->GadgetID == MINI_IRC_GID_JOIN ||
                    gad->GadgetID == MINI_IRC_GID_JOIN_STR)
                    join_channel();
                else if (gad->GadgetID == MINI_IRC_GID_LIST)
                    open_channel_list_window();
                else if (gad->GadgetID == MINI_IRC_GID_SEND ||
                         gad->GadgetID == MINI_IRC_GID_MSG_STR)
                    send_message();
            }
        }
        poll_socket();
        service_whois_interval();
        Delay(1);
    }

    disconnect_irc("Disconnected");
    close_main_window();
    if (g_gui.socket_base)
        CloseLibrary(g_gui.socket_base);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    debug_close();
    return 0;
}
