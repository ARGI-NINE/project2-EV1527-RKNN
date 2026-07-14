#include <stdint.h>
#include <stdio.h>

#include <vector>

#include "postprocess.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static int run_empty_postprocess(void) {
    const int model_size = 32;
    std::vector<int8_t> head0((model_size / 8) * (model_size / 8) * 3 * PROP_BOX_SIZE, -128);
    std::vector<int8_t> head1((model_size / 16) * (model_size / 16) * 3 * PROP_BOX_SIZE, -128);
    std::vector<int8_t> head2((model_size / 32) * (model_size / 32) * 3 * PROP_BOX_SIZE, -128);
    std::vector<int32_t> zero_points(3, 0);
    std::vector<float> scales(3, 1.0f);
    detect_result_group_t group;
    BOX_RECT pads = {0, 0, 0, 0};

    const int rc = post_process(
        head0.data(),
        head1.data(),
        head2.data(),
        model_size,
        model_size,
        BOX_THRESH,
        NMS_THRESH,
        pads,
        1.0f,
        1.0f,
        zero_points,
        scales,
        &group
    );
    CHECK(rc == 0);
    CHECK(group.count == 0);
    return 0;
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    CHECK(setLabelNamePath(argv[1]) == 0);
    CHECK(run_empty_postprocess() == 0);

    deinitPostProcess();
    CHECK(run_empty_postprocess() == 0);
    CHECK(setLabelNamePath("a-different-label-file.txt") == -1);
    return 0;
}
