sokol-shdc -i src\shaders\text.glsl -o src\shaders\text.glsl.h -l hlsl5
sokol-shdc -i src\shaders\img.glsl -o src\shaders\img.glsl.h -l hlsl5

shader-hotreloader.exe -i src

cmd /k