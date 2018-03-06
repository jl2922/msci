#pragma once

#include <chrono>
#include <string>
#include <vector>

class Timer {
 public:
  static Timer& get_instance() {
    static Timer instance;
    return instance;
  }

  static void start(const std::string& event);

  static void end();

 private:
  Timer();

  void print_status() const;

  double get_duration(
      const std::chrono::high_resolution_clock::time_point start,
      const std::chrono::high_resolution_clock::time_point end) const;

  std::chrono::high_resolution_clock::time_point init_time;

  std::chrono::high_resolution_clock::time_point prev_time;

  std::vector<std::pair<std::string, std::chrono::high_resolution_clock::time_point>> start_times;

  bool is_master;
};