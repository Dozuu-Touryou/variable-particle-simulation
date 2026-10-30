#define VOLK_IMPLEMENTATION
#define VMA_IMPLEMENTATION

#include <iostream>
#include <fstream>
#include <format>

#include <Volk/volk.h>
#include <vma/vk_mem_alloc.h>
#include <SDL3/SDL_vulkan.h>

#include "application.h"
#include "cpu_physics.h"
#include "benchmarker.h"

bool Application::initialize()
{
   if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
      showError("Failed to initialize SDL video subsystem");
      return false;
   }
   constexpr uint64_t windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
   window = SDL_CreateWindow(
      "Simulation of Variably Sized Particles: CPU, 1∕120 time step, 128x128 grid resolution, 8000 particles",
      windowWidth, windowHeight, windowFlags);
   if (!window) {
      showError("Failed to create SDL window");
      return false;
   }
   if (!initializeParticleArray())
      return false;
   if (!initializeVulkan())
      return false;
   return true;
}

void Application::run()
{
   running = true;
   Benchmarker benchmarker{};
   timer.reset();
   while (running) {
      timer.tick();
      timer.incrementAccumulator();
      SDL_Event event{ 0 };
      loopOverEvents(event);
      while (timer.getAccumulator() >= sim::TimeStep && running) {
         //running = !benchmarker.stopSimulation();
         update(benchmarker);
         timer.decrementAccumulator(sim::TimeStep);
         benchmarker.incrementIterationCount();
         benchmarker.incrementWarmupCount(particleSpawner.getSpawnedParticles());
      }
      if (!running)
         break;
      render();
   }
   string deviceType = SimMode == SimulationMode::CPU ? "CPU" : "GPU";
   //benchmarker.logResults(deviceType, "timestep", "1∕120", 1);
}

void Application::shutdown()
{
   vkDeviceWaitIdle(device);
   if (descriptorPool)
      vkDestroyDescriptorPool(device, descriptorPool, nullptr);
   if (descriptorSetLayout)
      vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
   if (gridBuffer && gridBufferAllocation)
      vmaDestroyBuffer(vmaAllocator, gridBuffer, gridBufferAllocation);
   if (stagingBuffer && stagingBufferAllocation)
      vmaDestroyBuffer(vmaAllocator, stagingBuffer, stagingBufferAllocation);
   if (updateFence)
      vkDestroyFence(device, updateFence, nullptr);
   if (queryPool)
      vkDestroyQueryPool(device, queryPool, nullptr);
   if (particleBuffer && particleBufferAllocation)
      vmaDestroyBuffer(vmaAllocator, particleBuffer, particleBufferAllocation);
   if (timelineSemaphore)
      vkDestroySemaphore(device, timelineSemaphore, nullptr);
   if (updateCommandPool)
      vkDestroyCommandPool(device, updateCommandPool, nullptr);
   for (FrameResources& frameRes : frameResources) {
      vkDestroyCommandPool(device, frameRes.commandPool, nullptr);
      vkDestroySemaphore(device, frameRes.imageAcquiredSemaphore, nullptr);
   }
   if (pipelineLayout)
      vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
   for (VkPipeline& computePipeline : computePipelines)
      if (computePipeline)
         vkDestroyPipeline(device, computePipeline, nullptr);
   if (gfxPipeline)
      vkDestroyPipeline(device, gfxPipeline, nullptr);
   if (gridCompShader)
      vkDestroyShaderModule(device, gridCompShader, nullptr);
   if (collisionCompShader)
      vkDestroyShaderModule(device, collisionCompShader, nullptr);
   if (vertShader)
      vkDestroyShaderModule(device, vertShader, nullptr);
   if (fragShader)
      vkDestroyShaderModule(device, fragShader, nullptr);
   destroySwapchain();
   if (vmaAllocator)
      vmaDestroyAllocator(vmaAllocator);
   if (device)
      vkDestroyDevice(device, nullptr);
   if (surface)
      vkDestroySurfaceKHR(vulkanInstance, surface, nullptr);
   if (vulkanInstance)
      vkDestroyInstance(vulkanInstance, nullptr);
   if (window)
      SDL_DestroyWindow(window);
   SDL_Quit();
}

// Attempt to show an error in an SDL message box, if possible. Otherwise, print it in the console
void Application::showError(const string& errorMsg) const
{
   if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMsg.c_str(), window))
      std::cerr << "SDL_ShowSimpleMessageBox() failed:\n" << SDL_GetError() <<
      "\nThe message it tried to show was:\n" << errorMsg << '\n';
}

// Loop over SDL_Events
void Application::loopOverEvents(SDL_Event& event)
{
   while (SDL_PollEvent(&event))
      processEvent(event);
}

// Process an SDL_Event based on its type
void Application::processEvent(const SDL_Event& event)
{
   switch (event.type) {
   case SDL_EVENT_KEY_DOWN:
      if (event.key.key != SDLK_ESCAPE)
         break;
      [[fallthrough]];
   case SDL_EVENT_QUIT:
      running = false;
      break;
   case SDL_EVENT_WINDOW_RESIZED:
      windowWidth = event.window.data1;
      windowHeight = event.window.data2;
   }
}

bool Application::initializeParticleArray()
{
   if (!particleSpawner.initializeParticles(particles)) {
      const auto errorMsg = "The area of {} particles exceeds {}% of the simulation area ({}).\n"
         "Retry with less particles or smaller bounds for particle radii.";
      showError(std::format(errorMsg, sim::ParticleCount, sim::MaxFillFraction * 100, sim::FillableArea));
      return false;
   }
   particleSpawner.updateWaveData(particles);
   particleSpawner.initializeWaveTimer();
   return true;
}

bool Application::initializeVulkan()
{
   if (!createVulkanInstance())
      return false;
   if (!createSurface())
      return false;
   if (!pickPhysicalDevice())
      return false;
   if (!physDeviceSupportsFeatures())
      return false;
   if (!pickSurfaceFormat())
      return false;
   if (!findGraphicsQueueFamilyId())
      return false;
   if (!createDevice())
      return false;
   if (!setGfxQueue())
      return false;
   if (!initializeVMA())
      return false;
   if (!createSwapchain())
      return false;
   if (!retrieveSwapchainImages())
      return false;
   if (!createSwapchainImageViews())
      return false;
   if (!createRenderCompleteSemaphores())
      return false;
   if (!createShaders())
      return false;
   if (!createPipelineLayout())
      return false;
   if (SimMode == SimulationMode::GPU)
      if (!createComputePipelines())
         return false;
   if (!createGraphicsPipeline())
      return false;
   if (!createTimelineSemaphores())
      return false;
   if (!createImageAcquiredSemaphores())
      return false;
   if (!createCommandBuffers())
      return false;
   if (!createParticleBuffer())
      return false;
   if (SimMode == SimulationMode::GPU) {
      if (!createUpdateFence())
         return false;
      if (!createQueryPool())
         return false;
      if (!createStagingBuffer())
         return false;
      if (!createGridBuffer())
         return false;
      if (!createDescriptorPool())
         return false;
      if (!allocateDescriptorSet())
         return false;
      updateDescriptorSet();
   }
   return true;
}

