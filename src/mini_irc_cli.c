#include <exec/types.h>
#include <exec/libraries.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "amitcp13/bsdsocket.h"
#include "amitcp13/tools/mini_irc_session.h"

#define MINI_IRC_CONFIG_PATH "S:mini_irc.conf"
#define MINI_IRC_HOST_SIZE 128
#define MINI_IRC_NICK_SIZE 32
#define MINI_IRC_USER_SIZE 32
#define MINI_IRC_REAL_SIZE 128
#define MINI_IRC_CHAN_SIZE 64
#define MINI_IRC_RECV_SIZE 2048
#define MINI_IRC_SEND_SIZE 512
#define MINI_IRC_INPUT_SIZE 512
#define MINI_IRC_CONFIG_LINE_SIZE 256
#define MINI_IRC_LOG_PATH "mini_irc.log"
#define MINI_IRC_DEFAULT_PORT 6667
#define MINI_IRC_CONNECT_TIMEOUT 15

#define SOL_SOCKET AMITCP13_SOL_SOCKET
#define SO_ERROR   AMITCP13_SO_ERROR
#define FIONBIO    AMITCP13_FIONBIO

struct MiniIrcConfig
{
    char server[MINI_IRC_HOST_SIZE];
    UWORD port;
    char nick[MINI_IRC_NICK_SIZE];
    char username[MINI_IRC_USER_SIZE];
    char realname[MINI_IRC_REAL_SIZE];
    char channel[MINI_IRC_CHAN_SIZE];
    char message[MINI_IRC_SEND_SIZE];
    char raw_line[MINI_IRC_SEND_SIZE];
    int verbose;
};

struct MiniIrcSocketCtx
{
    struct Library *base;
    int fd;
    struct MiniIrcSession *session;
};

static char g_print_buf[32];
static char g_config_line[MINI_IRC_CONFIG_LINE_SIZE];
static char g_send_buf[MINI_IRC_SEND_SIZE];
static UBYTE g_recv_buf[MINI_IRC_RECV_SIZE];
static struct Amitcp13BsdSockAddrIn g_addr;
static struct Amitcp13BsdFdSet g_read_fds;
static struct Amitcp13BsdFdSet g_write_fds;
static struct Amitcp13BsdTimeVal g_timeout;
static ULONG g_wait_signals;
static char g_input_line[MINI_IRC_INPUT_SIZE];
static int g_input_len;
static char g_irc_rx_buf[MINI_IRC_RECV_SIZE];
static int g_irc_rx_len;
static FILE *g_log_file;
static LONG g_one = 1;
static ULONG g_loop_tick;
static ULONG g_last_rx_tick;
static ULONG g_last_line_tick;
static ULONG g_last_ping_tick;
static ULONG g_last_pong_tick;
static ULONG g_last_idle_watchdog_tick;
static ULONG g_last_probe_tick;

static void print_text(const char *text)
{
    LONG len = 0;

    while (text[len])
        ++len;

    Write(Output(), (APTR)text, len);
}

static void print_number(LONG value)
{
    char tmp[16];
    int i = 0;
    int j = 0;
    LONG n;

    if (value == 0) {
        g_print_buf[i++] = '0';
    } else {
        n = value;
        if (n < 0) {
            g_print_buf[i++] = '-';
            n = -n;
        }
        while (n > 0 && j < 16) {
            tmp[j++] = (char)('0' + (n % 10));
            n /= 10;
        }
        while (j > 0)
            g_print_buf[i++] = tmp[--j];
    }

    g_print_buf[i++] = '\n';
    Write(Output(), g_print_buf, i);
}

static void print_number_inline(LONG value)
{
    char tmp[16];
    int i = 0;
    int j = 0;
    LONG n;

    if (value == 0) {
        g_print_buf[i++] = '0';
    } else {
        n = value;
        if (n < 0) {
            g_print_buf[i++] = '-';
            n = -n;
        }
        while (n > 0 && j < 16) {
            tmp[j++] = (char)('0' + (n % 10));
            n /= 10;
        }
        while (j > 0)
            g_print_buf[i++] = tmp[--j];
    }

    Write(Output(), g_print_buf, i);
}

static void irc_log_open(void)
{
    g_log_file = fopen(MINI_IRC_LOG_PATH, "w");
    if (!g_log_file) {
        print_text("warning: mini_irc.log not opened\n");
        return;
    }

    fputs("PROGRAM START\n", g_log_file);
    fflush(g_log_file);
}

static void irc_log_close(void)
{
    if (!g_log_file)
        return;

    fputs("PROGRAM END\n", g_log_file);
    fflush(g_log_file);
    fclose(g_log_file);
    g_log_file = 0;
}

static void irc_log_text(const char *text)
{
    if (!g_log_file)
        return;

    fputs(text, g_log_file);
    fflush(g_log_file);
}

static void irc_log_number_line(const char *label, LONG value)
{
    if (!g_log_file)
        return;

    fprintf(g_log_file, "%s%ld\n", label, (long)value);
    fflush(g_log_file);
}

