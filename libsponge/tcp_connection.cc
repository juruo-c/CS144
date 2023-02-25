#include "tcp_connection.hh"

#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

using namespace std;

void TCPConnection::set_ackno_window_size(TCPHeader& header, bool rst) {
    auto receiver_ackno = _receiver.ackno();
    size_t window_size = _receiver.window_size();
    header.ack = receiver_ackno.has_value();
    if (header.ack) 
        header.ackno = receiver_ackno.value();
    header.win = window_size > numeric_limits<uint16_t>::max() ?
                    numeric_limits<uint16_t>::max() :
                    window_size;
    header.rst = rst;
}

void TCPConnection::clear_sender_segments(bool rst) {
    auto& segments = _sender.segments_out();
    while (!segments.empty()) {
        auto segment = segments.front();
        set_ackno_window_size(segment.header(), rst);
        _segments_out.push(segment);
        segments.pop();
    }
}

void TCPConnection::send_reset_segment() {
    // cout << "============== DEBUG ==============\n";
    // cout << "in send reset segment\n";
    // cout << "============== END DEBUG =============\n";
    _sender.send_empty_segment();
    clear_sender_segments(true);
}

void TCPConnection::unclean_shutdown() {
    //! set both the inbound and outbound streams to the error state
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    //! kills the connection permanently
    _active = false;
}

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {  
    if (!_active) return;
    _time_since_last_segment_received = 0;
    const TCPHeader& header = seg.header();
    //! if the RST flag is set
    if (header.rst) {
        unclean_shutdown();
        return;
    }
    //! give the segment to the TCPReceiver
    _receiver.segment_received(seg);
    //! if the ACK flag is set, tells the TCPSender about the ackno and the window_size
    if (header.ack)
        _sender.ack_received(header.ackno, header.win);

    // cout << "============= SEGMENT RECEIVE ==============\n" << endl;
    // cout << "seg.length = " << seg.length_in_sequence_space() << endl;
    // cout << "seg.ack = " << seg.header().ack << endl;
    // cout << "seg.seqno = " << header.seqno << endl;
    // cout << "seg.ackno = " << header.ackno << endl;
    // cout << "============= END SEGMENT     ==============\n" << endl;

    //! if the incoming segment occupied any sequence numbers,
    //! calls fill_window to reply
    if (seg.length_in_sequence_space()) {
        _sender.fill_window();
        if (_sender.segments_out().empty()) 
            _sender.send_empty_segment();
        clear_sender_segments();
    }
    //! keep-alive segment
    if (_receiver.ackno().has_value() && (seg.length_in_sequence_space() == 0)
        && seg.header().seqno == _receiver.ackno().value() - 1) {
        _sender.send_empty_segment();
        clear_sender_segments();
    }
    //! if the inbound stream ends before the TCPConnection has
    //! reached EOF on its outbound stream
    if (_receiver.stream_out().input_ended() && !_sender.stream_in().eof())
        _linger_after_streams_finish = false;
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    if (!_active) return 0;
    size_t bytes_actual_write = _sender.stream_in().write(data);
    _sender.fill_window();
    clear_sender_segments();
    return bytes_actual_write;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (_sender.stream_in().bytes_written() != 0) {
        _sender.fill_window();
        clear_sender_segments();
    }

    _sender.tick(ms_since_last_tick);
    //! the number of consecutive retransmission is more than an upper limit
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        //! send a reset segment to the peer
        _sender.segments_out().pop();
        send_reset_segment();
        unclean_shutdown();
        return;
    }
    clear_sender_segments();
    _time_since_last_segment_received += ms_since_last_tick;
    //TODO end the connection cleanly if necessary
    if (_receiver.stream_out().input_ended() &&
        _sender.stream_in().eof() &&
        _sender.bytes_in_flight() == 0) { //! satisfies #1 through #3
        if (!_linger_after_streams_finish) _active = false;
        else if (_time_since_last_segment_received >= 
                10 * static_cast<size_t>(_cfg.rt_timeout)) 
            _active = false;
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input(); 
    _sender.fill_window();
    clear_sender_segments();
}

void TCPConnection::connect() { _sender.fill_window(); clear_sender_segments(); }

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            send_reset_segment();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
