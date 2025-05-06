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
	shader = GFX::Shader::Get("texture");

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
	float shadow_bias = 0.001f;
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
	
	
	if (!single_pass) {
		shader->setUniform("u_shadowmap", fbo->depth_texture, 2);
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

		shader->setUniform("u_shadowmap", fbo->depth_texture, 2);
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




void Renderer::renderDeferred(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material) {
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	glEnable(GL_DEPTH_TEST);

	GFX::Mesh* quad = GFX::Mesh::getQuad();

	Camera* camera = Camera::current;

	//chose a shader
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



	vec3* light_pos = new vec3[light_list.size()];
	vec3* light_color = new vec3[light_list.size()];
	float* light_intensity = new float[light_list.size()];
	vec3* light_dir = new vec3[light_list.size()];
	int* light_type = new int[light_list.size()];
	float alpha_min;
	float alpha_max;
	float shadow_bias = 0.001f;
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


	shader->setUniform("u_ambient_light", Scene::instance->ambient_light);

	//upload uniforms
	shader->setUniform("u_model", model);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	// Render just the verticies as a wireframe
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	float t = getTime();
	shader->setUniform("u_time", t);


	shader->setTexture("u_gbuffer_color", gbuffer_fbo->color_textures[0], texture_slots++);
	shader->setTexture("u_gbuffer_normal", gbuffer_fbo->color_textures[1], texture_slots++);
	shader->setTexture("u_gbuffer_depth", fbo->depth_texture, texture_slots++);
	
	shader->setUniform("u_res_inv", vec2(1.0f / 1024, 1.0f / 1024));
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
