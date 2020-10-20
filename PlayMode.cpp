#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <random>

GLuint delivery_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > delivery_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("delivery.pnct"));
	delivery_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > delivery_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("delivery.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = delivery_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = delivery_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

WalkMesh const *walkmesh = nullptr;
Load< WalkMeshes > delivery_walkmeshes(LoadTagDefault, []() -> WalkMeshes const * {
	WalkMeshes *ret = new WalkMeshes(data_path("delivery.w"));
	// walkmesh = &ret->lookup("WalkMesh");
	walkmesh = &ret->lookup("ZMesh");
	return ret;
});

PlayMode::PlayMode() : scene(*delivery_scene) {
	//create a car transform:
	scene.transforms.emplace_back();
	car.transform = &scene.transforms.back();

	//create a car camera attached to a child of the car transform:
	scene.transforms.emplace_back();
	scene.cameras.emplace_back(&scene.transforms.back());
	car.camera = &scene.cameras.back();
	car.camera->fovy = glm::radians(60.0f);
	car.camera->near = 0.01f;
	car.camera->transform->parent = car.transform;

	//car's eyes are 1.8 units above the ground:
	car.camera->transform->position = glm::vec3(0.0f, -3.0f, 1.0f);

	//rotate camera facing direction (-z) to car facing direction (+y):
	car.camera->transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	//start car walking at nearest walk point:
	car.at = walkmesh->nearest_walk_point(car.transform->position);

	scene.transforms.emplace_back();
	walker.transform = &scene.transforms.back();
	scene.transforms.emplace_back();
	scene.cameras.emplace_back(&scene.transforms.back());
	walker.camera = &scene.cameras.back();
	walker.camera->fovy = glm::radians(60.0f);
	walker.camera->near = 0.01f;
	walker.camera->transform->parent = walker.transform;
	walker.camera->transform->position = glm::vec3(0.0f, 0.0f, 1.0f);
	walker.camera->transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	for (auto &transform : scene.transforms) {
		if (transform.name == "Player"){
			transform.parent = car.transform;
		}
	}

	button_hint = std::make_shared<view::TextSpan>();
	button_hint->set_text("").set_position(500, 650).set_visibility(true);

}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		} else if (
			evt.key.keysym.sym == SDLK_UP
			|| evt.key.keysym.sym == SDLK_DOWN
			|| evt.key.keysym.sym == SDLK_RETURN
			) {
			return order_controller->handle_keypress(evt.key.keysym.sym);
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_f) {
			switch_camera();
		} else if (evt.key.keysym.sym == SDLK_e) {
			update_order();
		}
	} 
	else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			return true;
		}
	} else if (evt.type == SDL_MOUSEMOTION) {
		if (SDL_GetRelativeMouseMode() == SDL_TRUE && !driving) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			glm::vec3 up = walkmesh->to_world_smooth_normal(walker.at);
			walker.transform->rotation = glm::angleAxis(-motion.x * walker.camera->fovy, up) * walker.transform->rotation;

			float pitch = glm::pitch(walker.camera->transform->rotation);
			pitch += motion.y * walker.camera->fovy;
			//camera looks down -z (basically at the walker's feet) when pitch is at zero.
			pitch = std::min(pitch, 0.95f * 3.1415926f);
			pitch = std::max(pitch, 0.05f * 3.1415926f);
			walker.camera->transform->rotation = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));

			return true;
		}
	}

	return false;
}

void PlayMode::update_order(){
	glm::vec3 playerLocation;
	if (driving)
		playerLocation = car.transform->position;
	else
		playerLocation = walker.transform->position;
	std::vector<Order> acceptedOrders = order_controller->accepted_orders_;
	for (Order o : acceptedOrders){
		if (o.is_delivering){
			if (glm::distance(playerLocation, get_location_position(o.client)) < enterDis){
				order_controller->deliver_order(o.client);
				std::cout << "deliver order" << std::endl;
			}
		} else {
			if (glm::distance(playerLocation, get_location_position(o.store)) < enterDis){
				order_controller->pickup_order(o.store);
				std::cout << "pickup order" << std::endl;
			}
		}
	}
}

void PlayMode::switch_camera(){
	if (driving){
		walker.transform->position = car.transform->position;
		walkmesh = &(delivery_walkmeshes->lookup("WalkMesh"));
		walker.at = walkmesh->nearest_walk_point(walker.transform->position);
	} else {
		if (glm::distance(walker.transform->position, car.transform->position) > enterDis)
			return;
		walkmesh = &(delivery_walkmeshes->lookup("ZMesh"));
	}
	button_hint->set_text("");
	driving = !driving;
}

glm::vec2 PlayMode::update_walker(float elapsed){
	//combine inputs into a move:
	constexpr float PlayerSpeed = 3.0f;
	glm::vec2 move = glm::vec2(0.0f);
	if (left.pressed && !right.pressed) move.x =-1.0f;
	if (!left.pressed && right.pressed) move.x = 1.0f;
	if (down.pressed && !up.pressed) move.y =-1.0f;
	if (!down.pressed && up.pressed) move.y = 1.0f;

	//make it so that moving diagonally doesn't go faster:
	if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

	if (glm::distance(walker.transform->position, car.transform->position) <= enterDis){
		button_hint->set_text("Press F to enter car.");
	} else {
		button_hint->set_text("");
	}

	glm::vec3 playerLocation;
	if (driving)
		playerLocation = car.transform->position;
	else
		playerLocation = walker.transform->position;
	std::vector<Order> acceptedOrders = order_controller->accepted_orders_;
	button_hint->set_text("");
	for (Order o : acceptedOrders){
		if (o.is_delivering){
			if (glm::distance(playerLocation, get_location_position(o.client)) < enterDis){
				button_hint->set_text("Press E to deliver order(s).");
			}
		} else {
			if (glm::distance(playerLocation, get_location_position(o.store)) < enterDis){
				button_hint->set_text("Press E to pickup order(s).");
			}
		}
	}

	return move;
}

