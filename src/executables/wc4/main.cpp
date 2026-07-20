#include "../../strike_commander/precomp.h"
#include "../../engine/gametimer.h"
#include "../../engine/desktoptimer.h"
#include "../../wing_commander_4/WC4MainMenu.h"
#include "../../wing_commander_4/WC4Globals.h"

int main(int argc, char* argv[]) {
    Config& config = Config::getInstance();
    config.load("./assets/config.ini");
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

    // Skip TV turn-on effect for WC4
    screen.is_spfx_finished = true;

    std::string wc4DataPath = config.getString("WC4", "data_path", "");
    if (wc4DataPath.empty()) {
        fprintf(stderr, "WC4 data path not configured.\n");
        fprintf(stderr, "Add the following to assets/config.ini:\n");
        fprintf(stderr, "  [WC4]\n");
        fprintf(stderr, "  data_path=/path/to/wc4data\n");
        return EXIT_FAILURE;
    }
    assets.SetBase(wc4DataPath.c_str());
    loader.init();

    loader.startLoading([](Loader* loader) {
        AssetManager& assets = AssetManager::getInstance();
        loader->setProgress(0.0f);

        // globals.iff bootstraps the engine: PATH table, fonts, cursors, tick rate.
        // Must be loaded before TRE archives so fonts/cursors are available at startup.
        FileData* globalsData = assets.GetFileData("globals.iff");
        if (globalsData && globalsData->data) {
            WC4Globals::getInstance().load(globalsData->data, globalsData->size);
        } else {
            fprintf(stderr, "WC4: globals.iff not found in data path\n");
        }

        // Load WC4 XTRE archives (lowercase filenames on disk)
        std::vector<std::string> treFiles = {
            "gameflow.tre",
            "objects.tre",
            "missions.tre",
            "install.tre",
            "movies.tre"
        };
        assets.init(treFiles);
        loader->setProgress(50.0f);

        // WC4 asset paths matching TRE archive entry names (uppercase DOS paths).
        // globals.iff PATH table has these same paths in lowercase; values are equivalent
        // since TRE archive lookups are case-sensitive and entries use uppercase.
        assets.object_root_path  = "..\\..\\DATA\\OBJECTS\\";
        assets.mission_root_path = "..\\..\\DATA\\MISSIONS\\";
        assets.intel_root_path   = "..\\..\\DATA\\INTEL\\";
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

    WC4MainMenu* mainMenu = new WC4MainMenu();
    mainMenu->init();
    game->addActivity(mainMenu);
    game->run();

    return EXIT_SUCCESS;
}
