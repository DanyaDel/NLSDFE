#version 450
void main() {
    // Full-screen triangle: 
    // (-1,-1), (3,-1), (-1,3)
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
