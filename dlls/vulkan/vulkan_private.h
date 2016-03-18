/*
 * Vulkan API implementation
 *
 * Copyright 2016 Sebastian Lackner
 * Copyright 2016 Michael Müller
 * Copyright 2016 Erich E. Hoover
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __VULKAN_PRIVATE_H
#define __VULKAN_PRIVATE_H

#include <stdint.h>

#if defined(HAVE_X11_XLIB_H)
# include <X11/Xlib.h>
#endif
#if defined(HAVE_X11_XLIB_XCB_H)
# include <X11/Xlib-xcb.h>
#endif

#if defined(__i386__)
# define USE_STRUCT_CONVERSION
#endif

#define VK_SUCCESS 0
#define VK_INCOMPLETE 5
#define VK_ERROR_EXTENSION_NOT_PRESENT (-7)
#define VK_ERROR_INCOMPATIBLE_DRIVER (-9)
#define VK_FALSE 0
#define VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR 1000004000
#define VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR 1000005000

#if !defined(HAVE_X11_XLIB_H)
typedef struct _XDisplay Display;
#endif
typedef void *PFN_vkAllocationFunction;
typedef void *PFN_vkAllocationFunction_host;
typedef void *PFN_vkDebugReportCallbackEXT;
typedef void *PFN_vkDebugReportCallbackEXT_host;
typedef void *PFN_vkFreeFunction;
typedef void *PFN_vkFreeFunction_host;
typedef void *PFN_vkInternalAllocationNotification;
typedef void *PFN_vkInternalAllocationNotification_host;
typedef void *PFN_vkInternalFreeNotification;
typedef void *PFN_vkInternalFreeNotification_host;
typedef void *PFN_vkReallocationFunction;
typedef void *PFN_vkReallocationFunction_host;
typedef void *PFN_vkVoidFunction;
typedef void *PFN_vkVoidFunction_host;
typedef void *VK_DEFINE_HANDLE;
typedef uint64_t VK_DEFINE_NON_DISPATCHABLE_HANDLE;
typedef uint32_t VkAccessFlags;
typedef uint32_t VkAttachmentDescriptionFlags;
typedef int VkAttachmentLoadOp;
typedef int VkAttachmentStoreOp;
typedef int VkBlendFactor;
typedef int VkBlendOp;
typedef uint32_t VkBool32;
typedef int VkBorderColor;
typedef uint64_t VkBuffer;
typedef uint32_t VkBufferCreateFlags;
typedef uint32_t VkBufferUsageFlags;
typedef uint64_t VkBufferView;
typedef uint32_t VkBufferViewCreateFlags;
typedef uint32_t VkColorComponentFlags;
typedef int VkColorSpaceKHR;
typedef void *VkCommandBuffer;
typedef int VkCommandBufferLevel;
typedef uint32_t VkCommandBufferResetFlags;
typedef uint32_t VkCommandBufferUsageFlags;
typedef uint64_t VkCommandPool;
typedef uint32_t VkCommandPoolCreateFlags;
typedef uint32_t VkCommandPoolResetFlags;
typedef int VkCompareOp;
typedef int VkComponentSwizzle;
typedef int VkCompositeAlphaFlagBitsKHR;
typedef uint32_t VkCompositeAlphaFlagsKHR;
typedef uint32_t VkCullModeFlags;
typedef uint64_t VkDebugReportCallbackEXT;
typedef uint32_t VkDebugReportFlagsEXT;
typedef int VkDebugReportObjectTypeEXT;
typedef uint32_t VkDependencyFlags;
typedef uint64_t VkDescriptorPool;
typedef uint32_t VkDescriptorPoolCreateFlags;
typedef uint32_t VkDescriptorPoolResetFlags;
typedef uint64_t VkDescriptorSet;
typedef uint64_t VkDescriptorSetLayout;
typedef uint32_t VkDescriptorSetLayoutCreateFlags;
typedef int VkDescriptorType;
typedef void *VkDevice;
typedef uint32_t VkDeviceCreateFlags;
typedef uint64_t VkDeviceMemory;
typedef uint32_t VkDeviceQueueCreateFlags;
typedef uint64_t VkDeviceSize;
typedef uint64_t VkDisplayKHR;
typedef uint32_t VkDisplayModeCreateFlagsKHR;
typedef uint64_t VkDisplayModeKHR;
typedef int VkDisplayPlaneAlphaFlagBitsKHR;
typedef uint32_t VkDisplayPlaneAlphaFlagsKHR;
typedef uint32_t VkDisplaySurfaceCreateFlagsKHR;
typedef int VkDynamicState;
typedef uint64_t VkEvent;
typedef uint32_t VkEventCreateFlags;
typedef uint64_t VkFence;
typedef uint32_t VkFenceCreateFlags;
typedef int VkFilter;
typedef uint32_t VkFlags;
typedef int VkFormat;
typedef uint32_t VkFormatFeatureFlags;
typedef uint64_t VkFramebuffer;
typedef uint32_t VkFramebufferCreateFlags;
typedef int VkFrontFace;
typedef uint64_t VkImage;
typedef uint32_t VkImageAspectFlags;
typedef uint32_t VkImageCreateFlags;
typedef int VkImageLayout;
typedef int VkImageTiling;
typedef int VkImageType;
typedef uint32_t VkImageUsageFlags;
typedef uint64_t VkImageView;
typedef uint32_t VkImageViewCreateFlags;
typedef int VkImageViewType;
typedef int VkIndexType;
typedef void *VkInstance;
typedef uint32_t VkInstanceCreateFlags;
typedef int VkLogicOp;
typedef uint32_t VkMemoryHeapFlags;
typedef uint32_t VkMemoryMapFlags;
typedef uint32_t VkMemoryPropertyFlags;
typedef void *VkPhysicalDevice;
typedef int VkPhysicalDeviceType;
typedef uint64_t VkPipeline;
typedef int VkPipelineBindPoint;
typedef uint64_t VkPipelineCache;
typedef uint32_t VkPipelineCacheCreateFlags;
typedef uint32_t VkPipelineColorBlendStateCreateFlags;
typedef uint32_t VkPipelineCreateFlags;
typedef uint32_t VkPipelineDepthStencilStateCreateFlags;
typedef uint32_t VkPipelineDynamicStateCreateFlags;
typedef uint32_t VkPipelineInputAssemblyStateCreateFlags;
typedef uint64_t VkPipelineLayout;
typedef uint32_t VkPipelineLayoutCreateFlags;
typedef uint32_t VkPipelineMultisampleStateCreateFlags;
typedef uint32_t VkPipelineRasterizationStateCreateFlags;
typedef uint32_t VkPipelineShaderStageCreateFlags;
typedef int VkPipelineStageFlagBits;
typedef uint32_t VkPipelineStageFlags;
typedef uint32_t VkPipelineTessellationStateCreateFlags;
typedef uint32_t VkPipelineVertexInputStateCreateFlags;
typedef uint32_t VkPipelineViewportStateCreateFlags;
typedef int VkPolygonMode;
typedef int VkPresentModeKHR;
typedef int VkPrimitiveTopology;
typedef uint32_t VkQueryControlFlags;
typedef uint32_t VkQueryPipelineStatisticFlags;
typedef uint64_t VkQueryPool;
typedef uint32_t VkQueryPoolCreateFlags;
typedef uint32_t VkQueryResultFlags;
typedef int VkQueryType;
typedef void *VkQueue;
typedef uint32_t VkQueueFlags;
typedef uint64_t VkRenderPass;
typedef uint32_t VkRenderPassCreateFlags;
typedef int VkResult;
typedef int VkSampleCountFlagBits;
typedef uint32_t VkSampleCountFlags;
typedef uint32_t VkSampleMask;
typedef uint64_t VkSampler;
typedef int VkSamplerAddressMode;
typedef uint32_t VkSamplerCreateFlags;
typedef int VkSamplerMipmapMode;
typedef uint64_t VkSemaphore;
typedef uint32_t VkSemaphoreCreateFlags;
typedef uint64_t VkShaderModule;
typedef uint32_t VkShaderModuleCreateFlags;
typedef int VkShaderStageFlagBits;
typedef uint32_t VkShaderStageFlags;
typedef int VkSharingMode;
typedef uint32_t VkSparseImageFormatFlags;
typedef uint32_t VkSparseMemoryBindFlags;
typedef uint32_t VkStencilFaceFlags;
typedef int VkStencilOp;
typedef int VkStructureType;
typedef int VkSubpassContents;
typedef uint32_t VkSubpassDescriptionFlags;
typedef uint64_t VkSurfaceKHR;
typedef int VkSurfaceTransformFlagBitsKHR;
typedef uint32_t VkSurfaceTransformFlagsKHR;
typedef uint32_t VkSwapchainCreateFlagsKHR;
typedef uint64_t VkSwapchainKHR;
typedef int VkVertexInputRate;
typedef uint32_t VkWin32SurfaceCreateFlagsKHR;
typedef uint32_t VkXcbSurfaceCreateFlagsKHR_host;
typedef uint32_t VkXlibSurfaceCreateFlagsKHR_host;
typedef unsigned long int Window;
#if !defined(HAVE_X11_XLIB_XCB_H)
typedef struct xcb_connection_t xcb_connection_t;
#endif
#if !defined(HAVE_X11_XLIB_XCB_H)
typedef uint32_t xcb_window_t;
#endif

typedef struct VkCommandBufferAllocateInfo
{
    VkStructureType sType;
    void *pNext;
    VkCommandPool DECLSPEC_ALIGN(8) commandPool;
    VkCommandBufferLevel level;
    uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferAllocateInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkCommandPool commandPool;
    VkCommandBufferLevel level;
    uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo_host;

typedef struct VkDescriptorSetAllocateInfo
{
    VkStructureType sType;
    void *pNext;
    VkDescriptorPool DECLSPEC_ALIGN(8) descriptorPool;
    uint32_t descriptorSetCount;
    VkDescriptorSetLayout *pSetLayouts;
} VkDescriptorSetAllocateInfo;

typedef struct VkDescriptorSetAllocateInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkDescriptorPool descriptorPool;
    uint32_t descriptorSetCount;
    VkDescriptorSetLayout *pSetLayouts;
} VkDescriptorSetAllocateInfo_host;

typedef struct VkMemoryAllocateInfo
{
    VkStructureType sType;
    void *pNext;
    VkDeviceSize DECLSPEC_ALIGN(8) allocationSize;
    uint32_t memoryTypeIndex;
} VkMemoryAllocateInfo;

typedef struct VkMemoryAllocateInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkDeviceSize allocationSize;
    uint32_t memoryTypeIndex;
} VkMemoryAllocateInfo_host;

typedef struct VkAllocationCallbacks
{
    void *pUserData;
    PFN_vkAllocationFunction pfnAllocation;
    PFN_vkReallocationFunction pfnReallocation;
    PFN_vkFreeFunction pfnFree;
    PFN_vkInternalAllocationNotification pfnInternalAllocation;
    PFN_vkInternalFreeNotification pfnInternalFree;
} VkAllocationCallbacks;

typedef struct VkAllocationCallbacks_host
{
    void *pUserData;
    PFN_vkAllocationFunction_host pfnAllocation;
    PFN_vkReallocationFunction_host pfnReallocation;
    PFN_vkFreeFunction_host pfnFree;
    PFN_vkInternalAllocationNotification_host pfnInternalAllocation;
    PFN_vkInternalFreeNotification_host pfnInternalFree;
} VkAllocationCallbacks_host;

typedef struct VkCommandBufferInheritanceInfo
{
    VkStructureType sType;
    void *pNext;
    VkRenderPass DECLSPEC_ALIGN(8) renderPass;
    uint32_t subpass;
    VkFramebuffer DECLSPEC_ALIGN(8) framebuffer;
    VkBool32 occlusionQueryEnable;
    VkQueryControlFlags queryFlags;
    VkQueryPipelineStatisticFlags pipelineStatistics;
} VkCommandBufferInheritanceInfo;

typedef struct VkCommandBufferInheritanceInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkRenderPass renderPass;
    uint32_t subpass;
    VkFramebuffer framebuffer;
    VkBool32 occlusionQueryEnable;
    VkQueryControlFlags queryFlags;
    VkQueryPipelineStatisticFlags pipelineStatistics;
} VkCommandBufferInheritanceInfo_host;

typedef struct VkCommandBufferBeginInfo
{
    VkStructureType sType;
    void *pNext;
    VkCommandBufferUsageFlags flags;
    VkCommandBufferInheritanceInfo *pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef struct VkCommandBufferBeginInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkCommandBufferUsageFlags flags;
    VkCommandBufferInheritanceInfo_host *pInheritanceInfo;
} VkCommandBufferBeginInfo_host;

typedef struct VkOffset2D
{
    int32_t x;
    int32_t y;
} VkOffset2D;

typedef struct VkExtent2D
{
    uint32_t width;
    uint32_t height;
} VkExtent2D;

typedef struct VkRect2D
{
    VkOffset2D offset;
    VkExtent2D extent;
} VkRect2D;

typedef union VkClearColorValue
{
    float float32[4];
    int32_t int32[4];
    uint32_t uint32[4];
} VkClearColorValue;

typedef struct VkClearDepthStencilValue
{
    float depth;
    uint32_t stencil;
} VkClearDepthStencilValue;

typedef union VkClearValue
{
    VkClearColorValue color;
    VkClearDepthStencilValue depthStencil;
} VkClearValue;

typedef struct VkRenderPassBeginInfo
{
    VkStructureType sType;
    void *pNext;
    VkRenderPass DECLSPEC_ALIGN(8) renderPass;
    VkFramebuffer DECLSPEC_ALIGN(8) framebuffer;
    VkRect2D renderArea;
    uint32_t clearValueCount;
    VkClearValue *pClearValues;
} VkRenderPassBeginInfo;

typedef struct VkRenderPassBeginInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    VkRect2D renderArea;
    uint32_t clearValueCount;
    VkClearValue *pClearValues;
} VkRenderPassBeginInfo_host;

typedef struct VkImageSubresourceLayers
{
    VkImageAspectFlags aspectMask;
    uint32_t mipLevel;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkImageSubresourceLayers;

typedef struct VkOffset3D
{
    int32_t x;
    int32_t y;
    int32_t z;
} VkOffset3D;

typedef struct VkImageBlit
{
    VkImageSubresourceLayers srcSubresource;
    VkOffset3D srcOffsets[2];
    VkImageSubresourceLayers dstSubresource;
    VkOffset3D dstOffsets[2];
} VkImageBlit;

typedef struct VkClearAttachment
{
    VkImageAspectFlags aspectMask;
    uint32_t colorAttachment;
    VkClearValue clearValue;
} VkClearAttachment;

typedef struct VkClearRect
{
    VkRect2D rect;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkClearRect;

typedef struct VkImageSubresourceRange
{
    VkImageAspectFlags aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkImageSubresourceRange;

typedef struct VkBufferCopy
{
    VkDeviceSize DECLSPEC_ALIGN(8) srcOffset;
    VkDeviceSize DECLSPEC_ALIGN(8) dstOffset;
    VkDeviceSize DECLSPEC_ALIGN(8) size;
} VkBufferCopy;

typedef struct VkBufferCopy_host
{
    VkDeviceSize srcOffset;
    VkDeviceSize dstOffset;
    VkDeviceSize size;
} VkBufferCopy_host;

typedef struct VkExtent3D
{
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} VkExtent3D;

typedef struct VkBufferImageCopy
{
    VkDeviceSize DECLSPEC_ALIGN(8) bufferOffset;
    uint32_t bufferRowLength;
    uint32_t bufferImageHeight;
    VkImageSubresourceLayers imageSubresource;
    VkOffset3D imageOffset;
    VkExtent3D imageExtent;
} VkBufferImageCopy;

typedef struct VkBufferImageCopy_host
{
    VkDeviceSize bufferOffset;
    uint32_t bufferRowLength;
    uint32_t bufferImageHeight;
    VkImageSubresourceLayers imageSubresource;
    VkOffset3D imageOffset;
    VkExtent3D imageExtent;
} VkBufferImageCopy_host;

typedef struct VkImageCopy
{
    VkImageSubresourceLayers srcSubresource;
    VkOffset3D srcOffset;
    VkImageSubresourceLayers dstSubresource;
    VkOffset3D dstOffset;
    VkExtent3D extent;
} VkImageCopy;

typedef struct VkMemoryBarrier
{
    VkStructureType sType;
    void *pNext;
    VkAccessFlags srcAccessMask;
    VkAccessFlags dstAccessMask;
} VkMemoryBarrier;

typedef struct VkBufferMemoryBarrier
{
    VkStructureType sType;
    void *pNext;
    VkAccessFlags srcAccessMask;
    VkAccessFlags dstAccessMask;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    VkBuffer DECLSPEC_ALIGN(8) buffer;
    VkDeviceSize DECLSPEC_ALIGN(8) offset;
    VkDeviceSize DECLSPEC_ALIGN(8) size;
} VkBufferMemoryBarrier;

typedef struct VkBufferMemoryBarrier_host
{
    VkStructureType sType;
    void *pNext;
    VkAccessFlags srcAccessMask;
    VkAccessFlags dstAccessMask;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize size;
} VkBufferMemoryBarrier_host;

typedef struct VkImageMemoryBarrier
{
    VkStructureType sType;
    void *pNext;
    VkAccessFlags srcAccessMask;
    VkAccessFlags dstAccessMask;
    VkImageLayout oldLayout;
    VkImageLayout newLayout;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    VkImage DECLSPEC_ALIGN(8) image;
    VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier;

typedef struct VkImageMemoryBarrier_host
{
    VkStructureType sType;
    void *pNext;
    VkAccessFlags srcAccessMask;
    VkAccessFlags dstAccessMask;
    VkImageLayout oldLayout;
    VkImageLayout newLayout;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    VkImage image;
    VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier_host;

typedef struct VkImageResolve
{
    VkImageSubresourceLayers srcSubresource;
    VkOffset3D srcOffset;
    VkImageSubresourceLayers dstSubresource;
    VkOffset3D dstOffset;
    VkExtent3D extent;
} VkImageResolve;

typedef struct VkViewport
{
    float x;
    float y;
    float width;
    float height;
    float minDepth;
    float maxDepth;
} VkViewport;

typedef struct VkBufferCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkBufferCreateFlags flags;
    VkDeviceSize DECLSPEC_ALIGN(8) size;
    VkBufferUsageFlags usage;
    VkSharingMode sharingMode;
    uint32_t queueFamilyIndexCount;
    uint32_t *pQueueFamilyIndices;
} VkBufferCreateInfo;

typedef struct VkBufferCreateInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkBufferCreateFlags flags;
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VkSharingMode sharingMode;
    uint32_t queueFamilyIndexCount;
    uint32_t *pQueueFamilyIndices;
} VkBufferCreateInfo_host;

typedef struct VkBufferViewCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkBufferViewCreateFlags flags;
    VkBuffer DECLSPEC_ALIGN(8) buffer;
    VkFormat format;
    VkDeviceSize DECLSPEC_ALIGN(8) offset;
    VkDeviceSize DECLSPEC_ALIGN(8) range;
} VkBufferViewCreateInfo;

typedef struct VkBufferViewCreateInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkBufferViewCreateFlags flags;
    VkBuffer buffer;
    VkFormat format;
    VkDeviceSize offset;
    VkDeviceSize range;
} VkBufferViewCreateInfo_host;

typedef struct VkCommandPoolCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkCommandPoolCreateFlags flags;
    uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkSpecializationMapEntry
{
    uint32_t constantID;
    uint32_t offset;
    size_t size;
} VkSpecializationMapEntry;

typedef struct VkSpecializationInfo
{
    uint32_t mapEntryCount;
    VkSpecializationMapEntry *pMapEntries;
    size_t dataSize;
    void *pData;
} VkSpecializationInfo;

typedef struct VkPipelineShaderStageCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineShaderStageCreateFlags flags;
    VkShaderStageFlagBits stage;
    VkShaderModule DECLSPEC_ALIGN(8) module;
    char *pName;
    VkSpecializationInfo *pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;

typedef struct VkPipelineShaderStageCreateInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkPipelineShaderStageCreateFlags flags;
    VkShaderStageFlagBits stage;
    VkShaderModule module;
    char *pName;
    VkSpecializationInfo *pSpecializationInfo;
} VkPipelineShaderStageCreateInfo_host;

typedef struct VkComputePipelineCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineCreateFlags flags;
    VkPipelineShaderStageCreateInfo DECLSPEC_ALIGN(8) stage;
    VkPipelineLayout DECLSPEC_ALIGN(8) layout;
    VkPipeline DECLSPEC_ALIGN(8) basePipelineHandle;
    int32_t basePipelineIndex;
} VkComputePipelineCreateInfo;

typedef struct VkComputePipelineCreateInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkPipelineCreateFlags flags;
    VkPipelineShaderStageCreateInfo_host stage;
    VkPipelineLayout layout;
    VkPipeline basePipelineHandle;
    int32_t basePipelineIndex;
} VkComputePipelineCreateInfo_host;

typedef struct VkDebugReportCallbackCreateInfoEXT
{
    VkStructureType sType;
    void *pNext;
    VkDebugReportFlagsEXT flags;
    PFN_vkDebugReportCallbackEXT pfnCallback;
    void *pUserData;
} VkDebugReportCallbackCreateInfoEXT;

typedef struct VkDebugReportCallbackCreateInfoEXT_host
{
    VkStructureType sType;
    void *pNext;
    VkDebugReportFlagsEXT flags;
    PFN_vkDebugReportCallbackEXT_host pfnCallback;
    void *pUserData;
} VkDebugReportCallbackCreateInfoEXT_host;

typedef struct VkDescriptorPoolSize
{
    VkDescriptorType type;
    uint32_t descriptorCount;
} VkDescriptorPoolSize;

typedef struct VkDescriptorPoolCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkDescriptorPoolCreateFlags flags;
    uint32_t maxSets;
    uint32_t poolSizeCount;
    VkDescriptorPoolSize *pPoolSizes;
} VkDescriptorPoolCreateInfo;

typedef struct VkDescriptorSetLayoutBinding
{
    uint32_t binding;
    VkDescriptorType descriptorType;
    uint32_t descriptorCount;
    VkShaderStageFlags stageFlags;
    VkSampler *pImmutableSamplers;
} VkDescriptorSetLayoutBinding;

typedef struct VkDescriptorSetLayoutCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkDescriptorSetLayoutCreateFlags flags;
    uint32_t bindingCount;
    VkDescriptorSetLayoutBinding *pBindings;
} VkDescriptorSetLayoutCreateInfo;

typedef struct VkDeviceQueueCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkDeviceQueueCreateFlags flags;
    uint32_t queueFamilyIndex;
    uint32_t queueCount;
    float *pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkPhysicalDeviceFeatures
{
    VkBool32 robustBufferAccess;
    VkBool32 fullDrawIndexUint32;
    VkBool32 imageCubeArray;
    VkBool32 independentBlend;
    VkBool32 geometryShader;
    VkBool32 tessellationShader;
    VkBool32 sampleRateShading;
    VkBool32 dualSrcBlend;
    VkBool32 logicOp;
    VkBool32 multiDrawIndirect;
    VkBool32 drawIndirectFirstInstance;
    VkBool32 depthClamp;
    VkBool32 depthBiasClamp;
    VkBool32 fillModeNonSolid;
    VkBool32 depthBounds;
    VkBool32 wideLines;
    VkBool32 largePoints;
    VkBool32 alphaToOne;
    VkBool32 multiViewport;
    VkBool32 samplerAnisotropy;
    VkBool32 textureCompressionETC2;
    VkBool32 textureCompressionASTC_LDR;
    VkBool32 textureCompressionBC;
    VkBool32 occlusionQueryPrecise;
    VkBool32 pipelineStatisticsQuery;
    VkBool32 vertexPipelineStoresAndAtomics;
    VkBool32 fragmentStoresAndAtomics;
    VkBool32 shaderTessellationAndGeometryPointSize;
    VkBool32 shaderImageGatherExtended;
    VkBool32 shaderStorageImageExtendedFormats;
    VkBool32 shaderStorageImageMultisample;
    VkBool32 shaderStorageImageReadWithoutFormat;
    VkBool32 shaderStorageImageWriteWithoutFormat;
    VkBool32 shaderUniformBufferArrayDynamicIndexing;
    VkBool32 shaderSampledImageArrayDynamicIndexing;
    VkBool32 shaderStorageBufferArrayDynamicIndexing;
    VkBool32 shaderStorageImageArrayDynamicIndexing;
    VkBool32 shaderClipDistance;
    VkBool32 shaderCullDistance;
    VkBool32 shaderFloat64;
    VkBool32 shaderInt64;
    VkBool32 shaderInt16;
    VkBool32 shaderResourceResidency;
    VkBool32 shaderResourceMinLod;
    VkBool32 sparseBinding;
    VkBool32 sparseResidencyBuffer;
    VkBool32 sparseResidencyImage2D;
    VkBool32 sparseResidencyImage3D;
    VkBool32 sparseResidency2Samples;
    VkBool32 sparseResidency4Samples;
    VkBool32 sparseResidency8Samples;
    VkBool32 sparseResidency16Samples;
    VkBool32 sparseResidencyAliased;
    VkBool32 variableMultisampleRate;
    VkBool32 inheritedQueries;
} VkPhysicalDeviceFeatures;

typedef struct VkDeviceCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkDeviceCreateFlags flags;
    uint32_t queueCreateInfoCount;
    VkDeviceQueueCreateInfo *pQueueCreateInfos;
    uint32_t enabledLayerCount;
    char **ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    char **ppEnabledExtensionNames;
    VkPhysicalDeviceFeatures *pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkDisplayModeParametersKHR
{
    VkExtent2D visibleRegion;
    uint32_t refreshRate;
} VkDisplayModeParametersKHR;

typedef struct VkDisplayModeCreateInfoKHR
{
    VkStructureType sType;
    void *pNext;
    VkDisplayModeCreateFlagsKHR flags;
    VkDisplayModeParametersKHR parameters;
} VkDisplayModeCreateInfoKHR;

typedef struct VkDisplaySurfaceCreateInfoKHR
{
    VkStructureType sType;
    void *pNext;
    VkDisplaySurfaceCreateFlagsKHR flags;
    VkDisplayModeKHR DECLSPEC_ALIGN(8) displayMode;
    uint32_t planeIndex;
    uint32_t planeStackIndex;
    VkSurfaceTransformFlagBitsKHR transform;
    float globalAlpha;
    VkDisplayPlaneAlphaFlagBitsKHR alphaMode;
    VkExtent2D imageExtent;
} VkDisplaySurfaceCreateInfoKHR;

typedef struct VkDisplaySurfaceCreateInfoKHR_host
{
    VkStructureType sType;
    void *pNext;
    VkDisplaySurfaceCreateFlagsKHR flags;
    VkDisplayModeKHR displayMode;
    uint32_t planeIndex;
    uint32_t planeStackIndex;
    VkSurfaceTransformFlagBitsKHR transform;
    float globalAlpha;
    VkDisplayPlaneAlphaFlagBitsKHR alphaMode;
    VkExtent2D imageExtent;
} VkDisplaySurfaceCreateInfoKHR_host;

typedef struct VkEventCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkEventCreateFlags flags;
} VkEventCreateInfo;

typedef struct VkFenceCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkFenceCreateFlags flags;
} VkFenceCreateInfo;

typedef struct VkFramebufferCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkFramebufferCreateFlags flags;
    VkRenderPass DECLSPEC_ALIGN(8) renderPass;
    uint32_t attachmentCount;
    VkImageView *pAttachments;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
} VkFramebufferCreateInfo;

typedef struct VkFramebufferCreateInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkFramebufferCreateFlags flags;
    VkRenderPass renderPass;
    uint32_t attachmentCount;
    VkImageView *pAttachments;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
} VkFramebufferCreateInfo_host;

typedef struct VkVertexInputBindingDescription
{
    uint32_t binding;
    uint32_t stride;
    VkVertexInputRate inputRate;
} VkVertexInputBindingDescription;

typedef struct VkVertexInputAttributeDescription
{
    uint32_t location;
    uint32_t binding;
    VkFormat format;
    uint32_t offset;
} VkVertexInputAttributeDescription;

typedef struct VkPipelineVertexInputStateCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineVertexInputStateCreateFlags flags;
    uint32_t vertexBindingDescriptionCount;
    VkVertexInputBindingDescription *pVertexBindingDescriptions;
    uint32_t vertexAttributeDescriptionCount;
    VkVertexInputAttributeDescription *pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;

typedef struct VkPipelineInputAssemblyStateCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineInputAssemblyStateCreateFlags flags;
    VkPrimitiveTopology topology;
    VkBool32 primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;

typedef struct VkPipelineTessellationStateCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineTessellationStateCreateFlags flags;
    uint32_t patchControlPoints;
} VkPipelineTessellationStateCreateInfo;

typedef struct VkPipelineViewportStateCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineViewportStateCreateFlags flags;
    uint32_t viewportCount;
    VkViewport *pViewports;
    uint32_t scissorCount;
    VkRect2D *pScissors;
} VkPipelineViewportStateCreateInfo;

typedef struct VkPipelineRasterizationStateCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineRasterizationStateCreateFlags flags;
    VkBool32 depthClampEnable;
    VkBool32 rasterizerDiscardEnable;
    VkPolygonMode polygonMode;
    VkCullModeFlags cullMode;
    VkFrontFace frontFace;
    VkBool32 depthBiasEnable;
    float depthBiasConstantFactor;
    float depthBiasClamp;
    float depthBiasSlopeFactor;
    float lineWidth;
} VkPipelineRasterizationStateCreateInfo;

typedef struct VkPipelineMultisampleStateCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineMultisampleStateCreateFlags flags;
    VkSampleCountFlagBits rasterizationSamples;
    VkBool32 sampleShadingEnable;
    float minSampleShading;
    VkSampleMask *pSampleMask;
    VkBool32 alphaToCoverageEnable;
    VkBool32 alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;

typedef struct VkStencilOpState
{
    VkStencilOp failOp;
    VkStencilOp passOp;
    VkStencilOp depthFailOp;
    VkCompareOp compareOp;
    uint32_t compareMask;
    uint32_t writeMask;
    uint32_t reference;
} VkStencilOpState;

typedef struct VkPipelineDepthStencilStateCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineDepthStencilStateCreateFlags flags;
    VkBool32 depthTestEnable;
    VkBool32 depthWriteEnable;
    VkCompareOp depthCompareOp;
    VkBool32 depthBoundsTestEnable;
    VkBool32 stencilTestEnable;
    VkStencilOpState front;
    VkStencilOpState back;
    float minDepthBounds;
    float maxDepthBounds;
} VkPipelineDepthStencilStateCreateInfo;

typedef struct VkPipelineColorBlendAttachmentState
{
    VkBool32 blendEnable;
    VkBlendFactor srcColorBlendFactor;
    VkBlendFactor dstColorBlendFactor;
    VkBlendOp colorBlendOp;
    VkBlendFactor srcAlphaBlendFactor;
    VkBlendFactor dstAlphaBlendFactor;
    VkBlendOp alphaBlendOp;
    VkColorComponentFlags colorWriteMask;
} VkPipelineColorBlendAttachmentState;

typedef struct VkPipelineColorBlendStateCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineColorBlendStateCreateFlags flags;
    VkBool32 logicOpEnable;
    VkLogicOp logicOp;
    uint32_t attachmentCount;
    VkPipelineColorBlendAttachmentState *pAttachments;
    float blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;

typedef struct VkPipelineDynamicStateCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineDynamicStateCreateFlags flags;
    uint32_t dynamicStateCount;
    VkDynamicState *pDynamicStates;
} VkPipelineDynamicStateCreateInfo;

typedef struct VkGraphicsPipelineCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineCreateFlags flags;
    uint32_t stageCount;
    VkPipelineShaderStageCreateInfo *pStages;
    VkPipelineVertexInputStateCreateInfo *pVertexInputState;
    VkPipelineInputAssemblyStateCreateInfo *pInputAssemblyState;
    VkPipelineTessellationStateCreateInfo *pTessellationState;
    VkPipelineViewportStateCreateInfo *pViewportState;
    VkPipelineRasterizationStateCreateInfo *pRasterizationState;
    VkPipelineMultisampleStateCreateInfo *pMultisampleState;
    VkPipelineDepthStencilStateCreateInfo *pDepthStencilState;
    VkPipelineColorBlendStateCreateInfo *pColorBlendState;
    VkPipelineDynamicStateCreateInfo *pDynamicState;
    VkPipelineLayout DECLSPEC_ALIGN(8) layout;
    VkRenderPass DECLSPEC_ALIGN(8) renderPass;
    uint32_t subpass;
    VkPipeline DECLSPEC_ALIGN(8) basePipelineHandle;
    int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

typedef struct VkGraphicsPipelineCreateInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkPipelineCreateFlags flags;
    uint32_t stageCount;
    VkPipelineShaderStageCreateInfo_host *pStages;
    VkPipelineVertexInputStateCreateInfo *pVertexInputState;
    VkPipelineInputAssemblyStateCreateInfo *pInputAssemblyState;
    VkPipelineTessellationStateCreateInfo *pTessellationState;
    VkPipelineViewportStateCreateInfo *pViewportState;
    VkPipelineRasterizationStateCreateInfo *pRasterizationState;
    VkPipelineMultisampleStateCreateInfo *pMultisampleState;
    VkPipelineDepthStencilStateCreateInfo *pDepthStencilState;
    VkPipelineColorBlendStateCreateInfo *pColorBlendState;
    VkPipelineDynamicStateCreateInfo *pDynamicState;
    VkPipelineLayout layout;
    VkRenderPass renderPass;
    uint32_t subpass;
    VkPipeline basePipelineHandle;
    int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo_host;

typedef struct VkImageCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkImageCreateFlags flags;
    VkImageType imageType;
    VkFormat format;
    VkExtent3D extent;
    uint32_t mipLevels;
    uint32_t arrayLayers;
    VkSampleCountFlagBits samples;
    VkImageTiling tiling;
    VkImageUsageFlags usage;
    VkSharingMode sharingMode;
    uint32_t queueFamilyIndexCount;
    uint32_t *pQueueFamilyIndices;
    VkImageLayout initialLayout;
} VkImageCreateInfo;

typedef struct VkComponentMapping
{
    VkComponentSwizzle r;
    VkComponentSwizzle g;
    VkComponentSwizzle b;
    VkComponentSwizzle a;
} VkComponentMapping;

typedef struct VkImageViewCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkImageViewCreateFlags flags;
    VkImage DECLSPEC_ALIGN(8) image;
    VkImageViewType viewType;
    VkFormat format;
    VkComponentMapping components;
    VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;

typedef struct VkImageViewCreateInfo_host
{
    VkStructureType sType;
    void *pNext;
    VkImageViewCreateFlags flags;
    VkImage image;
    VkImageViewType viewType;
    VkFormat format;
    VkComponentMapping components;
    VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo_host;

typedef struct VkApplicationInfo
{
    VkStructureType sType;
    void *pNext;
    char *pApplicationName;
    uint32_t applicationVersion;
    char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkInstanceCreateFlags flags;
    VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    char **ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    char **ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkPipelineCacheCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineCacheCreateFlags flags;
    size_t initialDataSize;
    void *pInitialData;
} VkPipelineCacheCreateInfo;

typedef struct VkPushConstantRange
{
    VkShaderStageFlags stageFlags;
    uint32_t offset;
    uint32_t size;
} VkPushConstantRange;

typedef struct VkPipelineLayoutCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkPipelineLayoutCreateFlags flags;
    uint32_t setLayoutCount;
    VkDescriptorSetLayout *pSetLayouts;
    uint32_t pushConstantRangeCount;
    VkPushConstantRange *pPushConstantRanges;
} VkPipelineLayoutCreateInfo;

typedef struct VkQueryPoolCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkQueryPoolCreateFlags flags;
    VkQueryType queryType;
    uint32_t queryCount;
    VkQueryPipelineStatisticFlags pipelineStatistics;
} VkQueryPoolCreateInfo;

typedef struct VkAttachmentDescription
{
    VkAttachmentDescriptionFlags flags;
    VkFormat format;
    VkSampleCountFlagBits samples;
    VkAttachmentLoadOp loadOp;
    VkAttachmentStoreOp storeOp;
    VkAttachmentLoadOp stencilLoadOp;
    VkAttachmentStoreOp stencilStoreOp;
    VkImageLayout initialLayout;
    VkImageLayout finalLayout;
} VkAttachmentDescription;

typedef struct VkAttachmentReference
{
    uint32_t attachment;
    VkImageLayout layout;
} VkAttachmentReference;

typedef struct VkSubpassDescription
{
    VkSubpassDescriptionFlags flags;
    VkPipelineBindPoint pipelineBindPoint;
    uint32_t inputAttachmentCount;
    VkAttachmentReference *pInputAttachments;
    uint32_t colorAttachmentCount;
    VkAttachmentReference *pColorAttachments;
    VkAttachmentReference *pResolveAttachments;
    VkAttachmentReference *pDepthStencilAttachment;
    uint32_t preserveAttachmentCount;
    uint32_t *pPreserveAttachments;
} VkSubpassDescription;

typedef struct VkSubpassDependency
{
    uint32_t srcSubpass;
    uint32_t dstSubpass;
    VkPipelineStageFlags srcStageMask;
    VkPipelineStageFlags dstStageMask;
    VkAccessFlags srcAccessMask;
    VkAccessFlags dstAccessMask;
    VkDependencyFlags dependencyFlags;
} VkSubpassDependency;

typedef struct VkRenderPassCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkRenderPassCreateFlags flags;
    uint32_t attachmentCount;
    VkAttachmentDescription *pAttachments;
    uint32_t subpassCount;
    VkSubpassDescription *pSubpasses;
    uint32_t dependencyCount;
    VkSubpassDependency *pDependencies;
} VkRenderPassCreateInfo;

typedef struct VkSamplerCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkSamplerCreateFlags flags;
    VkFilter magFilter;
    VkFilter minFilter;
    VkSamplerMipmapMode mipmapMode;
    VkSamplerAddressMode addressModeU;
    VkSamplerAddressMode addressModeV;
    VkSamplerAddressMode addressModeW;
    float mipLodBias;
    VkBool32 anisotropyEnable;
    float maxAnisotropy;
    VkBool32 compareEnable;
    VkCompareOp compareOp;
    float minLod;
    float maxLod;
    VkBorderColor borderColor;
    VkBool32 unnormalizedCoordinates;
} VkSamplerCreateInfo;

typedef struct VkSemaphoreCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkSemaphoreCreateFlags flags;
} VkSemaphoreCreateInfo;

typedef struct VkShaderModuleCreateInfo
{
    VkStructureType sType;
    void *pNext;
    VkShaderModuleCreateFlags flags;
    size_t codeSize;
    uint32_t *pCode;
} VkShaderModuleCreateInfo;

typedef struct VkSwapchainCreateInfoKHR
{
    VkStructureType sType;
    void *pNext;
    VkSwapchainCreateFlagsKHR flags;
    VkSurfaceKHR DECLSPEC_ALIGN(8) surface;
    uint32_t minImageCount;
    VkFormat imageFormat;
    VkColorSpaceKHR imageColorSpace;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkImageUsageFlags imageUsage;
    VkSharingMode imageSharingMode;
    uint32_t queueFamilyIndexCount;
    uint32_t *pQueueFamilyIndices;
    VkSurfaceTransformFlagBitsKHR preTransform;
    VkCompositeAlphaFlagBitsKHR compositeAlpha;
    VkPresentModeKHR presentMode;
    VkBool32 clipped;
    VkSwapchainKHR DECLSPEC_ALIGN(8) oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef struct VkSwapchainCreateInfoKHR_host
{
    VkStructureType sType;
    void *pNext;
    VkSwapchainCreateFlagsKHR flags;
    VkSurfaceKHR surface;
    uint32_t minImageCount;
    VkFormat imageFormat;
    VkColorSpaceKHR imageColorSpace;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkImageUsageFlags imageUsage;
    VkSharingMode imageSharingMode;
    uint32_t queueFamilyIndexCount;
    uint32_t *pQueueFamilyIndices;
    VkSurfaceTransformFlagBitsKHR preTransform;
    VkCompositeAlphaFlagBitsKHR compositeAlpha;
    VkPresentModeKHR presentMode;
    VkBool32 clipped;
    VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR_host;

typedef struct VkWin32SurfaceCreateInfoKHR
{
    VkStructureType sType;
    void *pNext;
    VkWin32SurfaceCreateFlagsKHR flags;
    HINSTANCE hinstance;
    HWND hwnd;
} VkWin32SurfaceCreateInfoKHR;

typedef struct VkXcbSurfaceCreateInfoKHR_host
{
    VkStructureType sType;
    void *pNext;
    VkXcbSurfaceCreateFlagsKHR_host flags;
    xcb_connection_t *connection;
    xcb_window_t window;
} VkXcbSurfaceCreateInfoKHR_host;

typedef struct VkXlibSurfaceCreateInfoKHR_host
{
    VkStructureType sType;
    void *pNext;
    VkXlibSurfaceCreateFlagsKHR_host flags;
    Display *dpy;
    Window window;
} VkXlibSurfaceCreateInfoKHR_host;

typedef struct VkExtensionProperties
{
    char extensionName[256];
    uint32_t specVersion;
} VkExtensionProperties;

typedef struct VkLayerProperties
{
    char layerName[256];
    uint32_t specVersion;
    uint32_t implementationVersion;
    char description[256];
} VkLayerProperties;

typedef struct VkMappedMemoryRange
{
    VkStructureType sType;
    void *pNext;
    VkDeviceMemory DECLSPEC_ALIGN(8) memory;
    VkDeviceSize DECLSPEC_ALIGN(8) offset;
    VkDeviceSize DECLSPEC_ALIGN(8) size;
} VkMappedMemoryRange;

typedef struct VkMappedMemoryRange_host
{
    VkStructureType sType;
    void *pNext;
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
} VkMappedMemoryRange_host;

typedef struct VkMemoryRequirements
{
    VkDeviceSize DECLSPEC_ALIGN(8) size;
    VkDeviceSize DECLSPEC_ALIGN(8) alignment;
    uint32_t memoryTypeBits;
} VkMemoryRequirements;

typedef struct VkMemoryRequirements_host
{
    VkDeviceSize size;
    VkDeviceSize alignment;
    uint32_t memoryTypeBits;
} VkMemoryRequirements_host;

typedef struct VkDisplayModePropertiesKHR
{
    VkDisplayModeKHR DECLSPEC_ALIGN(8) displayMode;
    VkDisplayModeParametersKHR parameters;
} VkDisplayModePropertiesKHR;

typedef struct VkDisplayModePropertiesKHR_host
{
    VkDisplayModeKHR displayMode;
    VkDisplayModeParametersKHR parameters;
} VkDisplayModePropertiesKHR_host;

typedef struct VkDisplayPlaneCapabilitiesKHR
{
    VkDisplayPlaneAlphaFlagsKHR supportedAlpha;
    VkOffset2D minSrcPosition;
    VkOffset2D maxSrcPosition;
    VkExtent2D minSrcExtent;
    VkExtent2D maxSrcExtent;
    VkOffset2D minDstPosition;
    VkOffset2D maxDstPosition;
    VkExtent2D minDstExtent;
    VkExtent2D maxDstExtent;
} VkDisplayPlaneCapabilitiesKHR;

typedef struct VkSparseImageFormatProperties
{
    VkImageAspectFlags aspectMask;
    VkExtent3D imageGranularity;
    VkSparseImageFormatFlags flags;
} VkSparseImageFormatProperties;

typedef struct VkSparseImageMemoryRequirements
{
    VkSparseImageFormatProperties formatProperties;
    uint32_t imageMipTailFirstLod;
    VkDeviceSize DECLSPEC_ALIGN(8) imageMipTailSize;
    VkDeviceSize DECLSPEC_ALIGN(8) imageMipTailOffset;
    VkDeviceSize DECLSPEC_ALIGN(8) imageMipTailStride;
} VkSparseImageMemoryRequirements;

typedef struct VkSparseImageMemoryRequirements_host
{
    VkSparseImageFormatProperties formatProperties;
    uint32_t imageMipTailFirstLod;
    VkDeviceSize imageMipTailSize;
    VkDeviceSize imageMipTailOffset;
    VkDeviceSize imageMipTailStride;
} VkSparseImageMemoryRequirements_host;

typedef struct VkImageSubresource
{
    VkImageAspectFlags aspectMask;
    uint32_t mipLevel;
    uint32_t arrayLayer;
} VkImageSubresource;

typedef struct VkSubresourceLayout
{
    VkDeviceSize DECLSPEC_ALIGN(8) offset;
    VkDeviceSize DECLSPEC_ALIGN(8) size;
    VkDeviceSize DECLSPEC_ALIGN(8) rowPitch;
    VkDeviceSize DECLSPEC_ALIGN(8) arrayPitch;
    VkDeviceSize DECLSPEC_ALIGN(8) depthPitch;
} VkSubresourceLayout;

typedef struct VkSubresourceLayout_host
{
    VkDeviceSize offset;
    VkDeviceSize size;
    VkDeviceSize rowPitch;
    VkDeviceSize arrayPitch;
    VkDeviceSize depthPitch;
} VkSubresourceLayout_host;

typedef struct VkDisplayPlanePropertiesKHR
{
    VkDisplayKHR DECLSPEC_ALIGN(8) currentDisplay;
    uint32_t currentStackIndex;
} VkDisplayPlanePropertiesKHR;

typedef struct VkDisplayPlanePropertiesKHR_host
{
    VkDisplayKHR currentDisplay;
    uint32_t currentStackIndex;
} VkDisplayPlanePropertiesKHR_host;

typedef struct VkDisplayPropertiesKHR
{
    VkDisplayKHR DECLSPEC_ALIGN(8) display;
    char *displayName;
    VkExtent2D physicalDimensions;
    VkExtent2D physicalResolution;
    VkSurfaceTransformFlagsKHR supportedTransforms;
    VkBool32 planeReorderPossible;
    VkBool32 persistentContent;
} VkDisplayPropertiesKHR;

typedef struct VkDisplayPropertiesKHR_host
{
    VkDisplayKHR display;
    char *displayName;
    VkExtent2D physicalDimensions;
    VkExtent2D physicalResolution;
    VkSurfaceTransformFlagsKHR supportedTransforms;
    VkBool32 planeReorderPossible;
    VkBool32 persistentContent;
} VkDisplayPropertiesKHR_host;

typedef struct VkFormatProperties
{
    VkFormatFeatureFlags linearTilingFeatures;
    VkFormatFeatureFlags optimalTilingFeatures;
    VkFormatFeatureFlags bufferFeatures;
} VkFormatProperties;

typedef struct VkImageFormatProperties
{
    VkExtent3D maxExtent;
    uint32_t maxMipLevels;
    uint32_t maxArrayLayers;
    VkSampleCountFlags sampleCounts;
    VkDeviceSize DECLSPEC_ALIGN(8) maxResourceSize;
} VkImageFormatProperties;

typedef struct VkImageFormatProperties_host
{
    VkExtent3D maxExtent;
    uint32_t maxMipLevels;
    uint32_t maxArrayLayers;
    VkSampleCountFlags sampleCounts;
    VkDeviceSize maxResourceSize;
} VkImageFormatProperties_host;

typedef struct VkMemoryType
{
    VkMemoryPropertyFlags propertyFlags;
    uint32_t heapIndex;
} VkMemoryType;

typedef struct VkMemoryHeap
{
    VkDeviceSize DECLSPEC_ALIGN(8) size;
    VkMemoryHeapFlags flags;
} VkMemoryHeap;

typedef struct VkMemoryHeap_host
{
    VkDeviceSize size;
    VkMemoryHeapFlags flags;
} VkMemoryHeap_host;

typedef struct VkPhysicalDeviceMemoryProperties
{
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32];
    uint32_t memoryHeapCount;
    VkMemoryHeap DECLSPEC_ALIGN(8) memoryHeaps[16];
} VkPhysicalDeviceMemoryProperties;

typedef struct VkPhysicalDeviceMemoryProperties_host
{
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32];
    uint32_t memoryHeapCount;
    VkMemoryHeap_host memoryHeaps[16];
} VkPhysicalDeviceMemoryProperties_host;

typedef struct VkPhysicalDeviceLimits
{
    uint32_t maxImageDimension1D;
    uint32_t maxImageDimension2D;
    uint32_t maxImageDimension3D;
    uint32_t maxImageDimensionCube;
    uint32_t maxImageArrayLayers;
    uint32_t maxTexelBufferElements;
    uint32_t maxUniformBufferRange;
    uint32_t maxStorageBufferRange;
    uint32_t maxPushConstantsSize;
    uint32_t maxMemoryAllocationCount;
    uint32_t maxSamplerAllocationCount;
    VkDeviceSize DECLSPEC_ALIGN(8) bufferImageGranularity;
    VkDeviceSize DECLSPEC_ALIGN(8) sparseAddressSpaceSize;
    uint32_t maxBoundDescriptorSets;
    uint32_t maxPerStageDescriptorSamplers;
    uint32_t maxPerStageDescriptorUniformBuffers;
    uint32_t maxPerStageDescriptorStorageBuffers;
    uint32_t maxPerStageDescriptorSampledImages;
    uint32_t maxPerStageDescriptorStorageImages;
    uint32_t maxPerStageDescriptorInputAttachments;
    uint32_t maxPerStageResources;
    uint32_t maxDescriptorSetSamplers;
    uint32_t maxDescriptorSetUniformBuffers;
    uint32_t maxDescriptorSetUniformBuffersDynamic;
    uint32_t maxDescriptorSetStorageBuffers;
    uint32_t maxDescriptorSetStorageBuffersDynamic;
    uint32_t maxDescriptorSetSampledImages;
    uint32_t maxDescriptorSetStorageImages;
    uint32_t maxDescriptorSetInputAttachments;
    uint32_t maxVertexInputAttributes;
    uint32_t maxVertexInputBindings;
    uint32_t maxVertexInputAttributeOffset;
    uint32_t maxVertexInputBindingStride;
    uint32_t maxVertexOutputComponents;
    uint32_t maxTessellationGenerationLevel;
    uint32_t maxTessellationPatchSize;
    uint32_t maxTessellationControlPerVertexInputComponents;
    uint32_t maxTessellationControlPerVertexOutputComponents;
    uint32_t maxTessellationControlPerPatchOutputComponents;
    uint32_t maxTessellationControlTotalOutputComponents;
    uint32_t maxTessellationEvaluationInputComponents;
    uint32_t maxTessellationEvaluationOutputComponents;
    uint32_t maxGeometryShaderInvocations;
    uint32_t maxGeometryInputComponents;
    uint32_t maxGeometryOutputComponents;
    uint32_t maxGeometryOutputVertices;
    uint32_t maxGeometryTotalOutputComponents;
    uint32_t maxFragmentInputComponents;
    uint32_t maxFragmentOutputAttachments;
    uint32_t maxFragmentDualSrcAttachments;
    uint32_t maxFragmentCombinedOutputResources;
    uint32_t maxComputeSharedMemorySize;
    uint32_t maxComputeWorkGroupCount[3];
    uint32_t maxComputeWorkGroupInvocations;
    uint32_t maxComputeWorkGroupSize[3];
    uint32_t subPixelPrecisionBits;
    uint32_t subTexelPrecisionBits;
    uint32_t mipmapPrecisionBits;
    uint32_t maxDrawIndexedIndexValue;
    uint32_t maxDrawIndirectCount;
    float maxSamplerLodBias;
    float maxSamplerAnisotropy;
    uint32_t maxViewports;
    uint32_t maxViewportDimensions[2];
    float viewportBoundsRange[2];
    uint32_t viewportSubPixelBits;
    size_t minMemoryMapAlignment;
    VkDeviceSize DECLSPEC_ALIGN(8) minTexelBufferOffsetAlignment;
    VkDeviceSize DECLSPEC_ALIGN(8) minUniformBufferOffsetAlignment;
    VkDeviceSize DECLSPEC_ALIGN(8) minStorageBufferOffsetAlignment;
    int32_t minTexelOffset;
    uint32_t maxTexelOffset;
    int32_t minTexelGatherOffset;
    uint32_t maxTexelGatherOffset;
    float minInterpolationOffset;
    float maxInterpolationOffset;
    uint32_t subPixelInterpolationOffsetBits;
    uint32_t maxFramebufferWidth;
    uint32_t maxFramebufferHeight;
    uint32_t maxFramebufferLayers;
    VkSampleCountFlags framebufferColorSampleCounts;
    VkSampleCountFlags framebufferDepthSampleCounts;
    VkSampleCountFlags framebufferStencilSampleCounts;
    VkSampleCountFlags framebufferNoAttachmentsSampleCounts;
    uint32_t maxColorAttachments;
    VkSampleCountFlags sampledImageColorSampleCounts;
    VkSampleCountFlags sampledImageIntegerSampleCounts;
    VkSampleCountFlags sampledImageDepthSampleCounts;
    VkSampleCountFlags sampledImageStencilSampleCounts;
    VkSampleCountFlags storageImageSampleCounts;
    uint32_t maxSampleMaskWords;
    VkBool32 timestampComputeAndGraphics;
    float timestampPeriod;
    uint32_t maxClipDistances;
    uint32_t maxCullDistances;
    uint32_t maxCombinedClipAndCullDistances;
    uint32_t discreteQueuePriorities;
    float pointSizeRange[2];
    float lineWidthRange[2];
    float pointSizeGranularity;
    float lineWidthGranularity;
    VkBool32 strictLines;
    VkBool32 standardSampleLocations;
    VkDeviceSize DECLSPEC_ALIGN(8) optimalBufferCopyOffsetAlignment;
    VkDeviceSize DECLSPEC_ALIGN(8) optimalBufferCopyRowPitchAlignment;
    VkDeviceSize DECLSPEC_ALIGN(8) nonCoherentAtomSize;
} VkPhysicalDeviceLimits;

typedef struct VkPhysicalDeviceLimits_host
{
    uint32_t maxImageDimension1D;
    uint32_t maxImageDimension2D;
    uint32_t maxImageDimension3D;
    uint32_t maxImageDimensionCube;
    uint32_t maxImageArrayLayers;
    uint32_t maxTexelBufferElements;
    uint32_t maxUniformBufferRange;
    uint32_t maxStorageBufferRange;
    uint32_t maxPushConstantsSize;
    uint32_t maxMemoryAllocationCount;
    uint32_t maxSamplerAllocationCount;
    VkDeviceSize bufferImageGranularity;
    VkDeviceSize sparseAddressSpaceSize;
    uint32_t maxBoundDescriptorSets;
    uint32_t maxPerStageDescriptorSamplers;
    uint32_t maxPerStageDescriptorUniformBuffers;
    uint32_t maxPerStageDescriptorStorageBuffers;
    uint32_t maxPerStageDescriptorSampledImages;
    uint32_t maxPerStageDescriptorStorageImages;
    uint32_t maxPerStageDescriptorInputAttachments;
    uint32_t maxPerStageResources;
    uint32_t maxDescriptorSetSamplers;
    uint32_t maxDescriptorSetUniformBuffers;
    uint32_t maxDescriptorSetUniformBuffersDynamic;
    uint32_t maxDescriptorSetStorageBuffers;
    uint32_t maxDescriptorSetStorageBuffersDynamic;
    uint32_t maxDescriptorSetSampledImages;
    uint32_t maxDescriptorSetStorageImages;
    uint32_t maxDescriptorSetInputAttachments;
    uint32_t maxVertexInputAttributes;
    uint32_t maxVertexInputBindings;
    uint32_t maxVertexInputAttributeOffset;
    uint32_t maxVertexInputBindingStride;
    uint32_t maxVertexOutputComponents;
    uint32_t maxTessellationGenerationLevel;
    uint32_t maxTessellationPatchSize;
    uint32_t maxTessellationControlPerVertexInputComponents;
    uint32_t maxTessellationControlPerVertexOutputComponents;
    uint32_t maxTessellationControlPerPatchOutputComponents;
    uint32_t maxTessellationControlTotalOutputComponents;
    uint32_t maxTessellationEvaluationInputComponents;
    uint32_t maxTessellationEvaluationOutputComponents;
    uint32_t maxGeometryShaderInvocations;
    uint32_t maxGeometryInputComponents;
    uint32_t maxGeometryOutputComponents;
    uint32_t maxGeometryOutputVertices;
    uint32_t maxGeometryTotalOutputComponents;
    uint32_t maxFragmentInputComponents;
    uint32_t maxFragmentOutputAttachments;
    uint32_t maxFragmentDualSrcAttachments;
    uint32_t maxFragmentCombinedOutputResources;
    uint32_t maxComputeSharedMemorySize;
    uint32_t maxComputeWorkGroupCount[3];
    uint32_t maxComputeWorkGroupInvocations;
    uint32_t maxComputeWorkGroupSize[3];
    uint32_t subPixelPrecisionBits;
    uint32_t subTexelPrecisionBits;
    uint32_t mipmapPrecisionBits;
    uint32_t maxDrawIndexedIndexValue;
    uint32_t maxDrawIndirectCount;
    float maxSamplerLodBias;
    float maxSamplerAnisotropy;
    uint32_t maxViewports;
    uint32_t maxViewportDimensions[2];
    float viewportBoundsRange[2];
    uint32_t viewportSubPixelBits;
    size_t minMemoryMapAlignment;
    VkDeviceSize minTexelBufferOffsetAlignment;
    VkDeviceSize minUniformBufferOffsetAlignment;
    VkDeviceSize minStorageBufferOffsetAlignment;
    int32_t minTexelOffset;
    uint32_t maxTexelOffset;
    int32_t minTexelGatherOffset;
    uint32_t maxTexelGatherOffset;
    float minInterpolationOffset;
    float maxInterpolationOffset;
    uint32_t subPixelInterpolationOffsetBits;
    uint32_t maxFramebufferWidth;
    uint32_t maxFramebufferHeight;
    uint32_t maxFramebufferLayers;
    VkSampleCountFlags framebufferColorSampleCounts;
    VkSampleCountFlags framebufferDepthSampleCounts;
    VkSampleCountFlags framebufferStencilSampleCounts;
    VkSampleCountFlags framebufferNoAttachmentsSampleCounts;
    uint32_t maxColorAttachments;
    VkSampleCountFlags sampledImageColorSampleCounts;
    VkSampleCountFlags sampledImageIntegerSampleCounts;
    VkSampleCountFlags sampledImageDepthSampleCounts;
    VkSampleCountFlags sampledImageStencilSampleCounts;
    VkSampleCountFlags storageImageSampleCounts;
    uint32_t maxSampleMaskWords;
    VkBool32 timestampComputeAndGraphics;
    float timestampPeriod;
    uint32_t maxClipDistances;
    uint32_t maxCullDistances;
    uint32_t maxCombinedClipAndCullDistances;
    uint32_t discreteQueuePriorities;
    float pointSizeRange[2];
    float lineWidthRange[2];
    float pointSizeGranularity;
    float lineWidthGranularity;
    VkBool32 strictLines;
    VkBool32 standardSampleLocations;
    VkDeviceSize optimalBufferCopyOffsetAlignment;
    VkDeviceSize optimalBufferCopyRowPitchAlignment;
    VkDeviceSize nonCoherentAtomSize;
} VkPhysicalDeviceLimits_host;

typedef struct VkPhysicalDeviceSparseProperties
{
    VkBool32 residencyStandard2DBlockShape;
    VkBool32 residencyStandard2DMultisampleBlockShape;
    VkBool32 residencyStandard3DBlockShape;
    VkBool32 residencyAlignedMipSize;
    VkBool32 residencyNonResidentStrict;
} VkPhysicalDeviceSparseProperties;

typedef struct VkPhysicalDeviceProperties
{
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    VkPhysicalDeviceType deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    VkPhysicalDeviceLimits DECLSPEC_ALIGN(8) limits;
    VkPhysicalDeviceSparseProperties sparseProperties;
} VkPhysicalDeviceProperties;

typedef struct VkPhysicalDeviceProperties_host
{
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    VkPhysicalDeviceType deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    VkPhysicalDeviceLimits_host limits;
    VkPhysicalDeviceSparseProperties sparseProperties;
} VkPhysicalDeviceProperties_host;

typedef struct VkQueueFamilyProperties
{
    VkQueueFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    VkExtent3D minImageTransferGranularity;
} VkQueueFamilyProperties;

typedef struct VkSurfaceCapabilitiesKHR
{
    uint32_t minImageCount;
    uint32_t maxImageCount;
    VkExtent2D currentExtent;
    VkExtent2D minImageExtent;
    VkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers;
    VkSurfaceTransformFlagsKHR supportedTransforms;
    VkSurfaceTransformFlagBitsKHR currentTransform;
    VkCompositeAlphaFlagsKHR supportedCompositeAlpha;
    VkImageUsageFlags supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;

typedef struct VkSurfaceFormatKHR
{
    VkFormat format;
    VkColorSpaceKHR colorSpace;
} VkSurfaceFormatKHR;

typedef struct VkSparseMemoryBind
{
    VkDeviceSize DECLSPEC_ALIGN(8) resourceOffset;
    VkDeviceSize DECLSPEC_ALIGN(8) size;
    VkDeviceMemory DECLSPEC_ALIGN(8) memory;
    VkDeviceSize DECLSPEC_ALIGN(8) memoryOffset;
    VkSparseMemoryBindFlags flags;
} VkSparseMemoryBind;

typedef struct VkSparseMemoryBind_host
{
    VkDeviceSize resourceOffset;
    VkDeviceSize size;
    VkDeviceMemory memory;
    VkDeviceSize memoryOffset;
    VkSparseMemoryBindFlags flags;
} VkSparseMemoryBind_host;

typedef struct VkSparseBufferMemoryBindInfo
{
    VkBuffer DECLSPEC_ALIGN(8) buffer;
    uint32_t bindCount;
    VkSparseMemoryBind *pBinds;
} VkSparseBufferMemoryBindInfo;

typedef struct VkSparseBufferMemoryBindInfo_host
{
    VkBuffer buffer;
    uint32_t bindCount;
    VkSparseMemoryBind_host *pBinds;
} VkSparseBufferMemoryBindInfo_host;

typedef struct VkSparseImageOpaqueMemoryBindInfo
{
    VkImage DECLSPEC_ALIGN(8) image;
    uint32_t bindCount;
    VkSparseMemoryBind *pBinds;
} VkSparseImageOpaqueMemoryBindInfo;

typedef struct VkSparseImageOpaqueMemoryBindInfo_host
{
    VkImage image;
    uint32_t bindCount;
    VkSparseMemoryBind_host *pBinds;
} VkSparseImageOpaqueMemoryBindInfo_host;

typedef struct VkSparseImageMemoryBind
{
    VkImageSubresource subresource;
    VkOffset3D offset;
    VkExtent3D extent;
    VkDeviceMemory DECLSPEC_ALIGN(8) memory;
    VkDeviceSize DECLSPEC_ALIGN(8) memoryOffset;
    VkSparseMemoryBindFlags flags;
} VkSparseImageMemoryBind;

typedef struct VkSparseImageMemoryBind_host
{
    VkImageSubresource subresource;
    VkOffset3D offset;
    VkExtent3D extent;
    VkDeviceMemory memory;
    VkDeviceSize memoryOffset;
    VkSparseMemoryBindFlags flags;
} VkSparseImageMemoryBind_host;

typedef struct VkSparseImageMemoryBindInfo
{
    VkImage DECLSPEC_ALIGN(8) image;
    uint32_t bindCount;
    VkSparseImageMemoryBind *pBinds;
} VkSparseImageMemoryBindInfo;

typedef struct VkSparseImageMemoryBindInfo_host
{
    VkImage image;
    uint32_t bindCount;
    VkSparseImageMemoryBind_host *pBinds;
} VkSparseImageMemoryBindInfo_host;

typedef struct VkBindSparseInfo
{
    VkStructureType sType;
    void *pNext;
    uint32_t waitSemaphoreCount;
    VkSemaphore *pWaitSemaphores;
    uint32_t bufferBindCount;
    VkSparseBufferMemoryBindInfo *pBufferBinds;
    uint32_t imageOpaqueBindCount;
    VkSparseImageOpaqueMemoryBindInfo *pImageOpaqueBinds;
    uint32_t imageBindCount;
    VkSparseImageMemoryBindInfo *pImageBinds;
    uint32_t signalSemaphoreCount;
    VkSemaphore *pSignalSemaphores;
} VkBindSparseInfo;

typedef struct VkBindSparseInfo_host
{
    VkStructureType sType;
    void *pNext;
    uint32_t waitSemaphoreCount;
    VkSemaphore *pWaitSemaphores;
    uint32_t bufferBindCount;
    VkSparseBufferMemoryBindInfo_host *pBufferBinds;
    uint32_t imageOpaqueBindCount;
    VkSparseImageOpaqueMemoryBindInfo_host *pImageOpaqueBinds;
    uint32_t imageBindCount;
    VkSparseImageMemoryBindInfo_host *pImageBinds;
    uint32_t signalSemaphoreCount;
    VkSemaphore *pSignalSemaphores;
} VkBindSparseInfo_host;

typedef struct VkPresentInfoKHR
{
    VkStructureType sType;
    void *pNext;
    uint32_t waitSemaphoreCount;
    VkSemaphore *pWaitSemaphores;
    uint32_t swapchainCount;
    VkSwapchainKHR *pSwapchains;
    uint32_t *pImageIndices;
    VkResult *pResults;
} VkPresentInfoKHR;

typedef struct VkSubmitInfo
{
    VkStructureType sType;
    void *pNext;
    uint32_t waitSemaphoreCount;
    VkSemaphore *pWaitSemaphores;
    VkPipelineStageFlags *pWaitDstStageMask;
    uint32_t commandBufferCount;
    VkCommandBuffer *pCommandBuffers;
    uint32_t signalSemaphoreCount;
    VkSemaphore *pSignalSemaphores;
} VkSubmitInfo;

typedef struct VkDescriptorImageInfo
{
    VkSampler DECLSPEC_ALIGN(8) sampler;
    VkImageView DECLSPEC_ALIGN(8) imageView;
    VkImageLayout imageLayout;
} VkDescriptorImageInfo;

typedef struct VkDescriptorImageInfo_host
{
    VkSampler sampler;
    VkImageView imageView;
    VkImageLayout imageLayout;
} VkDescriptorImageInfo_host;

typedef struct VkDescriptorBufferInfo
{
    VkBuffer DECLSPEC_ALIGN(8) buffer;
    VkDeviceSize DECLSPEC_ALIGN(8) offset;
    VkDeviceSize DECLSPEC_ALIGN(8) range;
} VkDescriptorBufferInfo;

typedef struct VkDescriptorBufferInfo_host
{
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize range;
} VkDescriptorBufferInfo_host;

typedef struct VkWriteDescriptorSet
{
    VkStructureType sType;
    void *pNext;
    VkDescriptorSet DECLSPEC_ALIGN(8) dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
    VkDescriptorType descriptorType;
    VkDescriptorImageInfo *pImageInfo;
    VkDescriptorBufferInfo *pBufferInfo;
    VkBufferView *pTexelBufferView;
} VkWriteDescriptorSet;

typedef struct VkWriteDescriptorSet_host
{
    VkStructureType sType;
    void *pNext;
    VkDescriptorSet dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
    VkDescriptorType descriptorType;
    VkDescriptorImageInfo_host *pImageInfo;
    VkDescriptorBufferInfo_host *pBufferInfo;
    VkBufferView *pTexelBufferView;
} VkWriteDescriptorSet_host;

typedef struct VkCopyDescriptorSet
{
    VkStructureType sType;
    void *pNext;
    VkDescriptorSet DECLSPEC_ALIGN(8) srcSet;
    uint32_t srcBinding;
    uint32_t srcArrayElement;
    VkDescriptorSet DECLSPEC_ALIGN(8) dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
} VkCopyDescriptorSet;

typedef struct VkCopyDescriptorSet_host
{
    VkStructureType sType;
    void *pNext;
    VkDescriptorSet srcSet;
    uint32_t srcBinding;
    uint32_t srcArrayElement;
    VkDescriptorSet dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
} VkCopyDescriptorSet_host;

extern VkResult (*p_vkAcquireNextImageKHR)( VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore,
        VkFence, uint32_t * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkAllocateCommandBuffers)( VkDevice, const VkCommandBufferAllocateInfo *,
        VkCommandBuffer * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkAllocateDescriptorSets)( VkDevice, const VkDescriptorSetAllocateInfo *,
        VkDescriptorSet * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkAllocateMemory)( VkDevice, const VkMemoryAllocateInfo *,
        const VkAllocationCallbacks_host *, VkDeviceMemory * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkBeginCommandBuffer)( VkCommandBuffer,
        const VkCommandBufferBeginInfo_host * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkBindBufferMemory)( VkDevice, VkBuffer, VkDeviceMemory,
        VkDeviceSize ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkBindImageMemory)( VkDevice, VkImage, VkDeviceMemory,
        VkDeviceSize ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdBeginQuery)( VkCommandBuffer, VkQueryPool, uint32_t,
        VkQueryControlFlags ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdBeginRenderPass)( VkCommandBuffer, const VkRenderPassBeginInfo *,
        VkSubpassContents ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdBindDescriptorSets)( VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout,
        uint32_t, uint32_t, const VkDescriptorSet *, uint32_t, const uint32_t * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdBindIndexBuffer)( VkCommandBuffer, VkBuffer, VkDeviceSize,
        VkIndexType ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdBindPipeline)( VkCommandBuffer, VkPipelineBindPoint,
        VkPipeline ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdBindVertexBuffers)( VkCommandBuffer, uint32_t, uint32_t, const VkBuffer *,
        const VkDeviceSize * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdBlitImage)( VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout,
        uint32_t, const VkImageBlit *, VkFilter ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdClearAttachments)( VkCommandBuffer, uint32_t, const VkClearAttachment *,
        uint32_t, const VkClearRect * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdClearColorImage)( VkCommandBuffer, VkImage, VkImageLayout,
        const VkClearColorValue *, uint32_t, const VkImageSubresourceRange * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdClearDepthStencilImage)( VkCommandBuffer, VkImage, VkImageLayout,
        const VkClearDepthStencilValue *, uint32_t,
        const VkImageSubresourceRange * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdCopyBuffer)( VkCommandBuffer, VkBuffer, VkBuffer, uint32_t,
        const VkBufferCopy * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdCopyBufferToImage)( VkCommandBuffer, VkBuffer, VkImage, VkImageLayout,
        uint32_t, const VkBufferImageCopy * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdCopyImage)( VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout,
        uint32_t, const VkImageCopy * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdCopyImageToBuffer)( VkCommandBuffer, VkImage, VkImageLayout, VkBuffer,
        uint32_t, const VkBufferImageCopy * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdCopyQueryPoolResults)( VkCommandBuffer, VkQueryPool, uint32_t, uint32_t,
        VkBuffer, VkDeviceSize, VkDeviceSize, VkQueryResultFlags ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdDispatch)( VkCommandBuffer, uint32_t, uint32_t, uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdDispatchIndirect)( VkCommandBuffer, VkBuffer, VkDeviceSize ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdDraw)( VkCommandBuffer, uint32_t, uint32_t, uint32_t,
        uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdDrawIndexed)( VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t,
        uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdDrawIndexedIndirect)( VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t,
        uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdDrawIndirect)( VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t,
        uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdEndQuery)( VkCommandBuffer, VkQueryPool, uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdEndRenderPass)( VkCommandBuffer ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdExecuteCommands)( VkCommandBuffer, uint32_t,
        const VkCommandBuffer * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdFillBuffer)( VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize,
        uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdNextSubpass)( VkCommandBuffer, VkSubpassContents ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdPipelineBarrier)( VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
        VkDependencyFlags, uint32_t, const VkMemoryBarrier *, uint32_t,
        const VkBufferMemoryBarrier *, uint32_t,
        const VkImageMemoryBarrier_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdPushConstants)( VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags,
        uint32_t, uint32_t, const void * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdResetEvent)( VkCommandBuffer, VkEvent, VkPipelineStageFlags ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdResetQueryPool)( VkCommandBuffer, VkQueryPool, uint32_t,
        uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdResolveImage)( VkCommandBuffer, VkImage, VkImageLayout, VkImage,
        VkImageLayout, uint32_t, const VkImageResolve * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdSetBlendConstants)( VkCommandBuffer, const float[4] ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdSetDepthBias)( VkCommandBuffer, float, float, float ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdSetDepthBounds)( VkCommandBuffer, float, float ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdSetEvent)( VkCommandBuffer, VkEvent, VkPipelineStageFlags ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdSetLineWidth)( VkCommandBuffer, float ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdSetScissor)( VkCommandBuffer, uint32_t, uint32_t,
        const VkRect2D * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdSetStencilCompareMask)( VkCommandBuffer, VkStencilFaceFlags,
        uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdSetStencilReference)( VkCommandBuffer, VkStencilFaceFlags,
        uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdSetStencilWriteMask)( VkCommandBuffer, VkStencilFaceFlags,
        uint32_t ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdSetViewport)( VkCommandBuffer, uint32_t, uint32_t,
        const VkViewport * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdUpdateBuffer)( VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize,
        const uint32_t * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdWaitEvents)( VkCommandBuffer, uint32_t, const VkEvent *, VkPipelineStageFlags,
        VkPipelineStageFlags, uint32_t, const VkMemoryBarrier *, uint32_t,
        const VkBufferMemoryBarrier *, uint32_t,
        const VkImageMemoryBarrier_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkCmdWriteTimestamp)( VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool,
        uint32_t ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateBuffer)( VkDevice, const VkBufferCreateInfo_host *,
        const VkAllocationCallbacks_host *, VkBuffer * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateBufferView)( VkDevice, const VkBufferViewCreateInfo_host *,
        const VkAllocationCallbacks_host *, VkBufferView * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateCommandPool)( VkDevice, const VkCommandPoolCreateInfo *,
        const VkAllocationCallbacks_host *, VkCommandPool * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateComputePipelines)( VkDevice, VkPipelineCache, uint32_t,
        const VkComputePipelineCreateInfo_host *, const VkAllocationCallbacks_host *,
        VkPipeline * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateDebugReportCallbackEXT)( VkInstance,
        const VkDebugReportCallbackCreateInfoEXT_host *, const VkAllocationCallbacks_host *,
        VkDebugReportCallbackEXT * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateDescriptorPool)( VkDevice, const VkDescriptorPoolCreateInfo *,
        const VkAllocationCallbacks_host *, VkDescriptorPool * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateDescriptorSetLayout)( VkDevice,
        const VkDescriptorSetLayoutCreateInfo *, const VkAllocationCallbacks_host *,
        VkDescriptorSetLayout * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateDevice)( VkPhysicalDevice, const VkDeviceCreateInfo *,
        const VkAllocationCallbacks_host *, VkDevice * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateDisplayModeKHR)( VkPhysicalDevice, VkDisplayKHR,
        const VkDisplayModeCreateInfoKHR *, const VkAllocationCallbacks_host *,
        VkDisplayModeKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateDisplayPlaneSurfaceKHR)( VkInstance,
        const VkDisplaySurfaceCreateInfoKHR_host *, const VkAllocationCallbacks_host *,
        VkSurfaceKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateEvent)( VkDevice, const VkEventCreateInfo *,
        const VkAllocationCallbacks_host *, VkEvent * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateFence)( VkDevice, const VkFenceCreateInfo *,
        const VkAllocationCallbacks_host *, VkFence * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateFramebuffer)( VkDevice, const VkFramebufferCreateInfo_host *,
        const VkAllocationCallbacks_host *, VkFramebuffer * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateGraphicsPipelines)( VkDevice, VkPipelineCache, uint32_t,
        const VkGraphicsPipelineCreateInfo_host *, const VkAllocationCallbacks_host *,
        VkPipeline * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateImage)( VkDevice, const VkImageCreateInfo *,
        const VkAllocationCallbacks_host *, VkImage * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateImageView)( VkDevice, const VkImageViewCreateInfo_host *,
        const VkAllocationCallbacks_host *, VkImageView * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateInstance)( const VkInstanceCreateInfo *,
        const VkAllocationCallbacks_host *, VkInstance * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreatePipelineCache)( VkDevice, const VkPipelineCacheCreateInfo *,
        const VkAllocationCallbacks_host *, VkPipelineCache * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreatePipelineLayout)( VkDevice, const VkPipelineLayoutCreateInfo *,
        const VkAllocationCallbacks_host *, VkPipelineLayout * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateQueryPool)( VkDevice, const VkQueryPoolCreateInfo *,
        const VkAllocationCallbacks_host *, VkQueryPool * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateRenderPass)( VkDevice, const VkRenderPassCreateInfo *,
        const VkAllocationCallbacks_host *, VkRenderPass * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateSampler)( VkDevice, const VkSamplerCreateInfo *,
        const VkAllocationCallbacks_host *, VkSampler * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateSemaphore)( VkDevice, const VkSemaphoreCreateInfo *,
        const VkAllocationCallbacks_host *, VkSemaphore * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateShaderModule)( VkDevice, const VkShaderModuleCreateInfo *,
        const VkAllocationCallbacks_host *, VkShaderModule * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateSharedSwapchainsKHR)( VkDevice, uint32_t,
        const VkSwapchainCreateInfoKHR_host *, const VkAllocationCallbacks_host *,
        VkSwapchainKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateSwapchainKHR)( VkDevice, const VkSwapchainCreateInfoKHR_host *,
        const VkAllocationCallbacks_host *, VkSwapchainKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateXcbSurfaceKHR)( VkInstance, const VkXcbSurfaceCreateInfoKHR_host *,
        const VkAllocationCallbacks_host *, VkSurfaceKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkCreateXlibSurfaceKHR)( VkInstance, const VkXlibSurfaceCreateInfoKHR_host *,
        const VkAllocationCallbacks_host *, VkSurfaceKHR * ) DECLSPEC_HIDDEN;
extern void (*p_vkDebugReportMessageEXT)( VkInstance, VkDebugReportFlagsEXT,
        VkDebugReportObjectTypeEXT, uint64_t, size_t, int32_t, const char *,
        const char * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyBuffer)( VkDevice, VkBuffer,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyBufferView)( VkDevice, VkBufferView,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyCommandPool)( VkDevice, VkCommandPool,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyDebugReportCallbackEXT)( VkInstance, VkDebugReportCallbackEXT,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyDescriptorPool)( VkDevice, VkDescriptorPool,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyDescriptorSetLayout)( VkDevice, VkDescriptorSetLayout,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyDevice)( VkDevice, const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyEvent)( VkDevice, VkEvent,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyFence)( VkDevice, VkFence,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyFramebuffer)( VkDevice, VkFramebuffer,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyImage)( VkDevice, VkImage,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyImageView)( VkDevice, VkImageView,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyInstance)( VkInstance,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyPipeline)( VkDevice, VkPipeline,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyPipelineCache)( VkDevice, VkPipelineCache,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyPipelineLayout)( VkDevice, VkPipelineLayout,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyQueryPool)( VkDevice, VkQueryPool,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyRenderPass)( VkDevice, VkRenderPass,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroySampler)( VkDevice, VkSampler,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroySemaphore)( VkDevice, VkSemaphore,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroyShaderModule)( VkDevice, VkShaderModule,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroySurfaceKHR)( VkInstance, VkSurfaceKHR,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkDestroySwapchainKHR)( VkDevice, VkSwapchainKHR,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkDeviceWaitIdle)( VkDevice ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkEndCommandBuffer)( VkCommandBuffer ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkEnumerateDeviceExtensionProperties)( VkPhysicalDevice, const char *,
        uint32_t *, VkExtensionProperties * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkEnumerateDeviceLayerProperties)( VkPhysicalDevice, uint32_t *,
        VkLayerProperties * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkEnumerateInstanceExtensionProperties)( const char *, uint32_t *,
        VkExtensionProperties * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkEnumerateInstanceLayerProperties)( uint32_t *,
        VkLayerProperties * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkEnumeratePhysicalDevices)( VkInstance, uint32_t *,
        VkPhysicalDevice * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkFlushMappedMemoryRanges)( VkDevice, uint32_t,
        const VkMappedMemoryRange * ) DECLSPEC_HIDDEN;
extern void (*p_vkFreeCommandBuffers)( VkDevice, VkCommandPool, uint32_t,
        const VkCommandBuffer * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkFreeDescriptorSets)( VkDevice, VkDescriptorPool, uint32_t,
        const VkDescriptorSet * ) DECLSPEC_HIDDEN;
extern void (*p_vkFreeMemory)( VkDevice, VkDeviceMemory,
        const VkAllocationCallbacks_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetBufferMemoryRequirements)( VkDevice, VkBuffer,
        VkMemoryRequirements * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetDeviceMemoryCommitment)( VkDevice, VkDeviceMemory,
        VkDeviceSize * ) DECLSPEC_HIDDEN;
extern PFN_vkVoidFunction_host (*p_vkGetDeviceProcAddr)( VkDevice, const char * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetDeviceQueue)( VkDevice, uint32_t, uint32_t, VkQueue * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetDisplayModePropertiesKHR)( VkPhysicalDevice, VkDisplayKHR, uint32_t *,
        VkDisplayModePropertiesKHR_host * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetDisplayPlaneCapabilitiesKHR)( VkPhysicalDevice, VkDisplayModeKHR,
        uint32_t, VkDisplayPlaneCapabilitiesKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetDisplayPlaneSupportedDisplaysKHR)( VkPhysicalDevice, uint32_t, uint32_t *,
        VkDisplayKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetEventStatus)( VkDevice, VkEvent ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetFenceStatus)( VkDevice, VkFence ) DECLSPEC_HIDDEN;
extern void (*p_vkGetImageMemoryRequirements)( VkDevice, VkImage,
        VkMemoryRequirements * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetImageSparseMemoryRequirements)( VkDevice, VkImage, uint32_t *,
        VkSparseImageMemoryRequirements * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetImageSubresourceLayout)( VkDevice, VkImage, const VkImageSubresource *,
        VkSubresourceLayout * ) DECLSPEC_HIDDEN;
extern PFN_vkVoidFunction_host (*p_vkGetInstanceProcAddr)( VkInstance,
        const char * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetPhysicalDeviceDisplayPlanePropertiesKHR)( VkPhysicalDevice, uint32_t *,
        VkDisplayPlanePropertiesKHR_host * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetPhysicalDeviceDisplayPropertiesKHR)( VkPhysicalDevice, uint32_t *,
        VkDisplayPropertiesKHR * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetPhysicalDeviceFeatures)( VkPhysicalDevice,
        VkPhysicalDeviceFeatures * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetPhysicalDeviceFormatProperties)( VkPhysicalDevice, VkFormat,
        VkFormatProperties * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetPhysicalDeviceImageFormatProperties)( VkPhysicalDevice, VkFormat,
        VkImageType, VkImageTiling, VkImageUsageFlags, VkImageCreateFlags,
        VkImageFormatProperties * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetPhysicalDeviceMemoryProperties)( VkPhysicalDevice,
        VkPhysicalDeviceMemoryProperties_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetPhysicalDeviceProperties)( VkPhysicalDevice,
        VkPhysicalDeviceProperties_host * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetPhysicalDeviceQueueFamilyProperties)( VkPhysicalDevice, uint32_t *,
        VkQueueFamilyProperties * ) DECLSPEC_HIDDEN;
extern void (*p_vkGetPhysicalDeviceSparseImageFormatProperties)( VkPhysicalDevice, VkFormat,
        VkImageType, VkSampleCountFlagBits, VkImageUsageFlags, VkImageTiling, uint32_t *,
        VkSparseImageFormatProperties * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)( VkPhysicalDevice, VkSurfaceKHR,
        VkSurfaceCapabilitiesKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetPhysicalDeviceSurfaceFormatsKHR)( VkPhysicalDevice, VkSurfaceKHR,
        uint32_t *, VkSurfaceFormatKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetPhysicalDeviceSurfacePresentModesKHR)( VkPhysicalDevice, VkSurfaceKHR,
        uint32_t *, VkPresentModeKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetPhysicalDeviceSurfaceSupportKHR)( VkPhysicalDevice, uint32_t,
        VkSurfaceKHR, VkBool32 * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetPipelineCacheData)( VkDevice, VkPipelineCache, size_t *,
        void * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetQueryPoolResults)( VkDevice, VkQueryPool, uint32_t, uint32_t, size_t,
        void *, VkDeviceSize, VkQueryResultFlags ) DECLSPEC_HIDDEN;
extern void (*p_vkGetRenderAreaGranularity)( VkDevice, VkRenderPass, VkExtent2D * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkGetSwapchainImagesKHR)( VkDevice, VkSwapchainKHR, uint32_t *,
        VkImage * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkInvalidateMappedMemoryRanges)( VkDevice, uint32_t,
        const VkMappedMemoryRange * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkMapMemory)( VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize,
        VkMemoryMapFlags, void ** ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkMergePipelineCaches)( VkDevice, VkPipelineCache, uint32_t,
        const VkPipelineCache * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkQueueBindSparse)( VkQueue, uint32_t, const VkBindSparseInfo_host *,
        VkFence ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkQueuePresentKHR)( VkQueue, const VkPresentInfoKHR * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkQueueSubmit)( VkQueue, uint32_t, const VkSubmitInfo *,
        VkFence ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkQueueWaitIdle)( VkQueue ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkResetCommandBuffer)( VkCommandBuffer,
        VkCommandBufferResetFlags ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkResetCommandPool)( VkDevice, VkCommandPool,
        VkCommandPoolResetFlags ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkResetDescriptorPool)( VkDevice, VkDescriptorPool,
        VkDescriptorPoolResetFlags ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkResetEvent)( VkDevice, VkEvent ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkResetFences)( VkDevice, uint32_t, const VkFence * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkSetEvent)( VkDevice, VkEvent ) DECLSPEC_HIDDEN;
extern void (*p_vkUnmapMemory)( VkDevice, VkDeviceMemory ) DECLSPEC_HIDDEN;
extern void (*p_vkUpdateDescriptorSets)( VkDevice, uint32_t, const VkWriteDescriptorSet_host *,
        uint32_t, const VkCopyDescriptorSet_host * ) DECLSPEC_HIDDEN;
extern VkResult (*p_vkWaitForFences)( VkDevice, uint32_t, const VkFence *, VkBool32,
        uint64_t ) DECLSPEC_HIDDEN;

extern void convert_PFN_vkAllocationFunction( PFN_vkAllocationFunction_host *out,
        const PFN_vkAllocationFunction *in ) DECLSPEC_HIDDEN;
extern void release_PFN_vkAllocationFunction( PFN_vkAllocationFunction *out,
        PFN_vkAllocationFunction_host *in ) DECLSPEC_HIDDEN;

extern void convert_PFN_vkReallocationFunction( PFN_vkReallocationFunction_host *out,
        const PFN_vkReallocationFunction *in ) DECLSPEC_HIDDEN;
extern void release_PFN_vkReallocationFunction( PFN_vkReallocationFunction *out,
        PFN_vkReallocationFunction_host *in ) DECLSPEC_HIDDEN;

extern void convert_PFN_vkFreeFunction( PFN_vkFreeFunction_host *out,
        const PFN_vkFreeFunction *in ) DECLSPEC_HIDDEN;
extern void release_PFN_vkFreeFunction( PFN_vkFreeFunction *out,
        PFN_vkFreeFunction_host *in ) DECLSPEC_HIDDEN;

extern void convert_PFN_vkInternalAllocationNotification(
        PFN_vkInternalAllocationNotification_host *out,
        const PFN_vkInternalAllocationNotification *in ) DECLSPEC_HIDDEN;
extern void release_PFN_vkInternalAllocationNotification(
        PFN_vkInternalAllocationNotification *out,
        PFN_vkInternalAllocationNotification_host *in ) DECLSPEC_HIDDEN;

extern void convert_PFN_vkInternalFreeNotification( PFN_vkInternalFreeNotification_host *out,
        const PFN_vkInternalFreeNotification *in ) DECLSPEC_HIDDEN;
extern void release_PFN_vkInternalFreeNotification( PFN_vkInternalFreeNotification *out,
        PFN_vkInternalFreeNotification_host *in ) DECLSPEC_HIDDEN;

extern VkAllocationCallbacks_host *convert_VkAllocationCallbacks( VkAllocationCallbacks_host *out,
        const VkAllocationCallbacks *in ) DECLSPEC_HIDDEN;
extern void release_VkAllocationCallbacks( VkAllocationCallbacks *out,
        VkAllocationCallbacks_host *in ) DECLSPEC_HIDDEN;
extern VkAllocationCallbacks_host *convert_VkAllocationCallbacks_array(
        const VkAllocationCallbacks *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkAllocationCallbacks_array( VkAllocationCallbacks *out,
        VkAllocationCallbacks_host *in, int count ) DECLSPEC_HIDDEN;

#if defined(USE_STRUCT_CONVERSION)
extern VkCommandBufferInheritanceInfo_host *convert_VkCommandBufferInheritanceInfo(
        VkCommandBufferInheritanceInfo_host *out,
        const VkCommandBufferInheritanceInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkCommandBufferInheritanceInfo( VkCommandBufferInheritanceInfo *out,
        VkCommandBufferInheritanceInfo_host *in ) DECLSPEC_HIDDEN;
extern VkCommandBufferInheritanceInfo_host *convert_VkCommandBufferInheritanceInfo_array(
        const VkCommandBufferInheritanceInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkCommandBufferInheritanceInfo_array( VkCommandBufferInheritanceInfo *out,
        VkCommandBufferInheritanceInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkCommandBufferInheritanceInfo_host *convert_VkCommandBufferInheritanceInfo(
        VkCommandBufferInheritanceInfo_host *out, const VkCommandBufferInheritanceInfo *in )
{
    return (VkCommandBufferInheritanceInfo_host *)in;
}
static inline void release_VkCommandBufferInheritanceInfo( VkCommandBufferInheritanceInfo *out,
        VkCommandBufferInheritanceInfo_host *in )
{
}
static inline VkCommandBufferInheritanceInfo_host *convert_VkCommandBufferInheritanceInfo_array(
        const VkCommandBufferInheritanceInfo *in, int count )
{
    return (VkCommandBufferInheritanceInfo_host *)in;
}
static inline void release_VkCommandBufferInheritanceInfo_array(
        VkCommandBufferInheritanceInfo *out, VkCommandBufferInheritanceInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkCommandBufferBeginInfo_host *convert_VkCommandBufferBeginInfo(
        VkCommandBufferBeginInfo_host *out, const VkCommandBufferBeginInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkCommandBufferBeginInfo( VkCommandBufferBeginInfo *out,
        VkCommandBufferBeginInfo_host *in ) DECLSPEC_HIDDEN;
extern VkCommandBufferBeginInfo_host *convert_VkCommandBufferBeginInfo_array(
        const VkCommandBufferBeginInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkCommandBufferBeginInfo_array( VkCommandBufferBeginInfo *out,
        VkCommandBufferBeginInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkCommandBufferBeginInfo_host *convert_VkCommandBufferBeginInfo(
        VkCommandBufferBeginInfo_host *out, const VkCommandBufferBeginInfo *in )
{
    return (VkCommandBufferBeginInfo_host *)in;
}
static inline void release_VkCommandBufferBeginInfo( VkCommandBufferBeginInfo *out,
        VkCommandBufferBeginInfo_host *in )
{
}
static inline VkCommandBufferBeginInfo_host *convert_VkCommandBufferBeginInfo_array(
        const VkCommandBufferBeginInfo *in, int count )
{
    return (VkCommandBufferBeginInfo_host *)in;
}
static inline void release_VkCommandBufferBeginInfo_array( VkCommandBufferBeginInfo *out,
        VkCommandBufferBeginInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkImageMemoryBarrier_host *convert_VkImageMemoryBarrier( VkImageMemoryBarrier_host *out,
        const VkImageMemoryBarrier *in ) DECLSPEC_HIDDEN;
extern void release_VkImageMemoryBarrier( VkImageMemoryBarrier *out,
        VkImageMemoryBarrier_host *in ) DECLSPEC_HIDDEN;
extern VkImageMemoryBarrier_host *convert_VkImageMemoryBarrier_array(
        const VkImageMemoryBarrier *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkImageMemoryBarrier_array( VkImageMemoryBarrier *out,
        VkImageMemoryBarrier_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkImageMemoryBarrier_host *convert_VkImageMemoryBarrier(
        VkImageMemoryBarrier_host *out, const VkImageMemoryBarrier *in )
{
    return (VkImageMemoryBarrier_host *)in;
}
static inline void release_VkImageMemoryBarrier( VkImageMemoryBarrier *out,
        VkImageMemoryBarrier_host *in )
{
}
static inline VkImageMemoryBarrier_host *convert_VkImageMemoryBarrier_array(
        const VkImageMemoryBarrier *in, int count )
{
    return (VkImageMemoryBarrier_host *)in;
}
static inline void release_VkImageMemoryBarrier_array( VkImageMemoryBarrier *out,
        VkImageMemoryBarrier_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkBufferCreateInfo_host *convert_VkBufferCreateInfo( VkBufferCreateInfo_host *out,
        const VkBufferCreateInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkBufferCreateInfo( VkBufferCreateInfo *out,
        VkBufferCreateInfo_host *in ) DECLSPEC_HIDDEN;
extern VkBufferCreateInfo_host *convert_VkBufferCreateInfo_array( const VkBufferCreateInfo *in,
        int count ) DECLSPEC_HIDDEN;
extern void release_VkBufferCreateInfo_array( VkBufferCreateInfo *out, VkBufferCreateInfo_host *in,
        int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkBufferCreateInfo_host *convert_VkBufferCreateInfo( VkBufferCreateInfo_host *out,
        const VkBufferCreateInfo *in )
{
    return (VkBufferCreateInfo_host *)in;
}
static inline void release_VkBufferCreateInfo( VkBufferCreateInfo *out,
        VkBufferCreateInfo_host *in )
{
}
static inline VkBufferCreateInfo_host *convert_VkBufferCreateInfo_array(
        const VkBufferCreateInfo *in, int count )
{
    return (VkBufferCreateInfo_host *)in;
}
static inline void release_VkBufferCreateInfo_array( VkBufferCreateInfo *out,
        VkBufferCreateInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkBufferViewCreateInfo_host *convert_VkBufferViewCreateInfo(
        VkBufferViewCreateInfo_host *out, const VkBufferViewCreateInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkBufferViewCreateInfo( VkBufferViewCreateInfo *out,
        VkBufferViewCreateInfo_host *in ) DECLSPEC_HIDDEN;
extern VkBufferViewCreateInfo_host *convert_VkBufferViewCreateInfo_array(
        const VkBufferViewCreateInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkBufferViewCreateInfo_array( VkBufferViewCreateInfo *out,
        VkBufferViewCreateInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkBufferViewCreateInfo_host *convert_VkBufferViewCreateInfo(
        VkBufferViewCreateInfo_host *out, const VkBufferViewCreateInfo *in )
{
    return (VkBufferViewCreateInfo_host *)in;
}
static inline void release_VkBufferViewCreateInfo( VkBufferViewCreateInfo *out,
        VkBufferViewCreateInfo_host *in )
{
}
static inline VkBufferViewCreateInfo_host *convert_VkBufferViewCreateInfo_array(
        const VkBufferViewCreateInfo *in, int count )
{
    return (VkBufferViewCreateInfo_host *)in;
}
static inline void release_VkBufferViewCreateInfo_array( VkBufferViewCreateInfo *out,
        VkBufferViewCreateInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkPipelineShaderStageCreateInfo_host *convert_VkPipelineShaderStageCreateInfo(
        VkPipelineShaderStageCreateInfo_host *out,
        const VkPipelineShaderStageCreateInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkPipelineShaderStageCreateInfo( VkPipelineShaderStageCreateInfo *out,
        VkPipelineShaderStageCreateInfo_host *in ) DECLSPEC_HIDDEN;
extern VkPipelineShaderStageCreateInfo_host *convert_VkPipelineShaderStageCreateInfo_array(
        const VkPipelineShaderStageCreateInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkPipelineShaderStageCreateInfo_array( VkPipelineShaderStageCreateInfo *out,
        VkPipelineShaderStageCreateInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkPipelineShaderStageCreateInfo_host *convert_VkPipelineShaderStageCreateInfo(
        VkPipelineShaderStageCreateInfo_host *out, const VkPipelineShaderStageCreateInfo *in )
{
    return (VkPipelineShaderStageCreateInfo_host *)in;
}
static inline void release_VkPipelineShaderStageCreateInfo( VkPipelineShaderStageCreateInfo *out,
        VkPipelineShaderStageCreateInfo_host *in )
{
}
static inline VkPipelineShaderStageCreateInfo_host *convert_VkPipelineShaderStageCreateInfo_array(
        const VkPipelineShaderStageCreateInfo *in, int count )
{
    return (VkPipelineShaderStageCreateInfo_host *)in;
}
static inline void release_VkPipelineShaderStageCreateInfo_array(
        VkPipelineShaderStageCreateInfo *out, VkPipelineShaderStageCreateInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkComputePipelineCreateInfo_host *convert_VkComputePipelineCreateInfo(
        VkComputePipelineCreateInfo_host *out,
        const VkComputePipelineCreateInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkComputePipelineCreateInfo( VkComputePipelineCreateInfo *out,
        VkComputePipelineCreateInfo_host *in ) DECLSPEC_HIDDEN;
extern VkComputePipelineCreateInfo_host *convert_VkComputePipelineCreateInfo_array(
        const VkComputePipelineCreateInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkComputePipelineCreateInfo_array( VkComputePipelineCreateInfo *out,
        VkComputePipelineCreateInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkComputePipelineCreateInfo_host *convert_VkComputePipelineCreateInfo(
        VkComputePipelineCreateInfo_host *out, const VkComputePipelineCreateInfo *in )
{
    return (VkComputePipelineCreateInfo_host *)in;
}
static inline void release_VkComputePipelineCreateInfo( VkComputePipelineCreateInfo *out,
        VkComputePipelineCreateInfo_host *in )
{
}
static inline VkComputePipelineCreateInfo_host *convert_VkComputePipelineCreateInfo_array(
        const VkComputePipelineCreateInfo *in, int count )
{
    return (VkComputePipelineCreateInfo_host *)in;
}
static inline void release_VkComputePipelineCreateInfo_array( VkComputePipelineCreateInfo *out,
        VkComputePipelineCreateInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

extern void convert_PFN_vkDebugReportCallbackEXT( PFN_vkDebugReportCallbackEXT_host *out,
        const PFN_vkDebugReportCallbackEXT *in ) DECLSPEC_HIDDEN;
extern void release_PFN_vkDebugReportCallbackEXT( PFN_vkDebugReportCallbackEXT *out,
        PFN_vkDebugReportCallbackEXT_host *in ) DECLSPEC_HIDDEN;

extern VkDebugReportCallbackCreateInfoEXT_host *convert_VkDebugReportCallbackCreateInfoEXT(
        VkDebugReportCallbackCreateInfoEXT_host *out,
        const VkDebugReportCallbackCreateInfoEXT *in ) DECLSPEC_HIDDEN;
extern void release_VkDebugReportCallbackCreateInfoEXT( VkDebugReportCallbackCreateInfoEXT *out,
        VkDebugReportCallbackCreateInfoEXT_host *in ) DECLSPEC_HIDDEN;
extern VkDebugReportCallbackCreateInfoEXT_host *convert_VkDebugReportCallbackCreateInfoEXT_array(
        const VkDebugReportCallbackCreateInfoEXT *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkDebugReportCallbackCreateInfoEXT_array(
        VkDebugReportCallbackCreateInfoEXT *out, VkDebugReportCallbackCreateInfoEXT_host *in,
        int count ) DECLSPEC_HIDDEN;

#if defined(USE_STRUCT_CONVERSION)
extern VkDisplaySurfaceCreateInfoKHR_host *convert_VkDisplaySurfaceCreateInfoKHR(
        VkDisplaySurfaceCreateInfoKHR_host *out,
        const VkDisplaySurfaceCreateInfoKHR *in ) DECLSPEC_HIDDEN;
extern void release_VkDisplaySurfaceCreateInfoKHR( VkDisplaySurfaceCreateInfoKHR *out,
        VkDisplaySurfaceCreateInfoKHR_host *in ) DECLSPEC_HIDDEN;
extern VkDisplaySurfaceCreateInfoKHR_host *convert_VkDisplaySurfaceCreateInfoKHR_array(
        const VkDisplaySurfaceCreateInfoKHR *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkDisplaySurfaceCreateInfoKHR_array( VkDisplaySurfaceCreateInfoKHR *out,
        VkDisplaySurfaceCreateInfoKHR_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkDisplaySurfaceCreateInfoKHR_host *convert_VkDisplaySurfaceCreateInfoKHR(
        VkDisplaySurfaceCreateInfoKHR_host *out, const VkDisplaySurfaceCreateInfoKHR *in )
{
    return (VkDisplaySurfaceCreateInfoKHR_host *)in;
}
static inline void release_VkDisplaySurfaceCreateInfoKHR( VkDisplaySurfaceCreateInfoKHR *out,
        VkDisplaySurfaceCreateInfoKHR_host *in )
{
}
static inline VkDisplaySurfaceCreateInfoKHR_host *convert_VkDisplaySurfaceCreateInfoKHR_array(
        const VkDisplaySurfaceCreateInfoKHR *in, int count )
{
    return (VkDisplaySurfaceCreateInfoKHR_host *)in;
}
static inline void release_VkDisplaySurfaceCreateInfoKHR_array( VkDisplaySurfaceCreateInfoKHR *out,
        VkDisplaySurfaceCreateInfoKHR_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkFramebufferCreateInfo_host *convert_VkFramebufferCreateInfo(
        VkFramebufferCreateInfo_host *out, const VkFramebufferCreateInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkFramebufferCreateInfo( VkFramebufferCreateInfo *out,
        VkFramebufferCreateInfo_host *in ) DECLSPEC_HIDDEN;
extern VkFramebufferCreateInfo_host *convert_VkFramebufferCreateInfo_array(
        const VkFramebufferCreateInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkFramebufferCreateInfo_array( VkFramebufferCreateInfo *out,
        VkFramebufferCreateInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkFramebufferCreateInfo_host *convert_VkFramebufferCreateInfo(
        VkFramebufferCreateInfo_host *out, const VkFramebufferCreateInfo *in )
{
    return (VkFramebufferCreateInfo_host *)in;
}
static inline void release_VkFramebufferCreateInfo( VkFramebufferCreateInfo *out,
        VkFramebufferCreateInfo_host *in )
{
}
static inline VkFramebufferCreateInfo_host *convert_VkFramebufferCreateInfo_array(
        const VkFramebufferCreateInfo *in, int count )
{
    return (VkFramebufferCreateInfo_host *)in;
}
static inline void release_VkFramebufferCreateInfo_array( VkFramebufferCreateInfo *out,
        VkFramebufferCreateInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkGraphicsPipelineCreateInfo_host *convert_VkGraphicsPipelineCreateInfo(
        VkGraphicsPipelineCreateInfo_host *out,
        const VkGraphicsPipelineCreateInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkGraphicsPipelineCreateInfo( VkGraphicsPipelineCreateInfo *out,
        VkGraphicsPipelineCreateInfo_host *in ) DECLSPEC_HIDDEN;
extern VkGraphicsPipelineCreateInfo_host *convert_VkGraphicsPipelineCreateInfo_array(
        const VkGraphicsPipelineCreateInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkGraphicsPipelineCreateInfo_array( VkGraphicsPipelineCreateInfo *out,
        VkGraphicsPipelineCreateInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkGraphicsPipelineCreateInfo_host *convert_VkGraphicsPipelineCreateInfo(
        VkGraphicsPipelineCreateInfo_host *out, const VkGraphicsPipelineCreateInfo *in )
{
    return (VkGraphicsPipelineCreateInfo_host *)in;
}
static inline void release_VkGraphicsPipelineCreateInfo( VkGraphicsPipelineCreateInfo *out,
        VkGraphicsPipelineCreateInfo_host *in )
{
}
static inline VkGraphicsPipelineCreateInfo_host *convert_VkGraphicsPipelineCreateInfo_array(
        const VkGraphicsPipelineCreateInfo *in, int count )
{
    return (VkGraphicsPipelineCreateInfo_host *)in;
}
static inline void release_VkGraphicsPipelineCreateInfo_array( VkGraphicsPipelineCreateInfo *out,
        VkGraphicsPipelineCreateInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkImageViewCreateInfo_host *convert_VkImageViewCreateInfo( VkImageViewCreateInfo_host *out,
        const VkImageViewCreateInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkImageViewCreateInfo( VkImageViewCreateInfo *out,
        VkImageViewCreateInfo_host *in ) DECLSPEC_HIDDEN;
extern VkImageViewCreateInfo_host *convert_VkImageViewCreateInfo_array(
        const VkImageViewCreateInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkImageViewCreateInfo_array( VkImageViewCreateInfo *out,
        VkImageViewCreateInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkImageViewCreateInfo_host *convert_VkImageViewCreateInfo(
        VkImageViewCreateInfo_host *out, const VkImageViewCreateInfo *in )
{
    return (VkImageViewCreateInfo_host *)in;
}
static inline void release_VkImageViewCreateInfo( VkImageViewCreateInfo *out,
        VkImageViewCreateInfo_host *in )
{
}
static inline VkImageViewCreateInfo_host *convert_VkImageViewCreateInfo_array(
        const VkImageViewCreateInfo *in, int count )
{
    return (VkImageViewCreateInfo_host *)in;
}
static inline void release_VkImageViewCreateInfo_array( VkImageViewCreateInfo *out,
        VkImageViewCreateInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkSwapchainCreateInfoKHR_host *convert_VkSwapchainCreateInfoKHR(
        VkSwapchainCreateInfoKHR_host *out, const VkSwapchainCreateInfoKHR *in ) DECLSPEC_HIDDEN;
extern void release_VkSwapchainCreateInfoKHR( VkSwapchainCreateInfoKHR *out,
        VkSwapchainCreateInfoKHR_host *in ) DECLSPEC_HIDDEN;
extern VkSwapchainCreateInfoKHR_host *convert_VkSwapchainCreateInfoKHR_array(
        const VkSwapchainCreateInfoKHR *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkSwapchainCreateInfoKHR_array( VkSwapchainCreateInfoKHR *out,
        VkSwapchainCreateInfoKHR_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkSwapchainCreateInfoKHR_host *convert_VkSwapchainCreateInfoKHR(
        VkSwapchainCreateInfoKHR_host *out, const VkSwapchainCreateInfoKHR *in )
{
    return (VkSwapchainCreateInfoKHR_host *)in;
}
static inline void release_VkSwapchainCreateInfoKHR( VkSwapchainCreateInfoKHR *out,
        VkSwapchainCreateInfoKHR_host *in )
{
}
static inline VkSwapchainCreateInfoKHR_host *convert_VkSwapchainCreateInfoKHR_array(
        const VkSwapchainCreateInfoKHR *in, int count )
{
    return (VkSwapchainCreateInfoKHR_host *)in;
}
static inline void release_VkSwapchainCreateInfoKHR_array( VkSwapchainCreateInfoKHR *out,
        VkSwapchainCreateInfoKHR_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkDisplayModePropertiesKHR_host *convert_VkDisplayModePropertiesKHR(
        VkDisplayModePropertiesKHR_host *out,
        const VkDisplayModePropertiesKHR *in ) DECLSPEC_HIDDEN;
extern void release_VkDisplayModePropertiesKHR( VkDisplayModePropertiesKHR *out,
        VkDisplayModePropertiesKHR_host *in ) DECLSPEC_HIDDEN;
extern VkDisplayModePropertiesKHR_host *convert_VkDisplayModePropertiesKHR_array(
        const VkDisplayModePropertiesKHR *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkDisplayModePropertiesKHR_array( VkDisplayModePropertiesKHR *out,
        VkDisplayModePropertiesKHR_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkDisplayModePropertiesKHR_host *convert_VkDisplayModePropertiesKHR(
        VkDisplayModePropertiesKHR_host *out, const VkDisplayModePropertiesKHR *in )
{
    return (VkDisplayModePropertiesKHR_host *)in;
}
static inline void release_VkDisplayModePropertiesKHR( VkDisplayModePropertiesKHR *out,
        VkDisplayModePropertiesKHR_host *in )
{
}
static inline VkDisplayModePropertiesKHR_host *convert_VkDisplayModePropertiesKHR_array(
        const VkDisplayModePropertiesKHR *in, int count )
{
    return (VkDisplayModePropertiesKHR_host *)in;
}
static inline void release_VkDisplayModePropertiesKHR_array( VkDisplayModePropertiesKHR *out,
        VkDisplayModePropertiesKHR_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkDisplayPlanePropertiesKHR_host *convert_VkDisplayPlanePropertiesKHR(
        VkDisplayPlanePropertiesKHR_host *out,
        const VkDisplayPlanePropertiesKHR *in ) DECLSPEC_HIDDEN;
extern void release_VkDisplayPlanePropertiesKHR( VkDisplayPlanePropertiesKHR *out,
        VkDisplayPlanePropertiesKHR_host *in ) DECLSPEC_HIDDEN;
extern VkDisplayPlanePropertiesKHR_host *convert_VkDisplayPlanePropertiesKHR_array(
        const VkDisplayPlanePropertiesKHR *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkDisplayPlanePropertiesKHR_array( VkDisplayPlanePropertiesKHR *out,
        VkDisplayPlanePropertiesKHR_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkDisplayPlanePropertiesKHR_host *convert_VkDisplayPlanePropertiesKHR(
        VkDisplayPlanePropertiesKHR_host *out, const VkDisplayPlanePropertiesKHR *in )
{
    return (VkDisplayPlanePropertiesKHR_host *)in;
}
static inline void release_VkDisplayPlanePropertiesKHR( VkDisplayPlanePropertiesKHR *out,
        VkDisplayPlanePropertiesKHR_host *in )
{
}
static inline VkDisplayPlanePropertiesKHR_host *convert_VkDisplayPlanePropertiesKHR_array(
        const VkDisplayPlanePropertiesKHR *in, int count )
{
    return (VkDisplayPlanePropertiesKHR_host *)in;
}
static inline void release_VkDisplayPlanePropertiesKHR_array( VkDisplayPlanePropertiesKHR *out,
        VkDisplayPlanePropertiesKHR_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkMemoryHeap_host *convert_VkMemoryHeap( VkMemoryHeap_host *out,
        const VkMemoryHeap *in ) DECLSPEC_HIDDEN;
extern void release_VkMemoryHeap( VkMemoryHeap *out, VkMemoryHeap_host *in ) DECLSPEC_HIDDEN;
extern VkMemoryHeap_host *convert_VkMemoryHeap_array( const VkMemoryHeap *in,
        int count ) DECLSPEC_HIDDEN;
extern void release_VkMemoryHeap_array( VkMemoryHeap *out, VkMemoryHeap_host *in,
        int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkMemoryHeap_host *convert_VkMemoryHeap( VkMemoryHeap_host *out,
        const VkMemoryHeap *in )
{
    return (VkMemoryHeap_host *)in;
}
static inline void release_VkMemoryHeap( VkMemoryHeap *out, VkMemoryHeap_host *in )
{
}
static inline VkMemoryHeap_host *convert_VkMemoryHeap_array( const VkMemoryHeap *in, int count )
{
    return (VkMemoryHeap_host *)in;
}
static inline void release_VkMemoryHeap_array( VkMemoryHeap *out, VkMemoryHeap_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkPhysicalDeviceMemoryProperties_host *convert_VkPhysicalDeviceMemoryProperties(
        VkPhysicalDeviceMemoryProperties_host *out,
        const VkPhysicalDeviceMemoryProperties *in ) DECLSPEC_HIDDEN;
extern void release_VkPhysicalDeviceMemoryProperties( VkPhysicalDeviceMemoryProperties *out,
        VkPhysicalDeviceMemoryProperties_host *in ) DECLSPEC_HIDDEN;
extern VkPhysicalDeviceMemoryProperties_host *convert_VkPhysicalDeviceMemoryProperties_array(
        const VkPhysicalDeviceMemoryProperties *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkPhysicalDeviceMemoryProperties_array( VkPhysicalDeviceMemoryProperties *out,
        VkPhysicalDeviceMemoryProperties_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkPhysicalDeviceMemoryProperties_host *convert_VkPhysicalDeviceMemoryProperties(
        VkPhysicalDeviceMemoryProperties_host *out, const VkPhysicalDeviceMemoryProperties *in )
{
    return (VkPhysicalDeviceMemoryProperties_host *)in;
}
static inline void release_VkPhysicalDeviceMemoryProperties( VkPhysicalDeviceMemoryProperties *out,
        VkPhysicalDeviceMemoryProperties_host *in )
{
}
static inline VkPhysicalDeviceMemoryProperties_host *convert_VkPhysicalDeviceMemoryProperties_array(
        const VkPhysicalDeviceMemoryProperties *in, int count )
{
    return (VkPhysicalDeviceMemoryProperties_host *)in;
}
static inline void release_VkPhysicalDeviceMemoryProperties_array(
        VkPhysicalDeviceMemoryProperties *out, VkPhysicalDeviceMemoryProperties_host *in,
        int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkPhysicalDeviceLimits_host *convert_VkPhysicalDeviceLimits(
        VkPhysicalDeviceLimits_host *out, const VkPhysicalDeviceLimits *in ) DECLSPEC_HIDDEN;
extern void release_VkPhysicalDeviceLimits( VkPhysicalDeviceLimits *out,
        VkPhysicalDeviceLimits_host *in ) DECLSPEC_HIDDEN;
extern VkPhysicalDeviceLimits_host *convert_VkPhysicalDeviceLimits_array(
        const VkPhysicalDeviceLimits *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkPhysicalDeviceLimits_array( VkPhysicalDeviceLimits *out,
        VkPhysicalDeviceLimits_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkPhysicalDeviceLimits_host *convert_VkPhysicalDeviceLimits(
        VkPhysicalDeviceLimits_host *out, const VkPhysicalDeviceLimits *in )
{
    return (VkPhysicalDeviceLimits_host *)in;
}
static inline void release_VkPhysicalDeviceLimits( VkPhysicalDeviceLimits *out,
        VkPhysicalDeviceLimits_host *in )
{
}
static inline VkPhysicalDeviceLimits_host *convert_VkPhysicalDeviceLimits_array(
        const VkPhysicalDeviceLimits *in, int count )
{
    return (VkPhysicalDeviceLimits_host *)in;
}
static inline void release_VkPhysicalDeviceLimits_array( VkPhysicalDeviceLimits *out,
        VkPhysicalDeviceLimits_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkPhysicalDeviceProperties_host *convert_VkPhysicalDeviceProperties(
        VkPhysicalDeviceProperties_host *out,
        const VkPhysicalDeviceProperties *in ) DECLSPEC_HIDDEN;
extern void release_VkPhysicalDeviceProperties( VkPhysicalDeviceProperties *out,
        VkPhysicalDeviceProperties_host *in ) DECLSPEC_HIDDEN;
extern VkPhysicalDeviceProperties_host *convert_VkPhysicalDeviceProperties_array(
        const VkPhysicalDeviceProperties *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkPhysicalDeviceProperties_array( VkPhysicalDeviceProperties *out,
        VkPhysicalDeviceProperties_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkPhysicalDeviceProperties_host *convert_VkPhysicalDeviceProperties(
        VkPhysicalDeviceProperties_host *out, const VkPhysicalDeviceProperties *in )
{
    return (VkPhysicalDeviceProperties_host *)in;
}
static inline void release_VkPhysicalDeviceProperties( VkPhysicalDeviceProperties *out,
        VkPhysicalDeviceProperties_host *in )
{
}
static inline VkPhysicalDeviceProperties_host *convert_VkPhysicalDeviceProperties_array(
        const VkPhysicalDeviceProperties *in, int count )
{
    return (VkPhysicalDeviceProperties_host *)in;
}
static inline void release_VkPhysicalDeviceProperties_array( VkPhysicalDeviceProperties *out,
        VkPhysicalDeviceProperties_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkSparseMemoryBind_host *convert_VkSparseMemoryBind( VkSparseMemoryBind_host *out,
        const VkSparseMemoryBind *in ) DECLSPEC_HIDDEN;
extern void release_VkSparseMemoryBind( VkSparseMemoryBind *out,
        VkSparseMemoryBind_host *in ) DECLSPEC_HIDDEN;
extern VkSparseMemoryBind_host *convert_VkSparseMemoryBind_array( const VkSparseMemoryBind *in,
        int count ) DECLSPEC_HIDDEN;
extern void release_VkSparseMemoryBind_array( VkSparseMemoryBind *out, VkSparseMemoryBind_host *in,
        int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkSparseMemoryBind_host *convert_VkSparseMemoryBind( VkSparseMemoryBind_host *out,
        const VkSparseMemoryBind *in )
{
    return (VkSparseMemoryBind_host *)in;
}
static inline void release_VkSparseMemoryBind( VkSparseMemoryBind *out,
        VkSparseMemoryBind_host *in )
{
}
static inline VkSparseMemoryBind_host *convert_VkSparseMemoryBind_array(
        const VkSparseMemoryBind *in, int count )
{
    return (VkSparseMemoryBind_host *)in;
}
static inline void release_VkSparseMemoryBind_array( VkSparseMemoryBind *out,
        VkSparseMemoryBind_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkSparseBufferMemoryBindInfo_host *convert_VkSparseBufferMemoryBindInfo(
        VkSparseBufferMemoryBindInfo_host *out,
        const VkSparseBufferMemoryBindInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkSparseBufferMemoryBindInfo( VkSparseBufferMemoryBindInfo *out,
        VkSparseBufferMemoryBindInfo_host *in ) DECLSPEC_HIDDEN;
extern VkSparseBufferMemoryBindInfo_host *convert_VkSparseBufferMemoryBindInfo_array(
        const VkSparseBufferMemoryBindInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkSparseBufferMemoryBindInfo_array( VkSparseBufferMemoryBindInfo *out,
        VkSparseBufferMemoryBindInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkSparseBufferMemoryBindInfo_host *convert_VkSparseBufferMemoryBindInfo(
        VkSparseBufferMemoryBindInfo_host *out, const VkSparseBufferMemoryBindInfo *in )
{
    return (VkSparseBufferMemoryBindInfo_host *)in;
}
static inline void release_VkSparseBufferMemoryBindInfo( VkSparseBufferMemoryBindInfo *out,
        VkSparseBufferMemoryBindInfo_host *in )
{
}
static inline VkSparseBufferMemoryBindInfo_host *convert_VkSparseBufferMemoryBindInfo_array(
        const VkSparseBufferMemoryBindInfo *in, int count )
{
    return (VkSparseBufferMemoryBindInfo_host *)in;
}
static inline void release_VkSparseBufferMemoryBindInfo_array( VkSparseBufferMemoryBindInfo *out,
        VkSparseBufferMemoryBindInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkSparseImageOpaqueMemoryBindInfo_host *convert_VkSparseImageOpaqueMemoryBindInfo(
        VkSparseImageOpaqueMemoryBindInfo_host *out,
        const VkSparseImageOpaqueMemoryBindInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkSparseImageOpaqueMemoryBindInfo( VkSparseImageOpaqueMemoryBindInfo *out,
        VkSparseImageOpaqueMemoryBindInfo_host *in ) DECLSPEC_HIDDEN;
extern VkSparseImageOpaqueMemoryBindInfo_host *convert_VkSparseImageOpaqueMemoryBindInfo_array(
        const VkSparseImageOpaqueMemoryBindInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkSparseImageOpaqueMemoryBindInfo_array(
        VkSparseImageOpaqueMemoryBindInfo *out, VkSparseImageOpaqueMemoryBindInfo_host *in,
        int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkSparseImageOpaqueMemoryBindInfo_host *convert_VkSparseImageOpaqueMemoryBindInfo(
        VkSparseImageOpaqueMemoryBindInfo_host *out, const VkSparseImageOpaqueMemoryBindInfo *in )
{
    return (VkSparseImageOpaqueMemoryBindInfo_host *)in;
}
static inline void release_VkSparseImageOpaqueMemoryBindInfo(
        VkSparseImageOpaqueMemoryBindInfo *out, VkSparseImageOpaqueMemoryBindInfo_host *in )
{
}
static inline VkSparseImageOpaqueMemoryBindInfo_host *convert_VkSparseImageOpaqueMemoryBindInfo_array(
        const VkSparseImageOpaqueMemoryBindInfo *in, int count )
{
    return (VkSparseImageOpaqueMemoryBindInfo_host *)in;
}
static inline void release_VkSparseImageOpaqueMemoryBindInfo_array(
        VkSparseImageOpaqueMemoryBindInfo *out, VkSparseImageOpaqueMemoryBindInfo_host *in,
        int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkSparseImageMemoryBind_host *convert_VkSparseImageMemoryBind(
        VkSparseImageMemoryBind_host *out, const VkSparseImageMemoryBind *in ) DECLSPEC_HIDDEN;
extern void release_VkSparseImageMemoryBind( VkSparseImageMemoryBind *out,
        VkSparseImageMemoryBind_host *in ) DECLSPEC_HIDDEN;
extern VkSparseImageMemoryBind_host *convert_VkSparseImageMemoryBind_array(
        const VkSparseImageMemoryBind *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkSparseImageMemoryBind_array( VkSparseImageMemoryBind *out,
        VkSparseImageMemoryBind_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkSparseImageMemoryBind_host *convert_VkSparseImageMemoryBind(
        VkSparseImageMemoryBind_host *out, const VkSparseImageMemoryBind *in )
{
    return (VkSparseImageMemoryBind_host *)in;
}
static inline void release_VkSparseImageMemoryBind( VkSparseImageMemoryBind *out,
        VkSparseImageMemoryBind_host *in )
{
}
static inline VkSparseImageMemoryBind_host *convert_VkSparseImageMemoryBind_array(
        const VkSparseImageMemoryBind *in, int count )
{
    return (VkSparseImageMemoryBind_host *)in;
}
static inline void release_VkSparseImageMemoryBind_array( VkSparseImageMemoryBind *out,
        VkSparseImageMemoryBind_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkSparseImageMemoryBindInfo_host *convert_VkSparseImageMemoryBindInfo(
        VkSparseImageMemoryBindInfo_host *out,
        const VkSparseImageMemoryBindInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkSparseImageMemoryBindInfo( VkSparseImageMemoryBindInfo *out,
        VkSparseImageMemoryBindInfo_host *in ) DECLSPEC_HIDDEN;
extern VkSparseImageMemoryBindInfo_host *convert_VkSparseImageMemoryBindInfo_array(
        const VkSparseImageMemoryBindInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkSparseImageMemoryBindInfo_array( VkSparseImageMemoryBindInfo *out,
        VkSparseImageMemoryBindInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkSparseImageMemoryBindInfo_host *convert_VkSparseImageMemoryBindInfo(
        VkSparseImageMemoryBindInfo_host *out, const VkSparseImageMemoryBindInfo *in )
{
    return (VkSparseImageMemoryBindInfo_host *)in;
}
static inline void release_VkSparseImageMemoryBindInfo( VkSparseImageMemoryBindInfo *out,
        VkSparseImageMemoryBindInfo_host *in )
{
}
static inline VkSparseImageMemoryBindInfo_host *convert_VkSparseImageMemoryBindInfo_array(
        const VkSparseImageMemoryBindInfo *in, int count )
{
    return (VkSparseImageMemoryBindInfo_host *)in;
}
static inline void release_VkSparseImageMemoryBindInfo_array( VkSparseImageMemoryBindInfo *out,
        VkSparseImageMemoryBindInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkBindSparseInfo_host *convert_VkBindSparseInfo( VkBindSparseInfo_host *out,
        const VkBindSparseInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkBindSparseInfo( VkBindSparseInfo *out,
        VkBindSparseInfo_host *in ) DECLSPEC_HIDDEN;
extern VkBindSparseInfo_host *convert_VkBindSparseInfo_array( const VkBindSparseInfo *in,
        int count ) DECLSPEC_HIDDEN;
extern void release_VkBindSparseInfo_array( VkBindSparseInfo *out, VkBindSparseInfo_host *in,
        int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkBindSparseInfo_host *convert_VkBindSparseInfo( VkBindSparseInfo_host *out,
        const VkBindSparseInfo *in )
{
    return (VkBindSparseInfo_host *)in;
}
static inline void release_VkBindSparseInfo( VkBindSparseInfo *out, VkBindSparseInfo_host *in )
{
}
static inline VkBindSparseInfo_host *convert_VkBindSparseInfo_array( const VkBindSparseInfo *in,
        int count )
{
    return (VkBindSparseInfo_host *)in;
}
static inline void release_VkBindSparseInfo_array( VkBindSparseInfo *out,
        VkBindSparseInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkDescriptorImageInfo_host *convert_VkDescriptorImageInfo( VkDescriptorImageInfo_host *out,
        const VkDescriptorImageInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkDescriptorImageInfo( VkDescriptorImageInfo *out,
        VkDescriptorImageInfo_host *in ) DECLSPEC_HIDDEN;
extern VkDescriptorImageInfo_host *convert_VkDescriptorImageInfo_array(
        const VkDescriptorImageInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkDescriptorImageInfo_array( VkDescriptorImageInfo *out,
        VkDescriptorImageInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkDescriptorImageInfo_host *convert_VkDescriptorImageInfo(
        VkDescriptorImageInfo_host *out, const VkDescriptorImageInfo *in )
{
    return (VkDescriptorImageInfo_host *)in;
}
static inline void release_VkDescriptorImageInfo( VkDescriptorImageInfo *out,
        VkDescriptorImageInfo_host *in )
{
}
static inline VkDescriptorImageInfo_host *convert_VkDescriptorImageInfo_array(
        const VkDescriptorImageInfo *in, int count )
{
    return (VkDescriptorImageInfo_host *)in;
}
static inline void release_VkDescriptorImageInfo_array( VkDescriptorImageInfo *out,
        VkDescriptorImageInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkDescriptorBufferInfo_host *convert_VkDescriptorBufferInfo(
        VkDescriptorBufferInfo_host *out, const VkDescriptorBufferInfo *in ) DECLSPEC_HIDDEN;
extern void release_VkDescriptorBufferInfo( VkDescriptorBufferInfo *out,
        VkDescriptorBufferInfo_host *in ) DECLSPEC_HIDDEN;
extern VkDescriptorBufferInfo_host *convert_VkDescriptorBufferInfo_array(
        const VkDescriptorBufferInfo *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkDescriptorBufferInfo_array( VkDescriptorBufferInfo *out,
        VkDescriptorBufferInfo_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkDescriptorBufferInfo_host *convert_VkDescriptorBufferInfo(
        VkDescriptorBufferInfo_host *out, const VkDescriptorBufferInfo *in )
{
    return (VkDescriptorBufferInfo_host *)in;
}
static inline void release_VkDescriptorBufferInfo( VkDescriptorBufferInfo *out,
        VkDescriptorBufferInfo_host *in )
{
}
static inline VkDescriptorBufferInfo_host *convert_VkDescriptorBufferInfo_array(
        const VkDescriptorBufferInfo *in, int count )
{
    return (VkDescriptorBufferInfo_host *)in;
}
static inline void release_VkDescriptorBufferInfo_array( VkDescriptorBufferInfo *out,
        VkDescriptorBufferInfo_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkWriteDescriptorSet_host *convert_VkWriteDescriptorSet( VkWriteDescriptorSet_host *out,
        const VkWriteDescriptorSet *in ) DECLSPEC_HIDDEN;
extern void release_VkWriteDescriptorSet( VkWriteDescriptorSet *out,
        VkWriteDescriptorSet_host *in ) DECLSPEC_HIDDEN;
extern VkWriteDescriptorSet_host *convert_VkWriteDescriptorSet_array(
        const VkWriteDescriptorSet *in, int count ) DECLSPEC_HIDDEN;
extern void release_VkWriteDescriptorSet_array( VkWriteDescriptorSet *out,
        VkWriteDescriptorSet_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkWriteDescriptorSet_host *convert_VkWriteDescriptorSet(
        VkWriteDescriptorSet_host *out, const VkWriteDescriptorSet *in )
{
    return (VkWriteDescriptorSet_host *)in;
}
static inline void release_VkWriteDescriptorSet( VkWriteDescriptorSet *out,
        VkWriteDescriptorSet_host *in )
{
}
static inline VkWriteDescriptorSet_host *convert_VkWriteDescriptorSet_array(
        const VkWriteDescriptorSet *in, int count )
{
    return (VkWriteDescriptorSet_host *)in;
}
static inline void release_VkWriteDescriptorSet_array( VkWriteDescriptorSet *out,
        VkWriteDescriptorSet_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */

