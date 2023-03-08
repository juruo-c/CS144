#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // Your code here.
    _route_table.push_back({route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // Your code here.
    IPv4Header& header = dgram.header();

    // ttl will down to 0 or has been 0
    if (header.ttl <= 1) return;

    size_t table_num = 0;
    optional<size_t> longest_prefix_length{};
    const size_t route_table_size = _route_table.size();
    const uint32_t ip = header.dst;

    for (size_t i = 0; i < route_table_size; i ++ ) {
        auto& tuple = _route_table[i];
        size_t mask = (~0ul) ^ ((1ul << (32 - tuple.prefix_length)) - 1ul);
        if ((ip & mask) == (tuple.route_prefix & mask)) {
            if (!longest_prefix_length.has_value() ||
                longest_prefix_length.value() < tuple.prefix_length) {
                table_num = i;
                longest_prefix_length = {tuple.prefix_length};
            }
        }
    }

    // no routes match
    if (!longest_prefix_length.has_value()) return;

    header.ttl --;
    auto& tuple = _route_table[table_num];
    // send the datagram to the specific interface
    _interfaces[tuple.interface_num].send_datagram(
        dgram,
        tuple.next_hop.has_value() ? 
        tuple.next_hop.value() : 
        Address::from_ipv4_numeric(ip)
    );

}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
