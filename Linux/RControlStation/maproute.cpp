#include "maproute.h"
#include "attributes_masks.h"
#include "mainwindow.h"

MapRoute::MapRoute()
    : bAnalysisShowable(false),
    iAnalysisStart(-1),
    iAnalysisEnd(-1),
    mRoute(),    // Initialize empty QList<LocPoint>
    mInfoTrace() // Initialize empty QList<LocPoint>
{
 //   qDebug() << "MapRoute default constructor called";
}

MapRoute::MapRoute(const MapRoute &other)
    : bAnalysisShowable(other.bAnalysisShowable),
    iAnalysisStart(other.iAnalysisStart),
    iAnalysisEnd(other.iAnalysisEnd),
    mRoute(other.mRoute),          // Copy the QList<LocPoint>
    mInfoTrace(other.mInfoTrace)  // Copy the QList<LocPoint>
{
  //  qDebug() << "MapRoute copy constructor called";
}

MapRoute::~MapRoute() {
  //  qDebug() << "MapRoute destructor called";
}

// Copy assignment operator
MapRoute &MapRoute::operator=(const MapRoute &other) {
    if (this != &other) { // Protect against self-assignment
        bAnalysisShowable = other.bAnalysisShowable;
        iAnalysisStart = other.iAnalysisStart;
        iAnalysisEnd = other.iAnalysisEnd;
        mRoute = other.mRoute;          // Copy the QList<LocPoint>
        mInfoTrace = other.mInfoTrace;  // Copy the QList<LocPoint>
    }
    // qDebug() << "MapRoute copy assignment called";
    return *this;
}

MapRoute::MapRoute(MapRoute &&other) noexcept
    : bAnalysisShowable(other.bAnalysisShowable),
    iAnalysisStart(other.iAnalysisStart),
    iAnalysisEnd(other.iAnalysisEnd),
    mRoute(std::move(other.mRoute)),          // Move the QList<LocPoint>
    mInfoTrace(std::move(other.mInfoTrace))  // Move the QList<LocPoint>
{
    // qDebug() << "MapRoute move constructor called";
}

// Move assignment operator (C++11+)
MapRoute &MapRoute::operator=(MapRoute &&other) noexcept {
    if (this != &other) {
        bAnalysisShowable = other.bAnalysisShowable;
        iAnalysisStart = other.iAnalysisStart;
        iAnalysisEnd = other.iAnalysisEnd;
        mRoute = std::move(other.mRoute);          // Move the QList<LocPoint>
        mInfoTrace = std::move(other.mInfoTrace);  // Move the QList<LocPoint>
    }
    qDebug() << "MapRoute move assignment called";
    return *this;
}

void MapRoute::clear()
{
    mRoute.clear();
}

bool MapRoute::isEmpty() const
{
    return mRoute.isEmpty();
}

void MapRoute::append(LocPoint point)
{
//    qDebug() << "Appending to mRoute:" << point.getX() << point.getY();
    mRoute.append(point);
}

void MapRoute::append(const QList<LocPoint> &points)
{
//    qDebug() << "Appending QList<LocPoint> to mRoute";
    mRoute.append(points);
}

void MapRoute::append(const MapRoute &mr)
{
//    qDebug() << "Appending MapRoute to mRoute";
    mRoute.append(mr.mRoute);
}

LocPoint& MapRoute::first()
{
    return mRoute.first();
}

LocPoint& MapRoute::last()
{
    return mRoute.last();
}

void MapRoute::removeLast()
{
    mRoute.removeLast();
}
void MapRoute::prepend(const LocPoint &value)
{
    mRoute.prepend(value);
}

void MapRoute::removeAt(int i)
{
    mRoute.removeAt(i);
}

QList<LocPoint>::const_iterator MapRoute::begin() const
{
    return mRoute.begin();
}

QList<LocPoint>::const_iterator MapRoute::end() const
{
    return mRoute.end();
}

double MapRoute::getArea()
{
    int n = this->size();
    double area = 0.0;

    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        area += mRoute[i].getX() * mRoute[j].getY();
        area -= mRoute[i].getY() * mRoute[j].getX();
    }
    area = std::abs(area) * 0.5;
    return area;
}

double MapRoute::getLength(bool bForAnalysis)
{

    int iMin = (bForAnalysis) ? iAnalysisStart : 0;
    int iMax = (bForAnalysis) ? iAnalysisEnd : size();
    double length=0;

    for (int i = iMin; i <= iMax; ++i) {
        qDebug() << i;
        double dx=(mRoute[i].getX()-mRoute[i-1].getX());
        double dy=(mRoute[i].getY()-mRoute[i-1].getY());
        double linelength=sqrt(dx*dx+dy*dy);
        length+=linelength;
    }
    return length;
}


