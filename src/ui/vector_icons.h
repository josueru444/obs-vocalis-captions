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

    static QIcon iconRefresh(const QColor &color = QColor("#e0e0e0"), int size = 24)
    {
        // Render at higher resolution for crisp scaling
        int renderSize = size < 32 ? size * 2 : size;
        QPixmap pixmap(renderSize, renderSize);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        qreal s = renderSize;
        qreal penWidth = qMax(1.8, s * 0.09);
        QPen pen(color, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        qreal margin = s * 0.20;
        QRectF arcRect(margin, margin, s - 2 * margin, s - 2 * margin);
        // Draw 270 degree circular arc (from 50 deg to 320 deg)
        p.drawArc(arcRect, 50 * 16, 270 * 16);

        // Draw distinct triangular arrowhead at the top-right
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(color));
        QPainterPath arrow;
        qreal tipX = s * 0.76;
        qreal tipY = s * 0.28;
        qreal arrowSize = s * 0.22;
        arrow.moveTo(tipX, tipY);
        arrow.lineTo(tipX - arrowSize * 1.1, tipY - arrowSize * 0.6);
        arrow.lineTo(tipX - arrowSize * 0.5, tipY + arrowSize * 0.7);
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

    static QIcon iconEye(const QColor &color = QColor("#cccccc"), int size = 24)
    {
        int renderSize = size < 32 ? size * 2 : size;
        QPixmap pixmap(renderSize, renderSize);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        qreal s = renderSize;
        QPen pen(color, qMax(1.6, s * 0.08), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        // Eye shape
        QPainterPath eyePath;
        eyePath.moveTo(s * 0.15, s * 0.50);
        eyePath.cubicTo(s * 0.32, s * 0.22, s * 0.68, s * 0.22, s * 0.85, s * 0.50);
        eyePath.cubicTo(s * 0.68, s * 0.78, s * 0.32, s * 0.78, s * 0.15, s * 0.50);
        p.drawPath(eyePath);

        // Pupil
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(color));
        p.drawEllipse(QPointF(s * 0.50, s * 0.50), s * 0.14, s * 0.14);

        return QIcon(pixmap);
    }

    static QIcon iconEyeOff(const QColor &color = QColor("#cccccc"), int size = 24)
    {
        int renderSize = size < 32 ? size * 2 : size;
        QPixmap pixmap(renderSize, renderSize);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        qreal s = renderSize;
        QPen pen(color, qMax(1.6, s * 0.08), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        // Eye shape
        QPainterPath eyePath;
        eyePath.moveTo(s * 0.15, s * 0.50);
        eyePath.cubicTo(s * 0.32, s * 0.22, s * 0.68, s * 0.22, s * 0.85, s * 0.50);
        eyePath.cubicTo(s * 0.68, s * 0.78, s * 0.32, s * 0.78, s * 0.15, s * 0.50);
        p.drawPath(eyePath);

        // Small Pupil outline
        p.drawEllipse(QPointF(s * 0.50, s * 0.50), s * 0.12, s * 0.12);

        // Diagonal slash line
        QPen slashPen(color, qMax(1.8, s * 0.09), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(slashPen);
        p.drawLine(QPointF(s * 0.20, s * 0.22), QPointF(s * 0.80, s * 0.78));

        return QIcon(pixmap);
    }

    static QIcon iconCheck(const QColor &color = QColor("#4CAF50"), int size = 24)
    {
        int renderSize = size < 32 ? size * 2 : size;
        QPixmap pixmap(renderSize, renderSize);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        qreal s = renderSize;
        QPen pen(color, qMax(2.0, s * 0.12), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        QPainterPath path;
        path.moveTo(s * 0.22, s * 0.52);
        path.lineTo(s * 0.42, s * 0.72);
        path.lineTo(s * 0.78, s * 0.28);
        p.drawPath(path);

        return QIcon(pixmap);
    }

    static QIcon iconDownload(const QColor &color = QColor("#5B9BD5"), int size = 24)
    {
        int renderSize = size < 32 ? size * 2 : size;
        QPixmap pixmap(renderSize, renderSize);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        qreal s = renderSize;
        QPen pen(color, qMax(1.8, s * 0.09), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        // Arrow shaft
        p.drawLine(QPointF(s * 0.50, s * 0.18), QPointF(s * 0.50, s * 0.58));
        // Arrow head
        QPainterPath head;
        head.moveTo(s * 0.30, s * 0.42);
        head.lineTo(s * 0.50, s * 0.62);
        head.lineTo(s * 0.70, s * 0.42);
        p.drawPath(head);
        // Base tray
        QPainterPath tray;
        tray.moveTo(s * 0.22, s * 0.70);
        tray.lineTo(s * 0.22, s * 0.82);
        tray.lineTo(s * 0.78, s * 0.82);
        tray.lineTo(s * 0.78, s * 0.70);
        p.drawPath(tray);

        return QIcon(pixmap);
    }

    static QIcon iconTrash(const QColor &color = QColor("#E06C75"), int size = 24)
    {
        int renderSize = size < 32 ? size * 2 : size;
        QPixmap pixmap(renderSize, renderSize);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);

        qreal s = renderSize;
        QPen pen(color, qMax(1.6, s * 0.08), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        // Lid handle
        p.drawLine(QPointF(s * 0.40, s * 0.18), QPointF(s * 0.60, s * 0.18));
        // Lid bar
        p.drawLine(QPointF(s * 0.20, s * 0.28), QPointF(s * 0.80, s * 0.28));
        // Bin body
        QPainterPath body;
        body.moveTo(s * 0.28, s * 0.32);
        body.lineTo(s * 0.32, s * 0.82);
        body.lineTo(s * 0.68, s * 0.82);
        body.lineTo(s * 0.72, s * 0.32);
        p.drawPath(body);
        // Vertical ribs inside bin
        p.drawLine(QPointF(s * 0.42, s * 0.42), QPointF(s * 0.42, s * 0.72));
        p.drawLine(QPointF(s * 0.58, s * 0.42), QPointF(s * 0.58, s * 0.72));

        return QIcon(pixmap);
    }
};
