#pragma once

#include <QIcon>
#include <QColor>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>

class VectorIcons {
public:
    static QIcon iconMic(const QColor &color = QColor("#cccccc"), int size = 24)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(QBrush(color));

        // Mic capsule
        p.drawRoundedRect(QRectF(size * 0.35, size * 0.15, size * 0.30, size * 0.45), size * 0.15, size * 0.15);

        // Mic arc
        p.setBrush(Qt::NoBrush);
        QRectF arcRect(size * 0.22, size * 0.25, size * 0.56, size * 0.42);
        p.drawArc(arcRect, 0, -180 * 16);

        // Stand base
        p.drawLine(QPointF(size * 0.5, size * 0.67), QPointF(size * 0.5, size * 0.84));
        p.drawLine(QPointF(size * 0.32, size * 0.84), QPointF(size * 0.68, size * 0.84));

        return QIcon(pixmap);
    }

    static QIcon iconMicMuted(const QColor &color = QColor("#d4883b"), int size = 24)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(QBrush(color));

        // Mic capsule
        p.drawRoundedRect(QRectF(size * 0.35, size * 0.15, size * 0.30, size * 0.45), size * 0.15, size * 0.15);

        // Mic arc
        p.setBrush(Qt::NoBrush);
        QRectF arcRect(size * 0.22, size * 0.25, size * 0.56, size * 0.42);
        p.drawArc(arcRect, 0, -180 * 16);

        // Stand base
        p.drawLine(QPointF(size * 0.5, size * 0.67), QPointF(size * 0.5, size * 0.84));
        p.drawLine(QPointF(size * 0.32, size * 0.84), QPointF(size * 0.68, size * 0.84));

        // Diagonal slash line
        QPen slashPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(slashPen);
        p.drawLine(QPointF(size * 0.20, size * 0.15), QPointF(size * 0.80, size * 0.85));

        return QIcon(pixmap);
    }

    static QIcon iconPause(const QColor &color = QColor("#cccccc"), int size = 24)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(color));

        p.drawRoundedRect(QRectF(size * 0.24, size * 0.2, size * 0.20, size * 0.6), 2, 2);
        p.drawRoundedRect(QRectF(size * 0.56, size * 0.2, size * 0.20, size * 0.6), 2, 2);

        return QIcon(pixmap);
    }

    static QIcon iconPlay(const QColor &color = QColor("#cccccc"), int size = 24)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(color));

        QPainterPath path;
        path.moveTo(size * 0.28, size * 0.20);
        path.lineTo(size * 0.80, size * 0.50);
        path.lineTo(size * 0.28, size * 0.80);
        path.closeSubpath();
        p.drawPath(path);

        return QIcon(pixmap);
    }

    static QIcon iconRefresh(const QColor &color = QColor("#cccccc"), int size = 24)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        QRectF rect(size * 0.2, size * 0.2, size * 0.6, size * 0.6);
        p.drawArc(rect, 45 * 16, 270 * 16);

        // Arrow head
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(color));
        QPainterPath arrow;
        arrow.moveTo(size * 0.72, size * 0.18);
        arrow.lineTo(size * 0.88, size * 0.35);
        arrow.lineTo(size * 0.60, size * 0.38);
        arrow.closeSubpath();
        p.drawPath(arrow);

        return QIcon(pixmap);
    }

    static QIcon iconFolder(const QColor &color = QColor("#cccccc"), int size = 24)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QBrush(QColor(color.red(), color.green(), color.blue(), 40)));

        QPainterPath path;
        path.moveTo(size * 0.15, size * 0.30);
        path.lineTo(size * 0.40, size * 0.30);
        path.lineTo(size * 0.50, size * 0.40);
        path.lineTo(size * 0.85, size * 0.40);
        path.lineTo(size * 0.85, size * 0.78);
        path.lineTo(size * 0.15, size * 0.78);
        path.closeSubpath();
        p.drawPath(path);

        return QIcon(pixmap);
    }

    static QIcon iconClear(const QColor &color = QColor("#cccccc"), int size = 24)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        // Angled eraser body
        QPainterPath path;
        path.moveTo(size * 0.35, size * 0.18);
        path.lineTo(size * 0.78, size * 0.61);
        path.lineTo(size * 0.58, size * 0.81);
        path.lineTo(size * 0.15, size * 0.38);
        path.closeSubpath();
        p.drawPath(path);

        // Division line on eraser
        p.drawLine(QPointF(size * 0.45, size * 0.28), QPointF(size * 0.25, size * 0.48));
        // Base sweep underline
        p.drawLine(QPointF(size * 0.48, size * 0.84), QPointF(size * 0.88, size * 0.84));

        return QIcon(pixmap);
    }

    static QIcon iconSettings(const QColor &color = QColor("#cccccc"), int size = 24)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        p.drawEllipse(QRectF(size * 0.32, size * 0.32, size * 0.36, size * 0.36));

        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(size * 0.5, size * 0.5);
            p.rotate(i * 45);
            p.fillRect(QRectF(-size * 0.07, -size * 0.44, size * 0.14, size * 0.16), QBrush(color));
            p.restore();
        }

        return QIcon(pixmap);
    }

    static QPixmap statusDot(const QColor &color, int size = 16)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        qreal center = size / 2.0;
        qreal coreRadius = (size * 0.25);
        qreal haloRadius = (size * 0.40);

        // Soft outer halo with safety margin
        QColor halo = color;
        halo.setAlpha(50);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(QPointF(center, center), haloRadius, haloRadius);

        // Core solid circle
        p.setBrush(color);
        p.drawEllipse(QPointF(center, center), coreRadius, coreRadius);

        return pixmap;
    }
};
