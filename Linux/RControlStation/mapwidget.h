/*
    Copyright 2012 - 2019 Benjamin Vedder	benjamin@vedder.se

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QWidget>
#include <QSet>
#include <QPainter>
#include <QPaintEvent>
#include <QBrush>
#include <QFont>
#include <QPen>
#include <QPalette>
#include <QList>
#include <QInputDialog>
#include <QTimer>
#include <QPinchGesture>
#include <QImage>
#include <QTransform>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>

#include "locpoint.h"
#include "carinfo.h"
#include "perspectivepixmap.h"
#include "osmclient.h"
#include "database.h"
#include "maproutecollection.h"

class MainWindow;

class MapModule
{
public:
    virtual void processPaint(QPainter &painter, int width, int height, bool highQuality,
                              QTransform drawTrans, QTransform txtTrans, double scale) = 0;
    virtual bool processMouse(bool isPress, bool isRelease, bool isMove, bool isWheel,
                              QPoint widgetPos, LocPoint mapPos, double wheelAngleDelta,
                              bool ctrl, bool shift, bool ctrlShift,
                              bool leftButton, bool rightButton, double scale) = 0;
};

class MapRoute;
class MapRouteCollection;

class MapWidget : public QWidget
{
    Q_OBJECT

    friend class MapRoute;
public:
    using MouseEventHandler = std::function<void(QMouseEvent*)>;
    using WheelEventHandler = std::function<void(QWheelEvent*)>;
    void setMousePressEventHandler(MouseEventHandler handler);
    void setMouseReleaseEventHandler(MouseEventHandler handler);
    void setWheelEventHandler(WheelEventHandler handler);
    void mousePressEventFields(QMouseEvent *e);
    void mousePressEventPaths(QMouseEvent *e);
    void mousePressEventAnalysis(QMouseEvent *e);
    void mouseReleaseEventFields(QMouseEvent *e);
    void mouseReleaseEventPaths(QMouseEvent *e);
    void wheelEventFields(QWheelEvent *e);
    void wheelEventPaths(QWheelEvent *e);

    typedef enum {
        InteractionModeDefault,
        InteractionModeCtrlDown,
        InteractionModeShiftDown,
        InteractionModeCtrlShiftDown
    } InteractionMode;

    explicit MapWidget(QWidget *parent = 0);

    CarInfo* getCarInfo(int car);
    void setFollowCar(int car);
    void setTraceCar(int car);
    void setSelectedCar(int car);
    void addCar(const CarInfo &car);
    bool removeCar(int carId);
    void clearCars();
    void setScaleFactor(double scale);
    double getScaleFactor();
    void setRotation(double rotation);
    void setXOffset(double offset);
    void setYOffset(double offset);
    void moveView(double px, double py);
    void clearTrace();
    void addRoutePoint(double px, double py, double speed = 0.0, qint32 time = 0);

    void setBorderFocus(bool focus);
    bool saveRoutes(QString filename);

    MapRoute& getPath(int ind = -1);
    QList<MapRoute>& getPaths();
    void setPath(const MapRoute &route);
    void addPath(const MapRoute &route);
    int getPathNow() const;
    void setPathNow(int routeNow);
    int getPathNum();
    void clearPath();
    void clearAllPaths();

    MapRoute& getCurrentPath();
    MapRoute& getField(int ind = -1);
    QList<MapRoute>& getFields();
    void setField(const MapRoute &field);
    void addField(const MapRoute &field);
    int getFieldNow() const;
    void setFieldNow(int fieldNow);
    int getFieldNum();
    void clearField();
    void clearAllFields();

    void setSelectedActionAttribute(int attribute);
    int getSelectedActionAttribute() const;

    void setTrace(QVector<LocPoint> mTrace);

    void setRoutePointSpeed(double speed);
    void addInfoPoint(LocPoint &info, bool updateMap = true);
    void clearInfoTrace();
    void clearAllInfoTraces();
    void addPerspectivePixmap(PerspectivePixmap map);
    void clearPerspectivePixmaps();
    QPoint getMousePosRelative();
    QPoint getMousePosRelativeNew(QPoint EventPos);
    void setAntialiasDrawings(bool antialias);
    void setAntialiasOsm(bool antialias);
    bool getDrawOpenStreetmap() const;
    void setDrawOpenStreetmap(bool drawOpenStreetmap);
    // Flygfotot flyttat (m öster, m norr): ESRI-fotona ligger ofta 1–3 m fel mot RTK.
    void setOsmOffset(double east, double north);
    // RobAnt styr med bakhjulen: positionen är framaxelns mitt, bilen ritas bakåt från den.
    void setCarRefAtFront(int car, bool front);
    double getOsmOffsetEast() const { return mOsmOffsetE; }
    double getOsmOffsetNorth() const { return mOsmOffsetN; }
    void setEnuRef(double lat, double lon, double height);
    void getEnuRef(double *llh);
    double getOsmRes() const;
    void setOsmRes(double osmRes);
    double getInfoTraceTextZoom() const;
    void setInfoTraceTextZoom(double infoTraceTextZoom);
    OsmClient *osmClient();
    int getInfoTraceNum();
    int getInfoPointsInTrace(int trace);
    int setNextEmptyOrCreateNewInfoTrace();
    void removeLastRoutePoint();
    void zoomInOnRoute(int id, double margins, double wWidth = -1, double wHeight = -1);
    void zoomInOnFieldBorders();
    void zoomInOnPathBorders();

    int getOsmMaxZoomLevel() const;
    void setOsmMaxZoomLevel(int osmMaxZoomLevel);

    int getOsmZoomLevel() const;

    bool getDrawGrid() const;
    void setDrawGrid(bool drawGrid);

    bool getDrawOsmStats() const;
    void setDrawOsmStats(bool drawOsmStats);

    qint32 getRoutePointTime() const;
    void setRoutePointTime(const qint32 &routePointTime);

    double getTraceMinSpaceCar() const;
    void setTraceMinSpaceCar(double traceMinSpaceCar);

    double getTraceMinSpaceGps() const;
    void setTraceMinSpaceGps(double traceMinSpaceGps);

    int getInfoTraceNow() const;
    void setInfoTraceNow(int infoTraceNow);

    bool getDrawRouteText() const;
    void setDrawRouteText(bool drawRouteText);

    bool getDrawUwbTrace() const;
    void setDrawUwbTrace(bool drawUwbTrace);

    MapWidget::InteractionMode getInteractionMode() const;
    void setInteractionMode(const MapWidget::InteractionMode &controlMode);

    void addMapModule(MapModule *m);
    void removeMapModule(MapModule *m);
    void removeMapModuleLast();

    quint32 getRoutePointAttributes() const;
    void setRoutePointAttributes(const quint32 &routePointAttributes);

    LocPoint* getCurrentPoint(void);
    
    // Get the selected point index
    int getRoutePointSelected() const;
    
    // Find points with specific control state values
    // Selects points where controlId has targetValue, from first occurrence until controlId changes value
    QList<int> findPointsWithControlState(int controlId, double targetValue);
    
    // Control state methods
    QList<ControlState> getRoutePointControlStates() const;
    void setRoutePointControlStates(const QList<ControlState> &controlStates);

    bool loadXMLRoute(QXmlStreamReader* stream,bool isBorder);
    void saveXMLRoutes(QXmlStreamWriter* stream,bool withId);
    void saveXMLCurrentRoute(QXmlStreamWriter* stream);

    std::array<double, 4> findExtremeValuesFieldBorders();
    std::array<double, 4> findExtremeValuesPathBorders();
    void setLogStart(int iStart);
    void setLogEnd(int iEnd);
    int Elements();
    int firstElement();
    int lastElement();

    void setMainWindow(MainWindow* _mw);
    void setDb(database* _db);
    void setAnalysisActive(bool state);
signals:
    void scaleChanged(double newScale);
    void offsetChanged(double newXOffset, double newYOffset);
    void posSet(quint8 id, LocPoint pos);
    void routePointAdded(LocPoint pos);
    void lastRoutePointRemoved(LocPoint pos);
    void infoTraceChanged(int traceNow);
    void activePointChanged(LocPoint point);

    void mouseClickedInField(const int field);
    void pathsUpdated();

private slots:
    void tileReady(OsmTile tile);
    void errorGetTile(QString reason);
    void timerSlot();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent (QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

public:
    MapRouteCollection* mFields;
    MapRouteCollection* mPaths;

private:
    MouseEventHandler mousePressEventHandler;
    MouseEventHandler mouseReleaseEventHandler;
    WheelEventHandler wheelEventHandler;

    QList<CarInfo> mCarInfo;
    QVector<LocPoint> mCarTrace;
    QVector<LocPoint> mCarTraceGps;
    QVector<LocPoint> mCarTraceUwb;
    QVector<LocPoint> mLoadedTrace;

    QList<QList<LocPoint>> mInfoTraces;
    //    QList<MapRoute> mInfoTraces;
    QList<LocPoint> mVisibleInfoTracePoints;
    QList<PerspectivePixmap> mPerspectivePixmaps;
    double mRoutePointSpeed;
    qint32 mRoutePointTime;
    quint32 mRoutePointAttributes;
    QList<ControlState> mRoutePointControlStates;
    double mScaleFactor;
    double mRotation;
    double mXOffset;
    double mYOffset;
    int mMouseLastX;
    int mMouseLastY;
    int mFollowCar;
    int mTraceCar;
    int mSelectedCar;
    double xRealPos;
    double yRealPos;
    bool mAntialiasDrawings;
    bool mAntialiasOsm;
    double mOsmRes;
    double mInfoTraceTextZoom;
    OsmClient *mOsm;
    int mOsmZoomLevel;
    int mOsmMaxZoomLevel;
    bool mDrawOpenStreetmap;
    double mOsmOffsetE = 0.0;
    QSet<int> mCarRefAtFront;
    double mOsmOffsetN = 0.0;
    bool mDrawOsmStats;
    double mRefLat;
    double mRefLon;
    double mRefHeight;
    LocPoint mClosestInfo;
    bool mDrawGrid;
    int mRoutePointSelected;
    int mAnchorSelected;
    int mInfoTraceNow;
    QList<int> mSelectedControlPoints; // Points selected by control search
    double mTraceMinSpaceCar;
    double mTraceMinSpaceGps;
    bool mAnchorMode;
    bool mDrawRouteText;
    bool mDrawUwbTrace;
    int mSelectedActionAttribute;
    QImage mLastCameraImage;
    double mCameraImageWidth;
    double mCameraImageOpacity;
    InteractionMode mInteractionMode;
    QTimer *mTimer;
    QVector<MapModule*> mMapModules;

    void updateClosestInfoPoint();
    int drawInfoPoints(QPainter &painter, const QList<LocPoint> &pts,
                       QTransform drawTrans, QTransform txtTrans,
                       double xStart, double xEnd, double yStart, double yEnd,
                       double min_dist);
    int getClosestPoint(LocPoint p, QList<LocPoint> route, double &dist);
//    int getClosestPoint(LocPoint p, MapRoute route, double &dist);

    void paint(QPainter &painter, int width, int height, bool highQuality = false);
    void updateTraces();
    void paintTiles(QPainter &painter,const double cx, const double cy, const double view_w, const double view_h, bool highQuality);
    void paintGrid(QPainter &painter,QPen &pen, QTransform drawTrans, QTransform txtTrans, int width,int height, double stepGrid,QString txt, QPointF pt_txt,const QColor textColor, const QColor zeroAxisColor,const QColor firstAxisColor,const QColor secondAxisColor,const double zeroAxisWidth);
    void paintCar(CarInfo &carInfo,QPainter &painter,QPen &pen, QTransform drawTrans, QTransform txtTrans,QString &txt, QPointF &pt_txt,const QColor &textColor,QRectF &rect_txt);
    void paintRobAnt(QPainter &painter, const QColor &hull, const QColor &wheels, const QColor &front);
    void paintAnchor(LocPoint &anchor,QPainter &painter,QPen &pen, QTransform drawTrans, QTransform txtTrans,QString &txt, QPointF &pt_txt,const QColor &textColor,QRectF &rect_txt);
    void paintTrace(QList<LocPoint> &itNow,QPainter &painter,QPen &pen, bool isActive, QTransform drawTrans, QTransform txtTrans,const double cx, const double cy, const double view_w, const double view_h,int& info_segments, int& info_points);
    void paintCarTraces(QPainter &painter,QPen &pen, QTransform drawTrans);
    void paintUnitZoomGeneralinfo(QPainter &painter,QFont font, QTransform txtTrans, int width, double stepGrid,QString& txt, const QColor textColor, double& start_txt,const double txtOffset,const double txt_row_h,    int& info_segments, int& info_points);
    void paintClosestPoint(QPainter &painter,QPen &pen,QTransform drawTrans, QTransform txtTrans, QPointF& pt_txt,QRectF &rect_txt );
    void paintTraceVechicle(QPainter &painter,QPen &pen, QTransform drawTrans,QVector<LocPoint> mTrace,double traceWidth,QColor traceColour);

    void setFarm(double px,double py);
    bool getCurrentRoute(MapRouteCollection* mRoutes, int& routeindex, int& mRoutePointSelected, LocPoint mousePosMap,double maxSnapDistance);
    void changeField(QMouseEvent *e,LocPoint mousePosMap);
    void updateAnalysis(bool state);
    int iLogstart,iLogend;
    bool focusBorder;
    bool bUpdateable;
    bool hasChanged;
    MainWindow* mw;
    database* db;
    bool bAnalysisActive;
};

#endif // MAPWIDGET_H