#if defined(USE_STRUCT_CONVERSION)
extern VkCopyDescriptorSet_host *convert_VkCopyDescriptorSet( VkCopyDescriptorSet_host *out,
        const VkCopyDescriptorSet *in ) DECLSPEC_HIDDEN;
extern void release_VkCopyDescriptorSet( VkCopyDescriptorSet *out,
        VkCopyDescriptorSet_host *in ) DECLSPEC_HIDDEN;
extern VkCopyDescriptorSet_host *convert_VkCopyDescriptorSet_array( const VkCopyDescriptorSet *in,
        int count ) DECLSPEC_HIDDEN;
extern void release_VkCopyDescriptorSet_array( VkCopyDescriptorSet *out,
        VkCopyDescriptorSet_host *in, int count ) DECLSPEC_HIDDEN;
#else  /* defined(USE_STRUCT_CONVERSION) */
static inline VkCopyDescriptorSet_host *convert_VkCopyDescriptorSet( VkCopyDescriptorSet_host *out,
        const VkCopyDescriptorSet *in )
{
    return (VkCopyDescriptorSet_host *)in;
}
static inline void release_VkCopyDescriptorSet( VkCopyDescriptorSet *out,
        VkCopyDescriptorSet_host *in )
{
}
static inline VkCopyDescriptorSet_host *convert_VkCopyDescriptorSet_array(
        const VkCopyDescriptorSet *in, int count )
{
    return (VkCopyDescriptorSet_host *)in;
}
static inline void release_VkCopyDescriptorSet_array( VkCopyDescriptorSet *out,
        VkCopyDescriptorSet_host *in, int count )
{
}
#endif /* defined(USE_STRUCT_CONVERSION) */


extern BOOL init_vulkan( void ) DECLSPEC_HIDDEN;
extern BOOL is_null_func( const char *name ); DECLSPEC_HIDDEN
extern void free_vulkan( void ) DECLSPEC_HIDDEN;

#endif /* __VULKAN_PRIVATE_H */
