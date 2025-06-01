//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs texture.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs
compute test.cs
plain basic.vs plain.fs
fill basic.vs gbuffer_fill_fs
quad quad.vs quad.fs
light_first_pass quad.vs light.fs
light basic.vs light.fs
pbr basic.vs pbr.fs
quad_pbr quad.vs quad_pbr.fs

ambient_oclussion quad.vs ambient.fs

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
  float invmax = inversesqrt(max(dot(T,T), dot(B,B)));
  return mat3(T * invmax, B * invmax, N);
}

vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel){
	//normal_pixel = normal_pixel * 255./127. -128./127.;
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

uniform vec3 u_light_pos[10];
uniform vec3 u_light_color[10];
uniform vec3 u_light_dir[10];
uniform float u_light_intensity[10];
uniform int u_type[10];


uniform vec3 u_mlight_pos;
uniform vec3 u_mlight_color;
uniform vec3 u_mlight_dir;
uniform float u_mlight_intensity;
uniform int u_mtype;

uniform float u_shine;
uniform vec3 u_ambient_light;
uniform float u_alpha_max;
uniform float u_alpha_min;
uniform sampler2D u_normal_map;
uniform int u_single_pass;
uniform int location;

uniform sampler2D u_shadowmap;
uniform mat4 u_shadowvp;

uniform float u_shadow_bias;


uniform float u_scale; //color scale before tonemapper
// Average light intensity, this values goes from 0 to the
// higher light value
uniform float u_average_lum; 
// The value that defines the higher color intensity of our frame
// and so we set it as the white of our scene(but squared on the CPU)
uniform float u_lumwhite2; 
uniform float u_igamma; // inverse gamma (division done in the CPU)

out vec4 FragColor;



void main()
{

	vec4 proj_pos =u_shadowvp*vec4(v_world_position,1.0);
	float real_depth=(proj_pos.z-u_shadow_bias)/proj_pos.w;
	proj_pos=proj_pos/proj_pos.w;
	proj_pos=(proj_pos+1)/2;
	real_depth=(real_depth+1)/2;
	vec2 proj_coords = vec2(proj_pos.x,proj_pos.y);

	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, v_uv );

	

	vec3 texture_normal = texture(u_normal_map, uv).xyz;

	texture_normal = (texture_normal * 2.0) -1.0;
	texture_normal = normalize(texture_normal);

	vec3 normal = perturbNormal(normalize(v_normal), v_world_position, uv, texture_normal);

	vec3 light_component = vec3(0.0);


	if(u_single_pass == 1){
		for(int i = 0; i < 4; i++){
			vec3 L;
			float intensity =0.0;
			vec3 L_unnorm = u_light_pos[i] - v_world_position;
			float d = length(L_unnorm);


			if(u_type[i] == 1){
				L = normalize(u_light_pos[i] - v_world_position);
				intensity = u_light_intensity[i]/(d*d);
			}

			else if(u_type[i] == 2){
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

			else if(u_type[i] == 3){
				if(real_depth <= texture(u_shadowmap,proj_coords).r){
					L = normalize(u_light_dir[i]);
					intensity = u_light_intensity[i];
				}
			}
			
			vec3 R = reflect(L,normal);
			float r_dot_v = clamp(dot(R, normalize(normal)),0.0,1.0);
			float n_dot_v = clamp(dot(L, normalize(normal)),0.0,1.0);
			
			light_component += intensity*u_light_color[i]*n_dot_v + u_light_color[i]*pow(r_dot_v, u_shine)*intensity;
		}
	}

	else{
		vec3 L;
		float intensity;
		vec3 L_unnorm = u_mlight_pos - v_world_position;
		float d = length(L_unnorm);


		if(u_mtype == 1){
			L = normalize(u_mlight_pos - v_world_position);
			intensity = u_mlight_intensity/(d*d);
		}

		else if(u_mtype == 2){
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

		else if(u_mtype == 3){
			if(real_depth <= texture(u_shadowmap,proj_coords).r){
				L = normalize(u_mlight_dir);
				intensity = u_mlight_intensity;
			
			}
		}
		
		vec3 R = reflect(L,normal);
		float r_dot_v = clamp(dot(R, normalize(normal)),0.0,1.0);
		float n_dot_v = clamp(dot(L, normalize(normal)),0.0,1.0);
		
		light_component += intensity*u_mlight_color*n_dot_v + u_mlight_color*pow(r_dot_v, u_shine)*intensity;


	}

	light_component +=u_ambient_light;

/*
	vec3 rgb = color.xyz;
	
	float lum = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
	float LL = (u_scale / u_average_lum) * lum;
	float Ld = (LL * (1.0 + LL/ u_lumwhite2)) / (1.0 + LL);

	rgb = (rgb / lum) * Ld;
	rgb = max(rgb,vec3(0.001));
	rgb = pow( rgb, vec3( u_igamma ) );
	color = vec4( rgb, color.a );
*/
	if(color.a < u_alpha_cutoff)
		discard;


	FragColor = color * vec4(light_component, 1.0);
	
}


\pbr.fs

#version 330 core

#include "PBR_functions"

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
  float invmax = inversesqrt(max(dot(T,T), dot(B,B)));
  return mat3(T * invmax, B * invmax, N);
}

vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel){
	mat3 TBN = cotangentFrame(N, WP, uv);
	return normalize(TBN*normal_pixel);
}

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_color;
uniform sampler2D u_albedo_map;
uniform sampler2D u_metallic_roughness_map;
uniform sampler2D u_normal_map;
uniform float u_alpha_cutoff;

