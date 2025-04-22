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
GFX::Mesh sphere;

Renderer::Renderer(const char* shader_atlas_filename)
{
	render_wireframe = false;
	render_boundaries = false;
	scene = nullptr;
	skybox_cubemap = nullptr;

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();

	texture = new GFX::Texture(1024,1024);
	fbo = new GFX::FBO();
	fbo->setTexture(texture);
	fbo->setDepthOnly(1024, 1024);
    
	// Initialize shadow map FBOs
	for (int i = 0; i < MAX_SHADOW_MAPS; i++) {
		shadow_textures[i] = new GFX::Texture(1024, 1024);
		shadow_fbos[i] = new GFX::FBO();
		shadow_fbos[i]->setTexture(shadow_textures[i]);
		shadow_fbos[i]->setDepthOnly(1024, 1024);
	}
	active_shadow_maps = 0;
}

void Renderer::setupScene()
{
	if (scene->skybox_filename.size())
		skybox_cubemap = GFX::Texture::Get(std::string(scene->base_folder + "/" + scene->skybox_filename).c_str());
	else
		skybox_cubemap = nullptr;
}

void Renderer::parseNodes(SCN::Node* node, Camera* cam) {
	if (!node) return;



	if(node->mesh){
		Matrix44 global_matrix = node->getGlobalMatrix();
		float distance = cam->eye.distance(global_matrix.getTranslation());
		bool must_render = true;
		must_render &= (distance < 30);
		Vector3f bb_center = global_matrix * node->aabb.center;
		Vector3f bb_halfsize = global_matrix * node->aabb.halfsize;
		cam->extractFrustum();
		must_render &= (cam->testBoxInFrustum(bb_center, bb_halfsize) != CLIP_OUTSIDE);
		if (must_render) {
			sDrawCommand values;
			values.mesh = node->mesh;
			values.model = node->getGlobalMatrix();
			values.material = node->material;
			values.distance = distance;
			if (values.material->alpha_mode == BLEND) {
				transparent_to_render.push_back(values);
				std::sort(transparent_to_render.begin(), transparent_to_render.end(), [](const sDrawCommand& s1, const sDrawCommand& s2) {
					return s1.distance > s2.distance;
					});

			}
			else {
				entities_to_render.push_back(values);
				std::sort(entities_to_render.begin(), entities_to_render.end(), [](const sDrawCommand& s1, const sDrawCommand& s2) {
					return s1.distance < s2.distance;
					});
			}
		}
	}

	for (SCN::Node* child : node->children) {
		parseNodes(child , cam);
	}

}

void Renderer::parseSceneEntities(SCN::Scene* scene, Camera* cam) {
	// HERE =====================
	// TODO: GENERATE RENDERABLES
	// ==========================

	entities_to_render.clear();
	transparent_to_render.clear();
	light_list.clear();
	for (int i = 0; i < scene->entities.size(); i++) {
		BaseEntity* entity = scene->entities[i];

		if (!entity->visible) {
			continue;
		}

		
		if (entity->getType() == eEntityType::PREFAB) {
			parseNodes(&((PrefabEntity*)entity)->root, cam);
		}
		else if (entity->getType() == eEntityType::LIGHT) light_list.push_back( (LightEntity*) entity);

		// Store Lights
		// ...
	}
	
}

