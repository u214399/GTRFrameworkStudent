#include "renderer.h"

#include <algorithm> //sort

//#include "camera.h"
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
	texture_slots = 0;
	render_wireframe = false;
	render_boundaries = false;
	scene = nullptr;
	skybox_cubemap = nullptr;

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();


	for (int i = 0; i < 5; i++) {
		pulse_active[i] = false;
		pulse_bspeed[i] = 0.0f;
		pulse_center[i] = Vector3f(0.0f, 0.0f, 0.0f);
		pulse_color[i] = Vector3f(1.0f, 1.0f, 1.0f);
		pulse_speed[i] = 0.0f;
		pulse_width[i] = 0.0f;
	}

	texture = new GFX::Texture(1024,1024);
	fbo = new GFX::FBO();
	fbo->setTexture(texture);
	fbo->setDepthOnly(1024, 1024);

	
	gbuffer_fbo = new GFX::FBO();
	gbuffer_fbo->create(CORE::getWindowSize().x, CORE::getWindowSize().y, 2, GL_RGBA, GL_UNSIGNED_BYTE, true);

	light_fbo = new GFX::FBO();
	light_fbo->create(CORE::getWindowSize().x, CORE::getWindowSize().y, 1, GL_RGBA, GL_UNSIGNED_BYTE, true);

	ssao_FBO = new GFX::FBO();
	ssao_FBO->create(CORE::getWindowSize().x, CORE::getWindowSize().y, 1, GL_RGB, GL_UNSIGNED_BYTE, false);


	volume_camera = std::vector<Camera>();

	sphere.createSphere(10);

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

std::vector<Vector3f>SCN::Renderer::generateSpherePoints(int num,
	float radius, bool hemi) {
	std::vector<Vector3f> points;
	points.resize(num);
	for (int i = 0; i < num; i += 1) {
		Vector3f& p = points[i];
		float u = random();
		float v = random();
		float theta = u * 2.0 * PI;
		float phi = acos(2.0 * v - 1.0);
		float r = cbrt(random() * 0.9 + 0.1) * radius;
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);
		float sinPhi = sin(phi);
		float cosPhi = cos(phi);
		p.x = r * sinPhi * cosTheta;
		p.y = r * sinPhi * sinTheta;
		p.z = r * cosPhi;
		if (hemi && p.z < 0)
			p.z *= -1.0;
	}
	return points;
}


