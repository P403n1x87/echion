// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#include <echion/render.h>

#include <iostream>
#include <string_view>

#include <echion/frame.h>
#include <echion/timing.h>

static constexpr std::string_view missing_filename = "<unknown file>";
static constexpr std::string_view missing_name = "<unknown function>";

void WhereRenderer::render_thread_begin(PyThreadState *tstate, std::string_view name,
                                        microsecond_t cpu_time, uintptr_t thread_id,
                                        unsigned long native_id)
{
  (void)tstate;
  (void)cpu_time;
  (void)thread_id;
  (void)native_id;
  *output << "    🧵 " << name << ":" << std::endl;
}

void WhereRenderer::render_frame(Frame &frame)
{
  std::string_view filename_str;
  std::string_view name_str;

  try
  {
    filename_str = string_table.lookup(frame.filename);
  }
  catch (std::out_of_range &e)
  {
    filename_str = missing_filename;
  }

  try
  {
    name_str = string_table.lookup(frame.name);
  }
  catch (std::out_of_range &e)
  {
    name_str = missing_name;
  }

  if (filename_str.rfind("native@", 0) == 0)
  {
    *output << "\033[33;1m" << name_str << "\033[0m (\033[36m" << filename_str
            << "\033[0m:\033[32m" << frame.location.line << "\033[0m)" << std::endl;
  }
  else
  {
    *output << "\033[38;5;248;1m" << name_str << "\033[0m \033[38;5;246m("
            << filename_str << "\033[0m:\033[38;5;246m" << frame.location.line << ")\033[0m"
            << std::endl;
  }
}

inline void MojoRenderer::render_frame(Frame &frame)
{
  std::lock_guard<std::mutex> guard(lock);

  auto key = frame.cache_key;

  if (key == 0)
  {
    event(MOJO_FRAME_INVALID);
  }
  else
  {
    event(MOJO_FRAME_REF);
    ref(key);
  }
}