uniform vec3 u_light_pos[10];
uniform vec3 u_light_color[10];
uniform vec3 u_light_dir[10];
uniform float u_light_intensity[10];
uniform int u_type[10];

uniform vec3 u_camera_position;
uniform vec3 u_ambient_light;
uniform float u_alpha_max;
uniform float u_alpha_min;

uniform sampler2D u_shadowmap;
uniform mat4 u_shadowvp;
uniform float u_shadow_bias;

out vec4 FragColor;

void main()
{
	// Calculate view direction
	vec3 V = normalize(u_camera_position - v_world_position);
	
	// Sample textures
	vec4 albedo = u_color * texture(u_albedo_map, v_uv);
	vec4 metallic_roughness = texture(u_metallic_roughness_map, v_uv);
	
	// Get material properties
	float roughness = metallic_roughness.g;
	float metallic = metallic_roughness.b;
	float ao = metallic_roughness.r;
	
	// Process normal map
	vec3 texture_normal = texture(u_normal_map, v_uv).xyz;
	texture_normal = (texture_normal * 2.0) - 1.0;
	texture_normal = normalize(texture_normal);
	vec3 N = perturbNormal(normalize(v_normal), v_world_position, v_uv, texture_normal);
	
	// Initialize lighting
	vec3 lighting = vec3(0.0);
	
	// Process each light
	for(int i = 0; i < 4; i++) {
		vec3 L;
		float attenuation = 1.0;
		
		// Calculate light direction and attenuation based on light type
		if(u_type[i] == 1) { // Point light
			vec3 L_unnorm = u_light_pos[i] - v_world_position;
			float distance = length(L_unnorm);
			L = normalize(L_unnorm);
			attenuation = 1.0 / (distance * distance);
		}
		else if(u_type[i] == 2) { // Spot light
			vec3 L_unnorm = u_light_pos[i] - v_world_position;
			float distance = length(L_unnorm);
			L = normalize(L_unnorm);
			attenuation = 1.0 / (distance * distance);
			
			float cos_angle = dot(L, normalize(u_light_dir[i]));
			if(cos_angle < cos(u_alpha_max)) {
				attenuation = 0.0;
			}
			else {
				attenuation *= 1.0 - clamp((cos_angle - cos(u_alpha_min)) / 
					(cos(u_alpha_max) - cos(u_alpha_min)), 0.0, 1.0);
			}
		}
		else if(u_type[i] == 3) { // Directional light
			L = normalize(u_light_dir[i]);
			
			// Shadow calculation
			vec4 proj_pos = u_shadowvp * vec4(v_world_position, 1.0);
			float real_depth = (proj_pos.z - u_shadow_bias) / proj_pos.w;
			proj_pos = proj_pos / proj_pos.w;
			proj_pos = (proj_pos + 1.0) / 2.0;
			vec2 proj_coords = vec2(proj_pos.x, proj_pos.y);
			
			if(real_depth > texture(u_shadowmap, proj_coords).r) {
				attenuation = 0.0;
			}
		}
		
		// Calculate PBR lighting
		vec3 light_contribution = calculatePBRLighting(
			N, V, L, albedo.rgb, metallic, roughness,
			u_light_color[i], u_light_intensity[i], attenuation
		);
		
		lighting += light_contribution;
	}
	
	// Add ambient light
	lighting += u_ambient_light * albedo.rgb;
	
	// Apply alpha cutoff
	if(albedo.a < u_alpha_cutoff)
		discard;
	
	FragColor = vec4(lighting, albedo.a);
}