static void irc_log_watchdog_state(const struct MiniIrcSession *session,
                                   int select_result,
                                   int read_ready)
{
    if (!session)
        return;

    irc_log_text("RX WATCHDOG STATE\n");
    irc_log_number_line("loop_ticks=", (LONG)g_loop_tick);
    irc_log_number_line("WaitSelect result=", (LONG)select_result);
    irc_log_number_line("read_ready=", (LONG)read_ready);
    irc_log_number_line("recv_calls=", (LONG)session->recv_calls);
    irc_log_number_line("recv_bytes_total=", (LONG)session->recv_bytes);
    irc_log_number_line("lines_total=", (LONG)session->lines_received);
    irc_log_number_line("ewouldblock_count=", (LONG)session->ewouldblock_count);
    irc_log_number_line("last_rx_tick=", (LONG)g_last_rx_tick);
    irc_log_number_line("last_line_tick=", (LONG)g_last_line_tick);
    irc_log_number_line("last_ping_tick=", (LONG)g_last_ping_tick);
    irc_log_number_line("last_pong_tick=", (LONG)g_last_pong_tick);
    irc_log_number_line("rx_buf_len=", (LONG)g_irc_rx_len);
    irc_log_number_line("discard_until_lf=", 0);
}

static void irc_log_line(const char *label, const char *text)
{
    if (!g_log_file)
        return;

    fputs(label, g_log_file);
    if (text)
        fputs(text, g_log_file);
    fputc('\n', g_log_file);
    fflush(g_log_file);
}

static void irc_log_raw(const char *label, const char *data, int len)
{
    if (!g_log_file)
        return;

    fputs(label, g_log_file);
    if (data && len > 0)
        fwrite(data, 1, (size_t)len, g_log_file);
    fputc('\n', g_log_file);
    fflush(g_log_file);
}

static void irc_log_partial_rx(void)
{
    int i;
    int start;

    if (!g_log_file || g_irc_rx_len <= 0)
        return;

    fprintf(g_log_file, "RX partial len=%d first=", g_irc_rx_len);
    for (i = 0; i < g_irc_rx_len && i < 16; ++i) {
        if (g_irc_rx_buf[i] >= ' ' && g_irc_rx_buf[i] <= '~')
            fputc(g_irc_rx_buf[i], g_log_file);
        else
            fputc('.', g_log_file);
    }
    fputs(" last=", g_log_file);
    start = g_irc_rx_len - 16;
    if (start < 0)
        start = 0;
    for (i = start; i < g_irc_rx_len; ++i) {
        if (g_irc_rx_buf[i] >= ' ' && g_irc_rx_buf[i] <= '~')
            fputc(g_irc_rx_buf[i], g_log_file);
        else
            fputc('.', g_log_file);
    }
    fputc('\n', g_log_file);
    fflush(g_log_file);
}

static void irc_log_probe_result(int got, int err)
{
    if (!g_log_file)
        return;

    fprintf(g_log_file, "RX WATCHDOG PROBE recv=%d errno=%d\n", got, err);
    fflush(g_log_file);
}

static int append_text(char *dst, int *pos, int max_len, const char *text)
{
    int i = 0;

    while (text && text[i]) {
        if (*pos >= max_len - 1)
            return 0;
        dst[*pos] = text[i];
        ++(*pos);
        ++i;
    }

    dst[*pos] = '\0';
    return 1;
}

