#include "renderer.h"

#include <algorithm> //sort

#include "camera.h"
#include "../gfx/gfx.h"
#include "../gfx/shader.h"
#include "../gfx/mesh.h"
#include "../gfx/texture.h"
#include "../gfx/fbo.h"
#include "../pipeline/prefab.h"
#include "../pipeline/material.h"
#include "../pipeline/animation.h"
#include "../utils/utils.h"
#include "../extra/hdre.h"
#include "../core/ui.h"
#include "../pipeline/light.h"

#include "scene.h"


using namespace SCN;

//some globals





// Renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, bool transparent,Camera light_cam)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	if(pbr && single_pass && !transparent)
		shader = GFX::Shader::Get("pbr");
	else {
		shader = GFX::Shader::Get("texture");
		if(!transparent)
			pbr = false;
	}
    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	material->bind(shader);



	//Sending the lights
	vec3* light_pos = new vec3[light_list.size()];
	vec3* light_color = new vec3[light_list.size()];
	float* light_intensity = new float[light_list.size()];
	vec3* light_dir = new vec3[light_list.size()];
	int* light_type = new int[light_list.size()];
	float alpha_min;
	float alpha_max;
	
	int i = 0u;
	for (LightEntity* light : light_list) {
		light_pos[i] = light->root.getGlobalMatrix().getTranslation();
		light_intensity[i] = light->intensity;
		light_color[i] = light->color;
		light_dir[i] = light->root.model.frontVector();
		light_type[i] = light->light_type;
		if (light->light_type == 2){
			alpha_min = light->cone_info.x * 6.28/360;
			alpha_max = light->cone_info.y * 6.28/360;
		}
		i++;
	}
	


	/*GFX::Texture* hdr_texture = ssao ? ssao_FBO->color_textures[0] : light_fbo->color_textures[0];
	int width = hdr_texture->width;
	int height = hdr_texture->height;
	std::vector<Color> pixels(width * height);

	float total_luminance = 0.0f;
	float max_luminance = 0.0f;

	for (const Color& color : pixels) {
		float lum = color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f;
		total_luminance += lum;
		if (lum > max_luminance) {
			max_luminance = lum;
		}
	}

	float u_average_lum = total_luminance / (width * height);
	float u_lumwhite2 = max_luminance * max_luminance;
	float u_scale = 1.0f;
	float u_igamma = 1.0f / 2.2f;
	shader->setUniform("u_scale", u_scale);
	shader->setUniform("u_average_lum", u_average_lum);
	shader->setUniform("u_lumwhite2", u_lumwhite2);
	shader->setUniform("u_igamma", u_igamma);
	shader->setUniform("u_texture", hdr_texture, 0);
	*/
	if (!single_pass || transparent) {
		shader->setUniform("u_shadowmap", fbo->depth_texture, 4);
		shader->setUniform("u_shadowvp", light_cam.viewprojection_matrix);
		shader->setUniform("u_single_pass", 0);
		glDepthFunc(GL_LEQUAL);

		glBlendFunc(GL_SRC_ALPHA, GL_ONE);

		for (int i = 0; i < light_list.size(); i++) {
			if (i == 0)
				if (!transparent)
					glDisable(GL_BLEND);
				else
					glEnable(GL_BLEND);
			else
				glEnable(GL_BLEND);
			shader->setUniform("u_mlight_pos", light_pos[i]);
			shader->setUniform("u_mlight_color", light_color[i]);
			shader->setUniform("u_mlight_intensity", light_intensity[i]);
			shader->setUniform("u_mlight_dir", light_dir[i]);
			shader->setUniform("u_mtype", light_type[i]);
			shader->setUniform("u_alpha_min", alpha_min);
			shader->setUniform("u_alpha_max", alpha_max);
			shader->setUniform("u_shadow_bias", shadow_bias);


			if(i!=0)
				shader->setUniform("u_ambient_light", vec3(0.f,0.f,0.f));
			else
				shader->setUniform("u_ambient_light", Scene::instance->ambient_light);

			//upload uniforms
			shader->setUniform("u_model", model);

			// Upload camera uniforms
			shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
			shader->setUniform("u_camera_position", camera->eye);

			float t = getTime();
			shader->setUniform("u_time", t);

			// Render just the verticies as a wireframe
			if (render_wireframe)
				glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

			//do the draw call that renders the mesh into the screen
			mesh->render(GL_TRIANGLES);
		}
		glDisable(GL_BLEND);
		glDepthFunc(GL_LESS);
	}
		
	else {

		shader->setUniform("u_shadowmap", fbo->depth_texture, 4);
		shader->setUniform("u_shadowvp", light_cam.viewprojection_matrix);
		shader->setUniform("u_single_pass", 1);
		shader->setUniform3Array("u_light_pos", (float*)light_pos, min(light_list.size(), 10));
		shader->setUniform3Array("u_light_color", (float*)light_color, min(light_list.size(), 10));
		shader->setUniform1Array("u_light_intensity", (float*)light_intensity, min(light_list.size(), 10));
		shader->setUniform3Array("u_light_dir", (float*)light_dir, min(light_list.size(), 10));
		shader->setUniform1Array("u_type", light_type, min(light_list.size(), 10));
		shader->setUniform("u_alpha_min", alpha_min);
		shader->setUniform("u_alpha_max", alpha_max);

		shader->setUniform("u_shadow_bias", shadow_bias);


		shader->setUniform("u_ambient_light", Scene::instance->ambient_light);

		//upload uniforms
		shader->setUniform("u_model", model);

		// Upload camera uniforms
		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		shader->setUniform("u_camera_position", camera->eye);

		// Upload time, for cool shader effects
		float t = getTime();
		shader->setUniform("u_time", t);

		// Render just the verticies as a wireframe
		if (render_wireframe)
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		//do the draw call that renders the mesh into the screen
		mesh->render(GL_TRIANGLES);
	}
	//disable shader

	delete[] light_pos;
	delete[] light_color;
	delete[] light_intensity;
	delete[] light_dir;
	delete[] light_type;


	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}


