#ifndef AMITCP13_TOOLS_MINI_IRC_SESSION_H
#define AMITCP13_TOOLS_MINI_IRC_SESSION_H

typedef int (*MiniIrcSendFn)(void *ctx, const char *data, int len);
typedef void (*MiniIrcDebugFn)(void *ctx, const char *tag, const char *text);

struct MiniIrcSession
{
    MiniIrcSendFn send_fn;
    void *send_ctx;
    MiniIrcDebugFn debug_fn;
    void *debug_ctx;
    char nick[32];
    char base_nick[32];
    char channel[64];
    char pending_channel[64];
    char pending_one_shot_message[512];
    char line_buf[512];
    int line_len;
    int line_discarding;
    int verbose;
    int console_output;
    int registered;
    int join_sent;
    int join_confirmed;
    int join_reported;
    int one_shot_sent;
    int quit_sent;
    int nick_fallback_count;
    unsigned long recv_calls;
    unsigned long recv_bytes;
    unsigned long lines_received;
    unsigned long waitselect_calls;
    unsigned long ewouldblock_count;
    unsigned long ping_detected_anywhere;
    unsigned long ping_command_detected;
    unsigned long ping_lines_seen;
    unsigned long pong_send_attempts;
    unsigned long pong_send_success;
    unsigned long pong_sent;
    unsigned long pong_bytes_sent;
    unsigned long pong_send_failures;
    unsigned long send_failures;
    int last_errno;
    char last_lines[10][160];
    int last_line_next;
    char last_pong[160];
    int last_pong_len;
};

void mini_irc_session_init(struct MiniIrcSession *session,
                           MiniIrcSendFn send_fn,
                           void *send_ctx);
void mini_irc_session_set_debug(struct MiniIrcSession *session,
                                MiniIrcDebugFn debug_fn,
                                void *debug_ctx);
void mini_irc_session_set_console_output(struct MiniIrcSession *session,
                                         int enabled);

int mini_irc_session_set_channel(struct MiniIrcSession *session,
                                 const char *channel);
int mini_irc_session_set_nick(struct MiniIrcSession *session,
                              const char *nick);
int mini_irc_session_set_pending_channel(struct MiniIrcSession *session,
                                         const char *channel);
int mini_irc_session_set_pending_one_shot(struct MiniIrcSession *session,
                                          const char *message);
int mini_irc_session_send_raw(struct MiniIrcSession *session,
                              const char *data,
                              int len);
int mini_irc_session_send_line(struct MiniIrcSession *session,
                               const char *line);
int mini_irc_session_send_pong(struct MiniIrcSession *session,
                               const char *payload);
int mini_irc_session_join(struct MiniIrcSession *session,
                          const char *channel);
int mini_irc_session_privmsg(struct MiniIrcSession *session,
                             const char *target,
                             const char *text);
int mini_irc_session_handle_line(struct MiniIrcSession *session,
                                 const char *line);
int mini_irc_session_receive_data(struct MiniIrcSession *session,
                                  const char *data,
                                  int len);

#endif
