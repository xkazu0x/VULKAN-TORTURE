#include <stdio.h>
#include <string>
#include <fstream>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#define VK_CHECK(x) if ((x) != VK_SUCCESS) { __debugbreak(); }
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

struct vulkan_queue {
	VkQueue handle;
	uint32_t family_index;
};

struct vulkan_context {
	VkAllocationCallbacks *allocator;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger;

	VkPhysicalDevice physical_device;
	VkDevice logical_device;

	uint32_t queue_count;
	vulkan_queue graphics_queue;
	vulkan_queue present_queue;

	VkSurfaceKHR surface;

	VkSurfaceFormatKHR swapchain_image_format;
	VkPresentModeKHR swapchain_present_mode;
	VkSwapchainKHR swapchain;
	VkImageView *swapchain_image_views;
	VkRenderPass render_pass;
	VkFramebuffer *framebuffers;

	VkCommandPool command_pool;
	VkCommandBuffer *command_buffers;

	VkShaderModule vertex_shader;
	VkShaderModule fragment_shader;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;

	VkSemaphore semaphore_image_available;
	VkSemaphore semaphore_rendering_done;
	VkFence fence;
};

struct window_info {
	uint32_t screen_width;
	uint32_t screen_height;
	const char *title;
};

struct window_state {
	HINSTANCE instance;
	HWND hwnd;
};

struct engine_state {
	bool running;
	bool debug;
};

static engine_state engine;
static window_state window;
static vulkan_context vkcontext;

LRESULT CALLBACK win32_process_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_types,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
	void *user_data);

std::vector<char> read_file(const std::string &filename);
VkShaderModule create_shader_module(vulkan_context *context, const std::vector<char> &shader_code);

