#pragma once
#define WIFI_SSID "TOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASS"

// ── Fixed LAN address for the web dashboard ──────────────────────────────
// Must be in your router's subnet and OUTSIDE its DHCP range.
// Check your network with "ipconfig" (Windows) / "ip a" (Linux/macOS).
// Comment out USE_STATIC_IP to let the router assign an address (DHCP).
#define USE_STATIC_IP
#define STATIC_IP    192, 168, 1, 50
#define STATIC_GW    192, 168, 1, 1
#define STATIC_MASK  255, 255, 255, 0
#define STATIC_DNS   192, 168, 1, 1   // usually the router; needed for NTP

// Also reachable as http://rj45-board.local (mDNS; works on most PCs/phones)
#define MDNS_NAME "rj45-board"