bool Application::createVulkanInstance()
{
   if (volkInitialize() != VK_SUCCESS) {
      showError("Failed to initialize volk library");
      return false;
   }
   VkApplicationInfo appInfo{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "Variable Particle Simulation",
      .apiVersion = VK_API_VERSION_1_4    // As Vulkan Instance Version is 1.4.350
   };
   uint32_t instanceExtCount = 0;
   const char* const* extensionNames = SDL_Vulkan_GetInstanceExtensions(&instanceExtCount);
   vector<const char*> extensions{
   #ifdef _DEBUG
      VK_EXT_DEBUG_UTILS_EXTENSION_NAME
   #endif
   };
   for (uint32_t i = 0; i < instanceExtCount; ++i)
      extensions.push_back(extensionNames[i]);
   vector<const char*> layers{
   #ifdef _DEBUG
      "VK_LAYER_KHRONOS_validation"
   #endif
   };
   auto debugInfo = makeDebugUtilsMessengerInfo();
   auto instanceInfo = makeVkInstanceInfo(debugInfo, appInfo, layers, extensions);
   if (vkCreateInstance(&instanceInfo, nullptr, &vulkanInstance) != VK_SUCCESS) {
      showError("Failed to obtain a valid VkInstance handle");
      return false;
   }
   volkLoadInstance(vulkanInstance);
   return true;
}

bool Application::createSurface()
{
   if (!SDL_Vulkan_CreateSurface(window, vulkanInstance, nullptr, &surface)) {
      showError("Failed to create a Vulkan surface");
      return false;
   }
   return true;
}

// Attempt to pick a discrete GPU physical device.
// Otherwise, pick any physical device available
bool Application::pickPhysicalDevice()
{
   uint32_t physDeviceCount = 0;
   VkResult result = vkEnumeratePhysicalDevices(vulkanInstance, &physDeviceCount, nullptr);
   if (result != VK_SUCCESS) {
      showError("vkEnumeratePhysicalDevices() failed to return # of physical devices");
      return false;
   }
   if (physDeviceCount == 0) {
      showError("No Vulkan physical devices found");
      return false;
   }
   vector<VkPhysicalDevice> physicalDevices(physDeviceCount);
   result = vkEnumeratePhysicalDevices(vulkanInstance, &physDeviceCount, physicalDevices.data());
   if (result != VK_SUCCESS) {
      showError("vkEnumeratePhysicalDevices() couldn't return the available VkPhysicalDevice handles");
      return false;
   }
   physicalDevice = physicalDevices[0];
   for (auto& physDevice : physicalDevices) {
      VkPhysicalDeviceProperties properties{};
      vkGetPhysicalDeviceProperties(physDevice, &properties);
      if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
         physicalDevice = physDevice;
         timestampPeriod = properties.limits.timestampPeriod;
         break;
      }
   }
   return true;
}

// Check if the selected physical device supports the features desired for this project
bool Application::physDeviceSupportsFeatures()
{
   VkPhysicalDeviceVulkan14Features supportedFeatures14{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
      .pNext = nullptr
   };
   VkPhysicalDeviceVulkan13Features supportedFeatures13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &supportedFeatures14
   };
   VkPhysicalDeviceVulkan12Features supportedFeatures12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &supportedFeatures13
   };
   VkPhysicalDeviceFeatures2 supportedFeatures{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &supportedFeatures12
   };
   vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);
   if (!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2 ||
      !supportedFeatures13.shaderDemoteToHelperInvocation || !supportedFeatures12.timelineSemaphore) {
      showError("Physical device doesn't meet the feature requirements");
      return false;
   }
   return true;
}

// Check if physicalDevice supports any surface formats.
// Also, whether it supports the desired surface format and if not use any available
bool Application::pickSurfaceFormat()
{
   uint32_t formatCount = 0;
   VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
   if (result != VK_SUCCESS) {
      showError("vkGetPhysicalDeviceSurfaceFormatsKHR() couldn't get the # of supported surface formats");
      return false;
   }
   if (formatCount == 0) {
      showError("Physical device does not support any surface formats");
      return false;
   }
   vector<VkSurfaceFormatKHR> formats(formatCount);
   result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
   if (result != VK_SUCCESS) {
      showError("vkGetPhysicalDeviceSurfaceFormatsKHR() couldn't retrieve supported surface formats");
      return false;
   }
   for (auto& fmt : formats)
      if (fmt.format == surfaceFormat.format && fmt.colorSpace == surfaceFormat.colorSpace)
         return true;
   surfaceFormat.format = formats[0].format;
   surfaceFormat.colorSpace = formats[0].colorSpace;
   return true;
}

// Attempt to find a graphics ( and compute in GPU version) queue family with presentation support
bool Application::findGraphicsQueueFamilyId()
{
   uint32_t queueFamCount = 0;
   vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamCount, nullptr);
   vector<VkQueueFamilyProperties2> queueFamProps(queueFamCount, {
      .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 });
   vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamCount, queueFamProps.data());
   VkQueueFlags desiredQueueFlags = VK_QUEUE_GRAPHICS_BIT;
   if (SimMode == SimulationMode::GPU)
      desiredQueueFlags |= VK_QUEUE_COMPUTE_BIT;
   for (int currentFamId = 0; currentFamId < queueFamProps.size(); ++currentFamId) {
      const auto& properties = queueFamProps[currentFamId].queueFamilyProperties;
      if (properties.queueFlags & desiredQueueFlags && queueFamilySupportsPresent(currentFamId)) {
         gfxQueueFamIdx = currentFamId;
         return true;
      }
   }
   return false;
}

// Check if queue family supports presentation to the surface stored in Application
bool Application::queueFamilySupportsPresent(int familyId)
{
   VkBool32 supportsPresent = VK_FALSE;
   VkResult result;
   result = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, familyId, surface, &supportsPresent);
   if (result != VK_SUCCESS) {
      showError("vkGetPhysicalDeviceSurfaceSupportKHR() couldn't determine presentation support");
      return false;
   };
   return supportsPresent;
}

bool Application::createDevice()
{
   const vector<float> queuePriorities{ 1.0f };
   VkDeviceQueueCreateInfo gfxQueueInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = gfxQueueFamIdx,
      .queueCount = 1,
      .pQueuePriorities = queuePriorities.data()
   };
   FeaturesList desiredFeatures;
   setDesiredFeaturesList(desiredFeatures);
   const vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
   VkDeviceCreateInfo devCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &desiredFeatures.features,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &gfxQueueInfo,
      .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
      .ppEnabledExtensionNames = deviceExtensions.data()
   };
   if (vkCreateDevice(physicalDevice, &devCreateInfo, nullptr, &device) != VK_SUCCESS) {
      showError("vkCreateDevice() failed to return a valid VkDevice handle");
      return false;
   }
   return true;
}

// Attempt to get a graphics queue handle from the selected logical device
bool Application::setGfxQueue()
{
   vkGetDeviceQueue(device, gfxQueueFamIdx, 0, &gfxQueue);
   if (!gfxQueue) {
      showError("Couldn't get the graphics queue");
      return false;
   }
   return true;
}

