// vulkan_backend.h — GPU compute backend using Vulkan with CPU fallback
// When Vulkan is unavailable, all methods transparently fall back to CPU.
#ifndef VULKAN_BACKEND_H
#define VULKAN_BACKEND_H
#include "generator.h"
#include "cuda_mock.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <mutex>
#if defined(__has_include)
#  if __has_include(<vulkan/vulkan.h>)
#    define VK_AVAILABLE 1
#    include <vulkan/vulkan.h>
#  endif
#endif
#ifndef EMBEDDED_SPIRV_PATH
#define EMBEDDED_SPIRV_PATH "shaders/chunk_gen.spv"
#endif
class VkChunkGenerator {
public:
  VkChunkGenerator() = default;
  ~VkChunkGenerator() { shutdown(); }

  bool init(int n = 256) {
#ifdef VK_AVAILABLE
    return vk_init(n);
#else
    (void)n; return false;
#endif
  }

  void generate(ChunkBuffer buf, int64_t cx, int64_t cz, int64_t seed) {
#ifdef VK_AVAILABLE
    if (vulkan_ready_) { vk_generate(buf, cx, cz, seed); return; }
#endif
    launch_chunk_generator(buf, cx, cz, seed, nullptr);
  }

  void shutdown() {
#ifdef VK_AVAILABLE
    if (vulkan_ready_ && device_) {
      vkDeviceWaitIdle(device_);
      vk_destroy_buffers(); vk_destroy_pipeline(); vk_cleanup();
      spv_code_.clear(); vulkan_ready_ = false;
    }
#endif
  }

  bool is_available() const { return vulkan_ready_; }

private:
  bool vulkan_ready_ = false;
  std::mutex mtx_;

#ifdef VK_AVAILABLE
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice phys_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue compute_queue_ = VK_NULL_HANDLE;
  VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_buf_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  VkShaderModule shader_module_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout desc_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet desc_set_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkBuffer output_buf_ = VK_NULL_HANDLE;
  VkDeviceMemory output_mem_ = VK_NULL_HANDLE;
  VkBuffer staging_buf_ = VK_NULL_HANDLE;
  VkDeviceMemory staging_mem_ = VK_NULL_HANDLE;
  size_t buf_capacity_ = 0;
  std::vector<uint32_t> spv_code_;
  std::string device_name_;
  uint32_t compute_family_ = UINT32_MAX;

  bool vk_init(int n); void vk_cleanup();
  bool vk_create_pipeline(); void vk_destroy_pipeline();
  bool vk_create_buffers(int n); void vk_destroy_buffers();
  void vk_generate(ChunkBuffer buf, int64_t cx, int64_t cz, int64_t seed);
  bool vk_load_spirv();
  bool vk_alloc_buffer(VkDeviceSize s, VkBufferUsageFlags u,
                       VkMemoryPropertyFlags m, VkBuffer* b, VkDeviceMemory* d);
#endif // VK_AVAILABLE
};

