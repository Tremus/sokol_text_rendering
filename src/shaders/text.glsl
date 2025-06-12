/* text vertex shader */
@vs text_vs
in vec4 position;
in vec2 texcoord0;
out vec2 uv;

void main() {
    gl_Position = position;
    uv = texcoord0;
}
@end

/* text fragment shader */
@fs text_fs
layout(binding=0) uniform texture2D text_tex;
layout(binding=0) uniform sampler text_smp;

in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(text_tex, text_smp), uv);
}
@end

/* text shader program */
@program text text_vs text_fs