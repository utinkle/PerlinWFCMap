#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include "MapView.h"
#include "ConfigPanel.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onGenerateMap();
    void onRegenerateMap();
    void onExportImage();
    void onExportJSON();
    void onExportPPM();
    void onExportPGM();
    void onPresetChanged(int index);
    void onViewTypeChanged(int type);
    void onAutoRegenerateChanged(bool checked);
    void updateStatistics();

private:
    void setupUI();
    void setupMenu();
    void setupToolbar();
    void setupStatusBar();
    void generateNewMap();
    void scheduleRegenerate();

    MapView *m_mapView;
    ConfigPanel *m_configPanel;
    QTimer *m_regenerateTimer;
    std::shared_ptr<MapGenerator::MapData> m_currentMapData;
    
    // Actions
    QAction *m_actionGenerate;
    QAction *m_actionExportImage;
    QAction *m_actionExportJSON;
    QAction *m_actionExportPPM;
    QAction *m_actionExportPGM;
    QAction *m_actionAutoRegenerate;
    QAction *m_actionShowGrid;
    QAction *m_actionShowCoordinates;
    
    // Status bar widgets
    QLabel *m_statusLabel;
    QLabel *m_sizeLabel;
    QLabel *m_timeLabel;
    QLabel *m_zoomLabel;
};

#endif // MAINWINDOW_H