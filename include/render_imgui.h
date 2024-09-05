/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2020 terryky1220@gmail.com
 * ------------------------------------------------ */
#ifndef UTIL_IMGUI_H_
#define UTIL_IMGUI_H_

#include "pch.h"
#include "common.h"
#include <string>

typedef struct _imgui_data_t {
} imgui_data_t;

int init_imgui(int width, int height);

void imgui_mousebutton(int button, int state, int x, int y);

void imgui_mousemove(int x, int y);

int invoke_imgui(const std::shared_ptr<AppState>& appState);

#endif /* UTIL_IMGUI_H_ */
 