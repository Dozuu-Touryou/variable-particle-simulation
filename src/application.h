#pragma once

#include <vector>
#include <array>
#include <string>
#include <chrono>

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include "particle.h"
#include "cell.h"
#include "timer.h"
#include "particle_spawner.h"
#include "simulation_constants.h"

using std::vector;
using std::array;
using std::string;

class Benchmarker;
struct GraphicsPipelineInfoData;

struct FrameResources {
   VkCommandPool commandPool;
   VkCommandBuffer gfxCommandBuffer;
   VkSemaphore imageAcquiredSemaphore;
};

class Application {
public:
   static constexpr uint32_t DynamicStateCount{ 2 };
   static constexpr uint32_t ShaderCount{ 2 };
   static constexpr uint32_t SpecialConstantCount{ 7 };
   static constexpr uint32_t TransferBarrierCount{ 2 };

   bool initialize();
   void run();
   void shutdown();
private:
   enum class SimulationMode {CPU, GPU};

   static constexpr SimulationMode SimMode{ SimulationMode::CPU };
   static constexpr uint64_t Timeout{ std::chrono::nanoseconds(std::chrono::seconds(6)).count() };
   static constexpr uint32_t MaxFramesInFlight{ 2 };
   static constexpr uint32_t DescriptorSetLayoutBindingCount = 2;
   static constexpr uint32_t ComputePipelineCount = 2;
   static constexpr uint32_t QueryAmount = 2;

   SDL_Window* window = nullptr;
   uint32_t windowWidth = 1280;
   uint32_t windowHeight = 720;
   bool running = false;
   uint64_t frameIndex = 0;
   uint64_t nextSignalValue = MaxFramesInFlight + 1;
   VkInstance vulkanInstance = nullptr;
   VkPhysicalDevice physicalDevice = nullptr;
   VkDevice device = nullptr;
   VkSurfaceKHR surface = nullptr;
   VkSurfaceFormatKHR surfaceFormat{
      .format = VK_FORMAT_B8G8R8A8_SRGB,
      .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
   };
   VkSwapchainKHR swapchain = nullptr;
   vector<VkImage> swapchainImages;
   vector<VkImageView> swapchainImageViews;
   vector<VkSemaphore> renderCompleteSemaphores;
   VmaAllocator vmaAllocator = nullptr;
   uint32_t gfxQueueFamIdx = UINT32_MAX;
   VkQueue gfxQueue = nullptr;
   uint32_t swapchainWidth = 0;
   uint32_t swapchainHeight = 0;
   VkShaderModule vertShader = nullptr;
   VkShaderModule fragShader = nullptr;
   VkShaderModule gridCompShader = nullptr;
   VkShaderModule collisionCompShader = nullptr;
   VkPipeline gfxPipeline = nullptr;
   array<VkPipeline, ComputePipelineCount> computePipelines{ nullptr };
   VkPipelineLayout pipelineLayout = nullptr;
   VkSemaphore timelineSemaphore = nullptr;
   array<FrameResources, MaxFramesInFlight> frameResources;
   bool needSwapchainRecreation = false;
   vector<Particle> particles{ sim::ParticleCount };
   VmaAllocation particleBufferAllocation = nullptr;
   Particle* mappedParticleData = nullptr;
   VkBuffer particleBuffer = nullptr;
   VkBuffer stagingBuffer = nullptr;
   VkBuffer gridBuffer = nullptr;
   VmaAllocation stagingBufferAllocation = nullptr;
   VmaAllocation gridBufferAllocation = nullptr;
   ParticleSpawner particleSpawner{};
   Timer timer{};
   vector<Cell> grid{ sim::CellCount };
   VkDescriptorSetLayout descriptorSetLayout = nullptr;
   VkDescriptorPool descriptorPool = nullptr;
   VkDescriptorSet descriptorSet = nullptr;
   VkCommandPool updateCommandPool = nullptr;
   VkCommandBuffer updateCommandBuffer = nullptr;
   VkFence updateFence = nullptr;
   VkQueryPool queryPool = nullptr;
   float timestampPeriod = 0.0f;