\gbuffer_fill_fs

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
  float invmax = inversesqrt(max(dot(T,T), dot(B,B)));
  return mat3(T * invmax, B * invmax, N);
}

vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel){
	normal_pixel = normal_pixel * 255./127. -128./127.;
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
uniform sampler2D u_normal_map;
uniform float u_alpha_cutoff;
uniform sampler2D u_metallic_roughness_map;

uniform float u_roughness_factor;
uniform float u_metallic_factor;
uniform vec3 u_emissive_factor;

layout(location = 0) out vec4 gbuffer_albedo;
layout(location = 1) out vec4 gbuffer_normal_mat;


void main()
{


	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, v_uv );

	vec4 metallic_roughness = texture(u_metallic_roughness_map, uv);
	float roughness = metallic_roughness.g * u_roughness_factor;
	float metallic = metallic_roughness.b * u_metallic_factor;
	float ao = metallic_roughness.r;

	vec3 texture_normal = texture(u_normal_map, uv).xyz;

	texture_normal = (texture_normal * 2.0) -1.0;
	texture_normal = normalize(texture_normal);

	vec3 normal =perturbNormal(normalize(v_normal), v_world_position, uv, texture_normal);

	
	normal = normal * vec3(0.5);
	normal = normal + vec3(0.5);
	normal=normalize(v_normal)*0.5+0.5;
	gbuffer_normal_mat = vec4(normal,metallic);
	if(color.a < u_alpha_cutoff)
		discard;
	gbuffer_albedo = vec4(color.rgb, roughness);
}




\quad.fs

#version 330 core

//Return <0 if point is inside the Sphere
//=0 if is in the border
//> if is outside the Sphere
//We asume point p is centered on sphere center
float sdSphere( vec3 p, float s ) {
	return length(p)-s;
}

// Replacements for < and > because math on GPU is fast. They return 1 or 0
float when_lt(float left_side, float right_side) {
	return max(sign(right_side - left_side), 0.0);
}
float when_gt(float left_side, float right_side) {
	return max(sign(left_side - right_side), 0.0);
}

vec3 degamma(vec3 c)
{
	return pow(c,vec3(2.2));
}

vec3 gamma(vec3 c)
{
	return pow(c,vec3(1.0/2.2));
}


in vec2 v_uv;

//Pulse uniforms
uniform vec3 u_pulse_color[5];
uniform float u_pulse_width[5];
uniform vec3 u_pulse_center[5];
uniform float u_pulse_radius[5];
uniform int u_pulse_active[5];
uniform float u_pulse_mixture[5];
uniform int u_pulse_border_width[5];
uniform float u_pulse_grid_width[5];


uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

uniform vec3 u_light_pos[10];
uniform vec3 u_light_color[10];
uniform vec3 u_light_dir[10];
uniform float u_light_intensity[10];
uniform int u_type[10];

uniform float u_shine;
uniform vec3 u_ambient_light;
uniform float u_alpha_max;
uniform float u_alpha_min;
uniform int location;


uniform vec2 u_res_inv;
uniform sampler2D u_gbuffer_color;
uniform sampler2D u_gbuffer_normal;
uniform sampler2D u_gbuffer_depth;
uniform mat4 u_inv_vp_mat;

uniform sampler2D u_shadowmap;
uniform mat4 u_shadowvp;

uniform float u_shadow_bias;

uniform sampler2D u_ssao;
uniform int u_use_ssao;

uniform int u_gamma;

out vec4 FragColor;


