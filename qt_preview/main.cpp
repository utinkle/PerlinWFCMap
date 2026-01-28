#include "MainWindow.h"
#include <QApplication>
#include <QFileInfo>
#include <QTranslator>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QTranslator translator;
    if (translator.load("mapgenerator_zh_CN", ":/"))
        app.installTranslator(&translator);
    
    app.setOrganizationName("MapGenerator");
    app.setApplicationName("MapGenerator Preview");
    app.setApplicationVersion("1.0.0");
    
    MainWindow window;
    window.setWindowTitle("Map Generator Preview");
    window.resize(1600, 900);
    window.show();
    
    return app.exec();
}
