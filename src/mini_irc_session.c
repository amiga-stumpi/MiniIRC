#include <exec/types.h>
#include <proto/dos.h>

#include "amitcp13/tools/mini_irc_session.h"

#define MINI_IRC_SEND_SIZE 512

static char g_send_buf[MINI_IRC_SEND_SIZE];

static void debug_event(struct MiniIrcSession *session, const char *tag, const char *text)
{
    if (session && session->debug_fn)
        session->debug_fn(session->debug_ctx, tag, text ? text : "");
}

static void print_text(const char *text)
{
    LONG len = 0;

    while (text[len])
        ++len;

    Write(Output(), (APTR)text, len);
}

static void print_number(LONG value)
{
    char out[16];
    char tmp[16];
    int i = 0;
    int j = 0;
    LONG n;

    if (value == 0) {
        out[i++] = '0';
    } else {
        n = value;
        if (n < 0) {
            out[i++] = '-';
            n = -n;
        }
        while (n > 0 && j < 16) {
            tmp[j++] = (char)('0' + (n % 10));
            n /= 10;
        }
        while (j > 0)
            out[i++] = tmp[--j];
    }

    out[i++] = '\n';
    Write(Output(), out, i);
}

static void print_escaped_line(const char *label, const char *text, int len)
{
    int i;
    char c;

    print_text(label);
    for (i = 0; i < len; ++i) {
        c = text[i];
        if (c == '\r')
            print_text("<CR>");
        else if (c == '\n')
            print_text("<LF>");
        else
            Write(Output(), &c, 1);
    }
    print_text("\n");
}

static char upper_char(char c)
{
    if (c >= 'a' && c <= 'z')
        return (char)(c - ('a' - 'A'));
    return c;
}

static int text_contains_ci(const char *text, const char *needle)
{
    int i;
    int j;

    if (!text || !needle || !needle[0])
        return 0;

    for (i = 0; text[i]; ++i) {
        for (j = 0; needle[j]; ++j) {
            if (!text[i + j])
                break;
            if (upper_char(text[i + j]) != upper_char(needle[j]))
                break;
        }
        if (!needle[j])
            return 1;
    }

    return 0;
}

static const char *skip_spaces(const char *p)
{
    while (p && *p == ' ')
        ++p;
    return p;
}

static int token_equals_ci(const char *token, int token_len, const char *match)
{
    int i;

    if (!token || !match)
        return 0;

    for (i = 0; i < token_len; ++i) {
        if (!match[i])
            return 0;
        if (upper_char(token[i]) != upper_char(match[i]))
            return 0;
    }

    return match[token_len] == '\0';
}

static void parse_irc_line(const char *line,
                           const char **prefix,
                           int *prefix_len,
                           const char **command,
                           int *command_len,
                           const char **payload)
{
    const char *p = line;
    const char *start;

    *prefix = "";
    *prefix_len = 0;
    *command = "";
    *command_len = 0;
    *payload = "";

    if (!p)
        return;

    if (*p == ':') {
        ++p;
        start = p;
        while (*p && *p != ' ')
            ++p;
        *prefix = start;
        *prefix_len = (int)(p - start);
        p = skip_spaces(p);
    }

    start = p;
    while (*p && *p != ' ')
        ++p;
    *command = start;
    *command_len = (int)(p - start);
    *payload = skip_spaces(p);
}

static void print_range(const char *label, const char *text, int len)
{
    int i;

    print_text(label);
    if (!text || len <= 0) {
        print_text("(none)\n");
        return;
    }

    for (i = 0; i < len; ++i)
        Write(Output(), (APTR)(text + i), 1);
    print_text("\n");
}

static void print_raw_range(const char *text, int len)
{
    int i;

    if (!text || len <= 0)
        return;

    for (i = 0; i < len; ++i)
        Write(Output(), (APTR)(text + i), 1);
}

static const char *irc_command_start(const char *line)
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

static int irc_command_is(const char *line, const char *command)
{
    const char *p = irc_command_start(line);
    int i = 0;

    while (command[i]) {
        if (upper_char(p[i]) != upper_char(command[i]))
            return 0;
        ++i;
    }

    return p[i] == '\0' || p[i] == ' ';
}

static int irc_line_visible(const char *line)
{
    return irc_command_is(line, "ERROR") ||
           irc_command_is(line, "001");
}

