/* text vertex shader */
@vs vs_text
in vec4 position;
in vec2 texcoord0;
out vec2 uv;

void main() {
    gl_Position = position;
    uv = texcoord0;
}
@end

@fs fs_text_singlechannel
layout(binding=0) uniform texture2D text_tex;
layout(binding=0) uniform sampler text_smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    float alpha = texture(sampler2D(text_tex, text_smp), uv).r;
    frag_color = vec4(1, 1, 1, alpha);
}
@end

@fs fs_text_multichannel
layout(binding=0) uniform texture2D text_tex;
layout(binding=0) uniform sampler text_smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(text_tex, text_smp), uv);
}
@end

@program text_singlechannel vs_text fs_text_singlechannel
@program text_multichannel vs_text fs_text_multichannel