glm::vec2 PlayMode::update_car(float elapsed){
		//combine inputs into a move:
		// constexpr float PlayerSpeed = 3.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x = 1.0f;
		if (!left.pressed && right.pressed) move.x = -1.0f;
		if (down.pressed && !up.pressed) car_speed -= acceleration*elapsed;
		if (!down.pressed && up.pressed) car_speed += acceleration*elapsed;
		if (abs(car_speed) > 0)
			car_speed -= friction*elapsed*(car_speed/abs(car_speed));
		car_speed /= std::max(abs(car_speed)/2, 1.0f);
		// std::cout << car_speed << std::endl;
		move.y = car_speed*elapsed;

		//make it so that moving diagonally doesn't go faster:
		// if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;
		glm::vec3 normal = walkmesh->to_world_smooth_normal(car.at);
		car.transform->rotation = glm::angleAxis(glm::radians(45.0f*move.x*elapsed), normal) * car.transform->rotation;
	
		move.x = 0.0f;
		/*
		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 right = frame[0];
		//glm::vec3 up = frame[1];
		glm::vec3 forward = -frame[2];

		camera->transform->position += move.x * right + move.y * forward;
		*/
	
	if (abs(car_speed) < 0.1f){
		button_hint->set_text("Press F to get out of car.");
	} else {
		button_hint->set_text("");
	}
	return move;
}

// void PlayMode::update_car(float elapsed){

// }

void PlayMode::update(float elapsed) {
	glm::vec2 move;
	Player *target;
	if (driving){
		move = update_car(elapsed);
		target = &car;
	} else {
		move = update_walker(elapsed);
		target = &walker;
	}
	
	//get move in world coordinate system:
	// glm::vec3 current_norm = walkmesh->to_world_smooth_normal(target->at);
	glm::vec3 remain = target->transform->make_local_to_world() * glm::vec4(move.x, move.y, 0.0f, 0.0f);

	//using a for() instead of a while() here so that if walkpoint gets stuck in
	// some awkward case, code will not infinite loop:
	for (uint32_t iter = 0; iter < 10; ++iter) {
		if (remain == glm::vec3(0.0f)) break;
		WalkPoint end;
		float time;
		walkmesh->walk_in_triangle(target->at, remain, &end, &time);
		target->at = end;
		if (time == 1.0f) {
			//finished within triangle:
			remain = glm::vec3(0.0f);
			break;
		}
		//some step remains:
		remain *= (1.0f - time);
		//try to step over edge:
		glm::quat rotation;
		if (walkmesh->cross_edge(target->at, &end, &rotation)) {
			//stepped to a new triangle:
			target->at = end;
			//rotate step to follow surface:
			remain = rotation * remain;
		} else {
			//ran into a wall, bounce / slide along it:
			glm::vec3 const &a = walkmesh->vertices[target->at.indices.x];
			glm::vec3 const &b = walkmesh->vertices[target->at.indices.y];
			glm::vec3 const &c = walkmesh->vertices[target->at.indices.z];
			glm::vec3 along = glm::normalize(b-a);
			glm::vec3 normal = glm::normalize(glm::cross(b-a, c-a));
			glm::vec3 in = glm::cross(normal, along);

			//check how much 'remain' is pointing out of the triangle:
			float d = glm::dot(remain, in);
			if (d < 0.0f) {
				//bounce off of the wall:
				remain += (-1.25f * d) * in;
			} else {
				//if it's just pointing along the edge, bend slightly away from wall:
				remain += 0.01f * d * in;
			}
		}
	}

	if (remain != glm::vec3(0.0f)) {
		std::cout << "NOTE: code used full iteration budget for walking." << std::endl;
	}

	//update car's position to respect walking:
	target->transform->position = walkmesh->to_world_point(target->at);

	{ //update car's rotation to respect local (smooth) up-vector:
		
		glm::quat adjust = glm::rotation(
			target->transform->rotation * glm::vec3(0.0f, 0.0f, 1.0f), //current up vector
			// current_norm,
			walkmesh->to_world_smooth_normal(target->at) //smoothed up vector at walk location
		);
		target->transform->rotation = glm::normalize(adjust * target->transform->rotation);
	
	}
	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	
	
	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.
	if (driving){
		car.camera->aspect = float(drawable_size.x) / float(drawable_size.y);
		scene.draw(*car.camera);
	} else{
		walker.camera->aspect = float(drawable_size.x) / float(drawable_size.y);
		scene.draw(*walker.camera);
	}
	
	button_hint->draw();
	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		constexpr float H = 0.09f;
		lines.draw_text("Mouse motion looks; WASD moves; escape ungrabs mouse",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("Mouse motion looks; WASD moves; escape ungrabs mouse",
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	}
	order_controller->draw();
	GL_ERRORS();
}
