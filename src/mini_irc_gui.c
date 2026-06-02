#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include "amitcp13/bsdsocket.h"
#include "amitcp13/tools/mini_irc_session.h"

#define MINI_IRC_GUI_TITLE "MiniIRC v0.2 by Marcel Jaehne (c)2026"
#define MINI_IRC_ADDRBOOK_PATH "mini_irc.addr"

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
#define MINI_IRC_LEFT_W 112
#define MINI_IRC_RIGHT_W 118
#define MINI_IRC_BOTTOM_H 26
#define MINI_IRC_DEFAULT_PORT 6667
#define MINI_IRC_CONNECT_TIMEOUT 15

#define MINI_IRC_GID_JOIN_STR 1
#define MINI_IRC_GID_JOIN     2
#define MINI_IRC_GID_MSG_STR  3
#define MINI_IRC_GID_SEND     4

#define MINI_IRC_CGID_HOST    20
#define MINI_IRC_CGID_PORT    21
#define MINI_IRC_CGID_NICK    22
#define MINI_IRC_CGID_CONNECT 23
#define MINI_IRC_CGID_SAVE    24
#define MINI_IRC_CGID_CANCEL  25

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
    int user_count;
    int names_receiving;
    int next_line;
    int line_count;
};

struct MiniIrcAddr
{
    char host[MINI_IRC_HOST_SIZE];
    char port[8];
    char nick[MINI_IRC_NICK_SIZE];
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
static struct TextFont *g_gui_font;
static WORD g_char_w = 8;
static WORD g_char_h = 8;
static WORD g_baseline = 7;

static struct MiniIrcGui g_gui;
static struct MiniIrcTab g_tabs[MINI_IRC_MAX_TABS];
static int g_tab_count;
static int g_active_tab;
static struct MiniIrcAddr g_addrs[MINI_IRC_MAX_ADDRS];
static int g_addr_count;

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

static struct Menu g_menus[2];
static struct MenuItem g_project_items[3];
static struct MenuItem g_settings_items[1];
static struct IntuiText g_project_text[3];
static struct IntuiText g_settings_text[1];

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
    clear_rect(4, g_status_y - g_baseline - 2, g_win->Width - 5, g_win->Height - 3);
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
    if (tab->user_count < MINI_IRC_MAX_USERS)
        copy_text(tab->users[tab->user_count++], MINI_IRC_NICK_SIZE, clean);
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
                ++i;
            }
            --tab->user_count;
            return;
        }
    }
}

static void tab_users_clear(int tab_idx)
{
    if (tab_idx < 0 || tab_idx >= g_tab_count)
        return;
    g_tabs[tab_idx].user_count = 0;
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
    max_chars = (g_chan_w - 8) / g_char_w;
    if (max_chars > MINI_IRC_CHAN_SIZE - 1)
        max_chars = MINI_IRC_CHAN_SIZE - 1;
    for (i = 0; i < g_tab_count; ++i) {
        y = (WORD)(g_list_top + g_baseline + 2 + i * g_char_h);
        if (y > g_list_bottom)
            break;
        if (i == g_active_tab) {
            SetAPen(g_win->RPort, 1);
            RectFill(g_win->RPort, (WORD)(g_chan_x + 2),
                     (WORD)(y - g_baseline - 1),
                     (WORD)(g_chan_x + g_chan_w - 3),
                     (WORD)(y + 2));
            SetAPen(g_win->RPort, 0);
        } else {
            SetAPen(g_win->RPort, 1);
        }
        copy_text(tmp, max_chars + 1, g_tabs[i].name);
        Move(g_win->RPort, (WORD)(g_chan_x + 4), y);
        Text(g_win->RPort, (STRPTR)tmp, text_len(tmp));
    }
    SetAPen(g_win->RPort, 1);
}

