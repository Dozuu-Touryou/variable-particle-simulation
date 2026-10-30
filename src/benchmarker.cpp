#include <filesystem>
#include <fstream>

#include "benchmarker.h"
#include "simulation_constants.h"


// Record initial timestamp for CPU frame execution time, if warm-up frames have elapsed
void Benchmarker::startRecording()
{
   if (!warmupComplete())
      return;
   startTime = steady_clock::now();
}

// Record ending timestamp for CPU frame execution time, if warm-up frames have elapsed.
// Then save computed time to array of execution times, provided there is space
void Benchmarker::stopRecording()
{
   using std::chrono::duration;
   if (!warmupComplete())
      return;
   double iterationMs = duration<double, std::milli>(steady_clock::now() - startTime).count();
   addExecutionTime(iterationMs);
}

// Fill the next slot in the array of execution times, if one is available
void Benchmarker::addExecutionTime(double executionTime)
{
   if (currentId < IterationsToRecord)
      executionTimes[currentId++] = executionTime;
}

// Save all recorded execution times with additional metadata to a csv file
void Benchmarker::logResults(
   const string& deviceName, const string& varType, const string& varValue, uint32_t runNumber) const
{
   using std::format;
   string folderPath = format("results/{}_{}", lowercase(varType), varValue);
   std::filesystem::create_directories(folderPath);
   const char* const filenamePattern =  "{}/{}_{}_{}_run#{}.csv";
   string filename = format(
      filenamePattern, folderPath, lowercase(deviceName), lowercase(varType), varValue, runNumber);
   std::ofstream csvWriter(filename.data(), std::ios::out | std::ios::trunc);
   const auto linePattern = "{},{},{}:{},run#{},{}";
   for (int i = 0; i < currentId; ++i) {
      csvWriter << format(linePattern, i + 1, deviceName, varType, varValue, runNumber, executionTimes[i]);
      if (i < currentId - 1)
         csvWriter << "\n";
   }
}

// Increment warm-up counter if desired warm-up iteration haven't elapsed and all particles have spawned
void Benchmarker::incrementWarmupCount(uint32_t spawnedParticleCount)
{
   if (warmupCount > WarmupIterations)
      return;
   if (spawnedParticleCount >= sim::ParticleCount)
      warmupCount++;
}

string lowercase(string text)
{
   for (char& symbol : text)
      symbol = tolower(symbol);
   return text;
}