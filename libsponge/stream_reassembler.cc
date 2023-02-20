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
    _eof(false),
    _eof_idx(0) {}

void StreamReassembler::reset_buffer() {
    if (_unassembled_buffer.size() <= 1) return;

    // merge and create new buffer
    list<pair<string, size_t>> new_buffer;
    auto iter = _unassembled_buffer.begin();
    auto end = _unassembled_buffer.end();
    size_t cur_index = iter->second;
    string cur_string = iter->first;
    size_t next_byte_index = cur_index + cur_string.size();
    iter ++;

    while (iter != end) {
        if (iter->second <= next_byte_index && 
            iter->second + iter->first.size() - 1 >= next_byte_index) 
            // overlap with last string
            cur_string += iter->first.substr(next_byte_index - iter->second);
        else if (iter->second > next_byte_index) { 
            // does not overlap with last string
            new_buffer.push_back({cur_string, cur_index});
            cur_string = iter->first;
            cur_index = iter->second;
        }
        next_byte_index = cur_index + cur_string.size();
        iter ++;
    }
    new_buffer.push_back({cur_string, cur_index});

    // update the buffer using move semantics
    _unassembled_buffer = move(new_buffer);
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    //! \details check if the given data is outside the window
    if (index >= _output.bytes_read() + _capacity) return;
    
    //! \details solve the empty data problem
    if (data.size() == 0) {
        if (eof) _output.end_input();
        return;
    } 

    if (eof) {
        _eof_idx = index + data.size() - 1;
        _eof = true;
    }
    
    //! \details cut the data if it is over the limit
    string actual_data = data;
    if (index + actual_data.size() - 1 >= _output.bytes_read() + _capacity) 
        actual_data = actual_data.substr(0, _output.bytes_read() + _capacity - index);

    size_t first_unassembled = _output.bytes_written();
    if (index <= first_unassembled && 
        index + actual_data.size() - 1 >= first_unassembled) { 
        //! data intersect with the byte stream
        _output.write(actual_data.substr(first_unassembled - index));

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

        if (_eof && _output.bytes_written() > _eof_idx) _output.end_input();
    }
    else if (index > first_unassembled) { 
        //! data does not intersect with the byte stream
        //! \details calculate the number of bytes which are not overlapping with
        //! other existed bytes
        auto iter = _unassembled_buffer.begin();
        auto end = _unassembled_buffer.end();
        size_t new_unassembled_bytes = actual_data.size();
        size_t data_end_index = index + actual_data.size() - 1;
        while (iter != end) {
            size_t element_l = iter->second, element_r = element_l + iter->first.size() - 1;
            if (!(element_l > data_end_index || 
                index > element_r)) {
                
                if (index >= element_l && data_end_index <= element_r) {
                    new_unassembled_bytes = 0;
                    break;
                }

                new_unassembled_bytes -= 
                    min(data_end_index, element_r) - max(index, element_l) + 1;
            }
            iter ++;
        }
        _unassembled_bytes += new_unassembled_bytes;

        //! \brief insert data into buffer
        iter = _unassembled_buffer.begin();
        while (iter != end && index > iter->second) iter ++;
        _unassembled_buffer.insert(iter, {actual_data, index});

        //! \brief reset buffer
        reset_buffer();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return _unassembled_bytes == 0; }
