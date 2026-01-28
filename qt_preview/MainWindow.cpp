#include "MainWindow.h"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QSpinBox>
#include <QApplication>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_mapView(new MapView(this))
    , m_configPanel(new ConfigPanel(this))
    , m_regenerateTimer(new QTimer(this))
    , m_statusLabel(new QLabel(this))
    , m_sizeLabel(new QLabel(this))
    , m_timeLabel(new QLabel(this))
    , m_zoomLabel(new QLabel(this))
{
    setupUI();
    setupMenu();
    setupToolbar();
    setupStatusBar();
    
    connect(m_configPanel, &ConfigPanel::generateClicked, this, &MainWindow::onGenerateMap);
    connect(m_configPanel, &ConfigPanel::regenerateClicked, this, &MainWindow::onRegenerateMap);
    connect(m_configPanel, &ConfigPanel::presetChanged, this, &MainWindow::onPresetChanged);
    connect(m_configPanel, &ConfigPanel::viewTypeChanged, this, &MainWindow::onViewTypeChanged);
    connect(m_configPanel, &ConfigPanel::autoRegenerateChanged, this, &MainWindow::onAutoRegenerateChanged);
    connect(m_mapView, &MapView::zoomChanged, this, &MainWindow::updateStatistics);
    connect(m_mapView, &MapView::viewChanged, this, [this]() {
        auto comboBox = this->findChild<QComboBox *>("ViewTypeCombo");
        if (comboBox) {
            comboBox->setCurrentIndex(m_mapView->viewType());
        }

        m_configPanel->updateCurrentMap(m_mapView->viewType());
        updateStatistics();
    });
    
    m_regenerateTimer->setSingleShot(true);
    connect(m_regenerateTimer, &QTimer::timeout, this, &MainWindow::generateNewMap);
    
    // Initial generation
    QTimer::singleShot(100, this, &MainWindow::generateNewMap);
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUI()
{
    // Central widget
    setCentralWidget(m_mapView);
    
    // Right dock widget for configuration
    QDockWidget *configDock = new QDockWidget(tr("Map Configuration"), this);
    configDock->setWidget(m_configPanel);
    configDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    configDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, configDock);
    
    // Left dock widget for legend
    QWidget *legendWidget = new QWidget(this);
    QVBoxLayout *legendLayout = new QVBoxLayout(legendWidget);
    
    QLabel *legendTitle = new QLabel(tr("<b>Terrain Legend</b>"), this);
    legendTitle->setAlignment(Qt::AlignCenter);
    legendLayout->addWidget(legendTitle);
    
    // Will be populated by MapView
    QWidget *legendContent = new QWidget(this);
    QVBoxLayout *legendContentLayout = new QVBoxLayout(legendContent);
    m_mapView->populateLegend(legendContentLayout);
    legendLayout->addWidget(legendContent);
    
    legendLayout->addStretch();
    
    QDockWidget *legendDock = new QDockWidget(tr("Legend"), this);
    legendDock->setWidget(legendWidget);
    legendDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    legendDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, legendDock);
}

