#include "timer.h"

#include "parallel.h"

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

Timer::Timer() {
  Parallel::barrier();
  const auto& now = std::chrono::high_resolution_clock::now();
  init_time = prev_time = now;
  is_master = Parallel::get_proc_id() == 0;
  if (is_master) {
    const time_t start_time = std::chrono::system_clock::to_time_t(now);
    printf("\nStart time: %s", asctime(localtime(&start_time)));
    printf("Format: " ANSI_COLOR_YELLOW "[DIFF/SECTION/TOTAL]" ANSI_COLOR_RESET "\n");
  }
}

void Timer::start(const std::string& event) {
  Parallel::barrier();
  const auto& now = std::chrono::high_resolution_clock::now();
  auto& instance = get_instance();
  instance.start_times.push_back(std::make_pair(event, now));
  if (instance.is_master) {
    printf("\n" ANSI_COLOR_GREEN "[START] " ANSI_COLOR_RESET);
    instance.print_event_path();
  }
  instance.prev_time = now;
}

void Timer::end() {
  Parallel::barrier();
  const auto& now = std::chrono::high_resolution_clock::now();
  auto& instance = get_instance();
  if (instance.is_master) {
    printf(ANSI_COLOR_GREEN "[=END=] " ANSI_COLOR_RESET);
    instance.print_event_path();
  }
  instance.start_times.pop_back();
  instance.prev_time = now;
}

void Timer::print_event_path() const {
  for (size_t i = 0; i < start_times.size() - 1; i++) {
    printf("%s >> ", start_times[i].first.c_str());
  }
  printf("%s ", start_times.back().first.c_str());
  const auto& now = std::chrono::high_resolution_clock::now();
  const auto& event_start_time = start_times.back().second;
  printf(
      ANSI_COLOR_YELLOW "[%.3f/%.3f/%.3f]\n" ANSI_COLOR_RESET,
      get_duration(prev_time, now),
      get_duration(event_start_time, now),
      get_duration(init_time, now));
}

double Timer::get_duration(
    const std::chrono::high_resolution_clock::time_point start,
    const std::chrono::high_resolution_clock::time_point end) const {
  return (std::chrono::duration_cast<std::chrono::duration<double>>(end - start)).count();
}