static int copy_limited(char *dst, int max_len, const char *src)
{
    int i = 0;

    if (!dst || max_len <= 0)
        return 0;

    if (!src)
        src = "";

    while (src[i]) {
        if (i >= max_len - 1)
            return 0;
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';
    return 1;
}

static void trim(char *s)
{
    char *p = s;
    int l = (int)strlen(p);

    while (l > 0 && (p[l - 1] == '\n' || p[l - 1] == '\r' ||
                     p[l - 1] == ' ' || p[l - 1] == '\t'))
        p[--l] = 0;

    while (*p == ' ' || *p == '\t')
        ++p, --l;

    if (p != s)
        memmove(s, p, (size_t)l + 1);
}

static int parse_dec_octet(const char **p, UBYTE *out)
{
    ULONG value = 0;
    int digits = 0;
    const char *s = *p;

    while (*s >= '0' && *s <= '9') {
        value = (value * 10) + (ULONG)(*s - '0');
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

    if (!text || !out_ip)
        return 0;

    for (i = 0; i < 4; ++i) {
        if (!parse_dec_octet(&p, &parts[i]))
            return 0;

        if (i != 3) {
            if (*p != '.')
                return 0;
            ++p;
        }
    }

    if (*p != '\0')
        return 0;

    *out_ip = ((ULONG)parts[0] << 24) |
              ((ULONG)parts[1] << 16) |
              ((ULONG)parts[2] << 8) |
              (ULONG)parts[3];
    return 1;
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

static int wait_for_socket(struct Library *base,
                           int fd,
                           int want_write,
                           LONG seconds,
                           int *last_errno)
{
    int result;
    int err;

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
    if (result > 0)
        return 1;

    if (result == 0)
        return 0;

    err = call_errno(base);
    if (last_errno)
        *last_errno = err;
    print_text("WaitSelect failed Errno=");
    print_number(err);
    return 0;
}

static int wait_for_read_logged(struct Library *base,
                                int fd,
                                LONG seconds,
                                int *last_errno,
                                int *select_result,
                                int *read_ready)
{
    int result;
    int err;

    AMITCP13_BSD_FD_ZERO(&g_read_fds);
    AMITCP13_BSD_FD_SET(fd, &g_read_fds);

    g_timeout.tv_sec = seconds;
    g_timeout.tv_usec = 0;
    g_wait_signals = 0;

    result = call_waitselect(base, fd + 1, &g_read_fds, 0, &g_timeout);
    if (select_result)
        *select_result = result;
    if (read_ready)
        *read_ready = (result > 0 && AMITCP13_BSD_FD_ISSET(fd, &g_read_fds)) ? 1 : 0;

    if (result > 0)
        return AMITCP13_BSD_FD_ISSET(fd, &g_read_fds) ? 1 : 0;

    if (result == 0)
        return 0;

    err = call_errno(base);
    if (last_errno)
        *last_errno = err;
    print_text("WaitSelect failed Errno=");
    print_number(err);
    return 0;
}

static int send_all(struct Library *base,
                    int fd,
                    const char *buf,
                    int len,
                    int *last_errno)
{
    int sent_total = 0;
    int sent;
    int err;

    while (sent_total < len) {
        sent = call_send(base, fd, buf + sent_total, len - sent_total, 0);
        if (sent > 0) {
            sent_total += sent;
            continue;
        }

        err = call_errno(base);
        if (err == AMITCP13_EWOULDBLOCK) {
            if (!wait_for_socket(base, fd, 1, MINI_IRC_CONNECT_TIMEOUT, last_errno))
                return 0;
            continue;
        }

        if (last_errno)
            *last_errno = err;
        print_text("send failed Errno=");
        print_number(err);
        return 0;
    }

    return 1;
}

static int data_starts_with(const char *data, int len, const char *prefix)
{
    int i;

    for (i = 0; prefix[i]; ++i) {
        if (i >= len || data[i] != prefix[i])
            return 0;
    }

    return 1;
}

static int send_buffer_is_pong(const char *data, int len)
{
    return data_starts_with(data, len, "PONG");
}

static void log_pong_token_from_raw(const char *data, int len)
{
    int start = 5;
    int end = len;

    if (!send_buffer_is_pong(data, len))
        return;

    if (len < 4)
        return;

    if (len <= start || data[4] != ' ')
        start = 4;

    while (end > start && (data[end - 1] == '\n' || data[end - 1] == '\r'))
        --end;

    irc_log_raw("TX PONG: ", data + start, end - start);
}

static int send_all_debug(struct Library *base,
                          int fd,
                          const char *buf,
                          int len,
                          int *last_errno,
                          int verbose)
{
    int sent_total = 0;
    int sent;
    int err;

    while (sent_total < len) {
        sent = call_send(base, fd, buf + sent_total, len - sent_total, 0);
        if (verbose) {
            print_text("PONG Send returned=");
            print_number((LONG)sent);
        }
        if (sent > 0) {
            sent_total += sent;
            continue;
        }

        err = call_errno(base);
        if (err == AMITCP13_EWOULDBLOCK) {
            if (!wait_for_socket(base, fd, 1, MINI_IRC_CONNECT_TIMEOUT, last_errno))
                return 0;
            continue;
        }

        if (last_errno)
            *last_errno = err;
        if (verbose) {
            print_text("PONG Send Errno=");
            print_number((LONG)err);
        } else {
            print_text("send failed Errno=");
            print_number(err);
        }
        return 0;
    }

    return sent_total;
}

static int irc_socket_send(void *ctx, const char *data, int len)
{
    struct MiniIrcSocketCtx *sock_ctx = (struct MiniIrcSocketCtx *)ctx;
    int *last_errno = 0;
    int is_pong;
    int sent_total;

    if (sock_ctx->session)
        last_errno = &sock_ctx->session->last_errno;

    irc_log_raw("TX RAW: ", data, len);
    is_pong = send_buffer_is_pong(data, len);
    if (is_pong)
        log_pong_token_from_raw(data, len);

    if (is_pong)
        sent_total = send_all_debug(sock_ctx->base,
                                    sock_ctx->fd,
                                    data,
                                    len,
                                    last_errno,
                                    sock_ctx->session && sock_ctx->session->verbose);
    else
        sent_total = send_all(sock_ctx->base, sock_ctx->fd, data, len, last_errno) ? len : 0;

    if (sent_total == len && is_pong) {
        g_last_pong_tick = g_loop_tick;
        if (sock_ctx->session)
            sock_ctx->session->pong_bytes_sent += (unsigned long)sent_total;
        if (sock_ctx->session && sock_ctx->session->verbose) {
            print_text("send_all complete len=");
            print_number((LONG)len);
        }
        wait_for_socket(sock_ctx->base, sock_ctx->fd, 1, 0, last_errno);
    }

    if (sent_total == len && data_starts_with(data, len, "JOIN "))
        irc_log_text("JOIN SENT\n");

    return sent_total == len;
}

static int resolve_server(struct Library *base, const char *server, ULONG *out_ip)
{
    struct hostent *he;

    if (parse_ipv4(server, out_ip))
        return 1;

    print_text("resolving server\n");
    he = call_gethostbyname(base, server);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        print_text("resolve failed Errno=");
        print_number(call_errno(base));
        return 0;
    }

    *out_ip = *(ULONG *)he->h_addr_list[0];
    return 1;
}

static int connect_socket(struct Library *base, int fd, ULONG ip, UWORD port)
{
    int result;
    int err;

    if (fd < 0) {
        print_text("invalid fd before connect\n");
        irc_log_text("invalid fd before connect\n");
        return 0;
    }

    irc_log_number_line("connect() using fd=", (LONG)fd);

    g_addr.sin_len = sizeof(g_addr);
    g_addr.sin_family = AMITCP13_AF_INET;
    g_addr.sin_port = port;
    g_addr.sin_addr.s_addr = ip;

    print_text("connecting\n");
    irc_log_text("TCP blocking connect attempt\n");
    result = call_connect(base,
                          fd,
                          (const struct Amitcp13BsdSockAddr *)&g_addr,
                          sizeof(g_addr));
    if (result < 0) {
        err = call_errno(base);
        print_text("connect failed Errno=");
        print_number(err);
        irc_log_number_line("connect failed Errno=", (LONG)err);
        return 0;
    }

    g_one = 1;
    if (call_ioctl(base, fd, FIONBIO, &g_one) < 0) {
        err = call_errno(base);
        print_text("IoctlSocket FIONBIO failed Errno=");
        print_number(err);
        irc_log_number_line("FIONBIO failed Errno=", (LONG)err);
        return 0;
    }

    irc_log_text("TCP connected\n");
    return 1;
}


static void config_defaults(struct MiniIrcConfig *cfg)
{
    cfg->server[0] = '\0';
    cfg->port = MINI_IRC_DEFAULT_PORT;
    cfg->nick[0] = '\0';
    copy_limited(cfg->username, sizeof(cfg->username), "amitcp13");
    copy_limited(cfg->realname, sizeof(cfg->realname), "amitcp13 AmigaOS 1.3 IRC Client");
    cfg->channel[0] = '\0';
    cfg->message[0] = '\0';
    cfg->raw_line[0] = '\0';
    cfg->verbose = 0;
}

static void config_set(struct MiniIrcConfig *cfg, const char *key, const char *value)
{
    if (!strcmp(key, "server"))
        copy_limited(cfg->server, sizeof(cfg->server), value);
    else if (!strcmp(key, "port"))
        cfg->port = (UWORD)atoi(value);
    else if (!strcmp(key, "nick"))
        copy_limited(cfg->nick, sizeof(cfg->nick), value);
    else if (!strcmp(key, "username"))
        copy_limited(cfg->username, sizeof(cfg->username), value);
    else if (!strcmp(key, "realname"))
        copy_limited(cfg->realname, sizeof(cfg->realname), value);
    else if (!strcmp(key, "channel"))
        copy_limited(cfg->channel, sizeof(cfg->channel), value);
    else if (!strcmp(key, "verbose"))
        cfg->verbose = atoi(value) ? 1 : 0;
}

static void load_config(struct MiniIrcConfig *cfg)
{
    FILE *f;

    f = fopen(MINI_IRC_CONFIG_PATH, "r");
    if (!f)
        return;

    while (fgets(g_config_line, sizeof(g_config_line), f)) {
        char *eq;
        char *key;
        char *value;

        trim(g_config_line);
        if (!g_config_line[0] || g_config_line[0] == '#')
            continue;

        eq = strchr(g_config_line, '=');
        if (!eq)
            continue;

        *eq = 0;
        key = g_config_line;
        value = eq + 1;
        trim(key);
        trim(value);
        config_set(cfg, key, value);
    }

    fclose(f);
}

static void apply_args(struct MiniIrcConfig *cfg, int argc, char **argv)
{
    int i;
    int pos = 0;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-v")) {
            cfg->verbose = 1;
            continue;
        }
        if (!strcmp(argv[i], "--send-raw")) {
            if (i + 1 < argc) {
                ++i;
                copy_limited(cfg->raw_line, sizeof(cfg->raw_line), argv[i]);
            }
            continue;
        }

        if (pos == 0)
            copy_limited(cfg->server, sizeof(cfg->server), argv[i]);
        else if (pos == 1)
            copy_limited(cfg->nick, sizeof(cfg->nick), argv[i]);
        else if (pos == 2)
            copy_limited(cfg->channel, sizeof(cfg->channel), argv[i]);
        else if (pos == 3)
            cfg->port = (UWORD)atoi(argv[i]);
        else if (pos == 4)
            copy_limited(cfg->message, sizeof(cfg->message), argv[i]);

        ++pos;
    }
}

