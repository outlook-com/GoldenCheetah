/*
 * Copyright (c) 2020 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "GenericPlot.h"

#include "Colors.h"
#include "TabView.h"
#include "RideFileCommand.h"
#include "Utils.h"

#include <limits>

GenericPlot::GenericPlot(QWidget *parent, Context *context) : QWidget(parent), context(context)
{
    // intitialise state info
    charttype=0;
    chartview=NULL;
    barseries=NULL;
    bottom=left=true;

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0,0,0,0);

    // setup the chart
    qchart = new QChart();
    qchart->setBackgroundVisible(false); // draw on canvas
    qchart->legend()->setVisible(false); // no legends --> custom todo
    qchart->setTitle("No title set"); // none wanted
    qchart->setAnimationOptions(QChart::NoAnimation);
    qchart->setFont(QFont());

    // set theme, but for now use a std one TODO: map color scheme to chart theme
    qchart->setTheme(QChart::ChartThemeDark);

    chartview = new QChartView(qchart, this);
    chartview->setRenderHint(QPainter::Antialiasing);

    // watch mouse hover etc on the chartview and scene
    chartview->setMouseTracking(true);
    chartview->scene()->installEventFilter(this);

    // add selector
    selector = new SelectionTool(this);
    chartview->scene()->addItem(selector);

    // the legend at the top for now
    legend = new GenericLegend(context, this);

    // filter ESC so we can stop scripts
    installEventFilter(this);

    // add all widgets to the view
    mainLayout->addWidget(legend);
    mainLayout->addWidget(chartview);

    // watch for colors changing
    connect(context, SIGNAL(configChanged(qint32)), this, SLOT(configChanged(qint32)));

    // config changed...
    configChanged(0);
}

//
// legends
//

GenericLegendItem::GenericLegendItem(Context *context, QWidget *parent, QString name, QColor color) :
    QWidget(parent), context(context), name(name), color(color)
{

    value=0;
    hasvalue=false;

    // set height and width, gets reset when configchanges
    configChanged(0);

    // watch for changes...
    connect(context, SIGNAL(configChanged(qint32)), this, SLOT(configChanged(qint32)));

}


void
GenericLegendItem::configChanged(qint32)
{
    static const double gl_margin = 3 * dpiXFactor;
    static const double gl_spacer = 3 * dpiXFactor;
    static const double gl_block = 7 * dpiXFactor;
    static const double gl_linewidth = 1 * dpiXFactor;

    // we just set geometry for now.
    QFont f; // based on what just got set in prefs
    QFontMetricsF fm(f);

    // so now the string we would display
    QString valuelabel = QString ("%1").arg("9999999.999"); // xxx later we might have scale/dp

    // maximum width of widget = margin + block + space + name + space + value + margin
    double width = gl_margin + gl_block + gl_spacer + fm.boundingRect(name).width()
                              + gl_spacer + fm.boundingRect(valuelabel).width() + gl_margin;

    // maximum height of widget = margin + textheight + spacer + line
    double height = gl_margin + fm.boundingRect(valuelabel).height() + gl_spacer + gl_linewidth;

    // now set geometry of widget
    setFixedWidth(width);
    setFixedHeight(height);

    // calculate all the rects used by the painter now since static
    blockrect = QRectF(gl_margin, gl_margin, gl_block, height-gl_margin);
    linerect = QRectF(gl_margin+gl_block, height-gl_linewidth, width-gl_margin, gl_linewidth);
    namerect = QRectF(gl_margin + gl_block + gl_spacer, gl_margin, fm.boundingRect(name).width(), fm.boundingRect(name).height());
    valuerect =QRectF(namerect.x() + namerect.width() + gl_spacer, gl_margin, fm.boundingRect(valuelabel).width(), fm.boundingRect(valuelabel).height());


    // redraw
    update();
}

void
GenericLegendItem::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.save();

    // fill background first
    painter.setBrush(QBrush(GColor(CPLOTBACKGROUND)));
    painter.setPen(Qt::NoPen);
    painter.drawRect(0,0,geometry().width()-1, geometry().height()-1);

    // block and line
    painter.setBrush(QBrush(color));
    painter.setPen(Qt::NoPen);
    //painter.drawRect(blockrect);
    painter.drawRect(linerect);

    // just paint the value for now
    QString string;
    if (hasvalue) string=QString("%1").arg(value, 0, 'f', 2);
    else string="   ";

    // remove redundat dps (e.g. trailing zeroes)
    string = Utils::removeDP(string);

    // set pen to series color for now
    painter.setPen(GCColor::invertColor(GColor(CPLOTBACKGROUND))); // use invert - usually black or white
    painter.setFont(QFont());

    // series
    painter.drawText(namerect, name, Qt::AlignHCenter|Qt::AlignVCenter);
    painter.drawText(valuerect, string, Qt::AlignHCenter|Qt::AlignVCenter);
    painter.restore();

}

GenericLegend::GenericLegend(Context *context, GenericPlot *plot) : context(context), plot(plot)
{
    layout = new QHBoxLayout(this);
    layout->addStretch();

    xname="";

    // get notifications when values change
    connect(plot->selector, SIGNAL(hover(QPointF,QString,QAbstractSeries*)), this, SLOT(hover(QPointF,QString,QAbstractSeries*)));
    connect(plot->selector, SIGNAL(unhover(QString)), this, SLOT(unhover(QString)));
    connect(plot->selector, SIGNAL(unhoverx()), this, SLOT(unhoverx()));
}

void
GenericLegend::addSeries(QString name, QAbstractSeries *series)
{
    // if it already exists remove it
    if (items.value(name,NULL) != NULL) removeSeries(name);

    GenericLegendItem *add = new GenericLegendItem(context, this, name, GenericPlot::seriesColor(series));
    layout->insertWidget(0, add);
    items.insert(name,add);

    // lets see ya!
    add->show();
}

void
GenericLegend::addX(QString name)
{
    // if it already exists remove it
    if (items.value(name,NULL) != NULL) removeSeries(name);

    GenericLegendItem *add = new GenericLegendItem(context, this, name, GColor(CPLOTMARKER));
    layout->insertWidget(0, add);
    items.insert(name,add);

    // lets see ya!
    add->show();

    // remember the x axis
    xname = name;
}

void
GenericLegend::removeSeries(QString name)
{
    GenericLegendItem *remove = items.value(name, NULL);
    if (remove) {
        layout->removeWidget(remove);
        items.remove(name);
        delete remove;
    }
}

void
GenericLegend::removeAllSeries()
{
    xname = "";
    QMapIterator<QString, GenericLegendItem*> i(items);
    while (i.hasNext()) {
        i.next();
        removeSeries(i.key());
    }
}

void
GenericLegend::hover(QPointF value, QString name, QAbstractSeries*)
{
    GenericLegendItem *call = items.value(name, NULL);
    if (call) call->setValue(value.y());

    // xaxis
    GenericLegendItem *xaxis = items.value(xname, NULL);
    if (xaxis) xaxis->setValue(value.x());
}

void
GenericLegend::unhover(QString name)
{
    GenericLegendItem *xaxis = items.value(name,NULL);
    if (xaxis) xaxis->noValue();
}

void
GenericLegend::unhoverx()
{
    if (xname != "") {
        GenericLegendItem *xaxis = items.value(xname, NULL);
        if (xaxis) xaxis->noValue();
    }
}

//
// Selecting points on the plot
//
SelectionTool::SelectionTool(GenericPlot *host) : QObject(host), QGraphicsItem(NULL), host(host)
{
    // start inactive and rectangle
    state = INACTIVE;
    mode = RECTANGLE;
    setVisible(true); // always visible - paints on axis
    setZValue(100); // always on top.
    hoverpoint = QPointF();
    hoverseries = NULL;
    rect = QRectF(0,0,0,0);
    connect(&drag, SIGNAL(timeout()), this, SLOT(dragStart()));
}

// the selection tool is painted only when it is active, but will be
// a lassoo or rectangle shape
void SelectionTool::paint(QPainter*painter, const QStyleOptionGraphicsItem *, QWidget*)
{
   // don't paint outside the canvas
   painter->save();
   painter->setClipRect(mapRectFromScene(host->qchart->plotArea()));

   // min max texts
   QFont stGiles; // hoho - Chart Font St. Giles ... ok you have to be British to get this joke
   stGiles.fromString(appsettings->value(NULL, GC_FONT_CHARTLABELS, QFont().toString()).toString());
   stGiles.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());

   switch (mode) {
   case CIRCLE:
        {

        }
        break;

   case XRANGE:
        {

            // current position for each series - we only do first, coz only interested in x axis anyway
            foreach(QAbstractSeries *series, host->qchart->series()) {

                // convert screen position to value for series
                QPointF v = host->qchart->mapToValue(spos,series);
                double miny=0;
                foreach (QAbstractAxis *axis, series->attachedAxes()) {
                    if (axis->orientation() == Qt::Vertical && axis->type()==QAbstractAxis::AxisTypeValue) {
                        miny=static_cast<QValueAxis*>(axis)->min();
                        break;
                    }
                }
                QPointF posxp = mapFromScene(host->qchart->mapToPosition(QPointF(v.x(),miny),series));

                QPen markerpen(GColor(CPLOTMARKER));
                painter->setPen(markerpen);
                painter->setBrush(QBrush(GColor(CPLOTBACKGROUND)));

                QFontMetrics fm(stGiles); // adjust position to align centre
                painter->setFont(stGiles);

                // x value
                QString label=QString("%1").arg(v.x(),0,'f',0); // no decimal places XXX fixup on series info
                label = Utils::removeDP(label); // remove unneccessary decimal places
                painter->drawText(posxp-(QPointF(fm.tightBoundingRect(label).width()/2.0,4)), label);
                break;

            }

            // draw the points we are hovering over
            foreach(SeriesPoint p, hoverpoints) {
                QPointF pos = mapFromScene(host->qchart->mapToPosition(p.xy,p.series));
                QColor invert = GCColor::invertColor(GColor(CPLOTBACKGROUND));
                painter->setBrush(invert);
                painter->setPen(invert);
                QRectF circle(0,0,5*dpiXFactor,5*dpiYFactor);
                circle.moveCenter(pos);
                painter->drawEllipse(circle);
                painter->setBrush(Qt::NoBrush);
            }
        }
        //
        // DROPS THROUGH INTO RECTANGLE FOR SELECTION RECTANGLE
        //
   case RECTANGLE:
        {
            if (mode == RECTANGLE && (state == ACTIVE || state == INACTIVE)) {

                if (host->charttype == GC_CHART_LINE || host->charttype == GC_CHART_SCATTER) {


                    painter->setFont(stGiles);

                    // hovering around - draw label for current position in axis
                    if (hoverpoint != QPointF()) {
                        // draw a circle using marker color
                        QColor invert = GCColor::invertColor(GColor(CPLOTBACKGROUND));
                        painter->setBrush(invert);
                        painter->setPen(invert);
                        QRectF circle(0,0,10*dpiXFactor,10*dpiYFactor);
                        circle.moveCenter(hoverpoint);
                        painter->drawEllipse(circle);
                        painter->setBrush(Qt::NoBrush);

                    }

                    // current position for each series
                    foreach(QAbstractSeries *series, host->qchart->series()) {


                        // convert screen position to value for series
                        QPointF v = host->qchart->mapToValue(spos,series);

                        QPointF posxp = mapFromScene(host->qchart->mapToPosition(QPointF(v.x(),0),series));
                        QPointF posyp = mapFromScene(host->qchart->mapToPosition(QPointF(0, v.y()),series));

                        QPen markerpen(GColor(CPLOTMARKER));
                        painter->setPen(markerpen);
                        painter->setBrush(QBrush(GColor(CPLOTBACKGROUND)));

                        QFontMetrics fm(stGiles); // adjust position to align centre

                        // x value
                        QString label=QString("%1").arg(v.x(),0,'f',0); // no decimal places XXX fixup on series info
                        label = Utils::removeDP(label); // remove unneccessary decimal places
                        painter->drawText(posxp-(QPointF(fm.tightBoundingRect(label).width()/2.0,4)), label);

                        if (series->type() == QAbstractSeries::SeriesTypeScatter) {
                            QPen markerpen(static_cast<QScatterSeries*>(series)->color());
                            painter->setPen(markerpen);
                        } else {
                            QPen markerpen(Qt::gray); // dunno?
                            painter->setPen(markerpen);
                        }

                        // y value
                        label=QString("%1").arg(v.y(),0,'f',0); // no decimal places XXX fixup on series info
                        label = Utils::removeDP(label); // remove unneccessary decimal places
                        painter->drawText(posyp+QPointF(0,fm.tightBoundingRect(label).height()/2.0), label);

                        // tell the legend or whoever else is listening
                        //fprintf(stderr,"cursor (%f,%f) @(%f,%f) for series %s\n", spos.x(), spos.y(),v.x(),v.y(),series->name().toStdString().c_str()); fflush(stderr);
                    }
                }
            }

            if (state != INACTIVE) {

                // there is a rectangle to draw on the screen
                QRectF r=QRectF(4,4,rect.width()-8,rect.height()-8);
                QColor color = GColor(CPLOTMARKER);
                color.setAlphaF(state == ACTIVE ? 0.05 : 0.2); // almost hidden if not moving/sizing
                painter->fillRect(r,QBrush(color));

                // now paint the statistics
                foreach(Calculator calc, stats) {
                    // slope and intercept?
                    if (calc.count<2) continue;

                    // slope calcs way over the top for a line chart
                    // where there are multiple series being plotted
                    if (host->charttype == GC_CHART_SCATTER) {
                        QString lr=QString("y = %1 x + %2").arg(calc.m).arg(calc.b);
                        QPen line(calc.color);
                        painter->setPen(line);
                        painter->drawText(QPointF(0,0), lr);
                    }

                    // slope
                    if (calc.xaxis != NULL) {

                        if (calc.xaxis->type() == QAbstractAxis::AxisTypeValue) { //XXX todo for log date etc?
                            double startx = static_cast<QValueAxis*>(calc.xaxis)->min();
                            double stopx = static_cast<QValueAxis*>(calc.xaxis)->max();
                            QPointF startp = mapFromScene(host->qchart->mapToPosition(QPointF(startx,calc.b),calc.series));
                            QPointF stopp = mapFromScene(host->qchart->mapToPosition(QPointF(stopx,calc.b+(stopx*calc.m)),calc.series));
                            QColor col=GColor(CPLOTMARKER);
                            col.setAlphaF(1);
                            QPen line(col);
                            line.setStyle(Qt::SolidLine);
                            line.setWidthF(0.5 * dpiXFactor);
                            painter->setPen(line);
                            painter->setClipRect(r);
                            painter->drawLine(startp, stopp);
                            painter->setClipRect(mapRectFromScene(host->qchart->plotArea()));
                        }

                        // scene coordinate for min/max (remember we get clipped)
                        QPointF minxp = mapFromScene(host->qchart->mapToPosition(QPointF(calc.x.min,0),calc.series));
                        QPointF maxxp = mapFromScene(host->qchart->mapToPosition(QPointF(calc.x.max,0),calc.series));
                        QPointF minyp = mapFromScene(host->qchart->mapToPosition(QPointF(0, calc.y.min),calc.series));
                        QPointF maxyp = mapFromScene(host->qchart->mapToPosition(QPointF(0, calc.y.max),calc.series));
                        QPointF avgyp = mapFromScene(host->qchart->mapToPosition(QPointF(0, calc.y.mean),calc.series));
                        QPointF avgxp = mapFromScene(host->qchart->mapToPosition(QPointF(calc.x.mean, 0),calc.series));

                        QColor linecol=GColor(CPLOTMARKER);
                        linecol.setAlphaF(0.25);
                        QPen gridpen(linecol);
                        gridpen.setStyle(Qt::DashLine);
                        gridpen.setWidthF(1 *dpiXFactor);
                        painter->setPen(gridpen);

#if 0 // way too busy on the chart
                        QPointF minxpinf = mapFromScene(host->qchart->mapToPosition(QPointF(calc.x.min,calc.y.min),calc.series));
                        QPointF maxxpinf = mapFromScene(host->qchart->mapToPosition(QPointF(calc.x.max,calc.y.max),calc.series));
                        QPointF minypinf = mapFromScene(host->qchart->mapToPosition(QPointF(calc.x.min, calc.y.min),calc.series));
                        QPointF maxypinf = mapFromScene(host->qchart->mapToPosition(QPointF(calc.x.max, calc.y.max),calc.series));
                        QPointF avgmid = mapFromScene(host->qchart->mapToPosition(QPointF(calc.x.mean, calc.y.mean),calc.series));

                        // min/max guides
                        painter->drawLine(minxp,minxpinf);
                        painter->drawLine(maxxp,maxxpinf);
                        painter->drawLine(minyp,minypinf);
                        painter->drawLine(maxyp,maxypinf);

                        linecol = QColor(Qt::red);
                        linecol.setAlphaF(0.25);
                        gridpen = QColor(linecol);
                        gridpen.setStyle(Qt::DashLine);
                        gridpen.setWidthF(1 *dpiXFactor);
                        painter->setPen(gridpen);

                        // avg guides
                        painter->drawLine(avgxp,avgmid);
                        painter->drawLine(avgyp,avgmid);
#endif

                        // min max texts
                        QFont stGiles; // hoho - Chart Font St. Giles ... ok you have to be British to get this joke
                        stGiles.fromString(appsettings->value(NULL, GC_FONT_CHARTLABELS, QFont().toString()).toString());
                        stGiles.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());
                        painter->setFont(stGiles);

                        QPen markerpen(GColor(CPLOTMARKER));
                        painter->setPen(markerpen);
                        QString label=QString("%1").arg(calc.x.max);
                        painter->drawText(maxxp-QPointF(0,4), label);
                        label=QString("%1").arg(calc.x.min);
                        painter->drawText(minxp-QPointF(0,4), label);
                        label=QString("%1").arg(calc.x.mean);
                        painter->drawText(avgxp-QPointF(0,4), label);

                        if (host->charttype == GC_CHART_SCATTER) {

                            // too noisy on a line chart, where there are
                            // typically multiple data series being plotted
                            // arguably this is too busy for a scatter chart too...
                            markerpen = QPen(calc.color);
                            painter->setPen(markerpen);
                            label=QString("%1").arg(calc.y.max);
                            painter->drawText(maxyp, label);
                            label=QString("%1").arg(calc.y.min);
                            painter->drawText(minyp, label);
                            label=QString("%1").arg(calc.y.mean);
                            painter->drawText(avgyp, label);
                        }
                    }

                }
            }
        }
        break;

   case LASSOO:
        {

        }
        break;
   }
   painter->restore();
}

// rect is updated during selection etc in the event handler
QRectF SelectionTool::boundingRect() const { return rect; }

// trap events and redirect to plot event handler
bool SelectionTool::sceneEventFilter(QGraphicsItem *watched, QEvent *event) { return host->eventHandler(0, watched,event); }
bool GenericPlot::eventFilter(QObject *obj, QEvent *e) { return eventHandler(1, obj, e); }

bool
SelectionTool::reset()
{
    state = INACTIVE;
    start=QPointF(0,0);
    finish=QPointF(0,0);
    rect = QRectF(0,0,0,0);
    hoverpoint = QPointF();
    hoverseries = NULL;
    hoverpoints.clear();
    resetSelections();
    update();
    return true;
}

// handle mouse events in selector
bool
SelectionTool::clicked(QPointF pos)
{
    if (mode == XRANGE || mode == RECTANGLE) {

        if (state==ACTIVE && sceneBoundingRect().contains(pos)) {

            // are we moving?
            state = MOVING;
            start = pos;
            startingpos = this->pos();
            update(rect);
            return true;

        } else {

            // initial sizing - or click hold to drag?
            state = SIZING;
            start = pos;
            finish = QPointF(0,0);

            if (mode == RECTANGLE) {
                rect = QRectF(-5,-5,5,5);
                setPos(start);
            } else if (mode == XRANGE) {
                rect = QRectF(0,0,5,host->qchart->plotArea().height());
                setPos(QPointF(start.x(),host->qchart->plotArea().y()));
            }

            // time 400ms to drag - after lots of playing
            // this seems a reasonable time period that isnt
            // too long but doesn't conflict with straight
            // forward click to select.
            // its probably no coincidence that this is also
            // the Doherty Threshold for UX
            drag.setInterval(400);
            drag.setSingleShot(true);
            drag.start();

            update(rect);
            return true;

        }
    }
    return false;
}

bool
SelectionTool::released(QPointF)
{
    if (mode == RECTANGLE || mode == XRANGE) {

        // width and heights can be negative if dragged in reverse
        if (state == DRAGGING) {

            host->setCursor(Qt::ArrowCursor);
            state = INACTIVE;
            rect = QRectF(0,0,0,0);
            return true;

        } else if (rect.width() < 10 && rect.width() > -10 && (mode == XRANGE || (rect.height() < 10 && rect.height() > -10))) {

            // tiny, as in click release - deactivate
            reset();
            return true;

        } else if (state == SIZING || state == MOVING) {

            // finishing move/resize
            state = ACTIVE;
            update(rect);
            return true;
        }
    }
    return false;
}

void
SelectionTool::dragStart()
{
    if (mode == RECTANGLE || mode == XRANGE) {
        // check still right state for it?
        if (state == SIZING) {
            fprintf(stderr, "drag mode!\n"); fflush(stderr);
            host->setCursor(Qt::ClosedHandCursor);
            state = DRAGGING;
        }
    }
}

// for std::lower_bound search of x QPointF value
struct CompareQPointFX {
    bool operator()(const QPointF p1, const QPointF p2) {
        return p1.x() < p2.x();
    }
};

bool
SelectionTool::moved(QPointF pos)
{
    // user hovers over points, but can select using a rectangle
    if (state == SIZING) {

        // cancel the timer to trigger drag
        drag.stop();

        // work out where we got to
        finish = pos;

        // reshape - rect might have negative sizes if sized backwards
        if (mode == RECTANGLE) rect = QRectF(QPointF(0,0), finish-start);
        else rect.setWidth (finish.x()-start.x());

        update(rect);
        return true;

    } else if (state == MOVING) {

        QPointF delta = pos - start;
        if (mode == RECTANGLE) setPos(this->startingpos + delta);
        else if (mode == XRANGE) setPos(this->startingpos + QPointF(delta.x(),0));
        update(rect);
        return true;

    } else if (state == DRAGGING) {

        //QPointF delta = pos - start;
        // move axis to reflect new pos...
        return true;

    } else {

        // HOVERING around.. the big one...

        if (mode == RECTANGLE) {
            // remember screen pos of cursor for tracking values
            // when painting on the axis/plot area
            spos = pos;

            // not moving or sizing so just hovering
            // look for nearest point for each series
            // this needs to be super quick as mouse
            // movements are very fast, so we use a
            // quadtree to find the nearest points
            QPointF hoverv; // value                    // series x,y co-ord used in signal (and legend later)
            hoverpoint = QPointF(); // screen coordinates
            QAbstractSeries *originalhoverseries = hoverseries;
            hoverseries = NULL;
            foreach(QAbstractSeries *series, host->qchart->series()) {

                Quadtree *tree= host->quadtrees.value(series,NULL);
                if (tree != NULL) {

                    // lets convert cursor pos to value pos to find nearest
                    double pixels = 10 * dpiXFactor; // within 10 pixels
                    QRectF srect(pos-QPointF(pixels,pixels), pos+QPointF(pixels,pixels));
                    QRectF vrect(host->qchart->mapToValue(srect.topLeft(),series), host->qchart->mapToValue(srect.bottomRight(),series));
                    //QPointF vpos = host->qchart->mapToValue(pos, series);

                    // find candidates all close by using paint co-ords
                    QList<QPointF> tohere;
                    tree->candidates(vrect, tohere);

                    QPointF cursorpos=mapFromScene(pos);
                    foreach(QPointF p, tohere) {
                        QPointF scpos = mapFromScene(host->qchart->mapToPosition(p, series));
                        if (hoverpoint == QPointF()) {
                            hoverpoint = scpos;
                            hoverseries = series;
                            hoverv = p;
                        } else if ((cursorpos-scpos).manhattanLength() < (cursorpos-hoverpoint).manhattanLength()) {
                            hoverpoint=scpos; // not happy with this XXX needs more work
                            hoverseries = series;
                            hoverv = p;
                        }
                    }

                    //if (tohere.count())  fprintf(stderr, "HOVER %d candidates nearby\n", tohere.count()); fflush(stderr);
                }

            }

            // hoverpoint changed - either a new series selected, a new point, or no point at all
            if (originalhoverseries != hoverseries || hoverv != QPointF()) {
                if (hoverseries != originalhoverseries && originalhoverseries != NULL) emit (unhover(originalhoverseries->name())); // old hover changed
                if (hoverseries != NULL)  emit hover(hoverv, hoverseries->name(), hoverseries); // new hover changed
            }

            // we need to clear x-axis if we aren't hovering on anything at all
            if (hoverv == QPointF()) {
                emit unhoverx();
            }

            // for mouse moves..
            update(rect);
            return true;

        } else if (mode == XRANGE) {

            // user hovers with a vertical line, but can select a range on the x axis
            // lets get x axis value (any old series will do as they should have a common x axis
            spos = pos;
            QMap<QAbstractSeries*,QPointF> vals; // what values were found
            double nearestx=-9999;
            foreach(QAbstractSeries *series, host->qchart->series()) {

                // get x value to search
                double xvalue=host->qchart->mapToValue(spos,series).x();

                // pointsVector
                if (series->type() == QAbstractSeries::SeriesTypeLine) {

                    // we take a copy, would love to avoid this.
                    QVector<QPointF> p = static_cast<QLineSeries*>(series)->pointsVector();

                    // value we want
                    QPointF x= QPointF(xvalue,0);

                    // lower_bound to value near x
                    QVector<QPointF>::const_iterator i = std::lower_bound(p.begin(), p.end(), x, CompareQPointFX());

                    // collect them away
                    vals.insert(series, QPointF(*i));

                    // nearest x?
                    if (i->x() != 0 && (nearestx == -9999 || (i->x()-xvalue) < (nearestx-xvalue))) nearestx = i->x();
                }

            }

            // run over what we found, updating paint points and signal (for legend)
            hoverpoints.clear();
            QMapIterator<QAbstractSeries*, QPointF> i(vals);
            while (i.hasNext()) {
                i.next();
                if (i.value().x() == nearestx) {
                    SeriesPoint add;
                    add.series = i.key();
                    add.xy = i.value();
                    emit hover(i.value(), i.key()->name(), i.key());
                    if (add.xy.y()) hoverpoints << add; // ignore zeroes
                } else emit unhover(i.key()->name());
            }
            if (vals.count()) return true;
        }
    }
    return false;
}

bool
SelectionTool::wheel(int delta)
{

    if (mode == RECTANGLE) {
        // mouse wheel resizes selection rect if it is active
        if (state == ACTIVE) {
            if (delta < 0) {
                rect.setSize(rect.size() * 0.9);
            } else {
                rect.setSize(rect.size() * 1.1);
            }
            return true;
        }
    }
    return false;
}

double
SelectionTool::miny(QAbstractSeries*series)
{
    return host->qchart->mapToValue(pos()+QPointF(0,rect.height()), series).y();
}

double
SelectionTool::maxy(QAbstractSeries*series)
{
    return host->qchart->mapToValue(pos(), series).y();
}

double
SelectionTool::minx(QAbstractSeries*series)
{
    return host->qchart->mapToValue(pos(), series).x();
}

double
SelectionTool::maxx(QAbstractSeries*series)
{
    return host->qchart->mapToValue(pos()+QPointF(rect.width(),0), series).x();
}

void
Calculator::initialise()
{
    count=0;
    m = b = sumxy = sumx2 =
    x.max = x.min = x.sum = x.mean =
    y.max = y.min = y.sum = y.mean = 0;

}

void
Calculator::addPoint(QPointF point)
{

    // max min set from values we've seen
    if (count >0) {
        if (point.x() < x.min) x.min = point.x();
        if (point.x() > x.max) x.max = point.x();
        if (point.y() < y.min) y.min = point.y();
        if (point.y() > y.max) y.max = point.y();
    } else {
        // use only value we've seen
        x.min = x.max = point.x();
        y.min = y.max = point.y();
    }

    count ++;
    x.sum += point.x();
    y.sum += point.y();
    x.mean = x.sum / double(count);
    y.mean = y.sum / double(count);
    sumx2 += point.x() * point.x();
    sumxy += point.x() * point.y();
}

void
Calculator::finalise()
{
    // add calcs for stuff cannot do on the fly
    if (count >=2) {
        m = (count * sumxy - x.sum * y.sum) / (count * sumx2 - (x.sum * x.sum));
        b = (y.sum - m * x.sum) / count;
    }
}

// source 0=scene, 1=widget
bool
GenericPlot::eventHandler(int, void *, QEvent *e)
{
    static bool block=false;

    // don't want to get interuppted
    if (block) return false;
    else block=true;

    // lets get some basic info first
    // where is the cursor?
    //QPoint wpos=cursor().pos();
    QPointF spos=QPointF();

    // so we want to trigger a scene update?
    bool updatescene = false;

    //
    // HANDLE EVENTS AND UPDATE STATE
    //
    switch(e->type()) {

    // mouse clicked
    case QEvent::GraphicsSceneMousePress:
        spos = static_cast<QGraphicsSceneMouseEvent*>(e)->scenePos();
    //case QEvent::MouseButtonPress:
        //fprintf(stderr,"POS: %g:%g \n", spos.x(), spos.y());
        //fprintf(stderr,"%s: mouse PRESSED for obj=%u\n", source ? "widget" : "scene", (void*)obj); fflush(stderr);
        {
            updatescene = selector->clicked(spos);
        }
    break;

    // mouse released
    case QEvent::GraphicsSceneMouseRelease:
        spos = static_cast<QGraphicsSceneMouseEvent*>(e)->scenePos();
    //case QEvent::MouseButtonRelease:
        //fprintf(stderr,"POS: %g:%g | %d:%d\n", spos.x(), spos.y(), wpos.x(), wpos.y());
        //fprintf(stderr,"%s: mouse RELEASED for obj=%u\n", source ? "widget" : "scene", (void*)obj); fflush(stderr);
        {
            updatescene = selector->released(spos);
        }
    break;

    // mouse move
    case QEvent::GraphicsSceneMouseMove:
        spos = static_cast<QGraphicsSceneMouseEvent*>(e)->scenePos();
    //case QEvent::MouseMove:
        //fprintf(stderr,"POS: %f:%f\n", spos.x(), spos.y());
        //fprintf(stderr,"%s: mouse MOVE for obj=%u\n", source ? "widget" : "scene", (void*)obj); fflush(stderr);
        {
            // see if selection tool cares about new mouse position
            updatescene = selector->moved(spos);
        }
    break;

    case QEvent::GraphicsSceneWheel:
        {
            QGraphicsSceneWheelEvent *w = static_cast<QGraphicsSceneWheelEvent*>(e);
            //fprintf(stderr,"%s: mouse WHEEL [%d] for obj=%u\n", source ? "widget" : "scene", w->delta(),(void*)obj); fflush(stderr);
            updatescene = selector->wheel(w->delta());
        }
        break;

    // resize
    case QEvent::Resize: {
        //canvas->fitInView(canvas->sceneRect(), Qt::KeepAspectRatio);
    }
    break;

    // tooltip, paused for a moment..
    case QEvent::GraphicsSceneHoverEnter:
    case QEvent::GraphicsSceneHelp:
    case QEvent::GraphicsSceneHoverMove: {

        // debug info
        //fprintf(stderr,"%s: HOVER scene item for obj=%u\n", source ? "widget" : "scene", (void*)obj); fflush(stderr);
    }
    break;

    // tooltip, paused for a moment..
    case QEvent::GraphicsSceneHoverLeave: {

        // debug info
        //fprintf(stderr,"%s: UNHOVER scene item for obj=%u\n", source ? "widget" : "scene", (void*)obj); fflush(stderr);
    }
    break;

    // tooltip, paused for a moment..
    case QEvent::ToolTip: {

        // debug info
        //fprintf(stderr,"%s: tooltip for obj=%u\n", source ? "widget" : "scene", (void*)obj); fflush(stderr);
    }
    break;


    default:
        //fprintf(stderr,"%s: some event %d for obj=%u\n", source ? "widget" : "scene", e->type(), (void*)obj); fflush(stderr);
        break;
    }

    //
    // UPDATE SCENE TO REFLECT STATE
    //
    if (updatescene) {

        selector->updateScene(); // really only do selection right now.. more to come

        // repaint everything
        chartview->scene()->update(0,0,chartview->scene()->width(),chartview->scene()->height());
    }

    // all done.
    block = false;
    return false;
}

// selector needs to update the chart for selections
void
SelectionTool::updateScene()
{
    // is the selection active?
    if (state != SelectionTool::INACTIVE) {

        // selection tool is active so set curves gray
        // and create curves for highlighted points etc
        QList<QAbstractSeries*> originallist=host->qchart->series();
        foreach(QAbstractSeries *x, originallist) { // because we update it below (!)

            if (ignore.contains(x)) continue;

            // Run through all the curves, setting them gray
            // selecting the points that fall into the selection
            // and creating selection curves and stats for painting
            // onto the plot
            //
            // We duplicate code by curve type (but not chart type)
            // so we can e.g. put a scatter curve on a line chart
            // and vice versa later.

            switch(x->type()) {
            case QAbstractSeries::SeriesTypeLine: {
                QLineSeries *line = static_cast<QLineSeries*>(x);

                // ignore empty series
                if (line->count() < 1) continue;

                // this will be used to plot selected points on the plot
                QLineSeries *selection =NULL;

                // the axes for the current series
                QAbstractAxis *xaxis=NULL, *yaxis=NULL;
                foreach (QAbstractAxis *ax, x->attachedAxes()) {
                    if (ax->orientation() == Qt::Vertical && yaxis==NULL) yaxis=ax;
                    if (ax->orientation() == Qt::Horizontal && xaxis==NULL) xaxis=ax;
                }

                if ((selection=static_cast<QLineSeries*>(selections.value(x, NULL))) == NULL) {

                    selection = new QLineSeries();

                    // all of this curve cloning should be in a new method xxx todo
                    selection->setUseOpenGL(line->useOpenGL());
                    selection->setPen(line->pen());
                    if (line->useOpenGL())
                        selection->setColor(Qt::gray); // use opengl ignores changing colors
                    else {
                        selection->setColor(line->color());
                        static_cast<QLineSeries*>(x)->setColor(Qt::gray);
                    }
                    selections.insert(x, selection);
                    ignore.append(selection);

                    // add after done all aesthetic for opengl snafus
                    host->qchart->addSeries(selection); // before adding data and axis

                    // only do when creating it.
                    if (yaxis) selection->attachAxis(yaxis);
                    if (xaxis) selection->attachAxis(xaxis);
                }

                // lets work out what range of values we need to be
                // selecting is, reverse since possible to have a backwards
                // rectangle in the selection tool
                double minx=0,maxx=0;
                minx =this->minx(x);
                maxx =this->maxx(x);
                if (maxx < minx) { double t=minx; minx=maxx; maxx=t; }

                //fprintf(stderr, "xaxis range %f-%f, yaxis range %f-%f, [%s] %d points to check\n", minx,maxx,miny,maxy,scatter->name().toStdString().c_str(), scatter->count());

                // add points to the selection curve and calculate as you go
                QList<QPointF> points;
                Calculator calc;
                calc.initialise();
                calc.color = selection->color(); // should this go into constructor?! xxx todo
                calc.xaxis = xaxis;
                calc.yaxis = yaxis;
                calc.series = line;
                for(int i=0; i<line->count(); i++) {
                    QPointF point = line->at(i); // avoid deep copy
                    if (point.x() >= minx && point.x() <= maxx) {
                        if (!points.contains(point)) points << point; // avoid dupes
                        calc.addPoint(point);
                    }
                }
                calc.finalise();
                stats.insert(line, calc);

                selection->clear();
                if (points.count()) selection->append(points);

            }
            break;
            case QAbstractSeries::SeriesTypeScatter: {

                QScatterSeries *scatter = static_cast<QScatterSeries*>(x);

                // ignore empty series
                if (scatter->count() < 1) continue;

                // this will be used to plot selected points on the plot
                QScatterSeries *selection =NULL;

                // the axes for the current series
                QAbstractAxis *xaxis=NULL, *yaxis=NULL;
                foreach (QAbstractAxis *ax, x->attachedAxes()) {
                    if (ax->orientation() == Qt::Vertical && yaxis==NULL) yaxis=ax;
                    if (ax->orientation() == Qt::Horizontal && xaxis==NULL) xaxis=ax;
                }

                if ((selection=static_cast<QScatterSeries*>(selections.value(x, NULL))) == NULL) {

                    selection = new QScatterSeries();

                    // all of this curve cloning should be in a new method xxx todo
                    host->qchart->addSeries(selection); // before adding data and axis
                    selection->setUseOpenGL(scatter->useOpenGL());
                    if (selection->useOpenGL())
                        selection->setColor(Qt::gray); // use opengl ignores changing colors
                    else {
                        selection->setColor(scatter->color());
                        static_cast<QScatterSeries*>(x)->setColor(Qt::gray);
                    }
                    selection->setMarkerSize(scatter->markerSize());
                    selection->setMarkerShape(scatter->markerShape());
                    selection->setPen(scatter->pen());
                    selections.insert(x, selection);
                    ignore.append(selection);

                    // only do when creating it.
                    if (yaxis) selection->attachAxis(yaxis);
                    if (xaxis) selection->attachAxis(xaxis);
                }

                // lets work out what range of values we need to be
                // selecting is, reverse since possible to have a backwards
                // rectangle in the selection tool
                double miny=0,maxy=0,minx=0,maxx=0;
                miny =this->miny(x);
                maxy =this->maxy(x);
                if (maxy < miny) { double t=miny; miny=maxy; maxy=t; }

                minx =this->minx(x);
                maxx =this->maxx(x);
                if (maxx < minx) { double t=minx; minx=maxx; maxx=t; }

                //fprintf(stderr, "xaxis range %f-%f, yaxis range %f-%f, [%s] %d points to check\n", minx,maxx,miny,maxy,scatter->name().toStdString().c_str(), scatter->count());

                // add points to the selection curve and calculate as you go
                QList<QPointF> points;
                Calculator calc;
                calc.initialise();
                calc.color = selection->color(); // should this go into constructor?! xxx todo
                calc.xaxis = xaxis;
                calc.yaxis = yaxis;
                calc.series = scatter;
                for(int i=0; i<scatter->count(); i++) {
                    QPointF point = scatter->at(i); // avoid deep copy
                    if (point.y() >= miny && point.y() <= maxy &&
                        point.x() >= minx && point.x() <= maxx) {
                        if (!points.contains(point)) points << point; // avoid dupes
                        calc.addPoint(point);
                    }
                }
                calc.finalise();
                stats.insert(scatter, calc);

                selection->clear();
                if (points.count()) selection->append(points);
            }
            break;
            default:
                break;
            }
        }

    } else {

        resetSelections();
    }
 }

 void
 SelectionTool::resetSelections()
 {
    // selection tool isn't active so reset curves to original
    if (selections.count()) {

        foreach(QAbstractSeries *x, host->qchart->series()) {

            if (ignore.contains(x)) continue;

            switch(x->type()) {

            case QAbstractSeries::SeriesTypeLine:
            {
                QLineSeries *selection=NULL;
                if ((selection=static_cast<QLineSeries*>(selections.value(x,NULL))) != NULL) {

                    // set greyed out original back to its proper color
                    // rememember when opengl is in use setColor is ignored
                    // so we didn't change it on selection, so no need to
                    // set it back to original in that case
                    if (!selection->useOpenGL())
                        static_cast<QLineSeries*>(x)->setColor(static_cast<QLineSeries*>(selection)->color());

                    // clear points, remove from axis and remove from chart
                    selection->clear();
                    foreach(QAbstractAxis *ax, selection->attachedAxes()) selection->detachAxis(ax);
                    host->qchart->removeSeries(selection);
                    delete selection;
                }
            }
            break;
            case QAbstractSeries::SeriesTypeScatter:
            {
                QScatterSeries *selection=NULL;
                if ((selection=static_cast<QScatterSeries*>(selections.value(x,NULL))) != NULL) {

                    // set greyed out original back to its proper color
                    // rememember when opengl is in use setColor is ignored
                    // so we didn't change it on selection, so no need to
                    // set it back to original in that case
                    if (!selection->useOpenGL())
                        static_cast<QScatterSeries*>(x)->setColor(static_cast<QScatterSeries*>(selection)->color());

                    // clear points, remove from axis and remove from chart
                    selection->clear();
                    foreach(QAbstractAxis *ax, selection->attachedAxes()) selection->detachAxis(ax);
                    host->qchart->removeSeries(selection);
                    delete selection;
                }
            }
            break;
            default: break;
            }
        }
        selections.clear();
    }
    ignore.clear();
    stats.clear();
}

void
GenericPlot::configChanged(qint32)
{
    // tinted palette for headings etc
    QPalette palette;
    palette.setBrush(QPalette::Window, QBrush(GColor(CPLOTBACKGROUND)));
    palette.setColor(QPalette::WindowText, GColor(CPLOTMARKER));
    palette.setColor(QPalette::Text, GColor(CPLOTMARKER));
    palette.setColor(QPalette::Base, GCColor::alternateColor(GColor(CPLOTBACKGROUND)));
    setPalette(palette);

    // chart colors
    chartview->setBackgroundBrush(QBrush(GColor(CPLOTBACKGROUND)));
    qchart->setBackgroundBrush(QBrush(GColor(CPLOTBACKGROUND)));
    qchart->setBackgroundPen(QPen(GColor(CPLOTMARKER)));
}

double
GenericPlot::min(QAbstractAxis *ax)
{
    if (ax == NULL) return 0;
    else {
        switch (ax->type()) {
            case QAbstractAxis::AxisTypeValue:
                return static_cast<QValueAxis*>(ax)->min();
                break;
        default: return 0;
        }
        return 0;
    }
}

double
GenericPlot::max(QAbstractAxis *ax)
{
    if (ax == NULL) return 0;
    else {
        switch (ax->type()) {
            case QAbstractAxis::AxisTypeValue:
                return static_cast<QValueAxis*>(ax)->max();
                break;
            default: break;
        }
        return 0;
    }
}

bool
GenericPlot::initialiseChart(QString title, int type, bool animate)
{
    // if we changed the type, all series must go
    if (charttype != type) {
        qchart->removeAllSeries();
        curves.clear();
        barseries=NULL;
    }

    foreach(Quadtree *tree, quadtrees) delete tree;
    quadtrees.clear();

    foreach(AxisInfo *axisinfo, axisinfos) delete axisinfo;
    axisinfos.clear();

    left=true;
    bottom=true;
    barsets.clear();

    // reset selections etc
    selector->reset();

    // remember type
    charttype=type;

    // title is allowed to be blank
    qchart->setTitle(title);

    // would avoid animations as they get very tiresome and are not
    // generally transition animations, so add very little value
    // by default they are disabled anyway
    qchart->setAnimationOptions(animate ? QChart::SeriesAnimations : QChart::NoAnimation);

    // what kind of selector do we use?
    if (charttype==GC_CHART_LINE) selector->setMode(SelectionTool::XRANGE);
    else selector->setMode(SelectionTool::RECTANGLE);

    return true;
}

// rendering to qt chart
bool
GenericPlot::addCurve(QString name, QVector<double> xseries, QVector<double> yseries, QString xname, QString yname,
                      QStringList labels, QStringList colors,
                      int /*line TODO*/, int /*symbol TODO */, int size, QString color, int opacity, bool opengl)
{
    // if curve already exists, remove it
    if (charttype==GC_CHART_LINE || charttype==GC_CHART_SCATTER || charttype==GC_CHART_PIE) {
        QAbstractSeries *existing = curves.value(name);
        if (existing) {
            qchart->removeSeries(existing);
            delete existing; // XXX is this such a great idea.. causes a lot of flicker...
        }
    }

    // lets find that axis - even blank ones
    AxisInfo *xaxis, *yaxis;
    xaxis=axisinfos.value(xname);
    yaxis=axisinfos.value(yname);
    if (xaxis==NULL) {
        xaxis=new AxisInfo(Qt::Horizontal, xname);

        // default alignment toggles
        xaxis->align = bottom ? Qt::AlignBottom : Qt::AlignTop;
        bottom = !bottom;

        // use marker color for x axes
        xaxis->labelcolor = xaxis->axiscolor = GColor(CPLOTMARKER);

        // add to list
        axisinfos.insert(xname, xaxis);
    }
    if (yaxis==NULL) {
        yaxis=new AxisInfo(Qt::Vertical, yname);

        // default alignment toggles
        yaxis->align = left ? Qt::AlignLeft : Qt::AlignRight;
        left = !left;

        // yaxis color matches, but not done for xaxis above
        yaxis->labelcolor = yaxis->axiscolor = QColor(color);

        // add to list
        axisinfos.insert(yname, yaxis);
    }

    switch (charttype) {
    default:

    case GC_CHART_LINE:
        {
            // set up the curves
            QLineSeries *add = new QLineSeries();
            add->setName(name);

            // aesthetics
            add->setBrush(Qt::NoBrush);
            QPen pen(color);
            pen.setStyle(Qt::SolidLine);
            pen.setWidth(size);
            add->setPen(pen);
            add->setOpacity(double(opacity) / 100.0); // 0-100% to 0.0-1.0 values

            // data
            for (int i=0; i<xseries.size() && i<yseries.size(); i++) {
                add->append(xseries.at(i), yseries.at(i));

                // tell axis about the data
                xaxis->point(xseries.at(i), yseries.at(i));
                yaxis->point(xseries.at(i), yseries.at(i));
            }

            // hardware support?
            chartview->setRenderHint(QPainter::Antialiasing);
            add->setUseOpenGL(opengl); // for scatter or line only apparently

            // chart
            qchart->addSeries(add);

            // add to list of curves
            curves.insert(name,add);
            xaxis->series.append(add);
            yaxis->series.append(add);
        }
        break;

    case GC_CHART_SCATTER:
        {
            // set up the curves
            QScatterSeries *add = new QScatterSeries();
            add->setName(name);

            // aesthetics
            add->setMarkerShape(QScatterSeries::MarkerShapeCircle); //TODO: use 'symbol'
            add->setMarkerSize(size);
            QColor col=QColor(color);
            add->setBrush(QBrush(col));
            add->setPen(Qt::NoPen);
            add->setOpacity(double(opacity) / 100.0); // 0-100% to 0.0-1.0 values

            // data
            Calculator calc; // watching as we add
            for (int i=0; i<xseries.size() && i<yseries.size(); i++) {
                add->append(xseries.at(i), yseries.at(i));

                // tell axis about the data
                xaxis->point(xseries.at(i), yseries.at(i));
                yaxis->point(xseries.at(i), yseries.at(i));

                // calculate stuff
                calc.addPoint(QPointF(xseries.at(i), yseries.at(i)));

            }

            // set the quadtree up - now we know the ranges...
            Quadtree *tree = new Quadtree(QPointF(calc.x.min, calc.y.min), QPointF(calc.x.max, calc.y.max));
            for (int i=0; i<xseries.size() && i<yseries.size(); i++)
                if (xseries.at(i) != 0 && yseries.at(i) != 0) // 0,0 is common and lets ignore (usually means no data)
                    tree->insert(QPointF(xseries.at(i), yseries.at(i)));

            if (tree->nodes.count()) quadtrees.insert(add, tree);

            // hardware support?
            chartview->setRenderHint(QPainter::Antialiasing);
            add->setUseOpenGL(opengl); // for scatter or line only apparently

            // chart
            qchart->addSeries(add);
            qchart->legend()->setMarkerShape(QLegend::MarkerShapeRectangle);
            qchart->setDropShadowEnabled(false);

            // add to list of curves
            curves.insert(name,add);
            xaxis->series.append(add);
            yaxis->series.append(add);

        }
        break;

    case GC_CHART_BAR:
        {
            // set up the barsets
            QBarSet *add= new QBarSet(name);

            // aesthetics
            add->setBrush(QBrush(QColor(color)));
            add->setPen(Qt::NoPen);

            // data and min/max values
            for (int i=0; i<yseries.size(); i++) {
                double value = yseries.at(i);
                *add << value;
                yaxis->point(i,value);
                xaxis->point(i,value);
            }

            // we are very particular regarding axis
            yaxis->type = AxisInfo::CONTINUOUS;
            xaxis->type = AxisInfo::CATEGORY;

            // add to list of barsets
            barsets << add;
        }
        break;

    case GC_CHART_PIE:
        {
            // set up the curves
            QPieSeries *add = new QPieSeries();

            // setup the slices
            for(int i=0; i<yseries.size(); i++) {
                // get label?
                if (i>=labels.size())
                    add->append(QString("%1").arg(i), yseries.at(i));
                else
                    add->append(labels.at(i), yseries.at(i));
            }

            // now do the colors
            int i=0;
            foreach(QPieSlice *slice, add->slices()) {

                slice->setExploded();
                slice->setLabelVisible();
                slice->setPen(Qt::NoPen);
                if (i <colors.size()) slice->setBrush(QColor(colors.at(i)));
                else slice->setBrush(Qt::red);
                i++;
            }

            // set the pie chart
            qchart->addSeries(add);

            // add to list of curves
            curves.insert(name,add);
        }
        break;

    }
    return true;
}