void Renderer::fillGBuff(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("fill");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	material->bind(shader);
	glDisable(GL_BLEND);

	//upload uniforms
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	// Upload time, for cool shader effects
	float t = getTime();
	shader->setUniform("u_time", t);

	// Render just the verticies as a wireframe
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);
	
	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}




void Renderer::renderDeferred(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, Camera* cam) {
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	glEnable(GL_DEPTH_TEST);

	GFX::Mesh* quad = GFX::Mesh::getQuad();

	Camera* camera = Camera::current;

	//chose a shader
	if(pbr)
		shader = GFX::Shader::Get("quad_pbr");
	else
		shader = GFX::Shader::Get("quad");
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	material->bind(shader);
	shader->setUniform("u_model", model);

	texture_slots = 0;


	vec3* light_pos = new vec3[light_list.size()];
	vec3* light_color = new vec3[light_list.size()];
	float* light_intensity = new float[light_list.size()];
	vec3* light_dir = new vec3[light_list.size()];
	int* light_type = new int[light_list.size()];
	float alpha_min;
	float alpha_max;
	
	int i = 0u;
	for (LightEntity* light : light_list) {
		light_pos[i] = light->root.getGlobalMatrix().getTranslation();
		light_intensity[i] = light->intensity;
		light_color[i] = light->color;
		light_dir[i] = light->root.model.frontVector();
		light_type[i] = light->light_type;
		if (light->light_type == 2) {
			alpha_min = light->cone_info.x * 6.28 / 360;
			alpha_max = light->cone_info.y * 6.28 / 360;
		}
		i++;
	}


	// Upload camera uniforms
	shader->setUniform3Array("u_light_pos", (float*)light_pos, min(light_list.size(), 10));
	shader->setUniform3Array("u_light_color", (float*)light_color, min(light_list.size(), 10));
	shader->setUniform1Array("u_light_intensity", (float*)light_intensity, min(light_list.size(), 10));
	shader->setUniform3Array("u_light_dir", (float*)light_dir, min(light_list.size(), 10));
	shader->setUniform1Array("u_type", light_type, min(light_list.size(), 10));
	shader->setUniform("u_alpha_min", alpha_min);
	shader->setUniform("u_alpha_max", alpha_max);

	if(gamma)
		shader->setUniform("u_gamma", 1);
	else
		shader->setUniform("u_gamma", 0);


	shader->setUniform("u_ambient_light", Scene::instance->ambient_light);

	shader->setUniform("u_shadowmap", fbo->depth_texture, 2);
	shader->setUniform("u_shadowvp", cam->viewprojection_matrix);
	shader->setUniform("u_shadow_bias", shadow_bias);

	// Render just the verticies as a wireframe
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	float t = getTime();
	shader->setUniform("u_time", t);


	shader->setTexture("u_gbuffer_color", gbuffer_fbo->color_textures[0], texture_slots++);
	shader->setTexture("u_gbuffer_normal", gbuffer_fbo->color_textures[1], texture_slots++);
	shader->setTexture("u_gbuffer_depth", gbuffer_fbo->depth_texture, texture_slots++);
	if (ssao || ssao_plus) {
		shader->setTexture("u_ssao", ssao_FBO->color_textures[0], texture_slots++);
		shader->setUniform("u_use_ssao", 1);
	}
	else
		shader->setUniform("u_use_ssao", 0);

	
	shader->setUniform("u_res_inv", vec2(1.0f / CORE::getWindowSize().x, 1.0f / CORE::getWindowSize().y));
	shader->setUniform("u_inv_vp_mat", camera->inverse_viewprojection_matrix);


	quad->render(GL_TRIANGLES);

	delete[] light_pos;
	delete[] light_color;
	delete[] light_intensity;
	delete[] light_dir;
	delete[] light_type;


	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);

	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}