void main()
{
	vec2 uv = gl_FragCoord.xy * u_res_inv;
	
	float depth = texture(u_gbuffer_depth, uv).r;
	float depth_clip = depth * 2.0 - 1.0;

	if(depth >= 1)
		discard;
		
	vec2 uv_clip = uv * 2.0 - 1.0;
	vec4 clip_coords = vec4(uv_clip.x, uv_clip.y, depth_clip, 1.0);

	vec4 not_norm_world = u_inv_vp_mat * clip_coords;

	vec3 world_position = not_norm_world.xyz / not_norm_world.w;
	
	vec3 v3_color;
	if(u_gamma == 1)
		v3_color = degamma(texture(u_gbuffer_color, uv).xyz);
	else
		v3_color = texture(u_gbuffer_color, uv).xyz;

	vec4 color = vec4(v3_color, 1.0);
	

	vec3 normal = texture(u_gbuffer_normal, uv).xyz;
	normal=(normal*2-vec3(1.0));
	vec3 light_component = vec3(0.0);


	vec4 proj_pos =u_shadowvp*vec4(world_position,1.0);
	float real_depth=(proj_pos.z-u_shadow_bias)/proj_pos.w;
	proj_pos=proj_pos/proj_pos.w;
	proj_pos=(proj_pos+1)/2;
	real_depth=(real_depth+1)/2;
	vec2 proj_coords = vec2(proj_pos.x,proj_pos.y);

	for(int i = 0; i < 4; i++){
		vec3 L;
		float intensity;
		vec3 L_unnorm = u_light_pos[i] - world_position;
		float d = length(L_unnorm);

		
		if(u_type[i] == 1){
			L = normalize(u_light_pos[i] - world_position);
			intensity = u_light_intensity[i]/(d*d);
		}

		else if(u_type[i] == 2){
			vec3 D = normalize(u_light_dir[i]);
			intensity = u_light_intensity[i]/(d*d);
			L = normalize(u_light_pos[i] - world_position);
			if(dot(L,D)<cos(u_alpha_max)){
				intensity = 0.0;
			}
			else {
				intensity *= 1 - clamp((dot(L,D) - cos(u_alpha_min))/(cos(u_alpha_max) - cos(u_alpha_min)), 0.0, 1.0);
			}
		}

		else if(u_type[i] == 3){
				if(real_depth <= texture(u_shadowmap,proj_coords).r){
					L = normalize(u_light_dir[i]);
					intensity = u_light_intensity[i];
				}
		}		
		
		vec3 R = reflect(L,normal);
		float r_dot_v = clamp(dot(R, normalize(normal)),0.0,1.0);
		float n_dot_v = clamp(dot(L, normalize(normal)),0.0,1.0);
		
		light_component += intensity*u_light_color[i]*n_dot_v + u_light_color[i]*pow(r_dot_v, u_shine)*intensity;
	}


	if(u_use_ssao == 1){
		float ssao_value = texture(u_ssao, uv).r;
		light_component +=u_ambient_light * ssao_value;
	}
	else{
		light_component +=u_ambient_light;

	}


	if(	color.a < 0.9 && 
	floor(mod(gl_FragCoord.x,2.0)) !=
	floor(mod(gl_FragCoord.y,2.0)) )
		discard;


	
	float max_mix=0.0;
	float mix_ratio = 0.0;
	vec3 pulse_color=vec3(0.0);
	float min_radius=100.0;
	vec3 bordercol=vec3(0.0);
	int borderwidth=0;
	float gridwidth=0.0;
	float border_mix=0.0;

	for(int i=0;i<5;i++){
		if(u_pulse_active[i]==1){
			vec3 adjusted_position = world_position-u_pulse_center[i];
			float dist = sdSphere(adjusted_position, u_pulse_radius[i]);
			
			float check = when_lt(dist, 0.0) * when_gt(dist, -u_pulse_width[i]);
			float percentage = abs(dist) / abs(u_pulse_width[i]);
			mix_ratio = u_pulse_mixture[i] * check - percentage;
			mix_ratio = clamp(mix_ratio, 0.0, 1.0);
			if(mix_ratio>max_mix){
				pulse_color=u_pulse_color[i];
				max_mix=mix_ratio;
				
			}
			if(dist<0){
				if(u_pulse_radius[i]<min_radius){
					min_radius=u_pulse_radius[i];
					bordercol=u_pulse_color[i];
					borderwidth=u_pulse_border_width[i];
					gridwidth=u_pulse_grid_width[i];
					border_mix=u_pulse_mixture[i];
				}
			}
		}
		
	}

	float neigh_bdepth[8];
	float neigh_gdepth[8];
	vec2 new_coords;
	vec2 uv_1;

	vec2 offsetsborder[8] = vec2[](
    vec2(-borderwidth, -borderwidth),
    vec2( 0.0, -borderwidth),
    vec2( borderwidth, -borderwidth),
    vec2(-borderwidth,  0.0),
    vec2( borderwidth,  0.0),
    vec2(-borderwidth,  borderwidth),
    vec2( 0.0,  borderwidth),
    vec2( borderwidth,  borderwidth)
);
	vec2 offsetsgrid[8] = vec2[](
    vec2(-gridwidth, -gridwidth),
    vec2( 0.0, -gridwidth),
    vec2( gridwidth, -gridwidth),
    vec2(-gridwidth,  0.0),
    vec2( gridwidth,  0.0),
    vec2(-gridwidth,  gridwidth),
    vec2( 0.0,  gridwidth),
    vec2( gridwidth,  gridwidth)
);
	
	for(int i=0;i<8;i++){
		new_coords=gl_FragCoord.xy+offsetsborder[i];
		uv_1 = new_coords * u_res_inv;
    	neigh_bdepth[i] = texture(u_gbuffer_depth, uv_1).r;
		new_coords=gl_FragCoord.xy+offsetsgrid[i];
		uv_1 = new_coords * u_res_inv;
    	neigh_gdepth[i] = texture(u_gbuffer_depth, uv_1).r;
	}
	float depth_bdif=0.0;
	float depth_gdif=0-0;
	for(int i=0;i<8;i++){
		depth_bdif+=neigh_bdepth[i];
		depth_gdif+=neigh_gdepth[i];
	}
	
	if(depth_bdif/8.0>depth+0.0001||depth_bdif/8.0+0.0001<depth){
		depth_bdif=border_mix;
	}
	else depth_bdif=0;
	if(depth_gdif/8.0>depth&&gridwidth!=0){
		depth_gdif=0.5*border_mix;
	}
	else depth_gdif=0;

	if(bordercol==vec3(0.0)) {
		depth_bdif=0;
		depth_gdif=0;
	}
	

	if(u_gamma == 1){
		vec3 final_color = gamma(color.rgb * light_component);

		
		FragColor=vec4(mix(mix(mix(final_color,bordercol,depth_gdif),bordercol,depth_bdif),pulse_color,max_mix),1.0);


			
	}

	else{
		vec4 final_color=color * vec4(light_component, 1.0);
		FragColor=mix(mix(mix(final_color,vec4(bordercol,1.0),depth_gdif),vec4(bordercol,1.0),depth_bdif),vec4(pulse_color,1.0),max_mix);
	}
}