void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	setupScene();

	parseSceneEntities(scene, camera);

	// Reset active shadow maps count
	active_shadow_maps = 0;

	// Generate shadow maps for different lights that cast shadows
	for (int i = 0; i < light_list.size() && active_shadow_maps < MAX_SHADOW_MAPS; i++) {
		SCN::LightEntity* light = light_list[i];
		
		// Skip lights that don't cast shadows
		if (!light->cast_shadows)
			continue;

		mat4 light_model = light->root.getGlobalMatrix();
		vec3 light_pos = light_model.getTranslation();
		vec3 light_dir = light_model.frontVector();

		// Set up light camera based on light type
		switch (light->light_type) {
			case eLightType::DIRECTIONAL: {
				// Orthographic projection for directional lights
				float half_size = light->area / 2.0f;
				light_cameras[active_shadow_maps].lookAt(light_pos, light_pos + light_dir, vec3(0.0f, 1.0f, 0.0f));
				light_cameras[active_shadow_maps].setOrthographic(
					-half_size, half_size, 
					-half_size, half_size, 
					light->near_distance, 
					light->max_distance
				);
				break;
			}
			case eLightType::SPOT: {
				// Perspective projection for spot lights
				float aspect = 1.0f; // Shadow maps are square
				float fov = light->cone_info.y * 2.0f; // Use the cone angle as FOV
				light_cameras[active_shadow_maps].lookAt(light_pos, light_pos + light_dir, vec3(0.0f, 1.0f, 0.0f));
				light_cameras[active_shadow_maps].setPerspective(
					fov, 
					aspect, 
					light->near_distance, 
					light->max_distance
				);
				break;
			}
			default:
				continue; // Skip unsupported light types
		}

		// Render shadow map for this light
		shadow_fbos[active_shadow_maps]->bind();
		glColorMask(false, false, false, false);
		glClear(GL_DEPTH_BUFFER_BIT);

		for (sDrawCommand &render_call : entities_to_render) {
			renderPlain(light_cameras[active_shadow_maps], render_call.model, render_call.mesh, render_call.material);
		}

		glColorMask(true, true, true, true);
		shadow_fbos[active_shadow_maps]->unbind();
		
		active_shadow_maps++;
	}

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if(skybox_cubemap)
		renderSkybox(skybox_cubemap);
	
	for(sDrawCommand draw : entities_to_render){
		renderMeshWithMaterial(draw.model, draw.mesh, draw.material, false, light_cameras);
	}

	for (sDrawCommand draw : transparent_to_render) {
		renderMeshWithMaterial(draw.model, draw.mesh, draw.material, true, light_cameras);
	}
}


void Renderer::renderSkybox(GFX::Texture* cubemap)
{
	Camera* camera = Camera::current;

	// Apply skybox necesarry config:
	// No blending, no dpeth test, we are always rendering the skybox
	// Set the culling aproppiately, since we just want the back faces
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GFX::Shader* shader = GFX::Shader::Get("skybox");
	if (!shader)
		return;
	shader->enable();

	// Center the skybox at the camera, with a big sphere
	Matrix44 m;
	m.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	m.scale(10, 10, 10);
	shader->setUniform("u_model", m);

	// Upload camera uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);

	shader->setUniform("u_texture", cubemap, 0);

	sphere.render(GL_TRIANGLES);

	shader->disable();

	// Return opengl state to default
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}

// Renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, bool transparent, Camera light_cameras[])
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
	int* light_shadow_map_index = new int[light_list.size()];
	float alpha_min;
	float alpha_max;
	float shadow_bias = 0.01f;
	int i = 0u;
	int shadow_map_index = 0;
	
	for (LightEntity* light : light_list) {
		light_pos[i] = light->root.getGlobalMatrix().getTranslation();
		light_intensity[i] = light->intensity;
		light_color[i] = light->color;
		light_dir[i] = light->root.model.frontVector();
		light_type[i] = light->light_type;
		
		// Check if this light has a shadow map
		light_shadow_map_index[i] = -1; // Default: no shadow map
		if (light->cast_shadows) {
			for (int j = 0; j < active_shadow_maps; j++) {
				// Compare light position to find matching shadow map
				if (light_pos[i].distance(light_cameras[j].eye) < 0.001f) {
					light_shadow_map_index[i] = j;
					break;
				}
			}
		}
		
		if (light->light_type == 2){
			alpha_min = light->cone_info.x * 6.28/360;
			alpha_max = light->cone_info.y * 6.28/360;
		}
		i++;
	}
	
	// Set number of active shadow maps and bind them
	shader->setUniform("u_active_shadow_maps", active_shadow_maps);
	
	// Create arrays to hold matrices and textures
	Matrix44 shadow_matrices[MAX_SHADOW_MAPS];
	
	// Fill the arrays
	for (int j = 0; j < active_shadow_maps; j++) {
		shadow_matrices[j] = light_cameras[j].viewprojection_matrix;
		
		// Use correct array syntax for textures
		char uniform_name[32];
		sprintf(uniform_name, "u_shadow_textures[%d]", j);
		shader->setUniform(uniform_name, shadow_fbos[j]->depth_texture, 2 + j); // Use texture units starting from 2
	}
	
	// Set matrix array all at once if any shadows are active
	if (active_shadow_maps > 0) {
		shader->setMatrix44Array("u_shadow_matrices", shadow_matrices, active_shadow_maps);
	}
	
	
	if (!single_pass) {
		// Single light implementation, not updated for this assignment
		// Only included for compatibility
		shader->setUniform("u_shadowmap_legacy", shadow_fbos[0]->depth_texture, 2);
		shader->setUniform("u_shadowvp_legacy", light_cameras[0].viewprojection_matrix);
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
			shader->setUniform("u_mlight_shadow_index", light_shadow_map_index[i]);

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
		// Multi-light implementation with multiple shadow maps
		shader->setUniform("u_single_pass", 1);
		shader->setUniform3Array("u_light_pos", (float*)light_pos, min(light_list.size(), 10));
		shader->setUniform3Array("u_light_color", (float*)light_color, min(light_list.size(), 10));
		shader->setUniform1Array("u_light_intensity", (float*)light_intensity, min(light_list.size(), 10));
		shader->setUniform3Array("u_light_dir", (float*)light_dir, min(light_list.size(), 10));
		shader->setUniform1Array("u_type", light_type, min(light_list.size(), 10));
		shader->setUniform1Array("u_light_shadow_index", light_shadow_map_index, min(light_list.size(), 10));
		shader->setUniform("u_alpha_min", alpha_min);
		shader->setUniform("u_alpha_max", alpha_max);
		shader->setUniform("u_shadow_bias", shadow_bias);
		shader->setUniform("u_num_lights", (int)min(light_list.size(), 10));

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
	delete[] light_shadow_map_index;

	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
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
#ifndef SKIP_IMGUI

void Renderer::showUI()
{
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);

	//add here your stuff
	ImGui::Checkbox("Single Pass", &single_pass);
	if (ImGui::SliderFloat("Shininess", &shine, 0, 100)) {
		for (int i = 0; i < entities_to_render.size(); i++) {
			entities_to_render[i].material->shininess = shine;
		}
		for (int i = 0; i < transparent_to_render.size(); i++) {
			transparent_to_render[i].material->shininess = shine;
		}
	}
	
	// Shadow map related settings
	ImGui::Separator();
	ImGui::Text("Shadow Maps: %d active", active_shadow_maps);
	
	// Light settings
	if (ImGui::TreeNode("Lights")) {
		for (int i = 0; i < light_list.size(); i++) {
			SCN::LightEntity* light = light_list[i];
			
			char light_name[32];
			sprintf(light_name, "Light %d", i);
			
			if (ImGui::TreeNode(light_name)) {
				// Light type selection
				const char* light_types[] = { "No Light", "Point", "Spot", "Directional" };
				int current_type = (int)light->light_type;
				if (ImGui::Combo("Light Type", &current_type, light_types, IM_ARRAYSIZE(light_types))) {
					light->light_type = (SCN::eLightType)current_type;
				}
				
				// Shadow casting
				ImGui::Checkbox("Cast Shadows", &light->cast_shadows);
				ImGui::SliderFloat("Shadow Bias", &light->shadow_bias, 0.0001f, 0.01f, "%.5f");
				
				// Light parameters based on type
				ImGui::ColorEdit3("Color", &light->color.x);
				ImGui::SliderFloat("Intensity", &light->intensity, 0.0f, 10.0f);
				ImGui::SliderFloat("Near Distance", &light->near_distance, 0.01f, 10.0f);
				ImGui::SliderFloat("Max Distance", &light->max_distance, 10.0f, 1000.0f);
				
				// Type-specific parameters
				if (light->light_type == SCN::eLightType::DIRECTIONAL) {
					ImGui::SliderFloat("Area Size", &light->area, 10.0f, 5000.0f);
				}
				else if (light->light_type == SCN::eLightType::SPOT) {
					ImGui::SliderFloat("Inner Cone Angle", &light->cone_info.x, 0.0f, light->cone_info.y);
					ImGui::SliderFloat("Outer Cone Angle", &light->cone_info.y, light->cone_info.x, 90.0f);
				}
				
				ImGui::TreePop();
			}
		}
		ImGui::TreePop();
	}
	
	// Scene shadow bias slider
	static float global_shadow_bias = 0.001f;
	if (ImGui::SliderFloat("Global Shadow Bias", &global_shadow_bias, 0.0001f, 0.01f, "%.5f")) {
		for (auto light : light_list) {
			light->shadow_bias = global_shadow_bias;
		}
	}
}

#else
void Renderer::showUI() {}
#endif