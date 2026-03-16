#include "rf_capture.h"
#include "rf_uart.h"

static void on_frame_ready(const rf_frame_t *frame, void *user) {
    (void)user;
    rf_uart_send_frame(frame);
}

int main(void) {
    rf_capture_ctx_t cap;

    rf_uart_hw_init_usart1_tx(115200u);
    rf_capture_init(&cap, on_frame_ready, NULL);

    /* No RF hardware: generate EV1527 code 0x12A5C3 and upload through UART format. */
    rf_capture_feed_virtual(&cap, 0x12A5C3u, 300u, 3u);
    return 0;
}