int main() {
	// engine
	engine.running = true;
	engine.debug = true;

	// windows
	window_info info = {};
	info.screen_width = 1920 / 2;
	info.screen_height = 1080 / 2;
	info.title = "VULKAN TORTURE";

	window.instance = GetModuleHandleA(0);

	WNDCLASSA wc = {};
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = win32_process_message;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = window.instance;
	wc.hIcon = LoadIcon(window.instance, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = 0;
	wc.lpszMenuName = 0;
	wc.lpszClassName = "vulkan_torture_class";

	if (!RegisterClassA(&wc)) {
		printf("Failed to register window class\n");
		return -1;
	}

	int screen_width = info.screen_width;
	int screen_height = info.screen_height;

	int xpos = (GetSystemMetrics(SM_CXSCREEN) - screen_width) / 2;
	int ypos = (GetSystemMetrics(SM_CYSCREEN) - screen_height) / 2;

	int window_style = WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_VISIBLE;
	int window_ex_style = WS_EX_APPWINDOW;

	//window_style |= WS_MAXIMIZEBOX;
	window_style |= WS_MINIMIZEBOX;
	// window_style |= WS_THICKFRAME;

	RECT border_rect = { 0, 0, 0, 0 };
	AdjustWindowRectEx(&border_rect, window_style, 0, window_ex_style);

	xpos += border_rect.left;
	ypos += border_rect.top;

	screen_width += border_rect.right - border_rect.left;
	screen_height += border_rect.bottom - border_rect.top;

	HWND hwnd = CreateWindowExA(
		window_ex_style, wc.lpszClassName, info.title,
		window_style, xpos, ypos, screen_width, screen_height,
		0, 0, window.instance, 0);
	if (!hwnd) {
		printf("Failed to create window\n");
		return -1;
	} else {
		window.hwnd = hwnd;
	}

	// vulkan instance
	VkApplicationInfo application_info = {};
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pNext = nullptr;
	application_info.pApplicationName = "vulkan_torture_application";
	application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	application_info.pEngineName = "vulkan_torture_engine";
	application_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	application_info.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo instance_create_info = {};
	instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.pNext = nullptr;
	instance_create_info.flags = 0;
	instance_create_info.pApplicationInfo = &application_info;

	// instance layers
	uint32_t available_layer_count = 0;
	VK_CHECK(vkEnumerateInstanceLayerProperties(&available_layer_count, nullptr));

	VkLayerProperties *available_layers = new VkLayerProperties[available_layer_count];
	VK_CHECK(vkEnumerateInstanceLayerProperties(&available_layer_count, available_layers));

	printf("\n-#-Available Instance Layers: %i\n", available_layer_count);
	for (uint32_t i = 0; i < available_layer_count; ++i) {
		printf(" + %s: %s\n", available_layers[i].layerName, available_layers[i].description);
	}

	if (engine.debug) {
		const char *enabled_layers[] = { "VK_LAYER_KHRONOS_validation" };
		printf("-+-Required Instance Layers:\n");
		printf(" + %s\n", enabled_layers[0]);
		for (uint32_t i = 0; i < ARRAY_SIZE(enabled_layers); ++i) {
			bool found = false;
			for (uint32_t j = 0; j < available_layer_count; ++j) {
				if (strcmp(available_layers[j].layerName, enabled_layers[i]) == 0) {
					found = true;
					printf(" - Layer is supported: %s\n", enabled_layers[i]);
					break;
				}
			}
			if (!found) {
				printf(" - Layer is not supported: %s\n", enabled_layers[i]);
				return -1;
			}
		}
		instance_create_info.enabledLayerCount = ARRAY_SIZE(enabled_layers);
		instance_create_info.ppEnabledLayerNames = enabled_layers;
	} else {
		instance_create_info.enabledLayerCount = 0;
		instance_create_info.ppEnabledLayerNames = nullptr;
	}

	// instance extensions
	uint32_t available_extension_count = 0;
	VK_CHECK(vkEnumerateInstanceExtensionProperties(0, &available_extension_count, nullptr));

	VkExtensionProperties *available_extensions = new VkExtensionProperties[available_extension_count];
	VK_CHECK(vkEnumerateInstanceExtensionProperties(0, &available_extension_count, available_extensions));

	printf("\n-#-Available Instance Extensions: %i\n", available_extension_count);
	for (uint32_t i = 0; i < available_extension_count; ++i) {
		printf(" + %s\n", available_extensions[i].extensionName);
	}

	if (engine.debug) {
		const char *enabled_extensions[] = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
		};
		printf("-+-Required Instance Extensions:\n");
		for (uint32_t i = 0; i < ARRAY_SIZE(enabled_extensions); ++i) {
			printf(" + %s\n", enabled_extensions[i]);
		}
		for (uint32_t i = 0; i < ARRAY_SIZE(enabled_extensions); ++i) {
			bool found = false;
			for (uint32_t j = 0; j < available_extension_count; ++j) {
				if (strcmp(available_extensions[j].extensionName, enabled_extensions[i]) == 0) {
					found = true;
					printf(" - Extension is supported: %s\n", enabled_extensions[i]);
					break;
				}
			}
			if (!found) {
				printf(" - Extension is not supported: %s\n", enabled_extensions[i]);
				return -1;
			}
		}
		instance_create_info.enabledExtensionCount = ARRAY_SIZE(enabled_extensions);
		instance_create_info.ppEnabledExtensionNames = enabled_extensions;
	} else {
		const char *enabled_extensions[] = {
			VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
		};
		printf("-+-Required Instance Extensions:\n");
		for (uint32_t i = 0; i < ARRAY_SIZE(enabled_extensions); ++i) {
			printf(" + %s\n", enabled_extensions[i]);
		}

		for (uint32_t i = 0; i < ARRAY_SIZE(enabled_extensions); ++i) {
			bool found = false;
			for (uint32_t j = 0; j < available_extension_count; ++j) {
				if (strcmp(available_extensions[j].extensionName, enabled_extensions[i]) == 0) {
					found = true;
					printf(" - Extension is supported: %s\n", enabled_extensions[i]);
					break;
				}
			}
			if (!found) {
				printf(" - Extension is not supported: %s\n", enabled_extensions[i]);
				return -1;
			}
		}
		instance_create_info.enabledExtensionCount = ARRAY_SIZE(enabled_extensions);
		instance_create_info.ppEnabledExtensionNames = enabled_extensions;
	}

	VK_CHECK(vkCreateInstance(&instance_create_info, vkcontext.allocator, &vkcontext.instance));

	delete[] available_extensions;
	delete[] available_layers;

	// instance debug messenger
	if (engine.debug) {
		uint32_t message_severity =
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT; //|
		//VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
		//VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;

		uint32_t message_type =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;

		VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = {};
		debug_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debug_messenger_create_info.messageSeverity = message_severity;
		debug_messenger_create_info.messageType = message_type;
		debug_messenger_create_info.pfnUserCallback = vk_debug_callback;

		PFN_vkCreateDebugUtilsMessengerEXT function =
			(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vkcontext.instance, "vkCreateDebugUtilsMessengerEXT");
		VK_CHECK(function(
			vkcontext.instance,
			&debug_messenger_create_info,
			vkcontext.allocator,
			&vkcontext.debug_messenger));
	}

	// vulkan select physical device
	uint32_t physical_device_count = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(vkcontext.instance, &physical_device_count, nullptr));
	if (physical_device_count == 0) {
		printf("Failed to find devices which support vulkan\n");
		return -1;
	}

	VkPhysicalDevice *physical_devices = new VkPhysicalDevice[physical_device_count];
	VK_CHECK(vkEnumeratePhysicalDevices(vkcontext.instance, &physical_device_count, physical_devices));
	printf("\n-#-Physical devices found: %i\n", physical_device_count);

	bool is_suitable = false;
	for (uint32_t i = 0; i < physical_device_count; ++i) {
		VkPhysicalDeviceProperties physical_device_properties;
		vkGetPhysicalDeviceProperties(physical_devices[i], &physical_device_properties);

		// TODO: useless code
		VkPhysicalDeviceFeatures physical_device_features;
		vkGetPhysicalDeviceFeatures(physical_devices[i], &physical_device_features);

		// TODO: useless code
		VkPhysicalDeviceMemoryProperties physical_device_memory;
		vkGetPhysicalDeviceMemoryProperties(physical_devices[i], &physical_device_memory);

		if (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			vkcontext.physical_device = physical_devices[i];
			is_suitable = true;
			break;
		}
	}
	if (!is_suitable) {
		vkcontext.physical_device = physical_devices[0];
	}

	VkPhysicalDeviceProperties physical_device_properties;
	vkGetPhysicalDeviceProperties(vkcontext.physical_device, &physical_device_properties);
	printf("-+-Selected Device: %s\n", physical_device_properties.deviceName);
	printf(" + API Version: %d.%d.%d\n",
		   VK_VERSION_MAJOR(physical_device_properties.apiVersion),
		   VK_VERSION_MINOR(physical_device_properties.apiVersion),
		   VK_VERSION_PATCH(physical_device_properties.apiVersion));
	printf(" + Driver Version: %d.%d.%d\n",
		   VK_VERSION_MAJOR(physical_device_properties.driverVersion),
		   VK_VERSION_MINOR(physical_device_properties.driverVersion),
		   VK_VERSION_PATCH(physical_device_properties.driverVersion));
	printf(" + Vendor ID: %d\n", physical_device_properties.vendorID);
	printf(" + Driver ID: %d\n", physical_device_properties.deviceID);
	switch (physical_device_properties.deviceType) {
		default:
		case VK_PHYSICAL_DEVICE_TYPE_OTHER:
			printf(" + Device Type: Unknown\n");
			break;
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			printf(" + Device Type: Integrated\n");
			break;
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			printf(" + Device Type: Discrete\n");
			break;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
			printf(" + Device Type: Virtual\n");
			break;
		case VK_PHYSICAL_DEVICE_TYPE_CPU:
			printf(" + Device Type: CPU\n");
			break;
	}

	delete[] physical_devices;

	// vulkan logical device
	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(vkcontext.physical_device, &queue_family_count, nullptr);

	VkQueueFamilyProperties *queue_families = new VkQueueFamilyProperties[queue_family_count];
	vkGetPhysicalDeviceQueueFamilyProperties(vkcontext.physical_device, &queue_family_count, queue_families);

	printf("\n-#-Available Queue Families: %i\n", queue_family_count);
	for (uint32_t i = 0; i < queue_family_count; ++i) {
		printf("-+-Queue family #%i\n", i);
		printf(" + Graphics Queue-: %d\n", ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0));
		printf(" + Transfer Queue-: %d\n", ((queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0));
		printf(" + Compute Queue--: %d\n", ((queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0));
		printf(" + Queue Count----: %i\n", queue_families[i].queueCount);
	}

	// TODO: add more queues -------
	uint32_t queue_count = 0;
	uint32_t graphics_queue_index = 0;
	for (uint32_t i = 0; i < queue_family_count; ++i) {
		if (queue_families[i].queueCount > 0) {
			if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				graphics_queue_index = i;
				queue_count++;
				break;
			}
		}
	}
	delete[] queue_families;

	vkcontext.graphics_queue.family_index = graphics_queue_index;
	uint32_t *queue_family_indices = new uint32_t[queue_count];
	queue_family_indices[0] = vkcontext.graphics_queue.family_index;
	// -----------------------------

	VkDeviceQueueCreateInfo *device_queue_create_infos = new VkDeviceQueueCreateInfo[queue_count];
	for (uint32_t i = 0; i < queue_count; ++i) {
		VkDeviceQueueCreateInfo device_queue_create_info = {};
		device_queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		device_queue_create_info.pNext = nullptr;
		device_queue_create_info.flags = 0;
		device_queue_create_info.queueFamilyIndex = queue_family_indices[i];
		device_queue_create_info.queueCount = 1;
		float queue_priority[] = { 1.0f };
		device_queue_create_info.pQueuePriorities = queue_priority;
		device_queue_create_infos[i] = device_queue_create_info;
	}

	VkPhysicalDeviceFeatures physical_device_features = {};
	physical_device_features.samplerAnisotropy = VK_FALSE;

	VkDeviceCreateInfo device_create_info = {};
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_create_info.pNext = nullptr;
	device_create_info.flags = 0;
	device_create_info.queueCreateInfoCount = queue_count;
	device_create_info.pQueueCreateInfos = device_queue_create_infos;
	device_create_info.enabledLayerCount = 0;
	device_create_info.ppEnabledLayerNames = nullptr;

	const char *device_extension_name[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	device_create_info.enabledExtensionCount = ARRAY_SIZE(device_extension_name);
	device_create_info.ppEnabledExtensionNames = device_extension_name;

	device_create_info.pEnabledFeatures = &physical_device_features;

	VK_CHECK(vkCreateDevice(
		vkcontext.physical_device,
		&device_create_info,
		vkcontext.allocator,
		&vkcontext.logical_device));

	delete[] device_queue_create_infos;
	delete[] queue_family_indices;

	// aquire graphics queue
	vkGetDeviceQueue(
		vkcontext.logical_device,
		vkcontext.graphics_queue.family_index,
		0,
		&vkcontext.graphics_queue.handle);

	// vulkan win32 surface
	VkWin32SurfaceCreateInfoKHR surface_create_info = {};
	surface_create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surface_create_info.pNext = nullptr;
	surface_create_info.flags = 0;
	surface_create_info.hinstance = window.instance;
	surface_create_info.hwnd = window.hwnd;

	VkResult result = vkCreateWin32SurfaceKHR(
		vkcontext.instance,
		&surface_create_info,
		vkcontext.allocator,
		&vkcontext.surface);
	if (result != VK_SUCCESS) {
		printf("Failed to create vulkan surface\n");
		return -1;
	}

	// vulkan swapchain
	// TODO: call physical device functions before creating the logical device
	// surface support
	VkBool32 surface_support = false;
	VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(
		vkcontext.physical_device,
		vkcontext.graphics_queue.family_index,
		vkcontext.surface,
		&surface_support));
	if (!surface_support) {
		printf("Graphics queue do not support present");
		return -1;
	}

	// surface capabilities
	VkSurfaceCapabilitiesKHR surface_capabilities;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		vkcontext.physical_device,
		vkcontext.surface,
		&surface_capabilities));
	printf("\n-#-Surface Capabilities:\n");
	printf(" + Min image count: %i\n", surface_capabilities.minImageCount);
	printf(" + Max image count: %i\n", surface_capabilities.maxImageCount);

	// surface formats
	uint32_t surface_format_count = 0;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
		vkcontext.physical_device,
		vkcontext.surface,
		&surface_format_count,
		nullptr));
	VkSurfaceFormatKHR *surface_formats = new VkSurfaceFormatKHR[surface_format_count];
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
		vkcontext.physical_device,
		vkcontext.surface,
		&surface_format_count,
		surface_formats));
	printf("\n-#-Supported surface formats: %i\n", surface_format_count);
	for (uint32_t i = 0; i < surface_format_count; ++i) {
		printf(" + Format: %d\n", surface_formats[i].format);
	}

	bool found_surface_format = false;
	for (uint32_t i = 0; i < surface_format_count; ++i) {
		if (surface_formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
			surface_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			vkcontext.swapchain_image_format = surface_formats[i];
			found_surface_format = true;
			break;
		} else {
			printf("Failed to find required swapchain image format\n");
			return -1;
		}
	}
	if (!found_surface_format) {
		vkcontext.swapchain_image_format = surface_formats[0];
	}

	// surface present modes
	uint32_t surface_present_mode_count = 0;
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
		vkcontext.physical_device,
		vkcontext.surface,
		&surface_present_mode_count,
		nullptr));
	VkPresentModeKHR *surface_present_modes = new VkPresentModeKHR[surface_present_mode_count];
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
		vkcontext.physical_device,
		vkcontext.surface,
		&surface_present_mode_count,
		surface_present_modes));
	printf("\n-#-Supported surface present modes: %i\n", surface_present_mode_count);
	for (uint32_t i = 0; i < surface_present_mode_count; ++i) {
		printf(" + Present mode: %d\n", surface_present_modes[i]);
	}

	for (uint32_t i = 0; i < surface_present_mode_count; ++i) {
		if (surface_present_modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
			vkcontext.swapchain_present_mode = surface_present_modes[i];
			break;
		} else {
			printf("Failed to find required present mode");
		}
	}

	// swapchain create
	uint32_t image_count = surface_capabilities.minImageCount + 1;
	if (surface_capabilities.minImageCount > 0 &&
		image_count > surface_capabilities.maxImageCount) {
		image_count = surface_capabilities.maxImageCount;
	}

	VkExtent2D swapchain_extent = { info.screen_width, info.screen_height };

	VkSwapchainCreateInfoKHR swapchain_create_info = {};
	swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain_create_info.pNext = nullptr;
	swapchain_create_info.flags = 0;
	swapchain_create_info.surface = vkcontext.surface;
	swapchain_create_info.minImageCount = image_count;
	swapchain_create_info.imageFormat = vkcontext.swapchain_image_format.format;
	swapchain_create_info.imageColorSpace = vkcontext.swapchain_image_format.colorSpace;
	swapchain_create_info.imageExtent = swapchain_extent;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_create_info.queueFamilyIndexCount = 0;
	swapchain_create_info.pQueueFamilyIndices = nullptr;
	swapchain_create_info.preTransform = surface_capabilities.currentTransform;
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchain_create_info.presentMode = vkcontext.swapchain_present_mode;
	swapchain_create_info.clipped = VK_TRUE;
	swapchain_create_info.oldSwapchain = nullptr;

	VK_CHECK(vkCreateSwapchainKHR(
		vkcontext.logical_device,
		&swapchain_create_info,
		vkcontext.allocator,
		&vkcontext.swapchain));

	// swapchain images
	uint32_t swapchain_image_count = 0;
	VK_CHECK(vkGetSwapchainImagesKHR(
		vkcontext.logical_device,
		vkcontext.swapchain,
		&swapchain_image_count,
		nullptr));
	VkImage *swapchain_images = new VkImage[swapchain_image_count];
	VK_CHECK(vkGetSwapchainImagesKHR(
		vkcontext.logical_device,
		vkcontext.swapchain,
		&swapchain_image_count,
		swapchain_images));

	// swapchain image view
	vkcontext.swapchain_image_views = new VkImageView[swapchain_image_count];
	for (uint32_t i = 0; i < swapchain_image_count; ++i) {
		VkImageViewCreateInfo image_view_create_info = {};
		image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		image_view_create_info.pNext = nullptr;
		image_view_create_info.flags = 0;
		image_view_create_info.image = swapchain_images[i];
		image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		image_view_create_info.format = vkcontext.swapchain_image_format.format;
		image_view_create_info.components.r = VK_COMPONENT_SWIZZLE_R;
		image_view_create_info.components.g = VK_COMPONENT_SWIZZLE_G;
		image_view_create_info.components.b = VK_COMPONENT_SWIZZLE_B;
		image_view_create_info.components.a = VK_COMPONENT_SWIZZLE_A;
		image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_view_create_info.subresourceRange.baseMipLevel = 0;
		image_view_create_info.subresourceRange.levelCount = 1;
		image_view_create_info.subresourceRange.baseArrayLayer = 0;
		image_view_create_info.subresourceRange.layerCount = 1;
		VK_CHECK(vkCreateImageView(
			vkcontext.logical_device,
			&image_view_create_info,
			vkcontext.allocator,
			&vkcontext.swapchain_image_views[i]));
	}

	delete[] swapchain_images;
	delete[] surface_present_modes;
	delete[] surface_formats;

	// TODO: depth image

	// vulkan render pass
	// attachment description
	VkAttachmentDescription color_attachment_description = {};
	color_attachment_description.flags = 0;
	color_attachment_description.format = vkcontext.swapchain_image_format.format;
	color_attachment_description.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment_description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment_description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment_description.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	// attachment reference
	VkAttachmentReference color_attachment_reference = {};
	color_attachment_reference.attachment = 0;
	color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// subpass
	VkSubpassDescription subpass_description = {};
	subpass_description.flags = 0;
	subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass_description.inputAttachmentCount = 0;
	subpass_description.pInputAttachments = nullptr;
	subpass_description.colorAttachmentCount = 1;
	subpass_description.pColorAttachments = &color_attachment_reference;
	subpass_description.pResolveAttachments = nullptr;
	subpass_description.pDepthStencilAttachment = nullptr;
	subpass_description.preserveAttachmentCount = 0;
	subpass_description.pPreserveAttachments = nullptr;

	VkSubpassDependency subpass_dependendy = {};
	subpass_dependendy.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpass_dependendy.dstSubpass = 0;
	subpass_dependendy.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpass_dependendy.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpass_dependendy.srcAccessMask = 0;
	subpass_dependendy.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpass_dependendy.dependencyFlags = 0;

	VkRenderPassCreateInfo render_pass_create_info = {};
	render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_create_info.pNext = nullptr;
	render_pass_create_info.flags = 0;
	render_pass_create_info.attachmentCount = 1;
	render_pass_create_info.pAttachments = &color_attachment_description;
	render_pass_create_info.subpassCount = 1;
	render_pass_create_info.pSubpasses = &subpass_description;
	render_pass_create_info.dependencyCount = 1;
	render_pass_create_info.pDependencies = &subpass_dependendy;

	VK_CHECK(vkCreateRenderPass(
		vkcontext.logical_device,
		&render_pass_create_info,
		vkcontext.allocator,
		&vkcontext.render_pass));

	// vulkan framebuffers
	vkcontext.framebuffers = new VkFramebuffer[swapchain_image_count];
	for (uint32_t i = 0; i < swapchain_image_count; ++i) {
		VkFramebufferCreateInfo framebuffer_create_info = {};
		framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_create_info.pNext = nullptr;
		framebuffer_create_info.flags = 0;
		framebuffer_create_info.renderPass = vkcontext.render_pass;
		framebuffer_create_info.attachmentCount = 1;
		framebuffer_create_info.pAttachments = &vkcontext.swapchain_image_views[i];
		framebuffer_create_info.width = info.screen_width;
		framebuffer_create_info.height = info.screen_height;
		framebuffer_create_info.layers = 1;
		VK_CHECK(vkCreateFramebuffer(
			vkcontext.logical_device,
			&framebuffer_create_info,
			vkcontext.allocator,
			&vkcontext.framebuffers[i]));
	}

	// vulkan command pool
	VkCommandPoolCreateInfo command_pool_create_info = {};
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.pNext = nullptr;
	command_pool_create_info.flags = 0;
	command_pool_create_info.queueFamilyIndex = vkcontext.graphics_queue.family_index;

	VK_CHECK(vkCreateCommandPool(
		vkcontext.logical_device,
		&command_pool_create_info,
		vkcontext.allocator,
		&vkcontext.command_pool));

	// vulkan command buffers
	VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
	command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_allocate_info.pNext = nullptr;
	command_buffer_allocate_info.commandPool = vkcontext.command_pool;
	command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_allocate_info.commandBufferCount = swapchain_image_count;

	vkcontext.command_buffers = new VkCommandBuffer[swapchain_image_count];
	VK_CHECK(vkAllocateCommandBuffers(
		vkcontext.logical_device,
		&command_buffer_allocate_info,
		vkcontext.command_buffers));

	// vulkan graphics pipeline
	// shader modules
	std::vector<char> vertex_code = read_file("res/shaders/vert.spv");
	std::vector<char> fragment_code = read_file("res/shaders/frag.spv");
	printf("\n-+-Vertex shader size: %zi\n", vertex_code.size());
	printf("-+-Fragment shader size: %zi\n", fragment_code.size());

	vkcontext.vertex_shader = create_shader_module(&vkcontext, vertex_code);
	vkcontext.fragment_shader = create_shader_module(&vkcontext, fragment_code);

	VkPipelineShaderStageCreateInfo vertex_shader_stage_info = {};
	vertex_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertex_shader_stage_info.pNext = nullptr;
	vertex_shader_stage_info.flags = 0;
	vertex_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertex_shader_stage_info.module = vkcontext.vertex_shader;
	vertex_shader_stage_info.pName = "main";
	vertex_shader_stage_info.pSpecializationInfo;

	VkPipelineShaderStageCreateInfo fragment_shader_stage_info = {};
	fragment_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragment_shader_stage_info.pNext = nullptr;
	fragment_shader_stage_info.flags = 0;
	fragment_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragment_shader_stage_info.module = vkcontext.fragment_shader;
	fragment_shader_stage_info.pName = "main";
	fragment_shader_stage_info.pSpecializationInfo;

	VkPipelineShaderStageCreateInfo shader_stages[] = {
		vertex_shader_stage_info,
		fragment_shader_stage_info
	};

	// dynamic state
	/*
	VkDynamicState dynamic_state[]{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {};
	dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state_create_info.pNext = nullptr;
	dynamic_state_create_info.flags = 0;
	dynamic_state_create_info.dynamicStateCount = static_cast<uint32_t>(ARRAY_COUNT(dynamic_state));
	dynamic_state_create_info.pDynamicStates = dynamic_state;
	*/

	// vertex input
	VkPipelineVertexInputStateCreateInfo vertex_input_create_info = {};
	vertex_input_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_create_info.pNext = nullptr;
	vertex_input_create_info.flags = 0;
	vertex_input_create_info.vertexBindingDescriptionCount = 0;
	vertex_input_create_info.pVertexBindingDescriptions = nullptr;
	vertex_input_create_info.vertexAttributeDescriptionCount = 0;
	vertex_input_create_info.pVertexAttributeDescriptions = nullptr;

	// input assembly
	VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info = {};
	input_assembly_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly_create_info.pNext = nullptr;
	input_assembly_create_info.flags = 0;
	input_assembly_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	input_assembly_create_info.primitiveRestartEnable = VK_FALSE;

	// viewport and scissors
	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(info.screen_width);
	viewport.height = static_cast<float>(info.screen_height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = { info.screen_width, info.screen_height };

	VkPipelineViewportStateCreateInfo viewport_state_create_info = {};
	viewport_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state_create_info.pNext = nullptr;
	viewport_state_create_info.flags = 0;
	viewport_state_create_info.viewportCount = 1;
	viewport_state_create_info.pViewports = &viewport; // NOTE: no need for this if dynamic state is specified
	viewport_state_create_info.scissorCount = 1;
	viewport_state_create_info.pScissors = &scissor; // NOTE: no need for this if dynamic state is specified

	// rasterization
	VkPipelineRasterizationStateCreateInfo rasterization_state_create_info = {};
	rasterization_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization_state_create_info.pNext = nullptr;
	rasterization_state_create_info.flags = 0;
	rasterization_state_create_info.depthClampEnable = VK_FALSE;
	rasterization_state_create_info.rasterizerDiscardEnable = VK_FALSE;
	rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterization_state_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterization_state_create_info.depthBiasEnable = VK_FALSE;
	rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
	rasterization_state_create_info.depthBiasClamp = 0.0f;
	rasterization_state_create_info.depthBiasSlopeFactor = 0.0f;
	rasterization_state_create_info.lineWidth = 1.0f;

	// multisample
	VkPipelineMultisampleStateCreateInfo multisample_state_create_info = {};
	multisample_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample_state_create_info.pNext = nullptr;
	multisample_state_create_info.flags = 0;
	multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisample_state_create_info.sampleShadingEnable = VK_FALSE;
	multisample_state_create_info.minSampleShading = 1.0f;
	multisample_state_create_info.pSampleMask = nullptr;
	multisample_state_create_info.alphaToCoverageEnable = VK_FALSE;
	multisample_state_create_info.alphaToOneEnable = VK_FALSE;

	// TODO: depth and stencil testing
	//VkPipelineDepthStencilStateCreateInfo

	// color blending
	VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
	color_blend_attachment_state.blendEnable = VK_TRUE;
	color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; // NOTE: for color blending
	color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; // NOTE: for color blending
	color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
	color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
	color_blend_attachment_state.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
	color_blend_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blend_state_create_info.pNext = nullptr;
	color_blend_state_create_info.flags = 0;
	color_blend_state_create_info.logicOpEnable = VK_FALSE;
	color_blend_state_create_info.logicOp = VK_LOGIC_OP_NO_OP;
	color_blend_state_create_info.attachmentCount = 1;
	color_blend_state_create_info.pAttachments = &color_blend_attachment_state;
	color_blend_state_create_info.blendConstants[0] = 0.0f;
	color_blend_state_create_info.blendConstants[1] = 0.0f;
	color_blend_state_create_info.blendConstants[2] = 0.0f;
	color_blend_state_create_info.blendConstants[3] = 0.0f;

	// pipeline layout
	VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
	pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_create_info.pNext = nullptr;
	pipeline_layout_create_info.flags = 0;
	pipeline_layout_create_info.setLayoutCount = 0;
	pipeline_layout_create_info.pSetLayouts = nullptr;
	pipeline_layout_create_info.pushConstantRangeCount = 0;
	pipeline_layout_create_info.pPushConstantRanges = nullptr;

	VK_CHECK(vkCreatePipelineLayout(
		vkcontext.logical_device,
		&pipeline_layout_create_info,
		vkcontext.allocator,
		&vkcontext.pipeline_layout));

	// graphics pipeline create
	VkGraphicsPipelineCreateInfo graphics_pipeline_create_info = {};
	graphics_pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	graphics_pipeline_create_info.pNext = nullptr;
	graphics_pipeline_create_info.flags = 0;
	graphics_pipeline_create_info.stageCount = 2;
	graphics_pipeline_create_info.pStages = shader_stages;
	graphics_pipeline_create_info.pVertexInputState = &vertex_input_create_info;
	graphics_pipeline_create_info.pInputAssemblyState = &input_assembly_create_info;
	graphics_pipeline_create_info.pTessellationState = nullptr;
	graphics_pipeline_create_info.pViewportState = &viewport_state_create_info;
	graphics_pipeline_create_info.pRasterizationState = &rasterization_state_create_info;
	graphics_pipeline_create_info.pMultisampleState = &multisample_state_create_info;
	graphics_pipeline_create_info.pDepthStencilState = nullptr;
	graphics_pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
	graphics_pipeline_create_info.pDynamicState = nullptr;
	graphics_pipeline_create_info.layout = vkcontext.pipeline_layout;
	graphics_pipeline_create_info.renderPass = vkcontext.render_pass;
	graphics_pipeline_create_info.subpass = 0;
	graphics_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
	graphics_pipeline_create_info.basePipelineIndex = 0;

	VK_CHECK(vkCreateGraphicsPipelines(
		vkcontext.logical_device,
		nullptr,
		1,
		&graphics_pipeline_create_info,
		vkcontext.allocator,
		&vkcontext.pipeline));

	// vulkan command buffer begin
	VkCommandBufferBeginInfo command_buffer_begin_info = {};
	command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	command_buffer_begin_info.pNext = nullptr;
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	command_buffer_begin_info.pInheritanceInfo = nullptr;

	for (uint32_t i = 0; i < swapchain_image_count; ++i) {
		VK_CHECK(vkBeginCommandBuffer(vkcontext.command_buffers[i], &command_buffer_begin_info));
		VkRenderPassBeginInfo render_pass_begin_info = {};
		render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_begin_info.pNext = nullptr;
		render_pass_begin_info.renderPass = vkcontext.render_pass;
		render_pass_begin_info.framebuffer = vkcontext.framebuffers[i];
		render_pass_begin_info.renderArea.offset = { 0, 0 };
		render_pass_begin_info.renderArea.extent = { info.screen_width, info.screen_height };
		VkClearValue clear_value = { 0.0f, 0.0f, 0.0f, 1.0f };
		render_pass_begin_info.clearValueCount = 1;
		render_pass_begin_info.pClearValues = &clear_value;
		vkCmdBeginRenderPass(
			vkcontext.command_buffers[i],
			&render_pass_begin_info,
			VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(
			vkcontext.command_buffers[i],
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkcontext.pipeline);

		vkCmdDraw(vkcontext.command_buffers[i], 3, 1, 0, 0);

		vkCmdEndRenderPass(vkcontext.command_buffers[i]);
		VK_CHECK(vkEndCommandBuffer(vkcontext.command_buffers[i]));
	}

	// vulkan fence
	VkFenceCreateInfo fence_create_info = {};
	fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_create_info.pNext = nullptr;
	fence_create_info.flags = 0;

	VK_CHECK(vkCreateFence(
		vkcontext.logical_device, 
		&fence_create_info, 
		vkcontext.allocator, 
		&vkcontext.fence));

	// vulkan semaphore
	VkSemaphoreCreateInfo semaphore_create_info = {};
	semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphore_create_info.pNext = nullptr;
	semaphore_create_info.flags = 0;

	VK_CHECK(vkCreateSemaphore(
		vkcontext.logical_device, 
		&semaphore_create_info, 
		vkcontext.allocator, 
		&vkcontext.semaphore_image_available));
	VK_CHECK(vkCreateSemaphore(
		vkcontext.logical_device,
		&semaphore_create_info,
		vkcontext.allocator,
		&vkcontext.semaphore_rendering_done));

	// MAIN LOOP
	// MAIN LOOP
	// MAIN LOOP
	while (engine.running) {
		MSG msg = {};
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		uint32_t image_index = 0;
		vkAcquireNextImageKHR(
			vkcontext.logical_device,
			vkcontext.swapchain,
			UINT64_MAX,
			vkcontext.semaphore_image_available,
			vkcontext.fence,
			&image_index);

		//vkResetCommandPool(vkcontext.logical_device, vkcontext.command_pool, 0);

		// NOTE: begin command buffer should be here

		VK_CHECK(vkWaitForFences(vkcontext.logical_device, 1, &vkcontext.fence, VK_TRUE, UINT64_MAX));
		VK_CHECK(vkResetFences(vkcontext.logical_device, 1, &vkcontext.fence));

		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.pNext = nullptr;
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = &vkcontext.semaphore_image_available;
		VkPipelineStageFlags wait_stage_mask[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submit_info.pWaitDstStageMask = wait_stage_mask;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &vkcontext.command_buffers[image_index];
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = &vkcontext.semaphore_rendering_done;
		VK_CHECK(vkQueueSubmit(
			vkcontext.graphics_queue.handle, 
			1, 
			&submit_info, 
			nullptr));

		VK_CHECK(vkDeviceWaitIdle(vkcontext.logical_device));

		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.pNext = nullptr;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = &vkcontext.semaphore_rendering_done;
		present_info.swapchainCount = 1;
		present_info.pSwapchains = &vkcontext.swapchain;
		present_info.pImageIndices = &image_index;
		present_info.pResults = nullptr;
		VK_CHECK(vkQueuePresentKHR(vkcontext.graphics_queue.handle, &present_info));
		
		// TODO: temporary
		Sleep(1);
	} // MAIN LOOP

	// destroy vulkan resources
	vkDeviceWaitIdle(vkcontext.logical_device); // NOTE: avoid crashes
	
	// semaphores
	if (vkcontext.semaphore_image_available) {
		vkDestroySemaphore(
			vkcontext.logical_device, 
			vkcontext.semaphore_image_available, 
			vkcontext.allocator);
		vkcontext.semaphore_image_available = 0;
	}

	if (vkcontext.semaphore_rendering_done) {
		vkDestroySemaphore(
			vkcontext.logical_device,
			vkcontext.semaphore_rendering_done,
			vkcontext.allocator);
		vkcontext.semaphore_rendering_done = 0;
	}

	// fence
	if (vkcontext.fence) {
		vkDestroyFence(
			vkcontext.logical_device,
			vkcontext.fence,
			vkcontext.allocator);
		vkcontext.fence = 0;
	}

	// command buffers
	if (vkcontext.command_buffers) {
		vkFreeCommandBuffers(
			vkcontext.logical_device, 
			vkcontext.command_pool, 
			swapchain_image_count, 
			vkcontext.command_buffers);
		delete[] vkcontext.command_buffers;
	}

	// command pool
	if (vkcontext.command_pool) {
		vkDestroyCommandPool(
			vkcontext.logical_device, 
			vkcontext.command_pool, 
			vkcontext.allocator);
		vkcontext.command_pool = 0;
	}

	// framebuffers
	if (vkcontext.framebuffers) {
		for (uint32_t i = 0; i < swapchain_image_count; ++i) {
			vkDestroyFramebuffer(
				vkcontext.logical_device, 
				vkcontext.framebuffers[i], 
				vkcontext.allocator);
		}
		delete[] vkcontext.framebuffers;
	}

	// pipeline
	if (vkcontext.pipeline) {
		vkDestroyPipeline(
			vkcontext.logical_device, 
			vkcontext.pipeline, 
			vkcontext.allocator);
		vkcontext.pipeline = 0;
	}

	// pipeline layout
	if (vkcontext.pipeline_layout) {
		vkDestroyPipelineLayout(
			vkcontext.logical_device, 
			vkcontext.pipeline_layout, 
			vkcontext.allocator);
		vkcontext.pipeline_layout = 0;
	}

	// render pass
	if (vkcontext.render_pass) {
		vkDestroyRenderPass(
			vkcontext.logical_device, 
			vkcontext.render_pass, 
			vkcontext.allocator);
		vkcontext.render_pass = 0;
	}


	// shaders
	if (vkcontext.fragment_shader) {
		vkDestroyShaderModule(
			vkcontext.logical_device, 
			vkcontext.fragment_shader, 
			vkcontext.allocator);
		vkcontext.fragment_shader = 0;
	}

	if (vkcontext.vertex_shader) {
		vkDestroyShaderModule(
			vkcontext.logical_device,
			vkcontext.vertex_shader,
			vkcontext.allocator);
		vkcontext.vertex_shader = 0;
	}

	// image views
	if (vkcontext.swapchain_image_views) {
		for (uint32_t i = 0; i < swapchain_image_count; ++i) {
			vkDestroyImageView(
				vkcontext.logical_device,
				vkcontext.swapchain_image_views[i],
				vkcontext.allocator);
		}
		delete[] vkcontext.swapchain_image_views;
	}

	// swapchain
	if (vkcontext.swapchain) {
		vkDestroySwapchainKHR(
			vkcontext.logical_device, 
			vkcontext.swapchain, 
			vkcontext.allocator);
		vkcontext.swapchain = 0;
	}

	// surface
	if (vkcontext.surface) {
		vkDestroySurfaceKHR(
			vkcontext.instance, 
			vkcontext.surface, 
			vkcontext.allocator);
		vkcontext.surface = 0;
	}

	// logical device
	if (vkcontext.logical_device) {
		vkDestroyDevice(vkcontext.logical_device, vkcontext.allocator);
		vkcontext.logical_device = 0;
	}

	// device messenger
	if (engine.debug) {
		if (vkcontext.debug_messenger) {
			PFN_vkDestroyDebugUtilsMessengerEXT function =
				(PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vkcontext.instance, "vkDestroyDebugUtilsMessengerEXT");
			function(vkcontext.instance, vkcontext.debug_messenger, vkcontext.allocator);
			vkcontext.debug_messenger = 0;
		}
	}

	// instance
	if (vkcontext.instance) {
		vkDestroyInstance(vkcontext.instance, vkcontext.allocator);
		vkcontext.instance = 0;
	}

	// destroy window
	if (window.hwnd) {
		DestroyWindow(window.hwnd);
		window.hwnd = 0;
	}

	return 0;
}

LRESULT CALLBACK win32_process_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	LRESULT result = 0;
	switch (message) {
		case WM_CLOSE:
			engine.running = false;
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default:
			result = DefWindowProcA(hwnd, message, wparam, lparam);
	}
	return result;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_types,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
	void *user_data) {
	switch (message_severity) {
		default:
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			printf("\n%s\n", callback_data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			printf("\n%s\n", callback_data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			printf("\n%s\n", callback_data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			printf("\n%s\n", callback_data->pMessage);
			break;
	}
	return VK_FALSE;
}

std::vector<char> read_file(const std::string &filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) { throw std::runtime_error("Failed to open file"); }
	size_t file_size = static_cast<size_t>(file.tellg());
	std::vector<char> buffer(file_size);
	file.seekg(0);
	file.read(buffer.data(), file_size);
	file.close();
	return buffer;
}

VkShaderModule create_shader_module(vulkan_context *context, const std::vector<char> &shader_code) {
	VkShaderModuleCreateInfo shader_module_create_info = {};
	shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shader_module_create_info.pNext = nullptr;
	shader_module_create_info.flags = 0;
	shader_module_create_info.codeSize = static_cast<size_t>(shader_code.size());
	shader_module_create_info.pCode = reinterpret_cast<const uint32_t *>(shader_code.data());
	
	VkShaderModule out_shader_module;
	VK_CHECK(vkCreateShaderModule(
		context->logical_device,
		&shader_module_create_info,
		context->allocator,
		&out_shader_module));
	return out_shader_module;
}