void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{

	this->scene = scene;
	setupScene();

	parseSceneEntities(scene, camera);

	Camera light_cam;

	fbo->bind();

	glColorMask(false, false, false, false);
	glClear(GL_DEPTH_BUFFER_BIT);

	glDrawBuffer(GL_NONE);
	glViewport(0, 0, 1024, 1024);

	mat4 light_model = light_list[3]->root.getGlobalMatrix();
	vec3 light_pos = light_model.getTranslation();

	light_cam.lookAt(light_pos, light_model * vec3(0.f, 0.f, -1.f), vec3(0.0f, 1.0f, 0.0f));

	float half_size = light_list[3]->area / 2.0f;

	light_cam.setOrthographic(-half_size, half_size, -half_size, half_size, light_list[3]->near_distance, light_list[3]->max_distance);

	for (sDrawCommand &render_call : entities_to_render) {
		renderPlain(light_cam, render_call.model, render_call.mesh, render_call.material);
	}

	glColorMask(true, true, true, true);

	fbo->unbind();

	gbuffer_fbo->bind();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	for (sDrawCommand& call : entities_to_render) {
		fillGBuff(call.model, call.mesh, call.material);

	}


	gbuffer_fbo->unbind();


	gbuffer_fbo->depth_texture->copyTo(light_fbo->depth_texture);

	light_fbo->bind();


	//
	//glColorMask(false, false, false, false);
	//glClear(GL_DEPTH_BUFFER_BIT);

	//glDrawBuffer(GL_NONE);
	//glViewport(0, 0, 1024, 1024);
	//for (int i = 0; i < light_list.size(); i++) {
	//	//
	//	Camera cam;
	//	mat4 light_model = light_list[i]->root.getGlobalMatrix();
	//	vec3 light_pos = light_model.getTranslation();

	//	cam.lookAt(light_pos, light_model * vec3(0.f, 0.f, -1.f), vec3(0.0f, 1.0f, 0.0f));

	//	float half_size = light_list[i]->area / 2.0f;

	//	cam.setOrthographic(-half_size, half_size, -half_size, half_size, light_list[i]->near_distance, light_list[i]->max_distance);
	//	
	//	volume_camera.push_back(cam);
	//}
	//glColorMask(true, true, true, true);
	//

	glClear(GL_COLOR_BUFFER_BIT);


	if (skybox_cubemap)
		renderSkybox(skybox_cubemap);

	renderVolumeFirstPass(light_cam);

	for(auto light : light_list)
		renderVolume(light_cam, light);


	light_fbo->unbind();

	
	if(ssao || ssao_plus){
		ssao_FBO->bind();
		gbuffer_fbo->depth_texture->copyTo(ssao_FBO->depth_texture);
		for (sDrawCommand draw : entities_to_render) {
			renderSSAO(draw.model, draw.mesh, draw.material);
		}

		ssao_FBO->unbind();
	}


	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();



	//render skybox
	if(skybox_cubemap)
		renderSkybox(skybox_cubemap);


	// HERE =====================
	// TODO: RENDER RENDERABLES
	// ==========================

	
	for (sDrawCommand draw : entities_to_render) {
		if (forward) {
			renderMeshWithMaterial(draw.model, draw.mesh, draw.material, false, light_cam);
			volume_light = false;
			ssao = false;
			ssao_plus = false;
		}
		else {
			if (volume_light) {
				light_fbo->color_textures[0]->toViewport();
				forward = false;
				pbr = false;
			}
			else
				renderDeferred(draw.material, &light_cam);
		}
	}

	for (sDrawCommand draw : transparent_to_render) {
		renderMeshWithMaterial(draw.model, draw.mesh, draw.material, true,light_cam);

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
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, bool transparent)
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
	
	single_pass = true;
	if (!single_pass) {
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

	ImGui::Checkbox("Forward Pipeline", &forward);
	ImGui::Checkbox("Volume Lights", &volume_light);
	ImGui::Checkbox("PBR", &pbr);
	ImGui::Checkbox("SSAO", &ssao);
	ImGui::Checkbox("SSAO+", &ssao_plus);
	if (ImGui::Checkbox("Active Pulse", &max_pulse)) {
		int i = 0;

		while (i < 5 && pulse_active[i])
		{
			i++;
		}
		if (i < 5) {
			pulse_active[i] = true;
			pulse_start_time[i] = CORE::getTime();
			pulse_center[i] = Camera::current->eye;
			pulse_color[i] = actualcolor;
			pulse_width[i] = actualwidth;
			pulse_speed[i] = actualspeed;
			pulse_bspeed[i] = actualbspeed;
			max_pulse = false;
			pulse_border_width[i] = actual_borderwidth;
		}
		else max_pulse = true;

	}

	ImGui::ColorEdit3("Pulse color", actualcolor.v);
	ImGui::SliderInt("Pulse border width", &actual_borderwidth, 0, 10);
	ImGui::SliderFloat("Pulse width", &actualwidth, 0.0f, 4.0f);
	ImGui::SliderFloat("Pulse diffusion Speed", &actualbspeed, 0.0f, 0.01f);
	ImGui::SliderFloat("Pulse Speed", &actualspeed, 0.0f, 0.01f);
	ImGui::Checkbox("Apply Gamma correction", &gamma);
	ImGui::Checkbox("Regenerate Points", &generate_points);
	ImGui::SliderInt("Samples", &samples, 15, 30);
	ImGui::SliderFloat("Radius", &radius, 0.01, 0.09);
	
}
#else
void Renderer::showUI() {}
#endif


