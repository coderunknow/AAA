vec3 v_wpos       : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec3 v_normal     : NORMAL    = vec3(0.0, 1.0, 0.0);
vec2 v_texcoord0  : TEXCOORD1 = vec2(0.0, 0.0);
vec4 v_color0     : COLOR0    = vec4(1.0, 1.0, 1.0, 1.0);
vec4 v_color1     : COLOR1    = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_extra      : TEXCOORD2 = vec4(0.0, 0.0, 0.0, 0.0);
vec3 v_dir        : TEXCOORD3 = vec3(0.0, 0.0, 1.0);

vec3 a_position   : POSITION;
vec3 a_normal     : NORMAL;
vec2 a_texcoord0  : TEXCOORD0;
vec4 a_color0     : COLOR0;
vec4 a_color1     : COLOR1;
vec4 a_indices    : BLENDINDICES;
vec4 a_weight     : BLENDWEIGHT;
vec4 i_data0      : TEXCOORD7;
vec4 i_data1      : TEXCOORD6;
