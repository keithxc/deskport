#!/usr/bin/env python3
"""Remove Sunshine's 10 ms absolute-mode left-button release delay."""
from pathlib import Path
import sys

path = Path(sys.argv[1]) / 'src/input.cpp'
source = path.read_text()

marker = '// DeskPort: deliver absolute-mode left-button releases without a 10 ms touch-right-click delay.'
if marker in source:
    raise SystemExit(0)

old = '''    if (button == BUTTON_LEFT && release && !input->mouse_left_button_timeout) {
      auto f = [=]() {
        auto left_released = mouse_press[BUTTON_LEFT];
        if (left_released) {
          // Already released left button
          return;
        }
        platf::button_mouse(platf_input, BUTTON_LEFT, release);

        mouse_press[BUTTON_LEFT] = false;
        input->mouse_left_button_timeout = nullptr;
      };

      input->mouse_left_button_timeout = task_pool.pushDelayed(std::move(f), 10ms).task_id;

      return;
    }'''

if source.count(old) != 1:
    raise SystemExit('Mouse latency anchor mismatch: src/input.cpp left-button delay block')

path.write_text(source.replace(old, f'    {marker}\n', 1))
