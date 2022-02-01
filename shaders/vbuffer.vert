#version 450

layout(push_constant) uniform PushConstants {
    mat4 mat_vp; // view-projection matrix
    layout(row_major) mat4x3 mat_model; // model matrix, last row is assumed identity
    uint in_mesh_id;
};

layout (location=0) in vec3 in_pos;
layout (location=0) out flat uint mesh_id;

void main()
{
    gl_Position = mat_vp * vec4(mat_model * vec4(in_pos, 1), 1);
    mesh_id = in_mesh_id + gl_InstanceIndex;
}
