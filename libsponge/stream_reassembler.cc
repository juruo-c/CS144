#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : 
    _output(capacity), 
    _capacity(capacity),
    _unassembled_buffer(),
    _unassembled_bytes(0),
    _eof(false) {}

void StreamReassembler::reset_buffer() {
    size_t buffer_size = _unassembled_buffer.size();
    if (buffer_size <= 1) return;

    // merge and create new buffer
    list<pair<string, size_t>> new_buffer;
    auto iter = _unassembled_buffer.begin();
    auto end = _unassembled_buffer.end();
    size_t cur_index = iter->second;
    string cur_string = iter->first;
    size_t next_byte_index = cur_index + cur_string.size();
    iter ++;

    while (iter != end) {
        if (iter->second <= next_byte_index) // overlap with last string
            cur_string += iter->first.substr(next_byte_index - iter->second);
        else { // does not overlap with last string
            new_buffer.push_back({cur_string, cur_index});
            cur_string = iter->first;
            cur_index = iter->second;
        }
        next_byte_index = cur_index + cur_string.size();
        iter ++;
    }

    // update the buffer using move semantics
    _unassembled_buffer = move(new_buffer);
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (eof) _eof = true;
    
    size_t first_unassembled = _output.bytes_written();
    if (index <= first_unassembled) { //! data intersect with the byte stream
        _output.write(data.substr(first_unassembled - index));

        first_unassembled = _output.bytes_written();
        while (_unassembled_buffer.size() && 
        _unassembled_buffer.front().second <= first_unassembled) {
            auto element = _unassembled_buffer.front();
            _unassembled_buffer.pop_front();
            _unassembled_bytes -= element.first.size();

            if (element.second + element.first.size() - 1 >= first_unassembled) {
                _output.write(element.first.substr(first_unassembled - element.second));
                break;
            }
        }
    }
    else { //! data does not intersect with the byte stream
        //! \brief insert data into buffer
        auto iter = _unassembled_buffer.begin();
        auto end = _unassembled_buffer.end();
        while (iter != end) {
            if (index <= iter->second) {
                _unassembled_buffer.insert(iter, {data, index});
                break;
            }
            iter ++;
            if (iter == end) 
                _unassembled_buffer.insert(iter, {data, index});
        }
        //! \brief reset buffer
        reset_buffer();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return _unassembled_bytes == 0; }
