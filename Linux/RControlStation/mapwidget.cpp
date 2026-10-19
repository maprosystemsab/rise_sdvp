/*
    Copyright 2016 - 2017 Benjamin Vedder	benjamin@vedder.se

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

#include <QDebug>
#include <math.h>
#include <qmath.h>
#include <QPrinter>
#include <QPrintEngine>
#include <QTime>

#include "mapwidget.h"
#include "qmessagebox.h"
#include "utility.h"
#include "routemagic.h"
#include "mainwindow.h"

namespace
{
static void normalizeAngleRad(double &angle)
{
    angle = fmod(angle, 2.0 * M_PI);

    if (angle < 0.0) {
        angle += 2.0 * M_PI;
    }
}

void minMaxEps(double x, double y, double &min, double &max) {
    double eps;

    if (fabs(x) > fabs(y)) {
        eps = fabs(x) / 1e10;
    } else {
        eps = fabs(y) / 1e10;
    }

    if (x > y) {
        max = x + eps;
        min = y - eps;
    } else {
        max = y + eps;
        min = x - eps;
    }
}

bool isPointWithinRect(const QPointF &p, double xStart, double xEnd, double yStart, double yEnd) {
    return p.x() >= xStart && p.x() <= xEnd && p.y() >= yStart && p.y() <= yEnd;
}

// See https://www.topcoder.com/community/data-science/data-science-tutorials/geometry-concepts-line-intersection-and-its-applications/
bool lineSegmentIntersection(const QPointF &p1, const QPointF &p2, const QPointF &q1, const QPointF &q2) {
    bool res = false;

    const double A1 = p2.y() - p1.y();
    const double B1 = p1.x() - p2.x();
    const double C1 = A1 * p1.x() + B1 * p1.y();

    const double A2 = q2.y() - q1.y();
    const double B2 = q1.x() - q2.x();
    const double C2 = A2 * q1.x() + B2 * q1.y();

    const double det = A1 * B2 - A2 * B1;

    if(fabs(det) < 1e-6) {
        //Lines are parallel
    } else {
        double x = (B2 * C1 - B1 * C2) / det;
        double y = (A1 * C2 - A2 * C1) / det;

        // Check if this point is on both line segments.
        double p1XMin, p1XMax, p1YMin, p1YMax;
        minMaxEps(p1.x(), p2.x(), p1XMin, p1XMax);
        minMaxEps(p1.y(), p2.y(), p1YMin, p1YMax);

        double q1XMin, q1XMax, q1YMin, q1YMax;
        minMaxEps(q1.x(), q2.x(), q1XMin, q1XMax);
        minMaxEps(q1.y(), q2.y(), q1YMin, q1YMax);

        if (    x <= p1XMax && x >= p1XMin &&
            y <= p1YMax && y >= p1YMin &&
            x <= q1XMax && x >= q1XMin &&
            y <= q1YMax && y >= q1YMin) {
            res = true;
        }
    }

    return res;
}

bool isLineSegmentWithinRect(const QPointF &p1, const QPointF &p2, double xStart, double xEnd, double yStart, double yEnd) {
    QPointF q1(xStart, yStart);
    QPointF q2(xEnd, yStart);

    bool res = lineSegmentIntersection(p1, p2, q1, q2);

    if (!res) {
        q1.setX(xStart);
        q1.setY(yStart);
        q2.setX(xStart);
        q2.setY(yEnd);
        res = lineSegmentIntersection(p1, p2, q1, q2);
    }

    if (!res) {
        q1.setX(xStart);
        q1.setY(yEnd);
        q2.setX(xEnd);
        q2.setY(yEnd);
        res = lineSegmentIntersection(p1, p2, q1, q2);
    }

    if (!res) {
        q1.setX(xEnd);
        q1.setY(yStart);
        q2.setX(xEnd);
        q2.setY(yEnd);
        res = lineSegmentIntersection(p1, p2, q1, q2);
    }

    return res;
}
}

MapWidget::MapWidget(QWidget *parent) : QWidget(parent)
{
    mScaleFactor = 0.1;
    mRotation = 0;
    mXOffset = 0;
    mYOffset = 0;
    mMouseLastX = 1000000;
    mMouseLastY = 1000000;
    mFollowCar = -1;
    // Utan startvärden fick nya ruttpunkter skräp från minnet: attributet 0x6518
    // (2026-09-28) betyder "bakre armar upp" för autopiloten (ATTR_HYDRAULIC_REAR_UP).
    mRoutePointAttributes = 0;
    mRoutePointSpeed = 0.0; // sätts från V-rutan av MainWindow vid start
    mRoutePointTime = 0;
    mTraceCar = -1;
    mSelectedCar = -1;
    xRealPos = 0;
    yRealPos = 0;
//    mAnchorId = 0;
//    mAnchorHeight = 1.0;
    mAntialiasDrawings = false;
    mAntialiasOsm = true;
    mInfoTraceTextZoom = 0.5;
    mDrawGrid = true;
    mRoutePointSelected = -1;
    mAnchorSelected = -1;
    mTraceMinSpaceCar = 0.05;
    mTraceMinSpaceGps = 0.05;
    mInfoTraceNow = 0;
    mAnchorMode = false;
    mSelectedControlPoints = QList<int>();
    mDrawRouteText = false;
    mDrawUwbTrace = false;
    mSelectedActionAttribute = 0;
    mCameraImageWidth = 0.46;
    mCameraImageOpacity = 0.8;
    hasChanged=false;

    bUpdateable=true;
    bAnalysisActive=false;
    mOsm = new OsmClient(this);
    mDrawOpenStreetmap = true;
    mOsmZoomLevel = 14;
    mOsmRes = 1.0;
    mOsmMaxZoomLevel = 18;
    mDrawOsmStats = false;

    focusBorder=false;
    mPaths=new MapRouteCollection();
    mFields=new MapRouteCollection();

    mPaths->clear();

    mTimer = new QTimer(this);
    mTimer->start(20);

    // Set this to the SP base station position for now
    // Ultuna
    mRefLat = 59.812;
    mRefLon = 17.658;
    mRefHeight = 38;

    // Hardcoded for now
    mOsm->setCacheDir("osm_tiles");
    mOsm->setTileServerUrl("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/");
    //    mOsm->setTileServerUrl("http://tile.openstreetmap.org");

    connect(mOsm, SIGNAL(tileReady(OsmTile)), this, SLOT(tileReady(OsmTile)));
    connect(mOsm, SIGNAL(errorGetTile(QString)), this, SLOT(errorGetTile(QString)));
    connect(mTimer, SIGNAL(timeout()), this, SLOT(timerSlot()));

    setMouseTracking(true);

    grabGesture(Qt::PinchGesture);
    // Install the event filter on itself
//    MapRoute::initPixmaps();
    
    // Set focus policy to accept key events
    setFocusPolicy(Qt::StrongFocus);
}

CarInfo *MapWidget::getCarInfo(int car)
{
    for (int i = 0;i < mCarInfo.size();i++) {
        if (mCarInfo[i].getId() == car) {
            return &mCarInfo[i];
        }
    }

    return 0;
}

// Function to set the mouse event handler
void MapWidget::setMousePressEventHandler(MouseEventHandler handler) {
    mousePressEventHandler = handler;
}

// Function to set the mouse event handler
void MapWidget::setMouseReleaseEventHandler(MouseEventHandler handler) {
    mouseReleaseEventHandler = handler;
}

// Function to set the mouse event handler
void MapWidget::setWheelEventHandler(WheelEventHandler handler) {
    wheelEventHandler = handler;
}

void MapWidget::addCar(const CarInfo &car)
{
    mCarInfo.append(car);
    update();
}

bool MapWidget::removeCar(int carId)
{
    QMutableListIterator<CarInfo> i(mCarInfo);
    while (i.hasNext()) {
        if (i.next().getId() == carId) {
            i.remove();
            return true;
        }
    }

    return false;
}

void MapWidget::clearCars()
{
    mCarInfo.clear();
}

void MapWidget::setScaleFactor(double scale)
{
    double scaleDiff = scale / mScaleFactor;
    mScaleFactor = scale;
    mXOffset *= scaleDiff;
    mYOffset *= scaleDiff;
    update();
}

double MapWidget::getScaleFactor()
{
    return mScaleFactor;
}

void MapWidget::setRotation(double rotation)
{
    mRotation = rotation;
    update();
}

void MapWidget::setXOffset(double offset)
{
    mXOffset = offset;
    update();
}

void MapWidget::setYOffset(double offset)
{
    mYOffset = offset;
    update();
}

void MapWidget::moveView(double px, double py)
{
    LocPoint followLoc;
    followLoc.setXY(px, py);
    mXOffset = -followLoc.getX() * 1000.0 * mScaleFactor;
    mYOffset = -followLoc.getY() * 1000.0 * mScaleFactor;
    update();
}

void MapWidget::clearTrace()
{
    mCarTrace.clear();
    mCarTraceGps.clear();
    mCarTraceUwb.clear();
    update();
}

void MapWidget::addRoutePoint(double px, double py, double speed, qint32 time)
{
    LocPoint pos;
    pos.setXY(px, py);
    pos.setSpeed(speed);
    pos.setTime(time);
    mPaths->getCurrent().append(pos);
    update();
}

MapRoute& MapWidget::getPath(int ind)
{
    return mPaths->getRoute(ind);
}

MapRoute& MapWidget::getField(int ind)
{
    return mFields->getRoute(ind);
}


QList<MapRoute>& MapWidget::getPaths()
{
    return mPaths->getRoutes();
}

QList<MapRoute>& MapWidget::getFields()
{
    return mFields->getRoutes();
}

void MapWidget::setPath(const MapRoute &path)
{
    mPaths->setRoute(path);
    update();
}

void MapWidget::setField(const MapRoute &field)
{
    mFields->setRoute(field);
    update();
}


void MapWidget::addPath(const MapRoute &path)
{
    mPaths->addRoute(path);
    update();
}

void MapWidget::addField(const MapRoute &field)
{
    int s1=mFields->size();
    mFields->addRoute(field);
    int s2=mFields->size();
    update();
}


int MapWidget::getPathNum()
{
    return mPaths->size();
}

int MapWidget::getFieldNum()
{
    return mFields->size();
}

void MapWidget::clearPath()
{
    mPaths->clearRoute();
    update();
}

void MapWidget::clearField()
{
    mFields->clearRoute();
    update();
}

void MapWidget::clearAllPaths()
{
    mPaths->clearAllRoutes();
    update();
}

void MapWidget::clearAllFields()
{
    mFields->clearAllRoutes();
    update();
}

int MapWidget::getPathNow() const
{
    return mPaths->mRouteNow;
}

void MapWidget::setPathNow(int pathNow)
{
    mPaths->setRouteNow(pathNow);
    update();
}

int MapWidget::getFieldNow() const
{
    return mFields->mRouteNow;
}

void MapWidget::setFieldNow(int fieldNow)
{
    mFields->setRouteNow(fieldNow);
    update();
}

void MapWidget::setSelectedActionAttribute(int attribute)
{
    mSelectedActionAttribute = attribute;
    update();
}

int MapWidget::getSelectedActionAttribute() const
{
    return mSelectedActionAttribute;
}


void MapWidget::setBorderFocus(bool focus)
{
    focusBorder=focus;
}

LocPoint* MapWidget::getCurrentPoint(void)
{
    // Check if we have paths and the current path is valid
    if (mPaths && mPaths->size() > 0) {
        MapRoute& currentRoute = mPaths->getCurrent();
        qDebug() << "Route size: " << currentRoute.size() ;
        // Check if the route has points and the current point index is valid
        if (currentRoute.size() > 0 && currentRoute.getActivePointNo() >= 0 && currentRoute.getActivePointNo() < currentRoute.size()) {
            qDebug() << "Point No: " << currentRoute.getActivePointNo();
            return &currentRoute.mRoute[currentRoute.getActivePointNo()];
        }
    }
    return nullptr; // Return null if no valid point is selected
}

void MapWidget::setRoutePointSpeed(double speed)
{
    qDebug() << "setRoutePointSpeed()";
    // Nya punkter får den här farten (V-rutan). Förut sattes bara den markerade
    // punkten, så nya punkter fick oinitierat minne som fart (0 km/h 2026-09-28).
    mRoutePointSpeed = speed;
    LocPoint* currentPoint = getCurrentPoint();
    if (currentPoint) {
        qDebug() << "OK";
        qDebug() << "Speed: " << speed;
        currentPoint->setSpeed(speed);

        MapRoute& currentRoute = mPaths->getCurrent();
        qDebug() << "Route size: " << currentRoute.size() ;
        for (int i=0;i< currentRoute.size();i++)
        {
            qDebug() << "Point No: " << i << ", speed: " <<  currentRoute.at(i).getSpeed();
        }
    } else {
        qDebug() << "Not OK";
        qWarning() << "setRoutePointSpeed: No current point selected or invalid point index";
    }
}

void MapWidget::addInfoPoint(LocPoint &info, bool updateMap)
{
    QList<LocPoint> l;
    while (mInfoTraces.size() < (mInfoTraceNow + 1)) {
        mInfoTraces.append(l);
    }

    mInfoTraces[mInfoTraceNow].append(info);

    if (updateMap) {
        update();
    }
}

void MapWidget::clearInfoTrace()
{
    QList<LocPoint> l;
    while (mInfoTraces.size() < (mInfoTraceNow + 1)) {
        mInfoTraces.append(l);
    }

    mInfoTraces[mInfoTraceNow].clear();
    update();
}

void MapWidget::clearAllInfoTraces()
{
    for (int i = 0;i < mInfoTraces.size();i++) {
        mInfoTraces[i].clear();
    }

    update();
}

void MapWidget::addPerspectivePixmap(PerspectivePixmap map)
{
    mPerspectivePixmaps.append(map);
}

void MapWidget::clearPerspectivePixmaps()
{
    mPerspectivePixmaps.clear();
    update();
}

QPoint MapWidget::getMousePosRelative()
{
    QPoint p = this->mapFromGlobal(QCursor::pos());
    p.setX((p.x() - mXOffset - width() / 2) / mScaleFactor);
    p.setY((-p.y() - mYOffset + height() / 2) / mScaleFactor);
    return p;
}

QPoint MapWidget::getMousePosRelativeNew(QPoint EventPos)
{
    QPoint p;
    p.setX((EventPos.x() - mXOffset - width() / 2) / mScaleFactor);
    p.setY((-EventPos.y() - mYOffset + height() / 2) / mScaleFactor);
    return p;
}

void MapWidget::setAntialiasDrawings(bool antialias)
{
    mAntialiasDrawings = antialias;
    update();
}

void MapWidget::setAntialiasOsm(bool antialias)
{
    mAntialiasOsm = antialias;
    update();
}

void MapWidget::tileReady(OsmTile tile)
{
    (void)tile;
    update();
}

void MapWidget::errorGetTile(QString reason)
{
    qWarning() << "OSM tile error:" << reason;
}

void MapWidget::timerSlot()
{
    updateTraces();
}

void MapWidget::setFollowCar(int car)
{
    int oldCar = mFollowCar;
    mFollowCar = car;

    if (oldCar != mFollowCar) {
        update();
    }
}

void MapWidget::setTraceCar(int car)
{
    mTraceCar = car;
}

void MapWidget::setSelectedCar(int car)
{
    int oldCar = mSelectedCar;
    mSelectedCar = car;

    if (oldCar != mSelectedCar) {
        update();
    }
}

void MapWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    paint(painter, event->rect().width(), event->rect().height());
}

void MapWidget::mouseMoveEvent(QMouseEvent *e)
{
    bool ctrl = e->modifiers() == Qt::ControlModifier;
    bool shift = e->modifiers() == Qt::ShiftModifier;
    bool ctrl_shift = e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);

    QPoint mousePosWidget = e->pos();
    LocPoint mousePosMap;
    QPoint p = getMousePosRelativeNew(e->pos());
    mousePosMap.setXY(p.x() / 1000.0, p.y() / 1000.0);

    for (MapModule *m: mMapModules) {
        if (m->processMouse(false, false, true, false,
                            mousePosWidget, mousePosMap, 0.0,
                            ctrl, shift, ctrl_shift,
                            e->buttons() & Qt::LeftButton,
                            e->buttons() & Qt::RightButton,
                            mScaleFactor)) {
            return;
        }
    }

    if (e->buttons() & Qt::LeftButton && !ctrl && !shift && !ctrl_shift) {
        int x = e->pos().x();
        int y = e->pos().y();

        if (mMouseLastX < 100000)
        {
            int diffx = x - mMouseLastX;
            mXOffset += diffx;
            update();
        }

        if (mMouseLastY < 100000)
        {
            int diffy = y - mMouseLastY;
            mYOffset -= diffy;

            emit offsetChanged(mXOffset, mYOffset);
            update();
        }

        mMouseLastX = x;
        mMouseLastY = y;
    }

    if (mRoutePointSelected >= 0) {
        getCurrentPoint()->setXY(mousePosMap.getX(), mousePosMap.getY());
//        mPaths->getCurrent()[mRoutePointSelected].setXY(mousePosMap.getX(), mousePosMap.getY());
        update();
    }
/*
    if (mAnchorSelected >= 0) {
        mAnchors[mAnchorSelected].setXY(mousePosMap.getX(), mousePosMap.getY());
        update();
    }
*/
    updateClosestInfoPoint();
}

