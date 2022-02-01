#version 450

layout (location=0) in flat uint mesh_id;
layout (location=0) out uint color;

void main()
{
    color = mesh_id;
}