void Renderer::renderVolume(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, std::vector<Camera> cam) {


	GFX::Shader* shader = NULL;
	shader = GFX::Shader::Get("light");
	
	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;

	Camera* camera = Camera::current;

	shader->enable();

	LightEntity* light = light_list[3];

	texture_slots = 0;



	material->bind(shader);
	shader->setUniform("u_model", model);

	shader->setUniform("u_light_pos", light->root.getGlobalMatrix().getTranslation());
	shader->setUniform("u_light_color", light->color);
	shader->setUniform("u_light_intensity", light->intensity);
	shader->setUniform("u_light_dir", light->root.model.frontVector());
													   
	shader->setUniform("u_ambient_light", Scene::instance->ambient_light);
													   
	shader->setUniform("u_type", 3);				   
	shader->setTexture("u_gbuffer_color", gbuffer_fbo->color_textures[0], texture_slots++);
	shader->setTexture("u_gbuffer_normal", gbuffer_fbo->color_textures[1], texture_slots++);
	shader->setTexture("u_gbuffer_depth", gbuffer_fbo->depth_texture, texture_slots++);
	
	if (ssao || ssao_plus) {
		shader->setTexture("u_ssao", ssao_FBO->color_textures[0], texture_slots++);
		shader->setUniform("u_use_ssao", 1);
	}
	else
		shader->setUniform("u_use_ssao", 0);



	shader->setUniform("u_res_inv", vec2(1.0f / CORE::getWindowSize().x, 1.0f / CORE::getWindowSize().y));
	shader->setUniform("u_inv_vp_mat", camera->inverse_viewprojection_matrix);


	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);


	shader->setUniform("u_shadowmap", fbo->depth_texture, 8);
	shader->setUniform("u_shadowvp", cam[3].viewprojection_matrix);
	shader->setUniform("u_shadow_bias", shadow_bias);

	// Upload time, for cool shader effects
	float t = getTime();
	shader->setUniform("u_time", t);

	mesh->render(GL_TRIANGLES);

	glDepthFunc(GL_GREATER);
	glDepthMask(GL_FALSE);
	glBlendFunc(GL_ONE, GL_ONE);
	glEnable(GL_BLEND);
	glFrontFace(GL_CW);

	for (int i = 0; i < light_list.size(); i++) {
		light = light_list[i];

		texture_slots = 0;

		int type = light->light_type;
		if (type != 3) {
			// Upload the necessary uniforms.
			
			sphere.radius = light->max_distance;

			shader->setUniform("u_light_pos", light->root.getGlobalMatrix().getTranslation());
			shader->setUniform("u_light_color", light->color);
			shader->setUniform("u_light_intensity", light->intensity);
			shader->setUniform("u_light_dir", light->root.model.frontVector());
			shader->setUniform("u_type", type);
			if (type == 2) {
				shader->setUniform("u_alpha_min", light->cone_info.x);
				shader->setUniform("u_alpha_max", light->cone_info.y);
			}
			
			shader->setUniform("u_ambient_light", vec3(0.f, 0.f, 0.f));


			shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
			shader->setUniform("u_camera_position", camera->eye);


			// Upload time, for cool shader effects
			float t = getTime();
			shader->setUniform("u_time", t);

			// Create the model from the light data.
			mat4 sphere_model;

			vec3 position = light->root.getGlobalMatrix().getTranslation();
			sphere_model.setTranslation(position.x, position.y, position.z);

			shader->setUniform("u_model", sphere_model);

			sphere.render(GL_TRIANGLES);

		}

	}

	//// Return the OpenGL config to what it was
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDisable(GL_BLEND);
	glFrontFace(GL_CCW);

	shader->disable();
}



