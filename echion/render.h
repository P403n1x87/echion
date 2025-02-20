// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <functional>
#include <iostream>
#include <string_view>

#include "timing.h" // microsecond_t

#include <echion/mojo.h>

class RendererInterface
{
public:
  virtual void render_message(std::string_view msg) = 0;
  virtual void render_thread_begin(PyThreadState *tstate, std::string_view name,
                                   microsecond_t cpu_time, uintptr_t thread_id,
                                   unsigned long native_id) = 0;
  virtual void render_task_begin(std::string_view name) = 0;
  virtual void render_stack_begin() = 0;
  virtual void render_python_frame(std::string_view name, std::string_view file,
                                   uint64_t line) = 0;
  virtual void render_native_frame(std::string_view name, std::string_view file,
                                   uint64_t line) = 0;
  virtual void render_cpu_time(uint64_t cpu_time) = 0;
  virtual void render_stack_end() = 0;

  // The validity of the interface is a two-step process
  // 1. If the RendererInterface has been destroyed, obviously it's invalid
  // 2. There might be state behind RendererInterface, and the lifetime of that
  //    state alone may be insufficient to know its usability.  is_valid
  //    should return false in such cases.
  virtual bool is_valid() = 0;
  virtual ~RendererInterface() = default;
};

class WhereRenderer : public RendererInterface
{
private:
  std::ostream *output;
  std::ofstream file_stream;

  WhereRenderer() {}
  ~WhereRenderer() {}

public:
  static WhereRenderer &get()
  {
    static WhereRenderer instance;
    return instance;
  }

  WhereRenderer(WhereRenderer &) = delete;
  WhereRenderer(WhereRenderer &&) = delete;
  void operator=(const WhereRenderer &) = delete;

  bool set_output(std::string_view file_name)
  {
    file_stream.close();
    file_stream.open(file_name.data(), std::ios::out);
    if (file_stream.is_open())
    {
      output = &file_stream;
      return true;
    }
    return false;
  }

  bool set_output(std::ostream &new_output)
  {
    file_stream.close();
    output = &new_output;
    return true;
  }

  void render_thread_begin(PyThreadState *tstate, std::string_view name,
                           microsecond_t cpu_time, uintptr_t thread_id,
                           unsigned long native_id) override
  {
    (void)tstate;
    (void)cpu_time;
    (void)thread_id;
    (void)native_id;
    ;
    *output << "    🧵 " << name << ":" << std::endl;
  }

  void render_task_begin(std::string_view name) override
  {
    *output << "  📝 " << name << ":" << std::endl;
  }

  void render_stack_begin() override { return; }

  void render_message(std::string_view msg) override
  {
    *output << msg << std::endl;
  }

  void render_python_frame(std::string_view name, std::string_view filename,
                           uint64_t line) override
  {
    *output << "\033[38;5;248;1m" << name << "\033[0m \033[38;5;246m("
            << filename << "\033[0m:\033[38;5;246m" << line << ")\033[0m"
            << std::endl;
  }

  void render_native_frame(std::string_view name, std::string_view filename,
                           uint64_t line) override
  {
    *output << "\033[33;1m" << name << "\033[0m (\033[36m" << filename
            << "\033[0m:\033[32m" << line << "\033[0m)" << std::endl;
  }

  void render_stack_end() override { return; }

  void render_cpu_time(uint64_t cpu_time) override
  {
    *output << " " << cpu_time << std::endl;
  }

  bool is_valid() override { return true; }
};

class MojoRenderer : public RendererInterface
{
private:
  MojoWriter mojo;

public:
  void render_message(std::string_view msg) {};
  void render_thread_begin(PyThreadState *tstate, std::string_view name,
                           microsecond_t cpu_time, uintptr_t thread_id,
                           unsigned long native_id) {};
  void render_task_begin(std::string_view name) {};
  void render_stack_begin() {};
  void render_python_frame(std::string_view name, std::string_view file,
                           uint64_t line) {};
  void render_native_frame(std::string_view name, std::string_view file,
                           uint64_t line) {};
  void render_cpu_time(uint64_t cpu_time) {};
  void render_stack_end() {};
  bool is_valid() override
  {
    return true;
  }
};

class Renderer
{
private:
  std::shared_ptr<RendererInterface> default_renderer =
      std::make_shared<MojoRenderer>();
  std::weak_ptr<RendererInterface> currentRenderer;

  std::shared_ptr<RendererInterface> getActiveRenderer()
  {
    if (auto renderer = currentRenderer.lock())
    {
      if (renderer->is_valid())
      {
        return renderer;
      }
    }
    return default_renderer;
  }

  Renderer() = default;
  ~Renderer() = default;

public:
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

  bool set_output(std::string_view file_name)
  {
    // Have to cause default_renderer to a WhereRenderer type before calling
    return std::dynamic_pointer_cast<WhereRenderer>(default_renderer)
        ->set_output(file_name);
  }

  bool set_output(std::ostream &new_output)
  {
    return std::dynamic_pointer_cast<WhereRenderer>(default_renderer)
        ->set_output(new_output);
  }

  static Renderer &get()
  {
    static Renderer instance;
    return instance;
  }

  void set_renderer(std::shared_ptr<RendererInterface> renderer)
  {
    currentRenderer = renderer;
  }

  void render_message(std::string_view msg)
  {
    getActiveRenderer()->render_message(msg);
  }

  void render_thread_begin(PyThreadState *tstate, std::string_view name,
                           microsecond_t cpu_time, uintptr_t thread_id,
                           unsigned long native_id)
  {
    getActiveRenderer()->render_thread_begin(tstate, name, cpu_time, thread_id,
                                             native_id);
  }

  void render_task_begin(std::string_view name)
  {
    getActiveRenderer()->render_task_begin(name);
  }

  void render_stack_begin() { getActiveRenderer()->render_stack_begin(); }

  void render_python_frame(std::string_view name, std::string_view filename,
                           uint64_t line)
  {
    getActiveRenderer()->render_python_frame(name, filename, line);
  }

  void render_native_frame(std::string_view name, std::string_view filename,
                           uint64_t line)
  {
    getActiveRenderer()->render_native_frame(name, filename, line);
  }

  void render_cpu_time(uint64_t cpu_time)
  {
    getActiveRenderer()->render_cpu_time(cpu_time);
  }

  void render_stack_end() { getActiveRenderer()->render_stack_end(); }
};