bool Application::initializeVMA()
{
   VmaVulkanFunctions vmaFunctionInfo{};
   VmaAllocatorCreateInfo vmaAllocatorInfo{
      .physicalDevice = physicalDevice,
      .device = device,
      .pVulkanFunctions = &vmaFunctionInfo,
      .instance = vulkanInstance,
      .vulkanApiVersion = VK_API_VERSION_1_3    // As target device runs on 1.3.280
   };
   if (vmaImportVulkanFunctionsFromVolk(&vmaAllocatorInfo, &vmaFunctionInfo) != VK_SUCCESS) {
      showError("vmaImportVulkanFunctionsFromVolk() failed to import functions from volk");
      return false;
   }
   if (vmaCreateAllocator(&vmaAllocatorInfo, &vmaAllocator) != VK_SUCCESS) {
      showError("vmaImportVulkanFunctionsFromVolk() failed to create VmaAllocator");
      return false;
   }
   return true;
}

bool Application::createSwapchain()
{
   swapchainWidth = windowWidth;
   swapchainHeight = windowHeight;
   VkSurfaceCapabilitiesKHR surfaceCapabilities{};
   VkResult result;
   result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities);
   if (result != VK_SUCCESS) {
      showError("vkGetPhysicalDeviceSurfaceCapabilitiesKHR couldn't get the surface capabilities");
      return false;
   }
   auto swapchainCreateInfo = makeSwapchainCreateInfo(surfaceCapabilities.minImageCount);
   result = vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain);
   if (result != VK_SUCCESS) {
      showError("vkCreateSwapchainKHR() failed to create a swapchain");
      return false;
   }
   return true;
}

// Try to retrieve and store images in swapchainImages data member
bool Application::retrieveSwapchainImages()
{
   uint32_t imageCount = 0;
   if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr) != VK_SUCCESS) {
      showError("vkGetSwapchainImagesKHR() failed to retrieve the # of presentable images");
      return false;
   }
   swapchainImages.resize(imageCount);
   if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()) != VK_SUCCESS) {
      showError("vkGetSwapchainImagesKHR() failed to retrieve swapchain images");
      return false;
   }
   swapchainImageViews.resize(imageCount);
   return true;
}

// Attempt to create a swapchain image view for each swapchain image
bool Application::createSwapchainImageViews()
{
   for (size_t i = 0; i < swapchainImages.size(); ++i) {
      VkImageViewCreateInfo imgViewInfo = makeImageViewCreateInfo(i);
      if (vkCreateImageView(device, &imgViewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
         showError("vkCreateImageView() failed to create an image view for a swapchain image");
         return false;
      }
   }
   return true;
}

// Fill renderCompleteSemaphores data member with semaphores meant to signal render completion
bool Application::createRenderCompleteSemaphores()
{
   renderCompleteSemaphores.resize(MaxFramesInFlight);
   for (VkSemaphore& semaphore : renderCompleteSemaphores) {
      VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
      if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
         showError("vkCreateSemaphore() failed to create a semaphore signaling render completion");
         return false;
      }
   }
   return true;
}

bool Application::createShaders()
{
   if (SimMode == SimulationMode::GPU) {
      if (gridCompShader = createShaderModule("grid_comp.spv"); !gridCompShader) {
         showError("Failed to create grid compute shader module");
         return false;
      }
      if (collisionCompShader = createShaderModule("collision_comp.spv"); !collisionCompShader) {
         showError("Failed to create collision compute shader module");
         return false;
      }
   }
   if (vertShader = createShaderModule("circle_vert.spv"); !vertShader) {
      showError("Failed to create vertex shader module");
      return false;
   }
   if (fragShader = createShaderModule("circle_frag.spv"); !fragShader) {
      showError("Failed to create fragment shader module");
      return false;
   }
   return true;
}

// set the Application::pipelineLayout data member
bool Application::createPipelineLayout()
{
   uint32_t descriptorSetLayoutCount = 0, constRangeCount = 0;
   VkPushConstantRange constRange;
   if (SimMode == SimulationMode::GPU) {
      if (!setDescriptorSetLayout())
         return false;
      descriptorSetLayoutCount = 1, constRangeCount = 1;
      constRange = {
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
         .size = sizeof(uint32_t)
      };
   }
   VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = descriptorSetLayoutCount,
      .pSetLayouts = &descriptorSetLayout,
      .pushConstantRangeCount = constRangeCount,
      .pPushConstantRanges = SimMode == SimulationMode::GPU ? &constRange : nullptr
   };
   if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout)) {
      showError("vkCreatePipelineLayout() failed to create pipeline layout");
      return false;
   }
   return true;
}

bool Application::setDescriptorSetLayout()
{
   array<VkDescriptorSetLayoutBinding, DescriptorSetLayoutBindingCount> descrSetLayoutBindings {
      {
         {
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT
         },
         {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
         }
      }
   };
   VkDescriptorSetLayoutCreateInfo layoutInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = descrSetLayoutBindings.size(),
      .pBindings = descrSetLayoutBindings.data()
   };
   if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
      showError("vkCreateDescriptorSetLayout() failed to create a descriptor set layout");
      return false;
   }
   return true;
}

bool Application::createComputePipelines()
{
   array<VkSpecializationMapEntry, SpecialConstantCount> specialMapEntries{};
   ComputeConstantData computeConstants{};
   VkSpecializationInfo specialInfo = makeSpecializationInfo(specialMapEntries, computeConstants);
   array<VkComputePipelineCreateInfo, ComputePipelineCount> compPipelineInfo;
   constexpr uint32_t shaderStageInfoCount = 2;
   VkPipelineShaderStageCreateInfo shaderStageInfos[shaderStageInfoCount];
   compPipelineInfo[0] = makeComputePipelineInfo(gridCompShader, specialInfo, shaderStageInfos[0]);
   compPipelineInfo[1] = makeComputePipelineInfo(collisionCompShader, specialInfo, shaderStageInfos[1]);
   if (vkCreateComputePipelines(
       device, nullptr, compPipelineInfo.size(), compPipelineInfo.data(), nullptr, computePipelines.data())) {
      showError("vkCreateComputePipelines() could not create a compute pipeline");
      return false;
   }
   return true;
}

VkComputePipelineCreateInfo Application::makeComputePipelineInfo(
   VkShaderModule& shader, VkSpecializationInfo& specInfo, VkPipelineShaderStageCreateInfo& stageInfo)
{
   stageInfo = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .module = shader,
         .pName = "main",
         .pSpecializationInfo = &specInfo
   };
   return{
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = stageInfo,
      .layout = pipelineLayout
   };
}

bool Application::createGraphicsPipeline()
{
   GraphicsPipelineInfoData gfxPipelineData;
   gfxPipelineData.shaderStageInfo = makeShaderStageCreateInfo();
   VkVertexInputBindingDescription bindDescription{};
   VkVertexInputAttributeDescription attribDescription[ParticleInfo::inputMemberCount];
   gfxPipelineData.vertexInputInfo = makeVertexInputStateCreateInfo(bindDescription, attribDescription);
   gfxPipelineData.inputAssemblyInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
   };
   gfxPipelineData.viewportInfo = makePipelineViewportInfo();
   gfxPipelineData.multiSampleInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
   };
   VkPipelineColorBlendAttachmentState colorBlendAttachState{};
   gfxPipelineData.colorBlendInfo = makeColorBlendCreateInfo(colorBlendAttachState);
   array<VkDynamicState, DynamicStateCount> dynamicStates;
   gfxPipelineData.dynamicStateInfo = makeDynamicStateInfo(dynamicStates);
   gfxPipelineData.rasterizationInfo = makePipelineRasterizationInfo();
   gfxPipelineData.renderingInfo = makePipelineRenderingCreateInfo();
   VkGraphicsPipelineCreateInfo pipelineInfo = makeGraphicsPipelineInfo(gfxPipelineData);
   if (vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineInfo, nullptr, &gfxPipeline) != VK_SUCCESS) {
      showError("vkCreateGraphicsPipelines() failed to create a graphics pipeline");
      return false;
   }
   return true;
}

