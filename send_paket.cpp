#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ether.h>
#include <net/if.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>

#define BUF_SIZE 42 // Minimum Ethernet frame size (including the Ethernet header)

int main() {
    int sockfd;
    char buffer[BUF_SIZE];
    struct sockaddr_ll device;
    struct ifreq if_idx;
    struct ifreq if_mac;
    const char* interface = "eth0"; // Replace with your network interface
    unsigned char dest_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56}; // Destination MAC address

    // Create a raw socket
    sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Get the index of the network interface
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_idx.ifr_name, interface, IFNAMSIZ - 1);
    if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0) {
        perror("SIOCGIFINDEX");
        close(sockfd);
        return 1;
    }

    // Get the MAC address of the interface
    memset(&if_mac, 0, sizeof(struct ifreq));
    strncpy(if_mac.ifr_name, interface, IFNAMSIZ - 1);
    if (ioctl(sockfd, SIOCGIFHWADDR, &if_mac) < 0) {
        perror("SIOCGIFHWADDR");
        close(sockfd);
        return 1;
    }

    // Construct the Ethernet header
    struct ether_header* eh = reinterpret_cast<struct ether_header*>(buffer);
    std::memcpy(eh->ether_shost, if_mac.ifr_hwaddr.sa_data, 6); // Source MAC address
    std::memcpy(eh->ether_dhost, dest_mac, 6);                 // Destination MAC address
    eh->ether_type = htons(ETH_P_IP);                          // EtherType: IPv4

    // Fill the payload with dummy data
    std::memset(buffer + sizeof(struct ether_header), 0xAA, BUF_SIZE - sizeof(struct ether_header));

    // Set up the sockaddr_ll structure
    std::memset(&device, 0, sizeof(device));
    device.sll_ifindex = if_idx.ifr_ifindex;
    device.sll_halen = ETH_ALEN;
    std::memcpy(device.sll_addr, dest_mac, 6);

    // Send the packet
    if (sendto(sockfd, buffer, BUF_SIZE, 0, reinterpret_cast<struct sockaddr*>(&device), sizeof(device)) < 0) {
        perror("sendto");
        close(sockfd);
        return 1;
    }

    std::cout << "Packet sent successfully." << std::endl;

    close(sockfd);
    return 0;
}
