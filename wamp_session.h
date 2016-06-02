#ifndef XXX_SESSION_H
#define XXX_SESSION_H

#include "Callbacks.h"

#include "io_listener.h"

#include <jalson/jalson.h>

#include <map>
#include <mutex>
#include <memory>
#include <future>

namespace XXX {

  class wamp_session;

  typedef std::function< void() > reply_fn;
  typedef std::function< void(wamp_args, std::unique_ptr<std::string> ) > wamp_invocation_reply_fn;
  typedef std::function< void(session_handle, bool) > session_state_fn;

  struct server_msg_handler
  {
    std::function<void(wamp_session*, std::string uri, wamp_args, wamp_invocation_reply_fn)> inbound_call;
    std::function<void(wamp_session*, std::string uri, wamp_args)> handle_inbound_publish;
    std::function<uint64_t (std::weak_ptr<wamp_session>, std::string realm, std::string uri)> inbound_register;
    std::function<uint64_t (wamp_session*, std::string uri, wamp_args)> inbound_subscribe;
  };

  class event_loop;
  class kernel;
  class IOHandle;
  class Logger;

  // Needs to support needs of service providers (rpc & topics), and service
  // consumers (rpc callers, and subscribers)
  class wamp_session : public std::enable_shared_from_this<wamp_session>, public io_listener
  {
  public:
    // wamp_session can only be created as shared_ptr
    static std::shared_ptr<wamp_session> create(kernel&,
                                                std::unique_ptr<IOHandle>,
                                                bool is_passive,
                                                std::string realm,
                                                session_state_fn state_cb,
                                                session_closed_cb  closed_cb,
                                                server_msg_handler = server_msg_handler());

    ~wamp_session();

    void send_msg(jalson::json_array&, bool final=false);

    void new_request_close(); // perform asynchronous close

    void close();

    /* After this is called, no more calls will be made from wamp_session into
     * the user space (e.g. to deliver invocation requests, etc) */
    void disable_callback();

    session_handle handle() { return shared_from_this(); }

    bool is_open() const;
    bool is_pending_open() const;

    void initiate_handshake();

    /* Number of seconds since session constructed  */
    int duration_since_creation() const;

    /* Time since last message */
    int duration_since_last() const;

    /* Does this session use heartbeats? */
    bool uses_heartbeats() const;

    /* return the realm, or empty string if a realm has not yet been provided,
     * eg, in case of a passive session */
    const std::string& realm() const;

    int hb_interval_secs() const { return m_hb_intvl; }


    t_request_id provide(std::string uri,
                         const jalson::json_object& options,
                         rpc_cb cb,
                         void * data);

    t_request_id subscribe(const std::string& uri,
                           const jalson::json_object& options,
                           subscription_cb cb,
                           void * user);

    t_request_id call(std::string uri,
                      const jalson::json_object& options,
                      wamp_args args,
                      wamp_call_result_cb user_cb,
                      void* user_data);

    t_request_id publish(std::string uri,
                         const jalson::json_object& options,
                         wamp_args args);

    t_request_id invocation(uint64_t registration_id,
                            const jalson::json_object& options,
                            wamp_args args,
                            wamp_invocation_reply_fn);

    t_sid unique_id();

  private:

    wamp_session(kernel&,
                 std::unique_ptr<IOHandle>,
                 bool is_passive,
                 std::string realm,
                 session_state_fn state_cb,
                 std::shared_ptr<wamp_session>& outref,
                 server_msg_handler,
                 session_closed_cb closed_cb);

    wamp_session(const wamp_session&) = delete;
    wamp_session& operator=(const wamp_session&) = delete;

    bool send_bytes(std::pair<const char*, size_t>*, size_t, bool final);

    void io_on_close() override;
    void io_on_read(char*, size_t) override;
    void io_on_read_impl(char*, size_t);
    void decode_and_process(char*, size_t len);
    void process_message(unsigned intmessage_type,
                         jalson::json_array&);


    void update_state_for_outbound(const jalson::json_array& msg);

    friend class IOHandle;

    enum SessionState
    {
      eInit = 0,

      // for active client
      eRecvHello,
      eSentChallenge,
      eRecvAuth,

      // next are client state values
      eSentHello,
      eRecvChallenge,
      eSentAuth,

      // main states
      eOpen,
      eClosing,
      eClosed,

      eStateMax
    } m_state;

    void change_state(SessionState expected, SessionState next);

    void handle_HELLO(jalson::json_array& ja);
    void handle_CHALLENGE(jalson::json_array& ja);
    void handle_AUTHENTICATE(jalson::json_array& ja);
    void handle_ABORT(jalson::json_array& ja);
    void handle_WELCOME(jalson::json_array& ja);

    void notify_session_state_change(bool is_open);
    static const char* state_to_str(wamp_session::SessionState);


    Logger *__logptr; /* name chosen for log macros */
    kernel& m_kernel;

    uint64_t m_sid;

    std::mutex m_handle_lock;
    std::unique_ptr<IOHandle> m_handle;

    /* Interval, in secs, at which to send heartbeats. Values below 30 seconds
        might not be too reliable, because the underlying housekeeping timer has
        around a 20 second precision. */
    int m_hb_intvl;
    time_t m_time_create;

    time_t m_time_last_msg_recv;

    mutable std::mutex m_request_lock;
    t_request_id m_next_request_id;

    char *  m_buf;
    size_t  m_bytes_avail;

    bool m_is_passive;

    jalson::json_value m_challenge; // full message


    std::string m_realm;
    mutable std::mutex m_realm_lock;

    session_state_fn m_notify_state_change_fn;

  private:

    void process_inbound_registered(jalson::json_array &);
    void process_inbound_invocation(jalson::json_array &);
    void process_inbound_subscribed(jalson::json_array &);
    void process_inbound_event(jalson::json_array &);
    void process_inbound_result(jalson::json_array &);
    void process_inbound_error(jalson::json_array &);
    void process_inbound_call(jalson::json_array &);
    void process_inbound_yield(jalson::json_array &);
    void process_inbound_publish(jalson::json_array &);
    void process_inbound_subscribe(jalson::json_array &);
    void process_inbound_register(jalson::json_array &);

    void invocation_yield(int request_id,
                          wamp_args args);

    void reply_with_error(int request_type,
                          int request_id,
                          wamp_args args,
                          std::string error_uri);

    server_msg_handler m_server_handler;

    struct procedure
    {
      std::string uri;
      rpc_cb user_cb;
      void * user_data;
    };

    struct subscription
    {
      std::string uri;
      subscription_cb user_cb;
      void * user_data;
    };

    struct wamp_call
    {
      std::string rpc;
      wamp_call_result_cb user_cb;
      void* user_data;
      wamp_call() : user_data( nullptr ) { }
    };

    struct wamp_invocation
    {
      wamp_invocation_reply_fn reply_fn;
    };


    mutable std::mutex m_pending_lock;
    std::map<t_request_id, subscription>    m_pending_subscribe;
    std::map<t_request_id, procedure>       m_pending_register;
    std::map<t_request_id, wamp_call>       m_pending_call;
    std::map<t_request_id, wamp_invocation> m_pending_invocation;

    // TODO: procedures -- not currently locked, however, need to add locking once
    // unprovide() is added, and if it is implemented synchronously.
    std::map<t_request_id, procedure> m_procedures;
    std::map<t_subscription_id, subscription> m_subscriptions;

    // control when callbacks are allowed into user code
    std::recursive_mutex m_user_cb_lock;
    bool                 m_user_cb_allowed;


    session_closed_cb  m_closed_cb;
    bool m_invoke_final_ev;

  };

} // namespace XXX

#endif
