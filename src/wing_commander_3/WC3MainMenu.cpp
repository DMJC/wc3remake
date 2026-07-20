#include "WC3MainMenu.h"
#include "WC3GameFlow.h"

WC3MainMenu::WC3MainMenu() {}
WC3MainMenu::~WC3MainMenu() {}

void WC3MainMenu::init() {
    this->start();
    this->focus();
}

void WC3MainMenu::runFrame(void) {
    WC3GameFlow* gameflow = new WC3GameFlow();
    gameflow->init();
    GameEngine::instance().addActivity(gameflow);
    this->stop();
}
