#pragma once

#include <chrono>

using std::chrono::duration;
using std::chrono::steady_clock;

class Timer {
public:
   Timer() : lastFrameTime(steady_clock::now()) {}

   void tick() {
      steady_clock::time_point currentFrameTime = steady_clock::now();
      frameSeconds = currentFrameTime - lastFrameTime;
      lastFrameTime = currentFrameTime;
   }
   void reset() {
      lastFrameTime = steady_clock::now();
      accumulator = 0.0f;
   }
   void incrementAccumulator() { accumulator += frameSeconds.count(); }
   void decrementAccumulator(float decrement) { accumulator -= decrement; }
   float getAccumulator() const { return accumulator; }
   uint64_t getTimestamp() const { return timestamp; }
   void incrementTimestamp() { timestamp++; }
private:
   duration<float> frameSeconds{};
   steady_clock::time_point lastFrameTime{};
   float accumulator = 0.0f;
   uint32_t timestamp = 0;
};