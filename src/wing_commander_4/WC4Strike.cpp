#include "WC4Strike.h"

WC4Strike::WC4Strike() {}
WC4Strike::~WC4Strike() {}

void WC4Strike::setMission(char const* missionName) {
    if (Mixer.isSoundPlaying(5)) {
        Mixer.stopSound(5);
    }
    Mixer.stopMusic();

    if (this->current_mission != nullptr) {
        this->current_mission->cleanup();
        delete this->current_mission;
        this->current_mission = nullptr;
    }

    this->miss_file_name = missionName;
    WC4Mission* wc4Mission = new WC4Mission(missionName, &object_cache);
    this->current_mission = wc4Mission;
    ai_planes.clear();
    ai_planes.shrink_to_fit();

    // Configure renderer for space or ground
    SCRenderer& renderer = SCRenderer::instance();
    if (wc4Mission->is_space_mission) {
        renderer.show_ground = false;
        renderer.show_starfield = true;
        renderer.show_fog = false;
        renderer.show_clouds = false;
    } else {
        renderer.show_ground = true;
        renderer.show_starfield = false;
    }

    if (this->current_mission->player == nullptr) {
        printf("WC4Strike: No player found in mission '%s'\n", missionName);
        return;
    }

    MISN_PART* playerCoord = this->current_mission->player->object;
    this->area = this->current_mission->area;
    new_position.x = playerCoord->position.x;
    new_position.z = playerCoord->position.z;
    new_position.y = playerCoord->position.y;

    camera = renderer.getCamera();
    camera->SetPosition(&new_position);
    this->current_target = 0;
    if (!this->current_mission->enemies.empty()) {
        this->target = this->current_mission->enemies[this->current_target];
    }
    this->nav_point_id = 0;
    this->player_plane = this->current_mission->player->plane;
    this->player_plane->yaw = (360 - playerCoord->azymuth) * 10.0f;
    this->player_plane->object = playerCoord;

    if (!wc4Mission->is_space_mission && this->area != nullptr) {
        float ground = this->area->getY(new_position.x, new_position.z);
        if (ground < new_position.y) {
            this->player_plane->SetThrottle(100);
            this->player_plane->SetWheel();
            this->player_plane->vz = -20;
            this->player_plane->Simulate();
        }
    } else {
        this->player_plane->SetThrottle(100);
        this->player_plane->vz = -20;
        this->player_plane->Simulate();
    }

    this->player_plane->InitLoadout();
    this->player_prof = this->current_mission->player->profile;
    this->cockpit = new SCCockpit();
    this->cockpit->player_plane = this->player_plane;
    this->cockpit->init();
    this->cockpit->current_mission = this->current_mission;
    this->cockpit->nav_point_id = &this->nav_point_id;

    Mixer.stopMusic();
    Mixer.switchBank(2);
    Mixer.playMusic(this->current_mission->mission->mission_data.tune + 1);

    verticalOffset = 0.0f;
    SCStrike::verticalOffset = 0.0f;
    renderer.verticalOffset = verticalOffset;
    renderer.initRenderCameraView();
}

void WC4Strike::updateCloakEffect() {
    if (this->player_plane == nullptr) return;

    float dt = 1.0f / 60.0f;
    float rampSpeed = 2.0f;

    if (this->player_plane->cloaked) {
        this->player_plane->cloak_factor += rampSpeed * dt;
        if (this->player_plane->cloak_factor > 1.0f)
            this->player_plane->cloak_factor = 1.0f;
    } else {
        this->player_plane->cloak_factor -= rampSpeed * dt;
        if (this->player_plane->cloak_factor < 0.0f)
            this->player_plane->cloak_factor = 0.0f;
    }

    if (this->player_plane->cloak_factor > 0.0f) {
        RSVGA& vga = RSVGA::instance();
        VGAPalette* colorPal = vga.getPalette();
        if (bwPalette != nullptr && colorPal != nullptr) {
            vga.interpolerPalettes(colorPal, bwPalette, this->player_plane->cloak_factor);
        }
    }
}

void WC4Strike::runFrame(void) {
    updateCloakEffect();
    SCStrike::runFrame();
}