LocPoint &	MapRoute::operator[](int i)
{
    return mRoute[i];
}

LocPoint &	MapRoute::at(int i)
{
    return mRoute[i];
}

void MapRoute::setIsBorder(bool border)
{
    isBorder=border;
}

bool MapRoute::getIsBorder() const
{
    return isBorder;
}

void MapRoute::setAnalysisFocus()
{
    iAnalysisStart=-1;
    iAnalysisEnd=-1;
    updateAnalysis();
}

void MapRoute::updateAnalysis()
{
    bAnalysisShowable=(iAnalysisStart>0) && (iAnalysisEnd>0) && (iAnalysisEnd>iAnalysisStart);
    if (bAnalysisShowable)
    {
        double linelength=getLength(true);
        findMainWindow()->getLogLabel()->setText("Length:" + QString::number(linelength));
    }
    return;
}



/*
void MapRoute::paint(MapWidget* mapWidget, QPainter &painter, QPen &pen, bool isSelected, double mScaleFactor, QTransform drawTrans, QString txt, QPointF pt_txt, QRectF rect_txt, QTransform txtTrans, bool highQuality)
{
    if (isBorder)
    {
        paintBorder(painter, pen, isSelected, mScaleFactor, drawTrans);
    } else
    {
        paintPath(mapWidget, painter, pen, isSelected, mScaleFactor, drawTrans, txt, pt_txt, rect_txt, txtTrans, highQuality);
    }
}
*/

void MapRoute::paintLine(int i, bool isSelected, bool isAnalysed, QPainter &painter, QPen &pen, Qt::GlobalColor defaultDarkColor, Qt::GlobalColor defaultColor, int selectedActionAttribute)
{
    // Store the original pen width to preserve it
    qreal originalWidth = pen.widthF();
    
    if (isAnalysed) {
        // qDebug() << "show analysis line: " << i;
        pen.setColor(Qt::darkBlue);
        painter.setBrush(Qt::blue);
    } else if (isSelected && selectedActionAttribute != 0) {
        // Use the new simplified logic based on selected action attribute
        quint32 attr = mRoute[i].getAttributes();
        if (attr == selectedActionAttribute) {
            // Attribute is on - use cyan colors
            pen.setColor(Qt::darkCyan);
            painter.setBrush(Qt::cyan);
        } else if (attr == (selectedActionAttribute - 8)) {
            // Attribute is off - use yellow colors
            pen.setColor(Qt::darkYellow);
            painter.setBrush(Qt::yellow);
        } else {
            // No match - use default colors (ignore this attribute)
            pen.setColor(defaultDarkColor);
            painter.setBrush(defaultColor);
        }
    } else {
        // Use default colors for unselected or when no specific attribute is selected
        pen.setColor(defaultDarkColor);
        painter.setBrush(defaultColor);
    }

    // Set the pen with the preserved width
    pen.setWidthF(originalWidth);
    painter.setPen(pen);
    painter.setOpacity(0.7);
    painter.drawLine(mRoute[i - 1].getX() * 1000.0, mRoute[i - 1].getY() * 1000.0,
                     mRoute[i].getX() * 1000.0, mRoute[i].getY() * 1000.0);
/*//
    if (isImplementsDownAndPathSelected) {
        pen.setBrush(Qt::red);
        painter.setPen(pen);
        painter.drawLine(mRoute[i - 1].getX() * 1000.0, mRoute[i - 1].getY() * 1000.0+10000,
                         mRoute[i].getX() * 1000.0, mRoute[i].getY() * 1000.0+10000);

    };*/
    painter.setOpacity(1.0);
}