void MapWidget::setFarm(double px,double py)
{
    double illh[3];
    double llh[3];
    double xyz_[3];

    getEnuRef(illh);
    xyz_[0] = px;
    xyz_[1] = py;
    xyz_[2] = 0;
    const double xyz[3]={xyz_[0], xyz_[1], xyz_[2]};
    utility::enuToLlh(illh, xyz, llh);
    qDebug() << "Move centre to: " << llh[0] << ", " << llh[1] << ", " << llh[2];
    int iFarm=mw->currentFarm();
    db->updateFarmLocation(mw->currentFarm(), llh[0], llh[1]);
    mw->updateFarms();
    qDebug() << "farm: " << iFarm;
    mw->setCurrentFarm(iFarm);
}

bool MapWidget::getCurrentRoute(MapRouteCollection* mRoutes, int& routeindex, int& mRoutePointSelected, LocPoint mousePosMap,double maxSnapDistance)
{
    if (mRoutes->size() > 0) {
        double routeDist = 999999999999.9; // i.e. a high number
        double routeDistTmp =0.0; // or any number
        for (int i=0;i<mRoutes->size();i++)
        {
            int mRoutePointSelectedTmp = getClosestPoint(mousePosMap, mRoutes->at(i).mRoute, routeDistTmp);  // Use . instead of ->
            if (routeDistTmp<routeDist)
            {
                routeindex=i;
                routeDist=routeDistTmp;
                mRoutePointSelected=mRoutePointSelectedTmp;
                mRoutes->at(i).setActivePoint(mRoutePointSelectedTmp);
                this->mRoutePointSelected = mRoutePointSelectedTmp; // Update member variable
                
                // Emit signal to update UI when active point changes
                if (mRoutePointSelectedTmp >= 0 && mRoutePointSelectedTmp < mRoutes->at(i).size()) {
                    emit activePointChanged(mRoutes->at(i).mRoute[mRoutePointSelectedTmp]);
                }
            }
        }
        return (routeDist * mScaleFactor * 1000.0) < maxSnapDistance; // Found route?
    } else {
        return false;
    }
}

void MapWidget::changeField(QMouseEvent *e,LocPoint mousePosMap)
{
    int selectedroute=0,mRoutePointSelected=0;
    if (getCurrentRoute(mFields,selectedroute, mRoutePointSelected,mousePosMap,20.0))
    {
        qDebug() << "selectedroute: " << selectedroute;
        qDebug() << "mRoutePointSelected: " << mRoutePointSelected;
        MapRoute& currentRoute=mFields->at(selectedroute);
        qDebug() << "close";
        if (e->buttons() & Qt::LeftButton) {
            qDebug() << "mFields->at(selectedroute).size(): " << mFields->at(selectedroute).size();
            qDebug() << "mRoutePointSelected: " << mRoutePointSelected;
            currentRoute.at(mRoutePointSelected).setXY(mousePosMap.getX(), mousePosMap.getY());
            hasChanged=true;
        }  else if (e->buttons() & Qt::RightButton) {
            qDebug() << "Pressed right key";
            qDebug() << mRoutePointSelected;
            currentRoute.removeAt(mRoutePointSelected );
        }
    } else
    {
        qDebug() << "not close";
        MapRoute& currentRoute=mFields->at(getFieldNow());
        if (e->buttons() & Qt::LeftButton) {
            currentRoute.insert(mRoutePointSelected,mousePosMap);
        }  else if (e->buttons() & Qt::RightButton) {
            qDebug() << "Pressed right key";
            qDebug() << mRoutePointSelected;
            removeLastRoutePoint();
        }
    }

    /*
        if (e->buttons() & Qt::LeftButton) {
        } else if (e->buttons() & Qt::RightButton) {
            qDebug() << "Pressed right key";
            qDebug() << mRoutePointSelected;
            if (routeFound) {
                currentRoute->removeAt(mRoutePointSelected );
            } else {
                removeLastRoutePoint();
            }
        }
        qDebug() << "Updating";*/
    update();

}
void MapWidget::mousePressEventFields(QMouseEvent *e)
{
    setFocus();

    bool ctrl = e->modifiers() == Qt::ControlModifier;
    bool shift = e->modifiers() == Qt::ShiftModifier;
    bool ctrl_shift = e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);

    // Initialize mouse position for panning
    if (e->buttons() & Qt::LeftButton && !ctrl && !shift && !ctrl_shift)
    {
        mMouseLastX = e->pos().x();
        mMouseLastY = e->pos().y();
    }

    QPoint mp = e->pos();
    QPoint p = getMousePosRelativeNew(e->pos());
    double px=p.x()/1000.0;
    double py=p.y()/1000.0;

    for (int i=0;i<getFieldNum();i++)
    {
        if (RouteMagic::isPointWithin(px, py, getField(i).mRoute))
        {
            setFieldNow(i);
            emit mouseClickedInField(i+1);
        };
    };

    LocPoint mousePosMap;
    mousePosMap.setXY(px, py);
    QPoint mousePosWidget = mp;

    for (MapModule *m: mMapModules) {
        if (m->processMouse(true, false, false, false,
                            mousePosWidget, mousePosMap, 0.0,
                            ctrl, shift, ctrl_shift,
                            e->buttons() & Qt::LeftButton,
                            e->buttons() & Qt::RightButton,
                            mScaleFactor)) {
            return;
        }
    }
    mousePosMap.setSpeed(0);
    mousePosMap.setTime(0);
    mousePosMap.setAttributes(0);

    if (ctrl) {
        if (e->buttons() & Qt::RightButton) {
            setFarm(px,py);
            };
    } else if (shift) {
        qDebug() << "Shift clicked!";
        changeField(e,mousePosMap);
    } else if (ctrl_shift) {
        if (e->buttons() & Qt::LeftButton) {
            QPoint p = getMousePosRelative();
            double iLlh[3], llh[3], xyz[3];
            iLlh[0] = mRefLat;
            iLlh[1] = mRefLon;
            iLlh[2] = mRefHeight;
            xyz[0] = p.x() / 1000.0;
            xyz[1] = p.y() / 1000.0;
            xyz[2] = 0.0;
            utility::enuToLlh(iLlh, xyz, llh);
            mRefLat = llh[0];
            mRefLon = llh[1];
            mRefHeight = 0.0;
        }
        update();
    }
}

void MapWidget::mousePressEventAnalysis(QMouseEvent *e)
{
    setFocus();

    bool ctrl = e->modifiers() == Qt::ControlModifier;
    bool shift = e->modifiers() == Qt::ShiftModifier;
    bool ctrl_shift = e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);

    // Initialize mouse position for panning
    if (e->buttons() & Qt::LeftButton && !ctrl && !shift && !ctrl_shift)
    {
        mMouseLastX = e->pos().x();
        mMouseLastY = e->pos().y();
    }

    QPoint mp = e->pos();
    QPoint p = getMousePosRelativeNew(e->pos());
    double px=p.x()/1000.0;
    double py=p.y()/1000.0;


    LocPoint mousePosMap;
    mousePosMap.setXY(px, py);
    QPoint mousePosWidget = mp;

    for (MapModule *m: mMapModules) {
        if (m->processMouse(true, false, false, false,
                            mousePosWidget, mousePosMap, 0.0,
                            ctrl, shift, ctrl_shift,
                            e->buttons() & Qt::LeftButton,
                            e->buttons() & Qt::RightButton,
                            mScaleFactor)) {
            return;
        }
    }
    mousePosMap.setSpeed(mRoutePointSpeed);
    mousePosMap.setTime(mRoutePointTime);
    mousePosMap.setAttributes(mRoutePointAttributes);

    int selectedroute=0,mRoutePointSelected=0;
    if (ctrl) {
    } else if (shift) {
    } else if (ctrl_shift) {
    } else {
        if (e->buttons() & Qt::LeftButton) {
            if (getCurrentRoute(mPaths,selectedroute, mRoutePointSelected,mousePosMap,20.0))
            {
                mPaths->at(selectedroute).iAnalysisStart=mRoutePointSelected;
                mPaths->at(selectedroute).updateAnalysis();
            }
        }
        if (e->buttons() & Qt::RightButton) {
            if (getCurrentRoute(mPaths,selectedroute, mRoutePointSelected,mousePosMap,20.0))
            {
                mPaths->at(selectedroute).iAnalysisEnd=mRoutePointSelected;
                mPaths->at(selectedroute).updateAnalysis();
            }
        }
    }
}


