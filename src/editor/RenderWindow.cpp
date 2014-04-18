#include "RenderWindow.hpp"
#include "MainWindow.hpp"

#include <string.h>
#include <QtGui>

namespace Tungsten
{

RenderWindow::RenderWindow(QWidget *proxyParent, MainWindow *parent)
: QWidget(proxyParent),
  _parent(*parent),
  _scene(nullptr),
  _rendering(false),
  _currentSpp(0),
  _zoom(1.0f),
  _panX(0.0f),
  _panY(0.0f),
  _exposure(0.0f),
  _pow2Exposure(1.0f),
  _gamma(2.2f)
{
    connect(this, SIGNAL(rendererFinished()), SLOT(finishRender()), Qt::QueuedConnection);

    new QShortcut(QKeySequence("Space"), this, SLOT(toggleRender()));
    new QShortcut(QKeySequence("+"), this, SLOT(zoomIn()));
    new QShortcut(QKeySequence("-"), this, SLOT(zoomOut()));
    new QShortcut(QKeySequence("F5"), this, SLOT(refresh()));
    new QShortcut(QKeySequence("Home"), this, SLOT(resetView()));
    new QShortcut(QKeySequence("Tab"), this, SLOT(togglePreview()));

    _sppLabel = new QLabel(this);
    _statusLabel = new QLabel(this);

    _sppLabel->setMinimumWidth(100);
}

void RenderWindow::addStatusWidgets(QStatusBar *statusBar)
{
    statusBar->addPermanentWidget(_sppLabel, 0);
    statusBar->addPermanentWidget(_statusLabel, 1);
}

void RenderWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(0, 0, width(), height(), QColor(Qt::darkGray));

    if (!_image)
        return;

    int offsetX =  width()/2 + (_panX - _image-> width()/2)*_zoom;
    int offsetY = height()/2 + (_panY - _image->height()/2)*_zoom;

    painter.translate(QPoint(offsetX, offsetY));
    painter.scale(_zoom, _zoom);

    QRect exposedRect = painter.matrix().inverted()
                 .mapRect(event->rect())
                 .adjusted(-1, -1, 1, 1);
    painter.drawImage(exposedRect, *_image, exposedRect);
}

void RenderWindow::mouseMoveEvent(QMouseEvent *eventMove)
{
    _panX += (eventMove->pos().x() - _lastMousePos.x())/_zoom;
    _panY += (eventMove->pos().y() - _lastMousePos.y())/_zoom;
    _lastMousePos = eventMove->pos();

    update();
}

void RenderWindow::mousePressEvent(QMouseEvent *eventMove)
{
    _lastMousePos = eventMove->pos();
}

void RenderWindow::wheelEvent(QWheelEvent *wheelEvent)
{
    for (int i = 0; i < std::abs(wheelEvent->delta()); ++i)
        wheelEvent->delta() < 0 ? zoomOut() : zoomIn();
}

void RenderWindow::sceneChanged()
{
    _scene = _parent.scene();
    if (_renderer)
        _renderer->abortRender();
    _renderer.reset();
    _flattenedScene.reset();
    _rendering = false;
    _currentSpp = 0;

    if (_scene) {
        Vec2u resolution = _scene->camera()->resolution();
         _buffer.reset(new  Vec3f[resolution.product()]);
        _weights.reset(new uint32[resolution.product()]);
        std::memset(_buffer.get(), 0, sizeof(Vec3f)*resolution.product());
        _image.reset(new QImage(QSize(resolution.x(), resolution.y()), QImage::Format_ARGB32));
        _image->fill(Qt::black);
    }
}

QRgb RenderWindow::tonemap(const Vec3f &c) const
{
    Vec3i pixel(min(std::pow(c*_pow2Exposure, 1.0f/_gamma)*255.0f, Vec3f(255.0f)));

    return qRgb(pixel.x(), pixel.y(), pixel.z());
}

