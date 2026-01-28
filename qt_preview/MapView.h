#ifndef MAPVIEW_H
#define MAPVIEW_H

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QVBoxLayout>
#include "MapGenerator.h"

class QScrollArea;
class QLabel;

class MapView : public QWidget
{
    Q_OBJECT

public:
    explicit MapView(QWidget *parent = nullptr);
    
    void setMapData(std::shared_ptr<MapGenerator::MapData> data);
    std::shared_ptr<MapGenerator::MapData> mapData() const { return m_mapData; }
    
    void setViewType(int type);
    int viewType() const { return m_viewType; }
    
    void setZoomLevel(int percent);
    int zoomLevel() const { return m_zoomLevel; }
    
    void setGridVisible(bool visible);
    bool isGridVisible() const { return m_showGrid; }
    
    void setCoordinatesVisible(bool visible);
    bool isCoordinatesVisible() const { return m_showCoordinates; }
    
    void populateLegend(QVBoxLayout *layout);

signals:
    void viewChanged();
    void zoomChanged(int percent);
    
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    
private:
    void updateView();
    void updateTransform();
    void generateImage();
    void drawGrid(QPainter &painter);
    void drawCoordinates(QPainter &painter);
    void drawLegend(QPainter &painter);
    QColor getTerrainColor(MapGenerator::TerrainType type) const;
    QColor getHeightColor(float height) const;
    QColor getResourceColor(uint32_t resource) const;
    
    std::shared_ptr<MapGenerator::MapData> m_mapData;
    QImage m_image;
    QImage m_scaledImage;
    
    int m_viewType = 3; // 0: height, 1: terrain, 2: decoration, 3: composite, 4: resource
    int m_zoomLevel = 100;
    
    bool m_showGrid = false;
    bool m_showCoordinates = true;
    bool m_showLegend = true;
    
    QPointF m_offset;
    QPoint m_lastMousePos;
    bool m_panning = false;
    
    // Legend data
    struct LegendItem {
        QString name;
        QColor color;
        int count = 0;
    };
    std::vector<LegendItem> m_legendItems;
};

#endif // MAPVIEW_H