void MapWidget::setAnalysisActive(bool state)
{
    bAnalysisActive=state;

}


void MapWidget::mousePressEventPaths(QMouseEvent *e)
{
    // Implementation for paths
    // qDebug() << "Paths: Mouse pressed at" << e->pos();

    bool ctrl = e->modifiers() == Qt::ControlModifier;
    bool shift = e->modifiers() == Qt::ShiftModifier;
    bool ctrl_shift = e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);

    // Initialize mouse position for panning
    if (e->buttons() & Qt::LeftButton && !ctrl && !shift && !ctrl_shift)
    {
        mMouseLastX = e->pos().x();
        mMouseLastY = e->pos().y();
    }

    QPoint mousePosWidget = e->pos();
    LocPoint mousePosMap;
    QPoint p = getMousePosRelativeNew(e->pos());
    mousePosMap.setXY(p.x() / 1000.0, p.y() / 1000.0);

    for (MapModule *m: mMapModules) {
        if (m->processMouse(true, false, false, false,
                            mousePosWidget, mousePosMap, 0.0,
                            ctrl, shift, ctrl_shift,
                            e->buttons() & Qt::LeftButton,
                            e->buttons() & Qt::RightButton,
                            mScaleFactor)) {
            return;
        }
    }

    mousePosMap.setSpeed(mRoutePointSpeed);
    mousePosMap.setTime(mRoutePointTime);
    mousePosMap.setAttributes(mRoutePointAttributes);
//    mousePosMap.setId(mAnchorId);
//    mousePosMap.setHeight(mAnchorHeight);

    double routeDist = 0.0;
    double anchorDist = 0.0;

//    int anchorInd = getClosestPoint(mousePosMap, mAnchors, anchorDist);
    bool routeFound = false;
    bool anchorFound = false;

    MapRoute *currentRoute;
    bool bHasCurrentRoute=false;

    if (mPaths->size()>0)
    {
 //       qDebug() << "Existing route";
 //       qDebug() << "Items: " << mPaths->size();
        currentRoute=&(mPaths->getCurrent());
        bHasCurrentRoute=true;
        this->mRoutePointSelected = getClosestPoint(mousePosMap, currentRoute->mRoute, routeDist);
        currentRoute->setActivePoint(this->mRoutePointSelected);
        
        // Emit signal to update UI when active point changes
        if (this->mRoutePointSelected >= 0 && this->mRoutePointSelected < currentRoute->size()) {
            emit activePointChanged(currentRoute->mRoute[this->mRoutePointSelected]);
        }
        routeFound = (routeDist * mScaleFactor * 1000.0) < 20 && routeDist >= 0.0;
        anchorFound = (anchorDist * mScaleFactor * 1000.0) < 20 && anchorDist >= 0.0;
        qDebug() << "Distance: " << routeDist;
    } else
    {
 //       qDebug() << "New route";
        currentRoute=new MapRoute();
    }

    if (ctrl) {
 //       qDebug() << "Left" << (e->buttons() & Qt::LeftButton);
 //       qDebug() << "Right" << (e->buttons() & Qt::RightButton);
        if (e->buttons() & Qt::LeftButton) {
            if (mSelectedCar >= 0) {
                for (int i = 0;i < mCarInfo.size();i++) {
                    CarInfo &carInfo = mCarInfo[i];
                    if (carInfo.getId() == mSelectedCar) {
                        LocPoint pos = carInfo.getLocation();
                        QPoint p = getMousePosRelative();
                        pos.setXY(p.x() / 1000.0, p.y() / 1000.0);
                        carInfo.setLocation(pos);
                        emit posSet(mSelectedCar, pos);
                    }
                }
            }
        } else if (e->buttons() & Qt::RightButton) {
            if (mAnchorMode) {} else {
                if (routeFound) {
                    LocPoint* lp=getCurrentPoint();
                    if (lp)
                    {
                        lp->setSpeed(mRoutePointSpeed);
                        lp->setTime(mRoutePointTime);
                        lp->setSpeed(mRoutePointSpeed);
                    };
/*                    (*currentRoute)[mRoutePointSelected].setSpeed(mRoutePointSpeed);
                    (*currentRoute)[mRoutePointSelected].setTime(mRoutePointTime);
                    (*currentRoute)[mRoutePointSelected].setAttributes(mRoutePointAttributes);*/
                }
            }
        }
        update();
    } else if (shift) {
        if (mAnchorMode) {} else {
//            qDebug() << "Shift route";
            if (e->buttons() & Qt::LeftButton) {
    //            qDebug() << "Shift left";
                if (routeFound) {
  /*                  qDebug() << "Found route";
                    qDebug() << "Size: " << currentRoute->size();
                    qDebug() << "Pos: " << mRoutePointSelected;*/

//                    (*currentRoute)[mRoutePointSelected].setXY(mousePosMap.getX(), mousePosMap.getY());
                     getCurrentPoint()->setXY(mousePosMap.getX(), mousePosMap.getY());
                    qDebug() << "Set position";
                } else {
                    if (currentRoute->size() < 2 ||
                        currentRoute->last().getDistanceTo(mousePosMap) < currentRoute->first().getDistanceTo(mousePosMap))
                    {
//                        qDebug() << "Shift left - Add start. Size: " << currentRoute->size();
                        currentRoute->append(mousePosMap);
                        emit routePointAdded(mousePosMap);
                    } else {
//                        qDebug() << "Shift left - Add end";
                        currentRoute->prepend(mousePosMap);
                    }
                    if (!bHasCurrentRoute)
                    {
                        mPaths->append(*currentRoute);
                        mPaths->mRouteNow=mPaths->size()-1;
                    }
                }
            } else if (bHasCurrentRoute && (e->buttons() & Qt::RightButton)) {
  //              qDebug() << "Shift right";
                if (routeFound) {
  //                  qDebug() << "Route found";
                    currentRoute->removeAt(mRoutePointSelected );
                } else {
  //                  qDebug() << "Route not found";
                    removeLastRoutePoint();
                }
            }
        }
        update();
    } else if (ctrl_shift) {
        if (e->buttons() & Qt::LeftButton) {
            QPoint p = getMousePosRelative();
            double iLlh[3], llh[3], xyz[3];
            iLlh[0] = mRefLat;
            iLlh[1] = mRefLon;
            iLlh[2] = mRefHeight;
            xyz[0] = p.x() / 1000.0;
            xyz[1] = p.y() / 1000.0;
            xyz[2] = 0.0;
            utility::enuToLlh(iLlh, xyz, llh);
            mRefLat = llh[0];
            mRefLon = llh[1];
            mRefHeight = 0.0;
        }
        update();
    }
/*
    qDebug() << "Size (mPath): " << mPaths->size();
    qDebug() << "Current item (mPath): " << mPaths->mRouteNow;
    qDebug() << "Size (currentRoute): " << currentRoute->size();
    qDebug() << "Current item (currentRoute): " << mRoutePointSelected;*/
}

void MapWidget::mousePressEvent(QMouseEvent *e)
{
    setFocus();

    if (mousePressEventHandler) {
        mousePressEventHandler(e);
    }
}

void MapWidget::mouseReleaseEvent(QMouseEvent *e)
{
    setFocus();

    if (mouseReleaseEventHandler) {
        mouseReleaseEventHandler(e);
    }
}

void MapWidget::wheelEvent(QWheelEvent *e)
{
    setFocus();

    if (wheelEventHandler) {
        wheelEventHandler(e);
    }
}

void MapWidget::keyPressEvent(QKeyEvent *event)
{
    // Handle Delete key to remove the currently selected path
    if (event->key() == Qt::Key_Delete && hasFocus()) {
        qDebug() << "Delete key pressed in MapWidget";
        qDebug() << "bAnalysisActive:" << bAnalysisActive;
        qDebug() << "mPaths valid:" << (mPaths != nullptr);
        if (mPaths) {
            qDebug() << "mPaths size:" << mPaths->size();
            qDebug() << "Current path index:" << mPaths->mRouteNow;
        }
        
        if (bAnalysisActive && mPaths && mPaths->size() > 0) {
            int currentPathIndex = mPaths->mRouteNow;
            
            qDebug() << "Removing path at index:" << currentPathIndex;
            
            // Remove the current path
            mPaths->removeAt(currentPathIndex);
            
            // Adjust the current path index
            if (mPaths->size() > 0) {
                // If we removed the last path, select the new last one
                if (currentPathIndex >= mPaths->size()) {
                    mPaths->mRouteNow = mPaths->size() - 1;
                }
                // Otherwise, keep the same index (now pointing to the next path)
                qDebug() << "New current path index:" << mPaths->mRouteNow;
            } else {
                // No more paths
                mPaths->mRouteNow = -1;
                qDebug() << "No more paths left";
            }
            
            update();
            qDebug() << "Emitting pathsUpdated signal to refresh analysis";
            emit pathsUpdated();
            event->accept();
            return;
        }
    }
    
    // Call base class implementation for other keys
    QWidget::keyPressEvent(event);
}

void MapWidget::mouseReleaseEventFields(QMouseEvent *e)
{
    bool ctrl = e->modifiers() == Qt::ControlModifier;
    bool shift = e->modifiers() == Qt::ShiftModifier;
    bool ctrl_shift = e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);

    QPoint mousePosWidget = e->pos();
    LocPoint mousePosMap;
    QPoint p = getMousePosRelativeNew(e->pos());
    mousePosMap.setXY(p.x() / 1000.0, p.y() / 1000.0);

    for (MapModule *m: mMapModules) {
        if (m->processMouse(false, true, false, false,
                            mousePosWidget, mousePosMap, 0.0,
                            ctrl, shift, ctrl_shift,
                            e->buttons() & Qt::LeftButton,
                            e->buttons() & Qt::RightButton,
                            mScaleFactor)) {
            return;
        }
    }

    if (!(e->buttons() & Qt::LeftButton)) {
        mMouseLastX = 1000000;
        mMouseLastY = 1000000;
        mRoutePointSelected = -1;
        mAnchorSelected = -1;
    }
}

void MapWidget::mouseReleaseEventPaths(QMouseEvent *e)
{
    bool ctrl = e->modifiers() == Qt::ControlModifier;
    bool shift = e->modifiers() == Qt::ShiftModifier;
    bool ctrl_shift = e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);

    QPoint mousePosWidget = e->pos();
    LocPoint mousePosMap;
    QPoint p = getMousePosRelativeNew(e->pos());
    mousePosMap.setXY(p.x() / 1000.0, p.y() / 1000.0);

    for (MapModule *m: mMapModules) {
        if (m->processMouse(false, true, false, false,
                            mousePosWidget, mousePosMap, 0.0,
                            ctrl, shift, ctrl_shift,
                            e->buttons() & Qt::LeftButton,
                            e->buttons() & Qt::RightButton,
                            mScaleFactor)) {
            return;
        }
    }

    if (!(e->buttons() & Qt::LeftButton)) {
        mMouseLastX = 1000000;
        mMouseLastY = 1000000;
        mRoutePointSelected = -1;
        mAnchorSelected = -1;
    }
}