\quad_pbr.fs

#version 330 core

#include "PBR_functions"

vec3 degamma(vec3 c)
{
	return pow(c,vec3(2.2));
}

vec3 gamma(vec3 c)
{
	return pow(c,vec3(1.0/2.2));
}


in vec2 v_uv;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

uniform vec3 u_light_pos[10];
uniform vec3 u_light_color[10];
uniform vec3 u_light_dir[10];
uniform float u_light_intensity[10];
uniform int u_type[10];

uniform vec3 u_camera_pos;
uniform float u_shine;
uniform vec3 u_ambient_light;
uniform float u_alpha_max;
uniform float u_alpha_min;
uniform int location;



uniform vec2 u_res_inv;
uniform sampler2D u_gbuffer_color;
uniform sampler2D u_gbuffer_normal;
uniform sampler2D u_gbuffer_depth;
uniform mat4 u_inv_vp_mat;

uniform sampler2D u_shadowmap;
uniform mat4 u_shadowvp;

uniform float u_shadow_bias;

uniform sampler2D u_ssao;
uniform int u_use_ssao;

uniform int u_gamma;

out vec4 FragColor;


void main()
{
	vec2 uv = gl_FragCoord.xy * u_res_inv;
	
	float depth = texture(u_gbuffer_depth, uv).r;
	float depth_clip = depth * 2.0 - 1.0;

	if(depth >= 1)
		discard;
		
	vec2 uv_clip = uv * 2.0 - 1.0;
	vec4 clip_coords = vec4(uv_clip.x, uv_clip.y, depth_clip, 1.0);

	vec4 not_norm_world = u_inv_vp_mat * clip_coords;

	vec3 world_position = not_norm_world.xyz / not_norm_world.w;
	
	vec3 v3_color;
	if(u_gamma == 1)
		v3_color = degamma(texture(u_gbuffer_color, uv).xyz);
	else
		v3_color = texture(u_gbuffer_color, uv).xyz;

	vec4 color = vec4(v3_color, 1.0);
	

	vec3 normal = texture(u_gbuffer_normal, uv).xyz;
	normal=(normal*2-vec3(1.0));
	vec3 light_component = vec3(0.0);


	// Calculate view direction
	vec3 V = normalize(u_camera_pos - world_position);
	
	// Get material properties
	float roughness = texture(u_gbuffer_color, uv).a;
	float metallic = texture(u_gbuffer_normal, uv).a;


	//Shadow Maps
	vec4 proj_pos =u_shadowvp*vec4(world_position,1.0);
	float real_depth=(proj_pos.z-u_shadow_bias)/proj_pos.w;
	proj_pos=proj_pos/proj_pos.w;
	proj_pos=(proj_pos+1)/2;
	real_depth=(real_depth+1)/2;
	vec2 proj_coords = vec2(proj_pos.x,proj_pos.y);

	for(int i = 0; i < 4; i++){
		vec3 L;
		float intensity = 1.0;
		vec3 L_unnorm = u_light_pos[i] - world_position;
		float d = length(L_unnorm);

		
		if(u_type[i] == 1){
			L = normalize(u_light_pos[i] - world_position);
			intensity = 1.0/(d*d);
		}

		else if(u_type[i] == 2){
			vec3 D = normalize(u_light_dir[i]);
			intensity = 1.0/(d*d);
			L = normalize(u_light_pos[i] - world_position);
			if(dot(L,D)<cos(u_alpha_max)){
				intensity = 0.0;
			}
			else {
				intensity *= 1 - clamp((dot(L,D) - cos(u_alpha_min))/(cos(u_alpha_max) - cos(u_alpha_min)), 0.0, 1.0);
			}
		}

		else if(u_type[i] == 3){
				if(real_depth > texture(u_shadowmap,proj_coords).r){
					L = normalize(u_light_dir[i]);
					intensity = 0.0;
				}
		}		
		
		vec3 light_contribution = calculatePBRLighting(
			normal, V, L, color.rgb, metallic, roughness,
			u_light_color[i], u_light_intensity[i], intensity
		);

		vec3 R = reflect(L,normal);
		float r_dot_v = clamp(dot(R, normalize(normal)),0.0,1.0);
		float n_dot_v = clamp(dot(L, normalize(normal)),0.0,1.0);
		
		light_component += light_contribution;
	}


	if(u_use_ssao == 1){
		float ssao_value = texture(u_ssao, uv).r;
		light_component +=u_ambient_light * ssao_value;
	}
	else{
		light_component +=u_ambient_light;

	}

	if(	color.a < 0.9 && 
	floor(mod(gl_FragCoord.x,2.0)) !=
	floor(mod(gl_FragCoord.y,2.0)) )
		discard;

	if(u_gamma == 1){
		vec3 final_color = gamma(color.rgb * light_component);
		FragColor = vec4(final_color, 1.0);
	}
	else
		FragColor = color * vec4(light_component, 1.0);
	
}