// 1 timeline semaphore to sync render work between CPU and GPU
bool Application::createTimelineSemaphores()
{
   VkSemaphoreTypeCreateInfo semaphoreTypeInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = MaxFramesInFlight
   };
   VkSemaphoreCreateInfo semaphoreInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &semaphoreTypeInfo
   };
   if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &timelineSemaphore) != VK_SUCCESS) {
      showError("vkCreateSemaphore() failed to create a timeline semaphore for rendering");
      return false;
   }
   return true;
}

bool Application::createImageAcquiredSemaphores()
{
   for (FrameResources& frameRes : frameResources) {
      VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
      VkResult result;
      result = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frameRes.imageAcquiredSemaphore);
      if (result != VK_SUCCESS) {
         showError("vkCreateSemaphore() failed to create an image acquisition signaling semaphore");
         return false;
      }
   }
   return true;
}

bool Application::createCommandBuffers()
{
   for (FrameResources& frameRes : frameResources) {
      VkCommandPoolCreateInfo poolInfo = makeCmdPoolCreateInfo(VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
      if (vkCreateCommandPool(device, &poolInfo, nullptr, &frameRes.commandPool) != VK_SUCCESS) {
         showError("vkCreateCommandPool() failed to create a command pool object");
         return false;
      }
      VkCommandBufferAllocateInfo cmdBufAllocInfo = makeCmdBufferAllocateInfo(frameRes.commandPool);
      if (vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &frameRes.gfxCommandBuffer) != VK_SUCCESS) {
         showError("vkAllocateCommandBuffers() failed to allocate a graphics command buffer");
         return false;
      }
   }
   if (SimMode == SimulationMode::GPU) {
      auto poolInfo = makeCmdPoolCreateInfo(VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
      if (vkCreateCommandPool(device, &poolInfo, nullptr, &updateCommandPool) != VK_SUCCESS) {
         showError("vkCreateCommandPool() failed to create update command pool object");
         return false;
      }
      VkCommandBufferAllocateInfo cmdBufAllocInfo = makeCmdBufferAllocateInfo(updateCommandPool);
      if (vkAllocateCommandBuffers(device, &cmdBufAllocInfo, &updateCommandBuffer) != VK_SUCCESS) {
         showError("vkAllocateCommandBuffers() failed to allocate an update command buffer");
         return false;
      }
   }
   return true;
}

// Allocate memory for and create a buffer to be used for particle data
bool Application::createParticleBuffer()
{
   VkBufferUsageFlags bufferFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
   if (SimMode == SimulationMode::GPU)
      bufferFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
   VkBufferCreateInfo bufferInfo{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = sizeof(Particle) * sim::ParticleCount,
      .usage = bufferFlags
   };
   VmaAllocationCreateFlags allocFlags = 0;
   if (SimMode == SimulationMode::CPU)
      allocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
   VmaAllocationCreateInfo bufferAllocCreateInfo{
      .flags = allocFlags,
      .usage = VMA_MEMORY_USAGE_AUTO
   };
   VmaAllocationInfo bufferAllocInfo;
   if (vmaCreateBuffer(vmaAllocator, &bufferInfo, &bufferAllocCreateInfo,
      &particleBuffer, &particleBufferAllocation, &bufferAllocInfo) != VK_SUCCESS) {
      showError("vmaCreateBuffer() failed to create a buffer for particles");
      return false;
   }
   if (SimMode == SimulationMode::CPU) {
      mappedParticleData = static_cast<Particle*>(bufferAllocInfo.pMappedData);
      memcpy(mappedParticleData, particles.data(), sizeof(Particle) * sim::ParticleCount);
   }
   return true;
}

bool Application::createUpdateFence()
{
   VkFenceCreateInfo fenceInfo{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT
   };
   if (vkCreateFence(device, &fenceInfo, nullptr, &updateFence) != VK_SUCCESS) {
      showError("vkCreateFence() failed to create update fence");
      return false;
   }
   return true;
}

bool Application::createQueryPool()
{
   VkQueryPoolCreateInfo queryPoolInfo{
      .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_TIMESTAMP,
      .queryCount = QueryAmount
   };
   if (vkCreateQueryPool(device, &queryPoolInfo, nullptr, &queryPool) != VK_SUCCESS) {
      showError("vkCreateQueryPool() failed to create a query pool");
      return false;
   }
   return true;
}

bool Application::createStagingBuffer()
{
   VkBufferCreateInfo bufferInfo{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = sizeof(Particle) * sim::ParticleCount,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
   };
   VmaAllocationCreateInfo bufferAllocCreateInfo{
      .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO
   };
   VmaAllocationInfo bufferAllocInfo;
   if (vmaCreateBuffer(vmaAllocator, &bufferInfo, &bufferAllocCreateInfo,
      &stagingBuffer, &stagingBufferAllocation, &bufferAllocInfo) != VK_SUCCESS) {
      showError("vmaCreateBuffer() failed to create a staging buffer");
      return false;
   }
   mappedParticleData = static_cast<Particle*>(bufferAllocInfo.pMappedData);
   return true;
}

bool Application::createGridBuffer()
{
   VkBufferCreateInfo bufferInfo{
   .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
   .size = sizeof(Cell) * sim::CellCount,
   .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
   };
   VmaAllocationCreateInfo bufferAllocCreateInfo{
      .usage = VMA_MEMORY_USAGE_AUTO
   };
   VmaAllocationInfo bufferAllocInfo;
   if (vmaCreateBuffer(vmaAllocator, &bufferInfo, &bufferAllocCreateInfo,
      &gridBuffer, &gridBufferAllocation, &bufferAllocInfo) != VK_SUCCESS) {
      showError("vmaCreateBuffer() failed to create a grid buffer");
      return false;
   }
   return true;
}

bool Application::createDescriptorPool()
{
   constexpr uint32_t descriptorAmount = 2;
   VkDescriptorPoolSize poolSize{
      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = descriptorAmount
   };
   VkDescriptorPoolCreateInfo poolCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &poolSize
   };
   if (vkCreateDescriptorPool(device, &poolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
      showError("vkCreateDescriptorPool() failed to create a descriptor pool");
      return false;
   }
   return true;
}

bool Application::allocateDescriptorSet()
{
   VkDescriptorSetAllocateInfo descrSetAllocInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptorPool,
      .descriptorSetCount = 1,
      .pSetLayouts = &descriptorSetLayout
   };
   if (vkAllocateDescriptorSets(device, &descrSetAllocInfo, &descriptorSet) != VK_SUCCESS) {
      showError("vkAllocateDescriptorSets() failed to allocate a descriptor set");
      return false;
   }
   return true;
}

void Application::updateDescriptorSet()
{
   VkDescriptorBufferInfo particleBufferInfo{
      .buffer = particleBuffer,
      .range = sizeof(Particle) * particles.size()
   };
   VkDescriptorBufferInfo gridBufferInfo{
      .buffer = gridBuffer,
      .range = sizeof(Cell) * grid.size()
   };
   array<VkWriteDescriptorSet, DescriptorSetLayoutBindingCount> writeDescrSets{
      {
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &particleBufferInfo
         }, {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &gridBufferInfo
         }
      }
   };
   vkUpdateDescriptorSets(device, writeDescrSets.size(), writeDescrSets.data(), 0, nullptr);
}