   void showError(const string&) const;
   void loopOverEvents(SDL_Event&);
   void processEvent(const SDL_Event&);
   bool initializeParticleArray();
   bool initializeVulkan();
   bool createVulkanInstance();
   bool createSurface();
   bool pickPhysicalDevice();
   bool physDeviceSupportsFeatures();
   bool pickSurfaceFormat();
   bool findGraphicsQueueFamilyId();
   bool queueFamilySupportsPresent(int);
   bool createDevice();
   bool setGfxQueue();
   bool initializeVMA();
   bool createSwapchain();
   bool retrieveSwapchainImages();
   bool createSwapchainImageViews();
   bool createRenderCompleteSemaphores();
   bool createShaders();
   bool createPipelineLayout();
   bool setDescriptorSetLayout();
   bool createComputePipelines();
   VkComputePipelineCreateInfo makeComputePipelineInfo(
      VkShaderModule&, VkSpecializationInfo&, VkPipelineShaderStageCreateInfo&);
   bool createGraphicsPipeline();
   bool createTimelineSemaphores();
   bool createImageAcquiredSemaphores();
   bool createCommandBuffers();
   bool createParticleBuffer();
   bool createUpdateFence();
   bool createQueryPool();
   bool createStagingBuffer();
   bool createGridBuffer();
   bool createDescriptorPool();
   bool allocateDescriptorSet();
   void updateDescriptorSet();
   void update(Benchmarker&);
   bool prepareUpdateFence();
   bool processQueryResults(Benchmarker&);
   bool recordUpdateCommandBuffer(uint32_t, uint32_t, uint32_t);
   bool beginUpdateCommandBuffer();
   void copyStagingBuffer(uint32_t, uint32_t);
   bool submitUpdateCommandBuffer();
   void render();
   bool handleSwapchainRecreation();
   bool recreateSwapchain();
   bool waitOnTimelineSemaphore(uint64_t);
   void destroySwapchain();
   bool handleSwapchainResult(VkResult, const string&, const string&);
   bool recordGfxCommandBuffer(VkCommandBuffer&, uint32_t);
   void recordRenderingCommands(VkCommandBuffer&, uint32_t);
   VkViewport setViewport(VkCommandBuffer&);
   void setSimulationBackground(VkCommandBuffer&, const VkViewport&);
   bool submitGfxCommandBuffer(uint32_t, uint64_t, const FrameResources&);
   void presentFrame(uint32_t);
   VkSwapchainCreateInfoKHR makeSwapchainCreateInfo(uint32_t);
   VkImageViewCreateInfo makeImageViewCreateInfo(size_t);
   VkShaderModule createShaderModule(const string&);
   array<VkPipelineShaderStageCreateInfo, ShaderCount> makeShaderStageCreateInfo();
   VkPipelineRenderingCreateInfo makePipelineRenderingCreateInfo();
   VkGraphicsPipelineCreateInfo makeGraphicsPipelineInfo(GraphicsPipelineInfoData&);
   VkCommandPoolCreateInfo makeCmdPoolCreateInfo(VkCommandPoolCreateFlags);
   VkImageMemoryBarrier2 makeColorAttachmentBarrier(uint32_t);
   VkRenderingAttachmentInfo makeRenderingAttachmentInfo(uint32_t);
   VkRenderingInfo makeRenderingInfo(const VkRenderingAttachmentInfo&);
   VkImageMemoryBarrier2 makePresentLayoutBarrier(uint32_t);
   VkSemaphoreSubmitInfo makeImageAcquireSemaphoreSubmitInfo(const VkSemaphore&);
   VkSemaphoreSubmitInfo makeSemaphoreSubmitInfo(const VkSemaphore&, uint32_t, VkPipelineStageFlagBits2);
   VkSubmitInfo2 makeSubmitInfo(const array<VkSemaphoreSubmitInfo, 3>&, const VkCommandBufferSubmitInfo&);
};

// Helper struct for setDesiredFeaturesList() helper function
struct FeaturesList {
   VkPhysicalDeviceVulkan13Features features13{};
   VkPhysicalDeviceVulkan12Features features12{};
   VkPhysicalDeviceFeatures2 features{};
};

// Helper struct for makeGraphicsPipelineInfo() helper function
struct GraphicsPipelineInfoData {
   array<VkPipelineShaderStageCreateInfo, Application::ShaderCount> shaderStageInfo{};
   VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
   VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
   VkPipelineViewportStateCreateInfo viewportInfo{};
   VkPipelineMultisampleStateCreateInfo multiSampleInfo{};
   VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
   VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
   VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
   VkPipelineRenderingCreateInfo renderingInfo{};
};

// Helper struct for data passed to compute shaders as specialization constants
struct ComputeConstantData {
   uint32_t maxParticlesPerCell = sim::MaxParticlesPerCell;
   uint32_t gridDimension = sim::GridDimension;
   float gravityY{ sim::Gravity.y };
   float timeStep = sim::TimeStep;
   float cellDimension = sim::CellDimension;
   float elasticity = sim::Elasticity;
   float velocityDamper = sim::VelocityDamper;
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
   VkDebugUtilsMessageSeverityFlagBitsEXT,
   VkDebugUtilsMessageTypeFlagsEXT,
   const VkDebugUtilsMessengerCallbackDataEXT*,
   void*);
VkDebugUtilsMessengerCreateInfoEXT makeDebugUtilsMessengerInfo();
VkInstanceCreateInfo makeVkInstanceInfo(
   const VkDebugUtilsMessengerCreateInfoEXT&,
   const VkApplicationInfo&,
   const vector<const char*>&,
   const vector<const char*>&);
VkSpecializationInfo makeSpecializationInfo(
   array<VkSpecializationMapEntry, Application::SpecialConstantCount>&, ComputeConstantData&);
VkPipelineVertexInputStateCreateInfo makeVertexInputStateCreateInfo(
   VkVertexInputBindingDescription&,
   VkVertexInputAttributeDescription*);
VkPipelineViewportStateCreateInfo makePipelineViewportInfo();
VkPipelineRasterizationStateCreateInfo makePipelineRasterizationInfo();
VkPipelineColorBlendStateCreateInfo makeColorBlendCreateInfo(VkPipelineColorBlendAttachmentState&);
VkPipelineDynamicStateCreateInfo makeDynamicStateInfo(
   array<VkDynamicState, Application::DynamicStateCount>&);
VkCommandBufferAllocateInfo makeCmdBufferAllocateInfo(const VkCommandPool&);
VkDependencyInfo makeBufferBarrierDependInfo(
   const array<VkBufferMemoryBarrier2, Application::TransferBarrierCount>&);
VkBufferMemoryBarrier2 makeTransferBufferBarrier(const VkBuffer&, VkPipelineStageFlagBits2);
VkBufferMemoryBarrier2 makeComputeBufferBarrier(const VkBuffer&, VkPipelineStageFlagBits2);
VkBufferMemoryBarrier2 makeTransitionBufferBarrier(const VkBuffer&);
VkDependencyInfo makeBarrierDependInfo(const VkImageMemoryBarrier2&, const VkBufferMemoryBarrier2*);
void setDesiredFeaturesList(FeaturesList&);
vector<uint32_t> readSpvFile(const string&);
bool isPositveMultipleOf(std::streamsize, uint32_t, string, string);