\light.fs

#version 330 core

vec3 degamma(vec3 c)
{
	return pow(c,vec3(2.2));
}

vec3 gamma(vec3 c)
{
	return pow(c,vec3(1.0/2.2));
}


in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;


uniform float u_time;

uniform vec3 u_light_pos;
uniform vec3 u_light_color;
uniform vec3 u_light_dir;
uniform float u_light_intensity;
uniform int u_type;

uniform float u_shine;
uniform vec3 u_ambient_light;
uniform float u_alpha_max;
uniform float u_alpha_min;
uniform int location;


uniform vec2 u_res_inv;
uniform sampler2D u_gbuffer_color;
uniform sampler2D u_gbuffer_normal;
uniform sampler2D u_gbuffer_depth;
uniform mat4 u_inv_vp_mat;

uniform sampler2D u_shadowmap;
uniform mat4 u_shadowvp;

uniform float u_shadow_bias;

uniform sampler2D u_ssao;
uniform int u_use_ssao;

out vec4 FragColor;

void main()
{
	vec2 uv = gl_FragCoord.xy * u_res_inv;
	
	float depth = texture(u_gbuffer_depth, uv).r;
	float depth_clip = depth * 2.0 - 1.0;

	if(depth >= 1)
		discard;
		
	vec2 uv_clip = uv * 2.0 - 1.0;
	vec4 clip_coords = vec4(uv_clip.x, uv_clip.y, depth_clip, 1.0);

	vec4 not_norm_world = u_inv_vp_mat * clip_coords;

	vec3 world_position = not_norm_world.xyz / not_norm_world.w;
	

	 
	vec3 v3_color = (texture(u_gbuffer_color, uv).xyz);
	vec4 color = vec4(v3_color, 1.0);
	

	vec3 normal = texture(u_gbuffer_normal, uv).xyz;
	normal=(normal*2-vec3(1.0));
	vec3 light_component = vec3(0.0);


	vec4 proj_pos =u_shadowvp*vec4(world_position,1.0);
	float real_depth=(proj_pos.z-u_shadow_bias)/proj_pos.w;
	proj_pos=proj_pos/proj_pos.w;
	proj_pos=(proj_pos+1)/2;
	real_depth=(real_depth+1)/2;
	vec2 proj_coords = vec2(proj_pos.x,proj_pos.y);

	vec3 L;
	float intensity;
	vec3 L_unnorm = u_light_pos - world_position;
	float d = length(L_unnorm);


	if(u_type == 1){
		L = normalize(u_light_pos - world_position);
		intensity = u_light_intensity/(d*d);
	}

	else if(u_type == 2){
		vec3 D = normalize(u_light_dir);
		intensity = u_light_intensity/(d*d);
		L = normalize(u_light_pos - world_position);
		if(dot(L,D)<cos(u_alpha_max)){
			intensity = 0.0;
		}
		else {
			intensity *= 1 - clamp((dot(L,D) - cos(u_alpha_min))/(cos(u_alpha_max) - cos(u_alpha_min)), 0.0, 1.0);
		}
	}

	else if(u_type == 3){
		if(real_depth <= texture(u_shadowmap,proj_coords).r){
			L = normalize(u_light_dir);
			intensity = u_light_intensity;
		}
	}
	
	
	vec3 R = reflect(L,normal);
	float r_dot_v = clamp(dot(R, normalize(normal)),0.0,1.0);
	float n_dot_v = clamp(dot(L, normalize(normal)),0.0,1.0);
	
	light_component += intensity*u_light_color*n_dot_v + u_light_color*pow(r_dot_v, u_shine)*intensity;

	if(u_use_ssao == 1){
		float ssao_value = texture(u_ssao, uv).r;
		light_component +=u_ambient_light * ssao_value;
	}
	else{
		light_component +=u_ambient_light;

	}


	if(	color.a < 0.9 && 
		floor(mod(gl_FragCoord.x,2.0)) !=
		floor(mod(gl_FragCoord.y,2.0)) )
		discard;

	vec3 final_color = (color.rgb * light_component);
	FragColor = vec4(final_color, 1.0);
	
}