static int build_line2(const char *a, const char *b)
{
    int pos = 0;

    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, a))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, b))
        return 0;

    return pos;
}

static int send_registration(struct MiniIrcSession *session,
                             const struct MiniIrcConfig *cfg)
{
    int pos;

    pos = build_line2("NICK ", cfg->nick);
    if (pos <= 0 || !mini_irc_session_send_line(session, g_send_buf))
        return 0;

    pos = 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, "USER "))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, cfg->username))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, " 0 * :"))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, cfg->realname))
        return 0;
    if (!mini_irc_session_send_line(session, g_send_buf))
        return 0;

    return 1;
}

static const char *skip_spaces_const(const char *p)
{
    while (p && (*p == ' ' || *p == '\t'))
        ++p;
    return p;
}

static int text_starts_with(const char *text, const char *prefix)
{
    int i = 0;

    if (!text || !prefix)
        return 0;

    while (prefix[i]) {
        if (text[i] != prefix[i])
            return 0;
        ++i;
    }

    return 1;
}

static int process_console_command(struct MiniIrcSession *session, const char *line)
{
    const char *p;
    const char *text;
    char target[64];
    int i;

    if (!session || !line || !line[0])
        return 1;

    if (text_starts_with(line, "/quit")) {
        irc_log_watchdog_state(session, 0, 0);
        if (mini_irc_session_send_line(session, "QUIT :bye"))
            session->quit_sent = 1;
        return -1;
    }

    if (text_starts_with(line, "/join")) {
        p = line + 5;
        if (*p && *p != ' ' && *p != '\t') {
            print_text("usage: /join #channel\n");
            return 1;
        }
        p = skip_spaces_const(p);
        if (!p[0]) {
            print_text("usage: /join #channel\n");
            return 1;
        }
        print_text("joining ");
        print_text(p);
        print_text("\n");
        irc_log_watchdog_state(session, 0, 0);
        if (!mini_irc_session_join(session, p)) {
            print_text("join send failed\n");
            return 0;
        }
        return 1;
    }

    if (text_starts_with(line, "/msg ")) {
        p = skip_spaces_const(line + 5);
        i = 0;
        while (p[i] && p[i] != ' ' && p[i] != '\t' && i < (int)sizeof(target) - 1) {
            target[i] = p[i];
            ++i;
        }
        target[i] = '\0';
        text = skip_spaces_const(p + i);
        if (!target[0] || !text[0]) {
            print_text("usage: /msg target text\n");
            return 1;
        }
        if (!mini_irc_session_privmsg(session, target, text)) {
            print_text("message send failed\n");
            return 0;
        }
        irc_log_watchdog_state(session, 0, 0);
        print_text("sent message\n");
        return 1;
    }

    if (text_starts_with(line, "/raw ")) {
        p = skip_spaces_const(line + 5);
        if (!p[0]) {
            print_text("usage: /raw COMMAND\n");
            return 1;
        }
        if (!mini_irc_session_send_line(session, p)) {
            print_text("raw send failed\n");
            return 0;
        }
        irc_log_watchdog_state(session, 0, 0);
        return 1;
    }

    if (!session->channel[0]) {
        print_text("no channel selected\n");
        return 1;
    }

    if (!mini_irc_session_privmsg(session, session->channel, line)) {
        print_text("message send failed\n");
        return 0;
    }
    irc_log_watchdog_state(session, 0, 0);
    print_text("sent message\n");
    return 1;
}