void Application::update(Benchmarker& benchmarker)
{
   const uint32_t oldSpawned = particleSpawner.getSpawnedParticles();
   particleSpawner.spawnWave(particles);
   const uint32_t spawnedParticles = particleSpawner.getSpawnedParticles();
   if (SimMode == SimulationMode::CPU) {
      benchmarker.startRecording();
      for (uint32_t i = 0; i < spawnedParticles; ++i) {
         cpu::physics::applyGravity(particles[i]);
         cpu::physics::mapParticle(grid, particles[i], i, timer.getTimestamp());
      }
      cpu::physics::collisionDetection(grid, particles, spawnedParticles, timer.getTimestamp());
      for (uint32_t i = 0; i < spawnedParticles; ++i)
         cpu::physics::resolveBoundaryCollisions(particles[i]);
      benchmarker.stopRecording();
      memcpy(mappedParticleData, particles.data(), sizeof(Particle) * sim::ParticleCount);
   }
   if (SimMode == SimulationMode::GPU) {
      if (!prepareUpdateFence())
         return;
      if (benchmarker.warmupComplete() && !processQueryResults(benchmarker))
         return;
      const uint32_t newlySpawned = spawnedParticles - oldSpawned;
      memcpy(mappedParticleData + oldSpawned, particles.data() + oldSpawned, sizeof(Particle) * newlySpawned);
      if (!recordUpdateCommandBuffer(oldSpawned, newlySpawned, spawnedParticles))
         return;
      if (!submitUpdateCommandBuffer())
         return;
   }
   timer.incrementTimestamp();
}

bool Application::prepareUpdateFence()
{
   VkResult waitResult = vkWaitForFences(device, 1, &updateFence, VK_TRUE, Timeout);
   switch (waitResult) {
   case VK_SUCCESS:
      break;
   case VK_TIMEOUT:
      showError("vkWaitForFences() timed out");
      return running = false;
   default:
      showError("vkWaitForFences() failed to make host wait for device to finish update work");
      return running = false;
   }
   if (vkResetFences(device, 1, &updateFence) != VK_SUCCESS) {
      showError("vkResetFences() failed to reset update fence");
      return running = false;
   }
   return true;
}

bool Application::processQueryResults(Benchmarker& benchmarker)
{
   uint64_t gpuTimestamps[QueryAmount];
   VkResult result;
   result = vkGetQueryPoolResults(
      device,
      queryPool,
      0,
      QueryAmount,
      QueryAmount * sizeof(uint64_t),
      gpuTimestamps,
      sizeof(uint64_t),
      VK_QUERY_RESULT_64_BIT);
   if (result != VK_SUCCESS && result != VK_NOT_READY) {
      showError("vkGetQueryPoolResults() failed to extract GPU compute work timestamps");
      return false;
   }
   double rawExecTime = (gpuTimestamps[1] - gpuTimestamps[0]) * timestampPeriod;
   constexpr double msConversionFactor = 1e-6;
   benchmarker.addExecutionTime(rawExecTime * msConversionFactor);
   return true;
}

bool Application::recordUpdateCommandBuffer(uint32_t offset, uint32_t newCount, uint32_t totalCount)
{
   if (!beginUpdateCommandBuffer())
      return false;
   vkCmdResetQueryPool(updateCommandBuffer, queryPool, 0, QueryAmount);
   copyStagingBuffer(offset, newCount);
   vkCmdFillBuffer(updateCommandBuffer, gridBuffer, 0, VK_WHOLE_SIZE, 0);
   array<VkBufferMemoryBarrier2, TransferBarrierCount> bufferBarriers;
   bufferBarriers.at(0) = makeTransferBufferBarrier(particleBuffer, VK_ACCESS_2_SHADER_READ_BIT);
   bufferBarriers.at(1) = makeTransferBufferBarrier(gridBuffer, VK_ACCESS_2_SHADER_READ_BIT);
   VkDependencyInfo dependencyInfo = makeBufferBarrierDependInfo(bufferBarriers);
   vkCmdPipelineBarrier2(updateCommandBuffer, &dependencyInfo);
   vkCmdBindPipeline(updateCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelines[0]);
   const uint32_t workGroupCount = (totalCount + sim::WorkGroupSize - 1) / sim::WorkGroupSize;
   vkCmdPushConstants(
      updateCommandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &totalCount);
   vkCmdBindDescriptorSets(
      updateCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
   vkCmdWriteTimestamp2(updateCommandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, queryPool, 0);
   vkCmdDispatch(updateCommandBuffer, workGroupCount, 1, 1);
   bufferBarriers.at(0) = makeComputeBufferBarrier(particleBuffer, VK_ACCESS_2_SHADER_WRITE_BIT);
   bufferBarriers.at(1) = makeComputeBufferBarrier(gridBuffer, VK_PIPELINE_STAGE_2_NONE);
   dependencyInfo = makeBufferBarrierDependInfo(bufferBarriers);
   vkCmdPipelineBarrier2(updateCommandBuffer, &dependencyInfo);
   vkCmdBindPipeline(updateCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelines[1]);
   vkCmdDispatch(updateCommandBuffer, workGroupCount, 1, 1);
   vkCmdWriteTimestamp2(updateCommandBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, queryPool, 1);
   if (vkEndCommandBuffer(updateCommandBuffer) != VK_SUCCESS) {
      showError("vkEndCommandBuffer() failed to end an update command buffer");
      return running = false;
   }
   return true;
}

bool Application::beginUpdateCommandBuffer()
{
   VkCommandBufferBeginInfo cmdBeginInfo{
   .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
   };
   if (vkBeginCommandBuffer(updateCommandBuffer, &cmdBeginInfo) != VK_SUCCESS) {
      showError("vkBeginCommandBuffer() failed to begin an update command buffer");
      return running = false;
   }
   return true;
}

void Application::copyStagingBuffer(uint32_t particleOffset, uint32_t newlySpawnedParticleCount)
{
   if (newlySpawnedParticleCount > 0) {
      VkBufferCopy bufferCopyRegion{
         .srcOffset = sizeof(Particle) * particleOffset,
         .dstOffset = sizeof(Particle) * particleOffset,
         .size = sizeof(Particle) * newlySpawnedParticleCount
      };
      vkCmdCopyBuffer(updateCommandBuffer, stagingBuffer, particleBuffer, 1, &bufferCopyRegion);
   }
}

bool Application::submitUpdateCommandBuffer()
{
   VkCommandBufferSubmitInfo cmdBuffSubmitInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = updateCommandBuffer,
   };
   VkSubmitInfo2 submitInfo{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cmdBuffSubmitInfo
   };
   if (vkQueueSubmit2(gfxQueue, 1, &submitInfo, updateFence) != VK_SUCCESS) {
      showError("vkQueueSubmit2() failed to submit a update work to graphics queue");
      return running = false;
   }
   return true;
}

