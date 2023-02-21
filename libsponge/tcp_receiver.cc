#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    //! \details Set the initial segment number if necessary.
    const TCPHeader& header = seg.header();
    if (!isn.has_value()) {
        if (!header.syn) return;
        isn = {header.seqno};
    }

    //! \details Push any data, or end-of-stream marker, to the StreamReassembler.
    _reassembler.push_substring(seg.payload().copy(), 
                                unwrap(header.seqno + header.syn, isn.value(), stream_out().bytes_written()) - 1, 
                                header.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if (isn.has_value()) {
        const ByteStream& stream = stream_out();
        size_t _ackno = stream.bytes_written() + 1;
        if (stream.input_ended()) _ackno ++;
        return optional<WrappingInt32>(wrap(_ackno, isn.value()));
    }
    return nullopt;
}

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
