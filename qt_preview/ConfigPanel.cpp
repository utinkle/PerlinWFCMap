#include "ConfigPanel.h"
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QThread>
#include <QApplication>
#include <random>

ConfigPanel::ConfigPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void ConfigPanel::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);
    
    // Preset selector
    QGroupBox *presetGroup = new QGroupBox(tr("Preset"), this);
    QFormLayout *presetLayout = new QFormLayout(presetGroup);
    
    m_presetCombo = new QComboBox(this);
    m_presetCombo->addItem(tr("Custom"), static_cast<int>(MapGenerator::MapConfig::Preset::CUSTOM));
    m_presetCombo->addItem(tr("Islands"), static_cast<int>(MapGenerator::MapConfig::Preset::ISLANDS));
    m_presetCombo->addItem(tr("Mountains"), static_cast<int>(MapGenerator::MapConfig::Preset::MOUNTAINS));
    m_presetCombo->addItem(tr("Plains"), static_cast<int>(MapGenerator::MapConfig::Preset::PLAINS));
    m_presetCombo->addItem(tr("Continent"), static_cast<int>(MapGenerator::MapConfig::Preset::CONTINENT));
    m_presetCombo->addItem(tr("Archipelago"), static_cast<int>(MapGenerator::MapConfig::Preset::ARCHIPELAGO));
    m_presetCombo->addItem(tr("Swamp & Lakes"), static_cast<int>(MapGenerator::MapConfig::Preset::SWAMP_LAKES));
    m_presetCombo->addItem(tr("Desert & Canyons"), static_cast<int>(MapGenerator::MapConfig::Preset::DESERT_CANYONS));
    m_presetCombo->addItem(tr("Alpine"), static_cast<int>(MapGenerator::MapConfig::Preset::ALPINE));
    m_presetCombo->setCurrentIndex(4); // Continent
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConfigPanel::onPresetChanged);
    
    presetLayout->addRow(tr("Preset:"), m_presetCombo);
    mainLayout->addWidget(presetGroup);
    
    // Tab widget for different parameter categories
    QTabWidget *tabWidget = new QTabWidget(this);
    
    // Basic parameters tab
    QWidget *basicTab = new QWidget(this);
    QFormLayout *basicLayout = new QFormLayout(basicTab);
    
    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(64, 4096);
    m_widthSpin->setValue(1024);
    m_widthSpin->setSingleStep(64);
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(64, 4096);
    m_heightSpin->setValue(768);
    m_heightSpin->setSingleStep(64);
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_seedSpin = new QSpinBox(this);
    m_seedSpin->setRange(0, 999999);
    m_seedSpin->setValue(12345);
    m_seedSpin->setSingleStep(1);
    connect(m_seedSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    basicLayout->addRow(tr("Width:"), m_widthSpin);
    basicLayout->addRow(tr("Height:"), m_heightSpin);
    basicLayout->addRow(tr("Seed:"), m_seedSpin);
    
    tabWidget->addTab(basicTab, tr("Basic"));
    
    // Noise parameters tab
    QWidget *noiseTab = new QWidget(this);
    QFormLayout *noiseLayout = new QFormLayout(noiseTab);
    
    m_noiseScaleSpin = new QDoubleSpinBox(this);
    m_noiseScaleSpin->setRange(10.0, 1000.0);
    m_noiseScaleSpin->setValue(100.0);
    m_noiseScaleSpin->setSingleStep(10.0);
    m_noiseScaleSpin->setDecimals(1);
    connect(m_noiseScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_noiseOctavesSpin = new QSpinBox(this);
    m_noiseOctavesSpin->setRange(1, 12);
    m_noiseOctavesSpin->setValue(6);
    connect(m_noiseOctavesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_noisePersistenceSpin = new QDoubleSpinBox(this);
    m_noisePersistenceSpin->setRange(0.1, 0.9);
    m_noisePersistenceSpin->setValue(0.5);
    m_noisePersistenceSpin->setSingleStep(0.1);
    m_noisePersistenceSpin->setDecimals(2);
    connect(m_noisePersistenceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_noiseLacunaritySpin = new QDoubleSpinBox(this);
    m_noiseLacunaritySpin->setRange(1.5, 4.0);
    m_noiseLacunaritySpin->setValue(2.0);
    m_noiseLacunaritySpin->setSingleStep(0.1);
    m_noiseLacunaritySpin->setDecimals(2);
    connect(m_noiseLacunaritySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    noiseLayout->addRow(tr("Scale:"), m_noiseScaleSpin);
    noiseLayout->addRow(tr("Octaves:"), m_noiseOctavesSpin);
    noiseLayout->addRow(tr("Persistence:"), m_noisePersistenceSpin);
    noiseLayout->addRow(tr("Lacunarity:"), m_noiseLacunaritySpin);
    
    tabWidget->addTab(noiseTab, tr("Noise"));
    
    // Height parameters tab
    QWidget *heightTab = new QWidget(this);
    QFormLayout *heightLayout = new QFormLayout(heightTab);
    
    m_seaLevelSpin = new QDoubleSpinBox(this);
    m_seaLevelSpin->setRange(0.0, 1.0);
    m_seaLevelSpin->setValue(0.3);
    m_seaLevelSpin->setSingleStep(0.01);
    m_seaLevelSpin->setDecimals(3);
    connect(m_seaLevelSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_beachHeightSpin = new QDoubleSpinBox(this);
    m_beachHeightSpin->setRange(0.0, 1.0);
    m_beachHeightSpin->setValue(0.32);
    m_beachHeightSpin->setSingleStep(0.01);
    m_beachHeightSpin->setDecimals(3);
    connect(m_beachHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_plainHeightSpin = new QDoubleSpinBox(this);
    m_plainHeightSpin->setRange(0.0, 1.0);
    m_plainHeightSpin->setValue(0.4);
    m_plainHeightSpin->setSingleStep(0.01);
    m_plainHeightSpin->setDecimals(3);
    connect(m_plainHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_hillHeightSpin = new QDoubleSpinBox(this);
    m_hillHeightSpin->setRange(0.0, 1.0);
    m_hillHeightSpin->setValue(0.6);
    m_hillHeightSpin->setSingleStep(0.01);
    m_hillHeightSpin->setDecimals(3);
    connect(m_hillHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_mountainHeightSpin = new QDoubleSpinBox(this);
    m_mountainHeightSpin->setRange(0.0, 1.0);
    m_mountainHeightSpin->setValue(0.8);
    m_mountainHeightSpin->setSingleStep(0.01);
    m_mountainHeightSpin->setDecimals(3);
    connect(m_mountainHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    heightLayout->addRow(tr("Sea Level:"), m_seaLevelSpin);
    heightLayout->addRow(tr("Beach Height:"), m_beachHeightSpin);
    heightLayout->addRow(tr("Plain Height:"), m_plainHeightSpin);
    heightLayout->addRow(tr("Hill Height:"), m_hillHeightSpin);
    heightLayout->addRow(tr("Mountain Height:"), m_mountainHeightSpin);
    
    tabWidget->addTab(heightTab, tr("Height"));
    
    // Climate parameters tab
    QWidget *climateTab = new QWidget(this);
    QFormLayout *climateLayout = new QFormLayout(climateTab);
    
    m_climateCombo = new QComboBox(this);
    m_climateCombo->addItem(tr("Temperate"), static_cast<int>(MapGenerator::ClimateType::TEMPERATE));
    m_climateCombo->addItem(tr("Tropical"), static_cast<int>(MapGenerator::ClimateType::TROPICAL));
    m_climateCombo->addItem(tr("Arid"), static_cast<int>(MapGenerator::ClimateType::ARID));
    m_climateCombo->addItem(tr("Continental"), static_cast<int>(MapGenerator::ClimateType::CONTINENTAL));
    m_climateCombo->addItem(tr("Polar"), static_cast<int>(MapGenerator::ClimateType::POLAR));
    m_climateCombo->addItem(tr("Mediterranean"), static_cast<int>(MapGenerator::ClimateType::MEDITERRANEAN));
    m_climateCombo->setCurrentIndex(0);
    connect(m_climateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_temperatureSpin = new QDoubleSpinBox(this);
    m_temperatureSpin->setRange(0.0, 1.0);
    m_temperatureSpin->setValue(0.5);
    m_temperatureSpin->setSingleStep(0.01);
    m_temperatureSpin->setDecimals(2);
    connect(m_temperatureSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_humiditySpin = new QDoubleSpinBox(this);
    m_humiditySpin->setRange(0.0, 1.0);
    m_humiditySpin->setValue(0.5);
    m_humiditySpin->setSingleStep(0.01);
    m_humiditySpin->setDecimals(2);
    connect(m_humiditySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    climateLayout->addRow(tr("Climate:"), m_climateCombo);
    climateLayout->addRow(tr("Temperature:"), m_temperatureSpin);
    climateLayout->addRow(tr("Humidity:"), m_humiditySpin);
    
    tabWidget->addTab(climateTab, tr("Climate"));
    
    // WFC parameters tab
    QWidget *wfcTab = new QWidget(this);
    QFormLayout *wfcLayout = new QFormLayout(wfcTab);
    
    m_wfcIterationsSpin = new QSpinBox(this);
    m_wfcIterationsSpin->setRange(100, 10000);
    m_wfcIterationsSpin->setValue(1000);
    m_wfcIterationsSpin->setSingleStep(100);
    connect(m_wfcIterationsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_wfcEntropyWeightSpin = new QDoubleSpinBox(this);
    m_wfcEntropyWeightSpin->setRange(0.0, 1.0);
    m_wfcEntropyWeightSpin->setValue(0.1);
    m_wfcEntropyWeightSpin->setSingleStep(0.01);
    m_wfcEntropyWeightSpin->setDecimals(2);
    connect(m_wfcEntropyWeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    m_wfcBacktrackingCheck = new QCheckBox(this);
    m_wfcBacktrackingCheck->setChecked(true);
    connect(m_wfcBacktrackingCheck, &QCheckBox::toggled,
            this, &ConfigPanel::onParameterChanged);
    
    wfcLayout->addRow(tr("Iterations:"), m_wfcIterationsSpin);
    wfcLayout->addRow(tr("Entropy Weight:"), m_wfcEntropyWeightSpin);
    wfcLayout->addRow(tr("Enable Backtracking:"), m_wfcBacktrackingCheck);
    
    tabWidget->addTab(wfcTab, tr("WFC"));
    
    // Performance parameters tab
    QWidget *perfTab = new QWidget(this);
    QFormLayout *perfLayout = new QFormLayout(perfTab);
    
    m_threadCountSpin = new QSpinBox(this);
    m_threadCountSpin->setRange(1, 16);
    m_threadCountSpin->setValue(QThread::idealThreadCount());
    connect(m_threadCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ConfigPanel::onParameterChanged);
    
    perfLayout->addRow(tr("Thread Count:"), m_threadCountSpin);
    
    tabWidget->addTab(perfTab, tr("Performance"));
    
    mainLayout->addWidget(tabWidget);
    
    // View controls
    QGroupBox *viewGroup = new QGroupBox(tr("View Options"), this);
    QFormLayout *viewLayout = new QFormLayout(viewGroup);
    
    m_viewTypeCombo = new QComboBox(this);
    m_viewTypeCombo->addItem(tr("Height Map"), 0);
    m_viewTypeCombo->addItem(tr("Terrain Map"), 1);
    m_viewTypeCombo->addItem(tr("Decoration Map"), 2);
    m_viewTypeCombo->addItem(tr("Composite Map"), 3);
    m_viewTypeCombo->addItem(tr("Resource Map"), 4);
    connect(m_viewTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConfigPanel::onViewTypeChanged);
    
    m_autoRegenerateCheck = new QCheckBox(tr("Auto-regenerate on parameter change"), this);
    connect(m_autoRegenerateCheck, &QCheckBox::stateChanged,
            this, &ConfigPanel::onAutoRegenerateChanged);
    
    viewLayout->addRow(tr("View Type:"), m_viewTypeCombo);
    viewLayout->addRow(m_autoRegenerateCheck);
    
    mainLayout->addWidget(viewGroup);
    
    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    
    m_generateButton = new QPushButton(tr("&Generate"), this);
    m_generateButton->setStyleSheet("QPushButton { font-weight: bold; padding: 5px; }");
    connect(m_generateButton, &QPushButton::clicked,
            this, &ConfigPanel::onGenerateClicked);
    
    m_regenerateButton = new QPushButton(tr("&Regenerate"), this);
    m_regenerateButton->setStyleSheet("QPushButton { padding: 5px; }");
    connect(m_regenerateButton, &QPushButton::clicked,
            this, &ConfigPanel::onRegenerateClicked);
    
    buttonLayout->addWidget(m_generateButton);
    buttonLayout->addWidget(m_regenerateButton);
    buttonLayout->addStretch();
    
    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
}

MapGenerator::MapConfig ConfigPanel::getConfig() const
{
    MapGenerator::MapConfig config;
    
    config.width = static_cast<uint32_t>(m_widthSpin->value());
    config.height = static_cast<uint32_t>(m_heightSpin->value());
    config.seed = static_cast<uint32_t>(m_seedSpin->value());
    
    config.noiseScale = static_cast<float>(m_noiseScaleSpin->value());
    config.noiseOctaves = static_cast<int32_t>(m_noiseOctavesSpin->value());
    config.noisePersistence = static_cast<float>(m_noisePersistenceSpin->value());
    config.noiseLacunarity = static_cast<float>(m_noiseLacunaritySpin->value());
    
    config.seaLevel = static_cast<float>(m_seaLevelSpin->value());
    config.beachHeight = static_cast<float>(m_beachHeightSpin->value());
    config.plainHeight = static_cast<float>(m_plainHeightSpin->value());
    config.hillHeight = static_cast<float>(m_hillHeightSpin->value());
    config.mountainHeight = static_cast<float>(m_mountainHeightSpin->value());
    
    config.climate = static_cast<MapGenerator::ClimateType>(
        m_climateCombo->currentData().toInt());
    config.temperature = static_cast<float>(m_temperatureSpin->value());
    config.humidity = static_cast<float>(m_humiditySpin->value());
    
    // config.wfcIterations = static_cast<uint32_t>(m_wfcIterationsSpin->value());
    // config.wfcEntropyWeight = static_cast<float>(m_wfcEntropyWeightSpin->value());
    // config.wfcEnableBacktracking = m_wfcBacktrackingCheck->isChecked();
    
    config.threadCount = static_cast<uint32_t>(m_threadCountSpin->value());
    
    config.preset = static_cast<MapGenerator::MapConfig::Preset>(
        m_presetCombo->currentData().toInt());
    
    return config;
}

void ConfigPanel::setConfig(const MapGenerator::MapConfig &config)
{
    blockSignals(true);
    
    m_widthSpin->setValue(static_cast<int>(config.width));
    m_heightSpin->setValue(static_cast<int>(config.height));
    m_seedSpin->setValue(static_cast<int>(config.seed));
    
    m_noiseScaleSpin->setValue(config.noiseScale);
    m_noiseOctavesSpin->setValue(static_cast<int>(config.noiseOctaves));
    m_noisePersistenceSpin->setValue(config.noisePersistence);
    m_noiseLacunaritySpin->setValue(config.noiseLacunarity);
    
    m_seaLevelSpin->setValue(config.seaLevel);
    m_beachHeightSpin->setValue(config.beachHeight);
    m_plainHeightSpin->setValue(config.plainHeight);
    m_hillHeightSpin->setValue(config.hillHeight);
    m_mountainHeightSpin->setValue(config.mountainHeight);
    
    m_climateCombo->setCurrentIndex(static_cast<int>(config.climate));
    m_temperatureSpin->setValue(config.temperature);
    m_humiditySpin->setValue(config.humidity);
    
    // m_wfcIterationsSpin->setValue(static_cast<int>(config.wfcIterations));
    // m_wfcEntropyWeightSpin->setValue(config.wfcEntropyWeight);
    // m_wfcBacktrackingCheck->setChecked(config.wfcEnableBacktracking);
    
    m_threadCountSpin->setValue(static_cast<int>(config.threadCount));
    
    int presetIndex = m_presetCombo->findData(static_cast<int>(config.preset));
    if (presetIndex >= 0) {
        m_presetCombo->setCurrentIndex(presetIndex);
    }
    
    blockSignals(false);
}

void ConfigPanel::setPreset(MapGenerator::MapConfig::Preset preset)
{
    int index = m_presetCombo->findData(static_cast<int>(preset));
    if (index >= 0) {
        m_presetCombo->setCurrentIndex(index);
    }
}

void ConfigPanel::updateCurrentMap(int index)
{
    m_viewTypeCombo->setCurrentIndex(index);
}

void ConfigPanel::onPresetChanged(int index)
{
    emit presetChanged(index);
    
    if (index == 0) { // Custom
        // Don't change parameters for custom preset
        return;
    }
    
    MapGenerator::MapConfig::Preset preset = static_cast<MapGenerator::MapConfig::Preset>(
        m_presetCombo->currentData().toInt());
    
    MapGenerator::MapConfig config = MapGenerator::MapGenerator::createConfigFromPreset(preset);
    
    // Update UI with preset parameters
    blockSignals(true);
    
    m_widthSpin->setValue(static_cast<int>(config.width));
    m_heightSpin->setValue(static_cast<int>(config.height));
    
    m_noiseScaleSpin->setValue(config.noiseScale);
    m_noiseOctavesSpin->setValue(static_cast<int>(config.noiseOctaves));
    m_noisePersistenceSpin->setValue(config.noisePersistence);
    m_noiseLacunaritySpin->setValue(config.noiseLacunarity);
    
    m_seaLevelSpin->setValue(config.seaLevel);
    m_beachHeightSpin->setValue(config.beachHeight);
    m_plainHeightSpin->setValue(config.plainHeight);
    m_hillHeightSpin->setValue(config.hillHeight);
    m_mountainHeightSpin->setValue(config.mountainHeight);
    
    m_climateCombo->setCurrentIndex(static_cast<int>(config.climate));
    m_temperatureSpin->setValue(config.temperature);
    m_humiditySpin->setValue(config.humidity);
    
    blockSignals(false);
    
    // Auto-generate if enabled
    if (m_autoRegenerateCheck->isChecked()) {
        emit generateClicked();
    }
}

void ConfigPanel::onGenerateClicked()
{
    emit generateClicked();
}

void ConfigPanel::onRegenerateClicked()
{
    // Randomize seed
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 999999);
    m_seedSpin->setValue(dis(gen));
    
    emit regenerateClicked();
}

void ConfigPanel::onViewTypeChanged(int index)
{
    emit viewTypeChanged(m_viewTypeCombo->itemData(index).toInt());
}

void ConfigPanel::onParameterChanged()
{
    // Set preset to custom when parameters are manually changed
    if (m_presetCombo->currentIndex() != 0) {
        m_presetCombo->setCurrentIndex(0);
    }
    
    emit parameterChanged();
}

void ConfigPanel::onAutoRegenerateChanged(int state)
{
    emit autoRegenerateChanged(state == Qt::Checked);
}

void ConfigPanel::blockSignals(bool block)
{
    m_widthSpin->blockSignals(block);
    m_heightSpin->blockSignals(block);
    m_seedSpin->blockSignals(block);
    m_noiseScaleSpin->blockSignals(block);
    m_noiseOctavesSpin->blockSignals(block);
    m_noisePersistenceSpin->blockSignals(block);
    m_noiseLacunaritySpin->blockSignals(block);
    m_seaLevelSpin->blockSignals(block);
    m_beachHeightSpin->blockSignals(block);
    m_plainHeightSpin->blockSignals(block);
    m_hillHeightSpin->blockSignals(block);
    m_mountainHeightSpin->blockSignals(block);
    m_climateCombo->blockSignals(block);
    m_temperatureSpin->blockSignals(block);
    m_humiditySpin->blockSignals(block);
    m_wfcIterationsSpin->blockSignals(block);
    m_wfcEntropyWeightSpin->blockSignals(block);
    m_wfcBacktrackingCheck->blockSignals(block);
    m_threadCountSpin->blockSignals(block);
}
