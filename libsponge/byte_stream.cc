#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) :
    _capacity{capacity} {}

size_t ByteStream::write(const string &data) {
    size_t numberAccept = remaining_capacity();
    if (numberAccept > data.size()) numberAccept = data.size();
    _buffer.push_back({move(data.substr(0, numberAccept))});
    _bytes_writen += numberAccept;
    _size += numberAccept;
    return numberAccept;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t _len = len;
    string res;
    for (auto& s : _buffer) {
        if (_len >= s.size()) {
            res.append(s.copy());
            _len -= s.size();
        } else {
            res.append(s.copy().substr(0, _len));
            break;
        }
    }
    return move(res);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    _bytes_read += len;
    _size -= len;
    
    size_t _len = len;
    while (_len > 0) {
        auto& s = _buffer.front();
        if (_len >= s.size()) {
            _len -= s.size();
            _buffer.pop_front();
        } else {
            s.remove_prefix(_len);
            break;
        }
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    size_t readNumber = min(len, buffer_size());
    string res = peek_output(readNumber);
    pop_output(readNumber);
    return move(res);
}

void ByteStream::end_input() { _input_ended = true; }

bool ByteStream::input_ended() const { return _input_ended; }

size_t ByteStream::buffer_size() const { return _size; }

bool ByteStream::buffer_empty() const { return _buffer.empty(); }

bool ByteStream::eof() const { return input_ended() && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _bytes_writen; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - buffer_size(); }