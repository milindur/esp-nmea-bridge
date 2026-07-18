#ifndef WIFI_MANAGER_H_
#define WIFI_MANAGER_H_

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/net/net_if.h>

int wifi_manager_start(void);
struct net_if *wifi_manager_sta_iface(void);
bool wifi_manager_sta_ready(void);
bool wifi_manager_get_sta_gateway(struct net_in_addr *gw);
bool wifi_manager_get_sta_ipv4(char *buf, size_t buf_len);
bool wifi_manager_get_sta_rssi(int *rssi_dbm);

#endif /* WIFI_MANAGER_H_ */