static int poll_console_input(BPTR input_fh,
                              struct MiniIrcSession *session,
                              int verbose)
{
    char c;
    LONG ready;
    LONG got;
    int result;
    int polled = 0;

    if (!input_fh)
        return 1;

    for (;;) {
        if (polled >= 32)
            return 1;

        ready = WaitForChar(input_fh, 0);
        if (!ready)
            return 1;

        if (verbose)
            print_text("input ready\n");

        got = Read(input_fh, &c, 1);
        if (got <= 0)
            return 1;
        ++polled;

        if (c == '\r' || c == '\n') {
            g_input_line[g_input_len] = '\0';
            g_input_len = 0;
            result = process_console_command(session, g_input_line);
            if (result <= 0)
                return result;
            continue;
        }

        if (c == 8 || c == 127) {
            if (g_input_len > 0)
                --g_input_len;
            continue;
        }

        if (g_input_len < (int)sizeof(g_input_line) - 1) {
            g_input_line[g_input_len++] = c;
        } else {
            g_input_len = 0;
            print_text("input line too long, dropped\n");
        }
    }
}

static int check_ctrl_c(void)
{
    ULONG signals;

    signals = SetSignal(0L, 0L);
    if (signals & SIGBREAKF_CTRL_C) {
        SetSignal(0L, SIGBREAKF_CTRL_C);
        print_text("CTRL+C detected\n");
        irc_log_text("CTRL+C detected\n");
        return 1;
    }

    return 0;
}

static void save_session_line(struct MiniIrcSession *session, const char *line)
{
    if (!session || !line)
        return;

    copy_limited(session->last_lines[session->last_line_next],
                 sizeof(session->last_lines[0]),
                 line);
    ++session->last_line_next;
    if (session->last_line_next >= 10)
        session->last_line_next = 0;
}

