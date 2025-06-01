#pragma once
#include "scene.h"
#include "prefab.h"

#include "light.h"

#include "camera.h"
#include "math.h"
//forward declarations
//class Camera;
class Skeleton;

namespace GFX {
	class Shader;
	class Mesh;
	class FBO;
}


struct sDrawCommand {
	GFX::Mesh* mesh = nullptr;
	Matrix44 model;
	SCN::Material* material = nullptr;
	float distance;
};

namespace SCN {

	class Prefab;
	class Material;

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{

		std::vector<sDrawCommand> entities_to_render;
		std::vector<sDrawCommand> transparent_to_render;

		std::vector<SCN::LightEntity*> light_list;

	public:

		bool single_pass = true;
		bool forward = false;
		bool volume_light = false;
		bool pbr = false;
		bool generate_points= true;
		bool ssao = false;
		bool ssao_plus = false;

		bool gamma = true;

		int samples = 15;
		float radius = 0.05;
		float shadow_bias = 0.001f;


		bool render_wireframe;
		bool render_boundaries;
		float shine=5;
		int texture_slots;
		GFX::Texture* skybox_cubemap;

		float actualwidth = 1.0f;
		Vector3f actualcolor = Vector3f(0.0f, 0.0f, 1.0f);
		float actualspeed = 0.001f;
		float actualbspeed = 0.001f;
		float actualstart_time = 0.0f;
		int actual_borderwidth = 1;
		float actual_gridwidth = 0.0f;

		//pulse effect
		bool pulse_active[5];

		float pulse_width[5];
		Vector3f pulse_color[5];
		float pulse_speed[5];
		float pulse_bspeed[5];
		float pulse_start_time[5];
		Vector3f pulse_center[5];
		int pulse_border_width[5];	
		float pulse_grid_width[5];
		GFX::Mesh sphere;

		GFX::Texture* texture;
		GFX::Texture* depth_texture;
		GFX::FBO* fbo;
		GFX::FBO* gbuffer_fbo;
		GFX::FBO* light_fbo;
		GFX::FBO* ssao_FBO;
		bool max_pulse = false;
		std::vector<Vector3f> ao_sample_points;

		std::vector<Camera> volume_camera;

		static GFX::Mesh* light_sphere;
		SCN::Scene* scene;

		//updated every frame
		Renderer(const char* shaders_atlas_filename );

		//just to be sure we have everything ready for the rendering
		void setupScene();

		//add here your functions
		//...
		void parseNodes(SCN::Node* node, Camera* cam);

		void parseSceneEntities(SCN::Scene* scene, Camera* camera);

		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);

		//render the skybox
		void renderSkybox(GFX::Texture* cubemap);

		//to render one mesh given its material and transformation matrix

		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, bool transparent);
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material, bool transparent, Camera cam);


		void fillGBuff(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);
		void renderDeferred(SCN::Material* material, Camera* cam);

		void renderVolumeFirstPass(Camera cam);
		void renderVolume(Camera cam, LightEntity* light);
		void renderSSAO(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);


		void renderPlain(Camera cam, const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		std::vector<Vector3f> generateSpherePoints(int num, float radius, bool hemi);

		void showUI();
		void scannerUI();

	};

};