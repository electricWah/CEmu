#ifndef MEMORYVISUALIZER_H
#define MEMORYVISUALIZER_H

#include "../dockedwidget.h"
#include "widgets/visualizerlcdwidget.h"

namespace KDDockWidgets
{
class DockWidgetBase;
}

#include <QtCore/QString>
#include <QtWidgets/QWidget>
QT_BEGIN_NAMESPACE
class QCloseEvent;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QRadioButton;
QT_END_NAMESPACE

class VisualizerWidget : public DockedWidget
{
    Q_OBJECT

public:
    explicit VisualizerWidget(CoreWindow *coreWindow, KDDockWidgets::DockWidgetBase *dock = nullptr);

    void setConfig(const QString &config);
    QString getConfig() const;
    void translate();
    void forceUpdate();

public slots:
    void lcdFrame();

private slots:
    void resetView();
    void showConfig();
    void showPresets();

signals:
    void configChanged(const QString &uniqueName, const QString &config);

private:
    void closeEvent(QCloseEvent *) override;

    void stringToView();
    void viewToString();

    QStringList mSetup;

    int mScale = 100;
    int mFps = 30;

    QLineEdit *mConfigStr;
    QGroupBox *mGroup;

    QPushButton *mBtnLcd;
    QPushButton *mBtnRefresh;
    QPushButton *mBtnConfig;

    VisualizerWidget *mPrev, *mNext;
    VisualizerLcdWidget *mLcd;
    VisualizerLcdWidgetConfig mLcdConfig;
};

#endif