#ifdef VK_AVAILABLE
inline bool VkChunkGenerator::vk_load_spirv() {
  FILE* f = fopen(EMBEDDED_SPIRV_PATH, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
  if (len <= 0 || len % 4 != 0) { fclose(f); return false; }
  spv_code_.resize(len / 4);
  if (fread(spv_code_.data(), 1, len, f) != (size_t)len) { fclose(f); return false; }
  fclose(f); return true;
}

inline bool VkChunkGenerator::vk_init(int n) {
  if (!vk_load_spirv()) return false;
  VkApplicationInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  ai.pApplicationName = "McChunkGen"; ai.applicationVersion = VK_MAKE_VERSION(1,0,0);
  ai.pEngineName = "McChunkGen"; ai.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo ii{}; ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ii.pApplicationInfo = &ai;
  if (vkCreateInstance(&ii, nullptr, &instance_) != VK_SUCCESS) return false;

  uint32_t dc = 0; vkEnumeratePhysicalDevices(instance_, &dc, nullptr);
  if (dc == 0) return false;
  std::vector<VkPhysicalDevice> devs(dc);
  vkEnumeratePhysicalDevices(instance_, &dc, devs.data());
  for (uint32_t i = 0; i < dc; i++) {
    VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(devs[i], &p);
    uint32_t qc = 0; vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qc, nullptr);
    std::vector<VkQueueFamilyProperties> qps(qc);
    vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qc, qps.data());
    for (uint32_t q = 0; q < qc; q++) {
      if (qps[q].queueFlags & VK_QUEUE_COMPUTE_BIT) {
        if (!phys_device_ || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
          phys_device_ = devs[i]; compute_family_ = q; device_name_ = p.deviceName;
          if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) goto fdev;
        }
        break;
      }
    }
  }
  fdev:
  if (!phys_device_) return false;

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qi{}; qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qi.queueFamilyIndex = compute_family_; qi.queueCount = 1; qi.pQueuePriorities = &prio;
  VkDeviceCreateInfo di{}; di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
  if (vkCreateDevice(phys_device_, &di, nullptr, &device_) != VK_SUCCESS) return false;
  vkGetDeviceQueue(device_, compute_family_, 0, &compute_queue_);

  VkCommandPoolCreateInfo pci{}; pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pci.queueFamilyIndex = compute_family_;
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vkCreateCommandPool(device_, &pci, nullptr, &cmd_pool_) != VK_SUCCESS) return false;
  VkCommandBufferAllocateInfo abi{}; abi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  abi.commandPool = cmd_pool_; abi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  abi.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(device_, &abi, &cmd_buf_) != VK_SUCCESS) return false;
  VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  if (vkCreateFence(device_, &fci, nullptr, &fence_) != VK_SUCCESS) return false;
  if (!vk_create_pipeline() || !vk_create_buffers(n)) return false;
  vulkan_ready_ = true;
  printf("[Vulkan] Backend ready (device: %s)\n", device_name_.c_str());
  return true;
}

inline void VkChunkGenerator::vk_cleanup() {
  if (fence_) vkDestroyFence(device_, fence_, nullptr);
  if (cmd_pool_) vkDestroyCommandPool(device_, cmd_pool_, nullptr);
  if (device_) vkDestroyDevice(device_, nullptr);
  if (instance_) vkDestroyInstance(instance_, nullptr);
  instance_ = VK_NULL_HANDLE; device_ = VK_NULL_HANDLE;
  cmd_pool_ = VK_NULL_HANDLE; fence_ = VK_NULL_HANDLE;
}

inline bool VkChunkGenerator::vk_create_pipeline() {
  VkShaderModuleCreateInfo smi{}; smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smi.codeSize = spv_code_.size() * 4; smi.pCode = spv_code_.data();
  if (vkCreateShaderModule(device_, &smi, nullptr, &shader_module_) != VK_SUCCESS) return false;

  VkDescriptorSetLayoutBinding b{}; b.binding = 0;
  b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; b.descriptorCount = 1;
  b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkDescriptorSetLayoutCreateInfo dl{}; dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dl.bindingCount = 1; dl.pBindings = &b;
  if (vkCreateDescriptorSetLayout(device_, &dl, nullptr, &desc_set_layout_) != VK_SUCCESS) return false;

  VkDescriptorPoolSize ps{}; ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps.descriptorCount = 1;
  VkDescriptorPoolCreateInfo dp{}; dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dp.maxSets = 1; dp.poolSizeCount = 1; dp.pPoolSizes = &ps;
  if (vkCreateDescriptorPool(device_, &dp, nullptr, &desc_pool_) != VK_SUCCESS) return false;

  VkDescriptorSetAllocateInfo dai{}; dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dai.descriptorPool = desc_pool_; dai.descriptorSetCount = 1;
  dai.pSetLayouts = &desc_set_layout_;
  if (vkAllocateDescriptorSets(device_, &dai, &desc_set_) != VK_SUCCESS) return false;

  VkPushConstantRange pcr{}; pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pcr.offset = 0; pcr.size = sizeof(int64_t) * 4;
  VkPipelineLayoutCreateInfo pli{}; pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 1; pli.pSetLayouts = &desc_set_layout_;
  pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
  if (vkCreatePipelineLayout(device_, &pli, nullptr, &pipeline_layout_) != VK_SUCCESS) return false;

  VkPipelineShaderStageCreateInfo ss{}; ss.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  ss.stage = VK_SHADER_STAGE_COMPUTE_BIT; ss.module = shader_module_; ss.pName = "main";
  VkComputePipelineCreateInfo cpi{}; cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  cpi.stage = ss; cpi.layout = pipeline_layout_;
  if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipeline_) != VK_SUCCESS) return false;
  return true;
}

