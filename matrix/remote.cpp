#include "remote.hpp"

#include "wifi_config.h"

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/apps/mdns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/tcp.h"

#include <cstdio>
#include <cstring>

constexpr int REMOTE_PORT = 2323;
constexpr int RING_SIZE = 128;
constexpr int JOIN_MS = 8000;
constexpr int RETRY_MS = 5000;
constexpr int BADAUTH_MS = 15000;

constexpr const char *HELP = "space selects the next pattern. up and down change brightness by 5%.";

enum class Link {
    Off,
    Idle,
    Joining,
    Up,
};

static Link link = Link::Off;
static absolute_time_t next_join;
static bool mdns_ready = false;
static bool netif_announced = false;

static struct tcp_pcb *listen_pcb = nullptr;
static struct tcp_pcb *client = nullptr;

// Filled from the TCP receive callback inside remote_poll, drained after poll returns.
static uint8_t ring[RING_SIZE];
static uint16_t ring_head = 0;
static uint16_t ring_tail = 0;

static char status_line[96] = "";

static void ring_push(uint8_t byte) {
    uint16_t next = (uint16_t)((ring_head + 1) % RING_SIZE);
    if (next == ring_tail) {
        return;
    }
    ring[ring_head] = byte;
    ring_head = next;
}

static void send_line(struct tcp_pcb *pcb, const char *text) {
    if (pcb == nullptr) {
        return;
    }
    uint16_t length = (uint16_t)strlen(text);
    if (tcp_sndbuf(pcb) < (uint16_t)(length + 2)) {
        return;
    }
    tcp_write(pcb, text, length, TCP_WRITE_FLAG_COPY);
    tcp_write(pcb, "\r\n", 2, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
}

static void close_pcb(struct tcp_pcb *pcb) {
    if (pcb == nullptr) {
        return;
    }
    tcp_arg(pcb, nullptr);
    tcp_accept(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_err(pcb, nullptr);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }
}

extern "C" {

static void on_err(void *arg, err_t err) {
    (void)arg;
    (void)err;
    client = nullptr;
}

static void forget_client(struct tcp_pcb *pcb) {
    if (client == pcb) {
        client = nullptr;
    }
    tcp_recv(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_arg(pcb, nullptr);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *packet, err_t err) {
    (void)arg;
    if (err != ERR_OK || packet == nullptr) {
        if (packet != nullptr) {
            pbuf_free(packet);
        }
        forget_client(pcb);
        return ERR_OK;
    }
    for (struct pbuf *part = packet; part != nullptr; part = part->next) {
        auto *bytes = static_cast<const uint8_t *>(part->payload);
        for (uint16_t i = 0; i < part->len; i++) {
            ring_push(bytes[i]);
        }
    }
    tcp_recved(pcb, packet->tot_len);
    pbuf_free(packet);
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || pcb == nullptr) {
        return ERR_VAL;
    }
    if (client != nullptr) {
        forget_client(client);
    }
    client = pcb;
    tcp_nagle_disable(pcb);
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    send_line(pcb, HELP);
    if (status_line[0] != '\0') {
        send_line(pcb, status_line);
    }
    return ERR_OK;
}

}  // extern "C"

static void stop_server() {
    if (client != nullptr) {
        struct tcp_pcb *pcb = client;
        client = nullptr;
        close_pcb(pcb);
    }
    if (listen_pcb != nullptr) {
        struct tcp_pcb *pcb = listen_pcb;
        listen_pcb = nullptr;
        close_pcb(pcb);
    }
}

static void start_server() {
    if (listen_pcb != nullptr) {
        return;
    }
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb == nullptr) {
        printf("tcp alloc failed\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, REMOTE_PORT) != ERR_OK) {
        tcp_abort(pcb);
        printf("tcp bind failed\n");
        return;
    }
    listen_pcb = tcp_listen_with_backlog(pcb, 1);
    if (listen_pcb == nullptr) {
        tcp_abort(pcb);
        printf("tcp listen failed\n");
        return;
    }
    tcp_accept(listen_pcb, on_accept);
}

static void bring_down() {
    stop_server();
    if (netif_announced && netif_default != nullptr) {
        mdns_resp_remove_netif(netif_default);
        netif_announced = false;
    }
}

static void bring_up() {
    if (!mdns_ready) {
        mdns_resp_init();
        mdns_ready = true;
    }
    const char *address = "0.0.0.0";
    if (netif_default != nullptr) {
        netif_set_hostname(netif_default, "matrix");
        if (mdns_resp_add_netif(netif_default, "matrix") == ERR_OK) {
            netif_announced = true;
        }
        address = ip4addr_ntoa(netif_ip4_addr(netif_default));
    }
    start_server();
    char line[64];
    snprintf(line, sizeof line, "matrix.local port 2323 at %s", address);
    printf("%s\n", line);
}

void remote_init() {
    if (WIFI_SSID[0] == '\0') {
        printf("wifi: set WIFI_SSID in wifi_config.h\n");
        return;
    }
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA) != 0) {
        printf("wifi init failed\n");
        return;
    }
    cyw43_arch_enable_sta_mode();
    link = Link::Idle;
    next_join = get_absolute_time();
}

void remote_poll() {
    if (link == Link::Off) {
        return;
    }
    cyw43_arch_poll();

    if (link == Link::Idle) {
        if (!time_reached(next_join)) {
            return;
        }
        const char *password = WIFI_PASSWORD[0] != '\0' ? WIFI_PASSWORD : nullptr;
        uint32_t auth = password != nullptr ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
        if (cyw43_arch_wifi_connect_async(WIFI_SSID, password, auth) != 0) {
            printf("wifi join start failed\n");
            next_join = make_timeout_time_ms(RETRY_MS);
            return;
        }
        link = Link::Joining;
        next_join = make_timeout_time_ms(JOIN_MS);
        return;
    }

    int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link == Link::Joining) {
        if (status == CYW43_LINK_UP) {
            bring_up();
            link = Link::Up;
            return;
        }
        if (status == CYW43_LINK_BADAUTH || time_reached(next_join)) {
            if (status == CYW43_LINK_BADAUTH) {
                printf("wifi authentication failed\n");
                next_join = make_timeout_time_ms(BADAUTH_MS);
            } else {
                printf("wifi join failed\n");
                next_join = make_timeout_time_ms(RETRY_MS);
            }
            link = Link::Idle;
        }
        return;
    }

    if (status != CYW43_LINK_UP) {
        printf("wifi down\n");
        bring_down();
        link = Link::Idle;
        next_join = make_timeout_time_ms(RETRY_MS);
        return;
    }
    if (listen_pcb == nullptr) {
        start_server();
    }
}

int remote_read() {
    if (ring_tail == ring_head) {
        return -1;
    }
    int byte = ring[ring_tail];
    ring_tail = (uint16_t)((ring_tail + 1) % RING_SIZE);
    return byte;
}

void remote_log(const char *text) {
    printf("%s\n", text);
    send_line(client, text);
}

void remote_status(const char *text) {
    snprintf(status_line, sizeof status_line, "%s", text);
    remote_log(text);
}
