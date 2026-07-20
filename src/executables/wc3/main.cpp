#include "../../strike_commander/precomp.h"
#include "../../engine/gametimer.h"
#include "../../engine/desktoptimer.h"
#include "../../wing_commander_3/WC3MainMenu.h"
#include "../../wing_commander_3/WC3Globals.h"
#include "../../wing_commander_3/WC3Options.h"
#include <sys/stat.h>

int main(int argc, char* argv[]) {
    static const char* kConfigPath = "./assets/config.ini";
    Config& config = Config::getInstance();

    // Create the config file (with sensible defaults, including a fresh
    // [Options] section — see WC3Options.h) if it doesn't exist yet, so a
    // first run always has a real file on disk to read/edit from here on,
    // not just in-memory getInt/getBool fallbacks.
    struct stat configSt;
    bool configExisted = (stat(kConfigPath, &configSt) == 0);
    config.load(kConfigPath);
    if (!configExisted) {
        printf("WC3: no config.ini found at %s — creating one with defaults\n", kConfigPath);
        config.setInt("Window", "width", 1920);
        config.setInt("Window", "height", 1080);
        config.setBool("Window", "fullscreen", false);
        saveOptions(WC3Options{}, config); // struct defaults
        config.save(kConfigPath);
    }

    // VGA/SVGA is a separate, dedicated file (not part of config.ini) — see
    // WC3Options.h. Read before anything that sizes RSVGA's canvas
    // (GameEngine::init() -> RSVGA::init(), below) so a saved SVGA choice
    // takes effect from the very first frame instead of defaulting to
    // g_ifVGA's own false/SVGA default and only picking up the real choice
    // on the next options-screen visit. Missing file is not an error —
    // g_ifVGA's default (false, SVGA) already matches a first run.
    loadGraphicsSettings();

    GameTimer::getInstance().setTimer(std::make_unique<DesktopTimer>());

    RSScreen::setInstance(std::make_unique<RSScreen>());
    GameEngine::setInstance(std::make_unique<GameEngine>());

    RSScreen& screen = RSScreen::instance();
    Loader& loader = Loader::getInstance();
    AssetManager& assets = AssetManager::getInstance();

    int width = config.getInt("Window", "width", 800);
    int height = config.getInt("Window", "height", 600);
    bool fullscreen = config.getBool("Window", "fullscreen", false);
    screen.init(width, height, fullscreen);
    width = screen.width;
    height = screen.height;
    config.setInt("Window", "width", width);
    config.setInt("Window", "height", height);

    // Skip TV turn-on effect for WC3
    screen.is_spfx_finished = true;

    std::string wc3DataPath = config.getString("WC3", "data_path", "");
    if (wc3DataPath.empty()) {
        fprintf(stderr, "WC3 data path not configured.\n");
        fprintf(stderr, "Add the following to assets/config.ini:\n");
        fprintf(stderr, "  [WC3]\n");
        fprintf(stderr, "  data_path=/path/to/wc3data\n");
        return EXIT_FAILURE;
    }
    assets.SetBase(wc3DataPath.c_str());
    loader.init();

    loader.startLoading([](Loader* loader) {
        AssetManager& assets = AssetManager::getInstance();
        loader->setProgress(0.0f);

        // globals.iff bootstraps the engine: PATH table, fonts, cursors, tick rate.
        // Must be loaded before TRE archives so fonts/cursors are available at startup.
        FileData* globalsData = assets.GetFileData("globals.iff");
        if (globalsData && globalsData->data) {
            WC3Globals::getInstance().load(globalsData->data, globalsData->size);
        } else {
            fprintf(stderr, "WC3: globals.iff not found in data path\n");
        }

        // Load WC3 XTRE archives (lowercase filenames on disk)
        std::vector<std::string> treFiles = {
            "gameflow.tre",
            "objects.tre",
            "missions.tre",
            "install.tre",
            "movies.tre"
        };
        assets.init(treFiles);
        loader->setProgress(50.0f);

        // WC3 asset paths matching TRE archive entry names (uppercase DOS paths).
        // globals.iff PATH table has these same paths in lowercase; values are equivalent
        // since TRE archive lookups are case-sensitive and entries use uppercase.
        assets.object_root_path  = "..\\..\\DATA\\OBJECTS\\";
        assets.mission_root_path = "..\\..\\DATA\\MISSIONS\\";
        assets.intel_root_path   = "..\\..\\DATA\\INTEL\\";
        assets.profile_root_path = "..\\..\\DATA\\PROFILE\\";
        assets.sound_root_path   = "..\\..\\DATA\\SOUND\\";
        assets.texture_root_path = "..\\..\\DATA\\TXM\\";
        assets.gameflow_root_path = "..\\..\\DATA\\GAMEFLOW\\";

        assets.gameflow_filename = assets.gameflow_root_path + "GAMEFLOW.IFF";
        assets.navmap_filename   = "..\\..\\DATA\\COCKPITS\\NAVMAP.IFF";

        loader->setProgress(100.0f);
    });

    while (!loader.isLoadingComplete()) {
        loader.runFrame();
        screen.refresh();
        SDL_PumpEvents();
    }

    GameEngine* game = &GameEngine::instance();
    game->init();

    WC3MainMenu* mainMenu = new WC3MainMenu();
    mainMenu->init();
    game->addActivity(mainMenu);
    game->run();

    return EXIT_SUCCESS;
}