inline void VkChunkGenerator::vk_destroy_pipeline() {
  if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
  if (pipeline_layout_) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
  if (desc_set_layout_) vkDestroyDescriptorSetLayout(device_, desc_set_layout_, nullptr);
  if (desc_pool_) vkDestroyDescriptorPool(device_, desc_pool_, nullptr);
  if (shader_module_) vkDestroyShaderModule(device_, shader_module_, nullptr);
}

inline bool VkChunkGenerator::vk_create_buffers(int n) {
  VkDeviceSize cs = CHUNK_VOLUME; buf_capacity_ = cs * n;
  if (!vk_alloc_buffer(buf_capacity_,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &output_buf_, &output_mem_)) return false;
  if (!vk_alloc_buffer(buf_capacity_, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      &staging_buf_, &staging_mem_)) return false;
  VkDescriptorBufferInfo dbi{}; dbi.buffer = output_buf_; dbi.offset = 0; dbi.range = buf_capacity_;
  VkWriteDescriptorSet wds{}; wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  wds.dstSet = desc_set_; wds.dstBinding = 0; wds.descriptorCount = 1;
  wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; wds.pBufferInfo = &dbi;
  vkUpdateDescriptorSets(device_, 1, &wds, 0, nullptr);
  return true;
}

inline void VkChunkGenerator::vk_destroy_buffers() {
  if (staging_buf_) vkDestroyBuffer(device_, staging_buf_, nullptr);
  if (staging_mem_) vkFreeMemory(device_, staging_mem_, nullptr);
  if (output_buf_) vkDestroyBuffer(device_, output_buf_, nullptr);
  if (output_mem_) vkFreeMemory(device_, output_mem_, nullptr);
}

inline bool VkChunkGenerator::vk_alloc_buffer(VkDeviceSize s, VkBufferUsageFlags u,
    VkMemoryPropertyFlags m, VkBuffer* b, VkDeviceMemory* d) {
  VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = s; bi.usage = u; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device_, &bi, nullptr, b) != VK_SUCCESS) return false;
  VkMemoryRequirements r; vkGetBufferMemoryRequirements(device_, *b, &r);
  VkPhysicalDeviceMemoryProperties pm; vkGetPhysicalDeviceMemoryProperties(phys_device_, &pm);
  uint32_t mt = UINT32_MAX;
  for (uint32_t i = 0; i < pm.memoryTypeCount; i++)
    if ((r.memoryTypeBits & (1u << i)) && (pm.memoryTypes[i].propertyFlags & m) == m) { mt = i; break; }
  if (mt == UINT32_MAX) return false;
  VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.allocationSize = r.size; ai.memoryTypeIndex = mt;
  if (vkAllocateMemory(device_, &ai, nullptr, d) != VK_SUCCESS) return false;
  if (vkBindBufferMemory(device_, *b, *d, 0) != VK_SUCCESS) return false;
  return true;
}

inline void VkChunkGenerator::vk_generate(ChunkBuffer buf, int64_t cx, int64_t cz, int64_t seed) {
  std::lock_guard<std::mutex> lock(mtx_);
  struct Push { int64_t cx, cz, seed; float offset; };
  Push push = { cx, cz, seed, (float)(seed % 65536) * 137.631f };
  VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(cmd_buf_, &bi);
  vkCmdBindPipeline(cmd_buf_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
  vkCmdBindDescriptorSets(cmd_buf_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1, &desc_set_, 0, nullptr);
  vkCmdPushConstants(cmd_buf_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);
  vkCmdDispatch(cmd_buf_, 1, 1, 1);
  VkBufferCopy cr{}; cr.size = CHUNK_VOLUME;
  vkCmdCopyBuffer(cmd_buf_, output_buf_, staging_buf_, 1, &cr);
  vkEndCommandBuffer(cmd_buf_);
  VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1; si.pCommandBuffers = &cmd_buf_;
  vkQueueSubmit(compute_queue_, 1, &si, fence_);
  vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
  vkResetFences(device_, 1, &fence_);
  void* mapped; vkMapMemory(device_, staging_mem_, 0, CHUNK_VOLUME, 0, &mapped);
  std::memcpy(buf.data, mapped, CHUNK_VOLUME);
  vkUnmapMemory(device_, staging_mem_);
  vkResetCommandBuffer(cmd_buf_, 0);
}
#endif // VK_AVAILABLE
#endif // VULKAN_BACKEND_H