static int irc_line_fatal(const char *line)
{
    return irc_command_is(line, "ERROR") ||
           text_contains_ci(line, "Closing Link") ||
           text_contains_ci(line, "Ping timeout");
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

static void save_last_line(struct MiniIrcSession *session, const char *line)
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

static void print_last_lines(struct MiniIrcSession *session)
{
    int i;
    int idx;

    if (!session)
        return;

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

static void print_last_pong(struct MiniIrcSession *session)
{
    if (!session || session->last_pong_len <= 0)
        return;

    print_escaped_line("last PONG: ", session->last_pong, session->last_pong_len);
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

static int append_number(char *dst, int *pos, int max_len, int value)
{
    char tmp[12];
    int i = 0;
    int j = 0;

    if (value == 0) {
        if (*pos >= max_len - 1)
            return 0;
        dst[*pos] = '0';
        ++(*pos);
        dst[*pos] = '\0';
        return 1;
    }

    while (value > 0 && j < (int)sizeof(tmp)) {
        tmp[j++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (j > 0) {
        if (*pos >= max_len - 1)
            return 0;
        dst[*pos] = tmp[--j];
        ++(*pos);
        ++i;
    }

    dst[*pos] = '\0';
    return i > 0;
}

static void nick_from_prefix(const char *prefix, int prefix_len, const char **nick, int *nick_len)
{
    int i;

    *nick = prefix ? prefix : "";
    *nick_len = prefix_len;

    for (i = 0; i < prefix_len; ++i) {
        if (prefix[i] == '!') {
            *nick_len = i;
            return;
        }
    }
}

static void split_params(const char *payload,
                         const char **params,
                         int *params_len,
                         const char **trailing)
{
    const char *p;

    *params = payload ? payload : "";
    *params_len = 0;
    *trailing = "";

    if (!payload)
        return;

    p = payload;
    while (*p) {
        if (p[0] == ':' && (p == payload || p[-1] == ' ')) {
            *trailing = p + 1;
            if (p > payload && p[-1] == ' ')
                --p;
            *params_len = (int)(p - payload);
            return;
        }
        ++p;
    }

    *params_len = (int)(p - payload);
}

static void first_param(const char *params,
                        int params_len,
                        const char **param,
                        int *param_len)
{
    int i = 0;

    *param = "";
    *param_len = 0;

    if (!params || params_len <= 0)
        return;

    while (i < params_len && params[i] == ' ')
        ++i;
    *param = params + i;
    while (i < params_len && params[i] != ' ')
        ++i;
    *param_len = (int)((params + i) - *param);
}

static int param_equals_text_ci(const char *param, int param_len, const char *text)
{
    int i;

    if (!param || !text)
        return 0;

    for (i = 0; i < param_len; ++i) {
        if (!text[i])
            return 0;
        if (upper_char(param[i]) != upper_char(text[i]))
            return 0;
    }

    return text[param_len] == '\0';
}

static int send_pending_one_shot(struct MiniIrcSession *session)
{
    if (!session || !session->join_confirmed)
        return 1;

    if (!session->one_shot_sent &&
        session->pending_one_shot_message[0] &&
        session->channel[0]) {
        print_text("sending one-shot message\n");
        if (!mini_irc_session_privmsg(session,
                                      session->channel,
                                      session->pending_one_shot_message))
            return 0;
        session->one_shot_sent = 1;
    }

    return 1;
}

static int confirm_join(struct MiniIrcSession *session,
                        const char *channel,
                        int channel_len)
{
    if (!session || channel_len <= 0)
        return 1;

    session->join_confirmed = 1;

    if (!session->join_reported) {
        print_text("joined ");
        print_raw_range(channel, channel_len);
        print_text("\n");
        session->join_reported = 1;
    }

    return send_pending_one_shot(session);
}

static void print_privmsg(struct MiniIrcSession *session,
                          const char *prefix,
                          int prefix_len,
                          const char *payload)
{
    const char *trailing;
    const char *target;
    const char *nick;
    int target_len;
    int nick_len;
    const char *p;

    p = skip_spaces(payload);
    target = p;
    while (*p && *p != ' ')
        ++p;
    target_len = (int)(p - target);
    p = skip_spaces(p);
    if (*p == ':')
        trailing = p + 1;
    else
        trailing = p;

    nick_from_prefix(prefix, prefix_len, &nick, &nick_len);

    if (target_len <= 0)
        return;

    if (session->verbose) {
        print_text("PRIVMSG parsed:\n");
        print_range("prefix=", prefix, prefix_len);
        print_range("nick=", nick, nick_len);
        print_range("target=", target, target_len);
        print_text("text=");
        print_text(trailing);
        print_text("\n");
    }

    if (param_equals_text_ci(target, target_len, session->nick)) {
        print_text("[PM ");
        print_raw_range(nick, nick_len);
        print_text("] ");
    } else {
        print_text("[");
        print_raw_range(target, target_len);
        print_text("] <");
        print_raw_range(nick, nick_len);
        print_text("> ");
    }

    print_text(trailing);
    print_text("\n");
}

static int print_join(struct MiniIrcSession *session,
                      const char *prefix,
                      int prefix_len,
                      const char *payload)
{
    const char *params;
    const char *trailing;
    const char *channel;
    const char *nick;
    int params_len;
    int channel_len;
    int nick_len;

    split_params(payload, &params, &params_len, &trailing);
    first_param(params, params_len, &channel, &channel_len);
    if (channel_len <= 0 && trailing[0]) {
        channel = trailing;
        channel_len = 0;
        while (channel[channel_len])
            ++channel_len;
    }

    nick_from_prefix(prefix, prefix_len, &nick, &nick_len);
    print_text("*** ");
    print_raw_range(nick, nick_len);
    print_text(" joined ");
    print_raw_range(channel, channel_len);
    print_text("\n");

    if (param_equals_text_ci(nick, nick_len, session->nick))
        return confirm_join(session, channel, channel_len);

    return 1;
}

static void print_part(const char *prefix, int prefix_len, const char *payload)
{
    const char *params;
    const char *trailing;
    const char *channel;
    const char *nick;
    int params_len;
    int channel_len;
    int nick_len;

    split_params(payload, &params, &params_len, &trailing);
    first_param(params, params_len, &channel, &channel_len);
    nick_from_prefix(prefix, prefix_len, &nick, &nick_len);

    print_text("*** ");
    print_raw_range(nick, nick_len);
    print_text(" left ");
    print_raw_range(channel, channel_len);
    if (trailing[0]) {
        print_text(" (");
        print_text(trailing);
        print_text(")");
    }
    print_text("\n");
}

static void print_quit(const char *prefix, int prefix_len, const char *payload)
{
    const char *params;
    const char *trailing;
    const char *nick;
    int params_len;
    int nick_len;

    split_params(payload, &params, &params_len, &trailing);
    nick_from_prefix(prefix, prefix_len, &nick, &nick_len);

    print_text("*** ");
    print_raw_range(nick, nick_len);
    print_text(" quit");
    if (trailing[0]) {
        print_text(" (");
        print_text(trailing);
        print_text(")");
    }
    print_text("\n");
}

static void print_nick(const char *prefix, int prefix_len, const char *payload)
{
    const char *params;
    const char *trailing;
    const char *nick;
    int params_len;
    int nick_len;

    split_params(payload, &params, &params_len, &trailing);
    nick_from_prefix(prefix, prefix_len, &nick, &nick_len);

    print_text("*** ");
    print_raw_range(nick, nick_len);
    print_text(" is now ");
    if (trailing[0])
        print_text(trailing);
    else
        print_raw_range(params, params_len);
    print_text("\n");
}

static void print_topic(const char *payload)
{
    const char *params;
    const char *trailing;
    int params_len;

    split_params(payload, &params, &params_len, &trailing);
    print_text("*** topic: ");
    print_text(trailing[0] ? trailing : payload);
    print_text("\n");
}

static void print_names(const char *payload)
{
    const char *params;
    const char *trailing;
    int params_len;

    split_params(payload, &params, &params_len, &trailing);
    print_text("*** names: ");
    print_text(trailing);
    print_text("\n");
}

static int handle_end_of_names(struct MiniIrcSession *session, const char *payload)
{
    const char *params;
    const char *trailing;
    const char *first;
    const char *channel;
    int params_len;
    int first_len;
    int channel_len;
    int i;

    split_params(payload, &params, &params_len, &trailing);
    first_param(params, params_len, &first, &first_len);

    i = first_len;
    while (i < params_len && params[i] == ' ')
        ++i;
    channel = params + i;
    while (i < params_len && params[i] != ' ')
        ++i;
    channel_len = (int)((params + i) - channel);

    if (channel_len <= 0 && session->channel[0]) {
        channel = session->channel;
        channel_len = 0;
        while (channel[channel_len])
            ++channel_len;
    }

    return confirm_join(session, channel, channel_len);
}

static int make_fallback_nick(struct MiniIrcSession *session)
{
    int pos = 0;

    if (!session)
        return 0;

    ++session->nick_fallback_count;

    if (session->nick_fallback_count == 1) {
        if (!append_text(session->nick, &pos, sizeof(session->nick), session->base_nick))
            return 0;
        if (!append_text(session->nick, &pos, sizeof(session->nick), "_"))
            return 0;
        return 1;
    }

    if (!append_text(session->nick, &pos, sizeof(session->nick), session->base_nick))
        return 0;
    if (!append_number(session->nick, &pos, sizeof(session->nick), session->nick_fallback_count - 1))
        return 0;

    return 1;
}

static int send_nick(struct MiniIrcSession *session)
{
    int pos = 0;

    if (!session || !session->nick[0])
        return 0;

    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, "NICK "))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, session->nick))
        return 0;

    return mini_irc_session_send_line(session, g_send_buf);
}

static int send_registered_actions(struct MiniIrcSession *session)
{
    if (!session || !session->registered)
        return 1;

    if (!session->join_sent && session->pending_channel[0]) {
        print_text("joining ");
        print_text(session->pending_channel);
        print_text("\n");
        if (!mini_irc_session_join(session, session->pending_channel))
            return 0;
        session->join_sent = 1;
    }

    return 1;
}

void mini_irc_session_init(struct MiniIrcSession *session,
                           MiniIrcSendFn send_fn,
                           void *send_ctx)
{
    if (!session)
        return;

    session->send_fn = send_fn;
    session->send_ctx = send_ctx;
    session->debug_fn = 0;
    session->debug_ctx = 0;
    session->nick[0] = '\0';
    session->base_nick[0] = '\0';
    session->channel[0] = '\0';
    session->pending_channel[0] = '\0';
    session->pending_one_shot_message[0] = '\0';
    session->line_buf[0] = '\0';
    session->line_len = 0;
    session->line_discarding = 0;
    session->verbose = 0;
    session->registered = 0;
    session->join_sent = 0;
    session->join_confirmed = 0;
    session->join_reported = 0;
    session->one_shot_sent = 0;
    session->quit_sent = 0;
    session->nick_fallback_count = 0;
    session->recv_calls = 0;
    session->recv_bytes = 0;
    session->lines_received = 0;
    session->waitselect_calls = 0;
    session->ewouldblock_count = 0;
    session->ping_detected_anywhere = 0;
    session->ping_command_detected = 0;
    session->ping_lines_seen = 0;
    session->pong_send_attempts = 0;
    session->pong_send_success = 0;
    session->pong_sent = 0;
    session->pong_bytes_sent = 0;
    session->pong_send_failures = 0;
    session->send_failures = 0;
    session->last_errno = 0;
    session->last_lines[0][0] = '\0';
    session->last_lines[1][0] = '\0';
    session->last_lines[2][0] = '\0';
    session->last_lines[3][0] = '\0';
    session->last_lines[4][0] = '\0';
    session->last_lines[5][0] = '\0';
    session->last_lines[6][0] = '\0';
    session->last_lines[7][0] = '\0';
    session->last_lines[8][0] = '\0';
    session->last_lines[9][0] = '\0';
    session->last_line_next = 0;
    session->last_pong[0] = '\0';
    session->last_pong_len = 0;
}

void mini_irc_session_set_debug(struct MiniIrcSession *session,
                                MiniIrcDebugFn debug_fn,
                                void *debug_ctx)
{
    if (!session)
        return;
    session->debug_fn = debug_fn;
    session->debug_ctx = debug_ctx;
}

int mini_irc_session_set_channel(struct MiniIrcSession *session,
                                 const char *channel)
{
    if (!session)
        return 0;

    return copy_limited(session->channel, sizeof(session->channel), channel);
}

int mini_irc_session_set_nick(struct MiniIrcSession *session,
                              const char *nick)
{
    if (!session)
        return 0;

    if (!copy_limited(session->nick, sizeof(session->nick), nick))
        return 0;
    if (!copy_limited(session->base_nick, sizeof(session->base_nick), nick))
        return 0;
    session->nick_fallback_count = 0;
    return 1;
}

int mini_irc_session_set_pending_channel(struct MiniIrcSession *session,
                                         const char *channel)
{
    if (!session)
        return 0;

    return copy_limited(session->pending_channel,
                        sizeof(session->pending_channel),
                        channel);
}

int mini_irc_session_set_pending_one_shot(struct MiniIrcSession *session,
                                          const char *message)
{
    if (!session)
        return 0;

    return copy_limited(session->pending_one_shot_message,
                        sizeof(session->pending_one_shot_message),
                        message);
}

int mini_irc_session_send_raw(struct MiniIrcSession *session,
                              const char *data,
                              int len)
{
    if (!session || !session->send_fn || !data || len <= 0)
        return 0;

    return session->send_fn(session->send_ctx, data, len);
}

int mini_irc_session_send_line(struct MiniIrcSession *session,
                               const char *line)
{
    int pos = 0;
    int result;

    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, line))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, "\r\n"))
        return 0;

    debug_event(session, "SEND_LINE", g_send_buf);
    result = mini_irc_session_send_raw(session, g_send_buf, pos);
    if (!result && session) {
        ++session->send_failures;
        debug_event(session, "SEND_LINE_FAIL", line);
    }

    return result;
}

