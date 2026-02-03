#include "MapView.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QtMath>

MapView::MapView(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(400, 300);
}

void MapView::setMapData(std::shared_ptr<MapGenerator::MapData> data)
{
    m_mapData = data;
    if (m_mapData) {
        generateImage();
        updateView();
    }
    update();
}

void MapView::setViewType(int type)
{
    if (m_viewType != type) {
        m_viewType = type;
        if (m_mapData) {
            generateImage();
            updateView();
        }
        update();
        emit viewChanged();
    }
}

void MapView::setZoomLevel(int percent)
{
    percent = qBound(10, percent, 1000);
    if (m_zoomLevel != percent) {
        m_zoomLevel = percent;
        updateView();
        update();
        emit zoomChanged(m_zoomLevel);
    }
}

void MapView::setGridVisible(bool visible)
{
    if (m_showGrid != visible) {
        m_showGrid = visible;
        update();
    }
}

void MapView::setCoordinatesVisible(bool visible)
{
    if (m_showCoordinates != visible) {
        m_showCoordinates = visible;
        update();
    }
}

void MapView::populateLegend(QVBoxLayout *layout)
{
    layout->setSpacing(2);

    // 静态翻译表：英文 -> 中文
    static const std::unordered_map<MapGenerator::TerrainType, QString> terrainTranslations = {
        {MapGenerator::TerrainType::DEEP_OCEAN, "深海"},
        {MapGenerator::TerrainType::SHALLOW_OCEAN, "浅海"},
        {MapGenerator::TerrainType::COAST, "海岸"},
        {MapGenerator::TerrainType::BEACH, "海滩"},
        {MapGenerator::TerrainType::PLAIN, "平原"},
        {MapGenerator::TerrainType::FOREST, "森林"},
        {MapGenerator::TerrainType::HILL, "丘陵"},
        {MapGenerator::TerrainType::MOUNTAIN, "山脉"},
        {MapGenerator::TerrainType::SNOW_MOUNTAIN, "雪山"},
        {MapGenerator::TerrainType::DESERT, "沙漠"},
        {MapGenerator::TerrainType::SWAMP, "沼泽"},
        {MapGenerator::TerrainType::RIVER, "河流"},
        {MapGenerator::TerrainType::LAKE, "湖泊"},
        {MapGenerator::TerrainType::TREE_DENSE, "密林"},
        {MapGenerator::TerrainType::TREE_SPARSE, "疏林"},
        {MapGenerator::TerrainType::TREE_PALM, "棕榈树"},
        {MapGenerator::TerrainType::TREE_SNOW, "雪松"},
        {MapGenerator::TerrainType::ROCK_SMALL, "小岩石"},
        {MapGenerator::TerrainType::ROCK_LARGE, "大岩石"},
        {MapGenerator::TerrainType::BUSH, "灌木"},
        {MapGenerator::TerrainType::FLOWERS, "花朵"},
        {MapGenerator::TerrainType::GRASS, "草地"},
        {MapGenerator::TerrainType::SAND, "沙地"},
        {MapGenerator::TerrainType::CLAY, "粘土"},
        {MapGenerator::TerrainType::SNOW, "雪地"},
        {MapGenerator::TerrainType::WATER, "水"},
        {MapGenerator::TerrainType::REEDS, "芦苇"}
    };

    // 创建两列布局的容器
    QWidget *legendContainer = new QWidget();
    QHBoxLayout *mainLayout = new QHBoxLayout(legendContainer);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // 左列：基础地形
    QWidget *leftColumn = new QWidget();
    QVBoxLayout *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setSpacing(2);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *terrainTitle = new QLabel("<b>基础地形</b>");
    terrainTitle->setFont(QFont(terrainTitle->font().family(), 12));
    terrainTitle->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(terrainTitle);
    leftLayout->addSpacing(10);

    // 基础地形类型 (1-13)
    for (int i = 1; i <= 13; i++) {
        auto type = static_cast<MapGenerator::TerrainType>(i);
        QString name = terrainTranslations.count(type) ?
                           terrainTranslations.at(type) :
                           QString::fromStdString(MapGenerator::MapGenerator::getTerrainName(type));

        QColor color = getTerrainColor(type);

        QWidget *itemWidget = new QWidget();
        QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(2, 2, 2, 2);

        QLabel *colorLabel = new QLabel();
        colorLabel->setFixedSize(16, 16);
        colorLabel->setStyleSheet(QString("background-color: %1; border: 1px solid #808080;").arg(color.name()));

        QLabel *nameLabel = new QLabel(name);
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        nameLabel->setStyleSheet("font-size: 9pt;");

        itemLayout->addWidget(colorLabel);
        itemLayout->addWidget(nameLabel);

        leftLayout->addWidget(itemWidget);
    }

    leftLayout->addStretch();

    // 右列：装饰和资源
    QWidget *rightColumn = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setSpacing(2);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *decorationTitle = new QLabel("<b>装饰类型</b>");
    decorationTitle->setFont(QFont(decorationTitle->font().family(), 12));
    decorationTitle->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(decorationTitle);
    rightLayout->addSpacing(10);

    // 装饰类型 (14-27)
    for (int i = 14; i <= 27; i++) {
        auto type = static_cast<MapGenerator::TerrainType>(i);
        QString name = terrainTranslations.count(type) ?
                           terrainTranslations.at(type) :
                           QString::fromStdString(MapGenerator::MapGenerator::getTerrainName(type));

        QColor color = getTerrainColor(type);

        QWidget *itemWidget = new QWidget();
        QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(2, 2, 2, 2);

        QLabel *colorLabel = new QLabel();
        colorLabel->setFixedSize(16, 16);
        colorLabel->setStyleSheet(QString("background-color: %1; border: 1px solid #808080;").arg(color.name()));

        QLabel *nameLabel = new QLabel(name);
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        nameLabel->setStyleSheet("font-size: 9pt;");

        itemLayout->addWidget(colorLabel);
        itemLayout->addWidget(nameLabel);

        rightLayout->addWidget(itemWidget);
    }

    rightLayout->addSpacing(20);

    // 资源类型标题
    QLabel *resourceTitle = new QLabel("<b>资源类型</b>");
    resourceTitle->setFont(QFont(resourceTitle->font().family(), 12));
    resourceTitle->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(resourceTitle);
    rightLayout->addSpacing(10);

    // 资源类型
    static const std::vector<std::pair<uint32_t, QString>> resourceTypes = {
        {1, "铁矿"}, {2, "铜矿"}, {3, "木材"},
        {4, "粘土"}, {5, "草药"}, {6, "鱼类"}
    };

    for (const auto& [resourceId, resourceName] : resourceTypes) {
        QColor color = getResourceColor(resourceId);

        QWidget *itemWidget = new QWidget();
        QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
        itemLayout->setContentsMargins(2, 2, 2, 2);

        QLabel *colorLabel = new QLabel();
        colorLabel->setFixedSize(16, 16);
        colorLabel->setStyleSheet(QString("background-color: %1; border: 1px solid #808080;").arg(color.name()));

        QLabel *nameLabel = new QLabel(resourceName);
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        nameLabel->setStyleSheet("font-size: 9pt;");

        itemLayout->addWidget(colorLabel);
        itemLayout->addWidget(nameLabel);

        rightLayout->addWidget(itemWidget);
    }

    // 将两列添加到主布局
    mainLayout->addWidget(leftColumn);
    mainLayout->addWidget(rightColumn);

    // 添加到父布局
    layout->addWidget(legendContainer);
}