static void draw_user_list(void)
{
    WORD y;
    int i;
    int max_chars;
    struct MiniIrcTab *tab;
    char tmp[MINI_IRC_NICK_SIZE];

    gui_rp();
    clear_rect(g_user_x, (WORD)(g_win->BorderTop + 2),
               (WORD)(g_user_x + g_user_w - 1), g_list_bottom);
    draw_panel_box(g_user_x, (WORD)(g_win->BorderTop + 2), g_user_w,
                   (WORD)(g_list_bottom - g_win->BorderTop - 1), "Users");
    if (g_active_tab < 0 || g_active_tab >= g_tab_count)
        return;
    tab = &g_tabs[g_active_tab];
    max_chars = (g_user_w - 8) / g_char_w;
    if (max_chars > MINI_IRC_NICK_SIZE - 1)
        max_chars = MINI_IRC_NICK_SIZE - 1;
    for (i = 0; i < tab->user_count; ++i) {
        y = (WORD)(g_list_top + g_baseline + 2 + i * g_char_h);
        if (y > g_list_bottom)
            break;
        copy_text(tmp, max_chars + 1, tab->users[i]);
        draw_text_at((WORD)(g_user_x + 4), y, tmp);
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
    draw_text_at(8, (WORD)(g_input_y + g_baseline + 5), "Join");
    draw_text_at(218, (WORD)(g_input_y + g_baseline + 5), "Input");
}

static void draw_button_window(struct Window *win,
                               WORD x,
                               WORD y,
                               WORD w,
                               WORD h,
                               const char *label);
static void draw_main_buttons(void);

static void draw_main_buttons(void)
{
    gui_rp();
    draw_button_window(g_win, g_join_button.LeftEdge, g_join_button.TopEdge,
                       g_join_button.Width, g_join_button.Height, "Join");
    draw_button_window(g_win, g_send_gadget.LeftEdge, g_send_gadget.TopEdge,
                       g_send_gadget.Width, g_send_gadget.Height, "Send");
}

static void draw_output(void)
{
    int visible_rows;
    int start;
    int line_index;
    int row;
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
    if (visible_rows > tab->line_count)
        visible_rows = tab->line_count;
    start = tab->next_line - visible_rows;
    if (start < 0)
        start += MINI_IRC_TAB_LINES;
    max_chars = (g_term_w - 8) / g_char_w;
    if (max_chars > MINI_IRC_LINE_SIZE - 1)
        max_chars = MINI_IRC_LINE_SIZE - 1;
    for (row = 0; row < visible_rows; ++row) {
        line_index = (start + row) % MINI_IRC_TAB_LINES;
        copy_text(tmp, max_chars + 1, tab->lines[line_index]);
        draw_text_at((WORD)(g_term_x + 4),
                     (WORD)(g_term_y + g_char_h + g_baseline + 2 + row * g_char_h),
                     tmp);
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

static int wait_for_socket(struct Library *base, int fd, int want_write, LONG seconds)
{
    int result;

    AMITCP13_BSD_FD_ZERO(&g_read_fds);
    AMITCP13_BSD_FD_ZERO(&g_write_fds);
    if (want_write)
        AMITCP13_BSD_FD_SET(fd, &g_write_fds);
    else
        AMITCP13_BSD_FD_SET(fd, &g_read_fds);
    g_timeout.tv_sec = seconds;
    g_timeout.tv_usec = 0;
    g_wait_signals = 0;
    result = call_waitselect(base,
                             fd + 1,
                             want_write ? 0 : &g_read_fds,
                             want_write ? &g_write_fds : 0,
                             &g_timeout);
    return result > 0;
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
        if (err == AMITCP13_EWOULDBLOCK) {
            if (!wait_for_socket(base, fd, 1, MINI_IRC_CONNECT_TIMEOUT))
                return 0;
            continue;
        }
        return 0;
    }
    return 1;
}

static int irc_socket_send(void *ctx, const char *data, int len)
{
    struct MiniIrcSocketCtx *sock_ctx = (struct MiniIrcSocketCtx *)ctx;

    if (!sock_ctx || !sock_ctx->base || sock_ctx->fd < 0)
        return 0;
    return send_all(sock_ctx->base, sock_ctx->fd, data, len);
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

static void disconnect_irc(const char *reason)
{
    if (g_gui.connected)
        mini_irc_session_send_line(&g_gui.session, "QUIT :bye");
    if (g_gui.fd >= 0 && g_gui.socket_base)
        call_close_socket(g_gui.socket_base, g_gui.fd);
    g_gui.fd = -1;
    g_gui.connected = 0;
    g_gui.sock_ctx.fd = -1;
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
    if (idx >= 0)
        g_tabs[idx].names_receiving = 0;
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
        append_text(out, &pos, sizeof(out), "<");
        append_text(out, &pos, sizeof(out), nick);
        append_text(out, &pos, sizeof(out), "> ");
        append_text(out, &pos, sizeof(out), payload);
        idx = (chan[0] == '#') ? tab_find(chan) : 0;
        if (idx < 0)
            idx = tab_add(chan);
        if (idx < 0)
            idx = 0;
        tab_append(idx, out);
        if (idx == g_active_tab)
            draw_output();
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
            if (g_rx_len > 0)
                route_line_to_tab(g_rx_line);
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

    if (!g_gui.connected || g_gui.fd < 0)
        return;
    AMITCP13_BSD_FD_ZERO(&g_read_fds);
    AMITCP13_BSD_FD_SET(g_gui.fd, &g_read_fds);
    g_timeout.tv_sec = 0;
    g_timeout.tv_usec = 0;
    g_wait_signals = 0;
    result = call_waitselect(g_gui.socket_base, g_gui.fd + 1, &g_read_fds, 0, &g_timeout);
    if (result <= 0 || !AMITCP13_BSD_FD_ISSET(g_gui.fd, &g_read_fds))
        return;
    for (;;) {
        got = call_recv(g_gui.socket_base, g_gui.fd, g_recv_buf, sizeof(g_recv_buf), 0);
        if (got > 0) {
            process_rx_bytes((const char *)g_recv_buf, got);
            continue;
        }
        if (got == 0) {
            disconnect_irc("Connection closed");
            return;
        }
        err = call_errno(g_gui.socket_base);
        if (err == AMITCP13_EWOULDBLOCK || err == AMITCP13_EAGAIN)
            return;
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

static void send_message(void)
{
    const char *target;
    char local[MINI_IRC_MSG_SIZE];
    int pos = 0;

    if (!g_gui.connected) {
        status_text("Not connected");
        return;
    }
    if (g_active_tab <= 0 || g_active_tab >= g_tab_count) {
        status_text("Select channel tab");
        return;
    }
    copy_text(local, sizeof(local), g_msg_buf);
    trim_text(local);
    if (!local[0])
        return;
    target = g_tabs[g_active_tab].name;
    if (!mini_irc_session_privmsg(&g_gui.session, target, local)) {
        status_text("Send failed");
        return;
    }
    append_text(g_send_buf, &pos, sizeof(g_send_buf), "<");
    append_text(g_send_buf, &pos, sizeof(g_send_buf), g_nick_buf);
    append_text(g_send_buf, &pos, sizeof(g_send_buf), "> ");
    append_text(g_send_buf, &pos, sizeof(g_send_buf), local);
    tab_append(g_active_tab, g_send_buf);
    g_msg_buf[0] = 0;
    g_msg_si.BufferPos = 0;
    g_msg_si.NumChars = 0;
    RefreshGList(&g_msg_gadget, g_win, 0, 1);
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

static void add_current_to_addrbook(void)
{
    int i;
    int slot = -1;

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

static void draw_connect_dialog(struct Window *win, int selected)
{
    int i;
    WORD y;

    SetAPen(win->RPort, 0);
    RectFill(win->RPort, 0, 0, win->Width - 1, win->Height - 1);
    SetAPen(win->RPort, 1);
    SetBPen(win->RPort, 0);
    SetDrMd(win->RPort, JAM2);
    Move(win->RPort, 12, 26);
    Text(win->RPort, (STRPTR)"Host", 4);
    Move(win->RPort, 12, 54);
    Text(win->RPort, (STRPTR)"Port", 4);
    Move(win->RPort, 156, 54);
    Text(win->RPort, (STRPTR)"Nick", 4);
    Move(win->RPort, 12, 84);
    Text(win->RPort, (STRPTR)"Address book", 12);
    for (i = 0; i < g_addr_count; ++i) {
        y = (WORD)(100 + i * 12);
        if (i == selected) {
            SetAPen(win->RPort, 1);
            RectFill(win->RPort, 10, y - 9, win->Width - 12, y + 2);
            SetAPen(win->RPort, 0);
        } else {
            SetAPen(win->RPort, 1);
        }
        Move(win->RPort, 14, y);
        Text(win->RPort, (STRPTR)g_addrs[i].host, text_len(g_addrs[i].host));
        Move(win->RPort, 164, y);
        Text(win->RPort, (STRPTR)g_addrs[i].port, text_len(g_addrs[i].port));
        Move(win->RPort, 218, y);
        Text(win->RPort, (STRPTR)g_addrs[i].nick, text_len(g_addrs[i].nick));
    }
    draw_button_window(win, 12, 210, 70, 18, "Connect");
    draw_button_window(win, 92, 210, 58, 18, "Save");
    draw_button_window(win, 160, 210, 64, 18, "Cancel");
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
    static struct Gadget cancel_gad;

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
    memset(&cancel_gad, 0, sizeof(cancel_gad));
    host_gad.NextGadget = &port_gad;
    host_gad.LeftEdge = 52;
    host_gad.TopEdge = 14;
    host_gad.Width = 232;
    host_gad.Height = 16;
    host_gad.Activation = GACT_RELVERIFY;
    host_gad.GadgetType = GTYP_STRGADGET;
    host_gad.SpecialInfo = &host_si;
    host_gad.GadgetID = MINI_IRC_CGID_HOST;
    port_gad.NextGadget = &nick_gad;
    port_gad.LeftEdge = 52;
    port_gad.TopEdge = 42;
    port_gad.Width = 78;
    port_gad.Height = 16;
    port_gad.Activation = GACT_RELVERIFY;
    port_gad.GadgetType = GTYP_STRGADGET;
    port_gad.SpecialInfo = &port_si;
    port_gad.GadgetID = MINI_IRC_CGID_PORT;
    nick_gad.NextGadget = &connect_gad;
    nick_gad.LeftEdge = 198;
    nick_gad.TopEdge = 42;
    nick_gad.Width = 86;
    nick_gad.Height = 16;
    nick_gad.Activation = GACT_RELVERIFY;
    nick_gad.GadgetType = GTYP_STRGADGET;
    nick_gad.SpecialInfo = &nick_si;
    nick_gad.GadgetID = MINI_IRC_CGID_NICK;
    connect_gad.NextGadget = &save_gad;
    connect_gad.LeftEdge = 12;
    connect_gad.TopEdge = 210;
    connect_gad.Width = 70;
    connect_gad.Height = 18;
    connect_gad.Flags = GFLG_GADGHCOMP;
    connect_gad.Activation = GACT_RELVERIFY;
    connect_gad.GadgetType = GTYP_BOOLGADGET;
    connect_gad.GadgetID = MINI_IRC_CGID_CONNECT;
    save_gad.NextGadget = &cancel_gad;
    save_gad.LeftEdge = 92;
    save_gad.TopEdge = 210;
    save_gad.Width = 58;
    save_gad.Height = 18;
    save_gad.Flags = GFLG_GADGHCOMP;
    save_gad.Activation = GACT_RELVERIFY;
    save_gad.GadgetType = GTYP_BOOLGADGET;
    save_gad.GadgetID = MINI_IRC_CGID_SAVE;
    cancel_gad.LeftEdge = 160;
    cancel_gad.TopEdge = 210;
    cancel_gad.Width = 64;
    cancel_gad.Height = 18;
    cancel_gad.Flags = GFLG_GADGHCOMP;
    cancel_gad.Activation = GACT_RELVERIFY;
    cancel_gad.GadgetType = GTYP_BOOLGADGET;
    cancel_gad.GadgetID = MINI_IRC_CGID_CANCEL;

    memset(&nw, 0, sizeof(nw));
    if (g_screen) {
        nw.LeftEdge = (WORD)((g_screen->Width - 312) / 2);
        nw.TopEdge = (WORD)((g_screen->Height - 248) / 2);
        if (nw.LeftEdge < 0)
            nw.LeftEdge = 0;
        if (nw.TopEdge < 12)
            nw.TopEdge = 12;
    } else {
        nw.LeftEdge = 30;
        nw.TopEdge = 24;
    }
    nw.Width = 312;
    nw.Height = 248;
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
                if (my >= 91 && my < 91 + g_addr_count * 12) {
                    selected = (my - 91) / 12;
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
                } else if (gad->GadgetID == MINI_IRC_CGID_CANCEL) {
                    done = 1;
                }
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
    setup_menu_text(&g_project_text[0], "Connect");
    setup_menu_text(&g_project_text[1], "Disconnect");
    setup_menu_text(&g_project_text[2], "Quit");
    setup_menu_text(&g_settings_text[0], "Address Book...");

    g_menus[0].NextMenu = &g_menus[1];
    g_menus[0].LeftEdge = 0;
    g_menus[0].TopEdge = 0;
    g_menus[0].Width = 64;
    g_menus[0].Height = 10;
    g_menus[0].Flags = MENUENABLED;
    g_menus[0].MenuName = (STRPTR)"Project";
    g_menus[0].FirstItem = &g_project_items[0];
    g_menus[1].LeftEdge = 74;
    g_menus[1].TopEdge = 0;
    g_menus[1].Width = 70;
    g_menus[1].Height = 10;
    g_menus[1].Flags = MENUENABLED;
    g_menus[1].MenuName = (STRPTR)"Settings";
    g_menus[1].FirstItem = &g_settings_items[0];

    g_project_items[0].NextItem = &g_project_items[1];
    g_project_items[1].NextItem = &g_project_items[2];
    g_project_items[0].ItemFill = &g_project_text[0];
    g_project_items[1].ItemFill = &g_project_text[1];
    g_project_items[2].ItemFill = &g_project_text[2];
    g_settings_items[0].ItemFill = &g_settings_text[0];
    g_project_items[0].TopEdge = 0;
    g_project_items[1].TopEdge = 10;
    g_project_items[2].TopEdge = 20;
    g_settings_items[0].TopEdge = 0;
    g_project_items[0].Width = 92;
    g_project_items[1].Width = 92;
    g_project_items[2].Width = 92;
    g_settings_items[0].Width = 132;
    g_project_items[0].Height = 10;
    g_project_items[1].Height = 10;
    g_project_items[2].Height = 10;
    g_settings_items[0].Height = 10;
    g_project_items[0].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    g_project_items[1].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    g_project_items[2].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    g_settings_items[0].Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
}

static void update_main_gadget_positions(void);

static void setup_main_gadgets(void)
{
    memset(&g_join_si, 0, sizeof(g_join_si));
    memset(&g_msg_si, 0, sizeof(g_msg_si));
    memset(&g_join_gadget, 0, sizeof(g_join_gadget));
    memset(&g_join_button, 0, sizeof(g_join_button));
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
    g_join_button.NextGadget = &g_msg_gadget;
    g_join_button.Width = 52;
    g_join_button.Height = 16;
    g_join_button.Flags = GFLG_GADGHCOMP;
    g_join_button.Activation = GACT_RELVERIFY;
    g_join_button.GadgetType = GTYP_BOOLGADGET;
    g_join_button.GadgetID = MINI_IRC_GID_JOIN;
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
    WORD y = (WORD)(g_input_y + 2);
    WORD send_x;

    g_join_gadget.LeftEdge = 42;
    g_join_gadget.TopEdge = y;
    g_join_gadget.Width = 128;
    g_join_button.LeftEdge = 176;
    g_join_button.TopEdge = y;
    g_msg_gadget.LeftEdge = 268;
    g_msg_gadget.TopEdge = y;
    send_x = (WORD)(w - 52);
    g_send_gadget.LeftEdge = send_x;
    g_send_gadget.TopEdge = y;
    g_msg_gadget.Width = (WORD)(send_x - g_msg_gadget.LeftEdge - 8);
    if (g_msg_gadget.Width < 80)
        g_msg_gadget.Width = 80;
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
    }
}

static void handle_mouse_click(WORD mx, WORD my)
{
    int row;

    if (mx < g_chan_x || mx > g_chan_x + g_chan_w ||
        my < g_list_top || my > g_list_bottom)
        return;
    row = (my - g_list_top) / g_char_h;
    if (row >= 0 && row < g_tab_count) {
        g_active_tab = row;
        draw_channel_list();
        draw_user_list();
        draw_output();
    }
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
    ns.Depth = 2;
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
    g_screen = OpenScreen(&ns);
    if (!g_screen && ns.Depth > 1) {
        ns.Depth = 1;
        g_screen = OpenScreen(&ns);
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
    g_gui_font = g_win->RPort->Font;
    if (g_gui_font) {
        g_char_w = g_gui_font->tf_XSize;
        g_char_h = g_gui_font->tf_YSize;
        g_baseline = g_gui_font->tf_Baseline;
    }
    layout_window();
    setup_main_gadgets();
    AddGList(g_win, &g_join_gadget, -1, 4, 0);
    RefreshGList(&g_join_gadget, g_win, 0, 4);
    setup_menu();
    SetMenuStrip(g_win, &g_menus[0]);
    return 1;
}

static void close_main_window(void)
{
    if (!g_win)
        return;
    ClearMenuStrip(g_win);
    RemoveGList(g_win, &g_join_gadget, 4);
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
    memset(&g_gui, 0, sizeof(g_gui));
    g_gui.fd = -1;
    g_gui.running = 1;
    g_tab_count = 0;
    tab_add("Status");
    g_active_tab = 0;
    load_addrbook();
    if (g_addr_count > 0) {
        copy_text(g_host_buf, sizeof(g_host_buf), g_addrs[0].host);
        copy_text(g_port_buf, sizeof(g_port_buf), g_addrs[0].port);
        copy_text(g_nick_buf, sizeof(g_nick_buf), g_addrs[0].nick);
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 0);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 0);
    if (!IntuitionBase || !GfxBase)
        return 20;
    if (!open_main_window())
        return 20;
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
                handle_mouse_click(g_win->MouseX, g_win->MouseY);
            } else if (cls == IDCMP_GADGETUP && gad) {
                if (gad->GadgetID == MINI_IRC_GID_JOIN)
                    join_channel();
                else if (gad->GadgetID == MINI_IRC_GID_SEND)
                    send_message();
            }
        }
        poll_socket();
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
    return 0;
}
