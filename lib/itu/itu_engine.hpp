// unity build header
// include this only once, and no other header

#ifndef ITU_ENGINE_HPP
#define ITU_ENGINE_HPP

#define ITU_UNITY_BUILD
#include <SDL3/SDL.h>

#define STB_DS_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include <stb_ds.h>
#include <stb_image.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_sdl3.h>
#include <imgui/imgui_impl_sdlrenderer3.h>
#include <imgui/imgui_impl_sdlgpu3.h>
#include <imgui/imgui_impl_sdlgpu3_shaders.h>

#include <box2d/box2d.h>

// low level libraries (no engine or memory allocation involved)
#include <itu_common.hpp>
#include <itu_lib_render_screen.hpp>
#include <itu_lib_overlaps.hpp>
#include <itu_lib_transform2d.hpp>

// context libraries (slightly higher-level functionality)
#include <itu_lib_context.hpp>
#include <utils/itu_utils_box2d.hpp>
#include <itu_lib_render2d.hpp>
#include <itu_lib_imgui.hpp>

// NOTE: some static code analysis will throw an error here mentioning "file cannot be included
//       recursively", or something similar. This is not a real error that will happen when we
//       compile (it might have implications on compile times, but we're gonna ignore that here)
//       If it happens to you and that annoys you, I've wrapped these .cpp inclusions to make it
//       easy to silence this. Just define THE `IN_IDE` preprocessor symbol IN THE TOOL ONLY!
#ifndef IN_IDE
	#include <itu_lib_context.cpp>
	#include <utils/itu_utils_box2d.cpp>
	#include <itu_lib_render2d.cpp>
	#include <itu_lib_imgui.cpp>
#endif // IN_IDE

#endif // ITU_ENGINE_HPP