void MapView::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::darkGray);
    
    if (m_scaledImage.isNull()) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, tr("No map data"));
        return;
    }
    
    // Calculate drawing area
    QRectF drawRect = QRectF(m_offset, m_scaledImage.size());
    painter.drawImage(drawRect.topLeft(), m_scaledImage);
    
    // Draw grid
    if (m_showGrid) {
        drawGrid(painter);
    }
    
    // Draw coordinates
    if (m_showCoordinates && m_mapData) {
        drawCoordinates(painter);
    }
    
    // Draw border
    painter.setPen(QPen(Qt::black, 2));
    painter.drawRect(drawRect);
}

void MapView::resizeEvent(QResizeEvent *event)
{
    updateView();
    QWidget::resizeEvent(event);
}

void MapView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y();
        int newZoom = m_zoomLevel + (delta > 0 ? 10 : -10);
        setZoomLevel(newZoom);
        event->accept();
    } else {
        // Pan vertically
        m_offset.ry() += event->angleDelta().y() / 8.0;
        update();
        event->accept();
    }
}

void MapView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void MapView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_offset += delta;
        m_lastMousePos = event->pos();
        update();
        event->accept();
    }
    
    // Show coordinates under mouse
    if (m_mapData && rect().contains(event->pos())) {
        QPointF imagePos = (QPointF(event->pos()) - m_offset) * (100.0 / m_zoomLevel);
        int x = qBound(0, (int)imagePos.x(), (int)m_mapData->config.width - 1);
        int y = qBound(0, (int)imagePos.y(), (int)m_mapData->config.height - 1);
        
        uint32_t idx = y * m_mapData->config.width + x;
        float height = m_mapData->heightMap[idx];
        auto terrain = static_cast<MapGenerator::TerrainType>(m_mapData->terrainMap[idx]);
        // auto decoration = static_cast<MapGenerator::TerrainType>(m_mapData->decorationMap[idx]);
        
        QString tooltip = QString("X: %1, Y: %2\n"
                                 "Height: %3\n"
                                 "Terrain: %4"
                                 /*"Decoration: %5"*/)
                         .arg(x).arg(y)
                         .arg(height, 0, 'f', 3)
                              .arg(MapGenerator::MapGenerator::getTerrainName(terrain).c_str());
                         // .arg(MapGenerator::MapGenerator::getTerrainName(decoration).c_str());
        
        setToolTip(tooltip);
    }
}