static const char *irc_command_start_const(const char *line)
{
    const char *p;

    if (!line)
        return "";

    if (line[0] != ':')
        return line;

    p = line;
    while (*p && *p != ' ')
        ++p;
    while (*p == ' ')
        ++p;
    return p;
}

static int irc_command_is_const(const char *line, const char *command)
{
    const char *p = irc_command_start_const(line);
    int i = 0;

    while (command[i]) {
        if (p[i] != command[i])
            return 0;
        ++i;
    }

    return p[i] == '\0' || p[i] == ' ';
}

static int process_irc_line(struct MiniIrcSession *session, char *line)
{
    int was_registered;
    int is_motd_done;

    if (!session || !line)
        return 0;

    if (!line[0])
        return 1;

    irc_log_line("RX LINE: ", line);
    print_text("RX LINE: ");
    print_text(line);
    print_text("\n");
    ++session->lines_received;
    g_last_line_tick = g_loop_tick;
    save_session_line(session, line);

    if (text_starts_with(line, "PING "))
    {
        g_last_ping_tick = g_loop_tick;
        irc_log_line("RX PING token: ", line + 5);
    }

    if (text_starts_with(line, "ERROR")) {
        print_text(line);
        print_text("\n");
        irc_log_line("ERROR: ", line);
    }

    was_registered = session->registered;
    is_motd_done = irc_command_is_const(line, "376") ||
                   irc_command_is_const(line, "422");

    if (!mini_irc_session_handle_line(session, line))
        return 0;

    if (!was_registered && session->registered)
        irc_log_text("REGISTERED (001)\n");

    if (is_motd_done)
        irc_log_text("MOTD DONE (376/422)\n");

    return 1;
}

static int find_crlf_in_rx(void)
{
    int i;

    for (i = 0; i + 1 < g_irc_rx_len; ++i) {
        if (g_irc_rx_buf[i] == '\r' && g_irc_rx_buf[i + 1] == '\n')
            return i;
    }

    return -1;
}

static int process_irc_rx_data(struct MiniIrcSession *session,
                               const char *data,
                               int len)
{
    int line_end;
    int rest;

    if (!session || !data || len < 0)
        return 0;

    if (len == 0)
        return 1;

    if (len > (int)sizeof(g_irc_rx_buf) - g_irc_rx_len) {
        print_text("irc line overflow\n");
        irc_log_text("irc line overflow\n");
        g_irc_rx_len = 0;
        if (len >= (int)sizeof(g_irc_rx_buf))
            return 1;
    }

    memcpy(g_irc_rx_buf + g_irc_rx_len, data, (size_t)len);
    g_irc_rx_len += len;

    for (;;) {
        line_end = find_crlf_in_rx();
        if (line_end < 0)
            break;

        g_irc_rx_buf[line_end] = '\0';
        if (!process_irc_line(session, g_irc_rx_buf))
            return 0;

        rest = g_irc_rx_len - (line_end + 2);
        if (rest > 0)
            memmove(g_irc_rx_buf, g_irc_rx_buf + line_end + 2, (size_t)rest);
        g_irc_rx_len = rest;
    }

    if (g_irc_rx_len >= (int)sizeof(g_irc_rx_buf)) {
        print_text("irc line overflow\n");
        irc_log_text("irc line overflow\n");
        g_irc_rx_len = 0;
    }

    return 1;
}

static void print_irc_stats(const struct MiniIrcSession *session);