void MapRoute::paintPoint(QPointF p, quint32 attr, bool isSelected, bool isAnalysed, QPainter &painter, QPen &pen, double mScaleFactor, QTransform drawTrans, bool highQuality, int selectedActionAttribute, bool isControlSelected)
{
    // qDebug() << "MapRoute::paintPoint called with selectedActionAttribute:" << selectedActionAttribute << "isSelected:" << isSelected << "attr:" << attr;
    painter.setTransform(drawTrans);

    if (highQuality) {
        if (isAnalysed)
        {
            qDebug() << "show analysis point";
            pen.setColor(Qt::darkBlue);
            painter.setBrush(Qt::blue);
        } else if (isSelected) {
            // Use the new simplified logic based on selected action attribute
            if (selectedActionAttribute == 0) {
                // No specific attribute selected - use default colors
                pen.setColor(Qt::darkYellow);
                painter.setBrush(Qt::yellow);
            } else {
                // Check if the attribute matches the selected action attribute or its off state
                if (attr == selectedActionAttribute) {
                    // Attribute is on - use cyan colors
                    pen.setColor(Qt::darkCyan);
                    painter.setBrush(Qt::cyan);
                } else if (attr == (selectedActionAttribute - 8)) {
                    // Attribute is off - use yellow colors
                    pen.setColor(Qt::darkYellow);
                    painter.setBrush(Qt::yellow);
                } else {
                    // No match - use existing default colors (ignore this attribute)
                    pen.setColor(Qt::darkGray);
                    painter.setBrush(Qt::gray);
                }
            }
        } else if (isControlSelected) {
            // Point is selected by control search - use special highlighting
            // TODO: Get color from database based on controlId
            pen.setColor(Qt::darkMagenta);
            painter.setBrush(Qt::magenta);
        } else {
            pen.setColor(Qt::darkGray);
            painter.setBrush(Qt::gray);
        }

        pen.setWidthF(3.0 / mScaleFactor);
        painter.setPen(pen);

        painter.drawEllipse(p, 10.0 / mScaleFactor,
                            10.0 / mScaleFactor);
    } else {
        // qDebug() << "low quality value: " << attr;
        // Use simplified logic for low quality as well
        int circleType = 1; // default (gray)
        if (isControlSelected) {
            // Control-selected points get highest priority - use magenta (type 4)
            circleType = 4;
        } else if (isSelected) {
            if (selectedActionAttribute == 0) {
                // No specific attribute selected - use yellow (type 0)
                circleType = 0;
            } else {
                // Check if the attribute matches the selected action attribute or its off state
                if (attr == selectedActionAttribute) {
                    // Attribute is on - use cyan (type 3)
                    circleType = 3;
                } else if (attr == (selectedActionAttribute - 8)) {
                    // Attribute is off - use yellow (type 0)
                    circleType = 0;
                } else {
                    // No match - use default gray (type 1)
                    circleType = 1;
                }
            }
        }
        drawCircleFast(painter, p, 10.0 / mScaleFactor, circleType);
    }
}

//MapWidget* mapWidget,
void MapRoute::paintInfoText(bool mDrawRouteText, int i, QPointF p, bool isSelected, QPainter &painter, QPen &pen, double mScaleFactor, QTransform drawTrans, QString txt, QPointF pt_txt, QRectF rect_txt, QTransform txtTrans)
{
    // Draw text only for selected route
    if (isSelected && mDrawRouteText) {
        QTime t = QTime::fromMSecsSinceStartOfDay(mRoute[i].getTime());
        txt=QString("P: %1 %2\n"
                      "%3 km/h\n"
                      "%4:%5:%6:%7\n"
                      "A: %8").
              arg(i).
              arg(((i == 0) ? "- start" : ((i == this->size()-1) ? "- end" : ""))).
              arg(mRoute[i].getSpeed() * 3.6,1,'f',1).
              arg(t.hour(),2,'d').
              arg(t.minute(),2,'d').
              arg(t.second(),2,'d').
              arg(t.msec(),3,'d').
              arg(mRoute[i].getAttributes());

        pt_txt.setX(p.x() + 10 / mScaleFactor);
        pt_txt.setY(p.y());
        painter.setTransform(txtTrans);
        pt_txt = drawTrans.map(pt_txt);
        pen.setColor(Qt::black);
        painter.setPen(pen);
        rect_txt.setCoords(pt_txt.x(), pt_txt.y() - 20,
                           pt_txt.x() + 200, pt_txt.y() + 60);
        painter.drawText(rect_txt, txt);
    } else {
        //           txt.sprintf("%d", rn);
        pt_txt.setX(p.x());
        pt_txt.setY(p.y());
        painter.setTransform(txtTrans);
        pt_txt = drawTrans.map(pt_txt);
        pen.setColor(Qt::black);
        painter.setPen(pen);
        rect_txt.setCoords(pt_txt.x() - 20, pt_txt.y() - 20,
                           pt_txt.x() + 20, pt_txt.y() + 20);
        painter.drawText(rect_txt, Qt::AlignCenter, txt);
    }
}
void MapRoute::paintPath(QPainter &painter, QPen &pen, bool isSelected, double mScaleFactor, QTransform drawTrans, QString txt, QPointF pt_txt, QRectF rect_txt, QTransform txtTrans, bool mDrawRouteText, bool highQuality, int selectedActionAttribute, const QList<int> &controlSelectedPoints)
{
        qDebug() << "paintPath called with" << controlSelectedPoints.size() << "control-selected points";
    Qt::GlobalColor defaultDarkColor = Qt::darkGray;
    Qt::GlobalColor defaultColor = Qt::gray;
    bool showSowing=false;
    if (isSelected) {
        defaultDarkColor = Qt::darkYellow;
        defaultColor = Qt::yellow;
    }
    pen.setWidthF(5.0 / mScaleFactor);
    painter.setTransform(drawTrans);

    int nPoints=this->size();
    for (int i = 1;i < nPoints;i++) {
        bool isAnalysed=(iAnalysisStart!=-1) && (iAnalysisEnd!=-1) && (i>=iAnalysisStart) && (i<iAnalysisEnd);
        paintLine(i,isSelected,isAnalysed,painter,pen,defaultDarkColor,defaultColor, selectedActionAttribute);
    }

    for (int i = 0;i < nPoints;i++) {
        QPointF p = mRoute[i].getPointMm();
        quint32 attr = mRoute[i].getAttributes();
        bool isAnalysed=(iAnalysisStart!=-1) && (iAnalysisEnd!=-1) && (i>=iAnalysisStart) && (i<iAnalysisEnd);
        bool isControlSelected = controlSelectedPoints.contains(i);
        paintPoint(p,attr,isSelected,isAnalysed,painter,pen,mScaleFactor,drawTrans, highQuality, selectedActionAttribute, isControlSelected);

        // Draw highlight for first and last point in active route
        if (isSelected && (i == 0 || i == nPoints-1)) {
            QPointF ptmp;
            ptmp.setX(p.x() - 7.0 / mScaleFactor);
            ptmp.setY(p.y() + 7.0 / mScaleFactor);
            if (i == 0) {
                pen.setColor(Qt::green);
                painter.setBrush(Qt::darkGreen);
            } else {
                pen.setColor(Qt::red);
                painter.setBrush(Qt::darkRed);
            }
            pen.setWidthF(2.0 / mScaleFactor);
            painter.setPen(pen);
            painter.drawEllipse(ptmp, 5.0 / mScaleFactor,
                                5.0 / mScaleFactor);
        }

        paintInfoText(mDrawRouteText,i,p,isSelected,painter,pen,mScaleFactor, drawTrans, txt, pt_txt, rect_txt, txtTrans);
        // mapWidget->mDrawRouteText
    }

}