void Renderer::renderPlain(Camera cam, const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("plain");
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	material->bind(shader);
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", cam.viewprojection_matrix);
	shader->setUniform("u_camera_position", cam.eye);
	
	float t = getTime();
	shader->setUniform("u_time", t);


	mesh->render(GL_TRIANGLES);

	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_CULL_FACE);
	glFrontFace(GL_CCW);

	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}


void Renderer::renderSSAO(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material) {

	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	GFX::Mesh* quad = GFX::Mesh::getQuad();
	GFX::Shader* ao_shader = GFX::Shader::Get("ambient_oclussion");
	if (!ao_shader)
		return;

	Camera* camera = Camera::current;


	if (generate_points) {
	
		if (ssao_plus) {
			ao_sample_points = generateSpherePoints(samples, radius, true);
			ssao = false;
		}
		else if (ssao) {
			ao_sample_points = generateSpherePoints(samples, radius, false);
			ssao_plus = false;
		}
		generate_points = false;
	}

	ao_shader->enable();
	ao_shader->setUniform("u_sample_count", samples);

	ao_shader->setUniform("u_sample_radius", radius);

	ao_shader->setUniform3Array("u_sample_pos",
		(float*)&ao_sample_points[0],
		samples);

	mat4 proj = camera->projection_matrix;

	// Compute the inverse
	mat4 proj_inv = proj;
	proj_inv.inverse();

	ao_shader->setUniform("u_p_mat", proj);
	ao_shader->setUniform("u_inv_p_mat", proj_inv);
	ao_shader->setUniform("u_normal_text", gbuffer_fbo->color_textures[1], 1);
	ao_shader->setUniform("u_view", camera->view_matrix);

	// Send the inverse of the FBO res, for the UVs
	float inv_width = 1.0f / ssao_FBO->color_textures[0]->width;
	float inv_height = 1.0f / ssao_FBO->color_textures[0]->height;
	vec2 res_inv = vec2(inv_width, inv_height);
	ao_shader->setUniform("u_res_inv", res_inv);

	ao_shader->setTexture("u_gbuffer_depth", gbuffer_fbo->depth_texture, 7);


	quad->render(GL_TRIANGLES);

	ao_shader->disable();
}