#include "MainWindow.h"
#include <QtWidgets/QApplication>
#include <iostream>

// #include "ecs/ecs.h"

#ifdef ECS
struct Name {
    std::string name;
};

struct ID {
    int id;
};

struct Timer {
    int t = 123;
};

void EcsTest() {
    ecs::World world;
    ecs::Commands command(world);
    auto person1 = command.SpawnAndReturn<Name>(Name{ "person1" });
    command.Spawn<Name, ID>(Name{ "person2" }, ID{ 1 })
        .Spawn<ID>(ID{ 2 })
        .SetResource<Timer>(Timer{ 123 });
    // command.RemoveResource<Timer>();

    ecs::Queryer queryer(world);
    for (auto entity : queryer.Query<Name>()) {
        std::cout << queryer.Get<Name>(entity).name << "\n";
    }
    std::cout << "=============================" << "\n";
    for (auto entity : queryer.Query<ID>()) {
        std::cout << queryer.Get<ID>(entity).id << "\n";
    }
    std::cout << "=============================" << "\n";
    for (auto entity : queryer.Query<ID, Name>()) {
        std::cout << queryer.Get<ID>(entity).id << ", ";
        std::cout << queryer.Get<Name>(entity).name << "\n";
    }

    ecs::Resources resources(world);
    std::cout << resources.Get<Timer>().t << "\n";

    command.Destroy(person1);

    for (auto entity : queryer.Query<Name>()) {
        std::cout << queryer.Get<Name>(entity).name << "\n";
    }
    std::cout << "=============================" << "\n";
    for (auto entity : queryer.Query<ID>()) {
        std::cout << queryer.Get<ID>(entity).id << "\n";
    }
    std::cout << "=============================" << "\n";
    for (auto entity : queryer.Query<ID, Name>()) {
        std::cout << queryer.Get<ID>(entity).id << ", ";
        std::cout << queryer.Get<Name>(entity).name << "\n";
    }
}

#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