void MapRoute::paintBorder(QPainter &painter, QPen &pen, bool isSelected, double mScaleFactor, QTransform drawTrans)
{
    pen.setWidthF(5.0 / mScaleFactor);
    painter.setTransform(drawTrans);

    QPolygon polygon;
    int nPoints=this->size();
    //    for (int i = 1;i < nPoints;i++) {
    for (int i = 0;i < nPoints;i++) {
        double x=mRoute[i].getX() * 1000.0;
        double y=mRoute[i].getY() * 1000.0;

        polygon << QPoint(x, y);
        QPointF p = mRoute[i].getPointMm();
        painter.setPen(QPen(Qt::black, 3.0 / mScaleFactor)); // Set the pen color to blue and width to 2
        painter.drawEllipse(p, 5.0 / mScaleFactor,
                            5.0 / mScaleFactor);

    }
    // Set the brush color for filling the polygon
    painter.setBrush(QBrush(Qt::yellow));
    if (isSelected)
    {
        painter.setOpacity(1.0);
    } else
    {
        painter.setOpacity(0.35);
    }
    // Set the pen color for drawing the border
    painter.setPen(QPen(Qt::darkRed, 5.0 / mScaleFactor, Qt::DashDotLine, Qt::RoundCap)); // Set the pen color to blue and width to 2

    // Draw and fill the polygon
    painter.drawPolygon(polygon);
}

void MapRoute::routeinfo(QPainter &painter,double start_txt,const double txtOffset,const double txt_row_h, int width, QString txt)
{
    int nPoints=this->size();
    if (nPoints > 0) {
        LocPoint prev = this->first();
        double len = 0.0;
        for (int i = 1;i < nPoints;i++) {
            len += prev.getDistanceTo(mRoute[i]);
            prev = mRoute[i];
        }

        txt=QString("RP: %1").arg(nPoints);
        painter.drawText(width - txtOffset, start_txt, txt);
        start_txt += txt_row_h;

        txt=QString("RLen: %1 m").arg(len,2,'f');
        painter.drawText(width - txtOffset, start_txt, txt);
        start_txt += txt_row_h;
    }
}