static int recv_loop(struct Library *base,
                     int fd,
                     struct MiniIrcSession *session,
                     int verbose)
{
    BPTR input_fh;
    int got;
    int err;
    int stats_waits = 0;
    int console_result;
    int select_result;
    int read_ready;
    int line_idle;

    if (verbose)
        print_text("irc loop starts\n");

    input_fh = Input();
    g_input_len = 0;
    g_irc_rx_len = 0;
    g_loop_tick = 0;
    g_last_rx_tick = 0;
    g_last_line_tick = 0;
    g_last_ping_tick = 0;
    g_last_pong_tick = 0;
    g_last_idle_watchdog_tick = 0;
    g_last_probe_tick = 0;

    for (;;) {
        ++g_loop_tick;
        if (check_ctrl_c())
            return 1;

        console_result = poll_console_input(input_fh, session, verbose);
        if (console_result < 0)
            return 1;
        if (console_result == 0)
            return 0;

        ++session->waitselect_calls;
        select_result = 0;
        read_ready = 0;
        if (!wait_for_read_logged(base, fd, 1, &session->last_errno,
                                  &select_result, &read_ready)) {
            if (verbose && ++stats_waits >= 30) {
                print_irc_stats(session);
                stats_waits = 0;
            }
            if ((g_loop_tick % 30) == 0) {
                irc_log_watchdog_state(session, select_result, read_ready);
            }
            line_idle = (int)(g_loop_tick - g_last_line_tick);
            if (line_idle > 120 &&
                (g_loop_tick - g_last_idle_watchdog_tick) > 30) {
                irc_log_text("RX idle watchdog\n");
                irc_log_number_line("fd=", (LONG)fd);
                irc_log_line("channel=", session->channel);
                irc_log_number_line("WaitSelect result=", (LONG)select_result);
                irc_log_number_line("read ready=", (LONG)read_ready);
                if (g_irc_rx_len > 0)
                    irc_log_partial_rx();
                g_last_idle_watchdog_tick = g_loop_tick;
            }
            if (line_idle > 120 &&
                (g_loop_tick - g_last_probe_tick) > 120) {
                g_last_probe_tick = g_loop_tick;
                got = call_recv(base, fd, g_recv_buf, sizeof(g_recv_buf), 0);
                ++session->recv_calls;
                if (got > 0) {
                    session->recv_bytes += (unsigned long)got;
                    g_last_rx_tick = g_loop_tick;
                    irc_log_probe_result(got, 0);
                    irc_log_text("RX WATCHDOG recovered data\n");
                    irc_log_number_line("RX BYTES: ", (LONG)got);
                    if (verbose)
                        irc_log_raw("RX RAW: ", (const char *)g_recv_buf, got);
                    if (!process_irc_rx_data(session,
                                             (const char *)g_recv_buf,
                                             got))
                        return 0;
                } else if (got == 0) {
                    irc_log_probe_result(got, 0);
                    print_text("connection closed\n");
                    irc_log_text("recv() returned 0\n");
                    return 1;
                } else {
                    err = call_errno(base);
                    irc_log_probe_result(got, err);
                    if (err == AMITCP13_EWOULDBLOCK) {
                        ++session->ewouldblock_count;
                        irc_log_text("RX WATCHDOG no data\n");
                    } else {
                        session->last_errno = err;
                        irc_log_text("recv() error\n");
                        print_text("recv failed Errno=");
                        print_number(err);
                        return 0;
                    }
                }
            }
            console_result = poll_console_input(input_fh, session, verbose);
            if (console_result < 0)
                return 1;
            if (console_result == 0)
                return 0;
            continue;
        }

        if (verbose && ++stats_waits >= 30) {
            print_irc_stats(session);
            stats_waits = 0;
        }

        if (verbose)
            print_text("socket ready\n");

        if ((g_loop_tick % 30) == 0) {
            irc_log_watchdog_state(session, select_result, read_ready);
        }

        for (;;) {
            got = call_recv(base, fd, g_recv_buf, sizeof(g_recv_buf), 0);
            ++session->recv_calls;
            if (got > 0) {
                session->recv_bytes += (unsigned long)got;
                g_last_rx_tick = g_loop_tick;
                irc_log_number_line("RX BYTES: ", (LONG)got);
                if (verbose)
                    irc_log_raw("RX RAW: ", (const char *)g_recv_buf, got);
                if (verbose) {
                    print_text("recv bytes=");
                    print_number(got);
                }
                if (!process_irc_rx_data(session, (const char *)g_recv_buf, got))
                    return 0;
                if (check_ctrl_c())
                    return 1;
                continue;
            }

            if (got == 0) {
                if (verbose)
                    print_text("recv EOF\n");
                print_text("connection closed\n");
                irc_log_text("recv() returned 0\n");
                return 1;
            }

            err = call_errno(base);
            if (err == AMITCP13_EWOULDBLOCK) {
                ++session->ewouldblock_count;
                if (read_ready)
                    irc_log_text("WaitSelect ready but Recv EWOULDBLOCK\n");
                if (verbose)
                    print_text("recv EWOULDBLOCK\n");
                break;
            }

            session->last_errno = err;
            irc_log_text("recv() error\n");
            print_text("recv failed Errno=");
            print_number(err);
            return 0;
        }

        console_result = poll_console_input(input_fh, session, verbose);
        if (console_result < 0)
            return 1;
        if (console_result == 0)
            return 0;
    }
}

static void print_usage(void)
{
    print_text("usage: mini_irc [-v] [server] [nick] [channel] [port] [message]\n");
    print_text("       mini_irc [server] [nick] [channel] [port] --send-raw \"PONG :test\"\n");
    print_text("commands: /quit, /join #chan, /msg target text, /raw COMMAND\n");
    print_text("or create S:mini_irc.conf\n");
}

static void print_counter(const char *label, unsigned long value)
{
    print_text(label);
    print_number((LONG)value);
}

static void print_last_lines(const struct MiniIrcSession *session)
{
    int i;
    int idx;

    print_text("last IRC lines:\n");
    for (i = 0; i < 10; ++i) {
        idx = session->last_line_next + i;
        if (idx >= 10)
            idx -= 10;
        if (session->last_lines[idx][0]) {
            print_text("  ");
            print_text(session->last_lines[idx]);
            print_text("\n");
        }
    }
}

