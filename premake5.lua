local BGFX_PATH = path.join(path.getabsolute(".."), "bgfx")

if not os.isdir(BGFX_PATH) then
	print("bgfx not found at " .. BGFX_PATH)
	os.exit()
end

local BX_PATH = path.join(path.getabsolute(".."), "bx")

if not os.isdir(BX_PATH) then
	print("bx not found at " .. BX_PATH)
	os.exit()
end

local IOQ3_PATH = path.join(path.getabsolute(".."), "ioq3")
local RENDERER_PATH = path.getabsolute(".")

if os.is("windows") then
	if not os.isdir(IOQ3_PATH) then
		print("ioquake3 not found at " .. IOQ3_PATH)
		os.exit()
	end
end

newaction
{
	trigger = "shaders",
	description = "Compile shaders",
	
	onStart = function()
		os.mkdir("build/shaders")
		dofile("shader.lua")
		initShaderCompilation()
		
		function compileAllShaders()
			compileShader(BGFX_PATH, "AdaptedLuminance", "fragment")
			compileShader(BGFX_PATH, "Depth", "fragment")
			compileShader(BGFX_PATH, "Depth", "fragment", "AlphaTest", "USE_ALPHA_TEST")
			compileShader(BGFX_PATH, "Depth", "vertex")
			compileShader(BGFX_PATH, "Depth", "vertex", "AlphaTest", "USE_ALPHA_TEST")
			compileShader(BGFX_PATH, "Fog", "fragment")
			compileShader(BGFX_PATH, "Fog", "vertex")
			compileShader(BGFX_PATH, "FXAA", "fragment")
			compileShader(BGFX_PATH, "Generic", "fragment")
			compileShader(BGFX_PATH, "Generic", "fragment", "AlphaTest", "USE_ALPHA_TEST")
			compileShader(BGFX_PATH, "Generic", "fragment", "AlphaTestSoftSprite", "USE_ALPHA_TEST;USE_SOFT_SPRITE")
			compileShader(BGFX_PATH, "Generic", "fragment", "SoftSprite", "USE_SOFT_SPRITE")
			compileShader(BGFX_PATH, "Generic", "vertex")
			compileShader(BGFX_PATH, "LinearDepth", "fragment")
			compileShader(BGFX_PATH, "Luminance", "fragment")
			compileShader(BGFX_PATH, "LuminanceDownsample", "fragment")
			compileShader(BGFX_PATH, "Texture", "fragment")
			compileShader(BGFX_PATH, "TextureSingleChannel", "fragment")
			compileShader(BGFX_PATH, "Texture", "vertex")
			compileShader(BGFX_PATH, "TextureColor", "fragment")
			compileShader(BGFX_PATH, "ToneMap", "fragment")
		end

		local ok, message = pcall(compileAllShaders)
		if ok then
			print("Done.")
		else
			print(message)
		end
    end,
}

solution "renderer_bgfx"
	configurations { "Release", "Debug" }
	location "build"
	
	if os.is64bit() and not os.is("windows") then
		platforms { "x86_64", "x86" }
	else
		platforms { "x86", "x86_64" }
	end
		
	startproject "renderer_bgfx"
	
	configuration "platforms:x86"
		architecture "x86"
		
	configuration "platforms:x86_64"
		architecture "x86_64"
	
	configuration "Debug"
		optimize "Debug"
		defines { "_DEBUG" }
		flags "Symbols"
		
	configuration { "Debug", "x86" }
		targetdir "build/bin_x86_debug"
		
	configuration { "Debug", "x86_64" }
		targetdir "build/bin_x64_debug"
		
	configuration "Release"
		optimize "Full"
		defines "NDEBUG"
		
	configuration { "Release", "x86" }
		targetdir "build/bin_x86"
		
	configuration { "Release", "x86_64" }
		targetdir "build/bin_x64"
		
	configuration "vs*"
		defines { "_CRT_SECURE_NO_DEPRECATE" }
		
	configuration { "vs*", "x86_64" }
		defines { "_WIN64", "__WIN64__" }
		
dofile("renderer_bgfx.lua")

if os.is("windows") then
	createRendererProject(BGFX_PATH, BX_PATH, RENDERER_PATH, path.join(IOQ3_PATH, "code/SDL2/include"), path.join(IOQ3_PATH, "code/libs/win32/libSDL2"), path.join(IOQ3_PATH, "code/libs/win64/libSDL264"))
else
	createRendererProject(BGFX_PATH, BX_PATH, RENDERER_PATH, nil, nil, nil)
end
