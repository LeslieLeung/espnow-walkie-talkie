#pragma once

#include "walkie/bsp.hpp"
#include "walkie/ui_model.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace walkie {

class Ui {
public:
    explicit Ui(BoardBsp& bsp) : bsp_(bsp) {}
    bool start();
    bool publish(const UiSnapshot& snapshot);

private:
    static void task_entry(void* context);
    void run();

    BoardBsp& bsp_;
    SemaphoreHandle_t model_mutex_{nullptr};
    TaskHandle_t task_handle_{nullptr};
    UiDeliveryPolicy delivery_{};
};

}  // namespace walkie
