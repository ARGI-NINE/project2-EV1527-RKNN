#include "rf_capture.h"
#include "rf_uart.h"

static void on_frame_ready(const rf_frame_t *frame, void *user) {
    (void)user;
    rf_uart_send_frame(frame);
}

int main(void) {
    rf_capture_ctx_t cap;
    uint16_t ccr_stream[64];
    uint16_t i = 0u;
    uint16_t ccr = 1000u;
    rf_frame_t vf;

    rf_uart_hw_init_usart1_tx(115200u);
    rf_capture_online_init(&cap, on_frame_ready, NULL, 1000000u, 0xFFFFu);
    rf_capture_set_filter(&cap, 80u, 60000u, 8000u, 48u);

    rf_capture_generate_virtual_frame(&vf, 0x12A5C3u, 300u);
    for (i = 0u; i < vf.len && i < (uint16_t)(sizeof(ccr_stream) / sizeof(ccr_stream[0])); ++i) {
        ccr = (uint16_t)(ccr + vf.pulse[i]);
        ccr_stream[i] = ccr;
    }
    rf_capture_process_dma(&cap, ccr_stream, vf.len);
    rf_capture_flush(&cap);
    return 0;
}