void MapWidget::wheelEventPaths(QWheelEvent *e)
{
    bool ctrl = e->modifiers() == Qt::ControlModifier;
    bool shift = e->modifiers() == Qt::ShiftModifier;
    bool ctrl_shift = e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);

    QPoint mousePosWidget = this->mapFromGlobal(QCursor::pos());
    LocPoint mousePosMap;
    QPoint p = getMousePosRelative();
    mousePosMap.setXY(p.x() / 1000.0, p.y() / 1000.0);

    for (MapModule *m: mMapModules) {
        if (m->processMouse(false, false, false, true,
                            mousePosWidget, mousePosMap, e->angleDelta().y(),
                            ctrl, shift, ctrl_shift,
                            e->buttons() & Qt::LeftButton,
                            e->buttons() & Qt::RightButton,
                            mScaleFactor)) {
            return;
        }
    }

    if (ctrl && mSelectedCar >= 0) {
        for (int i = 0;i < mCarInfo.size();i++) {
            CarInfo &carInfo = mCarInfo[i];
            if (carInfo.getId() == mSelectedCar) {
                LocPoint pos = carInfo.getLocation();
                //                double angle = pos.getYaw() + (double)e->angleDelta().y() * 0.0005;
                double angle = pos.getYaw() + (double)e->angleDelta().y() * 0.05;
                normalizeAngleRad(angle);
                pos.setYaw(angle);
                carInfo.setLocation(pos);
                emit posSet(mSelectedCar, pos);
                update();
            }
        }
    } else {
        double scaleDiff = ((double)e->angleDelta().y() / 600.0);
        if (scaleDiff > 0.8)
        {
            scaleDiff = 0.8;
        }

        if (scaleDiff < -0.8)
        {
            scaleDiff = -0.8;
        }

        mScaleFactor += mScaleFactor * scaleDiff;
        mXOffset += mXOffset * scaleDiff;
        mYOffset += mYOffset * scaleDiff;

        emit scaleChanged(mScaleFactor);
        emit offsetChanged(mXOffset, mYOffset);
        update();
    }

    updateClosestInfoPoint();
}

void MapWidget::wheelEventFields(QWheelEvent *e)
{
    bool ctrl = e->modifiers() == Qt::ControlModifier;
    bool shift = e->modifiers() == Qt::ShiftModifier;
    bool ctrl_shift = e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier);

    QPoint mousePosWidget = this->mapFromGlobal(QCursor::pos());
    LocPoint mousePosMap;
    QPoint p = getMousePosRelative();
    mousePosMap.setXY(p.x() / 1000.0, p.y() / 1000.0);

    for (MapModule *m: mMapModules) {
        if (m->processMouse(false, false, false, true,
                            mousePosWidget, mousePosMap, e->angleDelta().y(),
                            ctrl, shift, ctrl_shift,
                            e->buttons() & Qt::LeftButton,
                            e->buttons() & Qt::RightButton,
                            mScaleFactor)) {
            return;
        }
    }

    if (ctrl && mSelectedCar >= 0) {} else {
        double scaleDiff = ((double)e->angleDelta().y() / 600.0);
        if (scaleDiff > 0.8)
        {
            scaleDiff = 0.8;
        }

        if (scaleDiff < -0.8)
        {
            scaleDiff = -0.8;
        }

        mScaleFactor += mScaleFactor * scaleDiff;
        mXOffset += mXOffset * scaleDiff;
        mYOffset += mYOffset * scaleDiff;

        emit scaleChanged(mScaleFactor);
        emit offsetChanged(mXOffset, mYOffset);
        update();
    }

    updateClosestInfoPoint();
}

bool MapWidget::event(QEvent *event)
{
    if (event->type() == QEvent::Gesture) {
        QGestureEvent *ge = static_cast<QGestureEvent*>(event);

        if (QGesture *pinch = ge->gesture(Qt::PinchGesture)) {
            QPinchGesture *pg = static_cast<QPinchGesture *>(pinch);

            if (pg->changeFlags() & QPinchGesture::ScaleFactorChanged) {
                mScaleFactor *= pg->scaleFactor();
                mXOffset *= pg->scaleFactor();
                mYOffset *= pg->scaleFactor();
                update();
            }

            return true;
        }
    } else if (event->type() == QEvent::KeyPress) {
        // Generate scroll events from up and down arrow keys
        QKeyEvent *ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Up) {
            /*
            QWheelEvent we(QPointF(0, 0),
                           QPointF(0, 0),
                           QPoint(0, 0),
                           QPoint(0, 120),
                           0, Qt::Vertical, 0,
                           ke->modifiers());
            */
            // New code for Qt 6
            QWheelEvent we(
                QPointF(0, 0),          // Position
                QPointF(0, 0),          // Global position
                QPoint(0, 0),           // Pixel delta
                QPoint(0, 120),         // Angle delta
                Qt::NoButton,           // Buttons
                ke->modifiers(),        // Keyboard modifiers
                Qt::NoScrollPhase,      // Scroll phase
                false,                  // Inverted
                Qt::MouseEventNotSynthesized // Source
                );
            wheelEvent(&we);
            return true;
        } else if (ke->key() == Qt::Key_Down) {
            /*
            QWheelEvent we(QPointF(0, 0),
                           QPointF(0, 0),
                           QPoint(0, 0),
                           QPoint(0, -120),
                           0, Qt::Vertical, 0,
                           ke->modifiers());
            */
            QWheelEvent we(
                QPointF(0, 0),          // Position
                QPointF(0, 0),          // Global position
                QPoint(0, 0),           // Pixel delta
                QPoint(0,-120),         // Angle delta
                Qt::NoButton,           // Buttons
                ke->modifiers(),        // Keyboard modifiers
                Qt::NoScrollPhase,      // Scroll phase
                false,                  // Inverted
                Qt::MouseEventNotSynthesized // Source
                );
            wheelEvent(&we);
            return true;
        }
    }

    return QWidget::event(event);
}

quint32 MapWidget::getRoutePointAttributes() const
{
    return mRoutePointAttributes;
}

void MapWidget::setRoutePointAttributes(const quint32 &routePointAttributes)
{
    qDebug() << "setRoutePointAttributes()";
    mRoutePointAttributes = routePointAttributes;
    
    LocPoint* currentPoint = getCurrentPoint();
    if (currentPoint) {
        qDebug() << "OK";
        qDebug() << "Attributes: " << routePointAttributes;
        currentPoint->setAttributes(routePointAttributes);
        
        MapRoute& currentRoute = mPaths->getCurrent();
        qDebug() << "Route size: " << currentRoute.size();
        for (int i = 0; i < currentRoute.size(); i++) {
            qDebug() << "Point No: " << i << ", attributes: " << currentRoute.at(i).getAttributes();
        }
    } else {
        qDebug() << "Not OK";
        qWarning() << "setRoutePointAttributes: No current point selected or invalid point index";
    }
}

void MapWidget::addMapModule(MapModule *m)
{
    mMapModules.append(m);
}

void MapWidget::removeMapModule(MapModule *m)
{
    for (int i = 0;i < mMapModules.size();i++) {
        if (mMapModules.at(i) == m) {
            mMapModules.remove(i);
            break;
        }
    }
}

void MapWidget::removeMapModuleLast()
{
    if (!mMapModules.isEmpty()) {
        mMapModules.removeLast();
    }
}

bool MapWidget::getDrawRouteText() const
{
    return mDrawRouteText;
}

void MapWidget::setDrawRouteText(bool drawRouteText)
{
    mDrawRouteText = drawRouteText;
    update();
}

bool MapWidget::getDrawUwbTrace() const
{
    return mDrawUwbTrace;
}

void MapWidget::setDrawUwbTrace(bool drawUwbTrace)
{
    mDrawUwbTrace = drawUwbTrace;
    update();
}

double MapWidget::getTraceMinSpaceGps() const
{
    return mTraceMinSpaceGps;
}

void MapWidget::setTraceMinSpaceGps(double traceMinSpaceGps)
{
    mTraceMinSpaceGps = traceMinSpaceGps;
}

double MapWidget::getTraceMinSpaceCar() const
{
    return mTraceMinSpaceCar;
}

void MapWidget::setTraceMinSpaceCar(double traceMinSpaceCar)
{
    mTraceMinSpaceCar = traceMinSpaceCar;
}

qint32 MapWidget::getRoutePointTime() const
{
    return mRoutePointTime;
}

void MapWidget::setRoutePointTime(const qint32 &routePointTime)
{
    qDebug() << "setRoutePointTime()";
    mRoutePointTime = routePointTime;
    
    LocPoint* currentPoint = getCurrentPoint();
    if (currentPoint) {
        qDebug() << "OK";
        qDebug() << "Time: " << routePointTime;
        currentPoint->setTime(routePointTime);
        
        MapRoute& currentRoute = mPaths->getCurrent();
        qDebug() << "Route size: " << currentRoute.size();
        for (int i = 0; i < currentRoute.size(); i++) {
            qDebug() << "Point No: " << i << ", time: " << currentRoute.at(i).getTime();
        }
    } else {
        qDebug() << "Not OK";
        qWarning() << "setRoutePointTime: No current point selected or invalid point index";
    }
}

int MapWidget::getInfoTraceNow() const
{
    return mInfoTraceNow;
}

void MapWidget::setInfoTraceNow(int infoTraceNow)
{
    int infoTraceOld = mInfoTraceNow;
    mInfoTraceNow = infoTraceNow;

    while (mInfoTraces.size() < (mInfoTraceNow + 1)) {
        QList<LocPoint> l;
        mInfoTraces.append(l);
    }
    update();

    if (infoTraceOld != mInfoTraceNow) {
        emit infoTraceChanged(mInfoTraceNow);
    }
}

bool MapWidget::getDrawOsmStats() const
{
    return mDrawOsmStats;
}

void MapWidget::setDrawOsmStats(bool drawOsmStats)
{
    mDrawOsmStats = drawOsmStats;
    update();
}

bool MapWidget::getDrawGrid() const
{
    return mDrawGrid;
}

int MapWidget::getRoutePointSelected() const
{
    return mRoutePointSelected;
}

QList<ControlState> MapWidget::getRoutePointControlStates() const
{
    return mRoutePointControlStates;
}

void MapWidget::setRoutePointControlStates(const QList<ControlState> &controlStates)
{
    qDebug() << "setRoutePointControlStates()";
    mRoutePointControlStates = controlStates;
    
    LocPoint* currentPoint = getCurrentPoint();
    if (currentPoint) {
        qDebug() << "OK";
        qDebug() << "Control states count: " << controlStates.size();
        currentPoint->setControlStates(controlStates);
        
        MapRoute& currentRoute = mPaths->getCurrent();
        qDebug() << "Route size: " << currentRoute.size();
        for (int i = 0; i < currentRoute.size(); i++) {
            qDebug() << "Point No: " << i << ", control states: " << currentRoute.at(i).getControlStates().size();
        }
    } else {
        qDebug() << "Not OK";
        qWarning() << "setRoutePointControlStates: No current point selected or invalid point index";
    }
}

QList<int> MapWidget::findPointsWithControlState(int controlId, double targetValue)
{
    QList<int> selectedPointIndices;
    qDebug() << "Finding points with control" << controlId << "and target value" << targetValue;

    // Check if we have a valid current path
    if (!mPaths || mPaths->size() == 0) {
        qDebug() << "No paths available";
        return selectedPointIndices;
    }

    MapRoute& currentRoute = mPaths->getCurrent();
    if (currentRoute.size() == 0) {
        qDebug() << "Current route is empty";
        return selectedPointIndices;
    }

    // Track the current control state for the specified controlId
    double currentControlValue = -1; // Initialize to invalid value
    bool inSelectionRange = false;

    // Iterate through all points in the current route
    for (int i = 0; i < currentRoute.size(); i++) {
        const LocPoint& point = currentRoute[i];
        const QList<ControlState>& controlStates = point.getControlStates();

        // Find the control state with the specified controlId
        bool controlFound = false;
        double foundValue = -1;

        for (const ControlState& state : controlStates) {
            if (state.controlId == controlId) {
                controlFound = true;
                foundValue = state.targetValue;
                break;
            }
        }

        if (controlFound) {
            qDebug() << "Point" << i << "has control" << controlId << "with value" << foundValue;

            if (foundValue == targetValue) {
                // Start or continue selection
                if (!inSelectionRange) {
                    qDebug() << "Starting selection at point" << i;
                    inSelectionRange = true;
                }
                selectedPointIndices.append(i);
            } else {
                // End selection if we were in a selection range
                if (inSelectionRange) {
                    qDebug() << "Ending selection at point" << i << "(value changed to" << foundValue << ")";
                    inSelectionRange = false;
                }
            }
            currentControlValue = foundValue;
        } else {
            // Control not found in this point
            if (inSelectionRange) {
                // If we were in a selection range and the control is no longer present,
                // continue selection (control maintains its last value)
                selectedPointIndices.append(i);
                qDebug() << "Point" << i << "continues selection (control not present, assuming last value)";
            }
        }
    }

    qDebug() << "Found" << selectedPointIndices.size() << "points with the specified control state";
    
    // Store the results for highlighting
    mSelectedControlPoints = selectedPointIndices;
    update(); // Trigger a repaint to show the selected points
    
    return selectedPointIndices;
}