void MapView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
    }
}

void MapView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        setZoomLevel(m_zoomLevel + 10);
        break;
    case Qt::Key_Minus:
        setZoomLevel(m_zoomLevel - 10);
        break;
    case Qt::Key_0:
        setZoomLevel(100);
        m_offset = QPointF(0, 0);
        break;
    case Qt::Key_G:
        setGridVisible(!m_showGrid);
        break;
    case Qt::Key_C:
        setCoordinatesVisible(!m_showCoordinates);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void MapView::updateView()
{
    if (m_image.isNull()) return;
    
    // Calculate scaled image size
    QSize scaledSize = m_image.size() * m_zoomLevel / 100;
    
    // Scale image with high quality
    m_scaledImage = m_image.scaled(scaledSize, 
                                   Qt::KeepAspectRatio, 
                                   Qt::SmoothTransformation);
    
    // Center the image if offset is at origin
    if (m_offset.isNull()) {
        m_offset = QPointF((width() - scaledSize.width()) / 2.0,
                          (height() - scaledSize.height()) / 2.0);
    }
    
    // Keep image within bounds
    m_offset.setX(std::min(0.0, std::max<qreal>(width() - scaledSize.width(), m_offset.x())));
    m_offset.setY(std::min(0.0, std::max<qreal>(height() - scaledSize.height(), m_offset.y())));
    
    update();
}