int mini_irc_session_send_pong(struct MiniIrcSession *session,
                               const char *payload)
{
    int pos = 0;
    int result;

    if (session)
        ++session->pong_send_attempts;

    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, "PONG")) {
        if (session)
            ++session->pong_send_failures;
        return 0;
    }
    if (payload && payload[0]) {
        if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, " ")) {
            if (session)
                ++session->pong_send_failures;
            return 0;
        }
        if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, payload)) {
            if (session)
                ++session->pong_send_failures;
            return 0;
        }
    }
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, "\r\n")) {
        if (session)
            ++session->pong_send_failures;
        return 0;
    }

    if (session) {
        copy_limited(session->last_pong, sizeof(session->last_pong), g_send_buf);
        session->last_pong_len = pos;
    }

    if (session && session->verbose) {
        print_escaped_line("PONG line: ", g_send_buf, pos);
        print_text("PONG len=");
        print_number((LONG)pos);
    }

    debug_event(session, "PONG_SEND", g_send_buf);
    result = mini_irc_session_send_raw(session, g_send_buf, pos);
    if (result && session) {
        debug_event(session, "PONG_OK", g_send_buf);
        ++session->pong_send_success;
        ++session->pong_sent;
        if (session->verbose) {
            print_text("PING -> PONG\n");
            print_text("PONG sent\n");
        }
    } else if (session) {
        ++session->pong_send_failures;
        debug_event(session, "PONG_FAIL", g_send_buf);
    }

    return result;
}