void MainWindow::setupMenu()
{
    // File menu
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    
    m_actionGenerate = fileMenu->addAction(tr("&Generate Map"), this, &MainWindow::onGenerateMap);
    m_actionGenerate->setShortcut(QKeySequence::Refresh);
    
    fileMenu->addSeparator();
    
    m_actionExportImage = fileMenu->addAction(tr("Export as &Image..."), this, &MainWindow::onExportImage);
    m_actionExportJSON = fileMenu->addAction(tr("Export as &JSON..."), this, &MainWindow::onExportJSON);
    m_actionExportPPM = fileMenu->addAction(tr("Export as &PPM..."), this, &MainWindow::onExportPPM);
    m_actionExportPGM = fileMenu->addAction(tr("Export as P&GM..."), this, &MainWindow::onExportPGM);
    
    fileMenu->addSeparator();
    
    fileMenu->addAction(tr("E&xit"), qApp, &QApplication::quit)->setShortcut(QKeySequence::Quit);
    
    // View menu
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    
    m_actionShowGrid = new QAction(tr("Show &Grid"), this);
    m_actionShowGrid->setCheckable(true);
    m_actionShowGrid->setChecked(false);
    connect(m_actionShowGrid, &QAction::toggled, m_mapView, &MapView::setGridVisible);
    viewMenu->addAction(m_actionShowGrid);
    
    m_actionShowCoordinates = new QAction(tr("Show &Coordinates"), this);
    m_actionShowCoordinates->setCheckable(true);
    m_actionShowCoordinates->setChecked(true);
    connect(m_actionShowCoordinates, &QAction::toggled, m_mapView, &MapView::setCoordinatesVisible);
    viewMenu->addAction(m_actionShowCoordinates);
    
    m_actionAutoRegenerate = new QAction(tr("&Auto Regenerate"), this);
    m_actionAutoRegenerate->setCheckable(true);
    connect(m_actionAutoRegenerate, &QAction::toggled, this, &MainWindow::onAutoRegenerateChanged);
    viewMenu->addAction(m_actionAutoRegenerate);
    
    // Help menu
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), []() {
        QMessageBox::about(nullptr, "About Map Generator",
            "<h3>Map Generator Preview</h3>"
            "<p>Version 1.0.0</p>"
            "<p>Real-time preview tool for Map Generator library.</p>");
    });
}

void MainWindow::setupToolbar()
{
    QToolBar *toolbar = addToolBar(tr("Main Toolbar"));
    toolbar->setMovable(false);
    
    toolbar->addAction(m_actionGenerate);
    toolbar->addSeparator();
    
    QComboBox *viewTypeCombo = new QComboBox(this);
    viewTypeCombo->setObjectName("ViewTypeCombo");
    viewTypeCombo->addItem("Height Map", 0);
    viewTypeCombo->addItem("Terrain Map", 1);
    viewTypeCombo->addItem("Decoration Map", 2);
    viewTypeCombo->addItem("Composite Map", 3);
    viewTypeCombo->addItem("Resource Map", 4);
    viewTypeCombo->setCurrentIndex(3);
    connect(viewTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, viewTypeCombo](int index) { onViewTypeChanged(viewTypeCombo->itemData(index).toInt()); });
    
    toolbar->addWidget(new QLabel(tr("View: "), this));
    toolbar->addWidget(viewTypeCombo);
    
    toolbar->addSeparator();
    
    QSpinBox *zoomSpin = new QSpinBox(this);
    zoomSpin->setRange(10, 1000);
    zoomSpin->setValue(100);
    zoomSpin->setSuffix("%");
    connect(zoomSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            m_mapView, &MapView::setZoomLevel);
    connect(m_mapView, &MapView::zoomChanged,
            zoomSpin, &QSpinBox::setValue);
    
    toolbar->addWidget(new QLabel(tr("Zoom: "), this));
    toolbar->addWidget(zoomSpin);
}

