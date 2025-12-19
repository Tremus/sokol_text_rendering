/* text vertex shader */
@vs vs_text

struct text_buffer
{
    vec2 coord_topleft;
    vec2 coord_bottomright;
    uint tex_topleft;
    uint tex_bottomright;
};

layout(binding=0) readonly buffer sb_text {
    text_buffer vtx[];
};

layout(binding=0) uniform vs_text_uniforms {
    vec2 size;
};

out vec2 texcoord;


void main() {
    uint v_idx = gl_VertexIndex / 6u;
    uint i_idx = gl_VertexIndex - v_idx * 6;

    text_buffer obj = vtx[v_idx];

    //  0.5f,  0.5f,
    // -0.5f, -0.5f,
    //  0.5f, -0.5f,
    // -0.5f,  0.5f,
    // 0, 1, 2,
    // 1, 2, 3,

    // Is odd
    bool is_right = (gl_VertexIndex & 1) == 1;
    bool is_bottom = i_idx >= 2 && i_idx <= 4;

    vec2 pos = vec2(
        is_right  ? obj.coord_bottomright.x : obj.coord_topleft.x,
        is_bottom ? obj.coord_bottomright.y : obj.coord_topleft.y
    );

    pos = (pos + pos) / size - vec2(1);
    pos.y = -pos.y;

    gl_Position = vec4(pos, 1, 1);


    vec2 tex_topleft = unpackUnorm2x16(obj.tex_topleft);
    vec2 tex_bottomright = unpackUnorm2x16(obj.tex_bottomright);
    texcoord = vec2(
        is_right  ? tex_bottomright.x : tex_topleft.x,
        is_bottom ? tex_bottomright.y : tex_topleft.y
    );
}
@end

@fs fs_text_singlechannel
layout(binding=1) uniform texture2D text_tex;
layout(binding=0) uniform sampler text_smp;

layout(binding=1) uniform fs_text_singlechannel {
    vec4 u_colour;
};


in vec2 texcoord;
out vec4 frag_colour;

void main() {
    float alpha = texture(sampler2D(text_tex, text_smp), texcoord).r;
    frag_colour = vec4(u_colour.rgb, u_colour.a * alpha);
}
@end

@fs fs_text_multichannel
layout(binding=1) uniform texture2D text_tex;
layout(binding=0) uniform sampler text_smp;

in vec2 texcoord;
out vec4 frag_colour;

void main() {
    frag_colour = texture(sampler2D(text_tex, text_smp), texcoord);
}
@end

@program text_singlechannel vs_text fs_text_singlechannel
@program text_multichannel vs_text fs_text_multichannel