void Application::render()
{
   if (!handleSwapchainRecreation())
      return;
   const uint32_t frameResIndex = frameIndex++ % MaxFramesInFlight;
   const uint64_t signalValue = nextSignalValue++;
   const uint64_t waitValue = signalValue - MaxFramesInFlight;
   if (!waitOnTimelineSemaphore(waitValue))
      return;
   FrameResources& frameRes = frameResources[frameResIndex];
   if (vkResetCommandPool(device, frameRes.commandPool, 0) != VK_SUCCESS) {
      showError("vkResetCommandPool() failed to reset a frame-allocated command pool");
      running = false;
      return;
   }
   uint32_t imageIndex = 0;
   VkResult result = vkAcquireNextImageKHR(device, swapchain, Timeout,
      frameRes.imageAcquiredSemaphore, VK_NULL_HANDLE, &imageIndex);
   if (!handleSwapchainResult(result, "vkAcquireNextImageKHR()", "failed to acquire swapchain image"))
      return;
   if (!recordGfxCommandBuffer(frameRes.gfxCommandBuffer, imageIndex))
      return;
   if (!submitGfxCommandBuffer(imageIndex, signalValue, frameRes))
      return;
   presentFrame(imageIndex);
}

// Recreate the swapchain if needed
bool Application::handleSwapchainRecreation()
{
   if (!needSwapchainRecreation)
      return true;
   if (!recreateSwapchain()) {
      running = false;
      return false;
   }
   needSwapchainRecreation = false;
   return true;
}

// Call all member functions needed to recreate the swapchain.
// Using an array of member function pointers to reduce repetitiveness
bool Application::recreateSwapchain()
{
   if (vkDeviceWaitIdle(device) != VK_SUCCESS) {
      showError("vkDeviceWaitIdle() failed to make host wait for device");
      return false;
   }
   destroySwapchain();
   constexpr uint32_t SwapchainFunctionCount{ 4 };
   array<bool (Application::*)(), SwapchainFunctionCount> swapchainRecreateFunctions{
      &Application::createSwapchain, &Application::retrieveSwapchainImages,
      &Application::createSwapchainImageViews, &Application::createRenderCompleteSemaphores
   };
   for (auto recreateFunction : swapchainRecreateFunctions)
      if (!(this->*recreateFunction)())
         return false;
   return true;
}

bool Application::waitOnTimelineSemaphore(uint64_t waitValue)
{
   VkSemaphoreWaitInfo waitInfo{
   .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
   .semaphoreCount = 1,
   .pSemaphores = &timelineSemaphore,
   .pValues = &waitValue
   };
   VkResult waitResult = vkWaitSemaphores(device, &waitInfo, Timeout);
   if (waitResult == VK_TIMEOUT) {
      showError("vkWaitSemaphores() timed out");
      running = false;
   }
   else if (waitResult != VK_SUCCESS) {
      showError("vkWaitSemaphores() failed to make the CPU wait for the device properly");
      running = false;
   }
   return running;
}

void Application::destroySwapchain()
{
   for (VkImageView& imageView : swapchainImageViews)
      vkDestroyImageView(device, imageView, nullptr);
   swapchainImageViews.clear();
   for (VkSemaphore& semaphore : renderCompleteSemaphores)
      vkDestroySemaphore(device, semaphore, nullptr);
   renderCompleteSemaphores.clear();
   if (swapchain)
      vkDestroySwapchainKHR(device, swapchain, nullptr);
}

bool Application::handleSwapchainResult(VkResult result, const string& funcName, const string& errorMsg)
{
   switch (result) {
   case VK_SUBOPTIMAL_KHR:
      needSwapchainRecreation = true;
      [[fallthrough]];
   case VK_SUCCESS:
      return true;
   case VK_ERROR_OUT_OF_DATE_KHR:
      needSwapchainRecreation = true;
      return false;
   case VK_TIMEOUT:
      showError(std::format("{} timed out", funcName));
      running = false;
      return false;
   default:
      showError(std::format("{} {}", funcName, errorMsg));
      running = false;
      return false;
   }
}

bool Application::recordGfxCommandBuffer(VkCommandBuffer& cmdBuffer, uint32_t imageIndex)
{
   VkCommandBufferBeginInfo cmdBeginInfo{
   .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
   };
   if (vkBeginCommandBuffer(cmdBuffer, &cmdBeginInfo) != VK_SUCCESS) {
      showError("vkBeginCommandBuffer() could not begin recording a command buffer");
      running = false;
      return false;
   }
   VkBufferMemoryBarrier2 transitionBarrier = makeTransitionBufferBarrier(particleBuffer);
   VkImageMemoryBarrier2 colorAttacmentBarier = makeColorAttachmentBarrier(imageIndex);
   VkDependencyInfo colorAttachDependInfo = makeBarrierDependInfo(colorAttacmentBarier, &transitionBarrier);
   vkCmdPipelineBarrier2(cmdBuffer, &colorAttachDependInfo);
   recordRenderingCommands(cmdBuffer, imageIndex);
   VkImageMemoryBarrier2 presentLayoutBarrier = makePresentLayoutBarrier(imageIndex);
   VkDependencyInfo presentDependInfo = makeBarrierDependInfo(presentLayoutBarrier, nullptr);
   vkCmdPipelineBarrier2(cmdBuffer, &presentDependInfo);
   if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS) {
      showError("vkEndCommandBuffer() failed to end recording a command buffer");
      running = false;
   }
   return running;
}

void Application::recordRenderingCommands(VkCommandBuffer& cmdBuffer, uint32_t imageIndex)
{
   VkRenderingAttachmentInfo colorAttachInfo = makeRenderingAttachmentInfo(imageIndex);
   VkRenderingInfo renderingInfo = makeRenderingInfo(colorAttachInfo);
   vkCmdBeginRendering(cmdBuffer, &renderingInfo);
   VkViewport viewport = setViewport(cmdBuffer);
   setSimulationBackground(cmdBuffer, viewport);
   VkRect2D scissor{
      .offset{},
      .extent{
         .width = swapchainWidth,
         .height = swapchainHeight
      }
   };
   vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
   vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gfxPipeline);
   VkDeviceSize bufferOffset = 0;
   vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &particleBuffer, &bufferOffset);
   vkCmdDraw(cmdBuffer, 4, sim::ParticleCount, 0, 0);
   vkCmdEndRendering(cmdBuffer);
}

// Make sure simulationArea keeps 1:1 aspect ratio and is centered in SDL window
VkViewport Application::setViewport(VkCommandBuffer& cmdBuffer)
{
   VkViewport viewport{};
   if (swapchainWidth > swapchainHeight) {
      viewport.x = static_cast<float>(swapchainWidth - swapchainHeight) / 2.0f;
      viewport.width = static_cast<float>(swapchainHeight);
      viewport.height = static_cast<float>(swapchainHeight);
   }
   else {
      viewport.y = static_cast<float>(swapchainHeight - swapchainWidth) / 2.0f;
      viewport.width = static_cast<float>(swapchainWidth);
      viewport.height = static_cast<float>(swapchainWidth);
   }
   vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
   return viewport;
}

void Application::setSimulationBackground(VkCommandBuffer& cmdBuffer, const VkViewport& viewport)
{
   const VkClearAttachment simBGClearAttachment{
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .clearValue {
         .color{0.02f, 0.04f, 0.04f, 1.0}
      }
   };
   const VkClearRect simBGClearRect{
      .rect{
         .offset{
            .x = static_cast<int32_t>(viewport.x),
            .y = static_cast<int32_t>(viewport.y)
         },
         .extent{
            .width = static_cast<uint32_t>(viewport.width),
            .height = static_cast<uint32_t>(viewport.height)
         }
      },
      .layerCount = 1
   };
   vkCmdClearAttachments(cmdBuffer, 1, &simBGClearAttachment, 1, &simBGClearRect);
}