int mini_irc_session_join(struct MiniIrcSession *session,
                          const char *channel)
{
    int pos = 0;

    if (!session || !channel || !channel[0])
        return 0;

    if (!mini_irc_session_set_channel(session, channel))
        return 0;

    session->join_confirmed = 0;
    session->join_reported = 0;

    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, "JOIN "))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, channel))
        return 0;

    return mini_irc_session_send_line(session, g_send_buf);
}

int mini_irc_session_privmsg(struct MiniIrcSession *session,
                             const char *target,
                             const char *text)
{
    int pos = 0;
    int result;

    if (!session || !target || !target[0] || !text)
        return 0;

    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, "PRIVMSG "))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, target))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, " :"))
        return 0;
    if (!append_text(g_send_buf, &pos, MINI_IRC_SEND_SIZE, text))
        return 0;

    result = mini_irc_session_send_line(session, g_send_buf);
    if (result && session->verbose)
        print_text("sent PRIVMSG\n");

    return result;
}

int mini_irc_session_handle_line(struct MiniIrcSession *session,
                                 const char *line)
{
    const char *prefix;
    const char *command;
    const char *payload;
    int prefix_len;
    int command_len;
    int contains_ping;

    if (!session || !line)
        return 0;

    parse_irc_line(line, &prefix, &prefix_len, &command, &command_len, &payload);
    debug_event(session, "RX_LINE", line);

    contains_ping = text_contains_ci(line, "PING");
    if (contains_ping) {
        ++session->ping_detected_anywhere;
        if (session->verbose) {
            print_text("contains PING raw line: ");
            print_text(line);
            print_text("\n");
            print_range("parsed prefix: ", prefix, prefix_len);
            print_range("parsed command: ", command, command_len);
            print_text("parsed payload: ");
            print_text(payload);
            print_text("\n");
        }
    }

    if (text_contains_ci(line, "ERROR") ||
        text_contains_ci(line, "Closing Link") ||
        text_contains_ci(line, "Ping timeout")) {
        print_text("protocol notice: ");
        print_text(line);
        print_text("\n");
    }

    if (token_equals_ci(command, command_len, "PING")) {
        if (session->verbose) {
            print_text("PING line: ");
            print_text(line);
            print_text("\n");
        }
        ++session->ping_command_detected;
        ++session->ping_lines_seen;
        debug_event(session, "PING", payload);
        return mini_irc_session_send_pong(session, payload);
    }

    if (token_equals_ci(command, command_len, "001")) {
        if (!session->registered) {
            session->registered = 1;
            print_text("registered\n");
        }
    } else if (token_equals_ci(command, command_len, "376") ||
               token_equals_ci(command, command_len, "422")) {
        if (!send_registered_actions(session))
            return 0;
    } else if (token_equals_ci(command, command_len, "433")) {
        if (!session->registered) {
            if (!make_fallback_nick(session))
                return 0;
            print_text("nick in use, trying ");
            print_text(session->nick);
            print_text("\n");
            if (!send_nick(session))
                return 0;
        }
    } else if (token_equals_ci(command, command_len, "451")) {
        if (!session->registered)
            print_text("warning: command before registration rejected\n");
    }

    if (token_equals_ci(command, command_len, "PRIVMSG")) {
        print_text("PRIVMSG dispatch\n");
        print_privmsg(session, prefix, prefix_len, payload);
        return 1;
    } else if (token_equals_ci(command, command_len, "JOIN")) {
        if (!print_join(session, prefix, prefix_len, payload))
            return 0;
    } else if (token_equals_ci(command, command_len, "PART")) {
        print_part(prefix, prefix_len, payload);
    } else if (token_equals_ci(command, command_len, "QUIT")) {
        print_quit(prefix, prefix_len, payload);
    } else if (token_equals_ci(command, command_len, "NICK")) {
        print_nick(prefix, prefix_len, payload);
    } else if (token_equals_ci(command, command_len, "TOPIC") ||
               token_equals_ci(command, command_len, "332")) {
        print_topic(payload);
    } else if (token_equals_ci(command, command_len, "353")) {
        if (session->verbose)
            print_names(payload);
    } else if (token_equals_ci(command, command_len, "366")) {
        if (session->verbose)
            print_text("*** end of names\n");
        if (!handle_end_of_names(session, payload))
            return 0;
    } else if (session->verbose || irc_line_visible(line)) {
        print_text(line);
        print_text("\n");
    }

    if (irc_line_fatal(line))
    {
        print_last_lines(session);
        print_last_pong(session);
    }

    return 1;
}