void MainWindow::setupStatusBar()
{
    statusBar()->addPermanentWidget(m_statusLabel);
    statusBar()->addPermanentWidget(m_sizeLabel);
    statusBar()->addPermanentWidget(m_timeLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
    
    m_statusLabel->setText(tr("Ready"));
    m_sizeLabel->setText(tr("Size: --"));
    m_timeLabel->setText(tr("Time: --"));
    m_zoomLabel->setText(tr("Zoom: 100%"));
    
    statusBar()->show();
}

void MainWindow::onGenerateMap()
{
    generateNewMap();
}

void MainWindow::onRegenerateMap()
{
    generateNewMap();
}

void MainWindow::onExportImage()
{
    if (!m_currentMapData) return;
    
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Export Image"), QString(), tr("PNG Images (*.png);;All Files (*)"));
    
    if (!fileName.isEmpty()) {
        // TODO: Implement image export
        QMessageBox::information(this, tr("Export"), 
            tr("Image export functionality requires image library integration."));
    }
}

void MainWindow::onExportJSON()
{
    if (!m_currentMapData) return;
    
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Export JSON"), QString(), tr("JSON Files (*.json);;All Files (*)"));
    
    if (!fileName.isEmpty()) {
        // TODO: Implement JSON export
        QMessageBox::information(this, tr("Export"), 
            tr("JSON export functionality requires JSON library integration."));
    }
}

void MainWindow::onExportPPM()
{
    if (!m_currentMapData) return;
    
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Export PPM"), QString(), tr("PPM Files (*.ppm);;All Files (*)"));
    
    if (!fileName.isEmpty()) {
        MapGenerator::MapGenerator generator;
        bool success = generator.exportToPPM(*m_currentMapData, 
            fileName.toStdString(), true, m_mapView->viewType());
        
        if (success) {
            QMessageBox::information(this, tr("Export"), 
                tr("PPM file exported successfully."));
        } else {
            QMessageBox::warning(this, tr("Export"), 
                tr("Failed to export PPM file."));
        }
    }
}

void MainWindow::onExportPGM()
{
    if (!m_currentMapData) return;
    
    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Export PGM"), QString(), tr("PGM Files (*.pgm);;All Files (*)"));
    
    if (!fileName.isEmpty()) {
        MapGenerator::MapGenerator generator;
        bool success = generator.exportToPGM(*m_currentMapData, 
            fileName.toStdString(), 1.0f);
        
        if (success) {
            QMessageBox::information(this, tr("Export"), 
                tr("PGM file exported successfully."));
        } else {
            QMessageBox::warning(this, tr("Export"), 
                tr("Failed to export PGM file."));
        }
    }
}

void MainWindow::onPresetChanged(int index)
{
    m_configPanel->setPreset(static_cast<MapGenerator::MapConfig::Preset>(index));
}

void MainWindow::onViewTypeChanged(int type)
{
    m_mapView->setViewType(type);
}

void MainWindow::onAutoRegenerateChanged(bool checked)
{
    if (checked) {
        connect(m_configPanel, &ConfigPanel::parameterChanged,
                this, &MainWindow::scheduleRegenerate);
    } else {
        disconnect(m_configPanel, &ConfigPanel::parameterChanged,
                   this, &MainWindow::scheduleRegenerate);
        m_regenerateTimer->stop();
    }
}

void MainWindow::scheduleRegenerate()
{
    if (m_actionAutoRegenerate->isChecked()) {
        m_regenerateTimer->start(500); // 500ms delay
    }
}

void MainWindow::generateNewMap()
{
    MapGenerator::MapConfig config = m_configPanel->getConfig();
    
    m_statusLabel->setText(tr("Generating map..."));
    qApp->processEvents();
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    try {
        MapGenerator::MapGenerator generator;
        m_currentMapData = generator.generateMap(config);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        m_mapView->setMapData(m_currentMapData);
        m_statusLabel->setText(tr("Map generated successfully"));
        m_timeLabel->setText(tr("Time: %1 ms").arg(duration.count()));
        m_sizeLabel->setText(tr("Size: %1×%2").arg(config.width).arg(config.height));
        
        updateStatistics();
        
    } catch (const std::exception& e) {
        m_statusLabel->setText(tr("Error generating map"));
        QMessageBox::critical(this, tr("Error"), 
            tr("Failed to generate map: %1").arg(e.what()));
    }
}

void MainWindow::updateStatistics()
{
    if (!m_currentMapData) return;
    
    const auto& stats = m_currentMapData->stats;
    
    QString statsText = QString(
        "Water: %1%  Land: %2%  Forest: %3%  "
        "Mountains: %4%  Rivers: %5 tiles  "
        "Height: %6-%7 (avg: %8)")
        .arg(100 * stats.waterTiles / (m_currentMapData->config.width * m_currentMapData->config.height), 0, 'f', 1)
        .arg(100 * stats.landTiles / (m_currentMapData->config.width * m_currentMapData->config.height), 0, 'f', 1)
        .arg(100 * stats.forestTiles / (m_currentMapData->config.width * m_currentMapData->config.height), 0, 'f', 1)
        .arg(100 * stats.mountainTiles / (m_currentMapData->config.width * m_currentMapData->config.height), 0, 'f', 1)
        .arg(stats.riverTiles)
        .arg(stats.minHeight, 0, 'f', 3)
        .arg(stats.maxHeight, 0, 'f', 3)
        .arg(stats.averageHeight, 0, 'f', 3);
    
    statusBar()->showMessage(statsText, 5000);
}