void MapWidget::setDrawGrid(bool drawGrid)
{
    bool drawGridOld = mDrawGrid;
    mDrawGrid = drawGrid;

    if (drawGridOld != mDrawGrid) {
        update();
    }
}

void MapWidget::updateClosestInfoPoint()
{
    QPointF mpq = getMousePosRelative();
    LocPoint mp(mpq.x() / 1000.0, mpq.y() / 1000.0);
    double dist_min = 1e30;
    LocPoint closest;

    for (int i = 0;i < mVisibleInfoTracePoints.size();i++) {
        const LocPoint &ip = mVisibleInfoTracePoints[i];
        if (mp.getDistanceTo(ip) < dist_min) {
            dist_min = mp.getDistanceTo(ip);
            closest = ip;
        }
    }

    bool drawBefore = mClosestInfo.getInfo().size() > 0;
    bool drawNow = false;
    if ((dist_min * mScaleFactor) < 0.02) {
        if (closest != mClosestInfo) {
            mClosestInfo = closest;
            update();
        }

        drawNow = true;
    } else {
        mClosestInfo.setInfo("");
    }

    if (drawBefore && !drawNow) {
        update();
    }
}

int MapWidget::drawInfoPoints(QPainter &painter, const QList<LocPoint> &pts,
                              QTransform drawTrans, QTransform txtTrans,
                              double xStart, double xEnd, double yStart, double yEnd,
                              double min_dist)
{
    int last_visible = 0;
    int drawn = 0;
    QPointF pt_txt;
    QRectF rect_txt;

    painter.setTransform(txtTrans);

    for (int i = 0;i < pts.size();i++) {
        const LocPoint &ip = pts[i];
        QPointF p = ip.getPointMm();
        QPointF p2 = drawTrans.map(p);

        if (isPointWithinRect(p, xStart, xEnd, yStart, yEnd)) {
            if (drawn > 0) {
                double dist_view = pts.at(i).getDistanceTo(pts.at(last_visible)) * mScaleFactor;
                if (dist_view < min_dist) {
                    continue;
                }

                last_visible = i;
            }

            painter.setBrush(ip.getColor());
            painter.setPen(ip.getColor());
            painter.drawEllipse(p2, ip.getRadius(), ip.getRadius());

            drawn++;
            mVisibleInfoTracePoints.append(ip);

            if (mScaleFactor > mInfoTraceTextZoom) {
                pt_txt.setX(p.x() + 5 / mScaleFactor);
                pt_txt.setY(p.y());
                pt_txt = drawTrans.map(pt_txt);
                painter.setPen(Qt::black);
                painter.setFont(QFont("monospace"));
                rect_txt.setCoords(pt_txt.x(), pt_txt.y() - 20,
                                   pt_txt.x() + 500, pt_txt.y() + 500);
                painter.drawText(rect_txt, Qt::AlignTop | Qt::AlignLeft, ip.getInfo());
            }
        }
    }

    return drawn;
}

//int MapWidget::getClosestPoint(LocPoint p, MapRoute route, double &dist)
int MapWidget::getClosestPoint(LocPoint p, QList<LocPoint> route, double &dist)
{
    QList<LocPoint> points=route;
    int closest = -1;
    dist = -1.0;
    for (int i = 0;i < points.size();i++) {
        double d = points[i].getDistanceTo(p);
        if (dist < 0.0 || d < dist) {
            dist = d;
            closest = i;
        }
    }

    return closest;
}

int MapWidget::getOsmZoomLevel() const
{
    return mOsmZoomLevel;
}

int MapWidget::getOsmMaxZoomLevel() const
{
    return mOsmMaxZoomLevel;
}

void MapWidget::setOsmMaxZoomLevel(int osmMaxZoomLevel)
{
    mOsmMaxZoomLevel = osmMaxZoomLevel;
    update();
}

double MapWidget::getInfoTraceTextZoom() const
{
    return mInfoTraceTextZoom;
}

void MapWidget::setInfoTraceTextZoom(double infoTraceTextZoom)
{
    mInfoTraceTextZoom = infoTraceTextZoom;
    update();
}

OsmClient *MapWidget::osmClient()
{
    return mOsm;
}

int MapWidget::getInfoTraceNum()
{
    return mInfoTraces.size();
}

int MapWidget::getInfoPointsInTrace(int trace)
{
    int res = -1;

    if (trace >= 0 && trace < mInfoTraces.size()) {
        res = mInfoTraces.at(trace).size();
    }

    return res;
}

int MapWidget::setNextEmptyOrCreateNewInfoTrace()
{
    int next = -1;

    for (int i = 0;i < mInfoTraces.size();i++) {
        if (mInfoTraces.at(i).isEmpty()) {
            next = i;
            break;
        }
    }

    if (next < 0) {
        next = mInfoTraces.size();
    }

    setInfoTraceNow(next);

    return next;
}

/*
void MapWidget::setAnchorMode(bool anchorMode)
{
    mAnchorMode = anchorMode;
}

bool MapWidget::getAnchorMode()
{
    return mAnchorMode;
}

void MapWidget::setAnchorId(int id)
{
    mAnchorId = id;
}

void MapWidget::setAnchorHeight(double height)
{
    mAnchorHeight = height;
}
*/
void MapWidget::removeLastRoutePoint()
{
    LocPoint pos;
    MapRoute &current=mPaths->getCurrent();
    if (current.size() > 0) {
        pos = current.last();
        current.removeLast();
    }
    emit lastRoutePointRemoved(pos);
    update();
}

void MapWidget::zoomInOnRoute(int id, double margins, double wWidth, double wHeight)
{
    MapRoute route;

    if (id >= 0) {
        route = getPath(id);
    } else {
        for (auto r: mPaths->mCollection) {
            route.append(r);
        }
    }

    if (route.size() > 0) {
        double xMin = 1e12;
        double xMax = -1e12;
        double yMin = 1e12;
        double yMax = -1e12;

        for (auto p: route) {
            if (p.getX() < xMin) {
                xMin = p.getX();
            }
            if (p.getX() > xMax) {
                xMax = p.getX();
            }
            if (p.getY() < yMin) {
                yMin = p.getY();
            }
            if (p.getY() > yMax) {
                yMax = p.getY();
            }
        }

        double width = xMax - xMin;
        double height = yMax - yMin;

        if (wWidth <= 0 || wHeight <= 0) {
            wWidth = this->width();
            wHeight = this->height();
        }

        xMax += width * margins * 0.5;
        xMin -= width * margins * 0.5;
        yMax += height * margins * 0.5;
        yMin -= height * margins * 0.5;

        width = xMax - xMin;
        height = yMax - yMin;

        double scaleX = 1.0 / ((width * 1000) / wWidth);
        double scaleY = 1.0 / ((height * 1000) / wHeight);

        mScaleFactor = qMin(scaleX, scaleY);
        mXOffset = -(xMin + width / 2.0) * mScaleFactor * 1000.0;
        mYOffset = -(yMin + height / 2.0) * mScaleFactor * 1000.0;

        update();
    }
}

double MapWidget::getOsmRes() const
{
    return mOsmRes;
}

void MapWidget::setOsmRes(double osmRes)
{
    mOsmRes = osmRes;
    update();
}

bool MapWidget::getDrawOpenStreetmap() const
{
    return mDrawOpenStreetmap;
}

void MapWidget::setOsmOffset(double east, double north)
{
    mOsmOffsetE = east;
    mOsmOffsetN = north;
    update();
}

/**
 * RobAnt ovanifrån i verklig skala (mm), x framåt, y vänster, origo = framaxelns mitt
 * (positionen autopiloten styr efter). Mått mk1 2026-10-16: längd 1,9 m, bredd 1,35 m,
 * axelavstånd 1,42 m, hjulinfästningar 1,24 m isär, hjul Ø 0,46 m. Kroppen går tvärs
 * mellan sidorna (nödstopp och GPS-antenn på vänster sida), fyra ben ut till hjulen.
 * Bakhjulen styr.
 */
void MapWidget::paintRobAnt(QPainter &painter, const QColor &hull, const QColor &wheels, const QColor &front)
{
    const double axel = 1420.0;			// axelavstånd
    const double hjulY = 565.0;			// hjulmitt i sidled (bredd 1350 - hjulbredd 220)
    const double hjulL = 460.0, hjulB = 220.0;
    const double mittX = -axel / 2.0;
    const double faste = 620.0;			// hjulinfästningarna 1240 isär
    painter.save();
    QPen penna(QColor(40, 40, 40));
    penna.setWidthF(15.0);
    penna.setCosmetic(false);
    painter.setPen(penna);

    // Ben: från kroppen ut till hjulen, på båda sidor
    painter.setBrush(QColor(170, 170, 160));
    for (double sida : {1.0, -1.0}) {
        painter.drawRect(QRectF(mittX - axel / 2.0, sida * (faste - 70.0) - 45.0, axel, 90.0));
    }
    // Hjul
    painter.setBrush(wheels);
    for (double hx : {0.0, -axel}) {
        for (double sida : {1.0, -1.0}) {
            painter.drawRoundedRect(QRectF(hx - hjulL / 2.0, sida * hjulY - hjulB / 2.0, hjulL, hjulB), 60, 60);
        }
    }
    // Ett heltäckande lock mellan hjulen, som på mk2 (syns bättre på kartan än
    // mk1:s ram med två lådor)
    const double lockX = axel - hjulL - 80.0;	// fri längd mellan hjulen, lite marginal
    const double lockY = 2 * (hjulY - hjulB / 2.0) - 60.0;
    painter.setBrush(hull);
    painter.drawRoundedRect(QRectF(mittX - lockX / 2.0, -lockY / 2.0, lockX, lockY), 60, 60);
    // Nödstopp (gult med röd knapp) längst ut på vänster sida
    painter.setBrush(QColor(240, 200, 0));
    painter.drawRect(QRectF(mittX - 110.0, faste - 40.0, 220.0, 120.0));
    painter.setBrush(QColor(200, 0, 0));
    painter.drawEllipse(QPointF(mittX, faste + 20.0), 40.0, 40.0);
    // Fram: grön pil på framaxeln
    painter.setBrush(front);
    QPolygonF pil;
    pil << QPointF(150.0, 0.0) << QPointF(-60.0, 110.0) << QPointF(-60.0, -110.0);
    painter.drawPolygon(pil);
    painter.restore();
}

void MapWidget::setCarRefAtFront(int car, bool front)
{
    if (front == mCarRefAtFront.contains(car)) {
        return;
    }
    if (front) {
        mCarRefAtFront.insert(car);
    } else {
        mCarRefAtFront.remove(car);
    }
    update();
}

void MapWidget::setDrawOpenStreetmap(bool drawOpenStreetmap)
{
    mDrawOpenStreetmap = drawOpenStreetmap;
    update();
}

void MapWidget::setEnuRef(double lat, double lon, double height)
{
    mRefLat = lat;
    mRefLon = lon;
    mRefHeight = height;
    update();
}

void MapWidget::getEnuRef(double *llh)
{
    llh[0] = mRefLat;
    llh[1] = mRefLon;
    llh[2] = mRefHeight;
}

void MapWidget::setTrace(QVector<LocPoint> mTrace)
{
    mCarTrace=mTrace;
    setLogStart(0);
    setLogEnd(99);
    bUpdateable=false;
}

void MapWidget::setLogStart(int iStart)
{
    iLogstart=(int)(iStart*mCarTrace.size()/99);
    qDebug() << "istart: " << iStart;
    update();
};

void MapWidget::setLogEnd(int iEnd)
{
    iLogend=(int)(mCarTrace.size()*iEnd/99);
    qDebug() << "iend: " << iEnd;
    update();
};

int MapWidget::Elements()
{
    return iLogend-iLogstart+1;
}

int MapWidget::firstElement()
{
    return iLogstart;
}

int MapWidget::lastElement()
{
    return iLogend-1;
}


