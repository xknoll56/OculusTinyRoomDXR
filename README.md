# Overview

The goal of this project is to recreate the Oculus Tiny Room DirectX sample using DirectX Raytracing (DXR).
I wanted to first create a 1-1 replica (functionally) to the original sample while also providing some extra
capabilities and features of real-time raytracing in DirectX12. Because the pipeline for DXR is entirely different
than the rasterization pipeline, the code for the renderer is vastly different, although I did try to keep some of 
the same structure. This is partially due to the fact that you can only really have one pipeline running per frame 
in DXR, and rather than having shaders bound for every material like the original sample, you need to make use
of the shader binding table if you want to have different shaders. The original sample did have some interesting 
design decisions (like having a single pipeline per material, and a new instance of a material for every model) that 
needed to be refactored.  

## Installation

To install and run the sample, you must have Visual Studio 2022 (I am using the Community edition). You may be able
to run it on an earlier version but I haven't tested it. Open up the `.sln` file with Visual Studio and pick one of the
projects and run it. The first project is basically identical to the original sample. The second project adds some lighting,
reflections, and shadows, and also has controller input. The third sample adds a sphere by making use of procedural geometry
shading. You will need to have your Meta/Oculus headset connected to your computer along with Quest Link running. There
may be a method of using another type of headset if you can get it compatible with `libovr`. Your computer must have raytracing 
compatibility.

## Future Improvements

I will hopefully be adding DLSS and model loading and various other improvements. I may end up also setting this sample up
with Vulkan at some point (when/if I decide to learn Vulkan Raytracing). Enjoy!