// once python script has run polish the chart, fixup axes/ranges and so on.
void
GenericPlot::finaliseChart()
{
    if (!qchart) return;

    // clear ALL axes
    foreach(QAbstractAxis *axis, qchart->axes(Qt::Vertical)) {
        qchart->removeAxis(axis);
        delete axis;
    }
    foreach(QAbstractAxis *axis, qchart->axes(Qt::Horizontal)) {
        qchart->removeAxis(axis);
        delete axis;
    }

    // clear the legend
    legend->removeAllSeries();

    // basic aesthetics
    qchart->legend()->setMarkerShape(QLegend::MarkerShapeRectangle);
    qchart->setDropShadowEnabled(false);

    // no more than 1 category axis since barsets are all assigned.
    bool donecategory=false;

    // Create axes - for everyone except pie charts that don't have any
    if (charttype != GC_CHART_PIE) {
        // create desired axis
        foreach(AxisInfo *axisinfo, axisinfos) {
//fprintf(stderr, "Axis: %s, orient:%s, type:%d\n",axisinfo->name.toStdString().c_str(),axisinfo->orientation==Qt::Vertical?"vertical":"horizontal",(int)axisinfo->type);
//fflush(stderr);
            QAbstractAxis *add=NULL;
            switch (axisinfo->type) {
            case AxisInfo::DATERANGE: // TODO
            case AxisInfo::TIME:      // TODO
            case AxisInfo::CONTINUOUS:
                {
                    QValueAxis *vaxis= new QValueAxis(qchart);
                    add=vaxis; // gets added later

                    vaxis->setMin(axisinfo->min());
                    vaxis->setMax(axisinfo->max());

                    // attach to the chart
                    qchart->addAxis(add, axisinfo->locate());
                }
                break;
            case AxisInfo::CATEGORY:
                {
                    if (!donecategory) {

                        donecategory=true;

                        QBarCategoryAxis *caxis = new QBarCategoryAxis(qchart);
                        add=caxis;

                        // add the bar series
                        if (!barseries) { barseries = new QBarSeries(); qchart->addSeries(barseries); }
                        else barseries->clear();

                        // add the new barsets
                        foreach (QBarSet *bs, barsets)
                            barseries->append(bs);

                        // attach before addig barseries
                        qchart->addAxis(add, axisinfo->locate());

                        // attach to category axis
                        barseries->attachAxis(caxis);

                        // category labels
                        for(int i=axisinfo->categories.count(); i<=axisinfo->maxx; i++)
                            axisinfo->categories << QString("%1").arg(i+1);
                        caxis->setCategories(axisinfo->categories);
                    }
                }
                break;
            }

            // at this point the basic settngs have been done and the axis
            // is attached to the chart, so we can go ahead and apply common settings
            if (add) {

                // once we've done the basics, lets do the aesthetics
                QFont stGiles; // hoho - Chart Font St. Giles ... ok you have to be British to get this joke
                stGiles.fromString(appsettings->value(this, GC_FONT_CHARTLABELS, QFont().toString()).toString());
                stGiles.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());
                add->setTitleFont(stGiles);
                add->setLabelsFont(stGiles);

                if (axisinfo->name != "x" && axisinfo->name != "y")  // equivalent to being blank
                    add->setTitleText(axisinfo->name);
                add->setLinePenColor(axisinfo->axiscolor);
                if (axisinfo->orientation == Qt::Vertical) // we never have y axis lines
                    add->setLineVisible(false);
                add->setLabelsColor(axisinfo->labelcolor);
                add->setTitleBrush(QBrush(axisinfo->labelcolor));

                // grid lines, just color for now xxx todo: ticks (sigh)
                add->setGridLineColor(GColor(CPLOTGRID));
                if (charttype != GC_CHART_SCATTER && add->orientation()==Qt::Horizontal) // no x grids unless a scatter
                    add->setGridLineVisible(false);

                // add the series that are associated with this
                foreach(QAbstractSeries *series, axisinfo->series)
                    series->attachAxis(add);

            }
        }
    }

    if (charttype == GC_CHART_SCATTER || charttype == GC_CHART_LINE) {

        bool havexaxis=false;
        foreach(QAbstractSeries *series, qchart->series()) {
            // manufactured series to show X axis values
            // pick the first X-axis we find and add it
            // using the axis units
            if (havexaxis == false) {

                // look for first series with a horizontal axis (we will use this later)
                foreach(QAbstractAxis *axis, series->attachedAxes()) {
                    if (axis->orientation() == Qt::Horizontal && axis->type()==QAbstractAxis::AxisTypeValue) {
                        legend->addX(static_cast<QValueAxis*>(axis)->titleText());
                        havexaxis=true;
                        break;
                    }
                }
            }

            // add to the legend xxx might make some hidden?
            legend->addSeries(series->name(), series);

        }

        legend->show();
    }

    if (charttype ==GC_CHART_PIE || charttype== GC_CHART_BAR) { //XXX bar chart legend?
        // pie, never want a legend
        legend->hide();
    }

    // barseries special case
    if (charttype==GC_CHART_BAR && barseries) {

        // need to attach barseries to the value axes
        foreach(QAbstractAxis *axis, qchart->axes(Qt::Vertical))
            barseries->attachAxis(axis);
    }

    // install event filters on thes scene objects for Pie and Bar
    // charts only, since for line/scatter we select and interact via
    // collision detection (and don't want the double number of events).
    if (charttype == GC_CHART_BAR || charttype == GC_CHART_PIE) {

        // largely we just want the hover events coz they're handy
        foreach(QGraphicsItem *item, chartview->scene()->items())
            item->installSceneEventFilter(selector); // XXX create sceneitem to help us here!

    }
}