void MapWidget::paint(QPainter &painter, int width, int height, bool highQuality)
{
    //    qDebug() << "Start painting";
    if (highQuality) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    } else {
        painter.setRenderHint(QPainter::Antialiasing, mAntialiasDrawings);
        painter.setRenderHint(QPainter::TextAntialiasing, mAntialiasDrawings);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, mAntialiasDrawings);
    }

    const double scaleMax = 20;
    const double scaleMin = 0.000001;

    // Make sure scale and offsetappend is reasonable
    if (mScaleFactor < scaleMin) {
        double scaleDiff = scaleMin / mScaleFactor;
        mScaleFactor = scaleMin;
        mXOffset *= scaleDiff;
        mYOffset *= scaleDiff;
    } else if (mScaleFactor > scaleMax) {
        double scaleDiff = scaleMax / mScaleFactor;
        mScaleFactor = scaleMax;
        mXOffset *= scaleDiff;
        mYOffset *= scaleDiff;
    }

    // Optionally follow a car
    if (mFollowCar >= 0) {
        for (int i = 0;i < mCarInfo.size();i++) {
            CarInfo &carInfo = mCarInfo[i];
            if (carInfo.getId() == mFollowCar) {
                LocPoint followLoc = carInfo.getLocation();
                mXOffset = -followLoc.getX() * 1000.0 * mScaleFactor;
                mYOffset = -followLoc.getY() * 1000.0 * mScaleFactor;
            }
        }
    }

    // Limit the offset to avoid overflow at 2^31 mm
    double lim = 2000000000.0 * mScaleFactor;
    if (mXOffset > lim) {
        mXOffset = lim;
    } else if (mXOffset < -lim) {
        mXOffset = -lim;
    }

    if (mYOffset > lim) {
        mYOffset = lim;
    } else if (mYOffset < -lim) {
        mYOffset = -lim;
    }

    // Paint begins here
    painter.fillRect(0, 0, width, height, QBrush(Qt::transparent));

    // Map coordinate transforms
    QTransform drawTrans;
    drawTrans.translate(width / 2 + mXOffset, height / 2 - mYOffset);
    drawTrans.scale(mScaleFactor, -mScaleFactor);
    drawTrans.rotate(mRotation);

    // Text coordinates
    QTransform txtTrans;
    txtTrans.translate(0, 0);
    txtTrans.scale(1, 1);
    txtTrans.rotate(0);

    // Set font
    QFont font = this->font();
    font.setPointSize(10);
    font.setFamily("Monospace");
    painter.setFont(font);

    // Grid parameters
    double stepGrid = 20.0 * ceil(1.0 / ((mScaleFactor * 10.0) / 50.0));
    bool gridDiv = false;
    if (round(log10(stepGrid)) > log10(stepGrid)) {
        gridDiv = true;
    }
    stepGrid = pow(10.0, round(log10(stepGrid)));
    if (gridDiv) {
        stepGrid /= 2.0;
    }

    // Draw perspective pixmaps first
    painter.setTransform(drawTrans);
    for(int i = 0;i < mPerspectivePixmaps.size();i++) {
        mPerspectivePixmaps[i].drawUsingPainter(painter);
    }

    // View center, width and height in m
    const double cx = -mXOffset / mScaleFactor / 1000.0;
    const double cy = -mYOffset / mScaleFactor / 1000.0;
    const double view_w = width / mScaleFactor / 1000.0;
    const double view_h = height / mScaleFactor / 1000.0;

    // Draw openstreetmap tiles
    if (mDrawOpenStreetmap) {
        paintTiles(painter,cx, cy, view_w, view_h, highQuality);
    }

    const double zeroAxisWidth = 3;
    const QColor zeroAxisColor = Qt::red;
    const QColor firstAxisColor = Qt::gray;
    const QColor secondAxisColor = Qt::blue;
    QPalette palette; // Get the application palette or use a specific widget's palette
    const QColor textColor = palette.color(QPalette::WindowText);

    QString txt;
    QPointF pt_txt;
    QRectF rect_txt;
    QPen pen;

    if (mDrawGrid) {
        paintGrid(painter,pen, drawTrans, txtTrans, width, height, stepGrid, txt, pt_txt, textColor, zeroAxisColor, firstAxisColor,secondAxisColor,zeroAxisWidth);
    }

    // Draw routes
    bool isSelected;//
    // Draw fields
    for (int fn = 0;fn < mFields->size();fn++) {
        isSelected= (mFields->mRouteNow == fn) && (focusBorder==true);
        MapRoute &fieldNow = mFields->at(fn);
        fieldNow.paintBorder(painter, pen, isSelected, mScaleFactor, drawTrans);
    }

    for (int rn = 0;rn < mPaths->size();rn++) {
        MapRoute &routeNow = mPaths->at(rn);
        isSelected= (mPaths->mRouteNow == rn);
        // qDebug() << "Calling paintPath with selectedActionAttribute:" << mSelectedActionAttribute << "on widget:" << this << "paths:" << mPaths->size();
        routeNow.paintPath(painter, pen, isSelected, mScaleFactor, drawTrans, txt, pt_txt, rect_txt, txtTrans,  mDrawRouteText, highQuality, mSelectedActionAttribute, mSelectedControlPoints);
    }

    //paintInfoTraces();
    mVisibleInfoTracePoints.clear();


    // Draw info trace
    int info_segments = 0;
    int info_points = 0;

    for (int in = 0;in < mInfoTraces.size();in++) {
        paintTrace(mInfoTraces[in],painter,pen,mInfoTraceNow == in,drawTrans, txtTrans,cx, cy, view_w, view_h,info_segments, info_points);
    }

    paintClosestPoint(painter,pen,drawTrans, txtTrans, pt_txt,rect_txt );

    //    qDebug() << "Middle painting";

    paintCarTraces(painter,pen,drawTrans);

    // Map module painting
    painter.save();
    for (MapModule *m: mMapModules) {
        m->processPaint(painter, width, height, highQuality,
                        drawTrans, txtTrans, mScaleFactor);
    }
    painter.restore();

    // Draw cars
    painter.setPen(QPen(textColor));
    for(int i = 0;i < mCarInfo.size();i++) {
        paintCar(mCarInfo[i],painter,pen, drawTrans, txtTrans,txt, pt_txt,textColor,rect_txt);
    }

    double start_txt = 30.0;

    const double txtOffset = 145.0;
    const double txt_row_h = 20.0;

    paintUnitZoomGeneralinfo(painter,font, txtTrans, width, stepGrid,txt,textColor,start_txt,txtOffset,txt_row_h, info_segments, info_points);


/*    // Current route info
    if (mPaths->size()>0)
    {
        qDebug() << "E";
        mPaths->getCurrent().routeinfo(painter,start_txt,txtOffset,txt_row_h,width,txt);
    };
    */
    painter.end();
    //    qDebug() << "End painting";
}

void MapWidget::paintClosestPoint(QPainter &painter,QPen &pen,QTransform drawTrans, QTransform txtTrans, QPointF& pt_txt,QRectF &rect_txt )
{
    // Draw point closest to mouse pointer
    if (mClosestInfo.getInfo().size() > 0) {
        QPointF p = mClosestInfo.getPointMm();
        QPointF p2 = drawTrans.map(p);

        painter.setTransform(txtTrans);
        pen.setColor(Qt::green);
        painter.setBrush(Qt::green);
        painter.setPen(Qt::green);
        painter.drawEllipse(p2, mClosestInfo.getRadius(), mClosestInfo.getRadius());

        pt_txt.setX(p.x() + 5 / mScaleFactor);
        pt_txt.setY(p.y());
        painter.setTransform(txtTrans);
        pt_txt = drawTrans.map(pt_txt);
        pen.setColor(Qt::black);
        painter.setPen(pen);
        rect_txt.setCoords(pt_txt.x(), pt_txt.y() - 20,
                           pt_txt.x() + 500, pt_txt.y() + 500);
        painter.drawText(rect_txt, Qt::AlignTop | Qt::AlignLeft, mClosestInfo.getInfo());
    }

}

void MapWidget::paintUnitZoomGeneralinfo(QPainter &painter,QFont font, QTransform txtTrans, int width, double stepGrid,QString& txt, const QColor textColor,   double& start_txt,const double txtOffset,const double txt_row_h,    int &info_segments, int &info_points)
{
    painter.setTransform(txtTrans);
    font.setPointSize(10);
    painter.setFont(font);
    painter.setPen(QPen(textColor));

    // Draw units (m)
    if (mDrawGrid) {
        double res = stepGrid / 1000.0;
        if (res >= 1000.0) {
            txt.asprintf("Grid res: %.0f km", res / 1000.0);
        } else if (res >= 1.0) {
            txt.asprintf("Grid res: %.0f m", res);
        } else {
            txt.asprintf("Grid res: %.0f cm", res * 100.0);
        }
        painter.drawText(width - txtOffset, start_txt, txt);
        start_txt += txt_row_h;
    }

    // Draw zoom level
    txt.asprintf("Zoom: %.7f", mScaleFactor);
    painter.drawText(width - txtOffset, start_txt, txt);
    start_txt += txt_row_h;

    // Draw OSM zoom level
    if (mDrawOpenStreetmap) {
        txt.asprintf("OSM zoom: %d", mOsmZoomLevel);
        painter.drawText(width - txtOffset, start_txt, txt);
        start_txt += txt_row_h;

        if (mDrawOsmStats) {
            txt=QString("DL Tiles: %1").arg(mOsm->getTilesDownloaded());
            painter.drawText(width - txtOffset, start_txt, txt);
            start_txt += txt_row_h;

            txt=QString("HDD Tiles: %1").arg(mOsm->getHddTilesLoaded());
            painter.drawText(width - txtOffset, start_txt, txt);
            start_txt += txt_row_h;

            txt=QString("RAM Tiles: %1").arg(mOsm->getRamTilesLoaded());
            painter.drawText(width - txtOffset, start_txt, txt);
            start_txt += txt_row_h;
        }
    }

    if (mInteractionMode != InteractionModeDefault) {
        txt=QString("IMode: %1").arg(mInteractionMode);
        painter.drawText(width - txtOffset, start_txt, txt);
        start_txt += txt_row_h;
    }

    // Some info
    if (info_segments > 0) {
        txt=QString("Info seg: %1").arg(info_segments);
        painter.drawText(width - txtOffset, start_txt, txt);
        start_txt += txt_row_h;
    }

    if (info_points > 0) {
        txt=QString("Info pts: %1").arg(info_points);
        painter.drawText(width - txtOffset, start_txt, txt);
        start_txt += txt_row_h;
    }

}

void MapWidget::paintTraceVechicle(QPainter &painter,QPen &pen, QTransform drawTrans,QVector<LocPoint> mTrace,double traceWidth,QColor traceColour)
{
    // Draw trace for the selected car
    pen.setWidthF(traceWidth);
    pen.setColor(traceColour);
    painter.setPen(pen);
    painter.setTransform(drawTrans);
    if (bUpdateable)
    {
        for (int i = 1;i < mTrace.size();i++) {
            painter.drawLine(mTrace[i - 1].getX() * 1000.0, mTrace[i - 1].getY() * 1000.0,
                             mTrace[i].getX() * 1000.0, mTrace[i].getY() * 1000.0);
        }
    } else
    {
        if(mTrace.size()>0)
        {
            for (int i = iLogstart+1;i < iLogend;i++) {
                painter.drawLine(mTrace[i - 1].getX() * 1000.0, mTrace[i - 1].getY() * 1000.0,
                                 mTrace[i].getX() * 1000.0, mTrace[i].getY() * 1000.0);
            }
        }
    };
}

void MapWidget::paintCarTraces(QPainter &painter,QPen &pen, QTransform drawTrans)
{
    paintTraceVechicle(painter,pen, drawTrans,mCarTrace,5.0 / mScaleFactor,Qt::red);
    paintTraceVechicle(painter,pen, drawTrans,mCarTraceGps,2.5 / mScaleFactor,Qt::magenta);

    // Draw UWB trace for the selected car
    if (mDrawUwbTrace) {
        paintTraceVechicle(painter,pen, drawTrans,mCarTraceUwb ,2.5 / mScaleFactor,Qt::green);
    }
}

