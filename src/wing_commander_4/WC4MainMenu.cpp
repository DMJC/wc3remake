#include "WC4MainMenu.h"
#include "WC4GameFlow.h"

WC4MainMenu::WC4MainMenu() {}
WC4MainMenu::~WC4MainMenu() {}

void WC4MainMenu::init() {
    this->start();
    this->focus();
}

void WC4MainMenu::runFrame(void) {
    WC4GameFlow* gameflow = new WC4GameFlow();
    gameflow->init();
    GameEngine::instance().addActivity(gameflow);
    this->stop();
}