bool Application::submitGfxCommandBuffer(uint32_t imgId, uint64_t signalVal, const FrameResources& frameRes)
{
   array<VkSemaphoreSubmitInfo, 3> submitInfoSemaphores{
      makeSemaphoreSubmitInfo(
         frameRes.imageAcquiredSemaphore, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT),
      makeSemaphoreSubmitInfo(renderCompleteSemaphores[imgId], 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT),
      makeSemaphoreSubmitInfo(timelineSemaphore, signalVal, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
   };
   VkCommandBufferSubmitInfo cmdBufSubmitInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = frameRes.gfxCommandBuffer
   };
   VkSubmitInfo2 submitInfo = makeSubmitInfo(submitInfoSemaphores, cmdBufSubmitInfo);
   if (vkQueueSubmit2(gfxQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
      showError("vkQueueSubmit2() failed to submit a graphics command buffer to a queue");
      running = false;
   }
   return running;
}

void Application::presentFrame(uint32_t imageIndex)
{
   VkPresentInfoKHR presentInfo{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &renderCompleteSemaphores[imageIndex],
      .swapchainCount = 1,
      .pSwapchains = &swapchain,
      .pImageIndices = &imageIndex
   };
   VkResult presentResult = vkQueuePresentKHR(gfxQueue, &presentInfo);
   handleSwapchainResult(presentResult, "vkQueuePresentKHR()", "failed to queue image for presentation");
}

VkShaderModule Application::createShaderModule(const string& fileName)
{
   const string spvPath = "shaders_spv/" + fileName;
   const vector<uint32_t> spvCode = readSpvFile(spvPath);
   if (spvCode.empty()) {
      showError("Could not read compiled version of shader at: " + spvPath + '\n');
      return nullptr;
   }
   VkShaderModuleCreateInfo moduleCreateInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spvCode.size() * sizeof(uint32_t),
      .pCode = spvCode.data()
   };
   VkShaderModule shaderModule = nullptr;
   if (vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
      showError("vkCreateShaderModule() failed to create a shader module based on the file " + fileName);
      return nullptr;
   }
   return shaderModule;
}

// Callback function for VkDebugUtilsMessengerCreateInfoEXT
// Documentation says applications should always return VK_FALSE
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
   VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
   VkDebugUtilsMessageTypeFlagsEXT messageType,
   const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
   void* pUserData)
{
   if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
      std::cerr << "Validation Layer: " << pCallbackData->pMessage << '\n';
   }
   return VK_FALSE;
}

// Helper with hard-coded message severity and type values for readability
VkDebugUtilsMessengerCreateInfoEXT makeDebugUtilsMessengerInfo()
{
   return {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debugCallback
   };
}

// Returns a VkInstanceCreateInfo struct after setting its fields to the provided parameters
VkInstanceCreateInfo makeVkInstanceInfo(
   const VkDebugUtilsMessengerCreateInfoEXT& debugInfo,
   const VkApplicationInfo& appInfo,
   const vector<const char*>& layers,
   const vector<const char*>& extensions)
{
   const VkDebugUtilsMessengerCreateInfoEXT* utilsInfo = &debugInfo;
   #ifdef NDEBUG
      utilsInfo = nullptr;
   #endif
   return {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = utilsInfo,
      .pApplicationInfo = &appInfo,
      .enabledLayerCount = static_cast<uint32_t>(layers.size()),
      .ppEnabledLayerNames = layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .ppEnabledExtensionNames = extensions.data()
   };
}

VkSwapchainCreateInfoKHR Application::makeSwapchainCreateInfo(uint32_t minImageCount)
{
   return {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface,
      .minImageCount = minImageCount,
      .imageFormat = surfaceFormat.format,
      .imageColorSpace = surfaceFormat.colorSpace,
      .imageExtent{.width = swapchainWidth, .height = swapchainHeight},
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR
   };
}

VkImageViewCreateInfo Application::makeImageViewCreateInfo(size_t index)
{
   return {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = swapchainImages[index],
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = surfaceFormat.format,
      .subresourceRange{
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .levelCount = 1,
         .layerCount = 1
      }
   };
}

array<VkPipelineShaderStageCreateInfo, Application::ShaderCount> Application::makeShaderStageCreateInfo()
{
   return { {
   {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vertShader,
      .pName = "main"
   }, {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = fragShader,
      .pName = "main"
   }
   } };
}

VkPipelineRenderingCreateInfo Application::makePipelineRenderingCreateInfo()
{
   return {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &surfaceFormat.format
   };
}

VkGraphicsPipelineCreateInfo Application::makeGraphicsPipelineInfo(
   GraphicsPipelineInfoData& gfxPipelineData)
{
   return {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &gfxPipelineData.renderingInfo,
      .stageCount = static_cast<uint32_t>(gfxPipelineData.shaderStageInfo.size()),
      .pStages = gfxPipelineData.shaderStageInfo.data(),
      .pVertexInputState = &gfxPipelineData.vertexInputInfo,
      .pInputAssemblyState = &gfxPipelineData.inputAssemblyInfo,
      .pViewportState = &gfxPipelineData.viewportInfo,
      .pRasterizationState = &gfxPipelineData.rasterizationInfo,
      .pMultisampleState = &gfxPipelineData.multiSampleInfo,
      .pColorBlendState = &gfxPipelineData.colorBlendInfo,
      .pDynamicState = &gfxPipelineData.dynamicStateInfo,
      .layout = pipelineLayout
   };
}

VkCommandPoolCreateInfo Application::makeCmdPoolCreateInfo(VkCommandPoolCreateFlags flags)
{
   return {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = flags,
      .queueFamilyIndex = gfxQueueFamIdx
   };
}

VkImageMemoryBarrier2 Application::makeColorAttachmentBarrier(uint32_t imageIndex)
{
   return {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
      .srcAccessMask = 0,
      .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .image = swapchainImages[imageIndex],
      .subresourceRange{
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .levelCount = 1,
         .layerCount = 1
      }
   };
}

// Makes the rendering attacmhent info struct required for the rendering info struct
// The background color (clearValue) is also set here
VkRenderingAttachmentInfo Application::makeRenderingAttachmentInfo(uint32_t imageIndex)
{
   return {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = swapchainImageViews[imageIndex],
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue{.color{0.0f}}
   };
}

VkRenderingInfo Application::makeRenderingInfo(const VkRenderingAttachmentInfo& colorAttachInfo)
{
   return {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea{
         .extent{
            .width = swapchainWidth,
            .height = swapchainHeight
         }
      },
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachInfo
   };
}

VkImageMemoryBarrier2 Application::makePresentLayoutBarrier(uint32_t imageIndex)
{
   return {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
      .dstAccessMask = 0,
      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .image = swapchainImages[imageIndex],
      .subresourceRange {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .levelCount = 1,
         .layerCount = 1
      }
   };
}

VkSemaphoreSubmitInfo Application::makeImageAcquireSemaphoreSubmitInfo(const VkSemaphore& semaphore)
{
   return {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = semaphore,
      .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
   };
}

