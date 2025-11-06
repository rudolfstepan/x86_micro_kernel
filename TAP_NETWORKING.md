# TAP Networking Setup for NE2000 Testing

## Quick Start

### Option 1: Automatic Setup (Recommended)
```bash
sudo make run-net-tap-sudo
```
This will:
- Create TAP interface `tap0`
- Assign host IP: `10.0.2.1/24`
- Start QEMU with NE2000 connected to TAP
- Your kernel MAC: `52:54:00:12:34:56`

### Option 2: Manual Setup
```bash
# 1. Create TAP interface (run once)
make setup-tap

# 2. Run QEMU (no sudo needed after setup)
make run-net-tap
```

## Testing Network Communication

### In the Kernel Shell:

1. **Check Network Status:**
   ```
   net status
   ```
   Should show: `NE2000 MAC address: 52:54:00:12:34:56`

2. **Send a Packet:**
   ```
   net send
   ```
   Sends broadcast packet to LAN

3. **Listen for Incoming Packets:**
   ```
   net listen 10
   ```
   Monitors for up to 10 packets

4. **Receive Single Packet:**
   ```
   net recv
   ```
   Try to receive one packet

### From Host Machine:

1. **Ping the TAP interface (host side):**
   ```bash
   ping 10.0.2.1
   ```

2. **Send packets to kernel:**
   ```bash
   # Send ARP request to kernel's MAC
   sudo arping -I tap0 -c 1 52:54:00:12:34:56
   
   # Or broadcast ping (if kernel implements ICMP)
   ping -b 10.0.2.255
   ```

3. **Monitor traffic with tcpdump:**
   ```bash
   sudo tcpdump -i tap0 -XX
   ```

4. **Send raw ethernet frame:**
   ```bash
   # Install if needed: sudo apt-get install packeth
   sudo packeth
   ```

## Network Configuration

- **Host (TAP) IP:** `10.0.2.1`
- **Network:** `10.0.2.0/24`
- **Kernel MAC:** `52:54:00:12:34:56`
- **Available IPs for kernel:** `10.0.2.2` - `10.0.2.254`

## Cleanup

```bash
make cleanup-tap
```

## Troubleshooting

### No packets received:
1. Check if TAP is up: `ip link show tap0`
2. Check host firewall: `sudo iptables -L`
3. Enable promiscuous mode: `net info` (check RCR register)

### Permission denied:
- TAP operations require sudo
- Use `sudo make run-net-tap-sudo`

### Packets sent but not captured:
- Run `sudo tcpdump -i tap0` on host
- Verify MAC address matches in both directions

## Next Steps

1. Implement ARP protocol in kernel
2. Add IP stack (IPv4)
3. Implement ICMP (ping response)
4. Add UDP/TCP support