void MapWidget::paintTrace(QList<LocPoint> &itNow,QPainter &painter,QPen &pen, bool isActive, QTransform drawTrans, QTransform txtTrans,const double cx, const double cy, const double view_w, const double view_h,int& info_segments, int& info_points)
{
    // View boundries in mm
    const double xStart2 = (cx - view_w / 2.0) * 1000.0;
    const double yStart2 = (cy - view_h / 2.0) * 1000.0;
    const double xEnd2 = (cx + view_w / 2.0) * 1000.0;
    const double yEnd2 = (cy + view_h / 2.0) * 1000.0;

    if (isActive) {
        pen.setColor(Qt::darkGreen);
        painter.setBrush(Qt::green);
    } else {
        pen.setColor(Qt::darkGreen);
        painter.setBrush(Qt::green);
    }

    pen.setWidthF(3.0);
    painter.setPen(pen);
    painter.setTransform(txtTrans);

    const double info_min_dist = 0.02;

    int last_visible = 0;
    for (int i = 1;i < itNow.size();i++) {
        double dist_view = itNow.at(i).getDistanceTo(itNow.at(last_visible)) * mScaleFactor;
        if (dist_view < info_min_dist) {
            continue;
        }

        bool draw = isPointWithinRect(itNow[last_visible].getPointMm(), xStart2, xEnd2, yStart2, yEnd2);

        if (!draw) {
            draw = isPointWithinRect(itNow[i].getPointMm(), xStart2, xEnd2, yStart2, yEnd2);
        }

        if (!draw) {
            draw = isLineSegmentWithinRect(itNow[last_visible].getPointMm(),
                                           itNow[i].getPointMm(),
                                           xStart2, xEnd2, yStart2, yEnd2);
        }

        if (draw && itNow[i].getDrawLine()) {
            QPointF p1 = drawTrans.map(itNow[last_visible].getPointMm());
            QPointF p2 = drawTrans.map(itNow[i].getPointMm());

            painter.drawLine(p1, p2);
            info_segments++;
        }

        last_visible = i;
    }

    QList<LocPoint> pts_green;
    QList<LocPoint> pts_red;
    QList<LocPoint> pts_other;

    for (int i = 0;i < itNow.size();i++) {
        LocPoint ip = itNow[i];

        if (!isActive) {
            ip.setColor(Qt::gray);
        }

        if (ip.getColor() == Qt::darkGreen || ip.getColor() == Qt::green) {
            pts_green.append(ip);
        } else if (ip.getColor() == Qt::darkRed || ip.getColor() == Qt::red) {
            pts_red.append(ip);
        } else {
            pts_other.append(ip);
        }
    }

    info_points += drawInfoPoints(painter, pts_green, drawTrans, txtTrans,
                                  xStart2, xEnd2, yStart2, yEnd2, info_min_dist);
    info_points += drawInfoPoints(painter, pts_other, drawTrans, txtTrans,
                                  xStart2, xEnd2, yStart2, yEnd2, info_min_dist);
    info_points += drawInfoPoints(painter, pts_red, drawTrans, txtTrans,
                                  xStart2, xEnd2, yStart2, yEnd2, info_min_dist);

}

void MapWidget::paintAnchor(LocPoint &anchor,QPainter &painter,QPen &pen, QTransform drawTrans, QTransform txtTrans,QString& txt, QPointF& pt_txt,const QColor &textColor,QRectF &rect_txt)
{
    double x = anchor.getX() * 1000.0;
    double y = anchor.getY() * 1000.0;
    painter.setTransform(drawTrans);

    // Draw anchor
    painter.setBrush(QBrush(Qt::red));
    painter.translate(x, y);

    pt_txt.setX(x);
    pt_txt.setY(y);
    painter.setTransform(txtTrans);
    pt_txt = drawTrans.map(pt_txt);

    painter.drawRoundedRect(pt_txt.x() - 10,
                            pt_txt.y() - 10,
                            20, 20, 10, 10);

    // Print data
    txt=QString("Anchor %d\n"
                "Pos    : (%.3f, %.3f)\n"
                "Height : %.2f m").
                arg(anchor.getId(),3,'f').
                arg(anchor.getX(), anchor.getY(),3,'f').
                arg(anchor.getHeight(),2,'f');
    pt_txt.setX(x);
    pt_txt.setY(y);
    painter.setTransform(txtTrans);
    pt_txt = drawTrans.map(pt_txt);
    rect_txt.setCoords(pt_txt.x() + 15, pt_txt.y() - 20,
                       pt_txt.x() + 500, pt_txt.y() + 500);
    painter.drawText(rect_txt, txt);
}

void MapWidget::paintCar(CarInfo &carInfo,QPainter &painter,QPen &pen, QTransform drawTrans, QTransform txtTrans,QString &txt, QPointF &pt_txt,const QColor &textColor,QRectF &rect_txt)
{
    LocPoint pos = carInfo.getLocation();
    LocPoint pos_gps = carInfo.getLocationGps();

    const double car_len = carInfo.getLength() * 1000.0 * 5;
    const double car_w = carInfo.getWidth() * 1000.0 * 5;
    const double car_corner = carInfo.getCornerRadius() * 1000.0 * 5;

    double x = pos.getX() * 1000.0;
    double y = pos.getY() * 1000.0;
    double x_gps = pos_gps.getX() * 1000.0;
    double y_gps = pos_gps.getY() * 1000.0;
    double angle = pos.getYaw() * 180.0 / M_PI;
    painter.setTransform(drawTrans);

    QColor col_sigma = Qt::red;
    QColor col_wheels = Qt::black;
    QColor col_bumper = Qt::green;
    QColor col_hull = carInfo.getColor();
    QColor col_center = Qt::blue;
    QColor col_gps = Qt::magenta;
    QColor col_ap = carInfo.getColor();

    if (carInfo.getId() != mSelectedCar) {
        col_wheels = Qt::darkGray;
        col_bumper = Qt::lightGray;
        col_ap = Qt::lightGray;
    }

    // Draw standard deviation
    if (pos.getSigma() > 0.0) {
        QColor col = col_sigma;
        col.setAlphaF(0.2);
        painter.setBrush(QBrush(col));
        painter.drawEllipse(pos.getPointMm(), pos.getSigma() * 1000.0, pos.getSigma() * 1000.0);
    }

    // Draw car
    painter.setBrush(QBrush(col_wheels));
    painter.save();
    painter.translate(x, y);
    painter.rotate(-angle);
    if (mCarRefAtFront.contains(carInfo.getId())) {
        paintRobAnt(painter, col_hull, col_wheels, col_bumper);
    } else {
    // Wheels
    painter.drawRoundedRect(-car_len / 12.0,-(car_w / 2), car_len / 6.0, car_w, car_corner / 3, car_corner / 3);
    painter.drawRoundedRect(car_len - car_len / 2.5,-(car_w / 2), car_len / 6.0, car_w, car_corner / 3, car_corner / 3);
    // Front bumper
    painter.setBrush(col_bumper);
    painter.drawRoundedRect(-car_len / 6.0, -((car_w - car_len / 20.0) / 2.0), car_len, car_w - car_len / 20.0, car_corner, car_corner);
    // Hull
    painter.setBrush(col_hull);
    painter.drawRoundedRect(-car_len / 6.0, -((car_w - car_len / 20.0) / 2.0), car_len - (car_len / 20.0), car_w - car_len / 20.0, car_corner, car_corner);
    }
    painter.restore();

    // RobAnt ritas i verklig skala: prickarna i verklig storlek också (annars ~1 m)
    const bool robant = mCarRefAtFront.contains(carInfo.getId());
    const double r_center = robant ? 40.0 : car_w / 15.0;
    const double r_gps = robant ? 90.0 : car_w / 3.0;

    // Center
    painter.setBrush(col_center);
    painter.drawEllipse(QPointF(x, y), r_center, r_center);

    // GPS Location
    painter.setBrush(col_gps);
    //        painter.drawEllipse(QPointF(x_gps, y_gps), car_w / 15.0, car_w / 15.0);
    painter.drawEllipse(QPointF(x_gps, y_gps), r_gps, r_gps);

    // Autopilot state
    LocPoint ap_goal = carInfo.getApGoal();
    if (ap_goal.getRadius() > 0.0) {
        pen.setColor(col_ap);
        painter.setPen(pen);
        QPointF pm = pos.getPointMm();
        painter.setBrush(Qt::transparent);
        painter.drawEllipse(pm, ap_goal.getRadius() * 1000.0, ap_goal.getRadius() * 1000.0);

        QPointF p = ap_goal.getPointMm();
        pen.setWidthF(3.0 / mScaleFactor);
        painter.setBrush(col_gps);
        painter.drawEllipse(p, 10 / mScaleFactor, 10 / mScaleFactor);
    }

    painter.setPen(QPen(textColor));

    // Print data
    QTime t = QTime::fromMSecsSinceStartOfDay(carInfo.getTime());
    QString solStr;
    if (!carInfo.getLocationGps().getInfo().isEmpty()) {
        solStr = QString("Sol: %1\n").arg(carInfo.getLocationGps().getInfo());
    }
    txt=QString("%1\n"
                "%2"
                "(%3, %4, %5)\n"
                "%6:%7:%8:%9").
                arg(carInfo.getName().toLocal8Bit().data()).
                arg(solStr.toLocal8Bit().data()).
                arg(pos.getX(),3,'f').
                arg(pos.getY(),3,'f').
                arg(angle,3,'f').
                arg(t.hour(),2,'d').
                arg(t.minute(),2,'d').
                arg(t.second(),2,'d').
                arg(t.msec(),3,'d');
    pt_txt.setX(x + 120 + (car_len - 190) * ((cos(pos.getYaw()) + 1) / 2));
    pt_txt.setY(y);
    painter.setTransform(txtTrans);
    pt_txt = drawTrans.map(pt_txt);
    rect_txt.setCoords(pt_txt.x(), pt_txt.y() - 20,
                       pt_txt.x() + 400, pt_txt.y() + 100);
    painter.drawText(rect_txt, txt);

}

void MapWidget::paintGrid(QPainter &painter,QPen &pen, QTransform drawTrans, QTransform txtTrans, int width, int height, double stepGrid,QString txt, QPointF pt_txt,const QColor textColor, const QColor zeroAxisColor,const QColor firstAxisColor,const QColor secondAxisColor,const double zeroAxisWidth)
{
    // Grid boundries in mm
    const double xStart = -ceil(width / stepGrid / mScaleFactor) * stepGrid - ceil(mXOffset / stepGrid / mScaleFactor) * stepGrid;
    const double xEnd = ceil(width / stepGrid / mScaleFactor) * stepGrid - floor(mXOffset / stepGrid / mScaleFactor) * stepGrid;
    const double yStart = -ceil(height / stepGrid / mScaleFactor) * stepGrid - ceil(mYOffset / stepGrid / mScaleFactor) * stepGrid;
    const double yEnd = ceil(height / stepGrid / mScaleFactor) * stepGrid - floor(mYOffset / stepGrid / mScaleFactor) * stepGrid;

    painter.setTransform(txtTrans);

    // Draw Y-axis segments
    for (double i = xStart;i < xEnd;i += stepGrid) {
        if (fabs(i) < 1e-3) {
            i = 0.0;
        }

        if ((int)(i / stepGrid) % 2) {
            pen.setWidth(0);
            pen.setColor(firstAxisColor);
            painter.setPen(pen);
        } else {
            txt=QString("%1 m").arg(i / 1000.0,2,'f');

            pt_txt.setX(i);
            pt_txt.setY(0);
            pt_txt = drawTrans.map(pt_txt);
            pt_txt.setX(pt_txt.x() - 5);
            pt_txt.setY(height - 10);
            painter.setPen(QPen(textColor));
            painter.save();
            painter.translate(pt_txt);
            painter.rotate(-90);
            painter.drawText(0, 0, txt);
            painter.restore();

            if (fabs(i) < 1e-3) {
                pen.setWidthF(zeroAxisWidth);
                pen.setColor(zeroAxisColor);
            } else {
                pen.setWidth(0);
                pen.setColor(secondAxisColor);
            }
            painter.setPen(pen);
        }

        QPointF pt_start(i, yStart);
        QPointF pt_end(i, yEnd);
        pt_start = drawTrans.map(pt_start);
        pt_end = drawTrans.map(pt_end);
        painter.drawLine(pt_start, pt_end);
    }

    // Draw X-axis segments
    for (double i = yStart;i < yEnd;i += stepGrid) {
        if (fabs(i) < 1e-3) {
            i = 0.0;
        }

        if ((int)(i / stepGrid) % 2) {
            pen.setWidth(0);
            pen.setColor(firstAxisColor);
            painter.setPen(pen);
        } else {
            txt=QString("%1 m").arg(i / 1000.0,2,'f');
            pt_txt.setY(i);

            pt_txt = drawTrans.map(pt_txt);
            pt_txt.setX(10);
            pt_txt.setY(pt_txt.y() - 5);
            painter.setPen(QPen(textColor));
            painter.drawText(pt_txt, txt);

            if (fabs(i) < 1e-3) {
                pen.setWidthF(zeroAxisWidth);
                pen.setColor(zeroAxisColor);
            } else {
                pen.setWidth(0);
                pen.setColor(secondAxisColor);
            }
            painter.setPen(pen);
        }

        QPointF pt_start(xStart, i);
        QPointF pt_end(xEnd, i);
        pt_start = drawTrans.map(pt_start);
        pt_end = drawTrans.map(pt_end);
        painter.drawLine(pt_start, pt_end);
    }
}