bool
GenericPlot::configureAxis(QString name, bool visible, int align, double min, double max,
                      int type, QString labelcolor, QString color, bool log, QStringList categories)
{
    AxisInfo *axis = axisinfos.value(name);
    if (axis == NULL) return false;

    // lets update the settings then
    axis->visible = visible;

    // -1 if not passed
    if (align == 0) axis->align = Qt::AlignBottom;
    if (align == 1) axis->align = Qt::AlignLeft;
    if (align == 2) axis->align = Qt::AlignTop;
    if (align == 3) axis->align = Qt::AlignRight;

    // -1 if not passed
    if (min != -1)  axis->minx = axis->miny = min;
    if (max != -1)  axis->maxx = axis->maxy = max;

    // type
    if (type != -1) axis->type = static_cast<AxisInfo::AxisInfoType>(type);

    // color
    if (labelcolor != "") axis->labelcolor=QColor(labelcolor);
    if (color != "") axis->axiscolor=QColor(color);

    // log .. hmmm
    axis->log = log;

    // categories
    if (categories.count()) axis->categories = categories;
    return true;
}

QColor
GenericPlot::seriesColor(QAbstractSeries* series)
{
    switch (series->type()) {
    case QAbstractSeries::SeriesTypeScatter: return static_cast<QScatterSeries*>(series)->color(); break;
    case QAbstractSeries::SeriesTypeLine: return static_cast<QLineSeries*>(series)->color(); break;
    default: return GColor(CPLOTMARKER);
    }
}