void MapView::generateImage()
{
    if (!m_mapData) return;
    
    uint32_t width = m_mapData->config.width;
    uint32_t height = m_mapData->config.height;
    
    m_image = QImage(width, height, QImage::Format_RGB32);
    
    // Count legend items
    std::map<MapGenerator::TerrainType, int> terrainCounts;
    std::map<MapGenerator::TerrainType, int> decorationCounts;
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t idx = y * width + x;
            QColor color;
            
            switch (m_viewType) {
            case 0: // Height map
                color = getHeightColor(m_mapData->heightMap[idx]);
                break;
                
            case 1: // Terrain map
                {
                    auto terrain = static_cast<MapGenerator::TerrainType>(m_mapData->terrainMap[idx]);
                    color = getTerrainColor(terrain);
                    terrainCounts[terrain]++;
                }
                break;
                
            case 2: // Decoration map
                {
                    auto decoration = static_cast<MapGenerator::TerrainType>(m_mapData->decorationMap[idx]);
                    color = getTerrainColor(decoration);
                    decorationCounts[decoration]++;
                }
                break;
                
            case 3: // Composite map
                {
                    auto terrain = static_cast<MapGenerator::TerrainType>(m_mapData->terrainMap[idx]);
                    // auto decoration = static_cast<MapGenerator::TerrainType>(m_mapData->decorationMap[idx]);
                    
                    QColor terrainColor = getTerrainColor(terrain);
                    // QColor decorationColor = getTerrainColor(decoration);
                    
                    // // Blend colors if decoration is not default grass
                    // if (decoration != MapGenerator::TerrainType::GRASS) {
                    //     color = QColor(
                    //         (terrainColor.red() + decorationColor.red()) / 2,
                    //         (terrainColor.green() + decorationColor.green()) / 2,
                    //         (terrainColor.blue() + decorationColor.blue()) / 2
                    //     );
                    // } else {
                        color = terrainColor;
                    // }
                }
                break;
                
            case 4: // Resource map
                {
                    uint32_t resource = m_mapData->resourceMap[idx];
                    color = getResourceColor(resource);
                }
                break;
            }
            
            m_image.setPixelColor(x, y, color);
        }
    }
    
    // Update legend counts for current view type
    if (m_viewType == 1) {
        m_legendItems.clear();
        for (const auto& [type, count] : terrainCounts) {
            if (count > 0) {
                m_legendItems.push_back({
                    MapGenerator::MapGenerator::getTerrainName(type).c_str(),
                    getTerrainColor(type),
                    count
                });
            }
        }
    } else if (m_viewType == 2) {
        m_legendItems.clear();
        for (const auto& [type, count] : decorationCounts) {
            if (count > 0 && type != MapGenerator::TerrainType::GRASS) {
                m_legendItems.push_back({
                    MapGenerator::MapGenerator::getTerrainName(type).c_str(),
                    getTerrainColor(type),
                    count
                });
            }
        }
    }
}

void MapView::drawGrid(QPainter &painter)
{
    if (!m_mapData || m_zoomLevel < 50) return;
    
    painter.save();
    
    QPen gridPen(QColor(255, 255, 255, 80));
    gridPen.setWidth(1);
    painter.setPen(gridPen);
    
    uint32_t gridSize = m_zoomLevel > 200 ? 1 : 
                       m_zoomLevel > 100 ? 5 : 
                       m_zoomLevel > 50 ? 10 : 20;
    
    float scale = m_zoomLevel / 100.0f;
    
    // Draw vertical lines
    for (uint32_t x = 0; x <= m_mapData->config.width; x += gridSize) {
        float screenX = m_offset.x() + x * scale;
        painter.drawLine(QPointF(screenX, m_offset.y()),
                        QPointF(screenX, m_offset.y() + m_mapData->config.height * scale));
    }
    
    // Draw horizontal lines
    for (uint32_t y = 0; y <= m_mapData->config.height; y += gridSize) {
        float screenY = m_offset.y() + y * scale;
        painter.drawLine(QPointF(m_offset.x(), screenY),
                        QPointF(m_offset.x() + m_mapData->config.width * scale, screenY));
    }
    
    painter.restore();
}

void MapView::drawCoordinates(QPainter &painter)
{
    if (!m_mapData || m_zoomLevel < 100) return;
    
    painter.save();
    
    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 180));
    
    float scale = m_zoomLevel / 100.0f;
    
    // Draw coordinate labels every 50 pixels at current zoom
    int step = qMax(50.0f / scale, 10.0f);
    
    for (uint32_t x = 0; x < m_mapData->config.width; x += step) {
        float screenX = m_offset.x() + x * scale;
        painter.drawText(QRectF(screenX - 20, m_offset.y() - 20, 40, 15),
                        Qt::AlignCenter, QString::number(x));
    }
    
    for (uint32_t y = 0; y < m_mapData->config.height; y += step) {
        float screenY = m_offset.y() + y * scale;
        painter.drawText(QRectF(m_offset.x() - 25, screenY - 7, 20, 15),
                        Qt::AlignRight, QString::number(y));
    }
    
    painter.restore();
}

