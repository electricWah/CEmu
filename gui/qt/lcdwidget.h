#ifndef LCDWIDGET_H
#define LCDWIDGET_H

#include <QtWidgets/QWidget>
#include <QtCore/QTimer>
#include <chrono>

#include "../../core/lcd.h"

class LCDWidget : public QWidget {
  Q_OBJECT

public:
    explicit LCDWidget(QWidget *p = Q_NULLPTR);
    QImage getImage();
    void setup();
    double getFps();

public slots:
    void updateLcd();

protected:
    virtual void paintEvent(QPaintEvent*) Q_DECL_OVERRIDE;
    virtual void dropEvent(QDropEvent*) Q_DECL_OVERRIDE;
    virtual void dragEnterEvent(QDragEnterEvent*) Q_DECL_OVERRIDE;
    virtual void dragLeaveEvent(QDragLeaveEvent*) Q_DECL_OVERRIDE;
    virtual void dragMoveEvent(QDragMoveEvent*) Q_DECL_OVERRIDE;

signals:
    void sendROM(const QString& romPath);

private:
    enum lcd_side {
        LCD_LEFT=0,
        LCD_RIGHT
    };

    enum array_info {
        ARRAY_SIZE = 60
    };

    unsigned int sideDrag;
    bool drag = false;
    QRect left, right;
    QImage image;

    // for dragable roms
    QString dragROM;
    bool isSendingROM;

    unsigned int array[ARRAY_SIZE];
    int index = 0;
    double guiFps = 0;
};

#endif
