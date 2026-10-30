#pragma once

#include <chrono>

using std::chrono::steady_clock;
using std::string;

class Benchmarker {
public:
   void startRecording();
   void stopRecording();
   void addExecutionTime(double);
   void logResults(const string&, const string&, const string&, uint32_t) const;
   bool warmupComplete() const { return warmupCount >= WarmupIterations; }
   uint32_t getWarmupCount() const { return warmupCount; }
   uint32_t getIterationCount() const { return iterationCount; }
   void incrementWarmupCount(uint32_t);
   void incrementIterationCount() {
      if (warmupComplete())
         iterationCount++;
   }
   bool stopSimulation() const { return iterationCount >= IterationsToRecord; }
private:
   static constexpr uint32_t IterationsToRecord = 1000;
   static constexpr uint32_t WarmupIterations = 400;

   double executionTimes[IterationsToRecord]{};
   steady_clock::time_point startTime;
   uint32_t currentId = 0;
   uint32_t iterationCount = 0;
   uint32_t warmupCount = 0;
};

string lowercase(string);