void MapRoute::drawCircleFast(QPainter &painter, QPointF center, double radius, int type)
{
    painter.drawPixmap(center.x() - radius, center.y() - radius,
                       2.0 * radius, 2.0 * radius, getPixmaps().at(type));
}

QList<QPixmap> MapRoute::mPixmaps;

const QList<QPixmap>& MapRoute::getPixmaps() {
    if (mPixmaps.isEmpty()) {
        initPixmaps();
    }
    return mPixmaps;
}

void MapRoute::initPixmaps() {
    qDebug() << "Initializing mPixmaps";
    for (int i = 0; i < 7; i++) {
        QPixmap pix(24, 24);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        QPen pen;
        pen.setWidth(4);
        switch (i) {
        case 0: {
            pen.setColor(Qt::darkYellow);
            p.setBrush(Qt::yellow);
            p.setPen(pen);
            p.drawEllipse(2, 2, 20, 20);
        } break;
        case 1: {
            pen.setColor(Qt::darkGray);
            p.setBrush(Qt::gray);
            p.setPen(pen);
            p.drawEllipse(2, 2, 20, 20);
        } break;
        case 2: {
            pen.setColor(Qt::darkGreen);
            p.setBrush(Qt::green);
            p.setPen(pen);
            p.drawEllipse(2, 2, 20, 20);
        } break;
        case 3: {
            pen.setColor(Qt::darkCyan);
            p.setBrush(Qt::cyan);
            p.setPen(pen);
            p.drawEllipse(2, 2, 20, 20);
        } break;
        case 4: {
            pen.setColor(Qt::darkMagenta);
            p.setBrush(Qt::magenta);
            p.setPen(pen);
            p.drawEllipse(2, 2, 20, 20);
        } break;
        case 5: {
            pen.setColor(Qt::darkBlue);
            p.setBrush(Qt::blue);
            p.setPen(pen);
            p.drawEllipse(2, 2, 20, 20);
        } break;
        case 6: {
            pen.setColor(Qt::darkGray);
            p.setBrush(Qt::gray);
            p.setPen(pen);
            p.drawEllipse(2, 2, 20, 20);
        } break;
        }
        mPixmaps.append(pix);
    }
    qDebug() << "Done initializing mPixmaps";
}


void MapRoute::insert(int i,LocPoint &value)
{
    mRoute.insert(i,value);
}

void MapRoute::transform(double moveX,double moveY,double rotate)
{
    //no rotation yet!

    int nPoints=this->size();
    for (int i = 0;i < nPoints;i++) {
        mRoute[i].setX(mRoute[i].getX()+moveX);
        mRoute[i].setY(mRoute[i].getY()+moveY);
    }
}

void MapRoute::cut(int before,int after)
{
    qDebug() << "MAPROUTE";
    qDebug() << "before: " << before;
    qDebug() << "after: " << after;

    int nPoints=this->size();
    for (int i = nPoints-1;i > after;i--) {
        mRoute.removeAt(i);
    }
    for (int i = before;i >= 0;i--) {
        mRoute.removeAt(i);
    }
}

QList<MapRoute> MapRoute::cutByArea(double minX, double minY, double maxX, double maxY)
{
    qDebug() << "Cutting route by area:" << minX << "," << minY << "to" << maxX << "," << maxY;
    
    QList<MapRoute> resultRoutes;
    MapRoute* currentRoute = nullptr;
    bool insideArea = false;
    
    for (const LocPoint& point : mRoute) {
        double x = point.getX();
        double y = point.getY();
        
        // Check if the point is inside the area
        bool pointInside = (x >= minX && x <= maxX && y >= minY && y <= maxY);
        
        if (pointInside) {
            // Point is inside the area
            if (!insideArea) {
                // We just entered the area, start a new route
                currentRoute = new MapRoute();
                resultRoutes.append(*currentRoute);
                insideArea = true;
            }
            // Add the point to the current route
            currentRoute->append(point);
        } else {
            // Point is outside the area
            if (insideArea) {
                // We just exited the area, finalize the current route
                insideArea = false;
            }
            // Don't add points outside the area
        }
    }
    
    qDebug() << "Found" << resultRoutes.size() << "route sections within the area";
    return resultRoutes;
}

// Implement const-correct versions of size() and at()
int MapRoute::size() const
{
    return mRoute.size();
}

const LocPoint& MapRoute::at(int i) const
{
    return mRoute.at(i);
}

void MapRoute::setActivePoint(int p)
{
    iCurrentPoint=p;
}
LocPoint* MapRoute::getActivePoint(void)
{
    return &mRoute[iCurrentPoint];
};

int MapRoute::getActivePointNo(void)
{
    return iCurrentPoint;
};

