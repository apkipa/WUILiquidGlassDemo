#include "pch.h"

#include <winrt/Tenkai.h>
#include "App.h"

using namespace winrt;

void show_splash_screen_eagerly() {
    auto callback = Tenkai::ApplicationInitializationCallback([](auto const& o) {
        auto params = o.as<Tenkai::ApplicationInitializationCallbackParams>();
        params.MainWindow().ExtendsContentIntoTitleBar(true);
    });
    Tenkai::AppService::SetStartupSplashScreenParams(Tenkai::SplashScreenKind::Simple, true, callback);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    init_apartment(apartment_type::single_threaded);
    show_splash_screen_eagerly();
    Tenkai::AppService::InitializeForApplication([](auto&&) {
        make<WUILiquidGlassDemo::implementation::App>();
    });
    Tenkai::AppService::RunLoop();
	Tenkai::AppService::UninitializeForApplication();
    return 0;
}