void MapWidget::paintTiles(QPainter &painter,const double cx, const double cy, const double view_w, const double view_h, bool highQuality)
{
    double i_llh[3];
    i_llh[0] = mRefLat;
    i_llh[1] = mRefLon;
    i_llh[2] = mRefHeight;

    mOsmZoomLevel = (int)round(log(mScaleFactor * mOsmRes * 100000000.0 *
                                    cos(i_llh[0] * M_PI / 180.0)) / log(2.0));
    if (mOsmZoomLevel > mOsmMaxZoomLevel) {
        mOsmZoomLevel = mOsmMaxZoomLevel;
    } else if (mOsmZoomLevel < 0) {
        mOsmZoomLevel = 0;
    }

    int xt = OsmTile::long2tilex(i_llh[1], mOsmZoomLevel);
    int yt = OsmTile::lat2tiley(i_llh[0], mOsmZoomLevel);

    double llh_t[3];
    llh_t[0] = OsmTile::tiley2lat(yt, mOsmZoomLevel);
    llh_t[1] = OsmTile::tilex2long(xt, mOsmZoomLevel);
    llh_t[2] = 0.0;

    double xyz[3];
    utility::llhToEnu(i_llh, llh_t, xyz);

    // Calculate scale at ENU origin
    double w = OsmTile::lat2width(i_llh[0], mOsmZoomLevel);

    int t_ofs_x = (int)ceil(-(cx - view_w / 2.0) / w);
    int t_ofs_y = (int)ceil((cy + view_h / 2.0) / w);

    if (!highQuality) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, mAntialiasOsm);
    }

    QTransform transOld = painter.transform();
    QTransform trans = painter.transform();
    trans.scale(1, -1);
    painter.setTransform(trans);

    for (int j = 0;j < 40;j++) {
        for (int i = 0;i < 40;i++) {
            int xt_i = xt + i - t_ofs_x;
            int yt_i = yt + j - t_ofs_y;
            double ts_x = xyz[0] + w * i - (double)t_ofs_x * w + mOsmOffsetE;
            double ts_y = -xyz[1] + w * j - (double)t_ofs_y * w - mOsmOffsetN;

            // We are outside the view
            if (ts_x > (cx + view_w / 2.0)) {
                break;
            } else if ((ts_y - w) > (-cy + view_h / 2.0)) {
                break;
            }

            int res;
            OsmTile t = mOsm->getTile(mOsmZoomLevel, xt_i, yt_i, res);

            if (w < 0.0) {
                w = t.getWidthTop();
            }

            painter.drawPixmap(ts_x * 1000.0, ts_y * 1000.0,
                               w * 1000.0, w * 1000.0, t.pixmap());

            if (res == 0 && !mOsm->downloadQueueFull()) {
                mOsm->downloadTile(mOsmZoomLevel, xt_i, yt_i);
            }
        }
    }

    // Restore painter
    painter.setTransform(transOld);

    if (!highQuality) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, mAntialiasDrawings);
    }

}

void MapWidget::setMainWindow(MainWindow* _mw)
{
    mw=_mw;
}

void MapWidget::setDb(database* _db)
{
    db=_db;
};

bool MapWidget::loadXMLRoute(QXmlStreamReader* stream,bool isBorder)
{
    // Look for routes tag
    bool routes_found = false;
    while (stream->readNextStartElement()) {
        if (stream->name() == QLatin1String("routes")) {
            routes_found = true;
            break;
        }
    }

    if (routes_found) {
//        qDebug() << "found route";
        QList<QPair<int, MapRoute > > routes;

        while (stream->readNextStartElement()) {
            QString name = stream->name().toString();

            if (name == "route") {
                int id = -1;
                MapRoute route;
                while (stream->readNextStartElement()) {
                    QString name2 = stream->name().toString();
                    if (name2 == "id") {
                        id = stream->readElementText().toInt();
                    } else if (name2 == "point") {
                        LocPoint p;
                        LocPoint::loadFromXML(p, stream);
                        route.append(p);
                    } else {
                        qWarning() << ": Unknown XML element :" << name2;
                        stream->skipCurrentElement();
                    }

                    if (stream->hasError()) {
                        qWarning() << " : XML ERROR :" << stream->errorString();
                    }
                }
                MapRoute routeCopy = route; // Does copying work? WORKS
                routes.append(QPair<int, MapRoute >(id, route)); // DOES NOT WORK

//               unusedroutes.append(QPair<int, MapRoute >(id, route)); DOES NOT WORK
//                routes.append(QPair<int, MapRoute >(id, routeCopy)); // DOES NOT WORK
            } else if (name == "anchors") {
                while (stream->readNextStartElement()) {
                    QString name2 = stream->name().toString();

                    if (name2 == "anchor") {
                        LocPoint p;
                        LocPoint::loadFromXML(p, stream);
//                        anchors.append(p);
                    } else {
                        qWarning() << ": Unknown XML element :" << name2;
                        stream->skipCurrentElement();
                    }

                    if (stream->hasError()) {
                        qWarning() << " : XML ERROR :" << stream->errorString();
                    }
                }
            }
            if (stream->hasError()) {
                qWarning() << "XML ERROR :" << stream->errorString();
                qWarning() << stream->lineNumber() << stream->columnNumber();
            }
        }
        for (QPair<int, MapRoute > r: routes) {
            if (r.first >= 0) {
                if (isBorder)
                {
                    int fieldLast = this->getFieldNow();
                    this->setFieldNow(r.first);
                    this->setField(r.second);
                    this->setFieldNow(fieldLast);
                } else
                {
                    int routeLast = this->getPathNow();
                    this->setPathNow(r.first);
                    this->setPath(r.second);
                    this->setPathNow(routeLast);
                }
            } else {
                if (isBorder)
                {
                    this->addField(r.second);
                } else
                {
                    this->addPath(r.second);
                }
            }
        }
    }
    return routes_found;
}

void MapWidget::saveXMLCurrentRoute(QXmlStreamWriter* stream)
{
    stream->setAutoFormatting(true);
    stream->writeStartDocument();

    stream->writeStartElement("routes");

    MapRoute *currentRoute;
    int mRoutePointSelected;
    if (focusBorder)
    {
        currentRoute=&(mFields->getCurrent());
        qDebug() << "Acting on border";
        qDebug() << "Border size: " << currentRoute->size();
    } else
    {
        currentRoute=&(mPaths->getCurrent());
        qDebug() << "Acting on path";
    }
    currentRoute->saveXMLRoute(stream, false, 0);

    stream->writeEndDocument();
}

void MapRoute::saveXMLRoute(QXmlStreamWriter* stream,bool withId, int i) const
{
    stream->writeStartElement("route");

    if (withId) {
        stream->writeTextElement("id", QString::number(i));
    }

    for (const LocPoint p: mRoute) {
        p.saveToXML(stream);
    }
    stream->writeEndElement();
}

/*
void MapWidget::saveXMLRoute(QXmlStreamWriter* stream, MapRoute route,bool withId, int i)
{
    stream->writeStartElement("route");

    if (withId) {
        stream->writeTextElement("id", QString::number(i));
    }

    for (const LocPoint p: route.mRoute) {
        stream->writeStartElement("point");
        stream->writeTextElement("x", QString::number(p.getX()));
        stream->writeTextElement("y", QString::number(p.getY()));
        stream->writeTextElement("speed", QString::number(p.getSpeed()));
        stream->writeTextElement("time", QString::number(p.getTime()));
        stream->writeTextElement("attributes", QString::number(p.getAttributes()));
        stream->writeEndElement();
    }
    stream->writeEndElement();
}
*/

bool MapWidget::saveRoutes(QString filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Save Routes",
                              "Could not open\n" + filename + "\nfor writing");
        return false;
    }
    QXmlStreamWriter stream(&file);
    saveXMLRoutes(&stream,false);
    file.close();
    return true;
}


void MapWidget::saveXMLRoutes(QXmlStreamWriter* stream, bool withId)
{
    stream->setAutoFormatting(true);
    stream->writeStartDocument();

    stream->writeStartElement("routes");

//    QList<LocPoint> anchors = this->getAnchors();
    QList<MapRoute> routes;
    if (focusBorder)
    {
        routes = this->getFields();
    } else
    {
        routes = this->getPaths();
    }
    for (int i = 0;i < routes.size();i++) {
        if (!routes.at(i).isEmpty()) {
            routes.at(i).saveXMLRoute(stream,i,withId);
        }
    }
    stream->writeEndDocument();
}


void MapWidget::updateTraces()
{
    if (bUpdateable)
    {
        // Store trace for the selected car
        if (mTraceCar >= 0) {
            for (int i = 0;i < mCarInfo.size();i++) {
                CarInfo &carInfo = mCarInfo[i];
                if (carInfo.getId() == mTraceCar) {
                    if (mCarTrace.isEmpty()) {
                        mCarTrace.append(carInfo.getLocation());
                    }
                    if (mCarTrace.last().getDistanceTo(carInfo.getLocation()) > mTraceMinSpaceCar) {
                        mCarTrace.append(carInfo.getLocation());
                    }
                    // GPS trace
                    if (mCarTraceGps.isEmpty()) {
                        mCarTraceGps.append(carInfo.getLocationGps());
                    }
                    if (mCarTraceGps.last().getDistanceTo(carInfo.getLocationGps()) > mTraceMinSpaceGps) {
                        mCarTraceGps.append(carInfo.getLocationGps());
                    }
                    // UWB trace
                    if (mCarTraceUwb.isEmpty()) {
                        mCarTraceUwb.append(carInfo.getLocationUwb());
                    }
                    if (mCarTraceUwb.last().getDistanceTo(carInfo.getLocationUwb()) > mTraceMinSpaceCar) {
                        mCarTraceUwb.append(carInfo.getLocationUwb());
                    }
                }
            }
            //            qDebug() << "---";
            //            qDebug() << carInfo.getLocation().getX();
            //            qDebug() << carInfo.getLocationGps().getX();
            //            qDebug() << carInfo.getLocationUwb().getX();
        }

        // Truncate traces
        while (mCarTrace.size() > 5000) {
            mCarTrace.removeFirst();
        }

        while (mCarTraceGps.size() > 1800) {
            mCarTraceGps.removeFirst();
        }

        while (mCarTraceUwb.size() > 5000) {
            mCarTraceUwb.removeFirst();
        }
    }
}

std::array<double, 4> MapWidget::findExtremeValuesFieldBorders()
{
    std::array<double, 4> extremes;
    extremes[0]=9999999;
    extremes[1]=9999999;
    extremes[2]=-9999999;
    extremes[3]=-9999999;

    QList<LocPoint> allpoints;
    /*    for (const MapRoute& route : mRoutes)
    {
        if (route.getIsBorder())
        allpoints.append(route.mRoute);
    }*/

    /*
    for (const MapRoute& field : mFields->mCollection)
    {
        allpoints.append(field.mRoute);
    }*/

    for (const MapRoute& field : mFields->mCollection) {
        qDebug() << "Field mRoute size:" << field.mRoute.size();
        for (const LocPoint& point : field.mRoute) {
            qDebug() << "LocPoint: mX=" << point.getX() << ", mY=" << point.getY() << ", mInfo=" << point.getInfo();
        }
        qDebug() << "Before appending, size: "  << field.mRoute.size();
        allpoints.append(field.mRoute.begin(), field.mRoute.end());
        qDebug() << "After appending, size: "  << field.mRoute.size();
    }

    for (const LocPoint& point : allpoints)
    {
        double x=point.getX();
        double y=point.getY();

        if (x<extremes[0]) extremes[0]=x;
        if (x>extremes[2]) extremes[2]=x;
        if (y<extremes[1]) extremes[1]=y;
        if (y>extremes[3]) extremes[3]=y;

    }
    return extremes;
}

/*
MapRoute::MapRoute(QList<LocPoint> route, QList<LocPoint> infotrace)
{
    mRoute=route;
    mInfoTrace=infotrace;
}
*/

MapRoute& MapWidget::getCurrentPath()
{
    return mPaths->getCurrent();
};