VkSemaphoreSubmitInfo Application::makeSemaphoreSubmitInfo(
   const VkSemaphore& semaphore, uint32_t signalValue, VkPipelineStageFlagBits2 stageMask)
{
   return {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = semaphore,
      .value = signalValue,
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
   };
}

VkSubmitInfo2 Application::makeSubmitInfo(
   const array<VkSemaphoreSubmitInfo, 3>& semaphoreSubmitInfos,
   const VkCommandBufferSubmitInfo& cmdBufSubmitInfo)
{
   constexpr uint32_t waitCount = 1, signalCount = 2;
   return {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount = waitCount,
      .pWaitSemaphoreInfos = semaphoreSubmitInfos.data(),
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cmdBufSubmitInfo,
      .signalSemaphoreInfoCount = signalCount,
      .pSignalSemaphoreInfos = semaphoreSubmitInfos.data() + waitCount
   };
}

VkSpecializationInfo makeSpecializationInfo(
   array<VkSpecializationMapEntry, Application::SpecialConstantCount>& mapEntries,
   ComputeConstantData& computeConstants)
{
   mapEntries = { {
      {.constantID = 0, .size = sizeof(uint32_t)},
      {.constantID = 1, .offset = offsetof(ComputeConstantData, gridDimension), .size = sizeof(uint32_t)},
      {.constantID = 2, .offset = offsetof(ComputeConstantData, gravityY), .size = sizeof(float)},
      {.constantID = 3, .offset = offsetof(ComputeConstantData, timeStep), .size = sizeof(float)},
      {.constantID = 4, .offset = offsetof(ComputeConstantData, cellDimension), .size = sizeof(float)},
      {.constantID = 5, .offset = offsetof(ComputeConstantData, elasticity), .size = sizeof(float)},
      {.constantID = 6, .offset = offsetof(ComputeConstantData, velocityDamper), .size = sizeof(float)}
   } };
   return {
      .mapEntryCount = static_cast<uint32_t>(mapEntries.size()),
      .pMapEntries = mapEntries.data(),
      .dataSize = sizeof(ComputeConstantData),
      .pData = &computeConstants
   };
}

VkPipelineVertexInputStateCreateInfo makeVertexInputStateCreateInfo(
   VkVertexInputBindingDescription& bindingDescription,
   VkVertexInputAttributeDescription* attribDescriptions)
{
   bindingDescription.stride = sizeof(Particle),
      bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
   attribDescriptions[0] = {
      .format = VK_FORMAT_R32G32_SFLOAT
   };
   attribDescriptions[1] = {
       .location = 1,
       .format = VK_FORMAT_R32_SFLOAT,
       .offset = offsetof(Particle, radius)
   };
   return {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &bindingDescription,
      .vertexAttributeDescriptionCount = ParticleInfo::inputMemberCount,
      .pVertexAttributeDescriptions = attribDescriptions
   };
}

VkPipelineViewportStateCreateInfo makePipelineViewportInfo()
{
   return {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1
   };
}

VkPipelineRasterizationStateCreateInfo makePipelineRasterizationInfo()
{
   return {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .lineWidth = 1.0f
   };
}

VkPipelineColorBlendStateCreateInfo makeColorBlendCreateInfo(
   VkPipelineColorBlendAttachmentState& colorBlendAttachState)
{
   colorBlendAttachState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
      VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
   return {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &colorBlendAttachState
   };
}

VkPipelineDynamicStateCreateInfo makeDynamicStateInfo(
   array<VkDynamicState, Application::DynamicStateCount>& dynamicStates)
{
   dynamicStates.at(0) = VK_DYNAMIC_STATE_VIEWPORT;
   dynamicStates.at(1) = VK_DYNAMIC_STATE_SCISSOR;
   return {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
      .pDynamicStates = dynamicStates.data()
   };
}

VkCommandBufferAllocateInfo makeCmdBufferAllocateInfo(const VkCommandPool& cmdPool)
{
   return {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cmdPool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1
   };
}

// Selects desired features from each Vulkan version
void setDesiredFeaturesList(FeaturesList& desiredFeatures)
{
   desiredFeatures.features13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = nullptr,
      .shaderDemoteToHelperInvocation = VK_TRUE,      // for 'discard' keyword in shader
      .synchronization2 = VK_TRUE,
      .dynamicRendering = VK_TRUE
   };
   desiredFeatures.features12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &desiredFeatures.features13,
      .timelineSemaphore = VK_TRUE
   };
   desiredFeatures.features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &desiredFeatures.features12
   };
}

VkDependencyInfo makeBufferBarrierDependInfo(
   const array<VkBufferMemoryBarrier2, Application::TransferBarrierCount>& bufferBarriers)
{
   return {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()
   };
}

VkBufferMemoryBarrier2 makeTransferBufferBarrier(
   const VkBuffer& buffer, VkPipelineStageFlagBits2 dstAccessMask)
{
   return {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | dstAccessMask,
      .buffer = buffer,
      .size = VK_WHOLE_SIZE
   };
}

VkBufferMemoryBarrier2 makeComputeBufferBarrier(
   const VkBuffer& buffer, VkPipelineStageFlagBits2 dstAccessMask)
{
   return {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | dstAccessMask,
      .buffer = buffer,
      .size = VK_WHOLE_SIZE
   };
}

VkBufferMemoryBarrier2 makeTransitionBufferBarrier(const VkBuffer& buffer)
{
   return {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
      .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
      .buffer = buffer,
      .size = VK_WHOLE_SIZE
   };
}

VkDependencyInfo makeBarrierDependInfo(
   const VkImageMemoryBarrier2& imgBarrier, const VkBufferMemoryBarrier2* bufferBarrier)
{
   return {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(!bufferBarrier ? 0 : 1),
      .pBufferMemoryBarriers = bufferBarrier,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &imgBarrier
   };
}

// Read an .spv file that represents a shader compiled to SPIR-V
vector<uint32_t> readSpvFile(const string& spvFilePath)
{
   std::ifstream spvInFilestream(spvFilePath, std::ios::binary | std::ios::ate);
   if (!spvInFilestream.is_open()) {
      std::cerr << "Could not open file at: " << spvFilePath << '\n';
      return vector<uint32_t>();
   }
   string nonPositiveMessage = "The ifstream was expected to have a positive size\n";
   string nonMultipleMessage = std::format(
      "Size of SPIR-V code file at {} must be a multiple of {:d} bytes\n", spvFilePath, sizeof(uint32_t));
   const std::streamsize spvSizeInBytes = spvInFilestream.tellg();
   if (!isPositveMultipleOf(spvSizeInBytes, sizeof(uint32_t), nonPositiveMessage, nonMultipleMessage))
      return vector<uint32_t>();
   vector<uint32_t> spvCode(spvSizeInBytes / sizeof(uint32_t));
   spvInFilestream.seekg(0);
   spvInFilestream.read(reinterpret_cast<char*>(spvCode.data()), spvSizeInBytes);
   if (!spvInFilestream) {
      std::cerr << "Failed to read SPIR-V from file at: " << spvFilePath << '\n';
      return vector<uint32_t>();
   }
   return spvCode;
}

bool isPositveMultipleOf(std::streamsize streamSz, uint32_t n, string nonPosMsg, string nonMultipleMsg)
{
   if (streamSz <= 0) {
      std::cerr << nonPosMsg;
      return false;
   }
   if (streamSz % n != 0) {
      std::cerr << nonMultipleMsg;
      return false;
   }
   return true;
}