#include <mayonnaise/app.hpp>
#include <mayonnaise/window.hpp>

int main() {
    mayo::App app;
    app.createWindow({.title = "Hello", .width = 800, .height = 520});
    return app.run();
}
