#pragma once

// Wi-Fi TCP session on port 2323. Bytes arrive through remote_read.
// Call remote_init after the system clock is set and before the panel driver claims PIO.

void remote_init();
void remote_poll();
int remote_read();
void remote_log(const char *text);
void remote_status(const char *text);