static int mini_irc_session_finish_line(struct MiniIrcSession *session)
{
    session->line_buf[session->line_len] = '\0';
    if (session->line_len > 0 && session->line_buf[session->line_len - 1] == '\r')
        session->line_buf[--session->line_len] = '\0';
    session->line_len = 0;

    if (session->line_buf[0] == '\0')
        return 1;

    ++session->lines_received;
    save_last_line(session, session->line_buf);
    if (session->verbose)
        print_text("irc line received\n");
    return mini_irc_session_handle_line(session, session->line_buf);
}

int mini_irc_session_receive_data(struct MiniIrcSession *session,
                                  const char *data,
                                  int len)
{
    int i;
    char c;

    if (!session || !data || len < 0)
        return 0;

    for (i = 0; i < len; ++i) {
        c = data[i];
        if (c == '\n') {
            if (session->line_discarding) {
                session->line_discarding = 0;
                session->line_len = 0;
                session->line_buf[0] = '\0';
                continue;
            }
            if (!mini_irc_session_finish_line(session))
                return 0;
            continue;
        }

        if (session->line_discarding)
            continue;

        if (session->line_len < (int)sizeof(session->line_buf) - 1) {
            session->line_buf[session->line_len++] = c;
        } else {
            session->line_len = 0;
            session->line_buf[0] = '\0';
            session->line_discarding = 1;
            print_text("irc line overflow\n");
        }
    }

    return 1;
}
