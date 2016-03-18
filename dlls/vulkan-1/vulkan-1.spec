@ stdcall vkAcquireNextImageKHR(ptr int64 int64 int64 int64 ptr) vulkan.vkAcquireNextImageKHR
@ stdcall vkAllocateCommandBuffers(ptr ptr ptr) vulkan.vkAllocateCommandBuffers
@ stdcall vkAllocateDescriptorSets(ptr ptr ptr) vulkan.vkAllocateDescriptorSets
@ stdcall vkAllocateMemory(ptr ptr ptr ptr) vulkan.vkAllocateMemory
@ stdcall vkBeginCommandBuffer(ptr ptr) vulkan.vkBeginCommandBuffer
@ stdcall vkBindBufferMemory(ptr int64 int64 int64) vulkan.vkBindBufferMemory
@ stdcall vkBindImageMemory(ptr int64 int64 int64) vulkan.vkBindImageMemory
@ stdcall vkCmdBeginQuery(ptr int64 long long) vulkan.vkCmdBeginQuery
@ stdcall vkCmdBeginRenderPass(ptr ptr long) vulkan.vkCmdBeginRenderPass
@ stdcall vkCmdBindDescriptorSets(ptr long int64 long long ptr long ptr) vulkan.vkCmdBindDescriptorSets
@ stdcall vkCmdBindIndexBuffer(ptr int64 int64 long) vulkan.vkCmdBindIndexBuffer
@ stdcall vkCmdBindPipeline(ptr long int64) vulkan.vkCmdBindPipeline
@ stdcall vkCmdBindVertexBuffers(ptr long long ptr ptr) vulkan.vkCmdBindVertexBuffers
@ stdcall vkCmdBlitImage(ptr int64 long int64 long long ptr long) vulkan.vkCmdBlitImage
@ stdcall vkCmdClearAttachments(ptr long ptr long ptr) vulkan.vkCmdClearAttachments
@ stdcall vkCmdClearColorImage(ptr int64 long ptr long ptr) vulkan.vkCmdClearColorImage
@ stdcall vkCmdClearDepthStencilImage(ptr int64 long ptr long ptr) vulkan.vkCmdClearDepthStencilImage
@ stdcall vkCmdCopyBuffer(ptr int64 int64 long ptr) vulkan.vkCmdCopyBuffer
@ stdcall vkCmdCopyBufferToImage(ptr int64 int64 long long ptr) vulkan.vkCmdCopyBufferToImage
@ stdcall vkCmdCopyImage(ptr int64 long int64 long long ptr) vulkan.vkCmdCopyImage
@ stdcall vkCmdCopyImageToBuffer(ptr int64 long int64 long ptr) vulkan.vkCmdCopyImageToBuffer
@ stdcall vkCmdCopyQueryPoolResults(ptr int64 long long int64 int64 int64 long) vulkan.vkCmdCopyQueryPoolResults
@ stdcall vkCmdDispatch(ptr long long long) vulkan.vkCmdDispatch
@ stdcall vkCmdDispatchIndirect(ptr int64 int64) vulkan.vkCmdDispatchIndirect
@ stdcall vkCmdDraw(ptr long long long long) vulkan.vkCmdDraw
@ stdcall vkCmdDrawIndexed(ptr long long long long long) vulkan.vkCmdDrawIndexed
@ stdcall vkCmdDrawIndexedIndirect(ptr int64 int64 long long) vulkan.vkCmdDrawIndexedIndirect
@ stdcall vkCmdDrawIndirect(ptr int64 int64 long long) vulkan.vkCmdDrawIndirect
@ stdcall vkCmdEndQuery(ptr int64 long) vulkan.vkCmdEndQuery
@ stdcall vkCmdEndRenderPass(ptr) vulkan.vkCmdEndRenderPass
@ stdcall vkCmdExecuteCommands(ptr long ptr) vulkan.vkCmdExecuteCommands
@ stdcall vkCmdFillBuffer(ptr int64 int64 int64 long) vulkan.vkCmdFillBuffer
@ stdcall vkCmdNextSubpass(ptr long) vulkan.vkCmdNextSubpass
@ stdcall vkCmdPipelineBarrier(ptr long long long long ptr long ptr long ptr) vulkan.vkCmdPipelineBarrier
@ stdcall vkCmdPushConstants(ptr int64 long long long ptr) vulkan.vkCmdPushConstants
@ stdcall vkCmdResetEvent(ptr int64 long) vulkan.vkCmdResetEvent
@ stdcall vkCmdResetQueryPool(ptr int64 long long) vulkan.vkCmdResetQueryPool
@ stdcall vkCmdResolveImage(ptr int64 long int64 long long ptr) vulkan.vkCmdResolveImage
@ stdcall vkCmdSetBlendConstants(ptr ptr) vulkan.vkCmdSetBlendConstants
@ stdcall vkCmdSetDepthBias(ptr float float float) vulkan.vkCmdSetDepthBias
@ stdcall vkCmdSetDepthBounds(ptr float float) vulkan.vkCmdSetDepthBounds
@ stdcall vkCmdSetEvent(ptr int64 long) vulkan.vkCmdSetEvent
@ stdcall vkCmdSetLineWidth(ptr float) vulkan.vkCmdSetLineWidth
@ stdcall vkCmdSetScissor(ptr long long ptr) vulkan.vkCmdSetScissor
@ stdcall vkCmdSetStencilCompareMask(ptr long long) vulkan.vkCmdSetStencilCompareMask
@ stdcall vkCmdSetStencilReference(ptr long long) vulkan.vkCmdSetStencilReference
@ stdcall vkCmdSetStencilWriteMask(ptr long long) vulkan.vkCmdSetStencilWriteMask
@ stdcall vkCmdSetViewport(ptr long long ptr) vulkan.vkCmdSetViewport
@ stdcall vkCmdUpdateBuffer(ptr int64 int64 int64 ptr) vulkan.vkCmdUpdateBuffer
@ stdcall vkCmdWaitEvents(ptr long ptr long long long ptr long ptr long ptr) vulkan.vkCmdWaitEvents
@ stdcall vkCmdWriteTimestamp(ptr long int64 long) vulkan.vkCmdWriteTimestamp
@ stdcall vkCreateBuffer(ptr ptr ptr ptr) vulkan.vkCreateBuffer
@ stdcall vkCreateBufferView(ptr ptr ptr ptr) vulkan.vkCreateBufferView
@ stdcall vkCreateCommandPool(ptr ptr ptr ptr) vulkan.vkCreateCommandPool
@ stdcall vkCreateComputePipelines(ptr int64 long ptr ptr ptr) vulkan.vkCreateComputePipelines
@ stdcall vkCreateDebugReportCallbackEXT(ptr ptr ptr ptr) vulkan.vkCreateDebugReportCallbackEXT
@ stdcall vkCreateDescriptorPool(ptr ptr ptr ptr) vulkan.vkCreateDescriptorPool
@ stdcall vkCreateDescriptorSetLayout(ptr ptr ptr ptr) vulkan.vkCreateDescriptorSetLayout
@ stdcall vkCreateDevice(ptr ptr ptr ptr) vulkan.vkCreateDevice
@ stdcall vkCreateDisplayModeKHR(ptr int64 ptr ptr ptr) vulkan.vkCreateDisplayModeKHR
@ stdcall vkCreateDisplayPlaneSurfaceKHR(ptr ptr ptr ptr) vulkan.vkCreateDisplayPlaneSurfaceKHR
@ stdcall vkCreateEvent(ptr ptr ptr ptr) vulkan.vkCreateEvent
@ stdcall vkCreateFence(ptr ptr ptr ptr) vulkan.vkCreateFence
@ stdcall vkCreateFramebuffer(ptr ptr ptr ptr) vulkan.vkCreateFramebuffer
@ stdcall vkCreateGraphicsPipelines(ptr int64 long ptr ptr ptr) vulkan.vkCreateGraphicsPipelines
@ stdcall vkCreateImage(ptr ptr ptr ptr) vulkan.vkCreateImage
@ stdcall vkCreateImageView(ptr ptr ptr ptr) vulkan.vkCreateImageView
@ stdcall vkCreateInstance(ptr ptr ptr) vulkan.vkCreateInstance
@ stdcall vkCreatePipelineCache(ptr ptr ptr ptr) vulkan.vkCreatePipelineCache
@ stdcall vkCreatePipelineLayout(ptr ptr ptr ptr) vulkan.vkCreatePipelineLayout
@ stdcall vkCreateQueryPool(ptr ptr ptr ptr) vulkan.vkCreateQueryPool
@ stdcall vkCreateRenderPass(ptr ptr ptr ptr) vulkan.vkCreateRenderPass
@ stdcall vkCreateSampler(ptr ptr ptr ptr) vulkan.vkCreateSampler
@ stdcall vkCreateSemaphore(ptr ptr ptr ptr) vulkan.vkCreateSemaphore
@ stdcall vkCreateShaderModule(ptr ptr ptr ptr) vulkan.vkCreateShaderModule
@ stdcall vkCreateSharedSwapchainsKHR(ptr long ptr ptr ptr) vulkan.vkCreateSharedSwapchainsKHR
@ stdcall vkCreateSwapchainKHR(ptr ptr ptr ptr) vulkan.vkCreateSwapchainKHR
@ stdcall vkCreateWin32SurfaceKHR(ptr ptr ptr ptr) vulkan.vkCreateWin32SurfaceKHR
@ stdcall vkDebugReportMessageEXT(ptr long long int64 long long str str) vulkan.vkDebugReportMessageEXT
@ stdcall vkDestroyBuffer(ptr int64 ptr) vulkan.vkDestroyBuffer
@ stdcall vkDestroyBufferView(ptr int64 ptr) vulkan.vkDestroyBufferView
@ stdcall vkDestroyCommandPool(ptr int64 ptr) vulkan.vkDestroyCommandPool
@ stdcall vkDestroyDebugReportCallbackEXT(ptr int64 ptr) vulkan.vkDestroyDebugReportCallbackEXT
@ stdcall vkDestroyDescriptorPool(ptr int64 ptr) vulkan.vkDestroyDescriptorPool
@ stdcall vkDestroyDescriptorSetLayout(ptr int64 ptr) vulkan.vkDestroyDescriptorSetLayout
@ stdcall vkDestroyDevice(ptr ptr) vulkan.vkDestroyDevice
@ stdcall vkDestroyEvent(ptr int64 ptr) vulkan.vkDestroyEvent
@ stdcall vkDestroyFence(ptr int64 ptr) vulkan.vkDestroyFence
@ stdcall vkDestroyFramebuffer(ptr int64 ptr) vulkan.vkDestroyFramebuffer
@ stdcall vkDestroyImage(ptr int64 ptr) vulkan.vkDestroyImage
@ stdcall vkDestroyImageView(ptr int64 ptr) vulkan.vkDestroyImageView
@ stdcall vkDestroyInstance(ptr ptr) vulkan.vkDestroyInstance
@ stdcall vkDestroyPipeline(ptr int64 ptr) vulkan.vkDestroyPipeline
@ stdcall vkDestroyPipelineCache(ptr int64 ptr) vulkan.vkDestroyPipelineCache
@ stdcall vkDestroyPipelineLayout(ptr int64 ptr) vulkan.vkDestroyPipelineLayout
@ stdcall vkDestroyQueryPool(ptr int64 ptr) vulkan.vkDestroyQueryPool
@ stdcall vkDestroyRenderPass(ptr int64 ptr) vulkan.vkDestroyRenderPass
@ stdcall vkDestroySampler(ptr int64 ptr) vulkan.vkDestroySampler
@ stdcall vkDestroySemaphore(ptr int64 ptr) vulkan.vkDestroySemaphore
@ stdcall vkDestroyShaderModule(ptr int64 ptr) vulkan.vkDestroyShaderModule
@ stdcall vkDestroySurfaceKHR(ptr int64 ptr) vulkan.vkDestroySurfaceKHR
@ stdcall vkDestroySwapchainKHR(ptr int64 ptr) vulkan.vkDestroySwapchainKHR
@ stdcall vkDeviceWaitIdle(ptr) vulkan.vkDeviceWaitIdle
@ stdcall vkEndCommandBuffer(ptr) vulkan.vkEndCommandBuffer
@ stdcall vkEnumerateDeviceExtensionProperties(ptr str ptr ptr) vulkan.vkEnumerateDeviceExtensionProperties
@ stdcall vkEnumerateDeviceLayerProperties(ptr ptr ptr) vulkan.vkEnumerateDeviceLayerProperties
@ stdcall vkEnumerateInstanceExtensionProperties(str ptr ptr) vulkan.vkEnumerateInstanceExtensionProperties
@ stdcall vkEnumerateInstanceLayerProperties(ptr ptr) vulkan.vkEnumerateInstanceLayerProperties
@ stdcall vkEnumeratePhysicalDevices(ptr ptr ptr) vulkan.vkEnumeratePhysicalDevices
@ stdcall vkFlushMappedMemoryRanges(ptr long ptr) vulkan.vkFlushMappedMemoryRanges
@ stdcall vkFreeCommandBuffers(ptr int64 long ptr) vulkan.vkFreeCommandBuffers
@ stdcall vkFreeDescriptorSets(ptr int64 long ptr) vulkan.vkFreeDescriptorSets
@ stdcall vkFreeMemory(ptr int64 ptr) vulkan.vkFreeMemory
@ stdcall vkGetBufferMemoryRequirements(ptr int64 ptr) vulkan.vkGetBufferMemoryRequirements
@ stdcall vkGetDeviceMemoryCommitment(ptr int64 ptr) vulkan.vkGetDeviceMemoryCommitment
@ stdcall vkGetDeviceProcAddr(ptr str) vulkan.vkGetDeviceProcAddr
@ stdcall vkGetDeviceQueue(ptr long long ptr) vulkan.vkGetDeviceQueue
@ stdcall vkGetDisplayModePropertiesKHR(ptr int64 ptr ptr) vulkan.vkGetDisplayModePropertiesKHR
@ stdcall vkGetDisplayPlaneCapabilitiesKHR(ptr int64 long ptr) vulkan.vkGetDisplayPlaneCapabilitiesKHR
@ stdcall vkGetDisplayPlaneSupportedDisplaysKHR(ptr long ptr ptr) vulkan.vkGetDisplayPlaneSupportedDisplaysKHR
@ stdcall vkGetEventStatus(ptr int64) vulkan.vkGetEventStatus
@ stdcall vkGetFenceStatus(ptr int64) vulkan.vkGetFenceStatus
@ stdcall vkGetImageMemoryRequirements(ptr int64 ptr) vulkan.vkGetImageMemoryRequirements
@ stdcall vkGetImageSparseMemoryRequirements(ptr int64 ptr ptr) vulkan.vkGetImageSparseMemoryRequirements
@ stdcall vkGetImageSubresourceLayout(ptr int64 ptr ptr) vulkan.vkGetImageSubresourceLayout
@ stdcall vkGetInstanceProcAddr(ptr str) vulkan.vkGetInstanceProcAddr
@ stdcall vkGetPhysicalDeviceDisplayPlanePropertiesKHR(ptr ptr ptr) vulkan.vkGetPhysicalDeviceDisplayPlanePropertiesKHR
@ stdcall vkGetPhysicalDeviceDisplayPropertiesKHR(ptr ptr ptr) vulkan.vkGetPhysicalDeviceDisplayPropertiesKHR
@ stdcall vkGetPhysicalDeviceFeatures(ptr ptr) vulkan.vkGetPhysicalDeviceFeatures
@ stdcall vkGetPhysicalDeviceFormatProperties(ptr long ptr) vulkan.vkGetPhysicalDeviceFormatProperties
@ stdcall vkGetPhysicalDeviceImageFormatProperties(ptr long long long long long ptr) vulkan.vkGetPhysicalDeviceImageFormatProperties
@ stdcall vkGetPhysicalDeviceMemoryProperties(ptr ptr) vulkan.vkGetPhysicalDeviceMemoryProperties
@ stdcall vkGetPhysicalDeviceProperties(ptr ptr) vulkan.vkGetPhysicalDeviceProperties
@ stdcall vkGetPhysicalDeviceQueueFamilyProperties(ptr ptr ptr) vulkan.vkGetPhysicalDeviceQueueFamilyProperties
@ stdcall vkGetPhysicalDeviceSparseImageFormatProperties(ptr long long long long long ptr ptr) vulkan.vkGetPhysicalDeviceSparseImageFormatProperties
@ stdcall vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ptr int64 ptr) vulkan.vkGetPhysicalDeviceSurfaceCapabilitiesKHR
@ stdcall vkGetPhysicalDeviceSurfaceFormatsKHR(ptr int64 ptr ptr) vulkan.vkGetPhysicalDeviceSurfaceFormatsKHR
@ stdcall vkGetPhysicalDeviceSurfacePresentModesKHR(ptr int64 ptr ptr) vulkan.vkGetPhysicalDeviceSurfacePresentModesKHR
@ stdcall vkGetPhysicalDeviceSurfaceSupportKHR(ptr long int64 ptr) vulkan.vkGetPhysicalDeviceSurfaceSupportKHR
@ stdcall vkGetPhysicalDeviceWin32PresentationSupportKHR(ptr long) vulkan.vkGetPhysicalDeviceWin32PresentationSupportKHR
@ stdcall vkGetPipelineCacheData(ptr int64 ptr ptr) vulkan.vkGetPipelineCacheData
@ stdcall vkGetQueryPoolResults(ptr int64 long long long ptr int64 long) vulkan.vkGetQueryPoolResults
@ stdcall vkGetRenderAreaGranularity(ptr int64 ptr) vulkan.vkGetRenderAreaGranularity
@ stdcall vkGetSwapchainImagesKHR(ptr int64 ptr ptr) vulkan.vkGetSwapchainImagesKHR
@ stdcall vkInvalidateMappedMemoryRanges(ptr long ptr) vulkan.vkInvalidateMappedMemoryRanges
@ stdcall vkMapMemory(ptr int64 int64 int64 long ptr) vulkan.vkMapMemory
@ stdcall vkMergePipelineCaches(ptr int64 long ptr) vulkan.vkMergePipelineCaches
@ stdcall vkQueueBindSparse(ptr long ptr int64) vulkan.vkQueueBindSparse
@ stdcall vkQueuePresentKHR(ptr ptr) vulkan.vkQueuePresentKHR
@ stdcall vkQueueSubmit(ptr long ptr int64) vulkan.vkQueueSubmit
@ stdcall vkQueueWaitIdle(ptr) vulkan.vkQueueWaitIdle
@ stdcall vkResetCommandBuffer(ptr long) vulkan.vkResetCommandBuffer
@ stdcall vkResetCommandPool(ptr int64 long) vulkan.vkResetCommandPool
@ stdcall vkResetDescriptorPool(ptr int64 long) vulkan.vkResetDescriptorPool
@ stdcall vkResetEvent(ptr int64) vulkan.vkResetEvent
@ stdcall vkResetFences(ptr long ptr) vulkan.vkResetFences
@ stdcall vkSetEvent(ptr int64) vulkan.vkSetEvent
@ stdcall vkUnmapMemory(ptr int64) vulkan.vkUnmapMemory
@ stdcall vkUpdateDescriptorSets(ptr long ptr long ptr) vulkan.vkUpdateDescriptorSets
@ stdcall vkWaitForFences(ptr long ptr long int64) vulkan.vkWaitForFences