QColor MapView::getTerrainColor(MapGenerator::TerrainType type) const
{
    switch (type) {
        case MapGenerator::TerrainType::DEEP_OCEAN:
            return QColor(10, 45, 110);
        case MapGenerator::TerrainType::SHALLOW_OCEAN:
            return QColor(25, 90, 180);
        case MapGenerator::TerrainType::COAST:
            return QColor(230, 210, 160);
        case MapGenerator::TerrainType::BEACH:
            return QColor(240, 230, 190);
        case MapGenerator::TerrainType::PLAIN:
            return QColor(100, 180, 90);
        case MapGenerator::TerrainType::FOREST:
            return QColor(30, 120, 60);
        case MapGenerator::TerrainType::HILL:
            return QColor(140, 160, 100);
        case MapGenerator::TerrainType::MOUNTAIN:
            return QColor(120, 110, 100);
        case MapGenerator::TerrainType::SNOW_MOUNTAIN:
            return QColor(240, 240, 240);
        case MapGenerator::TerrainType::DESERT:
            return QColor(230, 210, 120);
        case MapGenerator::TerrainType::SWAMP:
            return QColor(80, 140, 100);
        case MapGenerator::TerrainType::RIVER:
            return QColor(60, 140, 220);
        case MapGenerator::TerrainType::LAKE:
            return QColor(40, 110, 200);
        case MapGenerator::TerrainType::TREE_DENSE:
            return QColor(20, 100, 40);
        case MapGenerator::TerrainType::TREE_SPARSE:
            return QColor(40, 130, 60);
        case MapGenerator::TerrainType::TREE_PALM:
            return QColor(60, 150, 80);
        case MapGenerator::TerrainType::TREE_SNOW:
            return QColor(220, 240, 240);
        case MapGenerator::TerrainType::ROCK_SMALL:
            return QColor(100, 90, 80);
        case MapGenerator::TerrainType::ROCK_LARGE:
            return QColor(70, 60, 50);
        case MapGenerator::TerrainType::BUSH:
            return QColor(80, 160, 80);
        case MapGenerator::TerrainType::FLOWERS:
            return QColor(255, 100, 150);
        case MapGenerator::TerrainType::GRASS:
            return QColor(120, 200, 100);
        case MapGenerator::TerrainType::SAND:
            return QColor(240, 230, 180);
        case MapGenerator::TerrainType::CLAY:
            return QColor(180, 160, 140);
        case MapGenerator::TerrainType::SNOW:
            return QColor(255, 255, 255);
        case ::MapGenerator::TerrainType::WATER:
            return QColor(30, 120, 180);
        case ::MapGenerator::TerrainType::REEDS:
            return QColor(80, 200, 100);
        default:
            return Qt::black;
    }
}

QColor MapView::getHeightColor(float height) const
{
    // Blue to white gradient
    int value = qBound(0, static_cast<int>(height * 255), 255);
    return QColor(value, value, 255);
}

QColor MapView::getResourceColor(uint32_t resource) const
{
    static const std::unordered_map<uint32_t, QColor> resourceColors = {
        {0, Qt::darkGray},    // 无资源
        {1, QColor(150, 80, 80)},     // 铁矿 - 红棕色
        {2, QColor(200, 120, 60)},    // 铜矿 - 铜橙色
        {3, QColor(100, 60, 30)},     // 木材 - 深棕色
        {4, QColor(180, 160, 140)},   // 粘土 - 浅棕色
        {5, QColor(100, 200, 100)},   // 草药 - 亮绿色
        {6, QColor(100, 150, 255)}    // 鱼类 - 浅蓝色
    };

    auto it = resourceColors.find(resource);
    return it != resourceColors.end() ? it->second : Qt::magenta; // 未知资源使用洋红色
}
