#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _timer(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

TCPHeader TCPSender::make_header(const WrappingInt32&& seqno, bool syn, bool fin) const {
    TCPHeader header;
    header.seqno = seqno;
    header.syn = syn;
    header.fin = fin;
    return header;
}

void TCPSender::send_segment(const TCPSegment& segment) {
    _segments_out.push(segment);
    //! 4. if _timer is not running, start it
    if (_timer.is_closed()) 
        _timer.start_timer();
}

void TCPSender::fill_window() {
    //! in SYN_SENT status, can not send anything
    if (next_seqno_absolute() > 0 && next_seqno_absolute() == bytes_in_flight()) 
        return;
    //! in FIN_SENT status, nothing to send
    if (stream_in().eof() && next_seqno_absolute() == stream_in().bytes_written() + 2)
        return;
 
    TCPSegment segment;
    if (next_seqno_absolute() == 0) { //! CLOSED, send a SYN segment, switch to SYN_SENT
        segment.header() = make_header(next_seqno(), true);

        send_segment(segment);
        _segments_outstanding.push({segment, next_seqno_absolute()});
        _bytes_in_flight += segment.length_in_sequence_space();
        _next_seqno += segment.length_in_sequence_space();
    }
    else { //! SYN_ACKED, send segments as many as possible
        //! \details make sure the window size
        const size_t last_ackno_absolute = _segments_outstanding.empty() ? 
                                        next_seqno_absolute() :
                                        _segments_outstanding.front().second;
        const size_t window_size = _window_size == 0 ? 1 : _window_size; 
        size_t window_left_size = 
            last_ackno_absolute + window_size > next_seqno_absolute() ?
            last_ackno_absolute + window_size - next_seqno_absolute() :
            0;
        //! \details send segments
        while (window_left_size > 0) {
            if (stream_in().eof() && next_seqno_absolute() == stream_in().bytes_written() + 2)
                break;
            //! \details make sure the payload size
            size_t payload_size = min(TCPConfig::MAX_PAYLOAD_SIZE,
                                    min(window_left_size, 
                                    stream_in().buffer_size()));

            // cout << "===============   WINDOW DEBUG   ===============\n";
            // cout << "next_seqno = " << next_seqno() << endl;
            // cout << "_window_size = " << window_left_size << endl;
            // cout << "stream_buffer_size = " << stream_in().buffer_size() << endl;
            // cout << "payload_size = " << payload_size << endl;
            // cout << "=============== END DEBUG ===============\n";

            segment.payload() = Buffer{stream_in().read(payload_size)};
            segment.header() = make_header(next_seqno(), false, 
                stream_in().eof() && payload_size + 1 <= window_left_size);
            
            //! if the segment is empty
            if (segment.length_in_sequence_space() == 0)
                break;

            send_segment(segment);
            _segments_outstanding.push({segment, next_seqno_absolute()});
            _bytes_in_flight += segment.length_in_sequence_space();
            _next_seqno += segment.length_in_sequence_space();
            window_left_size -= segment.length_in_sequence_space();
        }
    }

    // cout << "===============  WINDOW DEBUG   ===============\n";
    // cout << "_bytes_in_flight = " << bytes_in_flight() << endl;
    // cout << "=============== END DEBUG ===============\n";

}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    //! get the absolute ackno and check if the acknowledge message is legal
    const auto absolute_ackno = unwrap(ackno, _isn, next_seqno_absolute());
    if (absolute_ackno > next_seqno_absolute()) return;
    
    //! check the outstanding segment and judge if ackno is useful
    bool useful_ackno = false;
    while (!_segments_outstanding.empty()) {
        const auto& [segment, absolute_seqno] = _segments_outstanding.front();
        if (absolute_seqno + segment.length_in_sequence_space() - 1 >= absolute_ackno) break;
        useful_ackno = true;
        
        _bytes_in_flight -= segment.length_in_sequence_space();
        _segments_outstanding.pop();
    }

    //! fill the receiver's window
    _window_size = window_size;

    // cout << "===============  ACK DEBUG   ===============\n";
    // cout << "next_seqno = " << next_seqno() << endl;
    // cout << "_window_size = " << _window_size << endl;
    // cout << "_bytes_in_flight = " << bytes_in_flight() << endl;
    // cout << "=============== END DEBUG ===============\n";

    //! if the ackno is useful
    if (!useful_ackno) return;
    //! 7.(a) set _timer's RTO to initial value
    _timer.set_rto(_initial_retransmission_timeout);
    //! 7.(b) if sender has outstanding segments, restart the retransmission _timer
    if (!_segments_outstanding.empty()) {
        _timer.start_timer();
    } else { // 5. stop the retransmission _timer
        _timer.close_timer();
    }
    //! 7.(c) reset the count of consecutive retransmissions
    _consecutive_retransmissions = 0;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    //! 6. if tick is called and the retransmission timer has expired
    if (_timer.is_expired(ms_since_last_tick)) {
        //! (a) retransmit the earliest segment that hasn't been acknowledged
        send_segment(_segments_outstanding.front().first);
        //! (b) if the window size is nonzero, keep track of the number of 
        //  consecutive retransmission, and doble the timer's RTO
        if (_window_size) {
            _consecutive_retransmissions ++;
            _timer.double_rto();
        }
        //! (c) restart the timer
        _timer.start_timer();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header() = make_header(wrap(_next_seqno, _isn));
    _segments_out.push(segment);
}

bool Timer::is_expired(const size_t& ms_since_last_tick) {
    if (is_closed()) return false;
    if (_time_left > ms_since_last_tick) {
        _time_left -= ms_since_last_tick;
        return false;
    }
    return (_closed = true);
}