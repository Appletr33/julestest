#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <random>

const int WIDTH = 800;
const int HEIGHT = 600;
const int NUM_SPHERES = 1000;

struct alignas(16) Sphere {
    float x, y, z, radius;
    float r, g, b, reflectivity;
};

struct PushConstants {
    uint32_t width;
    uint32_t height;
    uint32_t num_spheres;
    float padding;
    float cx, cy, cz, cw;
};

void checkResult(VkResult result, const char* msg) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(msg);
    }
}

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file!");
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

int main() {
    // 1. Create Instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Raytracer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkInstance instance;
    checkResult(vkCreateInstance(&createInfo, nullptr, &instance), "Failed to create instance");

    // 2. Pick Physical Device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    VkPhysicalDevice physicalDevice = devices[0];

    // 3. Create Logical Device
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    int computeFamily = -1;
    for (int i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            computeFamily = i;
            break;
        }
    }
    if (computeFamily == -1) {
        throw std::runtime_error("Failed to find a compute queue family");
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = computeFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;

    VkDevice device;
    checkResult(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device), "Failed to create logical device");

    VkQueue computeQueue;
    vkGetDeviceQueue(device, computeFamily, 0, &computeQueue);

    // 4. Create Buffers
    VkDeviceSize outputBufferSize = WIDTH * HEIGHT * 4 * sizeof(float);
    VkDeviceSize sceneBufferSize = NUM_SPHERES * sizeof(Sphere);

    // Create Output Buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = outputBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer outputBuffer;
    checkResult(vkCreateBuffer(device, &bufferInfo, nullptr, &outputBuffer), "Failed to create output buffer");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, outputBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory outputBufferMemory;
    checkResult(vkAllocateMemory(device, &allocInfo, nullptr, &outputBufferMemory), "Failed to allocate output buffer memory");
    vkBindBufferMemory(device, outputBuffer, outputBufferMemory, 0);

    // Create Scene Buffer
    bufferInfo.size = sceneBufferSize;
    VkBuffer sceneBuffer;
    checkResult(vkCreateBuffer(device, &bufferInfo, nullptr, &sceneBuffer), "Failed to create scene buffer");

    vkGetBufferMemoryRequirements(device, sceneBuffer, &memRequirements);
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkDeviceMemory sceneBufferMemory;
    checkResult(vkAllocateMemory(device, &allocInfo, nullptr, &sceneBufferMemory), "Failed to allocate scene buffer memory");
    vkBindBufferMemory(device, sceneBuffer, sceneBufferMemory, 0);

    // 5. Populate Scene Buffer
    Sphere* spheres;
    vkMapMemory(device, sceneBufferMemory, 0, sceneBufferSize, 0, (void**)&spheres);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-50.0f, 50.0f);
    std::uniform_real_distribution<float> colorDist(0.1f, 1.0f);
    std::uniform_real_distribution<float> radiusDist(0.5f, 2.0f);
    std::uniform_real_distribution<float> reflectDist(0.1f, 0.9f);

    for (int i = 0; i < NUM_SPHERES; ++i) {
        spheres[i].x = dist(rng);
        spheres[i].y = radiusDist(rng); // sit on ground
        spheres[i].z = dist(rng);
        spheres[i].radius = spheres[i].y;

        spheres[i].r = colorDist(rng);
        spheres[i].g = colorDist(rng);
        spheres[i].b = colorDist(rng);
        spheres[i].reflectivity = reflectDist(rng);
    }
    vkUnmapMemory(device, sceneBufferMemory);

    // 6. Descriptor Sets
    VkDescriptorSetLayoutBinding outputBinding{};
    outputBinding.binding = 0;
    outputBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    outputBinding.descriptorCount = 1;
    outputBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding sceneBinding{};
    sceneBinding.binding = 1;
    sceneBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sceneBinding.descriptorCount = 1;
    sceneBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding bindings[] = {outputBinding, sceneBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout descriptorSetLayout;
    checkResult(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout), "Failed to create descriptor set layout");

    VkDescriptorPoolSize poolSizes[2];
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;

    VkDescriptorPool descriptorPool;
    checkResult(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "Failed to create descriptor pool");

    VkDescriptorSetAllocateInfo allocSetInfo{};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = descriptorPool;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet descriptorSet;
    checkResult(vkAllocateDescriptorSets(device, &allocSetInfo, &descriptorSet), "Failed to allocate descriptor set");

    VkDescriptorBufferInfo outputBufferDescInfo{};
    outputBufferDescInfo.buffer = outputBuffer;
    outputBufferDescInfo.offset = 0;
    outputBufferDescInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo sceneBufferDescInfo{};
    sceneBufferDescInfo.buffer = sceneBuffer;
    sceneBufferDescInfo.offset = 0;
    sceneBufferDescInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet descriptorWrites[2]{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &outputBufferDescInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &sceneBufferDescInfo;

    vkUpdateDescriptorSets(device, 2, descriptorWrites, 0, nullptr);

    // 7. Compute Pipeline
    auto shaderCode = readFile("raytracer.spv");
    VkShaderModuleCreateInfo createShaderInfo{};
    createShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createShaderInfo.codeSize = shaderCode.size();
    createShaderInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    VkShaderModule computeShaderModule;
    checkResult(vkCreateShaderModule(device, &createShaderInfo, nullptr, &computeShaderModule), "Failed to create shader module");

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = computeShaderModule;
    shaderStageInfo.pName = "main";

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkPipelineLayout pipelineLayout;
    checkResult(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout), "Failed to create pipeline layout");

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = pipelineLayout;

    VkPipeline computePipeline;
    checkResult(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline), "Failed to create compute pipeline");

    // 8. Command Buffer
    VkCommandPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCreateInfo.queueFamilyIndex = computeFamily;

    VkCommandPool commandPool;
    checkResult(vkCreateCommandPool(device, &poolCreateInfo, nullptr, &commandPool), "Failed to create command pool");

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    checkResult(vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer), "Failed to allocate command buffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    checkResult(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Failed to begin command buffer");

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    PushConstants pcs{};
    pcs.width = WIDTH;
    pcs.height = HEIGHT;
    pcs.num_spheres = NUM_SPHERES;
    pcs.cx = 0.0f;
    pcs.cy = 80.0f; // High up looking down
    pcs.cz = 0.0f;

    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pcs);

    vkCmdDispatch(commandBuffer, (WIDTH + 15) / 16, (HEIGHT + 15) / 16, 1);

    checkResult(vkEndCommandBuffer(commandBuffer), "Failed to end command buffer");

    // 9. Submit and Wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    checkResult(vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit compute queue");
    vkQueueWaitIdle(computeQueue);

    // 10. Save Image
    float* mappedData;
    vkMapMemory(device, outputBufferMemory, 0, outputBufferSize, 0, (void**)&mappedData);

    std::ofstream ppmFile("output.ppm");
    ppmFile << "P3\n" << WIDTH << " " << HEIGHT << "\n255\n";

    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            int index = (y * WIDTH + x) * 4;
            float r = mappedData[index];
            float g = mappedData[index + 1];
            float b = mappedData[index + 2];

            // Clamp
            r = std::min(1.0f, std::max(0.0f, r));
            g = std::min(1.0f, std::max(0.0f, g));
            b = std::min(1.0f, std::max(0.0f, b));

            int ir = static_cast<int>(255.999 * r);
            int ig = static_cast<int>(255.999 * g);
            int ib = static_cast<int>(255.999 * b);

            ppmFile << ir << " " << ig << " " << ib << "\n";
        }
    }
    ppmFile.close();

    vkUnmapMemory(device, outputBufferMemory);

    // 11. Cleanup
    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyShaderModule(device, computeShaderModule, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    vkDestroyBuffer(device, sceneBuffer, nullptr);
    vkFreeMemory(device, sceneBufferMemory, nullptr);
    vkDestroyBuffer(device, outputBuffer, nullptr);
    vkFreeMemory(device, outputBufferMemory, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    std::cout << "Successfully rendered image to output.ppm" << std::endl;

    return 0;
}