\skybox.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;

uniform samplerCube u_texture;
uniform vec3 u_camera_position;
out vec4 gbuffer_albedo;

void main()
{
	vec3 E = v_world_position - u_camera_position;
	vec4 color = texture( u_texture, E );
	gbuffer_albedo = color;
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



\ambient.fs

#version 330 core


in vec2 v_uv;


uniform int u_sample_count;
uniform float u_sample_radius;
uniform vec3 u_sample_pos[30];

uniform mat4 u_p_mat;
uniform mat4 u_inv_p_mat;
uniform vec2 u_res_inv;
uniform sampler2D u_normal_text;
uniform mat4 u_view;

uniform sampler2D u_gbuffer_depth;


out vec4 FragColor;

void main(){

	vec2 uv = v_uv + 0.5 * u_res_inv;

	
	float depth = texture(u_gbuffer_depth, uv).r;

	if (depth >= 1.0) {
		FragColor = vec4(1.0);
		return;
	}

	vec4 clip_coords = vec4(uv, depth, 1.0);
	clip_coords.xyz = clip_coords.xyz * 2.0 - 1.0;

	vec4 view_sample_origin = u_inv_p_mat * clip_coords;
	view_sample_origin /= view_sample_origin.w;

	vec3 N=texture(u_normal_text,uv).rgb*2.0-1.0;
	N = vec3(u_view * vec4(N, 0.0));
	vec3 v= vec3(0.0, 1.0, 0.0);

	vec3 T = normalize(v - N * dot(v, N));
	vec3 B = cross(N, T);

	mat3 rotmat = mat3(T, B, N);

	float ao_term = 0.0;

	for(int i = 0; i < u_sample_count; i++) {
		vec3 view_sample = rotmat*u_sample_pos[i];
		//view_sample *= u_sample_radius;
		view_sample += view_sample_origin.xyz;

		vec4 proj_sample = u_p_mat * vec4(view_sample, 1.0);
  		proj_sample /= proj_sample.w;


		vec2 sample_uv = proj_sample.xy * 0.5 + 0.5;
		float sample_depth = texture(u_gbuffer_depth, sample_uv).r;
		sample_depth=sample_depth*2.0-1.0;



		if(sample_depth > proj_sample.z){	
			ao_term+=1;
		}
	}
	ao_term /= float(u_sample_count);
	FragColor = vec4(vec3(ao_term),1.0);


}
\PBR_functions

const float PI = 3.14159265359;
const float EPSILON = 0.00001;

// Calculate F0 (base reflectivity) based on material properties
vec3 calculateF0(vec3 albedo, float metallic) {
    return mix(vec3(0.04), albedo, metallic);
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.00001);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / (denom);
    //return nom / (denom + EPSILON);

}

