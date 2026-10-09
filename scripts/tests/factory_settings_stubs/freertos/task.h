#pragma once
#include "FreeRTOS.h"

TaskHandle_t xTaskCreateStatic(void (*)(void *), const char *, uint32_t, void *,
                             UBaseType_t, StackType_t *, StaticTask_t *);
BaseType_t xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
