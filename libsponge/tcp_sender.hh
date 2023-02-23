#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>

//! \brief the TCP time keeper
class Timer {
  private:
    //! the retransmission timeout
    size_t _rto{0};
    //! the time left currently
    size_t _time_left{0};
    //! if the timer has been closed
    bool _closed{true};
  public:
    //! construtor
    Timer (const size_t& rto) : _rto(rto) {}
    //! \brief set the timer's RTO
    void set_rto(const size_t& rto) { _rto = rto; }
    //! \brief start the timer
    void start_timer() { _time_left = _rto; _closed = false; }
    //! \brief close the timer
    void close_timer() { _closed = true; }
    //! \brief double the retransmission timeout in the timer
    void double_rto() { _rto *= 2; }
    //! \brief if the time is closed
    bool is_closed() { return _closed; }
    //! \brief check if the timer is expired when call tick()
    bool is_expired(const size_t& ms_since_last_tick);
};

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    //！outstanding segments and its absolute seqno 
    //! that have been set but not been acknowledged
    std::queue<std::pair<TCPSegment, uint64_t>> _segments_outstanding{};
    //! TCP receiver's window size
    uint16_t _window_size{1};
    //! sequence numbers are occupied by segments sent but not yet acknowledged
    size_t _bytes_in_flight{0};
    //! the number of consecutive retransmissions
    size_t _consecutive_retransmissions{0};
    //! the retransimission timer
    Timer _timer;
    //! Make a TCP header
    TCPHeader make_header(const WrappingInt32&& seqno, bool syn = false, bool fin = false) const;
    //! "Send" a TCP segment
    void send_segment(const TCPSegment& segment);

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
