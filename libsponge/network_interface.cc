#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}


void NetworkInterface::set_ethernet_header(EthernetHeader& header, const EthernetAddress& dst, 
                            const EthernetAddress& src, const uint16_t& type) {
    header.dst = dst;
    header.src = src;
    header.type = type;
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    EthernetFrame new_frame;

    if (_map_ipv4_to_ethernet.count(next_hop_ip)) { 
        // if the mapping already exists
        set_ethernet_header(new_frame.header(), _map_ipv4_to_ethernet[next_hop_ip], 
                            this->_ethernet_address, EthernetHeader::TYPE_IPv4);
        new_frame.payload() = move(dgram.serialize());        
    } else if (!_ip_arp.count(next_hop_ip)) {
        // the mapping does not exist
        set_ethernet_header(new_frame.header(), ETHERNET_BROADCAST, this->_ethernet_address, 
                EthernetHeader::TYPE_ARP);
        ARPMessage request_arp_message;
        request_arp_message.opcode = ARPMessage::OPCODE_REQUEST;
        request_arp_message.sender_ethernet_address = this->_ethernet_address;
        request_arp_message.sender_ip_address = (this->_ip_address).ipv4_numeric();
        request_arp_message.target_ip_address = next_hop_ip;
        new_frame.payload() = {request_arp_message.serialize()};
        
        // set the last send time
        _ip_arp.insert(next_hop_ip);
        _ip_arp_time.push({next_hop_ip, 5000});

        // stoged the ip that hasn't know the Ethernet address of the next hop ip
        _ip_datagrams[next_hop_ip].push_back(dgram);
    } else return;

    _frames_out.push(new_frame);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    const EthernetHeader& header = frame.header();
    // ignore any frames not destined for the network interface
    if (header.dst != ETHERNET_BROADCAST && header.dst != _ethernet_address)
        return nullopt;
    
    if (header.type == EthernetHeader::TYPE_IPv4) {
        // if the inbound type is ipv4
        InternetDatagram datagram;
        if (datagram.parse(frame.payload()) == ParseResult::NoError)
            return optional<InternetDatagram>{datagram};
        return nullopt;
    } else {
        // if the inbound type is ARP
        ARPMessage arp_message;
        if (arp_message.parse(frame.payload()) != ParseResult::NoError)
            return nullopt;
        if (header.dst == ETHERNET_BROADCAST && arp_message.target_ip_address != (this->_ip_address).ipv4_numeric())
            return nullopt;

        const uint32_t sender_ip = arp_message.sender_ip_address;
        // learn a new mapping from "sender" fields
        _map_ipv4_to_ethernet[sender_ip] = arp_message.sender_ethernet_address;
        _map_ipv4_to_time[sender_ip] = 30000;
        _cached_mapping.push({sender_ip, 30000});

        // if the ARP type is request, reply
        if (arp_message.opcode == ARPMessage::OPCODE_REQUEST) {
            EthernetFrame new_frame;
            
            set_ethernet_header(new_frame.header(), arp_message.sender_ethernet_address, 
                                this->_ethernet_address, EthernetHeader::TYPE_ARP);
            ARPMessage reply_arp_message;
            reply_arp_message.opcode = ARPMessage::OPCODE_REPLY;
            reply_arp_message.sender_ethernet_address = this->_ethernet_address;
            reply_arp_message.sender_ip_address = (this->_ip_address).ipv4_numeric();
            reply_arp_message.target_ethernet_address = move(arp_message.sender_ethernet_address);
            reply_arp_message.target_ip_address = arp_message.sender_ip_address;
            new_frame.payload() = {reply_arp_message.serialize()};
            
            _frames_out.push(new_frame);
        }

        // resent when receive a ARP
        for (auto& dgram : _ip_datagrams[sender_ip]) 
            send_datagram(dgram, Address::from_ipv4_numeric(sender_ip));
        _ip_datagrams[sender_ip].clear();

        return nullopt;
    }

}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { 
    // Expire any mappings that have been expired
    while (!_cached_mapping.empty()) {
        auto& [ip, last_time] = _cached_mapping.front();

        if (last_time == _map_ipv4_to_time[ip]) {
            if (last_time > ms_since_last_tick) {
                last_time -= ms_since_last_tick;
                _map_ipv4_to_time[ip] = last_time;
                break;
            } else {
                _map_ipv4_to_ethernet.erase(ip);
                _map_ipv4_to_time.erase(ip);
            }
        }

        _cached_mapping.pop();
    }

    // update the arp sent last time
    while (!_ip_arp_time.empty()) {
        auto& [ip, last_time] = _ip_arp_time.front();
        
        if (last_time > ms_since_last_tick) {
            last_time -= ms_since_last_tick;
            break;            
        }

        _ip_arp.erase(ip);
        _ip_arp_time.pop();
    }
}
