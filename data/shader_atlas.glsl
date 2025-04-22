//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs texture.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs
compute test.cs
plain basic.vs plain.fs

\test.cs
#version 430 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main() 
{
	vec4 i = vec4(0.0);
}

\basic.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;
in vec4 a_color;

uniform vec3 u_camera_pos;

uniform mat4 u_model;
uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;

uniform float u_time;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( v_position, 1.0) ).xyz;
	
	//store the color in the varying var to use it from the pixel shader
	v_color = a_color;

	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}

\quad.vs

#version 330 core

in vec3 a_vertex;
in vec2 a_coord;
out vec2 v_uv;

void main()
{	
	v_uv = a_coord;
	gl_Position = vec4( a_vertex, 1.0 );
}


\plain.fs

#version 330 core

out vec4 FragColor;

void main()
{
	FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}




\flat.fs

#version 330 core

uniform vec4 u_color;

out vec4 FragColor;

void main()
{
	FragColor = u_color;
}


\texture.fs

#version 330 core

mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
  // get edge vectors of the pixel triangle
  vec3 dp1 = dFdx(p);
  vec3 dp2 = dFdy(p);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);

  // solve the linear system
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

  // construct a scale-invariant frame 
  float invmax = 1.0 / sqrt(max(dot(T,T), dot(B,B)));
  return mat3(normalize(T * invmax), normalize(B * invmax), N);
}

vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel){
	normal_pixel =normal_pixel * 255./127. -128./127.;
	mat3 TBN = cotangentFrame(N, WP, uv);
	return normalize(TBN*normal_pixel);
}

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;
uniform vec3 u_camera_position;

uniform vec3 u_light_pos[10];
uniform vec3 u_light_color[10];
uniform vec3 u_light_dir[10];
uniform float u_light_intensity[10];
uniform int u_type[10];
uniform int u_light_shadow_index[10];
uniform int u_num_lights;

uniform vec3 u_mlight_pos;
uniform vec3 u_mlight_color;
uniform vec3 u_mlight_dir;
uniform float u_mlight_intensity;
uniform int u_mtype;
uniform int u_mlight_shadow_index;

uniform float u_shine;
uniform vec3 u_ambient_light;
uniform float u_alpha_max;
uniform float u_alpha_min;
uniform sampler2D u_normal_map;
uniform int u_single_pass;

// Multiple shadow maps
uniform int u_active_shadow_maps;
uniform sampler2D u_shadow_textures[4];
uniform mat4 u_shadow_matrices[4];

// Legacy single shadow map
uniform sampler2D u_shadowmap_legacy;
uniform mat4 u_shadowvp_legacy;

uniform float u_shadow_bias;

out vec4 FragColor;

// Helper function to check if point is in shadow
float isInShadow(vec3 world_pos, int shadow_map_index) {
    if (shadow_map_index < 0 || shadow_map_index >= u_active_shadow_maps)
        return 1.0; // No shadow map for this light, assume not in shadow
        
    // Project position to shadow map space
    vec4 proj_pos = u_shadow_matrices[shadow_map_index] * vec4(world_pos, 1.0);
    float real_depth = (proj_pos.z - u_shadow_bias) / proj_pos.w;
    
    // Convert to texture coordinates [0,1]
    proj_pos = proj_pos / proj_pos.w;
    proj_pos = (proj_pos + 1.0) / 2.0;
    real_depth = (real_depth + 1.0) / 2.0;
    
    // Check if outside shadow map
    if (proj_pos.x < 0.0 || proj_pos.x > 1.0 || 
        proj_pos.y < 0.0 || proj_pos.y > 1.0 || 
        proj_pos.z < 0.0 || proj_pos.z > 1.0)
        return 1.0;
        
    // Compare depths
    float shadow_depth = texture(u_shadow_textures[shadow_map_index], proj_pos.xy).r;
    return (real_depth <= shadow_depth) ? 1.0 : 0.0;
}

