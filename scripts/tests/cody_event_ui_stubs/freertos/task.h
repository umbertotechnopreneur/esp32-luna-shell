#pragma once

#include "FreeRTOS.h"

uint32_t ulTaskNotifyTake(bool clear_on_exit, uint32_t timeout);
void xTaskNotifyGive(TaskHandle_t task);
TaskHandle_t xTaskCreateStatic(void (*worker)(void *), const char *name,
    uint32_t stack_bytes, void *argument, uint32_t priority,
    StackType_t *stack, StaticTask_t *storage);
