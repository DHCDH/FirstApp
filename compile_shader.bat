D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader.vert -o res\shaders\spv\shader.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader.frag -o res\shaders\spv\shader.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\point_light.vert -o res\shaders\spv\point_light.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\point_light.frag -o res\shaders\spv\point_light.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_instanced.vert -o res\shaders\spv\shader_instanced.vert.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_blank.vert -o res\shaders\spv\shader_slice_blank.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_blank.frag -o res\shaders\spv\shader_slice_blank.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_instanced.vert -o res\shaders\spv\shader_slice_instanced.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_instanced.frag -o res\shaders\spv\shader_slice_instanced.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_display.vert -o res\shaders\spv\shader_slice_display.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_display.frag -o res\shaders\spv\shader_slice_display.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_plane.vert -o res\shaders\spv\shader_slice_plane.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_plane.frag -o res\shaders\spv\shader_slice_plane.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_edge.frag -o res\shaders\spv\shader_slice_edge.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_geom.vert -o res\shaders\spv\shader_slice_geom.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_geom.geom -o res\shaders\spv\shader_slice_geom.geom.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_geom.frag -o res\shaders\spv\shader_slice_geom.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_empty.frag -o res\shaders\spv\shader_slice_empty.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_slice_resolve.frag -o res\shaders\spv\shader_slice_resolve.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_extract_contour.comp -o res\shaders\spv\shader_extract_contour.comp.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\calculate_rake_angle\shader_sort_contour.comp -o res\shaders\spv\shader_sort_contour.comp.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\calculate_rake_angle\shader_trace.comp -o res\shaders\spv\shader_trace.comp.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\calculate_rake_angle\shader_align.comp -o res\shaders\spv\shader_align.comp.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\calculate_rake_angle\shader_rake_angle.comp -o res\shaders\spv\shader_rake_angle.comp.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe res\shaders\shader_find_bbox.comp -o res\shaders\spv\shader_find_bbox.comp.spv


REM optimize

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_align.comp -o res\shaders\spv\optimize\shader_align.comp.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_blank.vert -o res\shaders\spv\optimize\shader_optimize_blank.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_blank.frag -o res\shaders\spv\optimize\shader_optimize_blank.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_empty.frag -o res\shaders\spv\optimize\shader_optimize_empty.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_extract_contour.comp -o res\shaders\spv\optimize\shader_optimize_extract_contour.comp.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_find_bbox.comp -o res\shaders\spv\optimize\shader_optimize_find_bbox.comp.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_generate_wheels.vert -o res\shaders\spv\optimize\shader_optimize_generate_wheels.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_generate_wheels.frag -o res\shaders\spv\optimize\shader_optimize_generate_wheels.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_plane.vert -o res\shaders\spv\optimize\shader_optimize_plane.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_plane.frag -o res\shaders\spv\optimize\shader_optimize_plane.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_resolve.frag -o res\shaders\spv\optimize\shader_optimize_resolve.frag.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_rake_angle.comp -o res\shaders\spv\optimize\shader_rake_angle.comp.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_sort_contour.comp -o res\shaders\spv\optimize\shader_sort_contour.comp.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_trace.comp -o res\shaders\spv\optimize\shader_trace.comp.spv

D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\shader_optimize_flute_width.comp -o res\shaders\spv\optimize\shader_optimize_flute_width.comp.spv

REM pure compute
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\pure_compute\shader_polar_intersect.comp -o res\shaders\spv\optimize\pure_compute\shader_polar_intersect.comp.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\pure_compute\shader_polar_evaluate.comp -o res\shaders\spv\optimize\pure_compute\shader_polar_evaluate.comp.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\pure_compute\shader_polar_reduce.comp -o res\shaders\spv\optimize\pure_compute\shader_polar_reduce.comp.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\optimize\pure_compute\shader_polar_PSO.comp -o res\shaders\spv\optimize\pure_compute\shader_polar_PSO.comp.spv

REM 3D simulation
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\3dsimulation\shader_outline.vert -o res\shaders\spv\3dsimulation\shader_outline.vert.spv
D:\Applications\DevTools\VulkanSDK\1.4.321.1\Bin\glslc.exe --target-env=vulkan1.2 res\shaders\3dsimulation\shader_outline.frag -o res\shaders\spv\3dsimulation\shader_outline.frag.spv

pause