#ifndef CONFIGPANEL_H
#define CONFIGPANEL_H

#include <QWidget>
#include <QTabWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include "MapGenerator.h"

class ConfigPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigPanel(QWidget *parent = nullptr);
    
    MapGenerator::MapConfig getConfig() const;
    void setConfig(const MapGenerator::MapConfig &config);
    
    void setPreset(MapGenerator::MapConfig::Preset preset);

    void updateCurrentMap(int index);

signals:
    void generateClicked();
    void regenerateClicked();
    void presetChanged(int index);
    void viewTypeChanged(int type);
    void parameterChanged();
    void autoRegenerateChanged(bool checked);

private slots:
    void onPresetChanged(int index);
    void onGenerateClicked();
    void onRegenerateClicked();
    void onViewTypeChanged(int index);
    void onParameterChanged();
    void onAutoRegenerateChanged(int state);

private:
    void setupUI();
    void updatePresetParameters();
    void blockSignals(bool block);
    
    // Preset selector
    QComboBox *m_presetCombo;
    
    // Basic parameters
    QSpinBox *m_widthSpin;
    QSpinBox *m_heightSpin;
    QSpinBox *m_seedSpin;
    
    // Noise parameters
    QDoubleSpinBox *m_noiseScaleSpin;
    QSpinBox *m_noiseOctavesSpin;
    QDoubleSpinBox *m_noisePersistenceSpin;
    QDoubleSpinBox *m_noiseLacunaritySpin;
    
    // Height parameters
    QDoubleSpinBox *m_seaLevelSpin;
    QDoubleSpinBox *m_beachHeightSpin;
    QDoubleSpinBox *m_plainHeightSpin;
    QDoubleSpinBox *m_hillHeightSpin;
    QDoubleSpinBox *m_mountainHeightSpin;
    
    // Climate parameters
    QComboBox *m_climateCombo;
    QDoubleSpinBox *m_temperatureSpin;
    QDoubleSpinBox *m_humiditySpin;
    
    // WFC parameters
    QSpinBox *m_wfcIterationsSpin;
    QDoubleSpinBox *m_wfcEntropyWeightSpin;
    QCheckBox *m_wfcBacktrackingCheck;
    
    // Performance parameters
    QSpinBox *m_threadCountSpin;
    
    // View controls
    QComboBox *m_viewTypeCombo;
    QCheckBox *m_autoRegenerateCheck;
    
    // Buttons
    QPushButton *m_generateButton;
    QPushButton *m_regenerateButton;
};

#endif // CONFIGPANEL_H