static void print_irc_stats(const struct MiniIrcSession *session)
{
    print_text("stats: lines=");
    print_number_inline((LONG)session->lines_received);
    print_text(" ping_any=");
    print_number_inline((LONG)session->ping_detected_anywhere);
    print_text(" ping_cmd=");
    print_number_inline((LONG)session->ping_command_detected);
    print_text(" pong=");
    print_number_inline((LONG)session->pong_sent);
    print_text(" pong_bytes=");
    print_number_inline((LONG)session->pong_bytes_sent);
    print_text(" recv_bytes=");
    print_number_inline((LONG)session->recv_bytes);
    print_text(" recv_calls=");
    print_number_inline((LONG)session->recv_calls);
    print_text(" waitselect_calls=");
    print_number_inline((LONG)session->waitselect_calls);
    print_text(" ewouldblock_count=");
    print_number_inline((LONG)session->ewouldblock_count);
    print_text(" pong_attempts=");
    print_number_inline((LONG)session->pong_send_attempts);
    print_text(" pong_failures=");
    print_number_inline((LONG)session->pong_send_failures);
    print_text(" last_errno=");
    print_number_inline((LONG)session->last_errno);
    print_text("\n");
    if (session->ping_detected_anywhere == 0)
        print_last_lines(session);
}

int main(int argc, char **argv)
{
    static struct MiniIrcConfig cfg;
    static struct MiniIrcSession session;
    static struct MiniIrcSocketCtx sock_ctx;
    struct Library *base = 0;
    ULONG ip;
    int fd = -1;
    int rc = 20;
    int session_ready = 0;

    irc_log_open();
    config_defaults(&cfg);
    load_config(&cfg);
    apply_args(&cfg, argc, argv);

    if (!cfg.server[0] || !cfg.nick[0]) {
        print_usage();
        irc_log_close();
        return 20;
    }

    base = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 1);
    if (!base) {
        print_text("OpenLibrary bsdsocket failed\n");
        irc_log_close();
        return 20;
    }

    if (!resolve_server(base, cfg.server, &ip))
        goto done;

    fd = call_socket(base,
                     AMITCP13_AF_INET,
                     AMITCP13_SOCK_STREAM,
                     AMITCP13_IPPROTO_TCP);
    irc_log_number_line("socket() returned fd=", (LONG)fd);
    if (fd < 0) {
        irc_log_text("socket() failed\n");
        print_text("socket failed Errno=");
        print_number(call_errno(base));
        goto done;
    }

    if (!connect_socket(base, fd, ip, cfg.port))
        goto done;

    sock_ctx.base = base;
    sock_ctx.fd = fd;
    sock_ctx.session = &session;
    mini_irc_session_init(&session, irc_socket_send, &sock_ctx);
    session.verbose = cfg.verbose;
    if (!mini_irc_session_set_nick(&session, cfg.nick)) {
        print_text("nick too long\n");
        goto done;
    }
    if (!mini_irc_session_set_pending_channel(&session, cfg.channel)) {
        print_text("channel too long\n");
        goto done;
    }
    if (!mini_irc_session_set_pending_one_shot(&session, cfg.message)) {
        print_text("message too long\n");
        goto done;
    }
    session_ready = 1;

    print_text("connected\n");
    if (!send_registration(&session, &cfg)) {
        print_text("registration send failed\n");
        goto done;
    }

    if (cfg.raw_line[0]) {
        print_text("sending raw IRC line\n");
        if (!mini_irc_session_send_line(&session, cfg.raw_line)) {
            print_text("raw send failed\n");
            goto done;
        }
    }

    if (cfg.verbose)
        print_text("receive/PING loop; Ctrl-C the process to exit\n");
    recv_loop(base, fd, &session, cfg.verbose);
    rc = 0;

done:
    if (fd >= 0) {
        if (rc == 0 && !session.quit_sent) {
            irc_log_text("graceful shutdown\n");
            irc_log_text("sending QUIT\n");
            mini_irc_session_send_line(&session, "QUIT :bye");
        }
        irc_log_number_line("CloseSocket(fd)=", (LONG)fd);
        call_close_socket(base, fd);
        fd = -1;
        irc_log_text("socket closed\n");
    }
    if (session_ready && session.verbose) {
        print_counter("recv calls=", session.recv_calls);
        print_counter("recv bytes=", session.recv_bytes);
        print_counter("lines received=", session.lines_received);
        print_counter("waitselect calls=", session.waitselect_calls);
        print_counter("ewouldblock count=", session.ewouldblock_count);
        print_counter("ping detected anywhere=", session.ping_detected_anywhere);
        print_counter("ping command detected=", session.ping_command_detected);
        print_counter("pong send attempts=", session.pong_send_attempts);
        print_counter("pong send success=", session.pong_send_success);
        print_counter("pong sent=", session.pong_sent);
        print_counter("pong bytes sent=", session.pong_bytes_sent);
        print_counter("pong send failures=", session.pong_send_failures);
        print_counter("send failures=", session.send_failures);
        print_counter("last errno=", (unsigned long)session.last_errno);
    }
    if (base) {
        irc_log_text("CloseLibrary\n");
        CloseLibrary(base);
        base = 0;
    }

    irc_log_text("cleanup done\n");
    irc_log_close();
    return rc;
}
