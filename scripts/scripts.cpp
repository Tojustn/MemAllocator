
#include <algorithm>
#include <chrono>
#include <cstdio>
// Measuring the time it takes to measure a time lol
int main() {
  using clk = std::chrono::steady_clock;
  long long smallest = 1LL << 60;
  for (int i = 0; i < 1'000'000; ++i) {
    auto a = clk::now();
    auto b = clk::now();
    long long d = std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    if (d > 0)
      smallest = std::min(smallest, d);
  }
  std::printf("smallest nonzero step: %lld ns\n", smallest);
}