uint32 RenderWindow::sampleStep(uint32 current, uint32 target) const
{
#if EXPORT_RAYS
    return target - current;
#else
    return min(target - current, 4u);
#endif
}

void RenderWindow::updateStatus()
{
    if (_scene) {
        _sppLabel->setText(QString("%1/%2 spp").arg(_currentSpp).arg(_scene->camera()->spp()));
        if (_rendering)
            _statusLabel->setText("Rendering...");
        else
            _statusLabel->setText("Render finished");
    } else {
        _sppLabel->setText("");
        _statusLabel->setText("");
    }
}

void RenderWindow::startRender()
{
    if (!_scene)
        return;

    if (!_flattenedScene)
        _flattenedScene.reset(new PackedGeometry(_scene->flatten()));

    auto pixelCallback = [&](uint32_t x, uint32_t y, const Vec3f& c, uint32 spp) {
        uint32 idx = x + y*_scene->camera()->resolution().x();
        _buffer[idx] += c;
        _weights[idx] += spp;
    };

    auto finishCallback = [&]() {
        emit rendererFinished();
    };

    int threadCount = std::max(QThread::idealThreadCount() - 1, 1);
    if (threadCount == -1)
        threadCount = 7;
#if EXPORT_RAYS
    threadCount = 1;
#endif

    if (!_renderer) {
        _currentSpp = 0;
        _renderer.reset(new Renderer<RenderIntegrator>(*_scene->camera(), *_flattenedScene, _scene->lights()));

        _image->fill(Qt::black);
        std::memset( _buffer.get(), 0, sizeof( Vec3f)*_scene->camera()->resolution().product());
        std::memset(_weights.get(), 0, sizeof(uint32)*_scene->camera()->resolution().product());
    }
    _nextSpp = _currentSpp + sampleStep(_currentSpp, _scene->camera()->spp());
    if (_nextSpp == _currentSpp)
        return;
    _renderer->startRender(pixelCallback, finishCallback, _currentSpp, _nextSpp, threadCount);

    _rendering = true;
    updateStatus();
}

void RenderWindow::abortRender()
{
    _rendering = false;
    if (_renderer) {
        _renderer->abortRender();
        _renderer.reset();
        _flattenedScene.reset();

        refresh();
        updateStatus();
    }
}

void RenderWindow::finishRender()
{
    if (_renderer)
        _renderer->waitForCompletion();
    _currentSpp = _nextSpp;
    _rendering = false;
    refresh();

    if (_scene && _currentSpp < _scene->camera()->spp())
        startRender();
    else {
        if (_scene) {
            std::string dst = FileUtils::addSlash(FileUtils::extractDir(_scene->path())) + _scene->camera()->outputFile();
            _image->save(QString::fromStdString(dst), "PNG", 100);
        }
        _renderer.reset();
        _flattenedScene.reset();

        updateStatus();
    }
}

void RenderWindow::refresh()
{
    if (!_image)
        return;

    uint32 w = _image->width(), h = _image->height();
    QRgb *pixels = reinterpret_cast<QRgb *>(_image->bits());

    for (uint32 y = 0, idx = 0; y < h; ++y)
        for (uint32 x = 0; x < w; ++x, ++idx)
            pixels[idx] = tonemap(_buffer[idx]/_weights[idx]);

    update();
}

void RenderWindow::toggleRender()
{
    if (_rendering)
        abortRender();
    else
        startRender();
}

void RenderWindow::zoomIn()
{
    _zoom = clamp(_zoom*1.25f, 0.05f, 20.0f);
    update();
}

void RenderWindow::zoomOut()
{
    _zoom = clamp(_zoom*0.75f, 0.05f, 20.0f);
    update();
}

void RenderWindow::resetView()
{
    _zoom = 1.0f;
    _panX = _panY = 0.0f;
    update();
}

void RenderWindow::togglePreview()
{
    if (!_rendering)
        _parent.togglePreview();
}

}