void main()
{
    // For legacy single shadow map support
    vec4 proj_pos = u_shadowvp_legacy * vec4(v_world_position, 1.0);
    float real_depth = (proj_pos.z - u_shadow_bias) / proj_pos.w;
    proj_pos = proj_pos / proj_pos.w;
    proj_pos = (proj_pos + 1.0) / 2.0;
    real_depth = (real_depth + 1.0) / 2.0;
    vec2 proj_coords = vec2(proj_pos.x, proj_pos.y);

	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture(u_texture, v_uv);

	vec3 texture_normal = texture(u_normal_map, uv).xyz;
	texture_normal = (texture_normal * 2.0) -1.0;
	texture_normal = normalize(texture_normal);

	vec3 normal = perturbNormal(v_normal, v_world_position, uv, texture_normal);

	vec3 light_component = vec3(0.0);
	if(u_single_pass == 1){
		for(int i = 0; i < u_num_lights; i++){
			vec3 L;
			float intensity = 0.0;
			vec3 L_unnorm = u_light_pos[i] - v_world_position;
			float d = length(L_unnorm);
            float in_shadow = 1.0;
            
            // Check shadow for this light
            if (u_light_shadow_index[i] >= 0) {
                in_shadow = isInShadow(v_world_position, u_light_shadow_index[i]);
            }

			if(u_type[i] == 1){ // Point light
				L = normalize(u_light_pos[i] - v_world_position);
				intensity = u_light_intensity[i]/(d*d);
			}
			else if(u_type[i] == 2){ // Spot light
				vec3 D = normalize(u_light_dir[i]);
				intensity = u_light_intensity[i]/(d*d);
				L = normalize(u_light_pos[i] - v_world_position);
				if(dot(L,D)<cos(u_alpha_max)){
					intensity = 0.0;
				}
				else {
					intensity *= 1 - clamp((dot(L,D) - cos(u_alpha_min))/(cos(u_alpha_max) - cos(u_alpha_min)), 0.0, 1.0);
				}
			}
			else if(u_type[i] == 3){ // Directional light
				L = normalize(u_light_dir[i]);
				intensity = u_light_intensity[i];
			}
			
			if (intensity > 0.0 && in_shadow > 0.0) {
				vec3 R = reflect(-L, normal);
				float r_dot_v = clamp(dot(R, normalize(u_camera_position - v_world_position)), 0.0, 1.0);
				float n_dot_l = clamp(dot(normal, L), 0.0, 1.0);
				
				light_component += intensity * u_light_color[i] * n_dot_l * in_shadow;
				light_component += intensity * u_light_color[i] * pow(r_dot_v, u_shine) * in_shadow;
			}
		}
	}
	else {
		// Legacy single light mode
		vec3 L;
		float intensity = 0.0;
		vec3 L_unnorm = u_mlight_pos - v_world_position;
		float d = length(L_unnorm);
        float in_shadow = 1.0;

		if(u_mtype == 1){ // Point light
			L = normalize(u_mlight_pos - v_world_position);
			intensity = u_mlight_intensity/(d*d);
		}
		else if(u_mtype == 2){ // Spot light
			vec3 D = normalize(u_mlight_dir);
			intensity = u_mlight_intensity/(d*d);
			L = normalize(u_mlight_pos - v_world_position);
			if(dot(L,D)<cos(u_alpha_max)){
				intensity = 0.0;
			}
			else {
				intensity *= 1 - clamp((dot(L,D) - cos(u_alpha_min))/(cos(u_alpha_max) - cos(u_alpha_min)), 0.0, 1.0);
			}
		}
		else if(u_mtype == 3){ // Directional light
			if(real_depth <= texture(u_shadowmap_legacy, proj_coords).r){
				L = normalize(u_mlight_dir);
				intensity = u_mlight_intensity;
				in_shadow = 1.0;
			}
			else {
				in_shadow = 0.0;
			}
		}
		
		if (intensity > 0.0 && in_shadow > 0.0) {
			vec3 R = reflect(-L, normal);
			float r_dot_v = clamp(dot(R, normalize(u_camera_position - v_world_position)), 0.0, 1.0);
			float n_dot_l = clamp(dot(normal, L), 0.0, 1.0);
			
			light_component += intensity * u_mlight_color * n_dot_l * in_shadow;
			light_component += intensity * u_mlight_color * pow(r_dot_v, u_shine) * in_shadow;
		}
	}

	light_component += u_ambient_light;

	if(color.a < u_alpha_cutoff)
		discard;

	FragColor = color * vec4(light_component, 1.0);
}


\skybox.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;

uniform samplerCube u_texture;
uniform vec3 u_camera_position;
out vec4 FragColor;

void main()
{
	vec3 E = v_world_position - u_camera_position;
	vec4 color = texture( u_texture, E );
	FragColor = color;
}


\multi.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, uv );

	if(color.a < u_alpha_cutoff)
		discard;

	vec3 N = normalize(v_normal);

	FragColor = color;
	NormalColor = vec4(N,1.0);
}


\depth.fs

#version 330 core

uniform vec2 u_camera_nearfar;
uniform sampler2D u_texture; //depth map
in vec2 v_uv;
out vec4 FragColor;

void main()
{
	float n = u_camera_nearfar.x;
	float f = u_camera_nearfar.y;
	float z = texture2D(u_texture,v_uv).x;
	if( n == 0.0 && f == 1.0 )
		FragColor = vec4(z);
	else
		FragColor = vec4( n * (z + 1.0) / (f + n - z * (f - n)) );
}


\instanced.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;

in mat4 u_model;

uniform vec3 u_camera_pos;

uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( a_vertex, 1.0) ).xyz;
	
	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}