// Geometry function (Smith's method with Schlick-GGX)
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness);
    float k = (r * r) / 2.0;

    //float r = (roughness + 1.0);
    //float k = (r * r) / 8.0;


    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / (denom);
    //return nom / (denom + EPSILON);

}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.00001);
    float NdotL = max(dot(N, L), 0.00001);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Cook-Torrance BRDF
vec3 cookTorranceBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness) {
    vec3 H = normalize(V + L);
    
    // Calculate F0
    vec3 F0 = calculateF0(albedo, metallic);
    
    // Calculate dot products
    float NdotL = max(dot(N, L), 0.00001);
    float NdotV = max(dot(N, V), 0.00001);
    float NdotH = max(dot(N, H), 0.00001);
    float HdotV = max(dot(H, V), 0.00001);
    
    // Calculate BRDF terms
    float D = distributionGGX(N, H, roughness);
    vec3 F = fresnelSchlick(HdotV, F0);
    float G = geometrySmith(N, V, L, roughness);
    
    // Calculate specular BRDF
    vec3 numerator = F * D * G;
    float denominator = 4.0 * NdotV * NdotL;
    //float denominator = 4.0 * NdotV * NdotL + EPSILON;

    vec3 specular = numerator / denominator;
    
    // Calculate diffuse BRDF (Lambertian)
    vec3 diffuse = albedo / PI;
    
    // Combine diffuse and specular based on metallic value
    // Scale up the diffuse term for better visibility
    //return diffuse + specular;
    return (diffuse + specular) * NdotL;
}

// Calculate final lighting
vec3 calculatePBRLighting(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, 
                         vec3 lightColor, float lightIntensity, float attenuation) {
    vec3 brdf = cookTorranceBRDF(N, V, L, albedo, metallic, roughness);
    // Match the specification: Lo = (f_diffuse * (1.0 - metalness) + f_specular) * radiance * NdotL * attenuation * shadow
    return brdf * lightColor * lightIntensity * attenuation;
}